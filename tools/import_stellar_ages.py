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


def vizier_tables(path):
    block = []
    for line in Path(path).read_text().splitlines():
        if line.startswith("#Table") and block:
            yield list(csv.DictReader(block[0:1] + block[3:], delimiter="\t"))
            block = []
        if line.strip() and not line.startswith("#"):
            block.append(line)
    if block:
        yield list(csv.DictReader(block[0:1] + block[3:], delimiter="\t"))


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
        sample = dict(value_gyr=age, source=source,
                      estimated=source != "Connelly_2012_Solar_System_chronology", priority=rank)
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
    for argument, source, priority, column in (
            ("gaia_ages", "Gaia_DR3_FLAME_phot", 1.5, "age_flame"),
            ("gaia_spec_ages", "Gaia_DR3_FLAME_spec", 2.5, "age_flame_spec")):
        path = getattr(args, argument, None)
        if path is None:
            continue
        catalog = json.loads(Path(path).read_text())
        columns = [item["name"] for item in catalog["metadata"]]
        for values in catalog["data"]:
            row = dict(zip(columns, values))
            add(dict(hip=int(row["hip"]), gaia=int(row["source_id"])), number(row[column]), source, priority,
                number(row[column + "_lower"]), number(row[column + "_upper"]))
    for argument, prefix, base_priority in (("giant_ages", "Luck_2015", 4.0),
                                             ("dwarf_ages", "Luck_2017", 4.1),
                                             ("southern_ages", "Luck_2018", 4.2)):
        path = getattr(args, argument, None)
        if path is None:
            continue
        identities = {}
        tables = list(vizier_tables(path))
        for rows in tables:
            for row in rows:
                if "HD" in row:
                    ids = identifier_fields(["HD " + row["HD"].strip()])
                    if row.get("HIP", "").strip().isdigit():
                        ids["hip"] = int(row["HIP"])
                    name = row.get("Name", row.get("ID", "")).strip()
                    ids["name"] = row.get("SimbadName", name).strip()
                    identities[name] = ids
        for rows in tables:
            for row in rows:
                ids = identities.get(row.get("Name", row.get("ID", "")).strip())
                if not ids or not ("<Age>" in row or "Ageavg" in row):
                    continue
                for key, label, rank in (("BAge", "Bertelli", .1), ("B1Age", "Bertelli", .1),
                                          ("DAge", "Dotter", .2), ("YAge", "YonseiYale", .3),
                                          ("B2Age", "BaSTI", .4), ("<Age>", "mean", .5),
                                          ("Ageavg", "mean", .5)):
                    if key in row:
                        add(ids, number(row[key]), prefix + "_" + label, base_priority + rank)
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
    # Dedicated fits are data, rather than object-specific branches in the estimator.
    references = json.loads(Path(getattr(args, "literature",
        Path(__file__).resolve().parent.parent / "catalogs/stellar_age_literature.json")).read_text())
    for row in references["rows"]:
        ids = {key: row[key] for key in ("name", "hd", "hip", "gaia") if key in row}
        add(ids, number(row["value_gyr"]), row["source"], row.get("priority", 6),
            number(row.get("lower_gyr")), number(row.get("upper_gyr")), row.get("limit", 0))
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
    return dict(schema=1, selection="Point age priority: dedicated fits, NASA, local Luck surveys, GALAH DR3, GCS Padova, Gaia FLAME spectra, GCS BaSTI, Gaia FLAME photometry, XHIP; alternatives and bounds retained",
                sources={"NASA_pscomppars": "https://exoplanetarchive.ipac.caltech.edu/",
                         "GALAH_DR3_BSTEP": "https://doi.org/10.1093/mnras/stab1242",
                         "Casagrande_2011": "https://doi.org/10.1051/0004-6361/201016276",
                         "XHIP_V_137D": "https://arxiv.org/abs/1108.4971",
                         "Pagano_2015_Tau_Ceti": "https://doi.org/10.1088/0004-637X/803/2/89",
                         "Connelly_2012_Solar_System_chronology": "https://doi.org/10.1126/science.1226919",
                         "Medhi_2026_HD20794": "https://doi.org/10.1093/mnras/stag802",
                         "Gaia_DR3_FLAME_phot": "https://doi.org/10.1051/0004-6361/202243688",
                         "Gaia_DR3_FLAME_spec": "https://doi.org/10.1051/0004-6361/202243688",
                         "Luck_2015": "https://doi.org/10.1088/0004-6256/150/3/88",
                         "Luck_2017": "https://doi.org/10.3847/1538-3881/153/1/21",
                         "Luck_2018": "https://doi.org/10.3847/1538-3881/aaa9b5",
                         **references["sources"]},
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
    parser.add_argument("--literature", type=Path, default=Path("catalogs/stellar_age_literature.json"))
    parser.add_argument("--gaia-ages", type=Path, default=Path("/tmp/alienorum-gaia-ages.json"))
    parser.add_argument("--gaia-spec-ages", type=Path, default=Path("/tmp/alienorum-gaia-spec-ages.json"))
    parser.add_argument("--giant-ages", type=Path, default=Path("/tmp/alienorum-luck-giants.tsv"))
    parser.add_argument("--dwarf-ages", type=Path, default=Path("/tmp/alienorum-luck-dwarfs.tsv"))
    parser.add_argument("--southern-ages", type=Path, default=Path("/tmp/alienorum-luck-southern.tsv"))
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
        for path, source in ((args.giant_ages, "J/AJ/150/88"), (args.dwarf_ages, "J/AJ/153/21"),
                             (args.southern_ages, "J/AJ/155/111")):
            downloads.append((path, endpoint + urllib.parse.urlencode(
                {"-source": source, "-out.all": "", "-out.max": "unlimited"})))
        for path, suffix, name in ((args.gaia_ages, "", "astrophysical_parameters"),
                                   (args.gaia_spec_ages, "_spec", "astrophysical_parameters_supp")):
            field = "age_flame" + suffix
            query = (f"select x.original_ext_source_id as hip,p.source_id,p.{field},"
                     f"p.{field}_lower,p.{field}_upper from gaiadr3.hipparcos2_best_neighbour x "
                     f"join gaiadr3.{name} p on x.source_id=p.source_id "
                     f"where x.number_of_neighbours=1 and p.{field} is not null")
            downloads.append((path, "https://gea.esac.esa.int/tap-server/tap/sync?" +
                              urllib.parse.urlencode({"REQUEST": "doQuery", "LANG": "ADQL",
                                                      "FORMAT": "json", "MAXREC": 200000, "QUERY": query})))
        for path, url in downloads:
            path.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(url, timeout=180) as response:
                path.write_bytes(response.read())
    data = build(args)
    write_json(args.output, data)
    print(json.dumps(data["coverage"], indent=2))


if __name__ == "__main__":
    main()
