"""Exercise view transitions with the local catalogs in a hidden OpenGL window."""

from collections import deque
from dataclasses import dataclass
import json
import os
from pathlib import Path
from queue import Empty, Queue
import shutil
import subprocess
import tempfile
from threading import Thread
from time import monotonic
from typing import Optional
import unicodedata
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ.get("ALIENORUM_REPLAY_BINARY")
PROBE_PREFIX = "VIEW_PROBE "
STAR, PLANET, MOON = 2, 3, 4


@dataclass(frozen=True)
class Target:
    query: str
    kind: int
    aliases: tuple = ()
    parent: tuple = ()
    candidate: Optional[bool] = None


TARGETS = (
    Target("Sun", STAR, ("Sol",)),
    Target("Earth", PLANET),
    Target("Neptune", PLANET),
    Target("beta Pic", STAR, ("Beta Pictoris", "bet Pic", "HD 39060")),
    Target("beta Pic b", PLANET),
    Target("beta Pic c", PLANET),
    Target("82 Eri", STAR, ("82 Eridani", "HD 20794")),
    Target("82 Eri b", PLANET),
    Target("82 Eri d", PLANET),
    Target("Tau Cet", STAR, ("Tau Ceti", "HD 10700")),
    Target("tau Cet f", PLANET),
    Target("Sirius", STAR, ("Sirius A",)),
    Target("WD 0642-166", STAR, ("Sirius B", "GJ 244 B", "HD 48915 B"), ("Sirius", "Sirius A")),
    Target("55 Cnc A", STAR, ("55 Cnc", "55 Cancri A", "Copernicus", "Rho 1 Cancri")),
    Target("55 Cnc B", STAR, ("55 Cancri B",)),
    Target("55 Cnc Ae", PLANET, ("Janssen",), ("55 Cnc", "55 Cnc A", "55 Cancri A", "Copernicus", "Rho 1 Cancri")),
    Target("55 Cnc Bb", PLANET, parent=("55 Cnc B", "55 Cancri B")),
    Target("47 UMa", STAR, ("47 Ursae Majoris", "Chalawan")),
    Target("47 UMa b", PLANET, ("Taphao Thong",)),
    Target("47 UMa c", PLANET, ("Taphao Kaew",)),
    Target("ups And", STAR, ("ups And A", "Upsilon Andromedae", "Upsilon Andromedae A", "Titawin")),
    Target("ups And Ab", PLANET, ("ups And b", "Saffar")),
    Target("ups And Ac", PLANET, ("ups And c", "Samh")),
    Target("HD 3167", STAR),
    Target("HD 3167 b", PLANET),
    Target("HD 3167 c", PLANET),
    Target("mu Ara", STAR, ("Mu Arae", "HD 160691", "GJ 691", "Cervantes")),
    Target("Quijote", PLANET, ("mu Ara b",)),
    Target("Dulcinea", PLANET, ("mu Ara c", "GJ 691 d", "GJ 691 c")),
    Target("GJ 486", STAR),
    Target("GJ 486 b", PLANET, candidate=False),
    Target("GJ 1153", STAR, ("HD 104067",)),
    Target("GJ 1153 b", PLANET, ("HD 104067 b",), candidate=False),
    Target("GJ 1153 d", PLANET, ("TOI-6713.01",)),
    Target("Proxima Centauri", STAR, ("Proxima Cen", "GJ 551",)),
    Target("Proxima Centauri b", PLANET),
    Target("Kepler-16", STAR, ("Kepler-16 A",)),
    Target("Kepler-16 b", PLANET, ("Kepler-16 (AB) b",)),
    Target("TRAPPIST-1", STAR),
    Target("TRAPPIST-1 b", PLANET, candidate=False),
    Target("TRAPPIST-1 e", PLANET),
    Target("L 98-59", STAR),
    Target("L 98-59 h", PLANET, candidate=True),
    Target("Altair", STAR),
    Target("Polaris", STAR, ("Polaris A", "Polaris Aa")),
) + tuple(Target(name, MOON) for name in (
    "Moon", "Phobos", "Deimos", "Io", "Europa", "Ganymede", "Callisto",
    "Enceladus", "Titan", "Iapetus", "Miranda", "Titania", "Triton", "Nereid", "Charon",
))

# The three successive map shortcuts select daylight, elevation and temperature.
VIEWS = (
    ("Spaceship", "&", 0),
    ("Horizon", "_", 1),
    ("Sky map", "\\", 3),
    ("System", "^v", 4),
    ("Model", ":", 7),
    ("Sunclock", "$", 2),
    ("Elevation", "$", 5),
    ("Thermal", "$", 6),
)


