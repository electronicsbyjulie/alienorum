#!/usr/bin/env python3
"""Enrich the condensed stellar catalog from NASA, exoplanet.eu, and XHIP."""

import argparse
import gzip
import json
import math
from pathlib import Path
import re
import shlex
import urllib.parse
import urllib.request


NASA = "NASA pscomppars"
EU = "exoplanet.eu"
XHIP = "XHIP V/137D"
NASA_URL = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync"
EU_URL = "http://voparis-tap-planeto.obspm.fr/tap/sync"
XHIP_URL = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync"


def fetch(endpoint, query):
    parameters = urllib.parse.urlencode(dict(
        request="doQuery", lang="ADQL", format="json", maxrec=50000, query=query))
    with urllib.request.urlopen(endpoint + "?" + parameters, timeout=90) as response:
        result = json.load(response)
    if isinstance(result, list):
        return result
    columns = [column["name"] for column in result.get("columns", result.get("metadata", []))]
    return [dict(zip(columns, row)) for row in result["data"]]


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def normalized(name):
    return re.sub(r"[\s-]+", "", name).casefold()


def identity_keys(name):
    if name:
        yield ("name", normalized(name))


def measurement(row, source):
    value = row.get("st_met")
    if not finite(value) or row.get("st_metlim") not in (None, 0):
        return None
    ratio = row.get("st_metratio") or ""
    if ratio not in ("[Fe/H]", "[M/H]", ""):
        return None
    upper, lower = row.get("st_meterr1"), row.get("st_meterr2")
    return [value, ratio, abs(upper) if finite(upper) else None,
            -abs(lower) if finite(lower) else None, source]


def rank(value):
    errors = [abs(error) for error in value[2:4] if error is not None]
    priority = 0 if value[4] == NASA else 1 if value[4] == EU else 2
    return (priority, value[1] != "[Fe/H]", -len(errors),
            sum(errors), value[0], value[1])


def enrich(lines, rows):
    index = {}
    for number, line in enumerate(lines):
        if line.startswith("#") or len(line) < 168:
            continue
        keys = set(identity_keys(line[15:54].strip()))
        keys.update(identity_keys(line[153:168].strip()))
        for kind, field in (("HD", line[113:119]), ("HIP", line[120:126])):
            if field.strip().isdigit() and int(field) > 0:
                keys.add((kind, int(field)))
        tail = shlex.split(line[655:]) if len(line) > 655 else []
        if "ALS1" in tail:
            for alias in tail[tail.index("ALS1") + 1:]:
                keys.update(identity_keys(alias))
        for key in keys:
            index.setdefault(key, set()).add(number)

    selected = {}
    ambiguous = 0
    for source, row in rows:
        value = measurement(row, source)
        if value is None:
            continue
        keys = list(identity_keys(row.get("hostname", "")))
        for kind, field in (("HD", "hd_name"), ("HIP", "hip_name")):
            match = re.fullmatch(kind + r"\s*(\d+)", row.get(field) or "", re.IGNORECASE)
            if match:
                keys.append((kind, int(match[1])))
        names = index.get(keys[0], set()) if keys else set()
        matches = set(names)
        if not matches:
            for key in keys[1:]:
                matches.update(index.get(key, set()))
        if len(matches) != 1:
            ambiguous += bool(matches)
            continue
        number = next(iter(matches))
        if number not in selected or rank(value) < rank(selected[number]):
            selected[number] = value

    for number, value in selected.items():
        line = lines[number].rstrip("\r\n")
        # Insert before aliases, whose legacy parser consumes the remainder of the row.
        tail = line[655:]
        tail = re.sub(r' MET1 "(?:\\.|[^"\\])*"', "", tail)
        marker = tail.find(" ALS1")
        marker = len(tail) if marker < 0 else marker
        extension = " MET1 " + json.dumps(json.dumps(value, separators=(",", ":")))
        lines[number] = line[:655] + tail[:marker] + extension + tail[marker:] + "\n"
    counts = {source: sum(value[4].startswith(source) for value in selected.values()) for source in (NASA, EU, XHIP)}
    return lines, counts, ambiguous


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=Path("catalogs/soles_alienorum.dat"))
    arguments = parser.parse_args()
    nasa = fetch(NASA_URL, "select distinct hostname,hd_name,hip_name,st_met,st_meterr1,st_meterr2,"
                 "st_metlim,st_metratio from pscomppars where st_met is not null and st_metlim=0")
    eu = fetch(EU_URL, "select distinct star_name as hostname,star_metallicity as st_met "
               "from exoplanet.epn_core where star_metallicity is not null "
               "and publication_status='Published in a refereed paper'")
    for row in eu:
        row["st_metratio"] = "[Fe/H]"
        # TAP exposes the value without uncertainty columns.
        for kind in ("HD", "HIP"):
            if re.fullmatch(kind + r"\s*\d+", row.get("hostname") or "", re.IGNORECASE):
                row[kind.lower() + "_name"] = row["hostname"]
    xhip = fetch(XHIP_URL, 'select HIP,Comp,"[Fe/H]","e_[Fe/H]","q_[Fe/H]" '
                 'from "V/137D/XHIP" where "[Fe/H]" is not null')
    compiled = []
    for row in xhip:
        quality = row.get("q_[Fe/H]")
        if quality not in ("A", "B", "C"):
            continue
        # XHIP: A calibrated; B calibrated without errors; C uncalibrated without errors.
        # Its 9.99 uncertainty sentinel means unavailable, rather than a measured bound.
        error = row.get("e_[Fe/H]")
        error = error if quality == "A" and finite(error) and 0 <= error < 9.99 else None
        compiled.append((XHIP + " " + quality, dict(
            hip_name="HIP " + str(row["HIP"]), st_met=row["[Fe/H]"],
            st_metratio="[Fe/H]", st_meterr1=error, st_meterr2=error)))
    with arguments.catalog.open(encoding="utf-8") as source:
        lines = source.readlines()
    lines, counts, ambiguous = enrich(lines,
        [(NASA, row) for row in nasa] + [(EU, row) for row in eu] + compiled)
    data = "".join(lines).encode("utf-8")
    temporary = arguments.catalog.with_suffix(".dat.tmp")
    temporary.write_bytes(data)
    compressed = Path(str(arguments.catalog) + ".gz")
    compressed_temporary = Path(str(compressed) + ".tmp")
    with compressed_temporary.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as archive:
            archive.write(data)
    temporary.replace(arguments.catalog)
    compressed_temporary.replace(compressed)
    print("Stored metallicity:", counts, "; ambiguous matches skipped:", ambiguous)


if __name__ == "__main__":
    main()
