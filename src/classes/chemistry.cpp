#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include "chemistry.h"
#include "cat.h"
#include "star.h"

using namespace alienorum;

namespace
{
    double numeric(const json& object, const std::string& key)
    {
        auto it = object.find(key);
        if (it == object.end() || !it->is_number()) return NAN;
        const double value = it->get<double>();
        return std::isfinite(value) ? value : NAN;
    }

    std::string text_field(const json& object, const std::string& key)
    {
        auto it = object.find(key);
        return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    std::string normalized(const std::string& name)
    {
        std::string result;
        for (unsigned char c : name) if (c != ' ' && c != '-') result += std::tolower(c);
        return result;
    }

    double abundance_log(const json& elements, const json& solar, const std::string& element)
    {
        if (!elements.contains(element) || !elements[element].is_object()) return NAN;
        const json& measurement = elements[element];
        if (text_field(measurement, "quality") != "accepted") return NAN;
        const std::string ratio = text_field(measurement, "ratio");
        double value = numeric(measurement, "dex");
        if (ratio == "[" + element + "/Fe]" && element != "Fe")
        {
            if (!elements.contains("Fe") || text_field(elements["Fe"], "ratio") != "[Fe/H]" ||
                text_field(elements["Fe"], "quality") != "accepted") return NAN;
            value += numeric(elements["Fe"], "dex");
        }
        else if (ratio != "[" + element + "/H]") return NAN;
        return value + numeric(solar, element);
    }

    double iron_proxy(const json& elements, const json& solar)
    {
        const double fe_si = pow(10.0, abundance_log(elements, solar, "Fe") - abundance_log(elements, solar, "Si"));
        const double mg_si = pow(10.0, abundance_log(elements, solar, "Mg") - abundance_log(elements, solar, "Si"));
        if (!std::isfinite(fe_si) || !std::isfinite(mg_si) || fe_si <= 0 || mg_si <= 0 || mg_si > 2) return NAN;
        const double iron = 55.845 * fe_si;
        const double silicates = 24.305 * mg_si + 28.085 + 15.999 * (mg_si + 2);
        return iron / (iron + silicates);
    }

    json classification_model()
    {
        // A separate entry per working directory keeps fixture data isolated from the app.
        static std::mutex mutex;
        static std::map<std::string, json> models;
        const std::string path = std::filesystem::absolute("catalogs/NASA/classification_model.json").string();
        std::lock_guard<std::mutex> lock(mutex);
        auto found = models.find(path);
        if (found != models.end()) return found->second;
        std::ifstream input(path);
        json model = input ? json::parse(input, nullptr, false) : json(nullptr);
        if (!model.is_object()) model = json::object();
        models.emplace(path, model);
        return model;
    }
}

bool Star::calibrated_iron_abundance(double& value, double& uncertainty) const
{
    if (metallicity_ratio != "[Fe/H]" || !std::isfinite(metallicity) ||
        !std::isfinite(metallicity_error_upper) || !std::isfinite(metallicity_error_lower) ||
        metallicity_source.empty() || metallicity_source.rfind("XHIP V/137D C", 0) == 0) return false;
    value = metallicity;
    uncertainty = fmax(fabs(metallicity_error_upper), fabs(metallicity_error_lower));
    return true;
}

json Star::chemistry_diagnostics() const
{
    json result = json::array();
    if (!stellar_abundances.is_object()) return result;
    for (const auto& item : stellar_abundances.items())
    {
        const json& bundle = item.value();
        if (!bundle.is_object() || !bundle.contains("elements") || !bundle["elements"].is_object() ||
            !bundle.contains("solar") || !bundle["solar"].is_object()) continue;
        const json& elements = bundle["elements"];
        const json& solar = bundle["solar"];
        json diagnostic =
        {
            {"source", item.key()}, {"solar_scale", text_field(bundle, "solar_scale")},
            {"uncertainty_interpretation", "approximate_input_scales_without_inter_measurement_covariance"}
        };
        const auto ratio = [&](const std::string& label, const std::string& first, const std::string& second)
        {
            const double logarithm = abundance_log(elements, solar, first) - abundance_log(elements, solar, second);
            if (!std::isfinite(logarithm) || fabs(logarithm) > 8) return;
            const double value = pow(10.0, logarithm);
            std::map<std::string, double> weights;
            weights[first] += 1;
            weights[second] -= 1;
            if (text_field(elements[first], "ratio") == "[" + first + "/Fe]" && first != "Fe") weights["Fe"] += 1;
            if (text_field(elements[second], "ratio") == "[" + second + "/Fe]" && second != "Fe") weights["Fe"] -= 1;
            double variance = 0;
            bool error_available = true;
            for (const auto& weight : weights)
            {
                if (fabs(weight.second) < 1e-12) continue;
                const double error = numeric(elements[weight.first], "error");
                if (!std::isfinite(error) || error < 0) error_available = false;
                else variance += pow(weight.second * error, 2);
            }
            diagnostic[label] =
            {
                {"value", value},
                {"error", error_available ? json(log(10.0) * value * sqrt(variance)) : json(nullptr)},
                {"error_method", "first_order_independent_measurements"}
            };
        };
        ratio("Fe_Mg", "Fe", "Mg");
        ratio("Mg_Si", "Mg", "Si");
        ratio("C_O", "C", "O");
        const double proxy = iron_proxy(elements, solar);
        if (std::isfinite(proxy))
        {
            double variance = 0;
            bool error_available = true;
            for (const char* element : {"Fe", "Mg", "Si"})
            {
                const double error = numeric(elements[element], "error");
                if (!std::isfinite(error) || error < 0) { error_available = false; break; }
                json perturbed = elements;
                perturbed[element]["dex"] = numeric(elements[element], "dex") + 1e-5;
                const double derivative = (iron_proxy(perturbed, solar) - proxy) / 1e-5;
                if (!std::isfinite(derivative)) { error_available = false; break; }
                variance += pow(derivative * error, 2);
            }
            diagnostic["primordial_iron_to_silicate_proxy"] =
            {
                {"value", proxy}, {"error", error_available ? json(sqrt(variance)) : json(nullptr)},
                {"assumptions", "metallic_Fe_and_Mg_Si_silicates_with_sufficient_oxygen"},
                {"error_method", "first_order_independent_measurements"}, {"planetary_core_fraction", nullptr}
            };
        }
        if (diagnostic.size() > 3) result.push_back(std::move(diagnostic));
    }
    return result;
}

void CatalogReader::apply_stellar_abundances(Star* host)
{
    if (!host) return;
    const auto keys = [](const json& row)
    {
        std::vector<std::string> result;
        for (const char* field : {"hip", "hd", "gaia"})
        {
            if (row.contains(field) && row[field].is_number_integer() && numeric(row, field) > 0)
                result.push_back(std::string(field) + ":" + std::to_string(row[field].get<uint64_t>()));
        }
        if (row.contains("name") && row["name"].is_string()) result.push_back("name:" + normalized(row["name"].get<std::string>()));
        return result;
    };
    if (!abundance_catalogs_loaded)
    {
        abundance_catalogs_loaded = true;
        std::set<uint64_t> hips, hds, gaia;
        for (int i = 0; cels && i < ncelobjs; ++i)
        {
            if (!cels[i] || cels[i]->typeclass() != class_star) continue;
            const Star* object = static_cast<Star*>(cels[i]);
            if (object->HIP && !hips.insert(object->HIP).second) duplicate_abundance_hips.insert(object->HIP);
            if (object->HD && !hds.insert(object->HD).second) duplicate_abundance_hds.insert(object->HD);
            if (object->gaia_source_id && !gaia.insert(object->gaia_source_id).second) duplicate_abundance_gaia.insert(object->gaia_source_id);
        }
        for (const char* path : {"catalogs/Hypatia/stellar_abundances.json", "catalogs/GALAH/stellar_abundances.json"})
        {
            std::ifstream source(path);
            if (!source) continue;
            json table = json::parse(source, nullptr, false);
            if (!table.is_object() || table.value("schema", 0) != 1 || !table.contains("rows") ||
                !table["rows"].is_array() || !table.contains("source") || !table["source"].is_string()) continue;
            const size_t catalog = abundance_catalogs.size();
            abundance_catalogs.push_back(std::move(table));
            const json& rows = abundance_catalogs.back()["rows"];
            for (size_t row = 0; row < rows.size(); ++row)
                for (const auto& key : keys(rows[row])) abundance_lookup[key].push_back({catalog, row});
        }
    }
    std::vector<std::string> identifiers;
    if (host->HIP && host->HIP <= MAX_HIP && !duplicate_abundance_hips.count(host->HIP))
        identifiers.push_back("hip:" + std::to_string(host->HIP));
    if (host->HD && host->HD <= MAX_HD && !duplicate_abundance_hds.count(host->HD))
        identifiers.push_back("hd:" + std::to_string(host->HD));
    if (host->gaia_source_id && !duplicate_abundance_gaia.count(host->gaia_source_id))
        identifiers.push_back("gaia:" + std::to_string(host->gaia_source_id));
    identifiers.push_back("name:" + normalized(host->name));
    for (const auto& alias : host->catalog_aliases) identifiers.push_back("name:" + normalized(alias));
    std::map<size_t, std::set<size_t>> matches;
    for (const auto& key : identifiers)
    {
        auto it = abundance_lookup.find(key);
        if (it == abundance_lookup.end()) continue;
        for (const auto& match : it->second) matches[match.first].insert(match.second);
    }
    for (const auto& match : matches)
    {
        if (match.second.size() != 1) continue;
        const json& table = abundance_catalogs[match.first];
        const json& row = table["rows"][*match.second.begin()];
        if (!row.contains("elements") || !row["elements"].is_object()) continue;
        const bool unique_identity =
            (host->HIP && !duplicate_abundance_hips.count(host->HIP) && numeric(row, "hip") == host->HIP) ||
            (host->HD && !duplicate_abundance_hds.count(host->HD) && numeric(row, "hd") == host->HD) ||
            (host->gaia_source_id && !duplicate_abundance_gaia.count(host->gaia_source_id) &&
                row.contains("gaia") && row["gaia"].is_number_integer() && row["gaia"].get<uint64_t>() == host->gaia_source_id);
        const bool ambiguous_identity = duplicate_abundance_hips.count(host->HIP) || duplicate_abundance_hds.count(host->HD) ||
            duplicate_abundance_gaia.count(host->gaia_source_id);
        if (ambiguous_identity && !unique_identity) continue;
        bool conflict = false;
        for (const auto& field : {std::make_pair("hip", host->HIP), std::make_pair("hd", host->HD)})
        {
            const double id = numeric(row, field.first);
            if (field.second && std::isfinite(id) && id > 0 && id != field.second) conflict = true;
        }
        if (conflict) continue;
        if (!host->stellar_abundances.is_object()) host->stellar_abundances = json::object();
        host->stellar_abundances[text_field(table, "source")] =
        {
            {"solar_scale", text_field(table, "solar_scale")}, {"solar", table.value("solar", json::object())},
            {"elements", row["elements"]}, {"reference", text_field(table, "reference")}
        };
        if (!std::isfinite(host->metallicity) && row["elements"].contains("Fe"))
        {
            const json& iron = row["elements"]["Fe"];
            const double value = numeric(iron, "dex");
            if (std::isfinite(value) && text_field(iron, "ratio") == "[Fe/H]")
            {
                host->metallicity = value;
                host->metallicity_ratio = "[Fe/H]";
                host->metallicity_source = text_field(table, "source");
                const double error = numeric(iron, "error");
                if (std::isfinite(error) && error >= 0)
                {
                    host->metallicity_error_upper = error;
                    host->metallicity_error_lower = -error;
                }
            }
        }
    }
}

MetallicityPrior alienorum::planet_envelope_prior(const Star& host, double earth_masses, double irradiation, double mass_relative_error)
{
    MetallicityPrior result;
    double metal, error;
    if (!host.calibrated_iron_abundance(metal, error)) { result.reason = "unqualified_iron_abundance"; return result; }
    const char* spectrum = host.spectral_type;
    if (*spectrum == 'd') ++spectrum;
    if (!spectrum[0] || !spectrum[1] || !std::isdigit(static_cast<unsigned char>(spectrum[1])))
    {
        result.reason = "outside_stellar_domain";
        return result;
    }
    const double subtype = std::strtod(spectrum + (spectrum[0] ? 1 : 0), nullptr);
    if (!host.is_main_sequence() || !((spectrum[0] == 'F' && subtype >= 8) || spectrum[0] == 'G' ||
        (spectrum[0] == 'K' && subtype <= 2))) { result.reason = "outside_stellar_domain"; return result; }
    const json model = classification_model();
    if (!model.contains("schema") || !model["schema"].is_number_integer() ||
        (model["schema"] != 1 && model["schema"] != 2) ||
        !model.contains("enabled") || !model["enabled"].is_boolean() || !model["enabled"].get<bool>())
    {
        result.reason = "metallicity_not_validated";
        return result;
    }
    result.class_balanced = model["schema"] == 2;
    if (result.class_balanced && (text_field(model, "training") != "class_balanced_logistic" ||
        numeric(model, "decision_threshold") != 0.5))
    {
        result.reason = "invalid_model";
        return result;
    }
    if (!result.class_balanced && (!model.contains("improvement_95_percent_host_bootstrap") ||
        !model["improvement_95_percent_host_bootstrap"].is_array() ||
        model["improvement_95_percent_host_bootstrap"].size() != 2 ||
        !model["improvement_95_percent_host_bootstrap"][0].is_number() ||
        !std::isfinite(model["improvement_95_percent_host_bootstrap"][0].get<double>()) ||
        !(model["improvement_95_percent_host_bootstrap"][0].get<double>() > 0)))
    {
        result.reason = "metallicity_not_validated";
        return result;
    }
    const auto within = [&](const char* key, double value)
    {
        if (!std::isfinite(value) || !model.contains("domain") || !model["domain"].contains(key)) return false;
        const json& bounds = model["domain"][key];
        return bounds.is_array() && bounds.size() == 2 && bounds[0].is_number() && bounds[1].is_number() &&
            value >= bounds[0].get<double>() && value <= bounds[1].get<double>();
    };
    if (!(earth_masses > 0) || !(irradiation > 0) || !within("mass_earth", earth_masses) || !within("irradiation_earth", irradiation) ||
        !within("metallicity_dex", metal) || !within("temperature_k", host.temperature) ||
        !std::isfinite(numeric(model, "maximum_metallicity_error")) || error > numeric(model, "maximum_metallicity_error") ||
        !std::isfinite(mass_relative_error) || mass_relative_error < 0 ||
        !std::isfinite(numeric(model, "maximum_mass_relative_error")) || mass_relative_error > numeric(model, "maximum_mass_relative_error"))
    {
        result.reason = "outside_calibration_domain";
        return result;
    }
    const auto evaluate = [&](const char* key, bool metallicity) -> double
    {
        const size_t count = metallicity ? 4 : 3;
        if (!model.contains(key) || !model[key].is_array() || model[key].size() != count) return NAN;
        const double features[] = {1, log10(earth_masses), log10(irradiation), metal};
        double score = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (!model[key][i].is_number() || !std::isfinite(model[key][i].get<double>())) return NAN;
            score += model[key][i].get<double>() * features[i];
        }
        return 1.0 / (1.0 + exp(-std::clamp(score, -40.0, 40.0)));
    };
    result.baseline = evaluate("baseline_coefficients", false);
    result.probability = evaluate("metallicity_coefficients", true);
    result.applicable = std::isfinite(result.baseline) && std::isfinite(result.probability);
    result.reason = result.applicable
        ? result.class_balanced ? "balanced_large_radius_prior" : "validated_large_radius_prior"
        : "invalid_model";
    return result;
}
