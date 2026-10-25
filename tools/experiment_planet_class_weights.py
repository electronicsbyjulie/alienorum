#!/usr/bin/env python3
"""Compare class-balanced planet fits without changing the application's model.

Development dependencies: NumPy and SciPy. Radius supplies the benchmark label
only; prediction features are mass, irradiation, and optionally stellar Fe/H.
"""

import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.optimize import minimize
from scipy.special import expit

from calibrate_planet_classifier import sample


def observation_weights(labels, balanced):
    labels = np.asarray(labels, dtype=int)
    counts = np.bincount(labels, minlength=2)
    if np.any(counts == 0):
        raise ValueError("Every training split must contain both classes")
    return len(labels) / (2.0 * counts[labels]) if balanced else np.ones(len(labels))


def objective(parameters, matrix, labels, weights, regularization):
    scores = matrix @ parameters
    penalty = parameters.copy()
    penalty[0] = 0
    regularization = np.asarray(regularization)
    value = (np.sum(weights * (np.logaddexp(0, scores) - labels * scores))
        + 0.5 * np.dot(regularization * penalty, penalty)) / len(labels)
    gradient = (matrix.T @ (weights * (expit(scores) - labels))
        + regularization * penalty) / len(labels)
    return value, gradient


def squared_hinge_objective(parameters, matrix, labels, weights, regularization):
    targets = 2 * labels - 1
    residuals = np.maximum(0, 1 - targets * (matrix @ parameters))
    penalty = parameters.copy()
    penalty[0] = 0
    regularization = np.asarray(regularization)
    value = (np.sum(weights * residuals * residuals)
        + .5 * np.dot(regularization * penalty, penalty)) / len(labels)
    gradient = (-2 * matrix.T @ (weights * targets * residuals)
        + regularization * penalty) / len(labels)
    return value, gradient


def fit(rows, dimensions, balanced, regularization, loss="logistic"):
    if loss not in ("logistic", "squared_hinge"):
        raise ValueError("Unknown fitting objective")
    matrix = np.array([row["features"][:dimensions] for row in rows])
    labels = np.array([row["label"] for row in rows])
    means = matrix.mean(axis=0)
    scales = np.maximum(1e-6, matrix.std(axis=0))
    means[0], scales[0] = 0, 1
    matrix = (matrix - means) / scales
    weights = observation_weights(labels, balanced)
    function = objective if loss == "logistic" else squared_hinge_objective
    result = minimize(function, np.zeros(dimensions),
        args=(matrix, labels, weights, regularization), jac=True, method="L-BFGS-B",
        options={"gtol": 1e-10, "ftol": 1e-13, "maxiter": 5000, "maxls": 100})
    # A line search can stall at a stationary point because of floating-point rounding.
    stationary = np.all(np.isfinite(result.jac)) and np.max(np.abs(result.jac)) <= 1e-8
    if not np.all(np.isfinite(result.x)) or (not result.success and not stationary):
        raise RuntimeError(str(result.message))
    coefficients = result.x / scales
    coefficients[0] -= np.sum(result.x[1:] * means[1:] / scales[1:])
    return coefficients.tolist()


def predict(coefficients, row):
    return float(expit(np.dot(coefficients, row["features"][:len(coefficients)])))


def metrics(predictions):
    groups = {}
    for label, name in ((0, "compact"), (1, "bulky")):
        rows = [row for row in predictions if row["label"] == label]
        if not rows:
            raise ValueError("Evaluation requires both classes")
        scores = np.clip([row["bulky_score"] for row in rows], 1e-12, 1 - 1e-12)
        correct = sum(int(row["bulky_score"] >= 0.5) == label for row in rows)
        groups[name] = dict(count=len(rows), correct=correct, recall=correct / len(rows),
            log_loss=float(-np.mean(np.log(scores if label else 1 - scores))))
    return dict(groups=groups,
        accuracy=sum(group["correct"] for group in groups.values()) / len(predictions),
        balanced_accuracy=sum(group["recall"] for group in groups.values()) / 2,
        balanced_log_loss=sum(group["log_loss"] for group in groups.values()) / 2,
        misclassified=[row["planet"] for row in predictions
            if int(row["bulky_score"] >= 0.5) != row["label"]])


def evaluate(rows, dimensions, balanced, regularization, leave_one_host_out=False, loss="logistic"):
    splits = sorted({row["host"] if leave_one_host_out else row["fold"] for row in rows})
    predictions = []
    training_counts = []
    for split in splits:
        training = [row for row in rows
            if (row["host"] if leave_one_host_out else row["fold"]) != split]
        testing = [row for row in rows
            if (row["host"] if leave_one_host_out else row["fold"]) == split]
        if {row["host"] for row in training} & {row["host"] for row in testing}:
            raise ValueError("Training and evaluation hosts overlap")
        coefficients = fit(training, dimensions, balanced, regularization, loss=loss)
        counts = Counter(row["label"] for row in training)
        training_counts.append(dict(split=split, compact=counts[0], bulky=counts[1]))
        for row in testing:
            predictions.append(dict(planet=row["planet"], host=row["host"], label=row["label"],
                fold=split, bulky_score=predict(coefficients, row)))
    return dict(metrics=metrics(predictions), predictions=predictions, training_counts=training_counts)


