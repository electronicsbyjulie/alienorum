#pragma once

#include <algorithm>
#include <cmath>
#include <array>
#include <memory>
#include "iceclimate.h"

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
        bool current_conditions = false;
        double current_mean_k = 0, water_fraction = 0;
        double instantaneous_flux = 1;
        double solar_frequency = 0, land_diurnal_seconds = 0, water_diurnal_seconds = 0;
        std::array<double, 33> land_seasonal_flux{}, water_seasonal_flux{};
        std::shared_ptr<const IceSeason> ice_season;
        bool ice_pending = false;
        double ice_mean_anomaly = 0;

        double ice_longitude(double x, double y) const
        {
            return std::atan2(y * sun_x - x * sun_y, x * sun_x + y * sun_y);
        }

        double regional_temperature(double x, double y, double z) const
        {
            // Several spatial scales break up ice margins without longitude seams
            // or changes between frames. This is a climate scenario, not weather data.
            return regional_k * (.65 * std::sin(3 * x + 1.7 * y) * std::sin(4 * z - 2 * y)
                + .25 * std::sin(13 * x - 7 * y + 3 * z) * std::sin(11 * z + 5 * y)
                + .10 * std::sin(43 * x + 29 * y) * std::sin(37 * z - 17 * y));
        }

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
            const double regional = regional_temperature(x, y, z);
            const double altitude = std::max(0.0, elevation_m - sea_level_m);
            // Broad subtropical land warming approximates dry descending air.
            const double belt_distance = (z * z - 0.18) / 0.2;
            const double belt = std::max(0.0, 1.0 - belt_distance * belt_distance);
            const double continental = !stellar_locked && elevation_m >= sea_level_m
                ? dry_belt_k * belt * belt : 0;
            double surface = base + regional + continental;
            if (current_conditions)
            {
                const double index = std::clamp((z + 1) * 0.5 * (land_seasonal_flux.size() - 1),
                    0.0, double(land_seasonal_flux.size() - 1));
                const size_t low = static_cast<size_t>(index), high = std::min(low + 1, land_seasonal_flux.size() - 1);
                const bool water = water_fraction > 0 && (water_fraction > 0.999 || elevation_m < sea_level_m);
                const auto& seasonal = water ? water_seasonal_flux : land_seasonal_flux;
                double flux = seasonal[low] + (seasonal[high] - seasonal[low]) * (index - low);
                const double seasonal_flux = flux;
                if (!stellar_locked)
                {
                    const double a = z * sun_z;
                    const double b = std::sqrt(std::max(0.0, (1 - z * z) * (1 - sun_z * sun_z)));
                    const double sunset = b > 1e-12 ? std::acos(std::clamp(-a / b, -1.0, 1.0)) : (a > 0 ? 3.141592653589793 : 0);
                    const double horizontal = std::hypot(x, y) * std::hypot(sun_x, sun_y);
                    const double cosine = horizontal > 1e-12 ? std::clamp((x * sun_x + y * sun_y) / horizontal, -1.0, 1.0) : 0;
                    const double sine = horizontal > 1e-12 ? (y * sun_x - x * sun_y) / horizontal : 0;
                    const double hour = std::atan2(sine, cosine);
                    const double tau = water ? water_diurnal_seconds : land_diurnal_seconds;
                    const auto integral = [sunset](int n) { return n ? std::sin(n * sunset) / n : sunset; };
                    for (int n = 1; n <= 8; ++n)
                    {
                        const double amplitude = 2 / 3.141592653589793 * (a * integral(n)
                            + b * 0.5 * (integral(n - 1) + integral(n + 1)));
                        const double response = n * solar_frequency * tau;
                        flux += 4 * instantaneous_flux * (1 - redistribution) * amplitude
                            * std::cos(n * hour - std::atan(response)) / std::sqrt(1 + response * response);
                    }
                }
                if (water && ice_season)
                {
                    const double longitude = ice_longitude(x, y);
                    const double energy = ice_season->energy_at(z, longitude, ice_mean_anomaly, regional);
                    const auto forcing = ice_season->forcing_at(z, longitude, ice_mean_anomaly, regional);
                    const double diurnal = surface * (std::pow(std::max(1e-8, flux), .25)
                        - std::pow(std::max(1e-8, seasonal_flux), .25));
                    surface = ice_season->column.state(energy, forcing).temperature + diurnal;
                }
                else surface *= std::pow(std::max(1e-8, flux), 0.25);
            }
            return std::max(2.7, surface - lapse_k_m * altitude);
        }

        static double frost_fraction(double temperature_k)
        {
            const double blend = std::clamp((276.15 - temperature_k) / 6.0, 0.0, 1.0);
            return blend * blend * (3.0 - 2.0 * blend);
        }
    };
}
