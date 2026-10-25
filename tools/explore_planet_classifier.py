#!/usr/bin/env python3
"""Explore predictor families and independent penalties with nested host validation.

This offline tool preserves the benchmark, equal class weights, and 0.5 threshold.
Radius supplies labels only. It never installs a model in the application.
"""

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path

from calibrate_planet_classifier import sample
from experiment_planet_class_weights import evaluate, fit, metrics, predict
from refine_planet_class_weights import compare, eligible, minimum_compact_score, rank, write_csv, write_tables


FAMILIES = ("linear", "temperature", "stellar_mass", "period", "mass_flux",
    "mass_curve", "flux_curve", "quadratic", "mass_metal", "flux_metal", "metal_interactions")


def configurations():
    candidates = []
    for mass, flux, metal in itertools.product((.25, .5, 1.5, 5, 15),
        (.25, .5, 1.5, 5, 15), (.5, 2.5, 7.5, 25, 75)):
        candidates.append(dict(id=f"linear_{mass:g}_{flux:g}_{metal:g}",
            family="linear", regularization=[0, mass, flux, metal]))
    for family in FAMILIES[1:8]:
        for strength in (.5, 1.5, 5):
            extras = 3 if family == "quadratic" else 1
            candidates.append(dict(id=f"{family}_{strength:g}", family=family,
                regularization=[0, strength, strength, 5 * strength] + [5 * strength] * extras))
    for family, strength, factor in itertools.product(FAMILIES[8:], (.5, 1.5, 5), (1, 5, 25)):
        extras = 2 if family == "metal_interactions" else 1
        candidates.append(dict(id=f"{family}_{strength:g}_{factor:g}", family=family,
            regularization=[0, strength, strength, 5 * strength] + [factor * strength] * extras))
    for strength, factor in itertools.product((.1, .5, 1.5, 5, 15), (1, 5, 25)):
        candidates.append(dict(id=f"squared_hinge_{strength:g}_{factor:g}", family="linear",
            loss="squared_hinge", regularization=[0, strength, strength, factor * strength]))
    return candidates


def feature_rows(rows, family, include_metallicity=True):
    if family not in FAMILIES:
        raise ValueError("Unknown feature family")
    if not include_metallicity and family in ("mass_metal", "flux_metal", "metal_interactions"):
        raise ValueError("Metallicity interactions require the metallicity predictor")
    result = []
    for row in rows:
        mass, flux = row["features"][1:3]
        extras = []
        if family == "temperature":
            extras = [math.log10(row["temperature"] / 5772)]
        elif family == "stellar_mass":
            extras = [math.log10(row["stellar_mass"])]
        elif family == "period":
            extras = [math.log10(row["period"])]
        elif family == "mass_flux":
            extras = [mass * flux]
        elif family == "mass_curve":
            extras = [mass * mass]
        elif family == "flux_curve":
            extras = [flux * flux]
        elif family == "quadratic":
            extras = [mass * mass, flux * flux, mass * flux]
        elif family == "mass_metal":
            extras = [mass * row["features"][3]]
        elif family == "flux_metal":
            extras = [flux * row["features"][3]]
        elif family == "metal_interactions":
            extras = [mass * row["features"][3], flux * row["features"][3]]
        base = row["features"] if include_metallicity else row["features"][:3]
        result.append(dict(row, features=base + extras))
    return result


def config_rows(rows, config):
    return feature_rows(rows, config["family"], config.get("include_metallicity", True))


def evaluate_config(rows, config, individual=False):
    features = config_rows(rows, config)
    return evaluate(features, len(config["regularization"]), True,
        config["regularization"], leave_one_host_out=individual, loss=config.get("loss", "logistic"))


def selection_rank(result):
    scores = result["metrics"]
    return (scores["groups"]["compact"]["recall"],
        scores["groups"]["bulky"]["recall"], -scores["balanced_log_loss"])


def outer_fold(rows, configs, split):
    training = [row for row in rows if row["fold"] != split]
    testing = [row for row in rows if row["fold"] == split]
    if {row["host"] for row in training} & {row["host"] for row in testing}:
        raise ValueError("Training and evaluation hosts overlap")
    # All candidate selection, scaling, and weighting use outer training data only.
    trials = [dict(config=config, **evaluate_config(training, config)) for config in configs]
    chosen = max(trials, key=selection_rank)
    config = chosen["config"]
    dimensions = len(config["regularization"])
    coefficients = fit(config_rows(training, config), dimensions, True,
        config["regularization"], loss=config.get("loss", "logistic"))
    predictions = [dict(planet=row["planet"], host=row["host"], label=row["label"],
        fold=split, bulky_score=predict(coefficients, row))
        for row in config_rows(testing, config)]
    return dict(split=split, selected_config=config, coefficients=coefficients,
        training_hosts=sorted({row["host"] for row in training}),
        testing_hosts=sorted({row["host"] for row in testing}),
        inner_trials=trials, predictions=predictions)


def nested_validation(rows, configs):
    folds = []
    for split in sorted({row["fold"] for row in rows}):
        result = outer_fold(rows, configs, split)
        folds.append(result)
        print("nested fold", split, result["selected_config"]["id"], flush=True)
    predictions = [row for fold in folds for row in fold["predictions"]]
    return dict(metrics=metrics(predictions), predictions=predictions, folds=folds)


