import json
import shlex
import unittest

from update_stellar_metallicity import EU, NASA, XHIP, enrich


def star(name, hd=0, alias=None):
    fields = [(15, name), (113, str(hd) if hd else "")]
    line = list(" " * 655)
    for offset, value in fields:
        line[offset:offset + len(value)] = value
    tail = ' SID1 "N:0"'
    if alias:
        tail += " ALS1 " + json.dumps(alias)
    return "".join(line) + tail + "\n"


def stored(line):
    tokens = shlex.split(line[655:])
    return json.loads(tokens[tokens.index("MET1") + 1])


class MetallicityImportTest(unittest.TestCase):
    def test_nasa_solar_value_takes_precedence_and_keeps_scale(self):
        rows = [(EU, dict(hostname="Host", st_met=0.3, st_metratio="[Fe/H]")),
                (NASA, dict(hostname="Host", st_met=0, st_metratio="[M/H]", st_meterr1=0.1))]
        lines, counts, ambiguous = enrich([star("Host")], rows)
        self.assertEqual(stored(lines[0]), [0, "[M/H]", 0.1, None, NASA])
        self.assertEqual(counts[NASA], 1)
        self.assertEqual(ambiguous, 0)

    def test_binary_shared_identifier_is_skipped(self):
        rows = [(NASA, dict(hostname="Unresolved host", hd_name="HD 12", st_met=-0.4))]
        lines, counts, ambiguous = enrich([star("Host A", 12), star("Host B", 12)], rows)
        self.assertEqual(counts[NASA], 0)
        self.assertEqual(ambiguous, 1)
        self.assertTrue(all(" MET1 " not in line for line in lines))

    def test_eu_fills_gap_and_extension_precedes_aliases(self):
        original = star("Host", alias="Published host")
        rows = [(EU, dict(hostname="Published host", st_met=-0.2, st_metratio="[Fe/H]"))]
        lines, counts, _ = enrich([original], rows)
        self.assertEqual(lines[0][:655], original[:655])
        self.assertEqual(counts[EU], 1)
        self.assertLess(lines[0].index(" MET1 "), lines[0].index(" ALS1 "))
        repeated, _, _ = enrich(lines.copy(), rows)
        self.assertEqual(repeated, lines)

    def test_limits_and_missing_values_stay_unknown(self):
        rows = [(NASA, dict(hostname="Host", st_met=0.2, st_metlim=1)),
                (EU, dict(hostname="Host", st_met=None))]
        lines, counts, _ = enrich([star("Host")], rows)
        self.assertNotIn(" MET1 ", lines[0])
        self.assertEqual(sum(counts.values()), 0)

    def test_compilation_fills_non_planet_host_by_identifier(self):
        original = list(star("Other star"))
        original[120:123] = "123"
        rows = [(XHIP + " B", dict(hip_name="HIP 123", st_met=-0.4, st_metratio="[Fe/H]"))]
        lines, counts, _ = enrich(["".join(original)], rows)
        self.assertEqual(stored(lines[0]), [-0.4, "[Fe/H]", None, None, XHIP + " B"])
        self.assertEqual(counts[XHIP], 1)

    def test_compilation_does_not_replace_planet_host_source(self):
        rows = [(NASA, dict(hostname="Host", st_met=0, st_metratio="[Fe/H]")),
                (XHIP + " A", dict(hostname="Host", st_met=0.2, st_metratio="[Fe/H]"))]
        lines, _, _ = enrich([star("Host")], rows)
        self.assertEqual(stored(lines[0])[4], NASA)


if __name__ == "__main__":
    unittest.main()
