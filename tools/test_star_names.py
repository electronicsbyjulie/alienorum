import datetime
from html import escape
import unittest

from update_star_names import records, update


DATE = datetime.date(2026, 10, 25)


def source(rows):
    return "<table>" + "".join("<tr>" + "".join("<td>" + escape(value) + "</td>" for value in row) + "</tr>" for row in rows) + "</table>"


def row(name, hip="", bayer="", reference="see ASE", date="2026/10/15", designation="HR 649"):
    values = [""] * 17
    values[0], values[2], values[3], values[4] = name, designation, hip, bayer
    values[9], values[10] = reference, date
    return values


class StarNameTests(unittest.TestCase):
    def test_filters_all_campaign_references_and_future_adoptions(self):
        html = source([row("Blaze Star", "78322"), row("Helvetios", reference="2015 NameExoWorlds"),
            row("Dilmun", reference="NEWC Bahrain"), row("Future", date="2027/01/01")])
        self.assertEqual([name for name, _ in records(html, DATE)], ["Blaze Star"])

    def test_unicode_names_preserve_byte_columns_and_components(self):
        result = dict(records(source([row("Áldu", "18532"), row("Pulcherrima", "72105", "ε Boo B")]), DATE))
        self.assertEqual(result["Áldu"].encode()[:25].decode().strip(), "Áldu")
        self.assertEqual(int(result["Áldu"].encode()[33:39]), 18532)
        self.assertEqual(result["Pulcherrima"].encode()[39:40], b"B")
        self.assertFalse(result["Áldu"].endswith(" "))

    def test_identifier_only_component_and_legacy_preservation(self):
        html = source([row("Siwarha", bayer="α Ori B", designation="-")])
        line = records(html, DATE)[0][1].encode()
        self.assertEqual(line[39:40], b"B")
        self.assertEqual(line[49:].decode(), "Alp Ori")
        legacy = "Legacy Name                  123\n# project alias\n"
        refreshed = update(html, legacy, DATE)
        self.assertTrue(refreshed.endswith(legacy))
        self.assertEqual(update(html, refreshed, DATE), refreshed)

    def test_variable_star_selectors_do_not_duplicate_v_prefix(self):
        data = dict(records(source([row("Dust-Ball", designation="V* V927 Per"),
            row("Pistol Star", designation="V*4647 Sgr")]), DATE))
        self.assertEqual(data["Dust-Ball"][49:], "V927 Per")
        self.assertEqual(data["Pistol Star"][49:], "V4647 Sgr")

    def test_rejects_missing_tables_and_duplicate_names(self):
        with self.assertRaises(ValueError):
            records("<html>unavailable</html>", DATE)
        with self.assertRaises(ValueError):
            records(source([row("Same"), row("Same")]), DATE)


if __name__ == "__main__":
    unittest.main()
