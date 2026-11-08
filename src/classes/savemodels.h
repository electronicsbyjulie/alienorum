#pragma once

#include <algorithm>
#include <initializer_list>
#include <map>
#include "shore.h"
#include "numeric.h"

namespace alienorum
{
    namespace SaveModels
    {
        inline json inventory_defaults()
        {
            // Format 1 defaults are fixed independently of future simulation tuning.
            return {{"age_gyr", 4.5}, {"co2_mass_fraction", 1.45e-4}, {"nitrogen_mass_fraction", 1e-6},
                {"water_mass_fraction", 2.3e-4}, {"weathering_efficiency", 1}, {"water_column_humidity", .15},
                {"xuv_scale", 1}, {"surface_water_share", 1}, {"basin_relief_m", 2500},
                {"nitrogen_frost_albedo", .8}, {"nitrogen_frost_emissivity", .67}, {"cold_temperature_k", 0},
                {"impact_flux_scale", 1}, {"impactor_water_fraction", .1}, {"impactor_co2_fraction", .01},
                {"impactor_nitrogen_fraction", .001}};
        }

        inline json select(const json& input, std::initializer_list<const char*> keys)
        {
            json output = json::object();
            for (const char* key : keys)
                if (input.contains(key)) output[key] = input[key];
            return output;
        }

        inline bool number(const json& input, const char* key, double& result)
        {
            auto value = input.find(key);
            if (value == input.end()) return false;
            if (value->is_number()) result = value->get<double>();
            else if (value->is_string())
            {
                if (!parse_numeric_value(value->get<std::string>(), result)) return false;
            }
            else return false;
            return std::isfinite(result);
        }

        inline json abundances(const json& input)
        {
            if (!input.is_object() || input.empty()) return input;
            struct Element
            {
                double sum = 0, low = INFINITY, high = -INFINITY, error = 0;
                size_t count = 0;
                bool errors = true, annotated = false;
            };
            std::map<std::string, Element> elements;
            json solar = json::object();
            for (const auto& source : input.items())
            {
                const auto& bundle = source.value();
                if (!bundle.is_object()) return input;
                if (bundle.value("model", "") == "element_summary_v1")
                {
                    if (input.size() == 1) return input;
                    // A catalog refresh can append full records beside the saved summary.
                    // Summarize the refreshed records without counting the old mean twice.
                    continue;
                }
                if (!bundle.contains("solar") || !bundle["solar"].is_object()
                    || !bundle.contains("elements") || !bundle["elements"].is_object()) return input;
                for (const auto& reference : bundle["solar"].items())
                    if (!solar.contains(reference.key())) solar[reference.key()] = reference.value();
                for (const auto& item : bundle["elements"].items())
                {
                    const std::string& symbol = item.key();
                    const auto& measurement = item.value();
                    double value, reference, target;
                    if (!measurement.is_object() || measurement.value("quality", "") != "accepted"
                        || !number(measurement, "dex", value)
                        || !number(bundle["solar"], symbol.c_str(), reference)
                        || !number(solar, symbol.c_str(), target)) return input;
                    const std::string ratio = measurement.value("ratio", "");
                    double error = 0, iron_error = 0;
                    bool has_error = number(measurement, "error", error) && error >= 0;
                    if (ratio == "[" + symbol + "/Fe]" && symbol != "Fe")
                    {
                        const auto& all = bundle["elements"];
                        double iron;
                        if (!all.contains("Fe") || !number(all["Fe"], "dex", iron)
                            || all["Fe"].value("ratio", "") != "[Fe/H]"
                            || all["Fe"].value("quality", "") != "accepted") return input;
                        value += iron;
                        has_error = has_error && number(all["Fe"], "error", iron_error) && iron_error >= 0;
                        error = std::hypot(error, iron_error);
                    }
                    else if (ratio != "[" + symbol + "/H]") return input;
                    value += reference - target;
                    auto& element = elements[symbol];
                    element.sum += value;
                    element.low = std::min(element.low, value);
                    element.high = std::max(element.high, value);
                    element.error = std::max(element.error, error);
                    element.errors = element.errors && has_error;
                    element.annotated = element.annotated || measurement["dex"].is_string();
                    ++element.count;
                }
            }
            if (elements.empty()) return input;
            json summary = {{"model", "element_summary_v1"}, {"solar_scale", "saved_reference"},
                {"solar", solar}, {"elements", json::object()}};
            for (const auto& item : elements)
            {
                const auto& element = item.second;
                json value = {{"dex", element.sum / element.count}, {"ratio", "[" + item.first + "/H]"},
                    {"quality", "accepted"}, {"error", element.errors ? json(element.error) : json(nullptr)}};
                // Retain the spread separately; a catalog disagreement is not an error bar.
                if (element.low != element.high)
                {
                    value["min_dex"] = element.low;
                    value["max_dex"] = element.high;
                }
                if (element.annotated) value["uncertain"] = true;
                summary["elements"][item.first] = std::move(value);
            }
            return {{"Summary", std::move(summary)}};
        }

