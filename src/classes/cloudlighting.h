#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace alienorum
{
    inline double precipitation_snow_fraction(double local_temperature_k)
    {
        if (!std::isfinite(local_temperature_k) || local_temperature_k <= 0) return 0;
        // A narrow melting transition permits mixed precipitation just above freezing.
        return 1 - std::clamp((local_temperature_k - 273.15) / 2.0, 0.0, 1.0);
    }

    struct CloudIllumination
    {
        double daylight = 0;
        double body = 0;
        std::array<double, 3> color = {1, 1, 1};
    };

    inline CloudIllumination cloud_illumination(double source_flux, double ambient,
        double instellation, double pressure_pa, double haze, bool overcast,
        std::array<double, 3> transmitted, std::array<double, 3> sky,
        double swing = 1.3, double exponent = 0.666, double maximum = 0.71)
    {
        CloudIllumination result;
        const auto positive = [](double value) { return std::isfinite(value) ? std::max(0.0, value) : 0.0; };
        // The application's starlight constant is a visibility floor, not direct sunlight.
        // Keep it out of the daytime adaptation curve and orbital normalization.
        const double background = std::pow(std::clamp(positive(ambient), 0.0, 1.0), 2);
        const double normalization = std::clamp(std::sqrt(positive(instellation)), 0.01, 1.0);
        const double direct = std::clamp(positive(source_flux) * 2.5e-11 / normalization, 0.0, 1.0);
        result.daylight = std::min(1.0, direct + background);
        result.body = maximum * std::min(1.0, swing * std::pow(direct, exponent) + background);

        const auto normalize = [&](std::array<double, 3> value)
        {
            for (double& channel : value) channel = positive(channel);
            const double maximum = std::max({value[0], value[1], value[2]});
            if (maximum > 0) for (double& channel : value) channel /= maximum;
            else value = {1, 1, 1};
            return value;
        };
        transmitted = normalize(transmitted);
        const double sky_brightness = positive(sky[0]) + positive(sky[1]) + positive(sky[2]);
        sky = sky_brightness > 0 ? normalize(sky) : transmitted;
        const double diffuse = overcast ? 1.0 : std::clamp(positive(haze), 0.0, 1.0);
        // Clear terrestrial noon keeps its white illumination. Thick air or an opaque
        // deck filters both puffs and wisps, rather than just warming their undersides.
        const double filtering = overcast ? 1.0
            : std::clamp(std::log10(std::max(1.0, positive(pressure_pa) / 101325.0)) / 2, 0.0, 1.0);
        for (size_t i = 0; i < result.color.size(); ++i)
        {
            const double incident = transmitted[i] * (1 - diffuse) + sky[i] * diffuse;
            result.color[i] = 1 - filtering + filtering * incident;
        }
        return result;
    }
}
