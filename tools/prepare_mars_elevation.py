#!/usr/bin/env python3
"""Convert NASA MOLA MEGT90N000EB to a lossless, pixel-centred terrain PNG.

Source: https://pds-geosciences.wustl.edu/mgs/urn-nasa-pds-mgs_mola_topography_derived/meg016/megt90n000eb.img
Label: same directory, megt90n000eb.lbl (version 2.0, metres above the areoid).
Usage: python3 tools/prepare_mars_elevation.py INPUT.img OUTPUT.png
Requires numpy and Pillow. No contrast stretch, clipping, or vertical rescaling.
"""
import argparse
import hashlib
from pathlib import Path

import numpy as np
from PIL import Image, PngImagePlugin

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
raw = args.source.read_bytes()
if len(raw) != 5760 * 2880 * 2:
    raise ValueError("Expected the 5760 by 2880, big-endian signed 16-bit MOLA grid")
heights = np.frombuffer(raw, dtype=">i2").reshape(2880, 5760)
# PDS runs 0..360 east. Alienorum runs -180..180 east; both use north-first rows.
heights = np.roll(heights, 2880, axis=1)
encoded = (heights.astype(np.int32) + 32768).astype(np.uint16)
metadata = PngImagePlugin.PngInfo()
metadata.add_text("Source", "NASA MGS MOLA MEGT90N000EB version 2.0; 16 pixels/degree")
metadata.add_text("Source SHA256", hashlib.sha256(raw).hexdigest())
metadata.add_text("Elevation", "metres above areoid = unsigned 16-bit sample - 32768")
metadata.add_text("Coordinates", "pixel centres; north first; longitude -180 to +180 east")
Image.fromarray(encoded).save(args.output, pnginfo=metadata)
print(f"Saved {args.output}: {heights.min()} to {heights.max()} metres; SHA256 {hashlib.sha256(raw).hexdigest()}")