        inline void compact(json& record)
        {
            if (record.contains("stellar_age") && record["stellar_age"].is_object())
                record["stellar_age"] = select(record["stellar_age"], {"value_gyr", "lower_gyr", "upper_gyr",
                    "limit", "estimated", "source", "uncertain"});
            if (record.contains("stellar_abundances"))
                record["stellar_abundances"] = abundances(record["stellar_abundances"]);
            json* pressure = nullptr;
            if (record.contains("atmosphere") && record["atmosphere"].is_object()
                && record["atmosphere"].contains("pressure_model")
                && record["atmosphere"]["pressure_model"].is_object())
                pressure = &record["atmosphere"]["pressure_model"];
            const json defaults = inventory_defaults();
            if (pressure && !pressure->contains("save_format")
                && (pressure->value("model", "") == "volatile_budget_v5"
                    || pressure->value("model", "") == "cloud_reference_v1")
                && pressure->contains("inventory") && (*pressure)["inventory"].is_object())
            {
                const json original = *pressure;
                json saved = select(original, {"model", "status", "inventory", "inventory_source"});
                saved["save_format"] = 1;
                // Keep baseline differences so a later edit is still recognized as an override.
                if (original.contains("default_inventory") && original["default_inventory"].is_object())
                {
                    saved["default_inventory"] = json::object();
                    for (const auto& item : original["default_inventory"].items())
                        if (!original["inventory"].contains(item.key()) || original["inventory"][item.key()] != item.value())
                            saved["default_inventory"][item.key()] = item.value();
                }
                double age, host_age;
                bool inherited_age = original.contains("stellar_age")
                    && number(original["stellar_age"], "value_gyr", host_age);
                if (!inherited_age && original.value("age_from_host", false)
                    && original.contains("input_state") && original["input_state"].is_array()
                    && original["input_state"].size() > 10)
                    inherited_age = number(original["input_state"][10], "age_gyr", host_age);
                if (inherited_age && number(saved["inventory"], "age_gyr", age) && age == host_age
                    && (!saved.contains("default_inventory") || !saved["default_inventory"].contains("age_gyr")))
                {
                    saved["age_from_host"] = true;
                    saved["inventory"].erase("age_gyr");
                }
                for (const auto& item : defaults.items())
                    if (saved["inventory"].contains(item.key()) && saved["inventory"][item.key()] == item.value())
                        saved["inventory"].erase(item.key());
                const auto& atmosphere = record["atmosphere"];
                if (original.contains("generated_composition") && atmosphere.contains("composition")
                    && original["generated_composition"] == atmosphere["composition"])
                    saved["composition_generated"] = true;
                *pressure = std::move(saved);
            }
            if (record.contains("surface_water_model") && record["surface_water_model"].is_object())
            {
                auto& water = record["surface_water_model"];
                if (water.value("model", "") != "surface_water_v1" || water.contains("save_format")) return;
                json saved = select(water, {"model", "inventory", "level", "coverage_fraction", "equivalent_depth_m",
                    "surface_mass_fraction", "terrain", "terrain_samples", "represented_depth_m"});
                saved["save_format"] = 1;
                // The pressure scenario is the single owner when both models share inputs.
                if (pressure && pressure->value("save_format", 0) == 1 && water.contains("inventory"))
                {
                    json inventory = defaults;
                    inventory.update((*pressure)["inventory"]);
                    json comparison = water["inventory"];
                    if (pressure->value("age_from_host", false)) comparison.erase("age_gyr");
                    if (pressure->value("age_from_host", false)) inventory.erase("age_gyr");
                    if (inventory == comparison) saved.erase("inventory");
                }
                water = std::move(saved);
            }
        }

