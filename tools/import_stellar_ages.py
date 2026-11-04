#!/usr/bin/env python3
"""Match published age catalogs to every identifiable star in the application."""

import argparse
from collections import Counter, defaultdict
import csv
import gzip
import json
from pathlib import Path
import urllib.parse
import urllib.request

from import_stellar_abundances import catalog_index, match_record, identifier_fields, write_json


def number(value):
    try:
        result = float(str(value).strip().rstrip("?"))
        return result if 0 <= result < float("inf") else None
    except (ValueError, TypeError):
        return None


def table(path):
    with Path(path).open() as stream:
        lines = (line for line in stream if line.strip() and not line.startswith("#"))
        rows = csv.DictReader(lines, delimiter="\t")
        next(rows, None)  # Units.
        next(rows, None)  # Column separators.
        yield from rows


def build(args):
    lines = args.catalog.read_text().splitlines()
    index = catalog_index(lines)
    matches = defaultdict(list)
    additional = {}

    def add(ids, age, source, rank, lower=None, upper=None, limit=0):
        if age is None:
            return
        target = match_record(ids, index)
        if target is None:
            if source != "NASA_pscomppars":
                return
            # Exoplanet hosts can enter the application after the base star catalog.
            if any(index.get((key, ids[key])) for key in ("hip", "hd", "gaia") if ids.get(key)):
                return
            target = "host:" + ids["name"]
            additional[target] = ids
        sample = dict(value_gyr=age, source=source, estimated=False, priority=rank)
        if lower is not None:
            sample["lower_gyr"] = lower
        if upper is not None:
            sample["upper_gyr"] = upper
        if limit:
            sample["limit"] = int(limit)
        if sample not in matches[target]:
            matches[target].append(sample)

    with gzip.open(args.xhip, "rt") as stream:
        for line in stream:
            add(dict(hip=int(line[:6])), number(line[299:303]), "XHIP_V_137D", 1,
                number(line[304:308]), number(line[309:313]))
    for row in table(args.gcs):
        hip = row["HIP"].strip()
        ids = dict(hip=int(hip)) if hip else identifier_fields([row["Name"].strip()])
        for suffix, label in (("P", "Padova"), ("B", "BaSTI")):
            add(ids, number(row["ageM" + suffix]), "Casagrande_2011_" + label,
                3 if suffix == "P" else 2, number(row["age16" + suffix]), number(row["age84" + suffix]))
    crossmatch = json.loads(args.crossmatch.read_text())
    hip_by_gaia = {int(row["source_id"]): int(row["original_ext_source_id"])
                   for row in crossmatch["rows"]}
    galah = {}
    for row in table(args.galah_ids):
        gaia = row["GaiaEDR3"].strip()
        if not gaia.isdigit():
            continue
        ids = dict(gaia=int(gaia))
        if ids["gaia"] in hip_by_gaia:
            ids["hip"] = hip_by_gaia[ids["gaia"]]
        if match_record(ids, index) is not None:
            galah[row["GALAH"]] = ids
    for row in table(args.galah_ages):
        if row["GALAH"] in galah:
            add(galah[row["GALAH"]], number(row["Age50"]), "GALAH_DR3_BSTEP", 4,
                number(row["Age16"]), number(row["Age84"]))
    for row in json.loads(args.nasa.read_text()):
        ids = identifier_fields([row.get(key) or "" for key in ("hostname", "hip_name", "hd_name")])
        ids["name"] = row["hostname"]
        age = number(row["st_age"])
        lower = age - abs(row["st_ageerr2"]) if age is not None and row.get("st_ageerr2") is not None else None
        upper = age + abs(row["st_ageerr1"]) if age is not None and row.get("st_ageerr1") is not None else None
        add(ids, age, "NASA_pscomppars", 5, lower, upper, row.get("st_agelim") or 0)
    # Dedicated abundance-aware stellar evolution fits outrank generic surveys.
    add(dict(name="Sun"), 4.5673, "Connelly_2012_Solar_System_chronology", 6)
    add(dict(hip=8102, hd=10700), 7.63, "Pagano_2015_Tau_Ceti", 6, 6.13, 8.50)
    add(dict(hip=15510, hd=20794), 9.0, "Medhi_2026_HD20794", 6)
    result = []
    counts = Counter()
    for target, samples in sorted(matches.items(), key=lambda pair: str(pair[0])):
        # A bound remains an alternative, rather than masquerading as a point age.
        points = [sample for sample in samples if not sample.get("limit")]
        selected = max(points or samples, key=lambda sample: sample["priority"])
        record = dict(selected)
        if not points:
            record["estimated"] = True
            record["interpretation"] = "Published_age_bound_used_as_scenario_anchor"
        record.pop("priority")
        if target in additional:
            record.update(additional[target])
        else:
            record["name"] = lines[target][15:54].strip()
            for key in ("hip", "hd", "gaia"):
                values = index.get(("row_" + key, target), set())
                if len(values) == 1:
                    record[key] = next(iter(values))
        record["alternatives"] = [{key: value for key, value in sample.items() if key != "priority"}
                                  for sample in samples]
        counts[record["source"]] += 1
        result.append(record)
    return dict(schema=1, selection="Point age priority: dedicated evolution fit, NASA, GALAH DR3, GCS Padova, GCS BaSTI, XHIP; bounds retained",
                sources={"NASA_pscomppars": "https://exoplanetarchive.ipac.caltech.edu/",
                         "GALAH_DR3_BSTEP": "https://doi.org/10.1093/mnras/stab1242",
                         "Casagrande_2011": "https://doi.org/10.1051/0004-6361/201016276",
                         "XHIP_V_137D": "https://arxiv.org/abs/1108.4971",
                         "Pagano_2015_Tau_Ceti": "https://doi.org/10.1088/0004-637X/803/2/89",
                         "Connelly_2012_Solar_System_chronology": "https://doi.org/10.1126/science.1226919",
                         "Medhi_2026_HD20794": "https://doi.org/10.1093/mnras/stag802"},
                coverage=dict(stars=len(result), alternatives=sum(map(len, matches.values())), selected_sources=dict(counts)),
                rows=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=Path("catalogs/soles_alienorum.dat"))
    parser.add_argument("--crossmatch", type=Path, default=Path("catalogs/GaiaDR3/hipparcos_crossmatch.json"))
    for name, default in (("xhip", "/tmp/xhip_main.gz"), ("gcs", "/tmp/gcs_ages.tsv"),
                          ("galah-ids", "/tmp/galah_ids.tsv"), ("galah-ages", "/tmp/galah_ages.tsv"),
                          ("nasa", "/tmp/nasa-stellar-ages.json")):
        parser.add_argument("--" + name, type=Path, default=Path(default))
    parser.add_argument("--output", type=Path, default=Path("catalogs/stellar_ages.json"))
    parser.add_argument("--download", action="store_true", help="Refresh public input tables before importing")
    args = parser.parse_args()
    if args.download:
        endpoint = "https://vizier.cfa.harvard.edu/viz-bin/asu-tsv?"
        exports = ((args.gcs, "J/A+A/530/A138", "HIP,Name,ageMP,age16P,age84P,ageMB,age16B,age84B"),
                   (args.galah_ids, "J/MNRAS/506/150/stars", "GALAH,GaiaEDR3"),
                   (args.galah_ages, "J/MNRAS/506/150/ages", "GALAH,Age50,Age16,Age84"))
        downloads = [(path, endpoint + urllib.parse.urlencode(
            {"-source": source, "-out": columns, "-out.max": "unlimited"})) for path, source, columns in exports]
        downloads.append((args.xhip, "https://vizier.cfa.harvard.edu/ftp/cats/V/137D/main.dat.gz"))
        query = "select hostname,hd_name,hip_name,st_age,st_ageerr1,st_ageerr2,st_agelim from pscomppars where st_age is not null"
        downloads.append((args.nasa, "https://exoplanetarchive.ipac.caltech.edu/TAP/sync?" +
                          urllib.parse.urlencode({"query": query, "format": "json"})))
        for path, url in downloads:
            path.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(url, timeout=180) as response:
                path.write_bytes(response.read())
    data = build(args)
    write_json(args.output, data)
    print(json.dumps(data["coverage"], indent=2))


if __name__ == "__main__":
    main()
