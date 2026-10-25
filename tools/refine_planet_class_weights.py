#!/usr/bin/env python3
"""Explore stronger shrinkage of metallicity while preserving compact recall.

This development experiment retains equal total class weights, the original
prediction features, a 0.5 threshold, and host-separated validation. Candidate
selection uses this benchmark, so its results remain exploratory.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path

from calibrate_planet_classifier import sample
from experiment_planet_class_weights import evaluate, fit


STRENGTHS = (0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0)
METAL_FACTORS = (1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 30.0, 100.0)


def minimum_compact_score(result):
    return min(1 - row["bulky_score"] for row in result["predictions"] if row["label"] == 0)


def eligible(candidate, previous):
    improved = False
    for protocol in ("five_fold", "individual_host"):
        current = candidate[protocol]
        old = previous[protocol]
        groups = current["metrics"]["groups"]
        if any(groups[name]["count"] != old["metrics"]["groups"][name]["count"]
            for name in ("compact", "bulky")):
            return False
        if groups["compact"]["correct"] != groups["compact"]["count"]:
            return False
        if groups["bulky"]["correct"] < old["metrics"]["groups"]["bulky"]["correct"]:
            return False
        if minimum_compact_score(current) < minimum_compact_score(old):
            return False
        improved |= groups["bulky"]["correct"] > old["metrics"]["groups"]["bulky"]["correct"]
    return improved


def rank(candidate):
    results = [candidate[protocol] for protocol in ("five_fold", "individual_host")]
    correct = [result["metrics"]["groups"]["bulky"]["correct"] for result in results]
    return (min(correct), sum(correct), minimum_compact_score(candidate["individual_host"]),
        -sum(result["metrics"]["balanced_log_loss"] for result in results))


def compare(previous, current):
    before = {row["planet"]: row for row in previous["predictions"]}
    after = {row["planet"]: row for row in current["predictions"]}
    changes = dict(corrected=[], regressed=[])
    for name in sorted(before):
        old = before[name]
        new = after[name]
        old_correct = int(old["bulky_score"] >= .5) == old["label"]
        new_correct = int(new["bulky_score"] >= .5) == new["label"]
        if old_correct != new_correct:
            changes["corrected" if new_correct else "regressed"].append(name)
    return changes


def write_csv(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def write_tables(folder, inputs, previous, candidate):
    mappings = {}
    for prefix, model in (("previous", previous), ("refined", candidate)):
        for protocol in ("five_fold", "individual_host"):
            mappings[f"{prefix}_{protocol}"] = {row["planet"]: row for row in model[protocol]["predictions"]}
    tables = dict(compact=[], bulky=[], bulky_misclassified=[], changed=[])
    for raw in sorted(inputs, key=lambda row: row["pl_name"]):
        name = raw["pl_name"]
        reference = mappings["refined_five_fold"][name]
        entry = dict(planet=name, host=reference["host"],
            observed_group="bulky" if reference["label"] else "compact",
            mass_earth=raw["pl_bmasse"], radius_earth=raw["pl_rade"], Fe_H_dex=raw["st_met"])
        changed = False
        for protocol in ("five_fold", "individual_host"):
            for prefix in ("previous", "refined"):
                key = f"{prefix}_{protocol}"
                row = mappings[key][name]
                entry[key + "_compact_score"] = 1 - row["bulky_score"]
                entry[key + "_prediction"] = "bulky" if row["bulky_score"] >= .5 else "compact"
            changed |= entry[f"previous_{protocol}_prediction"] != entry[f"refined_{protocol}_prediction"]
        tables["bulky" if reference["label"] else "compact"].append(entry)
        if reference["label"] and reference["bulky_score"] < .5:
            tables["bulky_misclassified"].append(entry)
        if changed:
            tables["changed"].append(entry)
    for name, rows in tables.items():
        write_csv(folder / (name + ".csv"), rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", type=Path, default=Path("catalogs/NASA/classification_benchmark.json"))
    parser.add_argument("--model", type=Path, default=Path("catalogs/NASA/classification_model.json"))
    parser.add_argument("--previous", type=Path, default=Path("scratch/planet_class_weighting/results.json"))
    parser.add_argument("--output", type=Path, default=Path("scratch/planet_class_refinement"))
    args = parser.parse_args()
    benchmark_bytes = args.benchmark.read_bytes()
    model_bytes = args.model.read_bytes()
    original = json.loads(model_bytes)
    digest = hashlib.sha256(benchmark_bytes).hexdigest()
    if digest != original["benchmark_sha256"]:
        raise ValueError("Benchmark checksum differs from the original input")
    snapshot = json.loads(benchmark_bytes)
    rows = [selected for raw in snapshot["rows"] if (selected := sample(raw)) is not None]
    stored = json.loads(args.previous.read_bytes())
    if stored["benchmark_sha256"] != digest:
        raise ValueError("Previous results use a different benchmark")
    previous = dict(five_fold=stored["variants"]["balanced_metallicity_l2_1"],
        individual_host=stored["leave_one_host_out"]["metallicity"])
    if {row["planet"] for row in rows} != {row["planet"] for row in previous["five_fold"]["predictions"]}:
        raise ValueError("Selected planet identities differ")
    candidates = []
    for strength in STRENGTHS:
        for factor in METAL_FACTORS:
            penalties = [0, strength, strength, strength * factor]
            candidate = dict(id=f"planet_l2_{strength:g}_metal_factor_{factor:g}",
                regularization=penalties, coefficients=fit(rows, 4, True, penalties),
                five_fold=evaluate(rows, 4, True, penalties),
                individual_host=evaluate(rows, 4, True, penalties, leave_one_host_out=True))
            candidate["eligible"] = eligible(candidate, previous)
            candidates.append(candidate)
            groups = candidate["five_fold"]["metrics"]["groups"]
            individual = candidate["individual_host"]["metrics"]["groups"]
            print(candidate["id"], "compact", groups["compact"]["correct"], individual["compact"]["correct"],
                "bulky", groups["bulky"]["correct"], individual["bulky"]["correct"],
                "eligible", candidate["eligible"], flush=True)
    qualified = [candidate for candidate in candidates if candidate["eligible"]]
    if not qualified:
        raise RuntimeError("No candidate improves bulky recall while retaining compact recall and minimum scores")
    selected = max(qualified, key=rank)
    selected["changes"] = {protocol: compare(previous[protocol], selected[protocol])
        for protocol in ("five_fold", "individual_host")}
    report = dict(schema=1, production_model_modified=False, benchmark_sha256=digest,
        production_model_sha256=hashlib.sha256(model_bytes).hexdigest(),
        scope="Exploratory tuning on the same benchmark; external validation remains outstanding",
        feature_order=original["feature_order"], threshold=.5,
        score_interpretation="Class-balanced scores are not calibrated population probabilities",
        class_weights="Equal total training weight for compact and bulky classes within each split",
        selection="Retain every compact classification and minimum compact score in both protocols; improve bulky recall without a protocol declining; rank by worst-protocol bulky recall, then total bulky recall, then individual-host compact margin",
        sample_count=len(rows), candidate_count=len(candidates), selected_id=selected["id"],
        previous=previous, selected=selected, candidates=candidates)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    names = {row["planet"] for row in rows}
    write_tables(args.output, [raw for raw in snapshot["rows"] if raw["pl_name"] in names], previous, selected)
    print("SELECTED", selected["id"], json.dumps(selected["changes"], sort_keys=True), flush=True)
    if args.model.read_bytes() != model_bytes:
        raise RuntimeError("Production model changed during the experiment")


if __name__ == "__main__":
    main()
