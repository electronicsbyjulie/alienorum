#include "planetinference.h"
#include "shore.h"
#include "chemistry.h"
#include "../housekeeping.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

using namespace alienorum;

thread_local bool alienorum::planet_environments_deferred = false;

std::recursive_mutex& alienorum::planet_inference_mutex(const Planet& planet)
{
    static std::array<std::recursive_mutex, 64> locks;
    return locks[(reinterpret_cast<std::uintptr_t>(&planet) >> 5) % locks.size()];
}

json alienorum::snapshot_joint_planet_inference(const Planet& planet)
{
    std::unique_lock<std::recursive_mutex> guard(planet_inference_mutex(planet), std::try_to_lock);
    if (!guard.owns_lock() || !planet.classification_evidence.is_object()) return nullptr;
    const auto found = planet.classification_evidence.find("joint_inference");
    return found != planet.classification_evidence.end() && found->is_object() ? *found : json(nullptr);
}

namespace
{
    constexpr int revision = 1;

    json inference_number(double value)
    {
        return std::isfinite(value) ? json(value) : json(nullptr);
    }

    bool authored_composition(const Planet& p)
    {
        return p.atm && p.atm->comp && (!p.atm->pressure_model.is_object()
            || p.atm->pressure_model.value("generated_composition", json(nullptr)) != p.atm->comp->to_json());
    }

    double log_likelihood(double prediction, double observation, double upper, double lower, double scatter)
    {
        const double error = prediction >= observation ? upper : lower;
        const double sigma = std::hypot(std::isfinite(error) ? fabs(error) : .1 * observation, scatter);
        const double residual = (prediction - observation) / std::max(sigma, 1e-12);
        return -.5 * residual * residual - log(std::max(sigma, 1e-12));
    }

    double envelope_radius(double mass_g, double fraction, double flux, double age, double water)
    {
        const double m = mass_g / earth_mass;
        return mass_g >= giant_mass_cutoff
            ? jupiter_radius * std::clamp(pow(m / 317.8, -.04), .7, 1.5)
            : 1.02 * earth_radius * pow(m, .27) * cbrt(1 + 1.7 * water) + 2.06 * earth_radius * pow(m, -.21)
                * pow(fraction / .05, .59) * pow(flux, .044) * pow(std::max(.1, age) / 5, -.18);
    }

    void geometry(Planet& target, const Planet& source)
    {
        target.orbit = new Orbit;
        target.orbit->center = source.orbit->center;
        target.orbit->semimajor_axis = source.orbit->semimajor_axis;
        target.orbit->eccentricity = source.orbit->eccentricity;
        target.orbit->period = source.orbit->period;
        target.albedo = source.albedo;
        target.bond_albedo = source.bond_albedo;
        if (source.is_bv_observed || source.lock_bv_color) target.BV_color = source.BV_color;
        target.sidereal_rotational_period = source.sidereal_rotational_period;
        target.tidal_k2_over_q = source.tidal_k2_over_q;
        target.temperature_supplied = source.has_supplied_mean_temperature();
        target.temperature = target.temperature_supplied ? source.temperature : 0;
        if (authored_composition(source))
            target.ensure_atmosphere()->ensure_composition()->from_json(source.atm->comp->to_json());
    }

    struct Branch
    {
        double log_weight = -INFINITY;
        double mass = 0, radius = 0, core = .33, water = 0, envelope = 0;
        AtmosphericInventory inventory;
        AtmosphericEstimate atmosphere;
        SurfaceWaterEstimate surface;
        double climate = 0;
        std::string category;
        cel_obj_type type = rocky;
    };

    json composition(const AtmosphericEstimate& atmosphere)
    {
        const double total = atmosphere.co2_moles + atmosphere.nitrogen_moles + atmosphere.water_moles;
        return {{"CO2", total > 0 ? atmosphere.co2_moles / total : 0},
            {"N2", total > 0 ? atmosphere.nitrogen_moles / total : 0},
            {"H2O", total > 0 ? atmosphere.water_moles / total : 0}};
    }

