#!/usr/bin/env python3
"""Refresh the fixed-width WGSN name section without changing project aliases."""

import argparse
import datetime
import hashlib
from html.parser import HTMLParser
from pathlib import Path
import re
import unicodedata
from urllib.request import urlopen

SOURCE = "https://exopla.net/star-names/modern-iau-star-names/"
BEGIN = "# BEGIN IAU WGSN"
END = "# END IAU WGSN"
GREEK = dict(zip("αβγδεζηθικλμνξοπρστυφχψω", "Alp Bet Gam Del Eps Zet Eta The Iot Kap Lam Mu Nu Xi Omi Pi Rho Sig Tau Ups Phi Chi Psi Ome".split()))


class CatalogTable(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.depth = 0
        self.finished = False
        self.cell = None
        self.row = []
        self.rows = []

    def handle_starttag(self, tag, attrs):
        if self.finished:
            return
        if tag == "table":
            self.depth += 1
        elif self.depth and tag == "tr":
            self.row = []
        elif self.depth and tag in ("td", "th"):
            self.cell = []

    def handle_data(self, text):
        if self.cell is not None:
            self.cell.append(text)

    def handle_endtag(self, tag):
        if self.finished:
            return
        if tag in ("td", "th") and self.cell is not None:
            self.row.append(" ".join("".join(self.cell).split()))
            self.cell = None
        elif tag == "tr" and self.depth:
            if len(self.row) == 17 and self.row[0] != "proper names":
                self.rows.append(self.row)
        elif tag == "table" and self.depth:
            self.depth -= 1
            self.finished = self.depth == 0


def records(html, as_of):
    table = CatalogTable()
    table.feed(html)
    if not table.rows:
        raise ValueError("The official catalog table was not found")
    result = []
    for row in table.rows:
        name, _, designation, hip, bayer = row[:5]
        if re.search(r"nameexoworld|\bNEWC\b", row[9], re.I):
            continue
        adopted = datetime.date.fromisoformat(row[10].replace("/", "-"))
        if adopted > as_of:
            continue
        hd = re.fullmatch(r"HD\s*(\d+)", designation)
        gliese = re.fullmatch(r"GJ\s*(\d+(?:\.\d+)?)(?:\s*([A-Z]))?", designation)
        component = re.search(r"\s([A-Z])$", bayer)
        selector = ""
        if not hip and not hd and not gliese:
            selector = bayer if component else designation
            variable = selector.startswith("V*")
            selector = re.sub(r"^V\*\s*", "", selector)
            if variable and selector[:1].isdigit():
                selector = "V" + selector
            selector = re.sub(r"^\*\s*", "", selector)
            selector = unicodedata.normalize("NFKC", selector).replace("ϵ", "ε")
            for letter, abbreviation in GREEK.items():
                selector = selector.replace(letter, abbreviation)
            if component:
                selector = selector.rsplit(" ", 1)[0]
        if hip and not hip.isdigit():
            raise ValueError(f"Unexpected HIP identifier: {hip}")
        name_bytes = name.encode("utf-8")
        if len(name_bytes) > 25:
            raise ValueError(f"Name exceeds the fixed field: {name}")
        line = (name_bytes.ljust(25) + str(int(hd[1]) if hd else "").encode().rjust(7)
            + b" " + hip.encode().rjust(6) + (component[1] if component else (gliese[2] if gliese and gliese[2] else " ")).encode()
            + (gliese[1] if gliese else "").encode().rjust(8))
        if selector:
            line += b" " + selector.encode("utf-8")
        result.append((name, line.rstrip().decode("utf-8")))
    if len({name for name, _ in result}) != len(result):
        raise ValueError("The source contains duplicate names")
    return sorted(result)


def update(html, current, as_of):
    start = current.find(BEGIN)
    if start >= 0:
        end = current.index("\n", current.index(END, start))
        current = current[end + 1:]
    rows = records(html, as_of)
    header = (f"{BEGIN} — official catalog as of {as_of.isoformat()}\n"
        f"# Source: {SOURCE}\n"
        f"# Source SHA256: {hashlib.sha256(html.encode()).hexdigest()}\n"
        "# Excludes NameExoWorlds/NEWC campaigns. Legacy/project records follow unchanged.\n"
        "# Byte fields: name 1-25; HD 26-32; HIP 34-39; component 40; GJ 41-48; optional exact selector 50+.\n")
    return header + "\n".join(line for _, line in rows) + f"\n{END}\n" + current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Use an already downloaded official HTML catalog")
    parser.add_argument("--output", type=Path, default=Path("starname.dat"))
    parser.add_argument("--as-of", type=datetime.date.fromisoformat, default=datetime.date.today())
    args = parser.parse_args()
    html = args.source.read_text(encoding="utf-8") if args.source else urlopen(SOURCE, timeout=60).read().decode("utf-8")
    current = args.output.read_text(encoding="utf-8")
    args.output.write_bytes(update(html, current, args.as_of).encode("utf-8"))
    print(f"Updated {len(records(html, args.as_of))} WGSN names, excluding NameExoWorlds")


if __name__ == "__main__":
    main()
