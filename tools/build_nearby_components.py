"""Extract named Gliese components from the Reylé et al. 10 pc catalog."""
import argparse
import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path


def build(source):
    groups = defaultdict(dict)
    with source.open(newline="", encoding="utf-8-sig") as catalog:
        for row in csv.DictReader(catalog):
            identity = re.fullmatch(r"(GJ \d+(?:\.\d+)?) ([A-Z])", row["GJ"])
            if not identity or row["OBJ_CAT"] not in {"*", "LM", "LM?", "WD", "BD"}:
                continue
            if not all(row[key] for key in ("RA", "DEC", "EPOCH", "PARALLAX")):
                continue
            group, component = identity.groups()
            entry = {"id": row["GJ"], "aliases": sorted({row[key] for key in
                     ("OBJ_NAME", "COMMON_NAME", "SIMBAD_NAME", "HD", "HIP") if row[key]})}
            for dest, key in (("ra", "RA"), ("dec", "DEC"), ("year", "EPOCH"), ("parallax", "PARALLAX"),
                              ("parallax_error", "PARALLAX_ERROR"),
                              ("pmra", "PMRA"), ("pmdec", "PMDEC"), ("rv", "RV"), ("visual", "V")):
                value = row[key].removesuffix(":").removesuffix("?")
                try:
                    value = float(value)
                except ValueError:
                    continue
                if math.isfinite(value):
                    entry[dest] = value
            if not all(key in entry for key in ("ra", "dec", "year", "parallax")) or entry["parallax"] <= 0:
                continue
            entry["spectrum"] = row["SP_TYPE"]
            entry["class"] = row["OBJ_CAT"]
            if row["PARALLAX_BIBCODE"]:
                entry["parallax_source"] = row["PARALLAX_BIBCODE"]
            if row["SYSTEM_BIBCODE"]:
                entry["system_source"] = row["SYSTEM_BIBCODE"]
            groups[group][component] = entry
    return {group: [records[key] for key in sorted(records)] for group, records in sorted(groups.items())
            if "A" in records and len(records) > 1}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    groups = build(args.input)
    with args.output.open("w", encoding="utf-8") as result:
        json.dump({"source": "https://gruze.org/10pc/The10pcSample_v2.csv", "systems": groups},
                  result, ensure_ascii=False, indent=2)
        result.write("\n")
