import io
import json
import math
from pathlib import Path
import tarfile
import tempfile
import unittest

from build_stellar_evolution_grid import build
from build_stellar_age_benchmarks import measurement, tex_rows


class StellarRecalibrationTest(unittest.TestCase):
    def test_grid_weights_integrate_mass_and_time_without_counting_rows(self):
        results = []
        for repeat in (1, 3):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / "models.txz"
                output = root / "grid.dat"
                records = []
                for age in (8, 9):
                    for mass in (1, 2, 3):
                        for _ in range(repeat):
                            fields = ["0"] * 25
                            fields[1:4] = [str(age), str(mass), str(mass)]
                            fields[7], fields[10], fields[24] = "0", "3.76", "0"
                            records.append(" ".join(fields))
                content = ("\n".join(records) + "\n").encode()
                with tarfile.open(archive, "w:xz") as stream:
                    info = tarfile.TarInfo("MIST_feh_p0.00_test.iso")
                    info.size = len(content)
                    stream.addfile(info, io.BytesIO(content))
                build(archive, output)
                rows = [list(map(float, line.split())) for line in output.read_text().splitlines()
                        if line and not line.startswith("#")]
                self.assertTrue(all(len(row) == 10 for row in rows))
                total = sum(math.exp(row[9]) for row in rows)
                expected = .9 * (.5 + 2 ** -2.35 + .5 * 3 ** -2.35)
                self.assertAlmostEqual(total, expected, places=7)
                results.append(rows)
        self.assertEqual(results[0], results[1])

    def test_latex_measurements_preserve_signs_and_uncertainties(self):
        self.assertEqual(measurement(r"$-0.30$ $\pm$ $0.12$"), (-.3, .12))
        self.assertEqual(list(tex_rows(r"A & 1 & 2 & 3 \\ B & 4 & 5 & 6 \\")),
                         [["A", "1", "2", "3"], ["B", "4", "5", "6"]])

    def test_frozen_reference_catalogue_keeps_sources_and_system_partitions(self):
        path = Path(__file__).resolve().parent.parent / "src/tests/data/stellar_age_benchmarks.json"
        catalog = json.loads(path.read_text())
        self.assertGreaterEqual(len(catalog["rows"]), 100)
        partitions = {}
        for row in catalog["rows"]:
            self.assertIn(row["source"], catalog["sources"])
            self.assertEqual(partitions.setdefault(row["system"], row["partition"]), row["partition"])


if __name__ == "__main__":
    unittest.main()
