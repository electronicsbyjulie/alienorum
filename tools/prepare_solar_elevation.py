#!/usr/bin/env python3
"""Convert numeric planetary grids to calibrated 16-bit terrain PNGs.

Dependencies: numpy, Pillow, scipy; raster inputs also use rasterio.
Inputs retain their source units until the explicit scale/reference conversion.
Shape maps store deviations from a least-squares ellipsoid, avoiding duplicate
triaxial shape in the renderer. Their longitude convention is an explicit option.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, PngImagePlugin
from scipy.ndimage import distance_transform_edt, map_coordinates
from scipy.spatial import cKDTree


def directions(width, height):
    latitude = np.radians(90 - (np.arange(height) + .5) * 180 / height)
    longitude = np.radians(-180 + (np.arange(width) + .5) * 360 / width)
    latitude, longitude = np.meshgrid(latitude, longitude, indexing="ij")
    return np.stack((np.cos(latitude) * np.cos(longitude),
                     np.cos(latitude) * np.sin(longitude), np.sin(latitude)), axis=-1)


def fit_residual(radii, unit):
    valid = np.isfinite(radii)
    design = unit[valid] ** 2
    weights = np.sqrt(np.maximum(.001, np.hypot(unit[valid, 0], unit[valid, 1])))
    coefficients = np.linalg.lstsq(design * weights[:, None], weights / radii[valid] ** 2, rcond=None)[0]
    if np.any(coefficients <= 0):
        raise ValueError("Shape does not admit a positive reference ellipsoid")
    reference = 1 / np.sqrt(np.sum(unit ** 2 * coefficients, axis=-1))
    return radii - reference, (1 / np.sqrt(coefficients)).tolist()


def prepare(args):
    details = {}
    axes = None
    if args.kind == "raw-moon":
        grid = np.fromfile(args.source, dtype="<i2").reshape(2880, 5760).astype(float) * .5
        grid = np.roll(grid, 2880, axis=1)
    elif args.kind == "earth-subset":
        grid = np.load(args.source).astype(float)
        # NOAA subset starts at source cell 0 with stride 4; align output cell centres.
        grid = .625 * grid + .375 * np.roll(grid, -1, axis=1)
        grid[:-1] = .625 * grid[:-1] + .375 * grid[1:]
        grid = grid[::-1]
    elif args.kind == "icq":
        xyz = np.loadtxt(args.source, skiprows=1) * 1000
        radii = np.linalg.norm(xyz, axis=1)
        unit = xyz / radii[:, None]
        if args.west:
            unit[:, 1] *= -1
        residual, axes = fit_residual(radii, unit)
        target = directions(args.width, args.width // 2)
        distances, indices = cKDTree(unit).query(target.reshape(-1, 3), k=4, workers=2)
        weights = 1 / np.maximum(distances, 1e-12) ** 2
        grid = (np.sum(weights * residual[indices], axis=1) / weights.sum(axis=1)).reshape(target.shape[:2])
    elif args.kind == "radii":
        data = np.loadtxt(args.source)
        latitudes, longitudes = np.unique(data[:, 0]), np.unique(data[:, 1])
        radii = data[:, 2].reshape(len(latitudes), len(longitudes)) * 1000
        lat, lon = np.meshgrid(np.radians(latitudes), np.radians(longitudes), indexing="ij")
        unit = np.stack((np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)), axis=-1)
        residual, axes = fit_residual(radii, unit)
        width = args.width
        target_lat = 90 - (np.arange(width // 2) + .5) * 360 / width
        target_lon = -180 + (np.arange(width) + .5) * 360 / width
        if args.west:
            target_lon = -target_lon
        x = np.mod(target_lon, 360) / (longitudes[1] - longitudes[0])
        y = (target_lat - latitudes[0]) / (latitudes[1] - latitudes[0])
        yy, xx = np.meshgrid(y, x, indexing="ij")
        grid = map_coordinates(residual, [yy, xx], order=1, mode="nearest")
    else:
        import rasterio
        from rasterio.warp import reproject, Resampling
        from affine import Affine
        with rasterio.open(args.source) as dataset:
            projection = dataset.crs.to_dict()
            radius = float(projection.get("R", projection.get("a", 1)))
            width = args.width or dataset.width
            height = width // 2
            target = Affine(2 * np.pi * radius / width, 0, -np.pi * radius,
                            0, -np.pi * radius / height, np.pi * radius / 2)
            grid = np.full((height, width), np.nan, dtype=np.float32)
            reproject(source=rasterio.band(dataset, 1), destination=grid,
                      src_transform=dataset.transform, src_crs=dataset.crs,
                      src_nodata=dataset.nodata, dst_transform=target, dst_crs=dataset.crs,
                      dst_nodata=np.nan, resampling=Resampling.bilinear, num_threads=2)
            grid = grid * dataset.scales[0] + dataset.offsets[0]
            if abs(float(projection.get("lon_0", 0)) - 180) < .001:
                grid = np.roll(grid, width // 2, axis=1)
            if args.radius_reference:
                grid += args.radius_reference
                grid, axes = fit_residual(grid, directions(width, height))
            details["source_scale"] = dataset.scales[0]
    valid = np.isfinite(grid)
    details["coverage_fraction"] = float(valid.mean())
    if not valid.all():
        nearest = distance_transform_edt(~valid, return_distances=False, return_indices=True)
        grid = grid[tuple(nearest)]
        details["gap_fill"] = "nearest available elevation; inferred cells remain approximate"
    quantum = args.quantum
    while max(abs(grid.min()), abs(grid.max())) / quantum > 32767:
        quantum *= 2
    encoded = np.rint(grid / quantum).astype(np.int32) + 32768
    if encoded.min() < 0 or encoded.max() > 65535:
        raise ValueError("Elevation quantization would clip the source")
    details.update(source_url=args.url, credit=args.credit, license=args.license,
                   source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),
                   metres_per_code=quantum, minimum_m=float(grid.min()), maximum_m=float(grid.max()),
                   reference_ellipsoid_axes_m=axes,
                   BumpHeight=65535 * quantum, BumpOffset=-.5 * quantum,
                   coordinates="pixel centres, north first, east longitude -180 to +180")
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text("Alienorum Elevation Provenance", json.dumps(details, sort_keys=True))
    Image.fromarray(encoded.astype(np.uint16)).save(args.output, pnginfo=metadata)
    print(args.output, json.dumps(details, sort_keys=True))
    return details


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=["raw-moon", "earth-subset", "icq", "radii", "raster"])
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, default=0)
    parser.add_argument("--quantum", type=float, default=1)
    parser.add_argument("--radius-reference", type=float, default=0)
    parser.add_argument("--west", action="store_true")
    parser.add_argument("--url", required=True)
    parser.add_argument("--credit", required=True)
    parser.add_argument("--license", required=True)
    prepare(parser.parse_args())