    void apply_result(Planet& p, const json& result, bool cached = false)
    {
        const auto& selected = result.at("representative");
        const cel_obj_type old_type = p.type;
        const double old_mass = p.mass, old_radius = p.volumetric_mean_radius;
        p.type = static_cast<cel_obj_type>(selected.at("class_code").get<int>());
        if (p.mass_evidence != PlanetMeasurement::observed)
        {
            if (p.mass_evidence == PlanetMeasurement::minimum && p.msini <= 0) p.msini = p.mass;
            p.mass = selected.at("mass_g");
            p.mass_evidence = PlanetMeasurement::estimated;
        }
        if (p.radius_evidence != PlanetMeasurement::observed)
        {
            p.volumetric_mean_radius = selected.at("radius_m");
            p.radius_evidence = PlanetMeasurement::estimated;
            p.radius_estimate_supplied = false;
        }
        if (!cached)
        {
            Atmosphere* air = p.ensure_atmosphere();
            const bool retain_composition = authored_composition(p);
            if (!air->pressure_supplied) air->surface_pressure = selected.at("pressure_pa");
            air->pressure_model = selected.at("atmosphere_model");
            air->pressure_model["inventory_source"] = "joint_inference";
            air->pressure_model["inference_model"] = "joint_planet_scenarios_v1";
            air->pressure_model["inventory"] = selected.at("inventory");
            air->pressure_model["default_inventory"] = result.at("default_inventory");
            if (!retain_composition)
            {
                AtmosphereComposition* gases = air->ensure_composition();
                *gases = AtmosphereComposition(&p);
                const auto& mixture = selected.at("composition");
                gases->CO2_portion = mixture.value("CO2", 0.0);
                gases->N2_portion = mixture.value("N2", 0.0);
                gases->H2O_portion = mixture.value("H2O", 0.0);
                gases->H2_portion = mixture.value("H2", 0.0);
                gases->He_portion = mixture.value("He", 0.0);
                gases->CH4_portion = mixture.value("CH4", 0.0);
                air->pressure_model["generated_composition"] = gases->to_json();
            }
            air->calculate_tau(air->surface_pressure);
            if (!p.has_water_supplied)
            {
                p.has_water = selected.at("water_level").get<float>();
                p.surface_water_model = selected.at("water_model");
                if (p.surface_water_model.is_object())
                    p.surface_water_model["input_state"] = air->pressure_model.value("input_state", json(nullptr));
            }
            if (!p.has_supplied_mean_temperature())
            {
                p.temperature = selected.at("climate_K");
                p.inferred_mean_temperature = p.temperature;
                p.cached_tidal_flux = p.tidal_heat_flux();
            }
        }
        p.classification_evidence["joint_inference"] = result;
        p.classification_evidence["joint_inference"]["selection_applied"] = true;
        // The atmosphere and terrain already retain their detailed ledgers. Avoid storing
        // another full copy per catalog planet in the ranking cache and universe file.
        p.classification_evidence["joint_inference"]["representative"].erase("atmosphere_model");
        p.classification_evidence["joint_inference"]["representative"].erase("water_model");
        p.classification_evidence["basis"] = "joint_scenario_posterior";
        if (p.classification_evidence.contains("primordial_envelope"))
        {
            p.classification_evidence["primordial_envelope"]["selection_applied"] = false;
            p.classification_evidence["primordial_envelope"]["selection_superseded_by"] = "joint_scenario_posterior";
        }
        if (p.radius_evidence != PlanetMeasurement::observed)
            p.classification_evidence["radius_model"] = "joint_conditional_structure";
        p.classification_evidence["class_code"] = static_cast<int>(p.type);
        p.classification_evidence["radius_evidence"] = static_cast<int>(p.radius_evidence);
        p.classification_evidence["mass_evidence"] = static_cast<int>(p.mass_evidence);
        p.classification_evidence["density_g_cm3"] = p.mass / sphere_volume(p.volumetric_mean_radius) * 1e-6;
        if (old_type != p.type && !p.is_bv_observed) p.set_color_from_type(p.is_in_con_HZ());
        if ((old_type != p.type || old_mass != p.mass || old_radius != p.volumetric_mean_radius)
            && cels && p.seqno >= 0 && p.seqno < ncelobjs && cels[p.seqno] == &p)
        {
            invalidate_frame_objects();
            p.sync_host_statistics();
        }
    }
}

