#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace alienorum
{
    // Morphological scenarios, not a diagnosis of plate tectonics. Bulk properties
    // constrain differentiation, relaxation and deposition; geological history is
    // represented by reproducible variation. No object names select a scenario.
    struct TerrainContours
    {
        double strength = 0, lowland_fraction = .5, boundary = .5;
        double margin_width = .025, province_coupling = 0, icy_crust = 0;
        bool liquid_surface = false;

        static double smooth(double low, double high, double value)
        {
            const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
            return t * t * (3 - 2 * t);
        }

        static double variation(uint32_t salt)
        {
            salt ^= salt >> 16;
            salt *= 0x7feb352dU;
            salt ^= salt >> 15;
            salt *= 0x846ca68bU;
            salt ^= salt >> 16;
            return salt / 4294967295.0;
        }

        static TerrainContours from_physics(double radius_m, double mass_kg, double temperature_k,
            double pressure_pa, bool liquid_water, uint32_t salt)
        {
            TerrainContours result;
            if (!(radius_m > 0) || !(mass_kg > 0) || !std::isfinite(radius_m)
                || !std::isfinite(mass_kg)) return result;
            const double density = mass_kg / (4.188790204786391 * radius_m * radius_m * radius_m);
            const double binding = 6.67430e-11 * mass_kg / radius_m;
            const double differentiated = smooth(1.5e5, 2e6, binding);
            const double temperature = std::isfinite(temperature_k) ? temperature_k : 250;
            const double pressure = std::isfinite(pressure_pa) ? std::max(0.0, pressure_pa) : 0;
            const double random = variation(salt ^ 0xb5297a4dU);
            result.icy_crust = (1 - smooth(180, 300, temperature)) * (1 - smooth(1700, 3500, density));
            const double deposition = std::clamp(std::log1p(pressure / 600) / 7.4, 0.0, 1.0);
            const double hot = smooth(420, 900, temperature);
            const double relaxation = smooth(.38, .8, temperature / (1400 - 1127 * result.icy_crust));
            result.liquid_surface = liquid_water;
            // Liquid-water terrains have distinct basins, shelves and uplands.
            // Dry and frozen crusts retain subdued or localized basin provinces.
            result.strength = liquid_water ? 1 : differentiated * (.22 + .35 * random) * (1 - .65 * relaxation);
            const double size = smooth(5e5, 3.5e6, radius_m);
            result.lowland_fraction = liquid_water ? .60 + .13 * random : .14 + .43 * size + .12 * (random - .5);
            const double plains = std::max(hot, result.icy_crust * deposition);
            result.lowland_fraction = std::clamp(result.lowland_fraction * (1 - plains)
                + (.78 + .09 * random) * plains, .08, .88);
            result.margin_width = liquid_water ? .018 : .035 + .03 * relaxation;
            result.province_coupling = std::max(liquid_water ? .55 : .0,
                differentiated * (.35 + .35 * std::max(deposition, result.icy_crust)));
            return result;
        }

        double upland(double field) const
        {
            return smooth(boundary - margin_width, boundary + margin_width, field);
        }

        double height(double detail, double field) const
        {
            const double delta = field - boundary, width = margin_width;
            // A basin floor, continental slope, shelf and upland. The shelf is a
            // morphological transition, not a sea-level threshold or water supply.
            const double slope = smooth(-2 * width, -.35 * width, delta);
            const double coast = smooth(.35 * width, 1.6 * width, delta);
            const double land = upland(field);
            const double texture = std::clamp((detail - .5) * 2.5, -.5, .5);
            const double shaped = .28 + .18 * slope + .20 * coast + (.045 + .11 * land) * texture;
            return std::clamp((1 - strength) * detail + strength * shaped, 0.0, 1.0);
        }

        double province(double independent, double elevation) const
        {
            // Actual elevation also aligns provinces on retained/imported relief.
            const double land = smooth(.38, .60, elevation);
            return std::clamp((1 - province_coupling) * independent + province_coupling * land, 0.0, 1.0);
        }
    };
}
