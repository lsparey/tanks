#!/usr/bin/env python3
"""Show final-ground spawn/route candidates and conservative footprint exclusions (Pillow)."""
import argparse
import csv
from pathlib import Path

from PIL import Image, ImageDraw


def render(metadata_path, output):
    metadata = dict(line.split("=", 1) for line in metadata_path.read_text().splitlines() if "=" in line)
    stem = metadata_path.with_suffix("")

    def rows(suffix):
        with Path(str(stem) + suffix).open() as file:
            return list(csv.DictReader(file))

    cells = rows("-playability.csv")
    route = rows("-route.csv")
    resolution = int(metadata["playability_resolution"])
    world = float(metadata["world_size"])
    selected = int(metadata.get("spawn_component", -1))
    side, margin, top = 600, 24, 112
    board = Image.new("RGB", (side * 2 + margin * 3, side + 258), (246, 246, 242))
    pen = ImageDraw.Draw(board)
    pen.text((margin, 16), f"Terrain playability v{metadata['playability_version']} | seed {metadata['seed']} | {metadata['playability_status']}", fill=(25, 35, 40))
    pen.text((margin, 39), f"Hull {float(metadata['playability_hull_width']):.3f} x {float(metadata['playability_hull_length']):.2f}; "
             f"clearance {float(metadata['playability_clearance']):.2f}; rotational envelope radius {float(metadata['playability_footprint_radius']):.2f} world units.", fill=(55, 65, 70))
    pen.text((margin, 61), "Static geometric candidates before scenery placement. No terrain grading; physics and runtime integration remain pending.", fill=(55, 65, 70))
    neutral = Image.open(Path(str(stem) + "-neutral.ppm")).convert("RGB").resize((side, side), Image.Resampling.BILINEAR)
    for panel in range(2):
        overlay = Image.new("RGBA", (resolution, resolution))
        pixels = []
        for cell in cells:
            flags = int(cell["terrain_flags"] if panel == 0 else cell["footprint_flags"])
            if flags & 16:
                color = (65, 72, 78, 150)
            elif flags & 1:
                color = (35, 110, 195, 195)
            elif flags & 2:
                color = (160, 75, 35, 180)
            elif flags & 8:
                color = (145, 55, 160, 180)
            elif panel == 0:
                color = (0, 0, 0, 0)
            elif flags & 4:
                color = (210, 160, 45, 140)
            elif int(cell["component"]) == selected:
                color = (55, 155, 75, 135)
            else:
                color = (65, 160, 145, 135)
            pixels.append(color)
        if len(pixels) != resolution * resolution:
            raise ValueError("Playability cell count disagrees with metadata")
        overlay.putdata(pixels)
        picture = Image.alpha_composite(neutral.convert("RGBA"), overlay.resize((side, side), Image.Resampling.NEAREST))
        draw = ImageDraw.Draw(picture)

        def at(x, z):
            return ((float(x) / world + .5) * side, (float(z) / world + .5) * side)

        boundary = float(metadata["playability_boundary_half_extent"])
        draw.rectangle((*at(-boundary, -boundary), *at(boundary, boundary)), outline=(245, 245, 240), width=1)
        if route:
            points = [at(row["x"], row["z"]) for row in route]
            draw.line(points, fill=(25, 30, 20), width=5)
            draw.line(points, fill=(255, 224, 70), width=3)
            x, z = float(metadata["spawn_x"]), float(metadata["spawn_z"])
            radius = float(metadata["playability_footprint_radius"])
            draw.rectangle((*at(x - radius, z - radius), *at(x + radius, z + radius)), outline=(255, 255, 255), width=2)
            px, pz = at(x, z)
            draw.ellipse((px - 4, pz - 4, px + 4, pz + 4), fill=(255, 255, 255), outline=(20, 30, 25))
            gx, gz = points[-1]
            draw.ellipse((gx - 4, gz - 4, gx + 4, gz + 4), fill=(255, 224, 70), outline=(20, 30, 25))
        left = margin + panel * (side + margin)
        pen.text((left, top - 23), "Final terrain with raw wet/steep quads" if panel == 0 else "Safe tank-centre cells after footprint expansion", fill=(25, 35, 40))
        board.paste(picture.convert("RGB"), (left, top))
    pen.text((margin, side + 135), "Blue: water exclusion. Brown: excessive route slope. Grey: boundary exclusion. Amber: route-safe, too steep to spawn.", fill=(45, 55, 60))
    pen.text((margin, side + 157), "Green: selected component's spawn-safe cells. Teal: other components. Yellow: connected route. White square: spawn envelope.", fill=(45, 55, 60))
    if route:
        pen.text((margin, side + 186), f"Connected centre area {float(metadata['spawn_connected_area']):.1f}; route length "
                 f"{float(metadata['playability_route_length']):.1f}; endpoint span {float(metadata['playability_route_span']):.1f} world units.", fill=(25, 35, 40))
    pen.text((margin, side + 211), "The route certifies a continuous corridor through accepted cells. It is not a dynamic driving simulation or a road carved into terrain.", fill=(45, 55, 60))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.metadata, args.output)
