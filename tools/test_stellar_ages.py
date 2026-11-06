import gzip
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from import_stellar_ages import build, number
from test_stellar_metallicity import star


class StellarAgeImportTest(unittest.TestCase):
    def test_uncertain_values_and_zero_are_available(self):
        self.assertEqual(number("8.7?"), 8.7)
        self.assertEqual(number(0), 0)
        for value in (None, "?", -1, "nan", "inf"):
            self.assertIsNone(number(value))

    def test_matching_retains_alternatives_bounds_and_late_loaded_hosts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {key: root / key for key in ("catalog", "crossmatch", "xhip", "gcs",
                                                "galah_ids", "galah_ages", "nasa")}
            paths["catalog"].write_text(star("Host", 12))
            paths["crossmatch"].write_text('{"rows":[]}')
            with gzip.open(paths["xhip"], "wt") as output:
                output.write("")
            for key, header in (("gcs", "HIP\tName\tageMP\tage16P\tage84P\tageMB\tage16B\tage84B"),
                                ("galah_ids", "GALAH\tGaiaEDR3"),
                                ("galah_ages", "GALAH\tAge50\tAge16\tAge84")):
                paths[key].write_text(header + "\nunits\n---\n")
            with paths["gcs"].open("a") as output:
                output.write("\tHD 12\t8\t6\t10\t9\t7\t11\n")
            records = [dict(hostname="Host", hd_name="HD 12", st_age=12, st_agelim=1),
                       dict(hostname="Other", hd_name="HD 13", st_age=5, st_ageerr1=1, st_ageerr2=-2)]
            paths["nasa"].write_text(json.dumps(records))
            result = build(SimpleNamespace(**paths))
            self.assertEqual(result["coverage"]["stars"], 2)
            host, other = result["rows"]
            self.assertEqual(host["value_gyr"], 8)
            self.assertEqual(len(host["alternatives"]), 3)
            self.assertEqual(host["alternatives"][-1]["limit"], 1)
            self.assertEqual(other["value_gyr"], 5)
            self.assertEqual(other["lower_gyr"], 3)
            self.assertEqual(other["upper_gyr"], 6)

            # New surveys use stable identifiers and retain every conflicting estimate.
            line = star("Host", 12)
            paths["catalog"].write_text(line[:120] + "1234  " + line[126:])
            paths["gaia_ages"] = root / "gaia"
            paths["gaia_ages"].write_text(json.dumps(dict(
                metadata=[dict(name=key) for key in ("hip", "source_id", "age_flame",
                                                     "age_flame_lower", "age_flame_upper")],
                data=[[1234, 1234567890123456789, "0?", 0, .1]])))
            paths["giant_ages"] = root / "giants"
            paths["giant_ages"].write_text(
                "#Table one\nName\tHD\tHIP\nunits\n---\nObject\t12\t1234\n"
                "#Table two\nName\tBAge\tDAge\tYAge\t<Age>\nunits\n---\n"
                "Object\t1.1\t2.2\t3.3\t2.2\n")
            result = build(SimpleNamespace(**paths))
            host = result["rows"][0]
            self.assertEqual(host["source"], "Luck_2015_mean")
            self.assertEqual(host["value_gyr"], 2.2)
            self.assertTrue(host["estimated"])
            self.assertEqual(len(host["alternatives"]), 8)
            self.assertTrue(any(row["value_gyr"] == 0 and row["source"] == "Gaia_DR3_FLAME_phot"
                                for row in host["alternatives"]))


if __name__ == "__main__":
    unittest.main()
