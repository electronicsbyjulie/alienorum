#ifndef ALIENORUM_SALINITY_H
#define ALIENORUM_SALINITY_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include "numeric.h"

namespace alienorum
{
    inline double ocean_numeric(const nlohmann::json& object, const char* key)
    {
        const auto value = object.find(key);
        if (value == object.end()) return NAN;
        if (value->is_number()) return value->get<double>();
        double parsed = NAN;
        if (value->is_string() && parse_numeric_value(value->get<std::string>(), parsed)) return parsed;
        return NAN;
    }

    inline double seawater_freezing_k(double salinity, double pressure_pa = 101325)
    {
        // UNESCO surface seawater polynomial; hydrostatic pressure is in decibars.
        const double s = std::clamp(salinity, 0.0, 120.0);
        const double dbar = std::max(0.0, pressure_pa - 101325.0) / 10000;
        const double value = 273.15 - .0575 * s + .001710523 * s * std::sqrt(s) - .0002154996 * s * s;
        // A labeled NaCl-brine scenario beyond the seawater fit, down to its eutectic.
        const double brine = salinity > 120 ? std::max(252.05, value - (value - 252.05) * (salinity - 120) / 113) : value;
        return brine - .000753 * dbar;
    }

    inline nlohmann::json estimate_ocean_chemistry(const nlohmann::json& bundles, double metallicity,
        double supplied = NAN, double liquid_scale = 1)
    {
        using nlohmann::json;
        if (std::isfinite(supplied) && supplied >= 0 && supplied < 1000)
            return {{"salinity_g_kg", supplied}, {"estimated", false}, {"source", "supplied"}};
        const std::map<std::string, double> solar =
        {
            {"Fe", 7.50}, {"Mg", 7.60}, {"O", 8.69}, {"Na", 6.24}, {"S", 7.12}, {"Cl", 5.50}, {"Ar", 6.40}
        };
        std::map<std::string, double> measured;
        std::map<std::string, std::string> sources;
        double fe = std::isfinite(metallicity) ? metallicity : 0;
        if (bundles.is_object())
            for (const auto& bundle : bundles.items())
            {
                if (!bundle.value().is_object()) continue;
                const json elements = bundle.value().value("elements", json::object());
                const json reference = bundle.value().value("solar", json::object());
                for (const auto& element : solar)
                {
                    if (!elements.contains(element.first) || !elements[element.first].is_object()) continue;
                    const auto& entry = elements[element.first];
                    const double dex = ocean_numeric(entry, "dex");
                    if (!std::isfinite(dex)) continue;
                    const std::string ratio = entry.value("ratio", "[" + element.first + "/H]");
                    double value = dex;
                    if (ratio == "[" + element.first + "/Fe]")
                    {
                        const double actual_fe = elements.contains("Fe") ? ocean_numeric(elements["Fe"], "dex") : NAN;
                        value += std::isfinite(actual_fe) ? actual_fe : fe;
                    }
                    else if (ratio != "[" + element.first + "/H]") continue;
                    const double offset = ocean_numeric(reference, element.first.c_str());
                    if (std::isfinite(offset)) value += offset - element.second;
                    // Direct entries always outrank a proxy; retain the first available source.
                    if (!measured.count(element.first))
                    {
                        measured[element.first] = value;
                        sources[element.first] = bundle.key();
                    }
                }
            }
        if (measured.count("Fe")) fe = measured["Fe"];
        std::string na_source = "solar_scaled_metallicity", cl_source = "solar_scaled_metallicity";
        double na = fe, cl = fe;
        for (const char* element : {"Na", "Mg"})
            if (measured.count(element)) { na = measured[element]; na_source = element; break; }
        for (const char* element : {"Cl", "S", "Ar"})
            if (measured.count(element)) { cl = measured[element]; cl_source = element; break; }
        const bool constrained = na_source != "solar_scaled_metallicity" || cl_source != "solar_scaled_metallicity";
        const double water = std::max(1e-6, liquid_scale);
        // Earth-calibrated mobilization and an Earth-like mixture of dissolved ions.
        // Bulk enrichment relative to rock cancels a shared metallicity shift.
        const double salt_scale = constrained ? std::pow(10.0, std::clamp(.5 * (na + cl) - fe, -4.0, 4.0)) : 1;
        const double salt_water_ratio = (35.0 / 965.0) * salt_scale / water;
        const double total = 1000 * salt_water_ratio / (1 + salt_water_ratio);
        const double salinity = std::min(264.0, total);
        json result = {{"salinity_g_kg", salinity}, {"estimated", true},
            {"source", constrained ? "stellar_abundances_and_water_budget_scenario" : "Earth_like_fallback"},
            {"Na_proxy", na_source}, {"Cl_proxy", cl_source}, {"abundance_sources", sources},
            {"liquid_water_scale", water}, {"salt_inventory_scale", salt_scale},
            {"precipitated_salt_g_kg_solution", std::max(0.0, total - salinity)},
            {"lower_g_kg", constrained ? salinity * .1 : 5.0}, {"upper_g_kg", constrained ? std::min(264.0, salinity * 10) : 100.0},
            {"interpretation", "Earth_calibrated_retention_and_ocean_mobilization;not_a_measured_ocean_composition"}};
        if (measured.count("Ar") && cl_source == "S") result["Cl_Ar_alternative_dex"] = measured["Ar"];
        return result;
    }
}
#endif
