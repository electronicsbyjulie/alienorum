import contextlib
import io
import tempfile
import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

import numpy as np
from PIL import Image

from prepare_solar_elevation import directions, fit_residual, prepare


class SolarElevationPreparationTest(unittest.TestCase):
    def test_geographic_grid_uses_pixel_centres_and_east_positive_longitudes(self):
        for width in (12, 24, 48):
            grid = directions(width, width // 2)
            np.testing.assert_allclose(np.linalg.norm(grid, axis=-1), 1, atol=1e-14)
            np.testing.assert_allclose(grid, -np.roll(grid[::-1], width // 2, axis=1), atol=1e-14)
            self.assertGreater(grid[0, 0, 2], .95)
            self.assertGreater(grid[width // 4, 3 * width // 4, 1], .9)

    def test_reference_ellipsoid_contributes_no_fictitious_relief(self):
        grid = directions(48, 24)
        for axes in ([500, 500, 500], [39000, 25000, 12000]):
            radii = 1 / np.linalg.norm(grid / axes, axis=-1)
            radii[2:4, 5:12] = np.nan
            remaining, fitted = fit_residual(radii, grid)
            np.testing.assert_allclose(fitted, axes, rtol=1e-10)
            self.assertLess(np.nanmax(abs(remaining)), 1e-7)
            np.testing.assert_array_equal(np.isnan(remaining), np.isnan(radii))

    def test_labelled_radius_samples_retain_the_same_mountain_after_row_reordering(self):
        with tempfile.TemporaryDirectory() as folder:
            args = SimpleNamespace(kind="radii", source=Path(folder) / "points.txt",
                                   output=Path(folder) / "terrain.png", width=48, west=False,
                                   quantum=.1, url="fixture", credit="fixture", license="fixture")
            samples = [[lat, lon, 20.5 if (lat, lon) == (30, 90) else 20]
                       for lat in range(-90, 91, 15) for lon in range(0, 360, 15)]
            def convert(rows):
                np.savetxt(args.source, rows)
                with contextlib.redirect_stdout(io.StringIO()):
                    prepare(args)
                with Image.open(args.output) as result:
                    return np.array(result)
            original = convert(samples)
            row, col = np.unravel_index(original.argmax(), original.shape)
            self.assertLess(abs(90 - (row + .5) * 7.5 - 30), 8)
            self.assertLess(abs(-180 + (col + .5) * 7.5 - 90), 8)
            np.testing.assert_array_equal(convert(list(reversed(samples))), original)


    def test_mars_cli_preserves_signed_metres_and_moves_the_seam_by_half_a_turn(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "mola.img"
            target = Path(folder) / "mars.png"
            heights = np.memmap(source, mode="w+", dtype=">i2", shape=(2880, 5760))
            peaks = {(0, 0): -32768, (2879, 5759): 32767, (123, 2879): -3456, (1300, 2880): 7890}
            for index, value in peaks.items():
                heights[index] = value
            heights.flush()
            del heights
            script = Path(__file__).with_name("prepare_mars_elevation.py")
            subprocess.run([sys.executable, str(script), str(source), str(target)], check=True, capture_output=True)
            with Image.open(target) as image:
                grid = np.asarray(image, dtype=np.int32) - 32768
                self.assertEqual(grid.shape, (2880, 5760))
                self.assertEqual(np.count_nonzero(grid), len(peaks))
                for (y, x), value in peaks.items():
                    self.assertEqual(grid[y, (x + 2880) % 5760], value)


if __name__ == "__main__":
    unittest.main()
