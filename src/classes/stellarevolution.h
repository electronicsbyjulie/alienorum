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
        std::map<int, double> phase_probabilities;
        double effective_samples = 0;
    };

    class StellarEvolutionGrid
    {
        struct Sample
        {
            std::array<double, 5> position;
            double mass, initial, age, lower, upper;
            double log_population_weight = 0;
            int phase;
        };
        struct Node { int sample, left = -1, right = -1, axis; };
        std::vector<Sample> samples;
        std::vector<Node> nodes;
        std::map<double, std::map<int, double>> main_sequence_lifetimes;
        std::array<int, 8> roots{{-1, -1, -1, -1, -1, -1, -1, -1}};

        int build(std::vector<int>& indices, int first, int last, int depth, int dimensions)
        {
            if (first >= last) return -1;
            const int middle = (first + last) / 2, axis = depth % dimensions;
            std::nth_element(indices.begin() + first, indices.begin() + middle, indices.begin() + last,
                [&](int a, int b) { return samples[a].position[axis] < samples[b].position[axis]; });
            const int node = nodes.size();
            nodes.push_back({indices[middle], -1, -1, axis});
            const int left = build(indices, first, middle, depth + 1, dimensions);
            const int right = build(indices, middle + 1, last, depth + 1, dimensions);
            nodes[node].left = left;
            nodes[node].right = right;
            return node;
        }

        using Neighbors = std::priority_queue<std::pair<double, int>>;
        void nearest(int node, const std::array<double, 5>& query,
            const std::array<double, 5>& scales, size_t count, Neighbors& result) const
        {
            if (node < 0) return;
            const Node& current = nodes[node];
            const Sample& sample = samples[current.sample];
            double distance = 0;
            for (int axis = 0; axis < 5; ++axis)
                distance += std::pow((query[axis] - sample.position[axis]) * scales[axis], 2);
            if (result.size() < count || distance < result.top().first)
            {
                result.emplace(distance, current.sample);
                if (result.size() > count) result.pop();
            }
            const double delta = query[current.axis] - sample.position[current.axis];
            nearest(delta < 0 ? current.left : current.right, query, scales, count, result);
            if (result.size() < count || std::pow(delta * scales[current.axis], 2) <= result.top().first)
                nearest(delta < 0 ? current.right : current.left, query, scales, count, result);
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

        public:
        explicit StellarEvolutionGrid(const std::string& path = "catalogs/stellar_evolution.dat")
        {
            std::ifstream input(path);
            std::string line;
            std::array<std::vector<int>, 8> groups;
            while (std::getline(input, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream row(line);
                Sample sample;
                double metallicity, temperature, luminosity;
                if (!(row >> metallicity >> sample.phase >> temperature >> luminosity >> sample.mass
                    >> sample.initial >> sample.age >> sample.lower >> sample.upper)) continue;
                if (!(sample.mass > 0) || !(sample.initial > 0) || !std::isfinite(sample.age)) continue;
                // Legacy nine-column grids retain their original population prior.
                if (!(row >> sample.log_population_weight))
                    sample.log_population_weight = sample.phase < 100
                        ? std::log(10.0) * (sample.age - 9) - 2.35 * std::log(sample.initial) : 0;
                if (!std::isfinite(temperature) || !std::isfinite(luminosity) || !std::isfinite(metallicity)
                    || !std::isfinite(sample.log_population_weight)) continue;
                sample.position = {temperature / .035, luminosity / .20, metallicity / .40,
                    std::log10(sample.mass) / .30, (sample.age - 9) / .30};
                const int index = samples.size();
                samples.push_back(sample);
                if (sample.phase == 0)
                {
                    auto& lifetime = main_sequence_lifetimes[metallicity][std::lround(std::log10(sample.initial) / .025)];
                    lifetime = std::max(lifetime, std::pow(10.0, sample.upper - 9));
                }
                if (sample.phase < 100) groups[0].push_back(index);
                // A dwarf-like spectrum alone cannot distinguish contraction from core H burning.
                if (sample.phase == -1 || sample.phase == 0) groups[1].push_back(index);
                if (sample.phase > 0 && sample.phase < 100) groups[2].push_back(index);
                if (sample.phase == 101) groups[3].push_back(index);
                if (sample.phase == 102) groups[4].push_back(index);
                if (sample.phase >= 100) groups[5].push_back(index);
                if (sample.phase == 0 || sample.phase == 1) groups[6].push_back(index);
                if (sample.phase >= 2 && sample.phase < 100) groups[7].push_back(index);
            }
            for (int group = 0; group < 8; ++group)
            {
                // HR splits also bound the full five-coordinate distance, while avoiding
                // unprunable mass/age splits for stars without those measurements.
                const int dimensions = group >= 3 && group <= 5 ? 5 : 3;
                roots[group] = build(groups[group], 0, groups[group].size(), 0, dimensions);
            }
        }

        public:
        static const StellarEvolutionGrid& instance()
        {
            static const StellarEvolutionGrid grid;
            return grid;
        }

        double main_sequence_lifetime_gyr(double initial_mass, double metallicity = 0) const
        {
            if (!(initial_mass > 0) || !std::isfinite(initial_mass) || main_sequence_lifetimes.empty()) return NAN;
            if (!std::isfinite(metallicity)) metallicity = 0;
            auto abundance = main_sequence_lifetimes.lower_bound(metallicity);
            if (abundance == main_sequence_lifetimes.end()) --abundance;
            else if (abundance != main_sequence_lifetimes.begin())
            {
                const auto previous = std::prev(abundance);
                if (metallicity - previous->first < abundance->first - metallicity) abundance = previous;
            }
            const auto& lifetimes = abundance->second;
            const double coordinate = std::log10(initial_mass) / .025;
            auto right = lifetimes.lower_bound(static_cast<int>(std::ceil(coordinate)));
            if (right == lifetimes.end()) return lifetimes.rbegin()->second;
            if (right == lifetimes.begin()) return right->second;
            const auto left = std::prev(right);
            const double fraction = std::clamp((coordinate - left->first) / (right->first - left->first), 0.0, 1.0);
            return std::exp((1 - fraction) * std::log(left->second) + fraction * std::log(right->second));
        }

        StellarEvolutionFit fit(double log_temperature, double log_luminosity,
            double metallicity, int group, double supplied_mass = 0, double supplied_age = -1,
            double temperature_sigma = .035, double luminosity_sigma = .20,
            double metallicity_sigma = .40, double mass_sigma = .30) const
        {
            StellarEvolutionFit fit;
            if (!std::isfinite(log_temperature) || !std::isfinite(log_luminosity)
                || !std::isfinite(metallicity) || group < 0 || group >= 8
                || !(temperature_sigma > 0) || !(luminosity_sigma > 0)
                || !(metallicity_sigma > 0) || !(mass_sigma > 0)) return fit;
            const bool cooling = group >= 3 && group <= 5;
            const bool mass_constraint = std::isfinite(supplied_mass) && supplied_mass > 0;
            const bool age_constraint = std::isfinite(supplied_age) && supplied_age >= 0;
            const std::array<double, 5> query{{log_temperature / .035, log_luminosity / .20, metallicity / .40,
                mass_constraint ? std::log10(supplied_mass) / .30 : 0,
                age_constraint ? std::log10(std::max(.0001, supplied_age)) / .30 : 0}};
            const std::array<double, 5> scales{{.035 / temperature_sigma, .20 / luminosity_sigma,
                .40 / metallicity_sigma, mass_constraint ? .30 / (cooling ? .04 : mass_sigma) : 0,
                age_constraint ? 1.0 : 0}};
            Neighbors neighbors;
            // Apply measured mass and age before candidate truncation, preserving viable tracks.
            nearest(roots[group], query, scales, cooling ? 64 : 128, neighbors);
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
                // Grid weights integrate initial-mass and linear-time cell volumes once.
                weights.push_back(-.5 * (match.first - minimum) + sample.log_population_weight);
            }
            const double maximum = *std::max_element(weights.begin(), weights.end());
            double total_weight = 0, squared_weight = 0;
            for (size_t i = 0; i < matches.size(); ++i)
            {
                const Sample& sample = samples[matches[i].second];
                const double weight = std::exp(weights[i] - maximum);
                total_weight += weight;
                squared_weight += weight * weight;
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
            for (const auto& phase : phases) fit.phase_probabilities[phase.first] = phase.second / total_weight;
            fit.effective_samples = total_weight * total_weight / squared_weight;
            return fit;
        }
    };
}
