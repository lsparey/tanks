#!/usr/bin/env python3
"""Compare neutral ground before/after channel carving and plot cut depth (Pillow)."""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def render(before, after, output):
    def metadata(path):
        return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)

    old, new = metadata(before), metadata(after)
    for key in ("seed", "resolution", "world_size", "erosion_duration", "rain_duration", "spacing", "light"):
        if old[key] != new[key]:
            raise ValueError(f"Incompatible comparison: {key}")

    def asset(path, suffix):
        return Image.open(path.with_name(path.stem + suffix))

    side, margin = 480, 24
    board = Image.new("RGB", (3 * side + 4 * margin, side + 185), (246, 246, 242))
    pen = ImageDraw.Draw(board)
    pen.text((margin, 16), f"Channel carving v{new['channel_carving_version']} | seed {new['seed']} | physical terrain, no stream water mesh", fill=(20, 30, 35))
    pen.text((margin, 38), f"Full apron: {new['channel_changed_cells']} changed vertices; "
             f"{float(new['channel_removed_ground']):.2f} volume removed; maximum allowed cut {float(new['channel_maximum_cut']):.2f} world units.", fill=(50, 60, 65))
    images = [asset(before, "-neutral.ppm").convert("RGB"), asset(after, "-neutral.ppm").convert("RGB")]
    cuts = asset(after, "-channel-cut.pgm")
    protected = asset(after, "-channel-protected.pgm")
    heat = Image.new("RGB", cuts.size)
    depth, mask = cuts.load(), protected.load()
    heat.putdata([(35, 105, 105) if mask[x, y] else
                  (int(35 + 220 * depth[x, y] / 65535), int(35 + 95 * depth[x, y] / 65535), 30)
                  for y in range(cuts.height) for x in range(cuts.width)])
    apron, resolution = int(new["apron_cells"]), int(new["resolution"])
    images.append(heat.crop((apron, apron, apron + resolution, apron + resolution)))
    labels = ["Before: settled erosion terrain", "After: bounded channel excavation", "Actual cut depth; teal = protected basin/rim"]
    for i, (picture, label) in enumerate(zip(images, labels)):
        x = margin + i * (side + margin)
        pen.text((x, 70), label, fill=(20, 30, 35))
        board.paste(picture.resize((side, side), Image.Resampling.NEAREST if i == 2 else Image.Resampling.BILINEAR), (x, 94))
    pen.text((margin, side + 110), "Same crop, scale and neutral light. Ground normals/contact mesh rebuilt; no vertical exaggeration.", fill=(50, 60, 65))
    pen.text((margin, side + 132), "Removed soil/bedrock are accounted exports. Existing lake triangles and spill rims are preserved.", fill=(50, 60, 65))
    x0, y0 = 2 * side + 3 * margin, side + 114
    for x in range(180):
        t = x / 179
        pen.line((x0 + x, y0, x0 + x, y0 + 12), fill=(int(35 + 220 * t), int(35 + 95 * t), 30))
    pen.text((x0, y0 + 18), f"0                             Cut depth                              {float(new['channel_maximum_cut']):.2f}", fill=(50, 60, 65))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.before, args.after, args.output)
