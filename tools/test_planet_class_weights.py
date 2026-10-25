#!/usr/bin/env python3
"""Numerical and validation checks for the offline class-weight experiment."""

import unittest
from copy import deepcopy

import numpy as np

from experiment_planet_class_weights import evaluate, fit, objective, observation_weights, predict
from refine_planet_class_weights import eligible


class ClassWeightTests(unittest.TestCase):
    def test_groups_have_equal_total_weight(self):
        labels = np.array([0] * 4 + [1] * 172)
        weights = observation_weights(labels, True)
        self.assertAlmostEqual(weights[labels == 0].sum(), 88)
        self.assertAlmostEqual(weights[labels == 1].sum(), 88)
        self.assertAlmostEqual(weights[0] / weights[-1], 43)
        np.testing.assert_array_equal(observation_weights(labels, False), np.ones(176))

    def test_gradient_matches_finite_differences(self):
        matrix = np.array([[1, -1, .3], [1, .5, -.7], [1, 1, .1]])
        labels = np.array([0, 1, 1])
        weights = observation_weights(labels, True)
        parameters = np.array([.2, -.3, .7])
        value, gradient = objective(parameters, matrix, labels, weights, 1)
        self.assertTrue(np.isfinite(value))
        numerical = []
        for dimension in range(len(parameters)):
            delta = np.zeros(len(parameters))
            delta[dimension] = 1e-6
            plus = objective(parameters + delta, matrix, labels, weights, 1)[0]
            minus = objective(parameters - delta, matrix, labels, weights, 1)[0]
            numerical.append((plus - minus) / 2e-6)
        np.testing.assert_allclose(gradient, numerical, atol=1e-8)

    def test_equal_weights_remove_majority_intercept_for_uninformative_features(self):
        rows = [dict(features=[1, 2, 3], label=label) for label in [0, 1, 1, 1, 1]]
        unweighted = fit(rows, 3, False, 1)
        balanced = fit(rows, 3, True, 1)
        self.assertAlmostEqual(predict(unweighted, rows[0]), .8, places=6)
        self.assertAlmostEqual(predict(balanced, rows[0]), .5, places=6)

    def test_separate_coefficient_penalties_have_the_correct_gradient(self):
        matrix = np.array([[1, -1, .3], [1, .5, -.7], [1, 1, .1]])
        labels = np.array([0, 1, 1])
        weights = observation_weights(labels, True)
        parameters = np.array([.2, -.3, .7])
        regularization = [0, 1.5, 7.5]
        gradient = objective(parameters, matrix, labels, weights, regularization)[1]
        for dimension in range(len(parameters)):
            delta = np.zeros(len(parameters))
            delta[dimension] = 1e-6
            plus = objective(parameters + delta, matrix, labels, weights, regularization)[0]
            minus = objective(parameters - delta, matrix, labels, weights, regularization)[0]
            self.assertAlmostEqual(gradient[dimension], (plus - minus) / 2e-6, places=8)

    def test_missing_training_class_is_rejected(self):
        with self.assertRaises(ValueError):
            observation_weights([1, 1, 1], True)

    def test_host_leakage_is_rejected(self):
        rows = [dict(host="Shared host", planet="Example", fold=fold,
            features=[1, fold, 0], label=fold) for fold in [0, 1]]
        with self.assertRaisesRegex(ValueError, "hosts overlap"):
            evaluate(rows, 3, True, 1)


class RefinementSelectionTests(unittest.TestCase):
    def result(self, scores, bulky_correct):
        return dict(metrics=dict(groups=dict(
            compact=dict(count=len(scores), correct=sum(score > .5 for score in scores)),
            bulky=dict(count=172, correct=bulky_correct))),
            predictions=[dict(label=0, bulky_score=1 - score) for score in scores])

    def setUp(self):
        self.previous = dict(five_fold=self.result([.57, .83, .97, .999], 157),
            individual_host=self.result([.518, .83, .97, .999], 158))
        self.candidate = dict(five_fold=self.result([.58, .85, .96, .999], 160),
            individual_host=self.result([.526, .85, .96, .999], 160))

    def test_accepts_improved_bulky_recall_with_compact_recall_and_minimum_scores_retained(self):
        self.assertTrue(eligible(self.candidate, self.previous))

    def test_rejects_losing_a_compact_prediction(self):
        candidate = deepcopy(self.candidate)
        candidate["individual_host"] = self.result([.49, .85, .96, .999], 170)
        self.assertFalse(eligible(candidate, self.previous))

    def test_rejects_an_eroded_minimum_compact_score(self):
        candidate = deepcopy(self.candidate)
        candidate["individual_host"] = self.result([.501, .85, .96, .999], 170)
        self.assertFalse(eligible(candidate, self.previous))

    def test_rejects_a_protocol_with_worse_bulky_recall(self):
        candidate = deepcopy(self.candidate)
        candidate["five_fold"]["metrics"]["groups"]["bulky"]["correct"] = 156
        self.assertFalse(eligible(candidate, self.previous))

    def test_rejects_omitted_compact_examples(self):
        candidate = deepcopy(self.candidate)
        candidate["individual_host"] = self.result([.85, .96, .999], 170)
        self.assertFalse(eligible(candidate, self.previous))


if __name__ == "__main__":
    unittest.main()
