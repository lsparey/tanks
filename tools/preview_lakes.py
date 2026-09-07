#!/usr/bin/env python3
"""Preview the exact clipped lake mesh and shoreline query field (Pillow).

Pass the TXT from terrain_probe --preset drained-valley --lake-water on.
This is an offline top-down CPU view, not a Vulkan/material validation.
"""
import argparse
import csv
from pathlib import Path

from PIL import Image, ImageDraw


def render(source, output):
    metadata = dict(line.split("=", 1) for line in source.read_text().splitlines() if "=" in line)
    n, world = int(metadata["resolution"]), float(metadata["world_size"])
    base = Image.open(source.with_name(source.stem + "-neutral.ppm")).convert("RGB")
    with source.with_name(source.stem + "-water-samples.csv").open() as file:
        samples = list(csv.DictReader(file))
    if len(samples) != n * n or base.size != (n, n):
        raise ValueError("water sample and image dimensions disagree")
    vertices, faces = [], []
    with source.with_name(source.stem + "-lake-water.obj").open() as file:
        for line in file:
            words = line.split()
            if words and words[0] == "v":
                vertices.append(tuple(map(float, words[1:4])))
            elif words and words[0] == "f":
                faces.append([int(word.split("//")[0]) - 1 for word in words[1:]])
    side, margin = 500, 22
    water = base.resize((side, side), Image.Resampling.BILINEAR)
    draw_water = ImageDraw.Draw(water)
    for face in faces:
        polygon = [((vertices[i][0] / world + .5) * (side - 1),
                    (vertices[i][2] / world + .5) * (side - 1)) for i in face]
        draw_water.polygon(polygon, fill=(28, 110, 150))
    distance = Image.new("RGB", (n, n))
    pixels = []
    for row in samples:
        if not row["shoreline_distance"]:
            pixels.append((222, 223, 220))
            continue
        value = float(row["shoreline_distance"])
        amount = min(abs(value) / 10, 1)
        far = (25, 100, 155) if value < 0 else (70, 120, 85)
        pixels.append(tuple(round(244 * (1 - amount) + c * amount) for c in far))
    distance.putdata(pixels)
    board = Image.new("RGB", (2 * side + 3 * margin, side + 160), (246, 246, 242))
    draw = ImageDraw.Draw(board)
    draw.text((margin, 14), f"Persistent lake prototype v{metadata['lake_water_version']} | seed {metadata['seed']} | playable crop", fill=(20, 30, 35))
    draw.text((margin, 36), f"{metadata['lakes_present']} supplied full-domain lakes; {metadata['water_triangles']} cropped water triangles. Streams are not generated.", fill=(50, 60, 65))
    for x, title, field in [(margin, "Exact lake mesh over final neutral ground", water),
                             (2 * margin + side, "Signed distance to full-apron shorelines", distance.resize((side, side), Image.Resampling.NEAREST))]:
        draw.text((x, 64), title, fill=(20, 30, 35))
        board.paste(field, (x, 86))
    draw.text((margin, side + 100), "Flat lake surface, triangle-clipped banks. Top = -Z; left = -X.", fill=(50, 60, 65))
    draw.text((2 * margin + side, side + 100), "Blue: wet; cream: bank; green: dry. Saturates at +/-10 units.", fill=(50, 60, 65))
    draw.text((margin, side + 126), "CPU diagnostic only. The crop edge is not treated as a shoreline.", fill=(50, 60, 65))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="lake-enabled metadata TXT")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.source, args.output)
