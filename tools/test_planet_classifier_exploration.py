#!/usr/bin/env python3
"""Check nested validation isolation and predictor construction."""

from copy import deepcopy
import unittest

import numpy as np

from experiment_planet_class_weights import squared_hinge_objective
from explore_planet_classifier import FAMILIES, config_rows, configurations, evaluate_config, feature_rows, outer_fold, selection_rank
from ablate_planet_metallicity import ablated_configurations, without_metallicity


class ExplorationTests(unittest.TestCase):
    def setUp(self):
        self.rows = [dict(planet=f"Example {fold} {label}", host=f"Host {fold} {label}",
            fold=fold, label=label, features=[1, .4 + label + fold * .03, 2 - label, -.1],
            temperature=5500, stellar_mass=.9, period=10)
            for fold in range(5) for label in (0, 1)]
        self.configs = [dict(id=f"linear_{strength}", family="linear",
            regularization=[0, strength, strength, 5 * strength]) for strength in (.5, 5)]

    def test_outer_labels_and_predictors_cannot_change_selection_or_fit(self):
        before = outer_fold(self.rows, self.configs, 0)
        changed = deepcopy(self.rows)
        for row in changed:
            if row["fold"] == 0:
                row["label"] = 1 - row["label"]
                row["features"] = [1, 100, -100, 10]
        after = outer_fold(changed, self.configs, 0)
        self.assertEqual(before["selected_config"], after["selected_config"])
        self.assertEqual(before["coefficients"], after["coefficients"])
        self.assertEqual(before["inner_trials"], after["inner_trials"])
        self.assertNotEqual(before["predictions"], after["predictions"])
        held_out = set(before["testing_hosts"])
        for trial in before["inner_trials"]:
            self.assertTrue(held_out.isdisjoint(row["host"] for row in trial["predictions"]))

    def test_host_split_across_outer_partitions_is_rejected(self):
        self.rows[2]["host"] = self.rows[0]["host"]
        with self.assertRaisesRegex(ValueError, "hosts overlap"):
            outer_fold(self.rows, self.configs, 0)

    def test_observed_radius_does_not_enter_predictors(self):
        changed = deepcopy(self.rows)
        for row in changed:
            row["radius"] = 100
            row["density"] = 100
        for family in FAMILIES:
            before = [row["features"] for row in feature_rows(self.rows, family)]
            after = [row["features"] for row in feature_rows(changed, family)]
            self.assertEqual(before, after)

    def test_compact_recall_has_priority_over_bulky_recall(self):
        def result(compact, bulky):
            return dict(metrics=dict(groups=dict(compact=dict(recall=compact),
                bulky=dict(recall=bulky)), balanced_log_loss=.2))
        self.assertGreater(selection_rank(result(1, .9)), selection_rank(result(.75, 1)))

    def test_margin_gradient_matches_finite_differences(self):
        matrix = np.array([[1, -1, .3], [1, .5, -.7], [1, 1, .1]])
        labels = np.array([0, 1, 1])
        weights = np.array([1.5, .75, .75])
        parameters = np.array([.2, -.3, .7])
        regularization = [0, 1.5, 7.5]
        gradient = squared_hinge_objective(parameters, matrix, labels, weights, regularization)[1]
        for dimension in range(len(parameters)):
            delta = np.zeros(len(parameters))
            delta[dimension] = 1e-6
            plus = squared_hinge_objective(parameters + delta, matrix, labels, weights, regularization)[0]
            minus = squared_hinge_objective(parameters - delta, matrix, labels, weights, regularization)[0]
            self.assertAlmostEqual(gradient[dimension], (plus - minus) / 2e-6, places=8)

    def test_ablation_removes_all_metallicity_dependence(self):
        changed = deepcopy(self.rows)
        for row in changed:
            row["features"][3] = float("nan")
        for original in configurations():
            ablated = without_metallicity(original)
            before = config_rows(self.rows, ablated)
            after = config_rows(changed, ablated)
            self.assertEqual([row["features"] for row in before], [row["features"] for row in after])
            self.assertEqual(len(before[0]["features"]), len(ablated["regularization"]))

    def test_ablation_preserves_other_predictors_and_penalties(self):
        original = dict(family="quadratic", regularization=[0, 1, 2, 3, 4, 5, 6])
        ablated = without_metallicity(original)
        self.assertEqual(ablated["regularization"], [0, 1, 2, 4, 5, 6])
        before = config_rows(self.rows, original)[0]["features"]
        after = config_rows(self.rows, ablated)[0]["features"]
        self.assertEqual(after, before[:3] + before[4:])

    def test_ablated_fits_ignore_metallicity(self):
        changed = deepcopy(self.rows)
        for row in changed:
            row["features"][3] = 1000 * (row["label"] + 1)
        config = without_metallicity(self.configs[0])
        self.assertEqual(evaluate_config(self.rows, config), evaluate_config(changed, config))

    def test_ablated_grid_deduplicates_equivalent_models(self):
        configs = ablated_configurations()
        signatures = [(c["family"], c["loss"], tuple(c["regularization"])) for c in configs]
        self.assertEqual(len(signatures), len(set(signatures)))


if __name__ == "__main__":
    unittest.main()
