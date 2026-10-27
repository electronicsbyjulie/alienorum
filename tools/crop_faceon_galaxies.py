#!/usr/bin/env python3
"""Losslessly trim tracked face-on JPEGs and retain their original sky framing."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--backup", type=Path, default=Path("/tmp/alienorum-faceon-originals"))
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    folder = Path("galaxies/faceon")
    manifest = folder / "framing.dat"
    if args.check:
        import shlex
        rows = [shlex.split(row) for row in manifest.read_text().splitlines() if row and not row.startswith("#")]
        for row in rows:
            name, original_w, original_h, left, top, width, height = row
            values = list(map(int, row[1:]))
            original_w, original_h, left, top, width, height = values
            with Image.open(folder / name) as image:
                assert image.size == (width, height), name
            assert 0 <= left < original_w and 0 <= top < original_h, name
            assert left+width <= original_w and top+height <= original_h, name
        print(f"Verified {len(rows)} cropped assets and their sky framing.")
        return
    if manifest.exists() and not args.source:
        raise SystemExit("Framing already exists; use --check to validate the prepared assets.")
    args.backup.mkdir(parents=True, exist_ok=True)
    tracked = subprocess.check_output(["git", "ls-files", "-z", str(folder)], text=True).split("\0")
    records = []
    changed = 0
    for filename in filter(None, tracked):
        path = Path(filename)
        if path.suffix.lower() not in (".jpg", ".jpeg"):
            continue
        input_path = args.source/path.name if args.source else path
        with Image.open(input_path) as image:
            original_w, original_h = image.size
            pixels = np.asarray(image.convert("RGB"))
            brightness = pixels.max(axis=2).astype(float)
            labels, count = ndimage.label(ndimage.gaussian_filter(brightness, 2) >= 4, np.ones((3, 3)))
            flux = np.bincount(labels.ravel(), weights=brightness.ravel())
            flux[0] = 0
            significant = flux >= max(1, flux.max()*0.005)
            visible = significant[labels]
            columns = np.flatnonzero(visible.sum(axis=0) >= max(3, original_h//200))
            rows = np.flatnonzero(visible.sum(axis=1) >= max(3, original_w//200))
            block_w = max(layer[1] for layer in image.layer)*8
            block_h = max(layer[2] for layer in image.layer)*8
        left, top, right, bottom = 0, 0, original_w, original_h
        if len(columns) and len(rows):
            left = max(0, int(columns[0])-2)//block_w*block_w
            top = max(0, int(rows[0])-2)//block_h*block_h
            right = min(original_w, ((int(columns[-1])+3+block_w-1)//block_w)*block_w)
            bottom = min(original_h, ((int(rows[-1])+3+block_h-1)//block_h)*block_h)
        width, height = right-left, bottom-top
        if input_path.resolve() != (args.backup/path.name).resolve():
            shutil.copy2(input_path, args.backup/path.name)
        if (left, top, width, height) != (0, 0, original_w, original_h):
            temporary = path.with_suffix(".crop-tmp")
            subprocess.run(["jpegtran", "-copy", "all", "-optimize", "-crop",
                            f"{width}x{height}+{left}+{top}", "-outfile", str(temporary), str(input_path)], check=True)
            with Image.open(temporary) as cropped:
                assert cropped.size == (width, height), path.name
            temporary.replace(path)
            changed += 1
        records.append(f"{json.dumps(path.name)} {original_w} {original_h} {left} {top} {width} {height}")
    manifest.write_text("# Original canvas and lossless crop for face-on galaxy sky placement.\n"
                        "# filename original_width original_height left top width height\n"+"\n".join(records)+"\n")
    print(f"Cropped {changed} of {len(records)} face-on JPEGs; all files have framing records.")


if __name__ == "__main__":
    main()
