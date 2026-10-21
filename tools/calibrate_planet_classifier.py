#!/usr/bin/env python3
"""Validate a host-metallicity contribution to a compact versus large-radius prior."""

import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import random
import statistics

from import_stellar_abundances import write_json
from update_stellar_metallicity import NASA_URL, fetch, finite


QUERY = ("select pl_name,hostname,pl_orbper,pl_eqt,st_mass,pl_bmasse,pl_bmasseerr1,pl_bmasseerr2,pl_bmasselim,pl_bmassprov,"
         "pl_rade,pl_radeerr1,pl_radeerr2,pl_radelim,pl_insol,pl_orbsmax,st_lum,"
         "st_teff,st_logg,st_spectype,st_met,st_meterr1,st_meterr2,st_metlim,st_metratio "
         "from pscomppars order by pl_name")


def fold(host):
    return int.from_bytes(hashlib.sha256(host.encode()).digest()[:4], "big") % 5


def sample(row):
    mass, radius, metal = row.get("pl_bmasse"), row.get("pl_rade"), row.get("st_met")
    temperature, gravity = row.get("st_teff"), row.get("st_logg")
    if not all(finite(value) for value in (mass, radius, metal, temperature, gravity)):
        return None
    if row.get("pl_bmassprov") != "Mass" or row.get("st_metratio") != "[Fe/H]":
        return None
    if any(row.get(key) not in (0, None) for key in ("pl_bmasselim", "pl_radelim", "st_metlim")):
        return None
    if not (1 <= mass <= 30 and 0.5 <= radius <= 6 and -0.5 <= metal <= 0.5 and
            4900 <= temperature <= 6300 and 4.0 <= gravity <= 4.7):
        return None
    if not (radius <= 1.4 or radius >= 2.0):
        return None
    for key, value, relative in (("pl_bmasse", mass, 0.35), ("pl_rade", radius, 0.15), ("st_met", 1.0, 0.2)):
        errors = [row.get(key + suffix) for suffix in ("err1", "err2")]
        if not all(finite(error) for error in errors) or max(abs(error) for error in errors) > relative * value:
            return None
    flux = row.get("pl_insol")
    if not finite(flux) or flux <= 0:
        luminosity, distance = row.get("st_lum"), row.get("pl_orbsmax")
        if not finite(luminosity) or not finite(distance) or distance <= 0:
            return None
        flux = 10 ** luminosity / distance ** 2
    if not 0.1 <= flux <= 3000:
        return None
    density = 5.514 * mass / radius ** 3
    if not 0.2 <= density <= 15:
        return None
    return dict(host=row["hostname"], planet=row["pl_name"], fold=fold(row["hostname"]),
                features=[1.0, math.log10(mass), math.log10(flux), metal], label=int(radius >= 2.0),
                mass=mass, flux=flux, metal=metal, temperature=temperature)


def probability(coefficients, features):
    score = sum(weight * value for weight, value in zip(coefficients, features))
    return 1.0 / (1.0 + math.exp(-max(-40, min(40, score))))


def fit(rows, dimensions):
    means = [0.0] + [statistics.mean(row["features"][i] for row in rows) for i in range(1, dimensions)]
    scales = [1.0] + [max(1e-6, statistics.pstdev(row["features"][i] for row in rows)) for i in range(1, dimensions)]
    matrix = [[1.0] + [(row["features"][i] - means[i]) / scales[i] for i in range(1, dimensions)] for row in rows]
    mean_label = max(0.001, min(0.999, statistics.mean(row["label"] for row in rows)))
    weights = [math.log(mean_label / (1 - mean_label))] + [0.0] * (dimensions - 1)
    for iteration in range(5000):
        gradient = [0.0] * dimensions
        for row, features in zip(rows, matrix):
            residual = probability(weights, features) - row["label"]
            for i in range(dimensions):
                gradient[i] += residual * features[i]
        for i in range(dimensions):
            gradient[i] = (gradient[i] + (weights[i] if i else 0.0)) / len(rows)
            weights[i] -= 0.15 * gradient[i]
        if max(abs(value) for value in gradient) < 1e-7:
            break
    coefficients = [weights[i] / scales[i] for i in range(dimensions)]
    coefficients[0] -= sum(weights[i] * means[i] / scales[i] for i in range(1, dimensions))
    return coefficients


