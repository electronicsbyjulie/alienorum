#!/usr/bin/env python3
"""Build the selected class-balanced metallicity prior with host validation.

Development dependencies: NumPy and SciPy; the application reads the fitted JSON.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

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


# Fixed settings selected in the compact-preserving benchmark experiments.
REGULARIZATION = [0, 1.5, 1.5, 7.5]


def calibrate(rows):
    # Local import shares the experiment's solver without a module import cycle.
    from experiment_planet_class_weights import evaluate, fit

    hosts = {row["host"] for row in rows}
    if len(hosts) < 30 or {row["label"] for row in rows} != {0, 1}:
        return dict(schema=2, enabled=False, reason="insufficient_independent_hosts")
    for held_out in range(5):
        training = [row for row in rows if row["fold"] != held_out]
        testing = [row for row in rows if row["fold"] == held_out]
        if not testing or {row["label"] for row in training} != {0, 1}:
            return dict(schema=2, enabled=False, reason="insufficient_host_folds")
    validation = {}
    for name, individual in (("five_fold", False), ("individual_host", True)):
        validation[name] = dict(
            baseline=evaluate(rows, 3, True, REGULARIZATION[:3], individual),
            metallicity=evaluate(rows, 4, True, REGULARIZATION, individual))
    baseline = {row["planet"]: row["bulky_score"]
        for row in validation["five_fold"]["baseline"]["predictions"]}
    predictions = [dict(host=row["host"], planet=row["planet"], fold=row["fold"], label=row["label"],
        baseline=baseline[row["planet"]], metallicity=row["bulky_score"])
        for row in validation["five_fold"]["metallicity"]["predictions"]]
    return dict(schema=2, enabled=True, reason="selected_class_balanced_model",
        training="class_balanced_logistic", decision_threshold=0.5,
        regularization=REGULARIZATION,
        class_weights="Equal total compact and bulky weight in every training partition",
        score_interpretation="Class-balanced scores are not calibrated population probabilities",
        target="Large-radius proxy: R >= 2 Earth radii versus compact R <= 1.4 Earth radii; not a measured composition",
        feature_order=["intercept", "log10_mass_earth", "log10_irradiation_earth", "stellar_Fe_H_dex"],
        baseline_coefficients=fit(rows, 3, True, REGULARIZATION[:3]),
        metallicity_coefficients=fit(rows, 4, True, REGULARIZATION),
        maximum_metallicity_error=0.2, maximum_mass_relative_error=0.35,
        domain=dict(mass_earth=[min(row["mass"] for row in rows), max(row["mass"] for row in rows)],
            irradiation_earth=[min(row["flux"] for row in rows), max(row["flux"] for row in rows)],
            metallicity_dex=[min(row["metal"] for row in rows), max(row["metal"] for row in rows)],
            temperature_k=[4900, 6300]),
        sample_count=len(rows), host_count=len(hosts),
        class_counts=dict(compact=sum(row["label"] == 0 for row in rows),
            bulky=sum(row["label"] == 1 for row in rows)),
        validation="Five host-grouped folds and individual-host exclusion; weights and scaling refit in each training partition",
        validation_scope="Settings selected on this benchmark; independent external validation remains outstanding",
        validation_results=validation, held_out_predictions=predictions, solar_system_in_training=False)


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
    if not model["enabled"]:
        raise ValueError("Calibration input lacks sufficient host/class coverage; preserving the installed model")
    model["benchmark_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    model["selection"] = "Measured mass and radius with finite errors; mass error <=35%; radius error <=15%; Fe/H error <=0.2 dex; dwarf logg 4.0-4.7; compact or extended radius labels"
    output_path = Path("catalogs/NASA/classification_model.json")
    temporary = output_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(model, indent=4, allow_nan=False) + "\n")
    temporary.replace(output_path)
    print("Calibration:", model.get("sample_count", len(rows)), "planets;", model.get("host_count", 0), "hosts; enabled:", model["enabled"], flush=True)
    for name, result in model.get("validation_results", {}).items():
        print(name, result["metallicity"]["metrics"], flush=True)


if __name__ == "__main__":
    main()