        inline void restore(Planet& planet)
        {
            CelestialObject* light = planet.get_light_center();
            if (!light || light->typeclass() != class_star) return;
            Star& host = *static_cast<Star*>(light);
            if (planet.atm && planet.atm->pressure_model.is_object()
                && planet.atm->pressure_model.value("save_format", 0) == 1)
            {
                const json saved = planet.atm->pressure_model;
                AtmosphericInventory inventory;
                if (!saved.contains("inventory") || !saved["inventory"].is_object()) return;
                json full_inventory = inventory_defaults();
                full_inventory.update(saved["inventory"]);
                if (!inventory.from_json(full_inventory)) return;
                if (saved.value("age_from_host", false)) inventory.age_gyr = host.estimate_age_gyr();
                if (saved.value("composition_generated", false) && planet.atm->comp)
                    planet.atm->pressure_model["generated_composition"] = planet.atm->comp->to_json();
                AtmosphericEstimate estimate = CosmicShore::estimate_atmosphere(host, planet, inventory);
                if (!estimate.valid) return;
                auto& model = estimate.diagnostics;
                model["inventory_source"] = saved.value("inventory_source", "saved_scenario");
                if (saved.contains("default_inventory"))
                {
                    model["default_inventory"] = inventory.to_json();
                    model["default_inventory"].update(saved["default_inventory"]);
                }
                if (saved.value("age_from_host", false)) model["age_from_host"] = true;
                if (saved.value("composition_generated", false) && planet.atm->comp)
                    model["generated_composition"] = planet.atm->comp->to_json();
                // Physical state belongs to the saved world, even after model revisions.
                model["pressure_pa"] = planet.atm->surface_pressure;
                planet.atm->pressure_model = std::move(model);
            }
            if (planet.surface_water_model.is_object() && planet.surface_water_model.value("save_format", 0) == 1)
            {
                const json saved = planet.surface_water_model;
                AtmosphericInventory inventory;
                const json* budget = planet.atm ? &planet.atm->pressure_model : nullptr;
                const json* scenario = saved.contains("inventory") ? &saved["inventory"]
                    : budget && budget->contains("inventory") ? &(*budget)["inventory"] : nullptr;
                if (!scenario || !inventory.from_json(*scenario)) return;
                AtmosphericEstimate atmosphere;
                if (saved.contains("inventory") || !budget || !budget->contains("reservoir_mass_fractions"))
                {
                    atmosphere = CosmicShore::estimate_atmosphere(host, planet, inventory);
                    if (!atmosphere.valid) return;
                    budget = &atmosphere.diagnostics;
                }
                auto estimate = CosmicShore::estimate_surface_water(planet, *budget);
                if (!estimate.valid) return;
                auto& model = estimate.diagnostics;
                model.update(saved);
                model.erase("save_format");
                model["inventory"] = inventory.to_json();
                model["input_state"] = CosmicShore::input_state(host, planet, inventory);
                planet.surface_water_model = std::move(model);
            }
        }
    }
}
