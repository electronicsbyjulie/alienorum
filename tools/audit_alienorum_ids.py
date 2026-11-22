"""Compare the published stellar IDs with the bundled source cross-reference."""

import argparse
import csv
import re
from collections import Counter, defaultdict
from pathlib import Path


RANKED = re.compile(r"^(?:-?\d+|V)[bcwyor] [A-Za-z]{3}(?: \d+)?$")
BAKED_IDS = (("HD", 113, 119), ("HIP", 120, 126), ("Gaia", 513, 533))
REFERENCE_IDS = (("HD", 58, 66), ("HIP", 50, 58), ("Gaia", 15, 35))


def fields(row, columns):
    for source, start, end in columns:
        value = row[start:end].strip()
        if value.isdigit() and int(value) > 0:
            yield source, int(value)


def records(path):
    with path.open("rb") as source:
        yield from (row for row in source if row.strip() and not row.startswith(b"#"))


def audit(catalogs, crosswalk):
    counts = Counter()
    index = defaultdict(set)
    hipparcos_ids = defaultdict(set)
    henry_draper_ids = set()
    for row in records(catalogs / "soles_alienorum.dat"):
        name = row[:14].decode("ascii", errors="replace").strip()
        for _, identifier in fields(row, (("HIP", 120, 126),)):
            hipparcos_ids[identifier].add(name)
        henry_draper_ids.update(identifier for _, identifier in fields(row, (("HD", 113, 119),)))
        if not RANKED.fullmatch(name):
            continue
        counts["App ranked primaries"] += 1
        key = (name, row[15:54].strip().decode("utf-8", errors="replace"))
        for identifier in fields(row, BAKED_IDS):
            index[identifier].add(key)

    for path in sorted((catalogs / "cross_ref").glob("*.dat")):
        for row in records(path):
            name = row[:15].decode("ascii", errors="replace").strip()
            if not RANKED.fullmatch(name):
                continue
            counts["Cross-reference ranked primaries"] += 1
            identifiers = list(fields(row, REFERENCE_IDS))
            matches = set().union(*(index.get(identifier, set()) for identifier in identifiers))
            if len(matches) != 1:
                counts["Cross-reference ambiguous or unmatched"] += 1
                continue
            current, star = matches.pop()
            counts["Cross-reference unique matches"] += 1
            if current == name:
                continue
            counts["Cross-reference with differing ID"] += 1
            if crosswalk is not None:
                crosswalk.writerow((current, name, star, ", ".join(f"{source} {value}" for source, value in identifiers)))

    hipparcos = catalogs / "Hipparcos" / "hip_main.dat"
    if hipparcos.is_file():
        for row in records(hipparcos):
            parts = row.split(b"|", 6)
            if len(parts) < 6:
                continue
            try:
                identifier, magnitude = int(parts[1]), float(parts[5])
            except ValueError:
                continue
            if magnitude >= 10:
                continue
            counts["Hipparcos V below 10"] += 1
            identifiers = hipparcos_ids.get(identifier)
            if identifiers is None:
                counts["Hipparcos number absent from app catalog"] += 1
                if any(value in henry_draper_ids for _, value in fields(row, (("HD", 390, 396),))):
                    counts["Absent Hipparcos number with an HD counterpart"] += 1
            elif not any(identifiers):
                counts["Hipparcos number with only blank app IDs"] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalogs", type=Path, default=Path("catalogs"))
    parser.add_argument("--crosswalk", type=Path, help="Write differing IDs and source identifiers as a TSV")
    args = parser.parse_args()
    if args.crosswalk is None:
        result = audit(args.catalogs, None)
    else:
        with args.crosswalk.open("x", encoding="utf-8", newline="") as output:
            writer = csv.writer(output, delimiter="\t")
            writer.writerow(("App ID", "Bundled cross-reference ID", "App object", "Cross-reference identifiers"))
            result = audit(args.catalogs, writer)
    for label, count in result.items():
        print(f"{label}: {count:,}")


if __name__ == "__main__":
    main()
