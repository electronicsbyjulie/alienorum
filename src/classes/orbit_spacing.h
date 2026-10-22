#pragma once

#include <algorithm>
#include <cmath>

namespace alienorum
{
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
