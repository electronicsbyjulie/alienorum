#!/usr/bin/env python3
"""Remove stellar metallicity, refit, and compare on the unchanged benchmark."""

import argparse
import hashlib
import json
from pathlib import Path

from calibrate_planet_classifier import sample
from explore_planet_classifier import configurations, complete_candidate, evaluate_config, nested_validation
from refine_planet_class_weights import compare, rank, write_csv


def without_metallicity(config):
    family = config.get("family", "linear")
    penalties = config["regularization"][:3]
    if family in ("mass_metal", "flux_metal", "metal_interactions"):
        family = "linear"
    else:
        penalties += config["regularization"][4:]
    loss = config.get("loss", "logistic")
    return dict(id=f"{family}_{loss}_no_metal_" + "_".join(f"{x:g}" for x in penalties[1:]),
        family=family, loss=loss, include_metallicity=False, regularization=penalties)


def ablated_configurations():
    unique = {}
    for config in configurations():
        ablated = without_metallicity(config)
        unique.setdefault(ablated["id"], ablated)
    return list(unique.values())


def evaluate_both(rows, config):
    return dict(config, five_fold=evaluate_config(rows, config),
        individual_host=evaluate_config(rows, config, individual=True))


def all_compact_correct(candidate):
    return all(candidate[key]["metrics"]["groups"]["compact"]["correct"] ==
        candidate[key]["metrics"]["groups"]["compact"]["count"]
        for key in ("five_fold", "individual_host"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", type=Path, default=Path("catalogs/NASA/classification_benchmark.json"))
    parser.add_argument("--model", type=Path, default=Path("catalogs/NASA/classification_model.json"))
    parser.add_argument("--previous", type=Path, default=Path("scratch/planet_class_exploration/results.json"))
    parser.add_argument("--output", type=Path, default=Path("scratch/planet_metallicity_ablation"))
    args = parser.parse_args()
    model_bytes = args.model.read_bytes()
    benchmark_bytes = args.benchmark.read_bytes()
    previous = json.loads(args.previous.read_bytes())
    digest = hashlib.sha256(benchmark_bytes).hexdigest()
    if digest != previous["benchmark_sha256"] or digest != json.loads(model_bytes)["benchmark_sha256"]:
        raise ValueError("Benchmark checksum differs")
    rows = []
    for raw in json.loads(benchmark_bytes)["rows"]:
        row = sample(raw)
        if row is not None:
            rows.append(dict(row, period=raw["pl_orbper"], stellar_mass=raw["st_mass"]))
    leader = previous["previous"]
    if {row["planet"] for row in rows} != {row["planet"] for row in leader["five_fold"]["predictions"]}:
        raise ValueError("Benchmark identities differ")
    paired = {}
    for name, original in (("leader", leader), ("margin_alternative", previous["margin_alternative"])):
        config = dict(original, family=original.get("family", "linear"))
        # Reproduce the saved predictions before comparing a refitted ablation.
        for protocol, individual in (("five_fold", False), ("individual_host", True)):
            if evaluate_config(rows, config, individual) != original[protocol]:
                raise ValueError("Previous predictions do not reproduce")
        ablated = evaluate_both(rows, without_metallicity(config))
        complete_candidate(ablated, original, rows)
        paired[name] = dict(with_metallicity=original, without_metallicity=ablated)
        print("paired", name, [ablated[key]["metrics"]["groups"]
            for key in ("five_fold", "individual_host")], flush=True)
    configs = ablated_configurations()
    nested = nested_validation(rows, configs)
    candidates = []
    for config in configs:
        candidate = evaluate_both(rows, config)
        candidates.append(candidate)
        print(config["id"], [(candidate[key]["metrics"]["groups"]["compact"]["correct"],
            candidate[key]["metrics"]["groups"]["bulky"]["correct"])
            for key in ("five_fold", "individual_host")], flush=True)
    qualified = [candidate for candidate in candidates if all_compact_correct(candidate)]
    selected = max(qualified, key=rank) if qualified else None
    complete_candidate(selected, leader, rows)
    report = dict(schema=1, benchmark_sha256=digest,
        production_model_sha256=hashlib.sha256(model_bytes).hexdigest(),
        production_model_modified=False, sample_count=len(rows), candidate_count=len(configs),
        scope="Exploratory ablation on the same benchmark; external validation remains outstanding",
        method="Delete Fe/H and all Fe/H interaction terms, then refit each training partition; paired checks retain the other penalties; retuned candidates deduplicate the prior grid after removal",
        threshold=.5, class_weights="Equal total class weights within every training partition",
        score_interpretation="Scores are not calibrated population probabilities",
        selection="Retain all compact predictions in both protocols; maximize worst-protocol bulky recall, then total bulky recall, then individual-host compact margin",
        paired=paired, selected=selected, candidates=candidates,
        nested_with_metallicity=previous["nested"], nested_without_metallicity=nested,
        nested_changes=compare(previous["nested"], nested))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    summary = []
    for name, result in [(f"{name}_{mode}", result) for name, pair in paired.items() for mode, result in pair.items()] + [("retuned_without_metallicity", selected)]:
        if result is None:
            continue
        for protocol in ("five_fold", "individual_host"):
            scores = result[protocol]["metrics"]
            summary.append(dict(model=name, protocol=protocol,
                compact_correct=scores["groups"]["compact"]["correct"],
                bulky_correct=scores["groups"]["bulky"]["correct"],
                balanced_log_loss=scores["balanced_log_loss"]))
    for mode, result in (("with_metallicity", previous["nested"]), ("without_metallicity", nested)):
        scores = result["metrics"]
        summary.append(dict(model="nested_" + mode, protocol="nested_five_fold",
            compact_correct=scores["groups"]["compact"]["correct"],
            bulky_correct=scores["groups"]["bulky"]["correct"],
            balanced_log_loss=scores["balanced_log_loss"]))
    write_csv(args.output / "comparison.csv", summary)
    print("SELECTED", selected["id"] if selected else "none", flush=True)
    print("NESTED", json.dumps(nested["metrics"], sort_keys=True), flush=True)
    if args.model.read_bytes() != model_bytes:
        raise RuntimeError("Production model changed during the experiment")


if __name__ == "__main__":
    main()
