#!/usr/bin/env python3
"""Render terrain_probe's drainage CSV as an analysis board (requires Pillow).

Example: python3 tools/preview_drainage.py out/drained-valley/\
drained-valley-v1-seed-7331-257.txt out/drained-valley/seed-7331-drainage.png
This is an offline field visualization, not persistent water or a game view.
"""
import argparse
import colorsys
import csv
import math
from pathlib import Path

from PIL import Image, ImageDraw


def render(source, output):
    metadata = dict(line.split("=", 1) for line in source.read_text().splitlines() if "=" in line)
    with source.with_name(source.stem + "-drainage.csv").open() as file:
        rows = list(csv.DictReader(file))
    n = int(metadata["domain_resolution"])
    apron = int(metadata["apron_cells"])
    if len(rows) != n * n:
        raise ValueError("drainage CSV dimensions disagree with metadata")
    heights = [float(row["ground"]) for row in rows]
    spill = [float(row["spill_elevation"]) - h for row, h in zip(rows, heights)]
    runoff = [float(row["runoff"]) for row in rows]
    labels = [int(row["basin"]) for row in rows]
    spacing = float(metadata["domain_world_size"]) / (n - 1)
    shades = []
    for z in range(n):
        for x in range(n):
            xa, xb = max(x - 1, 0), min(x + 1, n - 1)
            za, zb = max(z - 1, 0), min(z + 1, n - 1)
            dx = (heights[z * n + xb] - heights[z * n + xa]) / ((xb - xa) * spacing)
            dz = (heights[zb * n + x] - heights[za * n + x]) / ((zb - za) * spacing)
            light = (.6 * dx + 1 + .4 * dz) / math.sqrt(1.52 * (1 + dx * dx + dz * dz))
            shades.append(round(255 * (.2 + .7 * max(light, 0))))
    max_spill, max_runoff = max(spill), max(runoff)

    def pixels(panel):
        for i, shade in enumerate(shades):
            grey = (shade, shade, shade)
            color, amount = grey, 0
            if panel == 1:
                amount = math.log1p(runoff[i]) / max(math.log1p(max_runoff), 1e-12)
                color = (25, 155, 235)
            elif panel == 2 and spill[i] > 0:
                amount = .3 + .7 * spill[i] / max(max_spill, 1e-12)
                color = (240, 80, 20)
            elif panel == 3 and labels[i] >= 0:
                color = tuple(round(c * 255) for c in colorsys.hsv_to_rgb((labels[i] * .618 + .1) % 1, .8, .95))
                amount = .9
            yield tuple(round(a * (1 - amount) + b * amount) for a, b in zip(grey, color))

    titles = ["Final ground: neutral hillshade", "Potential runoff: logarithmic colour scale",
              "External spill depth: orange, linear scale", "Depression components: categorical colours"]
    captions = ["No materials, water surface or vertical exaggeration.",
                f"Grey to blue: 0 to {max_runoff:.3f} volume/time; assumes overflow.",
                f"0 to {max_spill:.3f} world units; NOT permanent water depth.",
                f"{metadata['basin_count']} full-domain components; red crosses mark spill exits."]
    side, margin = 440, 22
    board = Image.new("RGB", (2 * side + 3 * margin, 2 * (side + 66) + 96), (246, 246, 242))
    draw = ImageDraw.Draw(board)
    draw.text((margin, 14), f"{metadata['generator']} v{metadata['version']} | seed {metadata['seed']} | final drainage analysis", fill=(20, 30, 35))
    draw.text((margin, 34), "Full apron domain; black rectangle marks playable crop. Left = -X; top = -Z. No lake/stream selection.", fill=(50, 60, 65))
    for panel in range(4):
        x = margin + (panel % 2) * (side + margin)
        y = 70 + (panel // 2) * (side + 66)
        field = Image.new("RGB", (n, n))
        field.putdata(list(pixels(panel)))
        board.paste(field.resize((side, side), Image.Resampling.NEAREST), (x, y + 24))
        lo, hi = (apron + .5) * side / n, (n - apron - .5) * side / n
        draw.rectangle((x + lo, y + 24 + lo, x + hi, y + 24 + hi), outline=(25, 25, 25))
        if panel == 3:
            with source.with_name(source.stem + "-basins.csv").open() as file:
                for basin in csv.DictReader(file):
                    i = int(basin["spill_to"])
                    if i < 0:
                        continue
                    px, py = x + (i % n + .5) * side / n, y + 24 + (i // n + .5) * side / n
                    draw.line((px - 3, py, px + 3, py), fill=(180, 0, 0), width=2)
                    draw.line((px, py - 3, px, py + 3), fill=(180, 0, 0), width=2)
        draw.text((x, y), titles[panel], fill=(20, 30, 35))
        draw.text((x, y + side + 34), captions[panel], fill=(50, 60, 65))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="metadata TXT exported by terrain_probe")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.source, args.output)
