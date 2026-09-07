#!/usr/bin/env python3
"""Sequential in-game benchmark using existing profiler and screenshot exit.

No desktop input is injected. Screenshot frames are excluded by the in-game
profiler. CPU values describe its final 240-frame window, GPU values its final
asynchronous EMA. Repeated processes regenerate terrain; OS/driver caches are
left intact. Linux RSS/DRM sampling is optional and never inferred when missing.
"""

import argparse
import csv
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time


def read(path):
    try:
        return Path(path).read_text()
    except OSError:
        return ""


def desktop_locked():
    """Best-effort check of this user's graphical sessions; never unlock them."""
    try:
        sessions = subprocess.run(["loginctl", "list-sessions", "--no-legend"], capture_output=True, text=True, timeout=2)
        for line in sessions.stdout.splitlines():
            fields = line.split()
            if len(fields) < 2 or fields[1] != str(os.getuid()):
                continue
            status = subprocess.run(["loginctl", "show-session", fields[0], "-p", "Type", "-p", "LockedHint"],
                                    capture_output=True, text=True, timeout=2).stdout
            if "LockedHint=yes" in status and ("Type=wayland" in status or "Type=x11" in status):
                return True
    except (OSError, subprocess.TimeoutExpired, AttributeError):
        pass  # Non-systemd/non-Linux hosts still have the run timeout.
    return False


def memory(pid):
    status = read(f"/proc/{pid}/status")
    match = re.search(r"^VmHWM:\s+(\d+) kB", status, re.M)
    rss = int(match[1]) if match else None
    clients = {}
    try:
        files = list(Path(f"/proc/{pid}/fdinfo").iterdir())
    except OSError:
        files = []
    for path in files:
        info = read(path)
        client = re.search(r"^drm-client-id:\s*(\S+)", info, re.M)
        device = re.search(r"^drm-pdev:\s*(\S+)", info, re.M)
        if not client or not device:
            continue
        # Duplicated file descriptors must not double-count the same client.
        key = (device[1], client[1])
        clients[key] = {name: int(value) for name, value in re.findall(
            r"^(drm-(?:total|resident|shared|active|purgeable)-[\w-]+):\s*(\d+) KiB", info, re.M)}
    totals = {}
    for (device, _), fields in clients.items():
        for field, value in fields.items():
            name = f"{device}/{field}_kib"
            totals[name] = totals.get(name, 0) + value
    return rss, totals