def name_key(name):
    return "".join(c for c in unicodedata.normalize("NFKD", name.casefold()) if c.isalnum())


def copy_runtime(root):
    for directory in ("assets", "catalogs", "ephemerides", "galaxies", "maps"):
        shutil.copytree(ROOT / directory, root / directory)
    for name in ("helios_system.json", "starname.dat", "consline.dat", "locales.json",
                 "exoname.dat", "object_name_pool.dat"):
        shutil.copyfile(ROOT / name, root / name)
    (root / "nonet").touch()
    (root / ".startup-complete").touch()


def read_lines(stream, output):
    for line in stream:
        output.put(line.rstrip())
    output.put(None)


@unittest.skipUnless(BINARY, "Set ALIENORUM_REPLAY_BINARY for desktop replay")
class ViewModeReplayTest(unittest.TestCase):
    def test_every_view_mode_completes_across_catalog_targets(self):
        binary = Path(BINARY).resolve(strict=True)
        commands = [str(binary), "nosat", "jd", "2460390.0", "F8", "viewprobe", "startup"]
        cases = []
        for target in TARGETS:
            commands.extend(("&", "go", target.query))
            for view, key, mode in VIEWS:
                tag = str(len(cases))
                commands.extend((key, "viewprobe", tag))
                cases.append((tag, target, view, 0 if target.kind == STAR and mode == 1 else mode))
        commands.extend(("&", "viewprobe", "finished"))

        with tempfile.TemporaryDirectory(prefix="alienorum-views-") as directory:
            root = Path(directory)
            copy_runtime(root)
            env = dict(os.environ, ALIENORUM_HOME=str(root), SDL_AUDIODRIVER="dummy")
            env.setdefault("LP_NUM_THREADS", "1")
            with subprocess.Popen(commands, cwd=root, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace") as app:
                output, recent = Queue(), deque(maxlen=20)
                reader = Thread(target=read_lines, args=(app.stdout, output), daemon=True)
                reader.start()

                def next_probe(tag, context, seconds):
                    deadline = monotonic() + seconds
                    while True:
                        remaining = deadline - monotonic()
                        if remaining <= 0:
                            self.fail(f"Timed out in {context}\n" + "\n".join(recent))
                        try:
                            line = output.get(timeout=remaining)
                        except Empty:
                            self.fail(f"Timed out in {context}\n" + "\n".join(recent))
                        if line is None:
                            self.fail(f"App exited before {context} (status {app.poll()})\n" + "\n".join(recent))
                        recent.append(line)
                        if line.startswith(PROBE_PREFIX):
                            frame = json.loads(line[len(PROBE_PREFIX):])
                            self.assertEqual(frame["tag"], tag, context)
                            self.assertTrue(frame["viewport_hidden"], context)
                            return frame

                try:
                    next_probe("startup", "catalog startup", 150)
                    counts = set()
                    saw_uncertain_system = False
                    for tag, target, view, expected_mode in cases:
                        context = f"{target.query} / {view}"
                        frame = next_probe(tag, context, 25)
                        with self.subTest(target=target.query, view=view):
                            self.assertEqual(frame.get("class"), target.kind, frame)
                            allowed_names = {name_key(name) for name in (target.query,) + target.aliases}
                            identifiers = {name_key(frame.get(field, "")) for field in ("name", "designation")}
                            self.assertTrue(identifiers & allowed_names, frame)
                            self.assertEqual(frame["mode"], expected_mode, frame)
                            if view == "System":
                                self.assertGreater(frame["system_members"], 0, frame)
                            if target.parent:
                                allowed_parents = {name_key(name) for name in target.parent}
                                self.assertIn(name_key(frame.get("parent", "")), allowed_parents, frame)
                            if target.candidate is not None:
                                self.assertEqual(frame.get("candidate"), target.candidate, frame)
                        if (confirmed := frame.get("confirmed_planets")) is not None:
                            counts.add(min(confirmed, 2))
                            saw_uncertain_system |= frame["uncertain_planets"] > 0
                    self.assertEqual(counts, {0, 1, 2})
                    self.assertTrue(saw_uncertain_system)
                    final_frame = next_probe("finished", "return to Spaceship after the last view", 25)
                    self.assertEqual(final_frame["mode"], 0, final_frame)
                    self.assertIsNone(app.poll(), "\n".join(recent))
                    print(f"Completed {len(cases)} view checks across {len(TARGETS)} targets.", flush=True)
                finally:
                    # The replay owns this disposable process and runtime directory.
                    if app.poll() is None:
                        app.kill()
                        app.wait(timeout=10)
                    reader.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
