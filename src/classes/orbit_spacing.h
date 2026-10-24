#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace alienorum
{
    inline constexpr size_t resonance_candidate_limit = 64;

    template<typename Candidate, typename Sibling, typename Factory>
    std::vector<Candidate> simplest_resonance_candidates(const std::vector<Sibling>& siblings,
        Factory make_candidate, size_t limit = resonance_candidate_limit)
    {
        std::vector<Candidate> result;
        // Visit every partner at a given complexity before considering larger ratios.
        for (int total = 3; total <= 36 && result.size() < limit; ++total)
        {
            for (int n = 1; n < 10 && result.size() < limit; ++n)
            {
                const int m = total - n;
                if (m < 1 || m >= 10 + n * 2 || m == n) continue;
                if (m == 2*n || n == 2*m || m == 3*n || n == 3*m || m == 4*n || n == 4*m) continue;
                if ((m%2 == 0 && n%2 == 0) || (m%3 == 0 && n%3 == 0)
                    || (m%5 == 0 && n%5 == 0) || (m%7 == 0 && n%7 == 0)) continue;
                if (m > .75*n && n > .75*m) continue;
                for (const auto& sibling : siblings)
                {
                    Candidate candidate{};
                    if (make_candidate(sibling, m, n, candidate)) result.push_back(candidate);
                    if (result.size() == limit) return result;
                }
            }
        }
        return result;
    }

    // A conservative coplanar screening rule: radial envelopes must remain
    // separated by the circular two-body Hill stability distance.
    inline bool orbital_spacing_warning(double axis, double eccentricity, double mass,
        double other_axis, double other_eccentricity, double other_mass, double parent_mass)
    {
        if (!(axis > 0) || !(other_axis > 0) || !(parent_mass > 0)) return false;
        if (!std::isfinite(axis) || !std::isfinite(other_axis)) return false;
        const double hill_radius = 0.5 * (axis + other_axis)
            * std::cbrt((std::max(0.0, mass) + std::max(0.0, other_mass)) / (3 * parent_mass));
        const double gap = std::abs(axis - other_axis)
            - axis * std::clamp(eccentricity, 0.0, 1.0)
            - other_axis * std::clamp(other_eccentricity, 0.0, 1.0);
        return gap <= 2 * std::sqrt(3.0) * hill_radius;
    }
}