def parse_report(log):
    def last(pattern):
        matches = list(re.finditer(pattern, log))
        if not matches:
            raise ValueError(f"missing benchmark result: {pattern}")
        return matches[-1]

    result = {}
    result["loading_ms"] = float(last(r"Loading complete: ([\d.]+) ms")[1])
    terrain = last(r"Terrain ([\w-]+) v\d+: generation ([\d.]+) ms, upload/BLAS ([\d.]+) ms")
    result.update(preset=terrain[1], generation_ms=float(terrain[2]), terrain_upload_ms=float(terrain[3]))
    landforms = list(re.finditer(r"Terrain landform v(\d+): requested (\S+), selected (\S+)", log))
    # Keep columns consistent for paired legacy runs and older reports that
    # predate explicit landform selection. An absent label is not inferred.
    result.update(landform_version=0, landform_requested="", landform_resolved="")
    if landforms:
        form = landforms[-1]
        result.update(landform_version=int(form[1]), landform_requested=form[2], landform_resolved=form[3])
    result["placement_ms"] = float(last(r"Scenery placement/verification: ([\d.]+) ms")[1])
    frame = last(r"PERF \(last (\d+) frames, (\d+)x(\d+)\) frame ms: avg ([\d.]+), p95 ([\d.]+), p99 ([\d.]+), worst ([\d.]+)")
    result.update(samples=int(frame[1]), width=int(frame[2]), height=int(frame[3]))
    for i, field in enumerate(("frame_mean_ms", "frame_p95_ms", "frame_p99_ms", "frame_worst_ms"), 4):
        result[field] = float(frame[i])
    # Parse only the final report, never mix CPU windows with earlier reports.
    final = log[frame.start():]
    for prefix, label in (("CPU ms", "cpu"), ("wait/API ms", "wait"), ("GPU ms (async EMA)", "gpu"), ("counts (window mean)", "count")):
        line = re.search(re.escape(prefix) + r": ([^\n]+)", final)
        if not line:
            raise ValueError(f"missing {prefix}")
        for name, value in re.findall(r"([^,]+?) ([\d.]+)(?:,|$)", line[1]):
            field = re.sub(r"\W+", "_", name.strip()).lower()
            result[f"{label}_{field}" + ("" if label == "count" else "_ms")] = float(value)
    if result["samples"] != 240:
        raise ValueError("final profiler window has fewer than 240 valid frames")
    scenery = re.search(r"scenery GPU ms \(async EMA, subsets\): ([^\n]+)", final)
    if scenery:
        parts = re.findall(r"([^,]+?) ([\d.]+)(?:,|$)", scenery[1])
        if len(parts) != 4 or abs(sum(float(value) for _, value in parts) - result["gpu_scenery_ms"]) > .05:
            raise ValueError("scenery GPU subphases disagree with their containing interval")
        for name, value in parts:
            result["gpu_scenery_" + name.strip().replace(" ", "_") + "_ms"] = float(value)
    if "Saved screenshot to " not in log:
        raise ValueError("run ended before requested screenshot")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("out/runtime-release/tanks"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", type=int, nargs="+", default=[7331, 0, 42])
    parser.add_argument("--presets", choices=["legacy", "drained-valley"], nargs="+", default=["legacy", "drained-valley"])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--view", choices=["landscape", "terrain", "tank-side", "water"], default="landscape")
    parser.add_argument("--weapon-preview", action="store_true", help="existing one-shot muzzle/explosion preview; not sustained combat")
    parser.add_argument("--landform", choices=["mixed", "valley", "hills", "ridges", "plain", "basin"],
                        help="override the new terrain's landform; legacy comparisons retain legacy geometry")
    parser.add_argument("--terrain-resolution", type=int, choices=[257, 513],
                        help="override the new terrain grid; legacy comparisons retain legacy geometry")
    parser.add_argument("--terrain-refinement", choices=["on", "off", "2x", "4x"], help="257 erosion grid; on/2x yields 513 final samples, 4x yields 1025")
    parser.add_argument("--frames", type=int, default=420)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if args.repeats < 1 or not 301 <= args.frames <= 0x7fffffff or not math.isfinite(args.timeout) or args.timeout <= 0 or any(not 0 <= seed <= 0xffffffff for seed in args.seeds):
        parser.error("positive finite repeats/timeout, 301..2147483647 frames and uint32 seeds required")
    if args.terrain_refinement in ["on", "2x", "4x"] and args.terrain_resolution == 513:
        parser.error("refinement requires the 257 erosion grid")
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    if any(args.output.iterdir()):
        parser.error("output directory must be empty; prior benchmark evidence is never overwritten")
    metadata = dict(timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    platform=platform.platform(), options=vars(args).copy())
    metadata["options"] = {key: str(value) if isinstance(value, Path) else value for key, value in metadata["options"].items()}
    metadata["power"] = {str(p): read(p).strip() for p in [Path("/sys/firmware/acpi/platform_profile"),
        Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"), *Path("/sys/class/power_supply").glob("*/online")]}
    metadata["display"] = {key: os.environ.get(key) for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE")}
    cache = read(binary.parent / "CMakeCache.txt")
    build_type = re.search(r"^CMAKE_BUILD_TYPE:[^=]+=(.*)$", cache, re.M)
    metadata["build_type"] = build_type[1] if build_type else "unknown"
    # Shaders are loaded from ASSET_ROOT at runtime, independently of the
    # executable. Record their compiled bytes so shader-only A/B runs can be
    # distinguished even when the binary hash stays the same.
    source_root = re.search(r"^CMAKE_HOME_DIRECTORY:[^=]+=(.*)$", cache, re.M)
    metadata["shader_sha256"] = None
    if source_root:
        shader_dir = Path(source_root[1]) / "shaders"
        metadata["shader_sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                     for path in sorted(shader_dir.glob("*.spv"))}
    metadata["cpu_models"] = sorted(set(re.findall(r"^model name\s*:\s*(.+)$", read("/proc/cpuinfo"), re.M)))
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    rows = []
    for seed in args.seeds:
        for repeat in range(args.repeats):
            # Alternate pair order to reduce systematic warmup/order bias.
            presets = args.presets if repeat % 2 == 0 else list(reversed(args.presets))
            for preset in presets:
                if desktop_locked():
                    raise RuntimeError("desktop is locked; unlock it before benchmarking")
                name = f"{preset}-{seed}-{args.view}-{repeat + 1}"
                screenshot = (args.output / f"{name}.png").resolve()
                log_path = args.output / f"{name}.log"
                if log_path.exists():
                    raise FileExistsError(f"refusing to overwrite prior run: {log_path}")
                command = [str(binary), "--terrain", preset, "--seed", str(seed), "--view", args.view,
                           "--profile", "--screenshot", str(screenshot), "--screenshot-frame", str(args.frames)]
                if args.weapon_preview:
                    command.append("--weapon-preview")
                if args.landform and preset != "legacy":
                    command.extend(["--landform", args.landform])
                if args.terrain_resolution and preset != "legacy":
                    command.extend(["--terrain-resolution", str(args.terrain_resolution)])
                if args.terrain_refinement and preset != "legacy":
                    command.extend(["--terrain-refinement", args.terrain_refinement])
                print("Starting", name, flush=True)
                started = time.monotonic()
                peak_rss = None
                peak_drm = {}
                samples = 0
                error = None
                next_lock_check = started + 5
                with log_path.open("w") as output:
                    process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
                    try:
                        while process.poll() is None:
                            if time.monotonic() - started > args.timeout:
                                raise TimeoutError(f"benchmark exceeded {args.timeout}s: {name}")
                            if time.monotonic() >= next_lock_check:
                                if desktop_locked():
                                    raise RuntimeError("desktop locked during benchmark; run is incomplete")
                                next_lock_check = time.monotonic() + 5
                            rss, drm = memory(process.pid)
                            samples += 1
                            if rss is not None:
                                peak_rss = max(peak_rss or 0, rss)
                            for field, value in drm.items():
                                peak_drm[field] = max(peak_drm.get(field, 0), value)
                            time.sleep(.1)
                    except Exception as exc:
                        error = str(exc)
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                memory_result = dict(peak_rss_kib=peak_rss, peak_drm_kib=peak_drm, polling_samples=samples,
                                     process_elapsed_s=time.monotonic() - started, command=command, exit_code=process.returncode,
                                     error=error)
                (args.output / f"{name}.json").write_text(json.dumps(memory_result, indent=2) + "\n")
                if error:
                    raise RuntimeError(f"{error}; see {log_path}")
                if process.returncode:
                    raise RuntimeError(f"benchmark exited {process.returncode}; see {log_path}")
                row = dict(seed=seed, repeat=repeat + 1, view=args.view, **parse_report(log_path.read_text()), peak_rss_kib=peak_rss)
                if row["preset"] != preset:
                    raise ValueError("runtime selected the wrong preset")
                rows.append(row)
                with (args.output / "summary.csv").open("w", newline="") as output:
                    writer = csv.DictWriter(output, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
                print(f"Finished {name}: loading {row['loading_ms']:.1f} ms, frame {row['frame_mean_ms']:.2f} ms, GPU {row['gpu_total_ms']:.2f} ms", flush=True)


if __name__ == "__main__":
    main()