def experiment(snapshot, original):
    rows = [selected for raw in snapshot["rows"] if (selected := sample(raw)) is not None]
    original_names = {row["planet"] for row in original["held_out_predictions"]}
    if original_names != {row["planet"] for row in rows}:
        raise ValueError("Selected planets differ from the original benchmark")
    variants = {}
    for balanced, strengths in ((False, (1.0,)), (True, (0.01, 0.1, 1.0, 10.0, 100.0))):
        for strength in strengths:
            for dimensions, feature_set in ((3, "baseline"), (4, "metallicity")):
                key = f"{'balanced' if balanced else 'unweighted'}_{feature_set}_l2_{strength:g}"
                variants[key] = dict(balanced=balanced, regularization=strength,
                    feature_set=feature_set,
                    coefficients=fit(rows, dimensions, balanced, strength),
                    **evaluate(rows, dimensions, balanced, strength))
    host_checks = {}
    for dimensions, feature_set in ((3, "baseline"), (4, "metallicity")):
        host_checks[feature_set] = evaluate(rows, dimensions, True, 1.0, leave_one_host_out=True)
    original_metrics = {}
    for key in ("baseline", "metallicity"):
        original_metrics[key] = metrics([dict(row, bulky_score=row[key])
            for row in original["held_out_predictions"]])
    return dict(schema=1, production_model_modified=False,
        threshold=0.5, score_interpretation="Class-balanced scores are not calibrated population probabilities",
        feature_order=original["feature_order"],
        training="L2 logistic regression; same objective and solver for weighted and unweighted fits; unpenalized intercept",
        validation="Original five host-grouped folds; weights and scaling calculated inside each training split",
        scope="Exploratory benchmark comparison; no independent external validation set",
        primary_regularization=1.0,
        sensitivity="Exploratory L2 grid 0.01, 0.1, 1, 10, 100; every result retained",
        sample_count=len(rows), host_count=len({row["host"] for row in rows}),
        class_counts=dict(compact=sum(row["label"] == 0 for row in rows),
            bulky=sum(row["label"] == 1 for row in rows)),
        selected_inputs=[raw for raw in snapshot["rows"] if raw["pl_name"] in original_names],
        original_metrics=original_metrics, variants=variants, leave_one_host_out=host_checks)


def write_tables(folder, report, original):
    raw = {row["pl_name"]: row for row in report["selected_inputs"]}
    old = {row["planet"]: row for row in original["held_out_predictions"]}
    predictions = {key: {row["planet"]: row for row in report["variants"][key]["predictions"]}
        for key in ("balanced_baseline_l2_1", "balanced_metallicity_l2_1")}
    individual = {row["planet"]: row for row in report["leave_one_host_out"]["metallicity"]["predictions"]}
    tables = {"compact_predictions.csv": [], "bulky_predictions.csv": [], "bulky_misclassified.csv": []}
    for name in sorted(raw):
        row = raw[name]
        metal = predictions["balanced_metallicity_l2_1"][name]
        base = predictions["balanced_baseline_l2_1"][name]
        entry = dict(planet=name, host=metal["host"],
            observed_group="bulky" if metal["label"] else "compact",
            radius_earth=row["pl_rade"], mass_earth=row["pl_bmasse"], Fe_H_dex=row["st_met"],
            original_metallicity_compact_score=1 - old[name]["metallicity"],
            balanced_baseline_compact_score=1 - base["bulky_score"],
            balanced_metallicity_compact_score=1 - metal["bulky_score"],
            prediction="bulky" if metal["bulky_score"] >= .5 else "compact",
            correct=int(metal["bulky_score"] >= .5) == metal["label"],
            individual_host_compact_score=1 - individual[name]["bulky_score"],
            individual_host_prediction="bulky" if individual[name]["bulky_score"] >= .5 else "compact")
        tables["bulky_predictions.csv" if metal["label"] else "compact_predictions.csv"].append(entry)
        if metal["label"] and not entry["correct"]:
            tables["bulky_misclassified.csv"].append(entry)
    for name, entries in tables.items():
        if entries:
            with (folder / name).open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=list(entries[0]))
                writer.writeheader()
                writer.writerows(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", type=Path, default=Path("catalogs/NASA/classification_benchmark.json"))
    parser.add_argument("--model", type=Path, default=Path("catalogs/NASA/classification_model.json"))
    parser.add_argument("--output", type=Path, default=Path("scratch/planet_class_weighting"))
    args = parser.parse_args()
    snapshot_bytes = args.benchmark.read_bytes()
    model_bytes = args.model.read_bytes()
    original = json.loads(model_bytes)
    digest = hashlib.sha256(snapshot_bytes).hexdigest()
    if digest != original["benchmark_sha256"]:
        raise ValueError("Benchmark checksum differs from the original calibration input")
    report = experiment(json.loads(snapshot_bytes), original)
    report["benchmark_sha256"] = digest
    report["production_model_sha256"] = hashlib.sha256(model_bytes).hexdigest()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    write_tables(args.output, report, original)
    for name, variant in report["variants"].items():
        print(name, json.dumps(variant["metrics"], sort_keys=True))
    for name, result in report["leave_one_host_out"].items():
        print("leave_one_host_out", name, json.dumps(result["metrics"], sort_keys=True))
    if args.model.read_bytes() != model_bytes:
        raise RuntimeError("Production model changed during the experiment")


if __name__ == "__main__":
    main()
