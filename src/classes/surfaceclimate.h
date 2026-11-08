#pragma once

#include "climate.h"
#include "salinity.h"
#include "vegetation.h"

namespace alienorum
{
    struct LocalSurfaceState
    {
        // Liquid describes exposed ocean water; a buried reservoir can remain beneath surface ice.
        double temperature = 273.15, ice = 0, liquid = 0, energy = NAN;
        bool ocean = false, frozen_surface = false, evaporated_surface = false, vegetation_active = false;
    };

    inline double surface_boiling_k(double pressure)
    {
        const double inverse = 1.0 / 373.15 - (8.314 / 40660.0) * std::log(std::max(1.0, pressure) / 1e5);
        return inverse > 0 ? std::min(647.096, 1.0 / inverse) : 647.096;
    }

    inline double surface_ice_step(double ice, double temperature, double salinity, double pressure,
        double elapsed, bool ocean)
    {
        const double salt_ratio = salinity / std::max(1.0, 1000 - salinity);
        const double brine = ocean ? std::min(264.0, 1000 * salt_ratio / (std::max(.01, 1 - ice) + salt_ratio)) : 0;
        const double freezing = seawater_freezing_k(brine, pressure);
        if (!std::isfinite(elapsed) || elapsed < 0)
        {
            if (!ocean) return std::clamp((freezing + .25 - temperature) / .5, 0.0, 1.0);
            if (temperature >= seawater_freezing_k(salinity, pressure)) return 0;
            if (temperature <= seawater_freezing_k(salinity > 0 ? 264 : 0, pressure)) return 1;
            double low = 0, high = 1;
            for (int iteration = 0; iteration < 16; ++iteration)
            {
                const double middle = (low + high) * .5;
                const double concentration = std::min(264.0, 1000 * salt_ratio / (std::max(.01, 1 - middle) + salt_ratio));
                if (temperature < seawater_freezing_k(concentration, pressure)) low = middle;
                else high = middle;
            }
            return (low + high) * .5;
        }
        // 10 W/m2/K exchange, latent heat 334 kJ/kg, and a 10 cm ice slab.
        // A thin land frost layer responds faster than floating ocean ice.
        const double latent = ocean ? 917 * .10 * 334000 : 917 * .01 * 334000;
        const double offset = temperature - freezing;
        const double driving = std::abs(offset) <= .15 ? 0 : offset - std::copysign(.15, offset);
        return std::clamp(ice - 10 * driving * std::min(elapsed, 3.15576e10) / latent, 0.0, 1.0);
    }

    inline LocalSurfaceState local_surface_state(const LocalClimate& climate, double x, double y, double z,
        double elevation, bool ocean, double salinity, double pressure, double previous_ice = 0,
        double previous_energy = NAN, double elapsed = -1)
    {
        LocalSurfaceState result;
        result.ocean = ocean;
        result.temperature = climate.temperature(x, y, z, elevation, ocean);
        if (ocean && climate.ice_season)
        {
            const auto& history = *climate.ice_season;
            const double longitude = climate.ice_longitude(x, y), phase = climate.ice_mean_anomaly;
            const double regional = climate.regional_temperature(x, y, z);
            const double reconstructed = history.energy_at(z, longitude, phase, regional);
            auto forcing = history.forcing_at(z, longitude, phase, regional);
            const double daily = climate.temperature(x, y, z, elevation, true, true, false)
                - climate.temperature(x, y, z, elevation, true, false, false);
            // Day/night forcing changes energy; it cannot move temperature across a phase boundary on its own.
            forcing.outgoing_at_freezing -= forcing.feedback * daily;
            result.energy = !std::isfinite(previous_energy) || elapsed < 0
                || elapsed > history.orbit.period / IceSeason::phases ? reconstructed
                : history.column.advance(previous_energy, forcing, elapsed);
            const auto state = history.column.state(result.energy, forcing);
            result.temperature = state.temperature;
            result.ice = state.cover;
            result.liquid = 1 - state.frozen_mass_fraction;
            result.frozen_surface = state.thickness >= .001;
        }
        else
        {
            result.ice = surface_ice_step(previous_ice, result.temperature, salinity, pressure, elapsed, ocean);
            result.liquid = ocean ? 1 - result.ice : 0;
            result.frozen_surface = ocean && result.temperature < seawater_freezing_k(salinity, pressure);
            // A cold surface film can freeze before a deep reservoir loses its latent heat.
            if (result.frozen_surface) result.liquid = 0;
        }
        result.evaporated_surface = !result.frozen_surface
            && (pressure < 611.657 || result.temperature >= surface_boiling_k(pressure));
        if (result.frozen_surface || result.evaporated_surface) result.liquid = 0;
        result.vegetation_active = pressure >= 611.657 && result.temperature >= seawater_freezing_k(salinity, pressure)
            && result.temperature < surface_boiling_k(pressure)
            && vegetation_temperature_suitable(result.temperature, climate.vegetation_min_k, climate.vegetation_max_k);
        return result;
    }
}
