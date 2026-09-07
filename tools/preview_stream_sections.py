#!/usr/bin/env python3
"""Render exact bank surveys and spill controls from terrain_probe (Pillow)."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageDraw


def render(source, output):
    metadata = dict(line.split("=", 1) for line in source.read_text().splitlines() if "=" in line)

    def rows(suffix):
        with source.with_name(source.stem + suffix).open() as file:
            return list(csv.DictReader(file))

    sections = rows("-stream-sections.csv")
    points = defaultdict(lambda: defaultdict(list))
    for row in rows("-section-points.csv"):
        points[int(row["section"])][row["side"]].append((float(row["distance"]), float(row["ground"])))
    world = float(metadata["world_size"])
    board = Image.new("RGB", (1100, 1100), (246, 246, 242))
    pen = ImageDraw.Draw(board)
    pen.text((24, 16), f"Stream cross-sections v{metadata['section_version']} | seed {metadata['seed']} | SURVEYS, not water meshes", fill=(20, 30, 35))
    pen.text((24, 38), f"Full apron: {metadata['bounded_sections']}/{metadata['sections']} sections have two banks; "
             f"{metadata['dry_sections']} dry sections; {metadata['spill_controls']} zero-depth control edges.", fill=(50, 60, 65))
    side = 490
    base = Image.open(source.with_name(source.stem + "-neutral.ppm")).convert("RGB").resize((side, side), Image.Resampling.BILINEAR)
    draw = ImageDraw.Draw(base)

    def project(x, z):
        return ((x / world + .5) * (side - 1), (z / world + .5) * (side - 1))

    for s in sections:
        if float(s["station"]) != .5:
            continue
        x, z = float(s["x"]), float(s["z"])
        dx, dz = float(s["left_x"]), float(s["left_z"])
        left, right = float(s["left_distance"]), float(s["right_distance"])
        if s["left_end"] == "dry-centre":
            p = project(x, z)
            draw.ellipse((p[0] - 2, p[1] - 2, p[0] + 2, p[1] + 2), fill=(200, 45, 45))
        else:
            color = (25, 125, 135) if s["bounded"] == "1" else (210, 140, 35)
            draw.line((project(x + dx * left, z + dz * left), project(x - dx * right, z - dz * right)), fill=color)
    board.paste(base, (24, 100))
    pen.text((24, 76), "Mid-edge bank surveys over final ground (playable crop)", fill=(20, 30, 35))
    pen.text((24, 605), "Teal: two banks. Amber: search/domain limit. Red: dry centre.", fill=(50, 60, 65))
    pen.text((24, 625), "Survey endpoints continue into the apron; crop edges are not banks.", fill=(50, 60, 65))

    visible = [s for s in sections if abs(float(s["x"])) < world / 2 and abs(float(s["z"])) < world / 2]
    bounded = [s for s in visible if s["bounded"] == "1"]
    limited = [s for s in visible if s["bounded"] == "0" and s["left_end"] != "dry-centre"]
    dry = [s for s in visible if s["left_end"] == "dry-centre"]
    choices = [
        (min(bounded, key=lambda s: abs(float(s["left_distance"]) + float(s["right_distance"]) - float(s["requested_width"]))) if bounded else None,
         "Bounded channel", 574, 100),
        (max(limited, key=lambda s: float(s["area"])) if limited else None, "Unclosed bank search", 24, 710),
        (dry[0] if dry else None, "Zero-depth control", 574, 710),
    ]
    for s, title, x0, y0 in choices:
        pen.text((x0, y0 - 24), title, fill=(20, 30, 35))
        if s is None:
            pen.text((x0, y0 + 20), "No example in the playable crop.", fill=(50, 60, 65))
            continue
        samples = points[int(s["section"])]
        profile = [(-d, h) for d, h in reversed(samples["left"])] + samples["right"][1:]
        water = float(s["water_level"])
        requested = float(s["requested_width"])
        extent = max(requested / 2, max(abs(d) for d, _ in profile), .1) * 1.1
        lo, hi = min(h for _, h in profile), water
        pad = max((hi - lo) * .15, .04)
        lo, hi = lo - pad, hi + pad
        width, height = 420, 235
        x0 += 48

        def xy(d, h):
            return (x0 + (d + extent) / (2 * extent) * width, y0 + (hi - h) / (hi - lo) * height)

        for k in range(5):
            h = lo + (hi - lo) * k / 4
            y = xy(0, h)[1]
            pen.line((x0, y, x0 + width, y), fill=(210, 215, 210))
            pen.text((x0 - 48, y - 5), f"{h:.2f}", fill=(55, 60, 60))
        if len(profile) > 1:
            polygon = [xy(d, h) for d, h in profile] + [xy(profile[-1][0], water), xy(profile[0][0], water)]
            pen.polygon(polygon, fill=(195, 222, 225))
            pen.line([xy(d, h) for d, h in profile], fill=(95, 80, 55), width=2)
            pen.line((xy(profile[0][0], water), xy(profile[-1][0], water)), fill=(25, 120, 185), width=2)
        else:
            p = xy(0, water)
            pen.ellipse((p[0] - 3, p[1] - 3, p[0] + 3, p[1] + 3), fill=(200, 45, 45))
        pen.line((xy(-requested / 2, hi - pad * .35), xy(requested / 2, hi - pad * .35)), fill=(210, 140, 35), width=3)
        pen.rectangle((x0, y0, x0 + width, y0 + height), outline=(100, 110, 100))
        pen.text((x0, y0 + height + 10), f"{-extent:.2f}          Distance across channel (world units)          {extent:.2f}", fill=(50, 60, 65))
        pen.text((x0 - 48, y0 + height + 34), f"Section {s['section']}: {s['left_end']} / {s['right_end']}", fill=(50, 60, 65))
        pen.text((x0 - 48, y0 + height + 54), f"Area {float(s['area']):.3f}; requested width {requested:.2f} (amber).", fill=(50, 60, 65))
        pen.text((x0 - 48, y0 + height + 74), "Brown: ground. Blue: level. Independent distance/elevation axes.", fill=(50, 60, 65))
    pen.text((24, 1068), "Three stations per terrain edge do not certify continuous banks or confluences. Ground and lake levels are unchanged.", fill=(50, 60, 65))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="section-enabled terrain_probe metadata TXT")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.source, args.output)
