#!/usr/bin/env python3
"""Import compatible elemental abundances into separate Hypatia and GALAH catalogs."""

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import gzip
import json
import math
from pathlib import Path
import re
import shlex
import time
import urllib.parse
import urllib.request

from update_stellar_metallicity import fetch, finite, identity_keys, normalized


ELEMENTS = ("Fe", "Mg", "Si", "C", "O")
HYPATIA = "https://hypatiacatalog.com/hypatia/api/v2/"
VIZIER = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync"
GAIA = "https://gea.esac.esa.int/tap-server/tap/sync"
SOLAR = dict(Fe=7.50, Mg=7.60, Si=7.51, C=8.43, O=8.69)
GALAH_SOLAR = dict(Fe=7.45, Mg=7.53, Si=7.51, C=8.39, O=8.66)


def read_json(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return json.load(response)
        except (OSError, ValueError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(str(path) + ".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(data, output, separators=(",", ":"), allow_nan=False)
        output.write("\n")
    temporary.replace(path)


def identifier_fields(names):
    result = {}
    for kind, pattern in (("hip", r"HIP\s*0*(\d+)"), ("hd", r"HD\s*0*(\d+)"),
                          ("gaia", r"Gaia DR3\s+(\d+)")):
        matches = set()
        for name in names:
            match = re.fullmatch(pattern, name or "", re.IGNORECASE)
            if match and int(match[1]) > 0:
                matches.add(int(match[1]))
        if len(matches) == 1:
            result[kind] = next(iter(matches))
    return result


def catalog_index(lines):
    index = defaultdict(set)
    for number, line in enumerate(lines):
        if line.startswith("#") or len(line) < 168:
            continue
        keys = set(identity_keys(line[15:54].strip()))
        keys.update(identity_keys(line[153:168].strip()))
        for key, start, end in (("hip", 120, 126), ("hd", 113, 119), ("gaia", 513, 533)):
            field = line[start:end].strip()
            if field.isdigit() and int(field) > 0:
                keys.add((key, int(field)))
                index[("row_" + key, number)].add(int(field))
        fields = shlex.split(line[655:]) if len(line) > 655 else []
        if "ALS1" in fields:
            for alias in fields[fields.index("ALS1") + 1:]:
                keys.update(identity_keys(alias))
        for key in keys:
            index[key].add(number)
    return index


def match_record(record, index):
    matches = set()
    for key in ("hip", "hd", "gaia"):
        if record.get(key):
            matches.update(index.get((key, record[key]), set()))
    if not matches:
        matches.update(index.get(("name", normalized(record.get("name", ""))), set()))
    if len(matches) != 1:
        return None
    number = next(iter(matches))
    for key in ("hip", "hd", "gaia"):
        known = index.get(("row_" + key, number), set())
        if known and record.get(key) and record[key] not in known:
            return None
    return number


def import_hypatia(index, refresh):
    path = Path("catalogs/Hypatia/stellar_abundances.json")
    if path.exists() and not refresh:
        return json.loads(path.read_text())
    names = set()
    for second, third in (("Mg", "Si"), ("C", "O")):
        params = dict(mode="scatter", xaxis1="Fe", yaxis1=second, zaxis1=third, solarnorm="asplund09")
        data = read_json(HYPATIA + "data/?" + urllib.parse.urlencode(params))
        for row in data.get("values", []):
            name = row.get("name", "")
            candidate = dict(name=name, **identifier_fields([name]))
            if match_record(candidate, index) is not None:
                names.add(name)
    names = sorted(names)
    print("Hypatia matching targets:", len(names), flush=True)

    def batch(group):
        params = []
        for name in group:
            for element in ELEMENTS:
                params.extend((("name", name), ("element", element.lower()), ("solarnorm", "asplund09")))
        return read_json(HYPATIA + "composition?" + urllib.parse.urlencode(params))

    rows = {}
    failures = 0
    groups = [names[start:start + 12] for start in range(0, len(names), 12)]
    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(batch, group) for group in groups]
        for completed, future in enumerate(as_completed(futures), 1):
            try:
                entries = future.result()
            except (OSError, ValueError):
                failures += 1
                continue
            for entry in entries:
                if not isinstance(entry, dict) or entry.get("solarnorm") != "asplund09":
                    continue
                element = entry.get("element", "")
                value = entry.get("median_value")
                if element not in ELEMENTS or not finite(value) or abs(value) > 8:
                    continue
                name = entry.get("name") or entry.get("requested_name")
                ids = identifier_fields(entry.get("all_names", []) + [name])
                record = rows.setdefault(name, dict(name=name, elements={}, **ids))
                record.update(ids)
                error = entry.get("plusminus")
                references = sorted(set(item.get("catalog", {}).get("id", "")
                    for item in entry.get("median", []) if item.get("catalog", {}).get("id")))
                record["elements"][element] = dict(dex=value, ratio="[" + element + "/H]",
                    error=error if finite(error) and error >= 0 else None, quality="accepted",
                    error_kind="literature_half_range_or_representative",
                    references=references, measurement_count=len(entry.get("all_values", [])))
            if completed % 50 == 0 or completed == len(groups):
                print("Hypatia batches:", completed, "/", len(groups), flush=True)
    table = dict(schema=1, source="Hypatia", solar_scale="asplund09", solar=SOLAR,
        reference="https://www.hypatiacatalog.com/api", failed_batches=failures,
        selection="Exact catalog matches; normalized literature medians; per-element representative errors or half-ranges",
        rows=sorted(rows.values(), key=lambda row: row["name"]))
    if not table["rows"] or failures:
        raise RuntimeError("Hypatia import is incomplete; existing data was preserved")
    write_json(path, table)
    return table


def galah_measurements(row):
    if any(row.get(flag) != 0 for flag in ("flagSp", "flagSpFit", "flagRed", "f_[Fe/H]")):
        return {}
    if not finite(row.get("SNRpxCCD3")) or row["SNRpxCCD3"] <= 30:
        return {}
    iron = row.get("[Fe/H]")
    if not finite(iron):
        return {}
    error = row.get("e_[Fe/H]")
    result = dict(Fe=dict(dex=iron, ratio="[Fe/H]", error=error if finite(error) and error >= 0 else None,
                          quality="accepted", error_kind="reported_standard_error"))
    for element in ELEMENTS[1:]:
        key = "[" + element + "/Fe]"
        value, error = row.get(key), row.get("e_" + key)
        if row.get("f_" + key) != 0 or not finite(value):
            continue
        result[element] = dict(dex=value, ratio=key, error=error if finite(error) and error >= 0 else None,
                               quality="accepted", error_kind="reported_standard_error")
    return result


def import_galah(refresh):
    path = Path("catalogs/GALAH/stellar_abundances.json")
    if path.exists() and not refresh:
        return json.loads(path.read_text())
    # Keep the astrometric crossmatch in its own source folder, rather than inside GALAH.
    crossmatch = Path("catalogs/GaiaDR3/hipparcos_crossmatch.json")
    if crossmatch.exists() and not refresh:
        cross = json.loads(crossmatch.read_text())["rows"]
    else:
        cross = fetch(GAIA, "select source_id,original_ext_source_id from gaiadr3.hipparcos2_best_neighbour "
                      "where number_of_neighbours=1", maxrec=200000)
        write_json(crossmatch, dict(schema=1, source="Gaia DR3", reference=GAIA, rows=cross))
    hip_counts = Counter(int(row["original_ext_source_id"]) for row in cross)
    gaia_counts = Counter(int(row["source_id"]) for row in cross)
    identities = dict((int(row["source_id"]), int(row["original_ext_source_id"])) for row in cross
        if hip_counts[int(row["original_ext_source_id"])] == 1 and gaia_counts[int(row["source_id"])] == 1)
    identifiers = sorted(identities)
    print("Unique Gaia Hipparcos matches:", len(identifiers), flush=True)
    columns = ["GaiaDR3", "flagSp", "flagSpFit", "flagRed", "SNRpxCCD3", "[Fe/H]", "e_[Fe/H]", "f_[Fe/H]"]
    for element in ELEMENTS[1:]:
        columns.extend(("[" + element + "/Fe]", "e_[" + element + "/Fe]", "f_[" + element + "/Fe]"))
    prefix = 'select ' + ','.join('"' + column + '"' for column in columns)
    prefix += ' from "J/other/PASA/42.51/allstar" where "flagSp"=0 and "flagSpFit"=0 and "flagRed"=0 '
    prefix += 'and "f_[Fe/H]"=0 and "SNRpxCCD3">30 and "GaiaDR3" in ('

    def batch(group):
        query = prefix + ','.join(str(identifier) for identifier in group) + ')'
        payload = urllib.parse.urlencode(dict(request="doQuery", lang="ADQL", format="json", maxrec=10000, query=query)).encode()
        request = urllib.request.Request(VIZIER, data=payload)
        with urllib.request.urlopen(request, timeout=90) as response:
            data = json.load(response)
        names = [column["name"] for column in data["metadata"]]
        return [dict(zip(names, row)) for row in data["data"]]

    rows = []
    with ThreadPoolExecutor(max_workers=4) as pool:
        groups = [identifiers[start:start + 1500] for start in range(0, len(identifiers), 1500)]
        for completed, response in enumerate(pool.map(batch, groups), 1):
            for row in response:
                elements = galah_measurements(row)
                if not elements:
                    continue
                gaia = int(row["GaiaDR3"])
                rows.append(dict(hip=identities[gaia], gaia=gaia, elements=elements))
            if completed % 10 == 0 or completed == len(groups):
                print("GALAH batches:", completed, "/", len(groups), flush=True)
    table = dict(schema=1, source="GALAH DR4", solar_scale="grevesse07", solar=GALAH_SOLAR,
        reference="https://arxiv.org/html/2409.19858v1#A3.T8", selection="Unique Gaia DR3 Hipparcos identities; zero global and elemental flags; SNRpxCCD3 > 30",
        rows=sorted(rows, key=lambda row: row["hip"]))
    if not rows:
        raise RuntimeError("GALAH import returned no accepted rows")
    write_json(path, table)
    return table


def bake_catalog(lines, tables, index):
    selected = defaultdict(dict)
    for table in tables:
        for row in table["rows"]:
            number = match_record(row, index)
            if number is None:
                continue
            selected[number][table["source"]] = dict(solar_scale=table["solar_scale"], solar=table["solar"],
                elements=row["elements"], reference=table["reference"])
    refreshed_sources = {table["source"] for table in tables}
    for number, original in enumerate(lines):
        if number not in selected and " CHEM1 " not in original:
            continue
        line = original.rstrip("\r\n")
        fields = shlex.split(line[655:])
        bundles = {}
        if "CHEM1" in fields:
            bundles = json.loads(fields[fields.index("CHEM1") + 1])
        for source in refreshed_sources:
            bundles.pop(source, None)
        bundles.update(selected.get(number, {}))
        tail = re.sub(r' CHEM1 "(?:\\.|[^"\\])*"', "", line[655:])
        marker = tail.find(" ALS1")
        marker = len(tail) if marker < 0 else marker
        extension = " CHEM1 " + json.dumps(json.dumps(bundles, separators=(",", ":"))) if bundles else ""
        lines[number] = line[:655] + tail[:marker] + extension + tail[marker:] + "\n"
    return lines, Counter(source for bundles in selected.values() for source in bundles)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=Path("catalogs/soles_alienorum.dat"))
    parser.add_argument("--refresh", action="store_true")
    parser.add_argument("--source", choices=("Hypatia", "GALAH", "all"), default="all")
    arguments = parser.parse_args()
    lines = arguments.catalog.read_text().splitlines(keepends=True)
    index = catalog_index(lines)
    tables = []
    if arguments.source in ("Hypatia", "all"):
        tables.append(import_hypatia(index, arguments.refresh))
    if arguments.source in ("GALAH", "all"):
        tables.append(import_galah(arguments.refresh))
    for table in tables:
        kind = "reported_standard_error" if table["source"] == "GALAH DR4" else "literature_half_range_or_representative"
        for row in table["rows"]:
            for measurement in row["elements"].values():
                measurement.setdefault("error_kind", kind)
        folder = "GALAH" if table["source"] == "GALAH DR4" else "Hypatia"
        write_json(Path("catalogs") / folder / "stellar_abundances.json", table)
    lines, counts = bake_catalog(lines, tables, index)
    data = "".join(lines).encode()
    arguments.catalog.write_bytes(data)
    Path(str(arguments.catalog) + ".gz").write_bytes(gzip.compress(data, mtime=0))
    print("Baked abundance bundles:", dict(counts), flush=True)


if __name__ == "__main__":
    main()
