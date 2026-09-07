#!/usr/bin/env python3
"""Render terrain_probe's OBJ to a neutral orthographic PNG, using only stdlib.

This is an offline shape check of the actual mesh, with a depth buffer and
interpolated vertex lighting. It does not replace Vulkan or gameplay checks.
All axes use the same scale: no vertical exaggeration, materials, AO or water.
"""
import argparse
import math
import struct
import zlib
from pathlib import Path


def render(source, output, azimuth, elevation, width=960, height=640, frame=None):
    vertices, normals, faces = [], [], []
    with source.open() as obj:
        for line in obj:
            values = line.split()
            if not values:
                continue
            if values[0] == "v":
                vertices.append(tuple(map(float, values[1:4])))
            elif values[0] == "vn":
                normals.append(tuple(map(float, values[1:4])))
            elif values[0] == "f":
                if len(values) != 4:
                    raise ValueError("expected triangles from terrain_probe")
                corners = [value.split("//") for value in values[1:]]
                faces.append(tuple((int(v) - 1, int(n) - 1) for v, n in corners))
    if not vertices or not normals or not faces:
        raise ValueError("expected an OBJ with vertices, normals and triangles")

    az, el = math.radians(azimuth), math.radians(elevation)
    right = (math.sin(az), 0, -math.cos(az))
    up = (-math.sin(el) * math.cos(az), math.cos(el), -math.sin(el) * math.sin(az))
    toward = (math.cos(el) * math.cos(az), math.sin(el), math.cos(el) * math.sin(az))

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    projected = [(dot(p, right), dot(p, up), dot(p, toward)) for p in vertices]
    if frame is None:
        xmin, xmax = min(p[0] for p in projected), max(p[0] for p in projected)
        ymin, ymax = min(p[1] for p in projected), max(p[1] for p in projected)
    else:
        # Shared projected bounds keep comparisons at exactly the same scale
        # and position even when the two surfaces have different height ranges.
        xmin, xmax, ymin, ymax = frame
        if not all(math.isfinite(v) for v in frame) or xmin >= xmax or ymin >= ymax:
            raise ValueError("expected finite, increasing projected frame bounds")
    scale = min((width - 80) / (xmax - xmin), (height - 80) / (ymax - ymin))
    projected = [(width / 2 + (x - (xmin + xmax) / 2) * scale,
                  height / 2 - (y - (ymin + ymax) / 2) * scale, z) for x, y, z in projected]
    light = (-.6, 1, -.4)
    light = tuple(v / math.sqrt(1.52) for v in light)
    shades = [.2 + .7 * max(0, dot(n, light)) for n in normals]
    pixels = bytearray([244, 245, 242] * width * height)
    depth = [-math.inf] * (width * height)
    for face in faces:
        a, b, c = (projected[v] for v, _ in face)
        sa, sb, sc = (shades[n] for _, n in face)
        area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(area) < 1e-9:
            continue
        for y in range(max(0, math.floor(min(a[1], b[1], c[1]))), min(height, math.ceil(max(a[1], b[1], c[1])))):
            for x in range(max(0, math.floor(min(a[0], b[0], c[0]))), min(width, math.ceil(max(a[0], b[0], c[0])))):
                px, py = x + .5 - a[0], y + .5 - a[1]
                wb = (px * (c[1] - a[1]) - py * (c[0] - a[0])) / area
                wc = ((b[0] - a[0]) * py - (b[1] - a[1]) * px) / area
                wa = 1 - wb - wc
                if min(wa, wb, wc) < -1e-7:
                    continue
                z = wa * a[2] + wb * b[2] + wc * c[2]
                i = y * width + x
                if z > depth[i]:
                    depth[i] = z
                    shade = max(0, min(255, round((wa * sa + wb * sb + wc * sc) * 255)))
                    pixels[i * 3:i * 3 + 3] = bytes((shade, shade, shade))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"\x89PNG\r\n\x1a\n" +
                       chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                       chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    output.with_suffix(".txt").write_text(
        f"source={source}\nview=orthographic\nazimuth={azimuth}\nelevation={elevation}\n"
        f"size={width},{height}\npixels_per_world_unit={scale}\nvertical_exaggeration=1\n"
        f"projected_frame={xmin},{xmax},{ymin},{ymax}\n"
        "light=normalize(-0.6,1,-0.4); shade=0.2+0.7*max(dot(normal,light),0)\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="OBJ exported by terrain_probe")
    parser.add_argument("output", type=Path)
    parser.add_argument("--azimuth", type=float, default=45)
    parser.add_argument("--elevation", type=float, default=28)
    args = parser.parse_args()
    if not math.isfinite(args.azimuth) or not math.isfinite(args.elevation) or not 0 < args.elevation <= 90:
        parser.error("angles must be finite; elevation must be in (0,90]")
    render(args.source, args.output, args.azimuth, args.elevation)
