#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "planet.h"

namespace alienorum
{
    enum class CloudCondensate
    {
        none, water, water_ice, carbon_dioxide, methane, ammonia, sulfuric_acid
    };

    struct CloudLayer
    {
        double optical_depth = 0;
        double coverage = 0;
        double convection = 0;
        CloudCondensate condensate = CloudCondensate::none;
    };

    // A rendering closure, not a weather forecast or a self-consistent radiative model.
    // Physical constraints: saturation, adiabatic cooling, hydrostatic column mass, and
    // tau = 3 * condensate_path / (2 * particle_density * effective_radius), with Qext~2.
    // Mixing/precipitation efficiencies, particle sizes, and horizontal coverage remain
    // scenario assumptions. See https://arxiv.org/abs/1301.5627 and
    // https://arxiv.org/abs/astro-ph/0103423 . No atmospheric inputs are overwritten.
    struct CloudClimate
    {
        bool valid = false, humidity_assumed = false;
        double surface_k = 0, pressure_pa = 0, relative_humidity = 0;
        double ocean_fraction = 0, rotation = 0, scale_height_m = 0;
        double moist_convection = 0;
        LocalClimate climate;
        std::array<CloudLayer, 17> layers{};

        static double saturation(double temperature, double reference_k, double reference_pa,
            double latent_j, double critical_k)
        {
            if (temperature >= critical_k) return INFINITY;
            return reference_pa * std::exp(std::clamp(latent_j / 8.314462618
                * (1 / reference_k - 1 / std::max(temperature, 2.7)), -700.0, 100.0));
        }

        static double water_saturation(double temperature)
        {
            return saturation(temperature, 273.16, 611.657,
                temperature < 273.16 ? 51060 : 45050, 647.096);
        }

        CloudLayer at(double x, double y, double z) const
        {
            if (!valid || surface_k <= 0) return {};
            const double local_k = climate.temperature(x, y, z, climate.sea_level_m);
            const double index = std::clamp((local_k / surface_k - 0.55) / 0.70 * (layers.size() - 1),
                0.0, double(layers.size() - 1));
            const size_t low = static_cast<size_t>(index), high = std::min(low + 1, layers.size() - 1);
            const double blend = index - low;
            CloudLayer result;
            result.optical_depth = layers[low].optical_depth * (1 - blend) + layers[high].optical_depth * blend;
            result.coverage = layers[low].coverage * (1 - blend) + layers[high].coverage * blend;
            result.convection = layers[low].convection * (1 - blend) + layers[high].convection * blend;
            result.condensate = blend < 0.5 ? layers[low].condensate : layers[high].condensate;
            if (climate.stellar_locked)
            {
                // Substellar convergence on moist, slowly rotating ocean planets:
                // https://arxiv.org/abs/1307.0515 . This is a spatial proxy, not a GCM.
                const double light = std::max(0.0, x * climate.sun_x + y * climate.sun_y + z * climate.sun_z);
                // Convergence transports moisture from the wider reservoir: local surface
                // relative humidity alone would incorrectly favor the cold night side.
                const double lift = std::clamp(1.0 - rotation, 0.0, 1.0)
                    * std::max(result.convection, moist_convection);
                result.coverage = std::clamp(result.coverage + lift * (0.55 * light - 0.28), 0.0, 0.98);
                result.optical_depth *= 1 - 0.5 * lift + 1.5 * lift * light;
            }
            else
            {
                // Rising equatorial/midlatitude air and descending subtropical air.
                const double latitude = std::abs(z);
                const double descent = std::exp(-std::pow((latitude - 0.5) / 0.16, 2));
                result.coverage *= 1 - 0.18 * result.convection * descent;
            }
            return result;
        }