def margin_alternative(candidate, previous):
    # Compare logistic scores only; the margin objective has a different score scale.
    if candidate.get("loss", "logistic") != "logistic":
        return False
    for protocol in ("five_fold", "individual_host"):
        current, old = candidate[protocol], previous[protocol]
        groups, old_groups = current["metrics"]["groups"], old["metrics"]["groups"]
        if any(groups[key]["count"] != old_groups[key]["count"] for key in ("compact", "bulky")):
            return False
        if groups["compact"]["correct"] != groups["compact"]["count"]:
            return False
        if groups["bulky"]["correct"] < old_groups["bulky"]["correct"]:
            return False
        if minimum_compact_score(current) <= minimum_compact_score(old):
            return False
        if current["metrics"]["balanced_log_loss"] >= old["metrics"]["balanced_log_loss"]:
            return False
    return True


def complete_candidate(candidate, previous, rows):
    if candidate is None:
        return
    candidate["coefficients"] = fit(config_rows(rows, candidate),
        len(candidate["regularization"]), True, candidate["regularization"],
        loss=candidate.get("loss", "logistic"))
    candidate["changes"] = {key: compare(previous[key], candidate[key])
        for key in ("five_fold", "individual_host")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", type=Path, default=Path("catalogs/NASA/classification_benchmark.json"))
    parser.add_argument("--model", type=Path, default=Path("catalogs/NASA/classification_model.json"))
    parser.add_argument("--previous", type=Path, default=Path("scratch/planet_class_refinement/results.json"))
    parser.add_argument("--output", type=Path, default=Path("scratch/planet_class_exploration"))
    args = parser.parse_args()
    benchmark_bytes = args.benchmark.read_bytes()
    model_bytes = args.model.read_bytes()
    original = json.loads(model_bytes)
    stored = json.loads(args.previous.read_bytes())
    digest = hashlib.sha256(benchmark_bytes).hexdigest()
    if digest != original["benchmark_sha256"] or digest != stored["benchmark_sha256"]:
        raise ValueError("Benchmark checksum differs")
    snapshot = json.loads(benchmark_bytes)
    rows = []
    for raw in snapshot["rows"]:
        row = sample(raw)
        if row is None:
            continue
        for key in ("pl_orbper", "st_mass"):
            if raw.get(key) is None or not math.isfinite(raw[key]) or raw[key] <= 0:
                raise ValueError("Additional predictors must cover every selected example")
        rows.append(dict(row, period=raw["pl_orbper"], stellar_mass=raw["st_mass"]))
    previous = stored["selected"]
    if {row["planet"] for row in rows} != {row["planet"] for row in previous["five_fold"]["predictions"]}:
        raise ValueError("Benchmark identities differ")
    configs = configurations()
    # Run the fixed selection procedure before inspecting full-benchmark winners.
    nested = nested_validation(rows, configs)
    candidates = []
    for config in configs:
        candidate = dict(config, five_fold=evaluate_config(rows, config),
            individual_host=evaluate_config(rows, config, individual=True))
        candidate["eligible"] = eligible(candidate, previous)
        candidates.append(candidate)
        print(config["id"], "eligible", candidate["eligible"],
            "counts", [(candidate[key]["metrics"]["groups"]["compact"]["correct"],
                candidate[key]["metrics"]["groups"]["bulky"]["correct"])
                for key in ("five_fold", "individual_host")], flush=True)
    qualified = [candidate for candidate in candidates if candidate["eligible"]]
    selected = max(qualified, key=rank) if qualified else None
    alternatives = [candidate for candidate in candidates if margin_alternative(candidate, previous)]
    alternative = max(alternatives, key=rank) if alternatives else None
    complete_candidate(selected, previous, rows)
    complete_candidate(alternative, previous, rows)
    ablation = None
    if alternative and alternative["family"] == "linear":
        ablation = {key: evaluate(rows, 3, True, alternative["regularization"][:3],
            leave_one_host_out=individual) for key, individual in
            (("five_fold", False), ("individual_host", True))}
    report = dict(schema=1, benchmark_sha256=digest,
        production_model_sha256=hashlib.sha256(model_bytes).hexdigest(),
        production_model_modified=False, sample_count=len(rows), candidate_count=len(configs),
        threshold=.5, class_weights="Equal total class weight inside every training partition",
        scope="Exploratory reuse of the benchmark; nested folds isolate parameter selection but do not supply external validation",
        nested_selection="Maximize inner compact recall, then bulky recall, then minimize balanced log loss; original host folds within outer training data",
        exploratory_selection="Preserve all compact classifications and minimum compact scores in both protocols; improve bulky recall without either protocol declining",
        score_interpretation="Class-balanced scores are not calibrated population probabilities; squared hinge margins use a logistic link for a common 0.5 decision boundary",
        feature_order=["intercept", "log10_mass_earth", "log10_flux_earth", "Fe_H_dex"],
        feature_families=list(FAMILIES), previous=previous, nested=nested,
        selected=selected, margin_alternative=alternative, alternative_without_metallicity=ablation,
        candidates=candidates)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    if selected or alternative:
        names = {row["planet"] for row in rows}
        write_tables(args.output, [raw for raw in snapshot["rows"] if raw["pl_name"] in names],
            previous, selected or alternative)
    write_csv(args.output / "nested_predictions.csv", nested["predictions"])
    print("SELECTED", selected["id"] if selected else "none", flush=True)
    print("MARGIN ALTERNATIVE", alternative["id"] if alternative else "none", flush=True)
    print("NESTED", json.dumps(nested["metrics"], sort_keys=True), flush=True)
    if args.model.read_bytes() != model_bytes:
        raise RuntimeError("Production model changed during the experiment")


if __name__ == "__main__":
    main()
