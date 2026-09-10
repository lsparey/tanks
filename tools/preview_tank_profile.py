#!/usr/bin/env python3
"""Orthographic mesh check in the supplied reference's 600x190 coordinates.

Standard-library-only software preview: no terrain, perspective or camouflage
to disguise silhouette errors. This inspects the authoring mesh; in-game
captures are still required to check the imported asset and actual materials.
"""
import argparse
import math
import struct
import zlib
from pathlib import Path

from generate_tank_model import PROFILE_SCALE, build


def render(output, view="side", clay=False):
    scale = 2
    width, height = 600*scale, 210*scale
    if view in ("front","rear"):
        width, height = 520, 470
    elif view == "top":
        width, height = 1200, 480
    elif view in ("iso-front", "iso-rear"):
        width, height = 1280, 900
    pixels = bytearray([245, 246, 244] * width * height)
    depth = [-math.inf] * (width * height)
    colours = {"Base": (105, 114, 82), "Turret": (105, 114, 82),
               "HullFittings": (105,114,82), "TurretDark": (45,49,43),
               "Tracks": (48, 51, 46), "Barrel": (65, 71, 54)}
    if clay:
        colours = {name: ((93, 102, 112) if name in ("Tracks", "TurretDark")
                         else (155, 166, 178)) for name in colours}
    def project(p):
        x,y,z = p
        if view == "front":
            return width/2+x*195,height-35-y*195,z
        if view == "rear":
            return width/2-x*195,height-35-y*195,-z
        if view == "top":
            return (341-z/PROFILE_SCALE)*scale,height/2+x*180,y
        if view in ("iso-front", "iso-rear"):
            direction = 1 if view == "iso-front" else -1
            horizontal = .8*x-direction*.6*z
            distance = direction*.6*x+.8*z
            return (width/2+horizontal*190, 565+(-.82*y+direction*.57*distance)*190,
                    .57*y+direction*.82*distance)
        return (341-z/PROFILE_SCALE)*scale,(191-y/PROFILE_SCALE)*scale,x
    light = {"side":(.8,.6,0),"front":(-.3,.6,.74),
             "rear":(.3,.6,-.74),"top":(.3,.88,.37),
             "iso-front":(.4,.82,.4),"iso-rear":(.4,.82,-.4)}[view]
    for _, material, vertices, faces in build().objects:
        projected = [project(p) for p in vertices]
        for face in faces:
            for corner in range(1,len(face)-1):
                ids = (face[0],face[corner],face[corner+1])
                a,b,c = (projected[i] for i in ids)
                area = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
                if abs(area) < 1e-8:
                    continue
                # Illuminate actual face normals, with generous diffuse fill.
                A,B,C = (vertices[i] for i in ids)
                u,v = ([B[i]-A[i] for i in range(3)], [C[i]-A[i] for i in range(3)])
                n = (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
                length = math.sqrt(sum(t*t for t in n))
                brightness = .48+.52*max(0,sum(n[i]*light[i] for i in range(3))/length)
                colour = bytes(round(channel*brightness) for channel in colours[material])
                for y in range(max(0,math.floor(min(a[1],b[1],c[1]))),
                               min(height,math.ceil(max(a[1],b[1],c[1])))):
                    for x in range(max(0,math.floor(min(a[0],b[0],c[0]))),
                                   min(width,math.ceil(max(a[0],b[0],c[0])))):
                        px,py = x+.5-a[0],y+.5-a[1]
                        wb = (px*(c[1]-a[1])-py*(c[0]-a[0]))/area
                        wc = ((b[0]-a[0])*py-(b[1]-a[1])*px)/area
                        wa = 1-wb-wc
                        if min(wa,wb,wc) < -1e-7:
                            continue
                        z = wa*a[2]+wb*b[2]+wc*c[2]
                        index = y*width+x
                        if z > depth[index]:
                            depth[index] = z
                            pixels[index*3:index*3+3] = colour
    # Small depth steps get a subdued inspection outline. This makes cap/hub
    # seams legible in flat shading; it is not the runtime material treatment.
    for y in range(1,height-1):
        for x in range(1,width-1):
            i = y*width+x
            if not math.isfinite(depth[i]):
                continue
            if any(math.isfinite(depth[j]) and depth[i]-depth[j] > .014
                   for j in (i-1,i+1,i-width,i+width)):
                for channel in range(3):
                    pixels[i*3+channel] = round(pixels[i*3+channel]*.70)
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
    rows = b"".join(b"\0"+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes(b"\x89PNG\r\n\x1a\n" +
                       chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,2,0,0,0)) +
                       chunk(b"IDAT",zlib.compress(rows)) + chunk(b"IEND",b""))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path)
    parser.add_argument("--view",choices=("side","front","rear","top","iso-front","iso-rear"),default="side")
    parser.add_argument("--clay",action="store_true",help="Neutral grey inspection materials")
    args = parser.parse_args()
    render(args.output,args.view,args.clay)