def loss(label, prediction):
    prediction = max(1e-12, min(1 - 1e-12, prediction))
    return -math.log(prediction if label else 1 - prediction)


def calibrate(rows):
    if len(set(row["host"] for row in rows)) < 30 or len(set(row["label"] for row in rows)) < 2:
        return dict(schema=1, enabled=False, reason="insufficient_independent_hosts")
    predictions = []
    for held_out in range(5):
        training = [row for row in rows if row["fold"] != held_out]
        testing = [row for row in rows if row["fold"] == held_out]
        if not training or not testing:
            return dict(schema=1, enabled=False, reason="insufficient_host_folds")
        baseline, metal = fit(training, 3), fit(training, 4)
        for row in testing:
            predictions.append(dict(host=row["host"], planet=row["planet"], fold=held_out, label=row["label"],
                baseline=probability(baseline, row["features"]), metallicity=probability(metal, row["features"])))
    clusters = defaultdict(list)
    for row in predictions:
        clusters[row["host"]].append(loss(row["label"], row["baseline"]) - loss(row["label"], row["metallicity"]))
    generator = random.Random(8403)
    hosts = sorted(clusters)
    resampled = []
    for iteration in range(2000):
        differences = [value for host in generator.choices(hosts, k=len(hosts)) for value in clusters[host]]
        resampled.append(statistics.mean(differences))
    resampled.sort()
    interval = [resampled[49], resampled[1949]]
    improvement = statistics.mean(value for cluster in clusters.values() for value in cluster)
    metrics = {}
    for key in ("baseline", "metallicity"):
        metrics[key] = dict(log_loss=statistics.mean(loss(row["label"], row[key]) for row in predictions),
                            brier=statistics.mean((row[key] - row["label"]) ** 2 for row in predictions),
                            accuracy=statistics.mean((row[key] >= 0.5) == bool(row["label"]) for row in predictions))
    return dict(schema=1, enabled=interval[0] > 0, reason="host_cluster_bootstrap_gate",
        target="Large-radius proxy: R >= 2 Earth radii versus compact R <= 1.4 Earth radii; not a measured composition",
        feature_order=["intercept", "log10_mass_earth", "log10_irradiation_earth", "stellar_Fe_H_dex"],
        baseline_coefficients=fit(rows, 3), metallicity_coefficients=fit(rows, 4),
        maximum_metallicity_error=0.2, maximum_mass_relative_error=0.35,
        domain=dict(mass_earth=[min(row["mass"] for row in rows), max(row["mass"] for row in rows)],
                    irradiation_earth=[min(row["flux"] for row in rows), max(row["flux"] for row in rows)],
                    metallicity_dex=[min(row["metal"] for row in rows), max(row["metal"] for row in rows)],
                    temperature_k=[4900, 6300]),
        sample_count=len(rows), host_count=len(hosts), metrics=metrics,
        mean_log_loss_improvement=improvement, improvement_95_percent_host_bootstrap=interval,
        validation="Five folds grouped by SHA256(hostname); coefficients refit within every training fold",
        solar_system_in_training=False, held_out_predictions=predictions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refresh", action="store_true")
    arguments = parser.parse_args()
    path = Path("catalogs/NASA/classification_benchmark.json")
    if path.exists() and not arguments.refresh:
        snapshot = json.loads(path.read_text())
    else:
        snapshot = dict(schema=1, endpoint=NASA_URL, query=QUERY, rows=fetch(NASA_URL, QUERY))
        write_json(path, snapshot)
    rows = [selected for row in snapshot["rows"] if (selected := sample(row)) is not None]
    model = calibrate(rows)
    model["benchmark_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    model["selection"] = "Measured mass and radius with finite errors; mass error <=35%; radius error <=15%; Fe/H error <=0.2 dex; dwarf logg 4.0-4.7; compact or extended radius labels"
    write_json(Path("catalogs/NASA/classification_model.json"), model)
    print("Calibration:", model.get("sample_count", len(rows)), "planets;", model.get("host_count", 0), "hosts; enabled:", model["enabled"], flush=True)
    print("Validation:", model.get("metrics"), "; improvement interval:", model.get("improvement_95_percent_host_bootstrap"), flush=True)


if __name__ == "__main__":
    main()
