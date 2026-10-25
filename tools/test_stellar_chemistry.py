import copy
import json
import shlex
import unittest
from pathlib import Path
import subprocess
import sys
import tempfile

from import_stellar_abundances import bake_catalog, catalog_index, galah_measurements, match_record
from calibrate_planet_classifier import calibrate, fold, sample
from test_stellar_metallicity import star


class AbundanceImportTest(unittest.TestCase):
    def test_shared_binary_and_conflicting_identifiers_are_rejected(self):
        index = catalog_index([star("A", 12), star("B", 12), star("C", 13)])
        self.assertIsNone(match_record(dict(hd=12), index))
        self.assertIsNone(match_record(dict(name="C", hd=99), index))
        self.assertEqual(match_record(dict(hd=13), index), 2)

    def test_refresh_removes_stale_bundle_and_preserves_other_sources(self):
        old = dict(Hypatia=dict(elements={}), Other=dict(elements={}))
        line = star("A", 12).rstrip() + " CHEM1 " + json.dumps(json.dumps(old)) + "\n"
        table = dict(source="Hypatia", solar_scale="asplund09", solar={}, reference="fixture", rows=[])
        lines, _ = bake_catalog([line], [table], catalog_index([line]))
        fields = shlex.split(lines[0][655:])
        self.assertEqual(json.loads(fields[fields.index("CHEM1") + 1]), dict(Other=dict(elements={})))
        repeated, _ = bake_catalog(lines.copy(), [table], catalog_index(lines))
        self.assertEqual(lines, repeated)

    def test_galah_quality_flags_and_missing_errors(self):
        row = {key: 0 for key in ("flagSp", "flagSpFit", "flagRed", "f_[Fe/H]", "f_[Mg/Fe]")}
        row.update({"SNRpxCCD3": 50, "[Fe/H]": -0.1, "[Mg/Fe]": 0.2, "e_[Mg/Fe]": 0.03})
        result = galah_measurements(row)
        self.assertIsNone(result["Fe"]["error"])
        self.assertEqual(result["Mg"]["ratio"], "[Mg/Fe]")
        row["f_[Mg/Fe]"] = 1
        self.assertNotIn("Mg", galah_measurements(row))
        row["flagSp"] = 1
        self.assertEqual(galah_measurements(row), {})


class CalibrationSelectionTest(unittest.TestCase):
    def setUp(self):
        self.row = dict(pl_name="Fixture b", hostname="Fixture", pl_bmasse=3, pl_rade=1.3,
            st_met=0, st_teff=5700, st_logg=4.4, pl_bmassprov="Mass", st_metratio="[Fe/H]",
            pl_bmasseerr1=0.3, pl_bmasseerr2=-0.3, pl_radeerr1=0.05, pl_radeerr2=-0.05,
            st_meterr1=0.05, st_meterr2=-0.05, pl_insol=1)

    def test_unknown_errors_limits_and_estimates_are_excluded(self):
        self.assertIsNotNone(sample(self.row))
        for key, value in (("pl_bmasseerr1", None), ("pl_bmassprov", "Msini"),
                           ("pl_radelim", 1), ("st_metratio", "[M/H]"),
                           ("st_meterr1", 0.3), ("pl_rade", 1.7)):
            row = copy.deepcopy(self.row)
            row[key] = value
            self.assertIsNone(sample(row), key)

    def test_host_folds_are_shared_and_small_samples_disable_model(self):
        first = sample(self.row)
        self.row["pl_name"] = "Fixture c"
        second = sample(self.row)
        self.assertEqual(first["fold"], second["fold"])
        self.assertEqual(first["fold"], fold("Fixture"))
        self.assertFalse(calibrate([first, second])["enabled"])

    def test_luminosity_fallback_requires_real_orbital_distance(self):
        self.row.update(pl_insol=None, st_lum=0, pl_orbsmax=2)
        self.assertEqual(sample(self.row)["flux"], 0.25)
        self.row["pl_orbsmax"] = None
        self.assertIsNone(sample(self.row))

    def test_rebuilding_shipped_model_preserves_balanced_specification(self):
        path = Path("catalogs/NASA/classification_benchmark.json")
        if not path.exists():
            self.skipTest("Local calibration snapshot is absent")
        rows = [selected for row in json.loads(path.read_text())["rows"]
            if (selected := sample(row)) is not None]
        rebuilt = calibrate(rows)
        shipped = json.loads(Path("catalogs/NASA/classification_model.json").read_text())
        self.assertTrue(rebuilt["enabled"])
        self.assertEqual(rebuilt["schema"], 2)
        self.assertEqual(rebuilt["training"], "class_balanced_logistic")
        self.assertEqual(rebuilt["regularization"], [0, 1.5, 1.5, 7.5])
        self.assertNotIn("improvement_95_percent_host_bootstrap", rebuilt)
        for key in ("metallicity_coefficients", "baseline_coefficients", "validation_results"):
            self.assertEqual(rebuilt[key], shipped[key])

    def test_invalid_refresh_preserves_installed_model(self):
        script = Path(__file__).with_name("calibrate_planet_classifier.py").resolve()
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "catalogs/NASA"
            folder.mkdir(parents=True)
            (folder / "classification_benchmark.json").write_text(json.dumps(dict(rows=[])))
            model = folder / "classification_model.json"
            original = "retained model\n"
            model.write_text(original)
            result = subprocess.run([sys.executable, str(script)], cwd=temporary, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("preserving the installed model", result.stderr)
            self.assertEqual(model.read_text(), original)


if __name__ == "__main__":
    unittest.main()
