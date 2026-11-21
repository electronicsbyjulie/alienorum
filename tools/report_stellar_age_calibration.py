#!/usr/bin/env python3
"""Compare frozen benchmark runs and optional whole-catalogue age audits."""

import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path


def report(before, after, catalog_before=None, catalog_after=None):
    result = dict(schema=1,
        interpretation="Log-age residuals compare estimates to published age labels; interval overlap is not independent-clock coverage",
        caveats=["Many reference ages are model dependent, as recorded per star",
            "Seismic luminosities and masses share model information with seismic age labels",
            "Clusters and binary systems remain wholly in one partition",
            "Poorly dated stars and alternative evolutionary branches retain broad scenarios",
            "No reference age is passed to the fit as an input"],
        before=before["summary"], after=after["summary"],
        benchmark_seconds=dict(before=before["elapsed_seconds"], after=after["elapsed_seconds"]),
        sources=after["sources"],
        outliers=sorted((row for row in after["rows"] if row["mode"] == "mass_constrained"),
            key=lambda row: abs(row["log10_age_ratio"]), reverse=True)[:20])
    if catalog_before and catalog_after:
        def read(path):
            with path.open() as stream:
                return {(row["HD"], row["HIP"], row["name"]): row for row in csv.DictReader(stream, delimiter="\t")}
        old, new = read(catalog_before), read(catalog_after)
        changes = []
        invalid = 0
        sources = Counter()
        published_changes = []
        for key, row in new.items():
            age = float(row["age_gyr"])
            invalid += not math.isfinite(age) or age < 0
            sources[row["source"]] += 1
            if key not in old:
                continue
            previous = old[key]
            prior = float(previous["age_gyr"])
            if previous["source"] not in ("MIST_1.2_HR_diagram_fit", "Bedard_2020_cooling_and_progenitor_prior",
                    "main_sequence_lifetime_prior", "unconstrained_stellar_age_prior",
                    "gyrochronology_Mamajek_Hillenbrand_2008", "cooling_plus_progenitor_lifetime_prior"):
                if age != prior:
                    published_changes.append(dict(name=row["name"], before_gyr=prior, after_gyr=age))
            if age > 0 and prior > 0 and abs(math.log10(age / prior)) > math.log10(2):
                changes.append(dict(name=row["name"], hd=row["HD"], hip=row["HIP"],
                    before_gyr=prior, after_gyr=age, log10_age_ratio=math.log10(age / prior)))
        result["catalogue"] = dict(stars=len(new), invalid_ages=invalid, sources=dict(sources),
            published_or_inherited_changes=published_changes, shifts_over_factor_two=len(changes),
            largest_shifts=sorted(changes, key=lambda row: abs(row["log10_age_ratio"]), reverse=True)[:30])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--catalog-before", type=Path)
    parser.add_argument("--catalog-after", type=Path)
    parser.add_argument("--output", type=Path, default=Path("tools/stellar_age_calibration_report.json"))
    args = parser.parse_args()
    data = report(json.loads(args.before.read_text()), json.loads(args.after.read_text()),
        args.catalog_before, args.catalog_after)
    args.output.write_text(json.dumps(data, indent=2) + "\n")
