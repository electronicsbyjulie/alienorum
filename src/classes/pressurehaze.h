#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Set to 0, or compile with -DPRESSURE_HAZE=0, to restore the original surface view.
#ifndef PRESSURE_HAZE
#define PRESSURE_HAZE 1
#endif

namespace alienorum
{
    struct PressureHaze
    {
        std::array<double, 3> extinction{};
        std::array<double, 3> light{}; // Display-space airlight, 0..255.
        double sky_opacity = 0;

        bool active() const { return extinction[1] > 0; }

        static PressureHaze prepare(double pressure_pa, double temperature_k, double scattering_ratio,
            double particulates, double scale_height_m)
        {
            PressureHaze result;
            #if PRESSURE_HAZE
            if (!std::isfinite(pressure_pa) || pressure_pa <= 2e5
                || !std::isfinite(temperature_k) || temperature_k <= 0
                || !std::isfinite(scattering_ratio) || scattering_ratio <= 0) return result;
            const double ramp = std::clamp((pressure_pa - 2e5) / 8e5, 0.0, 1.0);
            const double activation = ramp * ramp * (3 - 2 * ramp);
            // Ideal-gas number density times an approximate air cross section at 550 nm.
            // Rayleigh wavelength/composition scaling: Bodhaine et al. (1999),
            // https://doi.org/10.1175/1520-0426(1999)016<1854:ORODC>2.0.CO;2 .
            // Dense-fluid correlations and real-gas corrections are outside this closure.
            const double beta = pressure_pa / (1.380649e-23 * temperature_k)
                * 4.6e-31 * scattering_ratio * activation;
            const double dust = std::isfinite(particulates) ? std::clamp(particulates, 0.0, 1.0) : 0;
            const double wavelength[] = {650, 550, 450};
            for (int channel = 0; channel < 3; ++channel)
                result.extinction[channel] = beta * (std::pow(550 / wavelength[channel], 4) + dust);
            // Multiple scattering progressively replaces sky/cloud contrast with diffuse light.
            result.sky_opacity = -std::expm1(-beta * std::max(0.0, scale_height_m));
            #endif
            return result;
        }

        double transmission(double distance_m, int channel) const
        {
            if (!active() || !(distance_m > 0)) return 1;
            return std::exp(-extinction[channel] * distance_m);
        }

        std::array<double, 3> apply(const std::array<double, 3>& color, double distance_m) const
        {
            std::array<double, 3> result{};
            for (int channel = 0; channel < 3; ++channel)
            {
                const double transmittance = transmission(distance_m, channel);
                result[channel] = color[channel] * transmittance + light[channel] * (1 - transmittance);
            }
            return result;
        }
    };
}
