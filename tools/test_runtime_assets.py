import json
import shlex
import unittest
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


class RuntimeAssetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        lines = (ROOT / "package/runtime_files.txt").read_text().splitlines()
        cls.payload = {line for line in lines if line and not line.startswith("#")}

    def test_each_local_catalog_texture_and_band_is_available_in_the_installer(self):
        planets = json.loads((ROOT / "catalogs/planets.json").read_text())
        galaxies = json.loads((ROOT / "catalogs/galaxy_properties.json").read_text())
        fields = ("SurfMap", "NightMap", "CloudMap", "BumpMap", "RingColorMap", "RingTranspMap")
        paths = {body[key] for body in planets for key in fields if isinstance(body.get(key), str)}
        paths.update(galaxy[key] for galaxy in galaxies for key in ("internal_texture", "band_catalog")
                     if key in galaxy)
        for path in sorted(paths):
            if path.startswith(("https://", "http://")):
                continue
            with self.subTest(path=path):
                self.assertIn(path, self.payload)
                self.assertTrue((ROOT / path).is_file())

    def test_shipped_galaxy_photographs_fit_their_declared_canvas_without_rescaling(self):
        folder = Path("galaxies/faceon")
        rows = (ROOT / folder / "framing.dat").read_text().splitlines()
        entries = set()
        for row in rows:
            cells = shlex.split(row, comments=True)
            if not cells:
                continue
            name = cells[0]
            with self.subTest(name=name):
                self.assertNotIn(name, entries)
                entries.add(name)
                canvas_w, canvas_h, left, top, width, height = map(float, cells[1:])
                self.assertGreaterEqual(min(left, top), 0)
                self.assertLessEqual(left + width, canvas_w)
                self.assertLessEqual(top + height, canvas_h)
                self.assertIn((folder / name).as_posix(), self.payload)
                with Image.open(ROOT / folder / name) as photo:
                    self.assertEqual(photo.size, (width, height))
        self.assertTrue(entries)

    def test_windows_payload_has_no_case_collisions_or_escaping_paths(self):
        destinations = {}
        for name in sorted(self.payload):
            with self.subTest(path=name):
                relative = Path(name)
                self.assertFalse(relative.is_absolute())
                self.assertNotIn("..", relative.parts)
                key = name.casefold().replace("\\", "/")
                self.assertNotIn(key, destinations, destinations.get(key))
                destinations[key] = name


if __name__ == "__main__":
    unittest.main()
