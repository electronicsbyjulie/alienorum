#pragma once

#include <algorithm>
#include <cmath>

namespace alienorum
{
    // Prepared once for a map, with no catalog or atmosphere queries per pixel.
    struct LocalClimate
    {
        double mean_k = 288;
        double contrast_k = 48;
        double redistribution = 0.9;
        double lapse_k_m = 0.0065;
        double sea_level_m = 0;
        double regional_k = 3;
        double dry_belt_k = 12;
        double sun_x = -1, sun_y = 0, sun_z = 0;
        bool stellar_locked = false;

        double temperature(double x, double y, double z, double elevation_m) const
        {
            double base = mean_k + contrast_k * (1.0 / 3.0 - z * z);
            if (stellar_locked)
            {
                const double illumination = std::max(0.0, x * sun_x + y * sun_y + z * sun_z);
                const double flux = redistribution + (1.0 - redistribution) * 4.0 * illumination;
                base = mean_k * std::sqrt(std::sqrt(std::max(0.0, flux)));
            }
            // Smooth spherical variation avoids seams and ruler-straight frost boundaries.
            const double regional = regional_k * std::sin(3 * x + 1.7 * y)
                * std::sin(4 * z - 2 * y);
            const double altitude = std::max(0.0, elevation_m - sea_level_m);
            // Broad subtropical land warming approximates dry descending air.
            const double belt_distance = (z * z - 0.18) / 0.2;
            const double belt = std::max(0.0, 1.0 - belt_distance * belt_distance);
            const double continental = !stellar_locked && elevation_m >= sea_level_m
                ? dry_belt_k * belt * belt : 0;
            return std::max(2.7, base + regional + continental - lapse_k_m * altitude);
        }

        static double frost_fraction(double temperature_k)
        {
            const double blend = std::clamp((276.15 - temperature_k) / 6.0, 0.0, 1.0);
            return blend * blend * (3.0 - 2.0 * blend);
        }
    };
}
