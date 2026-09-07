#!/usr/bin/env python3
"""Plot actual combined water faces and queried flow over final terrain (Pillow)."""
import argparse
import csv
import math
from pathlib import Path

from PIL import Image, ImageDraw


def render(source, output):
    metadata = dict(line.split("=", 1) for line in source.read_text().splitlines() if "=" in line)

    def rows(suffix):
        with source.with_name(source.stem + suffix).open() as file:
            return list(csv.DictReader(file))

    vertices = rows("-combined-water-vertices.csv")
    faces = []
    with source.with_name(source.stem + "-combined-water.obj").open() as file:
        for line in file:
            if line.startswith("f "):
                faces.append([int(word.split("/")[0]) - 1 for word in line.split()[1:]])
    samples = rows("-combined-water-samples.csv")
    nodes = rows("-stream-nodes.csv")
    world = float(metadata["world_size"])
    junctions = [n for n in nodes if int(n["incoming"]) > 1 and n["kind"] == "channel" and
                 abs(float(n["x"])) < world * .4 and abs(float(n["z"])) < world * .4]
    junction = max(junctions, key=lambda n: (int(n["incoming"]), float(n["discharge"]))) if junctions else None
    cx, cz = (float(junction["x"]), float(junction["z"])) if junction else (0, 0)
    span = min(world, 32)
    cx, cz = [max(-world / 2 + span / 2, min(world / 2 - span / 2, v)) for v in (cx, cz)]
    views = [(-world / 2, -world / 2, world), (cx - span / 2, cz - span / 2, span)]
    side, margin = 530, 24
    board = Image.new("RGB", (2 * side + 3 * margin, side + 190), (246, 246, 242))
    pen = ImageDraw.Draw(board)
    pen.text((margin, 16), f"Combined terrain water v{metadata['combined_water_version']} | seed {metadata['seed']} | actual clipped water mesh", fill=(20, 30, 35))
    pen.text((margin, 38), f"{metadata['combined_stream_triangles']} stream faces; {metadata['combined_lake_triangles']} flat lake faces; "
             "query arrows show direction, not speed.", fill=(50, 60, 65))
    ground = Image.open(source.with_name(source.stem + "-neutral.ppm")).convert("RGB")
    points = [(float(v["x"]), float(v["z"]), float(v["depth"])) for v in vertices]
    resolution = int(metadata["resolution"])
    for panel, (left, top, width) in enumerate(views):
        crop = ((left / world + .5) * ground.width, (top / world + .5) * ground.height,
                ((left + width) / world + .5) * ground.width, ((top + width) / world + .5) * ground.height)
        picture = ground.crop(crop).resize((side, side), Image.Resampling.BILINEAR)
        draw = ImageDraw.Draw(picture)

        def project(x, z):
            return ((x - left) / width * (side - 1), (z - top) / width * (side - 1))

        for face in faces:
            p = [points[i] for i in face]
            if max(v[0] for v in p) < left or min(v[0] for v in p) > left + width or \
               max(v[1] for v in p) < top or min(v[1] for v in p) > top + width:
                continue
            depth = min(sum(v[2] for v in p) / 3, 1)
            color = tuple(round(a + (b - a) * depth) for a, b in zip((85, 180, 185), (15, 65, 110)))
            draw.polygon([project(v[0], v[1]) for v in p], fill=color)
        step = max(1, round(width / world * resolution / 18))
        for index, sample in enumerate(samples):
            if index % resolution % step or index // resolution % step or sample["kind"] == "dry":
                continue
            x, z = float(sample["x"]), float(sample["z"])
            fx, fz = float(sample["flow_x"]), float(sample["flow_z"])
            if not (left < x < left + width and top < z < top + width) or math.hypot(fx, fz) < .5:
                continue
            a = project(x, z)
            b = (a[0] + fx * 9, a[1] + fz * 9)
            draw.line((a, b), fill=(235, 250, 250))
            draw.line((b, (b[0] - fx * 3 - fz * 2, b[1] - fz * 3 + fx * 2)), fill=(235, 250, 250))
            draw.line((b, (b[0] - fx * 3 + fz * 2, b[1] - fz * 3 - fx * 2)), fill=(235, 250, 250))
        x0 = margin + panel * (side + margin)
        board.paste(picture, (x0, 94))
        label = "Full playable crop" if panel == 0 else f"Junction detail: {width:.0f} world units across"
        pen.text((x0, 70), label, fill=(20, 30, 35))
    pen.text((margin, side + 110), "Faces meet actual terrain banks. Lake/stream overlaps are partitioned; no fixed-width ribbon or raised spill film.", fill=(50, 60, 65))
    pen.text((margin, side + 132), "Static reconstruction: broad water, grid-following channels and unresolved dry spill controls remain visible.", fill=(50, 60, 65))
    pen.text((margin, side + 154), "Depth colors: pale teal = shallow, dark blue = 1+ world units. Neutral light; no vertical exaggeration.", fill=(50, 60, 65))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.source, args.output)
