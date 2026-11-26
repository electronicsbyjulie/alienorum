"""Set ALIENORUM_REPLAY_BINARY to run the desktop application and inspect F12 captures."""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from statistics import median

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.environ.get("ALIENORUM_REPLAY_BINARY"), "Opt-in desktop replay")
class ApplicationReplayTest(unittest.TestCase):
    def test_greenwich_ground_is_brighter_at_noon_in_a_saved_horizon_snapshot(self):
        binary = Path(os.environ["ALIENORUM_REPLAY_BINARY"]).resolve(strict=True)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            # The app can refresh caches and user settings. Give it its own runtime tree.
            for folder in ("assets", "catalogs", "ephemerides", "galaxies", "maps"):
                shutil.copytree(ROOT / folder, root / folder)
            for source in ("starname.dat", "consline.dat", "locales.json", "exoname.dat", "object_name_pool.dat"):
                shutil.copyfile(ROOT / source, root / source)
            preferences = {"Latitude": 0, "Longitude": 0, "Timezone": 0, "Gamma": 1}
            (root / "user.json").write_text(json.dumps(preferences))
            folder = root / "snapshots"
            env = dict(os.environ, ALIENORUM_HOME=str(root), SDL_AUDIODRIVER="dummy")

            def green_at(jd):
                command = [str(binary), "noexo", "nosat", "jd", str(jd), "go", "Earth", "hz",
                           "az", "0", "alt", "-55", "F12", "^w"]
                run = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=180)
                self.assertEqual(run.returncode, 0, run.stderr[-2000:])
                captures = list(folder.glob("*.png"))
                self.assertEqual(len(captures), 1, run.stdout[-2000:])
                with Image.open(captures[0]) as picture:
                    width, height = picture.size
                    ground = picture.crop((width // 4, height * 2 // 3, width * 3 // 4, height * 5 // 6))
                    green = median(ground.convert("RGB").getchannel("G").getdata())
                captures[0].unlink()
                return green

            night = green_at(2460389.5)
            noon = green_at(2460390.0)
            self.assertGreater(noon, night + 10, (noon, night))


if __name__ == "__main__":
    unittest.main()
