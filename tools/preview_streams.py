#!/usr/bin/env python3
"""Plot stream design guides and a longitudinal profile (requires Pillow).

These are candidate stream widths/levels, not rendered water or excavated beds.
The profile prioritizes a reach with insufficient depth, otherwise the longest.
"""
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

    nodes = rows("-stream-nodes.csv")
    reaches = defaultdict(list)
    for row in rows("-stream-reaches.csv"):
        reaches[int(row["reach"])].append((int(row["node"]), float(row["distance"])))
    world = float(metadata["world_size"])
    side, margin = 500, 24
    board = Image.new("RGB", (2 * side + 3 * margin, side + 180), (246, 246, 242))
    draw = ImageDraw.Draw(board)
    draw.text((margin, 14), f"Stream network v{metadata['stream_version']} | seed {metadata['seed']} | DESIGN GUIDES, not water geometry", fill=(20, 30, 35))
    draw.text((margin, 36), f"{metadata['stream_reaches']} reaches; {metadata['stream_confluences']} junctions; {metadata['stream_deficient_nodes']} full-domain nodes lack requested depth.", fill=(50, 60, 65))
    base = Image.open(source.with_name(source.stem + "-neutral.ppm")).convert("RGB")
    plan = base.resize((side, side), Image.Resampling.BILINEAR)
    pen = ImageDraw.Draw(plan)

    def point(node):
        return ((float(node["x"]) / world + .5) * (side - 1), (float(node["z"]) / world + .5) * (side - 1))

    lake_vertices = []
    with source.with_name(source.stem + "-lake-water.obj").open() as file:
        for line in file:
            words = line.split()
            if words and words[0] == "v":
                lake_vertices.append(point({"x": words[1], "z": words[3]}))
            elif words and words[0] == "f":
                pen.polygon([lake_vertices[int(word.split("//")[0]) - 1] for word in words[1:]], fill=(25, 85, 85))
    for node in nodes:
        target = int(node["downstream"])
        if target < 0:
            continue
        width = max(1, round(float(node["width"]) / world * (side - 1)))
        pen.line((point(node), point(nodes[target])), fill=(25, 120, 185), width=width)
    for node in nodes:
        p = point(node)
        if float(node["depth_deficit"]) > 1e-5:
            pen.ellipse((p[0] - 2, p[1] - 2, p[0] + 2, p[1] + 2), fill=(215, 85, 20))
        elif int(node["incoming"]) > 1:
            pen.ellipse((p[0] - 2, p[1] - 2, p[0] + 2, p[1] + 2), fill=(160, 20, 90))
    board.paste(plan, (margin, 84))
    draw.text((margin, 62), "Candidate centreline widths over final ground (playable crop)", fill=(20, 30, 35))
    draw.text((margin, side + 100), "Blue: stream guides. Magenta: junction. Orange: depth shortfall.", fill=(50, 60, 65))
    draw.text((margin, side + 122), "Dark teal: lake mesh. Guides continue into the apron; no ground edits.", fill=(50, 60, 65))

    x0, y0 = 2 * margin + side + 48, 116
    width, height = side - 68, side - 84
    if reaches:
        chosen = max(reaches, key=lambda key: (max(float(nodes[i]["depth_deficit"]) for i, _ in reaches[key]), reaches[key][-1][1]))
        reach = reaches[chosen]
        length = reach[-1][1]
        ground = [float(nodes[i]["ground"]) for i, _ in reach]
        level = [float(nodes[i]["water_level"]) for i, _ in reach]
        target = [float(nodes[i]["water_level"]) - float(nodes[i]["requested_depth"]) for i, _ in reach]
        lo, hi = min(ground + target), max(level + ground)
        padding = max((hi - lo) * .1, .05)
        lo, hi = lo - padding, hi + padding
        project = lambda distance, h: (x0 + distance / max(length, 1e-12) * width, y0 + (hi - h) / (hi - lo) * height)
        for k in range(5):
            elevation = lo + (hi - lo) * k / 4
            y = project(0, elevation)[1]
            draw.line((x0, y, x0 + width, y), fill=(210, 215, 210))
            draw.text((x0 - 46, y - 5), f"{elevation:.2f}", fill=(55, 60, 60))
        for values, color in [(ground, (95, 80, 55)), (level, (25, 120, 185)), (target, (215, 85, 20))]:
            draw.line([project(d, h) for (_, d), h in zip(reach, values)], fill=color, width=2)
        draw.rectangle((x0, y0, x0 + width, y0 + height), outline=(100, 110, 100))
        start, end = nodes[reach[0][0]], nodes[reach[-1][0]]
        draw.text((2 * margin + side, 62), f"Reach {chosen}: {start['kind']} to {end['kind']}", fill=(20, 30, 35))
        draw.text((x0, y0 + height + 12), f"0                         Distance downstream (world units)                    {length:.1f}", fill=(50, 60, 65))
        draw.text((2 * margin + side, side + 100), "Brown: ground. Blue: water-level guide. Orange: desired bed level.", fill=(50, 60, 65))
        draw.text((2 * margin + side, side + 122), "Elevation in world units; independent axis scales. No cut is approved.", fill=(50, 60, 65))
    else:
        draw.text((x0, y0), "No stream exceeds the discharge threshold.", fill=(50, 60, 65))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="stream-enabled terrain_probe metadata TXT")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.source, args.output)
