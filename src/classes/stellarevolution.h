#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

namespace alienorum
{
    struct StellarEvolutionFit
    {
        bool valid = false;
        double age = 0, lower = 0, upper = 0, mass = 0, initial_mass = 0, distance = 0;
        int phase = 0;
    };

    class StellarEvolutionGrid
    {
        struct Sample
        {
            std::array<double, 3> position;
            double mass, initial, age, lower, upper;
            int phase;
        };
        struct Node { int sample, left = -1, right = -1, axis; };
        std::vector<Sample> samples;
        std::vector<Node> nodes;
        std::array<int, 6> roots{{-1, -1, -1, -1, -1, -1}};

        int build(std::vector<int>& indices, int first, int last, int depth)
        {
            if (first >= last) return -1;
            const int middle = (first + last) / 2, axis = depth % 3;
            std::nth_element(indices.begin() + first, indices.begin() + middle, indices.begin() + last,
                [&](int a, int b) { return samples[a].position[axis] < samples[b].position[axis]; });
            const int node = nodes.size();
            nodes.push_back({indices[middle], -1, -1, axis});
            const int left = build(indices, first, middle, depth + 1);
            const int right = build(indices, middle + 1, last, depth + 1);
            nodes[node].left = left;
            nodes[node].right = right;
            return node;
        }

        using Neighbors = std::priority_queue<std::pair<double, int>>;
        void nearest(int node, const std::array<double, 3>& query, Neighbors& result) const
        {
            if (node < 0) return;
            const Node& current = nodes[node];
            const Sample& sample = samples[current.sample];
            double distance = 0;
            for (int axis = 0; axis < 3; ++axis)
                distance += std::pow(query[axis] - sample.position[axis], 2);
            if (result.size() < 64 || distance < result.top().first)
            {
                result.emplace(distance, current.sample);
                if (result.size() > 64) result.pop();
            }
            const double delta = query[current.axis] - sample.position[current.axis];
            nearest(delta < 0 ? current.left : current.right, query, result);
            if (result.size() < 64 || delta * delta <= result.top().first)
                nearest(delta < 0 ? current.right : current.left, query, result);
        }

        static double quantile(std::vector<std::pair<double, double>> values, double fraction)
        {
            std::sort(values.begin(), values.end());
            double total = 0;
            for (const auto& value : values) total += value.second;
            double accumulated = 0;
            for (const auto& value : values)
            {
                accumulated += value.second;
                if (accumulated >= fraction * total) return value.first;
            }
            return values.back().first;
        }

        StellarEvolutionGrid()
        {
            std::ifstream input("catalogs/stellar_evolution.dat");
            std::string line;
            std::array<std::vector<int>, 6> groups;
            while (std::getline(input, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream row(line);
                Sample sample;
                double metallicity, temperature, luminosity;
                if (!(row >> metallicity >> sample.phase >> temperature >> luminosity >> sample.mass
                    >> sample.initial >> sample.age >> sample.lower >> sample.upper)) continue;
                if (!(sample.mass > 0) || !(sample.initial > 0) || !std::isfinite(sample.age)) continue;
                sample.position = {temperature / .035, luminosity / .20, metallicity / .40};
                const int index = samples.size();
                samples.push_back(sample);
                if (sample.phase < 100) groups[0].push_back(index);
                if (sample.phase == 0) groups[1].push_back(index);
                if (sample.phase > 0 && sample.phase < 100) groups[2].push_back(index);
                if (sample.phase == 101) groups[3].push_back(index);
                if (sample.phase == 102) groups[4].push_back(index);
                if (sample.phase >= 100) groups[5].push_back(index);
            }
            for (int group = 0; group < 6; ++group)
                roots[group] = build(groups[group], 0, groups[group].size(), 0);
        }

        public:
        static const StellarEvolutionGrid& instance()
        {
            static const StellarEvolutionGrid grid;
            return grid;
        }

        StellarEvolutionFit fit(double log_temperature, double log_luminosity,
            double metallicity, int group, double supplied_mass = 0, double supplied_age = -1) const
        {
            StellarEvolutionFit fit;
            if (!std::isfinite(log_temperature) || !std::isfinite(log_luminosity)
                || !std::isfinite(metallicity) || group < 0 || group >= 6) return fit;
            Neighbors neighbors;
            nearest(roots[group], {log_temperature / .035, log_luminosity / .20, metallicity / .40}, neighbors);
            if (neighbors.empty()) return fit;
            std::vector<std::pair<double, int>> matches;
            while (!neighbors.empty())
            {
                matches.push_back(neighbors.top());
                neighbors.pop();
            }
            const double minimum = matches.back().first;
            std::vector<std::pair<double, double>> ages, lower, upper, masses, initial;
            std::vector<double> weights;
            std::map<int, double> phases;
            for (const auto& match : matches)
            {
                const Sample& sample = samples[match.second];
                // Constant star formation in linear time, with a Salpeter initial-mass prior.
                // Cooling sequences are interpolated at the adopted remnant mass instead;
                // their adaptive time sampling is not a population age distribution.
                const double mass_penalty = supplied_mass > 0
                    ? std::pow((log10(sample.mass) - log10(supplied_mass)) / (group >= 3 ? .04 : .30), 2) : 0;
                const double age_penalty = supplied_age >= 0
                    ? std::pow((sample.age - 9 - log10(std::max(.0001, supplied_age))) / .30, 2) : 0;
                const double prior = sample.phase < 100
                    ? std::log(10.0) * (sample.age - 9) - 2.35 * std::log(sample.initial) : 0;
                weights.push_back(-.5 * (match.first - minimum + mass_penalty + age_penalty) + prior);
            }
            const double maximum = *std::max_element(weights.begin(), weights.end());
            for (size_t i = 0; i < matches.size(); ++i)
            {
                const Sample& sample = samples[matches[i].second];
                const double weight = std::exp(weights[i] - maximum);
                phases[sample.phase] += weight;
                ages.emplace_back(sample.age, weight);
                lower.emplace_back(sample.lower, weight);
                upper.emplace_back(sample.upper, weight);
                masses.emplace_back(sample.mass, weight);
                initial.emplace_back(sample.initial, weight);
            }
            fit.valid = true;
            fit.age = std::pow(10.0, quantile(ages, .5) - 9);
            fit.lower = std::pow(10.0, quantile(lower, .1) - 9);
            fit.upper = std::pow(10.0, quantile(upper, .9) - 9);
            fit.mass = quantile(masses, .5);
            fit.initial_mass = quantile(initial, .5);
            fit.distance = minimum;
            fit.phase = std::max_element(phases.begin(), phases.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; })->first;
            return fit;
        }
    };
}