        static CloudClimate prepare(Planet& p)
        {
            CloudClimate model;
            model.pressure_pa = p.get_surface_pressure();
            if (!std::isfinite(model.pressure_pa) || model.pressure_pa <= 0
                || !std::isfinite(p.mass) || p.mass <= 0
                || !std::isfinite(p.volumetric_mean_radius) || p.volumetric_mean_radius <= 0) return model;
            model.surface_k = p.estimate_surface_temperature();
            if (!std::isfinite(model.surface_k) || model.surface_k <= 0) return model;
            model.climate = p.local_climate();
            const double gravity = G * p.mass / std::pow(p.volumetric_mean_radius, 2);
            if (!std::isfinite(gravity) || gravity <= 0) return model;

            // Equal-area geographic water/ice coverage, rather than the terrain threshold.
            if (p.has_water > 0)
            {
                if (p.type == waterworld || p.type == hycean) model.ocean_fraction = 1;
                else if (p.surf_map && p.surf_map->has_bump_data() && p.m_bump_scale > 0)
                {
                    for (int row = 0; row < 16; ++row)
                    {
                        const double latitude = std::asin(-1 + 2.0 * (row + 0.5) / 16);
                        for (int col = 0; col < 32; ++col)
                        {
                            const double longitude = -_pi + 2 * _pi * (col + 0.5) / 32;
                            const double height = p.surf_map->elevation_at(latitude, longitude) / p.m_bump_scale + 0.5;
                            if (p.has_water >= 1 || height < p.has_water) model.ocean_fraction += 1.0 / 512;
                        }
                    }
                }
                else model.ocean_fraction = std::clamp(double(p.has_water), 0.0, 1.0);
            }

            // Mole fractions and molar heat capacities. Normalize locally, preserving data.
            const AtmosphereComposition* comp = p.atm ? p.atm->comp : nullptr;
            double total = 0, molar_mass = 0, cp = 0;
            const auto gas = [&](double fraction, double mass, double heat)
            {
                if (!std::isfinite(fraction) || fraction < 0) return;
                total += fraction;
                molar_mass += fraction * mass;
                cp += fraction * heat;
            };
            if (comp)
            {
                gas(comp->H2_portion, .002016, 28.8); gas(comp->He_portion, .004003, 20.8);
                gas(comp->N2_portion, .028014, 29.1); gas(comp->O2_portion, .031999, 29.4);
                gas(comp->CO2_portion, .04401, 37.1); gas(comp->H2O_portion, .018015, 33.6);
                gas(comp->CH4_portion, .016043, 35.7); gas(comp->NH3_portion, .017031, 35.1);
                gas(comp->SO2_portion, .064066, 39.9); gas(comp->H2S_portion, .034081, 34.6);
                gas(comp->CO_portion, .02801, 29.1); gas(comp->Ar_portion, .039948, 20.8);
                gas(comp->O3_portion, .048, 39); gas(comp->HCN_portion, .027026, 35);
                gas(comp->C2H6_portion, .03007, 52.5); gas(comp->N2O_portion, .044013, 38.6);
            }
            const bool mixture_known = total > 0;
            molar_mass = mixture_known ? molar_mass / total : .02897;
            cp = mixture_known ? cp / total : 29.1;
            const auto portion = [&](double fraction)
            {
                return mixture_known && std::isfinite(fraction) && fraction > 0 ? fraction / total : 0.0;
            };
            double water = comp ? portion(comp->H2O_portion) : 0;
            if (!mixture_known && model.ocean_fraction > 0)
            {
                // Missing humidity: assume evaporative exchange, or weaker ice sublimation.
                // An explicitly dry, specified gas mixture never takes this fallback.
                const double rh = (model.surface_k >= 273.15 ? 0.75 : 0.20) * std::sqrt(model.ocean_fraction);
                const double vapor = water_saturation(model.surface_k);
                if (std::isfinite(vapor)) water = std::min(0.2, rh * vapor / model.pressure_pa);
                model.humidity_assumed = true;
            }
            const double co2 = comp ? portion(comp->CO2_portion) : 0;
            const double methane = comp ? portion(comp->CH4_portion) : 0;
            const double ammonia = comp ? portion(comp->NH3_portion) : 0;
            const double sulfur = comp ? portion(comp->SO2_portion) : 0;
            const double oxygen = comp ? portion(comp->O2_portion) : 0;
            model.relative_humidity = water * model.pressure_pa / water_saturation(model.surface_k);
            model.moist_convection = std::clamp((model.surface_k - 240) / 50, 0.0, 1.0)
                * std::sqrt(std::clamp(model.relative_humidity, 0.0, 1.0))
                * model.pressure_pa / (model.pressure_pa + 3000);
            const double gas_constant = 8.314462618 / molar_mass;
            model.scale_height_m = gas_constant * model.surface_k / gravity;
            const double spin = std::isfinite(p.sidereal_rotational_period) && p.sidereal_rotational_period != 0
                ? 2 * _pi / std::abs(p.sidereal_rotational_period) : 0;
            model.rotation = std::clamp(spin * p.volumetric_mean_radius
                / (1.7 * std::sqrt(gas_constant * model.surface_k)), 0.0, 3.0);
            double equilibrium = model.surface_k * 0.8;
            if (p.get_light_center() && p.get_light_center()->typeclass() == class_star)
            {
                const double value = p.equilibrium_temperature();
                if (std::isfinite(value) && value > 0) equilibrium = value;
            }
            const double column = model.pressure_pa / gravity;
            const double settling = std::clamp(std::sqrt(9.80665 / gravity), 0.2, 3.0)
                * model.pressure_pa / (model.pressure_pa + 1000);
            for (size_t bin = 0; bin < model.layers.size(); ++bin)
            {
                const double surface = model.surface_k * (0.55 + 0.70 * bin / (model.layers.size() - 1));
                const double skin = std::min(surface, std::max(30.0, equilibrium * 0.72 * surface / model.surface_k));
                double temperature = surface, dominant = 0;
                CloudLayer& layer = model.layers[bin];
                const auto add = [&](double tau, CloudCondensate species, double fraction, double convective)
                {
                    if (!std::isfinite(tau) || tau <= 0) return;
                    layer.optical_depth += tau;
                    layer.coverage = std::max(layer.coverage, fraction);
                    layer.convection = std::max(layer.convection, convective);
                    if (tau > dominant) { dominant = tau; layer.condensate = species; }
                };
                std::array<double, 4> best{};
                double water_cloud_k = surface;
                bool acid_layer = false;
                for (int level = 1; level <= 64; ++level)
                {
                    const double ratio = std::exp(-level * 0.12);
                    const double pressure = model.pressure_pa * ratio;
                    double exponent = 8.314462618 / cp;
                    if (water > 0 && temperature > 235 && temperature < 647
                        && water * pressure >= water_saturation(temperature))
                    {
                        // Saturated moist adiabat, including latent heat release.
                        const double q = water * .018015 / molar_mass;
                        const double specific_cp = cp / molar_mass;
                        exponent *= (1 + 2.5e6 * q / (gas_constant * temperature))
                            / (1 + 2.5e6 * 2.5e6 * q / (specific_cp * 461.5 * temperature * temperature));
                    }
                    temperature = std::max(skin, temperature * std::exp(-0.12 * exponent));
                    const std::array<double, 4> partial = {water, co2, methane, ammonia};
                    const std::array<double, 4> vapor =
                    {
                        water_saturation(temperature),
                        saturation(temperature, 216.59, 518500, temperature < 216.59 ? 25000 : 16000, 304.13),
                        saturation(temperature, 111.66, oneatm, temperature < 90.69 ? 9140 : 8200, 190.56),
                        saturation(temperature, 239.82, oneatm, temperature < 195.4 ? 29000 : 23350, 405.4)
                    };
                    for (size_t i = 0; i < best.size(); ++i)
                    {
                        if (partial[i] <= 0) continue;
                        const double available = ratio * std::max(0.0, 1 - vapor[i] / (partial[i] * pressure));
                        if (available > best[i])
                        {
                            best[i] = available;
                            if (i == 0) water_cloud_k = temperature;
                        }
                    }
                    acid_layer = acid_layer || (temperature >= 200 && temperature <= 400);
                }
                const double rh = water * model.pressure_pa / water_saturation(surface);
                const double wetness = std::clamp(std::sqrt(std::max(0.0, rh)), 0.0, 1.0);
                const double ice = std::clamp((273.15 - water_cloud_k) / 38.15, 0.0, 1.0);
                const double radius = (10 + 5 * model.ocean_fraction) * 1e-6 * (1 - ice) + 20e-6 * ice;
                const double path = column * water * .018015 / molar_mass * best[0]
                    * (0.0015 * (1 - ice) + 0.02 * ice) * settling;
                const double warm = std::clamp((surface - 240) / 50, 0.0, 1.0);
                add(3 * path / (2 * (1000 - 83 * ice) * radius),
                    ice > 0.5 ? CloudCondensate::water_ice : CloudCondensate::water,
                    std::clamp(0.12 + 0.72 * wetness + 0.08 * model.ocean_fraction, 0.0, 0.95),
                    warm * wetness * model.pressure_pa / (model.pressure_pa + 3000));
                // Large CO2 crystals and rapid sedimentation keep Martian clouds thin.
                // Observed condensates: https://arxiv.org/abs/2406.01515 .
                add(column * co2 * .04401 / molar_mass * best[1] * 1e-5 * settling
                    * 3 / (2 * 1600 * 30e-6), CloudCondensate::carbon_dioxide, 0.18, 0);
                add(column * methane * .016043 / molar_mass * best[2] * 0.0005 * settling
                    * 3 / (2 * 500 * 20e-6), CloudCondensate::methane, 0.12, 0.2);
                add(column * ammonia * .017031 / molar_mass * best[3] * 0.0005 * settling
                    * 3 / (2 * 700 * 15e-6), CloudCondensate::ammonia, 0.25, 0.15);
                // Photochemical sulfate proxy, not CO2 condensation or a sulfur kinetics
                // solver. Both SO2 and H2O plus an oxygen source must be present.
                if (acid_layer && surface > 300 && sulfur > 0 && water > 0 && co2 + oxygen > 0)
                {
                    const double precursors = std::min(sulfur, water) * .098079 / molar_mass;
                    add(column * precursors * 1e-4 * 3 / (2 * 1800 * 1e-6),
                        CloudCondensate::sulfuric_acid, 0.98, 0);
                }
                layer.optical_depth = std::min(100.0, layer.optical_depth);
                if (layer.optical_depth > 0.0001) model.valid = true;
            }
            return model;
        }
    };
}
