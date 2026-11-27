#pragma once

#include <cmath>

namespace alienorum
{
    struct EquilibriumTideSpin
    {
        double damping_factor;
        double equilibrium_ratio;
    };

    // Leconte et al. (2010), N_a(e) and N(e), at zero obliquity.
    inline EquilibriumTideSpin equilibrium_tide_spin(double eccentricity)
    {
        const double z = eccentricity * eccentricity, beta2 = 1 - z;
        const double rotation = 1 + z * (3 + z * .375);
        const double torque = 1 + z * (7.5 + z * (5.625 + z * .3125));
        return {rotation / std::pow(beta2, 4.5), torque / (rotation * std::pow(beta2, 1.5))};
    }

    // Orbit-averaged equilibrium tide, with zero spin obliquity. Full eccentricity
    // functions: Leconte et al. (2010), doi:10.1051/0004-6361/201014337.
    // k2/Q sets the lag at the dominant orbital or semidiurnal forcing frequency.
    // This estimates present heat loss, without evolving the orbit or interior.
    inline double tidal_surface_heat_flux(double parent_mass_kg, double radius_m,
        double axis_m, double eccentricity, double mean_motion, double spin_rate,
        double k2_over_q)
    {
        if (!std::isfinite(parent_mass_kg) || parent_mass_kg <= 0
            || !std::isfinite(radius_m) || radius_m <= 0
            || !std::isfinite(axis_m) || axis_m <= radius_m
            || !std::isfinite(eccentricity) || eccentricity < 0 || eccentricity >= 1
            || !std::isfinite(mean_motion) || mean_motion <= 0
            || !std::isfinite(spin_rate) || !std::isfinite(k2_over_q) || k2_over_q < 0) return 0;
        const double z = eccentricity * eccentricity, beta2 = 1 - z;
        const double rotation = 1 + z * (3 + z * .375);
        const auto tide = equilibrium_tide_spin(eccentricity);
        // Positive polynomial avoids cancellation for almost circular orbits.
        const double minimum = z * (3.5 + z * (11.25 + z * (28 + z * (685.0 / 64
            + z * (255.0 / 128 + z * 25.0 / 512))))) / (rotation * std::pow(beta2, 7.5));
        // An absent rotation uses the minimum-dissipation equilibrium spin.
        const double offset = spin_rate == 0 ? 0 : spin_rate / mean_motion - tide.equilibrium_ratio;
        const double shape = minimum + tide.damping_factor * offset * offset;
        const double spin_ratio = spin_rate == 0 ? tide.equilibrium_ratio : spin_rate / mean_motion;
        const double frequency_ratio = std::fmax(1.0, 2 * std::abs(spin_ratio - 1));
        constexpr double gravity_si = 6.6743015e-11, pi = 3.14159265358979323846;
        const double mass_per_axis_cubed = parent_mass_kg / axis_m / axis_m / axis_m;
        const double flux = 3 * k2_over_q * gravity_si * mass_per_axis_cubed * mass_per_axis_cubed
            * radius_m * radius_m * radius_m * mean_motion * shape / (4 * pi * frequency_ratio);
        return std::isfinite(flux) ? flux : 0;
    }
}