bool alienorum::apply_joint_planet_inference(Star& host, Planet& p, bool allow_initial_calculation)
{
    std::lock_guard<std::recursive_mutex> guard(planet_inference_mutex(p));
    if (!allow_initial_calculation && (!p.classification_evidence.is_object()
        || !p.classification_evidence.contains("joint_inference"))) return false;
    if (p.typeclass() != class_planet || !p.orbit || p.orbit->center != &host
        || (!p.user_added && host.seqno == 0) || p.lock_type || p.preserve_loaded_parameters
        || p.rendering_saved_parameters || !(p.orbit->semimajor_axis > 0)) return false;
    // Older authored bodies have physical values but no observational provenance. Their
    // existing environment model remains authoritative rather than reinterpreting geometry.
    if (p.mass_evidence == PlanetMeasurement::unknown && p.radius_evidence == PlanetMeasurement::unknown
        && !(p.msini > 0)) return false;
    if ((p.mass_evidence == PlanetMeasurement::observed && (!std::isfinite(p.mass) || p.mass <= 0))
        || (p.radius_evidence == PlanetMeasurement::observed && (!std::isfinite(p.volumetric_mean_radius)
            || p.volumetric_mean_radius <= 0))) return false;
    if (p.atm && p.atm->pressure_supplied
        && (!std::isfinite(p.atm->surface_pressure) || p.atm->surface_pressure < 0)) return false;
    const double flux = p.est_bolometric_flux();
    if (!(flux > 0) || !std::isfinite(flux)) return false;
    const bool observed_mass = p.mass_evidence == PlanetMeasurement::observed && p.mass > 0;
    const bool observed_radius = p.radius_evidence == PlanetMeasurement::observed && p.volumetric_mean_radius > 0;
    const double minimum_mass = p.msini > 0 ? p.msini
        : p.mass_evidence == PlanetMeasurement::minimum ? p.mass : 0;
    const json previous = p.classification_evidence.is_object()
        ? p.classification_evidence.value("joint_inference", json(nullptr)) : json(nullptr);
    const bool generated_rv_mass = previous.is_object() && previous.contains("representative")
        && previous["representative"].value("mass_g", 0.0) == p.mass
        && previous.value("rv_minimum_constraint", false) && !previous.value("inclination_mass_constraint", false);
    const bool constrained_inclination = minimum_mass > 0 && p.mass >= minimum_mass
        && (p.transiting || std::isfinite(p.inclination_error_lower) || std::isfinite(p.inclination_error_upper)
            || (p.mass_evidence == PlanetMeasurement::estimated && p.mass > minimum_mass && !generated_rv_mass));
    double mass_prior = !observed_radius && minimum_mass <= 0 && p.mass > 0 ? p.mass : 0;
    if (!observed_mass && previous.is_object() && previous.contains("representative")
        && previous["representative"].value("mass_g", 0.0) == p.mass)
        mass_prior = previous.value("mass_prior_g", mass_prior);
    // Canonical solid geometry prevents an inferred radius/type from becoming new evidence
    // or altering the formation prior on the next evaluation.
    Planet reference;
    geometry(reference, p);
    reference.mass = observed_mass || constrained_inclination ? p.mass
        : minimum_mass > 0 ? minimum_mass : mass_prior > 0 ? mass_prior : earth_mass;
    reference.volumetric_mean_radius = earth_radius * pow(reference.mass / earth_mass, .27);
    reference.type = rocky;
    AtmosphericInventory inventory = CosmicShore::default_inventory(host, reference);
    bool custom_inventory = false;
    if (p.atm && p.atm->pressure_model.is_object() && p.atm->pressure_model.contains("inventory")
        && p.atm->pressure_model.value("inventory_source", "") != "joint_inference")
    {
        const auto& saved = p.atm->pressure_model;
        if (!saved.contains("default_inventory") || saved["inventory"] != saved["default_inventory"])
        {
            AtmosphericInventory authored = inventory;
            if (!authored.from_json(saved["inventory"])) return false;
            inventory = authored;
            custom_inventory = true;
        }
    }
    if (previous.is_object() && previous.contains("representative") && p.atm && p.atm->pressure_model.is_object()
        && p.atm->pressure_model.contains("inventory")
        && p.atm->pressure_model.value("inventory_source", "") == "joint_inference"
        && p.atm->pressure_model["inventory"] != previous["representative"].value("inventory", json(nullptr)))
    {
        AtmosphericInventory authored = inventory;
        if (!authored.from_json(p.atm->pressure_model["inventory"])) return false;
        inventory = authored;
        custom_inventory = true;
    }
    if (previous.is_object() && previous.value("custom_inventory", false)
        && p.atm && p.atm->pressure_model.is_object())
    {
        AtmosphericInventory authored = inventory;
        if (authored.from_json(p.atm->pressure_model.value("inventory", json(nullptr))))
        {
            inventory = authored;
            custom_inventory = true;
        }
    }
    const json chemistry = planet_formation_chemistry(host);
    json input =
    {
        {"revision", revision}, {"observed_mass_g", observed_mass ? inference_number(p.mass) : json(nullptr)},
        {"observed_radius_m", observed_radius ? inference_number(p.volumetric_mean_radius) : json(nullptr)},
        {"mass_prior_g", mass_prior}, {"minimum_mass_g", minimum_mass},
        {"inclination_mass_g", constrained_inclination ? inference_number(p.mass) : json(nullptr)},
        {"mass_error_g", {inference_number(p.mass_error_lower), inference_number(p.mass_error_upper)}},
        {"radius_error_m", {inference_number(p.radius_error_lower), inference_number(p.radius_error_upper)}},
        {"flux_earth", flux}, {"orbit_m", p.orbit->semimajor_axis}, {"eccentricity", p.orbit->eccentricity},
        {"debris_belt", host.has_disk}, {"belt_inner_edge_m", inference_number(host.disk_inner_edge_sma)},
        {"tidal_k2_over_q", inference_number(p.tidal_k2_over_q)}, {"spin_s", p.sidereal_rotational_period},
        {"host_mass_g", inference_number(host.mass)}, {"chemistry", chemistry}, {"inventory", inventory.to_json()},
        {"host_spectrum", host.spectral_type}, {"host_temperature_K", inference_number(host.estimate_temperature())},
        {"host_metallicity", {inference_number(host.metallicity), host.metallicity_ratio, host.metallicity_source,
            inference_number(host.metallicity_error_lower), inference_number(host.metallicity_error_upper)}},
        {"albedo", inference_number(p.albedo)}, {"temperature_K", p.has_supplied_mean_temperature() ? inference_number(p.temperature) : json(nullptr)},
        {"supplied_BV", p.is_bv_observed || p.lock_bv_color ? inference_number(p.BV_color) : json(nullptr)},
        {"pressure_pa", p.atm && p.atm->pressure_supplied ? inference_number(p.atm->surface_pressure) : json(nullptr)},
        {"composition", authored_composition(p) ? p.atm->comp->to_json() : json(nullptr)},
        {"water_level", p.has_water_supplied ? inference_number(p.has_water) : json(nullptr)},
        {"custom_inventory", custom_inventory}
    };
    if (std::isfinite(p.bond_albedo)) input["bond_albedo"] = p.bond_albedo;
    if (previous.is_object() && previous.value("input_state", json(nullptr)) == input)
    {
        apply_result(p, previous, true);
        return true;
    }
    if (!allow_initial_calculation && planet_environments_deferred) return false;
    std::vector<std::pair<double, double>> masses;
    if (observed_mass || constrained_inclination)
    {
        const double low = std::isfinite(p.mass_error_lower) ? fabs(p.mass_error_lower) : 0;
        const double high = std::isfinite(p.mass_error_upper) ? fabs(p.mass_error_upper) : 0;
        masses = {{std::max(p.mass * 1e-6, p.mass - low), .25}, {p.mass, .5}, {p.mass + high, .25}};
    }
    else if (minimum_mass > 0)
    {
        // Equal bins of cos(i) for an isotropic, otherwise unknown RV inclination.
        for (double cosine : {1.0 / 6, .5, 5.0 / 6})
            masses.push_back({minimum_mass / sqrt(1 - cosine * cosine), 1.0 / 3});
    }
    else if (mass_prior > 0 && !observed_radius)
        masses = {{mass_prior / 2, .25}, {mass_prior, .5}, {mass_prior * 2, .25}};
    else
    {
        // A log-uniform fallback, not a measured occurrence distribution. Quarter-dex
        // spacing resolves the structural likelihood without assuming a radius-derived mass.
        for (int step = 0; step < 27; ++step)
            masses.push_back({pow(10.0, -3 + .25 * step) * earth_mass, 1.0 / 27});
    }
    double core_center = .33;
    if (chemistry.value("applicable", false) && chemistry.contains("iron_to_rock"))
        core_center = std::clamp(chemistry.value("iron_to_rock", .33), .05, .7);
    std::map<double, double> gas_priors;
    for (const auto& mass : masses)
    {
        reference.mass = mass.first;
        reference.volumetric_mean_radius = earth_radius * pow(mass.first / earth_mass, .27);
        const json envelope = CosmicShore::envelope_scenario(host, reference, inventory);
        const auto empirical = planet_envelope_prior(host, mass.first / earth_mass, flux);
        gas_priors[mass.first] = std::clamp(mass.first >= giant_mass_cutoff ? .995
            : envelope.value("retained_envelope_score", empirical.applicable ? empirical.probability : .2), .005, .995);
    }
    const std::array<double, 5> waters{{0, inventory.water_mass_fraction, .01, .1, .5}};
    std::array<double, 5> water_priors{{.15, .55, .1, .12, .08}};
    const double delivery_weight = std::clamp(pow(std::max(1e-12, inventory.water_mass_fraction) / .00069, .25), .2, 5.0);
    double water_normalization = 0;
    for (size_t i = 0; i < water_priors.size(); ++i)
    {
        if (i >= 2) water_priors[i] *= delivery_weight;
        water_normalization += water_priors[i];
    }
    for (double& weight : water_priors) weight /= water_normalization;
    std::vector<Branch> branches;
    double max_log_weight = -INFINITY;
    double core_normalization = 0;
    for (double core : {.1, .33, .65}) core_normalization += exp(-.5 * pow((core - core_center) / .2, 2));
    const int water_count = custom_inventory ? 1 : 5;
    const int gas_count = custom_inventory ? 1 : 3;
    // Marginalize structural uncertainties cheaply. Run a full coupled atmosphere/water
    // calculation only for the representative structure of each volatile branch.
    for (int w = 0; w < water_count; ++w)
    {
        for (int g = 0; g < gas_count; ++g)
        {
            Branch branch;
            branch.water = custom_inventory ? inventory.water_mass_fraction : waters[w];
            branch.inventory = inventory;
            branch.inventory.water_mass_fraction = branch.water;
            if (p.has_water_supplied && p.has_water == 0) branch.inventory.surface_water_share = 0;
            const double gas_scale = custom_inventory ? 1 : std::array<double, 3>{{.1, 1, 10}}[g];
            branch.inventory.co2_mass_fraction *= gas_scale;
            branch.inventory.nitrogen_mass_fraction *= gas_scale;
            branch.inventory.co2_mass_fraction = std::min(.05, branch.inventory.co2_mass_fraction);
            branch.inventory.nitrogen_mass_fraction = std::min(.01, branch.inventory.nitrogen_mass_fraction);
            double sum = 0, best = -INFINITY;
            std::vector<double> terms;
            for (const auto& mass : masses)
            {
                for (double core : {.1, .33, .65})
                {
                    // Zeng et al. 2016 (1512.08827), plus an explicit compressed-water
                    // specific-volume approximation. Broad model scatter covers extrapolation.
                    const double radius = (1.07 - .21 * core) * earth_radius * pow(mass.first / earth_mass, 1 / 3.7)
                        * cbrt(1 + 1.7 * branch.water);
                    double weight = log(mass.second) + log(1 - gas_priors.at(mass.first))
                        - .5 * pow((core - core_center) / .2, 2) - log(core_normalization);
                    if (observed_radius)
                        weight += log_likelihood(radius / earth_radius, p.volumetric_mean_radius / earth_radius,
                            p.radius_error_upper / earth_radius, p.radius_error_lower / earth_radius, .08 * radius / earth_radius);
                    terms.push_back(weight);
                    if (weight > best)
                    {
                        best = weight;
                        branch.mass = observed_mass || constrained_inclination ? p.mass : mass.first;
                        branch.radius = observed_radius ? p.volumetric_mean_radius
                            : observed_mass || constrained_inclination ? radius * pow(p.mass / mass.first, 1 / 3.7) : radius;
                        branch.core = core;
                    }
                }
            }
            for (double term : terms) sum += exp(term - best);
            branch.log_weight = best + log(sum)
                + log(custom_inventory ? 1 : water_priors[w])
                + log(custom_inventory ? 1 : std::array<double, 3>{{.2, .6, .2}}[g]);
            Planet candidate;
            geometry(candidate, p);
            candidate.mass = branch.mass;
            candidate.volumetric_mean_radius = branch.radius;
            candidate.type = rocky;
            branch.atmosphere = CosmicShore::estimate_atmosphere(host, candidate, branch.inventory);
            if (!branch.atmosphere.valid) continue;
            branch.atmosphere.diagnostics["age_origin"] = custom_inventory ? "scenario_inventory"
                : host.fictional_origin.is_object() && host.fictional_origin.contains("age_gyr")
                    ? "stellar_formation_scenario" : host.age_diagnostics().value("source", "stellar_age_estimate");
            branch.surface = CosmicShore::estimate_surface_water(candidate, branch.atmosphere.diagnostics);
            if (!branch.surface.valid) continue;
            branch.climate = branch.atmosphere.diagnostics.value("climate_proxy_k", 255 * pow(flux, .25));
            double pressure = branch.atmosphere.pressure;
            if (p.atm && p.atm->pressure_supplied)
            {
                branch.log_weight += log_likelihood(log1p(pressure), log1p(p.atm->surface_pressure), 1.5, 1.5, .5);
                pressure = p.atm->surface_pressure;
                if (!candidate.has_supplied_mean_temperature())
                {
                    AtmosphereComposition mixture(&candidate);
                    mixture.from_json(composition(branch.atmosphere));
                    branch.climate = CosmicShore::radiative_temperature(candidate, pressure,
                        authored_composition(candidate) ? candidate.atm->comp : &mixture, false, &branch.inventory);
                }
            }
            if (p.has_water_supplied)
                branch.log_weight += log_likelihood(branch.surface.level, p.has_water, .1, .1, .05);
            const double coverage = p.has_water_supplied ? (p.has_water >= 1 ? 1 : p.has_water > 0 ? .5 : 0)
                : branch.surface.diagnostics.value("coverage_fraction", 0.0);
            const double boiling = pressure > 611.657
                ? std::clamp(1 / (1 / 373.15 - 8.314462618 / 40660 * log(pressure / oneatm)), 273.16, 647.096) : 273.15;
            if (branch.climate > lava_T_cutoff) { branch.category = "lava_world"; branch.type = lavaworld; }
            else if (branch.climate < water_freezing && coverage > .05) { branch.category = "ice_world"; branch.type = icy; }
            else if (branch.climate >= boiling && branch.water > .001) { branch.category = "steam_world"; branch.type = rocky; }
            else if (coverage >= .95 && pressure > 611.657) { branch.category = "ocean_world"; branch.type = waterworld; }
            else if (coverage > .05 && pressure > 611.657) { branch.category = "mixed_land_and_water"; branch.type = rocky; }
            else { branch.category = "dry_rock"; branch.type = rocky; }
            max_log_weight = std::max(max_log_weight, branch.log_weight);
            branches.push_back(std::move(branch));
        }
    }
    // Retained H/He alternatives compete with water-rich interiors for the same radius.
    for (double fraction : {.001, .01, .1})
    {
        for (int w = 0; w < water_count; ++w)
        {
            if (p.atm && p.atm->pressure_supplied && p.atm->surface_pressure == 0) continue;
            Branch branch;
            branch.water = custom_inventory ? inventory.water_mass_fraction : waters[w];
            double best = -INFINITY, sum = 0;
            std::vector<double> terms;
            for (const auto& mass : masses)
            {
                const double radius = envelope_radius(mass.first, fraction, flux, inventory.age_gyr, branch.water);
                double weight = log(mass.second) + log(gas_priors.at(mass.first));
                if (observed_radius)
                    weight += log_likelihood(radius / earth_radius, p.volumetric_mean_radius / earth_radius,
                        p.radius_error_upper / earth_radius, p.radius_error_lower / earth_radius, .15 * radius / earth_radius);
                terms.push_back(weight);
                if (weight > best)
                {
                    best = weight;
                    branch.mass = observed_mass || constrained_inclination ? p.mass : mass.first;
                    branch.radius = observed_radius ? p.volumetric_mean_radius
                        : envelope_radius(branch.mass, fraction, flux, inventory.age_gyr, branch.water);
                }
            }
            for (double term : terms) sum += exp(term - best);
            branch.log_weight = best + log(sum) + log(fraction == .01 ? .6 : .2)
                + log(custom_inventory ? 1 : water_priors[w]);
            // Authored vacuum or surface water strongly disfavors a deep primordial envelope.
            if (p.atm && p.atm->pressure_supplied && p.atm->surface_pressure < .1 * oneatm) branch.log_weight -= 20;
            if (p.has_water_supplied && p.has_water > 0) branch.log_weight -= 20;
            if (authored_composition(p))
            {
                const double hydrogen_helium = p.atm->comp->H2_portion + p.atm->comp->He_portion;
                branch.log_weight -= .5 * pow((.995 - hydrogen_helium) / .1, 2);
            }
            branch.envelope = branch.mass >= giant_mass_cutoff ? .8 : fraction;
            if (branch.envelope + branch.water + inventory.co2_mass_fraction + inventory.nitrogen_mass_fraction >= 1) continue;
            branch.inventory = inventory;
            branch.inventory.water_mass_fraction = branch.water;
            branch.type = branch.mass >= giant_mass_cutoff ? gas_giant : ice_giant;
            if (branch.type == gas_giant)
            {
                if (p.orbit->period > 0 && p.orbit->period < 10 * oneday) branch.type = hot_jupiter;
                else if (255 * pow(flux, .25) >= 350 && 255 * pow(flux, .25) < 800) branch.type = clearskies;
            }
            branch.category = branch.mass >= giant_mass_cutoff ? "gas_giant" : "hydrogen_envelope";
            branch.atmosphere.valid = true;
            branch.atmosphere.pressure = 1e5;
            branch.atmosphere.diagnostics = {{"model", "cloud_reference_v1"}, {"status", "conventional_1_bar_level"}};
            Planet envelope_reference;
            geometry(envelope_reference, p);
            envelope_reference.mass = branch.mass;
            envelope_reference.volumetric_mean_radius = branch.radius;
            envelope_reference.type = branch.type;
            branch.atmosphere.diagnostics["primordial_envelope"] = CosmicShore::envelope_scenario(host, envelope_reference, inventory);
            branch.climate = envelope_reference.effective_radiating_temperature();
            max_log_weight = std::max(max_log_weight, branch.log_weight);
            branches.push_back(std::move(branch));
        }
    }
    if (branches.empty() || !std::isfinite(max_log_weight)) return false;
    std::map<std::string, double> totals;
    double normalization = 0;
    for (const auto& branch : branches)
    {
        const double weight = exp(branch.log_weight - max_log_weight);
        normalization += weight;
        totals[branch.category] += weight;
    }
    const auto winning = std::max_element(totals.begin(), totals.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    const Branch* selected = nullptr;
    for (const auto& branch : branches)
        if (branch.category == winning->first && (!selected || branch.log_weight > selected->log_weight)) selected = &branch;
    json alternatives = json::array();
    for (const auto& total : totals)
    {
        const Branch* representative = nullptr;
        for (const auto& branch : branches)
            if (branch.category == total.first && (!representative || branch.log_weight > representative->log_weight)) representative = &branch;
        alternatives.push_back({{"category", total.first}, {"conditional_probability", total.second / normalization},
            {"mass_g", representative->mass}, {"radius_m", representative->radius},
            {"bulk_water_mass_fraction", representative->water}, {"hydrogen_envelope_mass_fraction", representative->envelope},
            {"pressure_pa", representative->atmosphere.pressure}, {"climate_K", representative->climate}});
    }
    json mixture = authored_composition(p) ? p.atm->comp->to_json()
        : selected->envelope > 0 ? json{{"H2", .85}, {"He", .145}, {"CH4", .005}} : composition(selected->atmosphere);
    AtmosphereComposition normalized(&p);
    normalized.from_json(mixture);
    mixture = normalized.to_json();
    const json representative =
    {
        {"class_code", static_cast<int>(selected->type)}, {"mass_g", selected->mass}, {"radius_m", selected->radius},
        {"core_mass_fraction_of_dry_interior", selected->core}, {"bulk_water_mass_fraction", selected->water},
        {"hydrogen_envelope_mass_fraction", selected->envelope}, {"inventory", selected->inventory.to_json()},
        {"pressure_pa", p.atm && p.atm->pressure_supplied ? p.atm->surface_pressure : selected->atmosphere.pressure},
        {"predicted_pressure_pa", selected->atmosphere.pressure}, {"composition", mixture},
        {"climate_K", p.has_supplied_mean_temperature() ? p.temperature : selected->climate},
        {"water_level", p.has_water_supplied ? p.has_water : selected->surface.valid ? selected->surface.level : 0},
        {"atmosphere_model", selected->atmosphere.diagnostics}, {"water_model", selected->surface.diagnostics}
    };
    const json result =
    {
        {"model", "joint_planet_scenarios_v1"}, {"input_state", input}, {"mass_prior_g", mass_prior},
        {"selection_applied", true},
        {"custom_inventory", custom_inventory}, {"default_inventory", inventory.to_json()},
        {"selected_category", winning->first}, {"selected_probability", winning->second / normalization},
        {"alternatives", alternatives}, {"representative", representative},
        {"selection", "highest_integrated_category_weight_then_highest_weight_joint_representative"},
        {"probability_interpretation", "Conditional_on_explicit_approximate_models_and_assumed_priors;not_calibrated_occurrence_rates"},
        {"independent_radius_constraint", observed_radius}, {"independent_mass_constraint", observed_mass},
        {"rv_minimum_constraint", minimum_mass > 0}, {"structural_mass_samples", masses.size()},
        {"inclination_mass_constraint", constrained_inclination},
        {"prior_dominated", !observed_mass || !observed_radius},
        {"water_branch_priors", water_priors}, {"gas_inventory_scale_priors", {.2, .6, .2}},
        {"core_prior", {{"mean", core_center}, {"sigma", .2}, {"grid", {.1, .33, .65}}}},
        {"mass_prior", observed_mass ? "supplied_mass_and_errors" : constrained_inclination
            ? "supplied_RV_deprojection_or_inclination" : minimum_mass > 0 ? "isotropic_RV_inclination"
            : mass_prior > 0 ? "broad_supplied_mass_estimate" : "log_uniform_0.001_to_3162_Earth_masses"},
        {"climate_branches", branches.size()},
        {"limitations", "Broad_formation_and_gas_priors;compressed_water_volume_proxy;representative_climate_per_volatile_branch;no_detailed_interior_or_spectral_retrieval"},
        {"sources", {"https://arxiv.org/abs/1512.08827", "https://arxiv.org/abs/1311.0329", "https://arxiv.org/abs/1609.03908"}}
    };
    apply_result(p, result);
    return true;
}
