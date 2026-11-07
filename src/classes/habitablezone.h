#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace alienorum
{
    struct ConservativeHZ
    {
        double inner_flux, outer_flux;
    };

    inline ConservativeHZ conservative_hz(double temperature_k, double mass_earth)
    {
        // Kopparapu et al. (2014), Table 1 and Eq. 4: https://arxiv.org/abs/1404.5292
        // These are 1D runaway/maximum greenhouse limits. Bohl et al. (2026)'s
        // chart uses empirical Recent Venus/Early Mars and a separate 3D inner
        // limit, so its shaded bands are not this conservative 1D definition.
        // Interpolate every coefficient linearly between the tabulated masses.
        // Outside the calibration range, retain an endpoint approximation.
        const std::array<double, 5> low = {0.99, 1.209e-4, 1.404e-8, -7.418e-12, -1.713e-15};
        const std::array<double, 5> earth = {1.107, 1.332e-4, 1.580e-8, -8.308e-12, -1.931e-15};
        const std::array<double, 5> high = {1.188, 1.433e-4, 1.707e-8, -8.968e-12, -2.084e-15};
        const std::array<double, 5> outer = {0.356, 6.171e-5, 1.698e-9, -3.198e-12, -5.575e-16};
        const double mass = std::isfinite(mass_earth) && mass_earth > 0
            ? std::clamp(mass_earth, 0.1, 5.0) : 1.0;
        const auto& lower = mass < 1 ? low : earth;
        const auto& upper = mass < 1 ? earth : high;
        const double fraction = mass < 1 ? (mass - 0.1) / 0.9 : (mass - 1) / 4;
        std::array<double, 5> inner;
        for (size_t i = 0; i < inner.size(); ++i)
            inner[i] = lower[i] + fraction * (upper[i] - lower[i]);
        // The polynomial's reference is 5780 K, independently of the app's Sun.
        const double offset = std::clamp(temperature_k, 2600.0, 7200.0) - 5780;
        const auto evaluate = [offset](const std::array<double, 5>& coefficients)
        {
            double flux = coefficients.back();
            for (int i = 3; i >= 0; --i) flux = flux * offset + coefficients[i];
            return flux;
        };
        return {evaluate(inner), evaluate(outer)};
    }
}
