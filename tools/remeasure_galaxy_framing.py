#!/usr/bin/env python3
"""Fit edited face-on assets to centered square sky canvases without changing their pixels."""
import argparse
import json
import math
import shlex
import subprocess
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--center", action="append", default=[], metavar="IMAGE:X:Y")
    args = parser.parse_args()
    centers = {}
    for value in args.center:
        name, x, y = value.rsplit(":", 2)
        centers[name] = (float(x), float(y))
    folder = Path("galaxies/faceon")
    manifest = folder / "framing.dat"
    edited = set(subprocess.check_output(
        ["git", "diff", "--name-only", "HEAD", "--", str(folder)], text=True).splitlines())
    result = ["# Sky reference canvas and current face-on image placement, in pixel units.",
              "# filename canvas_width canvas_height left top image_width image_height",
              "# Edited assets use a square canvas to preserve their current pixel aspect and centering."]
    count = 0
    for row in manifest.read_text().splitlines():
        if not row.strip() or row.startswith("#"):
            continue
        name, canvas_w, canvas_h, left, top, width, height = shlex.split(row)
        path = folder/name
        with Image.open(path) as image:
            measured = image.size
        if path.as_posix() in edited or measured != (int(width), int(height)) or name in centers:
            width, height = measured
            x, y = centers.get(name, (width/2, height/2))
            if not (0 <= x <= width and 0 <= y <= height):
                raise SystemExit(f"Center is outside {name}.")
            side = math.ceil(2*max(x, width-x, y, height-y))
            canvas_w = canvas_h = side
            left, top = side/2-x, side/2-y
            count += 1
            print(f"{name}: {width}x{height}; canvas {side}x{side}; offset {left:.4f},{top:.4f}")
        values = [float(v) for v in (canvas_w, canvas_h, left, top, width, height)]
        result.append(json.dumps(name)+" "+" ".join(format(v, ".12g") for v in values))
    manifest.write_text("\n".join(result)+"\n")
    print(f"Re-measured {count} image frames.")


if __name__ == "__main__":
    main()
