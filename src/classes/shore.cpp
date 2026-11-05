#include "shore.h"
#include "numeric.h"
#include "chemistry.h"
#include "thermalclimate.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace alienorum;

namespace
{
    constexpr double reference_age = 4.5; // Gyr

    double stellar_flux(Star& s, Planet& p)
    {
        if (p.get_light_center() != &s) return NAN;
        CelestialObject* orbiter = &p;
        while (orbiter->orbit && orbiter->orbit->center != &s) orbiter = orbiter->orbit->center;
        if (!orbiter->orbit || !std::isfinite(orbiter->orbit->semimajor_axis)
            || orbiter->orbit->semimajor_axis <= 0 || !std::isfinite(orbiter->orbit->eccentricity)
            || orbiter->orbit->eccentricity < 0 || orbiter->orbit->eccentricity >= 1) return NAN;
        return p.est_bolometric_flux() / std::sqrt(1 - std::pow(orbiter->orbit->eccentricity, 2));
    }

    double transition(double value)
    {
        return 0.5 + 0.5 * std::tanh(value * 0.5);
    }

    // Integral of a saturated XUV phase followed by a t^-1.2 decline, in Gyr.
    double activity_integral(double age, double saturation_time)
    {
        if (age <= saturation_time) return age;
        return saturation_time * (1 + (1 - std::pow(age / saturation_time, -0.2)) / 0.2);
    }

    // Exponential mantle release into a reservoir with a constant effective loss rate.
    // This closed form conserves the initial inventory between mantle, retained material,
    // and escape. It approximates a history, rather than solving a coupled climate model.
    double released_and_retained(double age, double release_time, double loss_rate)
    {
        const double a = age / release_time;
        const double b = age * loss_rate;
        const double delta = std::abs(a - b);
        const double ratio = delta > 1e-8 ? -std::expm1(-delta) / delta : 1 - delta * 0.5;
        return std::clamp(a * std::exp(-std::min(a, b)) * ratio, 0.0, 1.0);
    }

    double vapor_pressure(double temperature, double reference_k, double reference_pa,
        double latent_heat, double critical_k)
    {
        if (temperature >= critical_k) return INFINITY;
        const double exponent = latent_heat / 8.314462618 * (1 / reference_k - 1 / temperature);
        return std::min(CosmicShore::pressure_ceiling, reference_pa * std::exp(std::min(100.0, exponent)));
    }

    bool cold_volatile_regime(const Planet& p, double flux)
    {
        const double density = p.mass * 0.001 / (4 * _pi / 3 * std::pow(p.volumetric_mean_radius, 3));
        return std::isfinite(density) && density > 0 && density < 3000 && flux < 0.08;
    }

    struct FormationDelivery
    {
        double mass_factor = 1, water_factor = 1, heavy_factor = 1;
        double orbit_au = 1, snowline_au = 2.7, outer_fraction = 0, ice_fraction = 0;
        bool cold = false;
    };

    FormationDelivery formation_delivery(Star& s, Planet& p)
    {
        FormationDelivery result;
        const CelestialObject* body = &p;
        int depth = 0;
        while (body->orbit && body->orbit->center && body->orbit->center != &s && ++depth < 32) body = body->orbit->center;
        if (body->orbit && body->orbit->center == &s && body->orbit->semimajor_axis > 0)
            result.orbit_au = body->orbit->semimajor_axis / AU;
        const double stellar_mass = std::isfinite(s.mass) && s.mass > 0 ? s.mass / solar_mass : 1;
        // Young-disc luminosity ~Mstar^2 and a 2.7 AU Solar reference are weak formation priors.
        // Present orbit is not a measured birth location: migration alternatives remain open.
        // Water delivery review: https://arxiv.org/abs/1801.05456.
        result.snowline_au = 2.7 * std::clamp(stellar_mass, .08, 10.0);
        result.outer_fraction = 1 / (1 + std::pow(result.snowline_au / result.orbit_au, 4));
        const double reference_outer = 1 / (1 + std::pow(2.7, 4));
        result.water_factor = (.001 + .02 * result.outer_fraction) / (.001 + .02 * reference_outer);
        const double mass = std::isfinite(p.mass) && p.mass > 0 ? p.mass / earth_mass : 1;
        // A broad accretion/impact-retention proxy, normalized at Earth; not an occurrence fit.
        result.mass_factor = std::pow(std::clamp(mass, .0001, 100.0), .25);
        result.heavy_factor = result.mass_factor * (.5 + .5 * std::sqrt(result.water_factor));
        result.cold = std::isfinite(CosmicShore::calculate_escape_velocity(p)) && cold_volatile_regime(p, stellar_flux(s, p));
        if (result.cold)
        {
            const double density = p.mass * .001 / sphere_volume(p.volumetric_mean_radius);
            // Additive specific volumes for compressed water/ice and rock. This is an
            // interior scenario, not a unique inversion of bulk density.
            result.ice_fraction = std::clamp((1 / density - 1.0 / 3500) / (1.0 / 1300 - 1.0 / 3500), .05, .7);
        }
        return result;
    }

    double nitrogen_vapor_pressure(double temperature)
    {
        if (temperature >= 126.19) return INFINITY;
        // NIST liquid-nitrogen Antoine fit: bar converted to Pa.
        // https://webbook.nist.gov/cgi/cbook.cgi?ID=C7727379&Mask=4
        const auto liquid = [](double t) { return 1e5 * std::pow(10.0, 3.7362 - 264.651 / (t - 6.788)); };
        if (temperature >= 63.14) return liquid(temperature);
        // Fray & Schmitt (2009), reproduced in Marboeuf et al. (2014), Table 3.
        // https://arxiv.org/abs/1407.7271 . Join the measured solid-phase fits continuously.
        const auto beta = [](double t)
        {
            const double x = 1 / t;
            return 1e5 * std::exp(8.514 + x * (-458.4 + x * (-1.987e4 + x * (4.8e5 - x * 4.524e6))));
        };
        const auto alpha = [](double t)
        {
            const double x = 1 / t;
            return 1e5 * std::exp(12.4 + x * (-807.4 + x * (-3926 + x * (6.297e4 + x * (-4.633e5 + x * 1.325e6)))));
        };
        const double join = liquid(63.14) / beta(63.14);
        if (temperature >= 35.61) return join * beta(temperature);
        // Below the fit's 10 K limit, continue monotonically towards vacuum.
        if (temperature < 10) return join * beta(35.61) / alpha(35.61)
            * alpha(10) * std::exp(850 * (0.1 - 1 / temperature));
        return join * beta(35.61) / alpha(35.61) * alpha(temperature);
    }

    double nitrogen_frost_temperature(double flux, const AtmosphericInventory& inventory)
    {
        // Lewis et al. (2020), https://arxiv.org/abs/1912.02333 : bright nitrogen frost,
        // reduced IR emissivity, and an alpha/beta emissivity contrast around 35.61 K.
        const double beta = 255 * std::pow(flux * (1 - inventory.nitrogen_frost_albedo)
            / (0.7 * inventory.nitrogen_frost_emissivity), 0.25);
        const double alpha = beta * std::pow(3.0, 0.25);
        if (beta >= 35.61) return beta;
        if (alpha <= 35.61) return std::max(2.7, alpha);
        return 35.61; // Coexisting solid phases provide a thermal plateau.
    }

    // q holds each constituent's weight per unit area in Pa, not its mole partial pressure.
    // For a well mixed atmosphere, x_i * sum(q) is its surface partial pressure. Solve
    // that expression for q_i to impose saturation without confusing mass and mole fractions.
    void condense(double& q, double other_weight, double other_moles, double molar_mass, double saturation)
    {
        if (std::isinf(saturation)) return;
        const double b = other_weight - saturation;
        const double c = saturation * molar_mass * other_moles;
        const double root = std::hypot(b, 2 * std::sqrt(c));
        const double limit = b > 0 ? 2 * c / (root + b) : (root - b) * 0.5;
        q = std::min(q, std::max(0.0, limit));
    }
}

bool AtmosphericInventory::from_json(const json& value)
{
    if (!value.is_object()) return false;
    json normalized = value;
    normalize_uncertain_numbers(normalized);
    AtmosphericInventory parsed = *this;
    struct Field
    {
        const char* key;
        double AtmosphericInventory::*member;
        double maximum;
    };
    const Field fields[] =
    {
        {"age_gyr", &AtmosphericInventory::age_gyr, INFINITY},
        {"co2_mass_fraction", &AtmosphericInventory::co2_mass_fraction, 1},
        {"nitrogen_mass_fraction", &AtmosphericInventory::nitrogen_mass_fraction, 1},
        {"water_mass_fraction", &AtmosphericInventory::water_mass_fraction, 1},
        {"weathering_efficiency", &AtmosphericInventory::weathering_efficiency, 1},
        {"xuv_scale", &AtmosphericInventory::xuv_scale, INFINITY},
        {"surface_water_share", &AtmosphericInventory::surface_water_share, 1},
        {"basin_relief_m", &AtmosphericInventory::basin_relief_m, INFINITY},
        {"nitrogen_frost_albedo", &AtmosphericInventory::nitrogen_frost_albedo, 1},
        {"nitrogen_frost_emissivity", &AtmosphericInventory::nitrogen_frost_emissivity, 1},
        {"cold_temperature_k", &AtmosphericInventory::cold_temperature_k, INFINITY},
        {"impact_flux_scale", &AtmosphericInventory::impact_flux_scale, 1000},
        {"impactor_water_fraction", &AtmosphericInventory::impactor_water_fraction, 1},
        {"impactor_co2_fraction", &AtmosphericInventory::impactor_co2_fraction, 1},
        {"impactor_nitrogen_fraction", &AtmosphericInventory::impactor_nitrogen_fraction, 1}
    };
    for (const Field& field : fields)
    {
        if (!normalized.contains(field.key)) continue;
        if (!normalized[field.key].is_number()) return false;
        const double number = normalized[field.key].get<double>();
        if (!std::isfinite(number) || number < 0 || number > field.maximum) return false;
        parsed.*(field.member) = number;
    }
    if (parsed.co2_mass_fraction + parsed.nitrogen_mass_fraction + parsed.water_mass_fraction > 1) return false;
    if (parsed.impactor_water_fraction + parsed.impactor_co2_fraction + parsed.impactor_nitrogen_fraction > 1) return false;
    if (parsed.nitrogen_frost_emissivity <= 0) return false;
    *this = parsed;
    return true;
}

json AtmosphericInventory::to_json() const
{
    json result = json::object();
    result["age_gyr"] = age_gyr;
    result["co2_mass_fraction"] = co2_mass_fraction;
    result["nitrogen_mass_fraction"] = nitrogen_mass_fraction;
    result["water_mass_fraction"] = water_mass_fraction;
    result["weathering_efficiency"] = weathering_efficiency;
    result["xuv_scale"] = xuv_scale;
    result["surface_water_share"] = surface_water_share;
    result["basin_relief_m"] = basin_relief_m;
    result["nitrogen_frost_albedo"] = nitrogen_frost_albedo;
    result["nitrogen_frost_emissivity"] = nitrogen_frost_emissivity;
    result["cold_temperature_k"] = cold_temperature_k;
    result["impact_flux_scale"] = impact_flux_scale;
    result["impactor_water_fraction"] = impactor_water_fraction;
    result["impactor_co2_fraction"] = impactor_co2_fraction;
    result["impactor_nitrogen_fraction"] = impactor_nitrogen_fraction;
    return result;
}

double CosmicShore::debris_bombardment_factor(const Star& s)
{
    if (!s.has_disk || !std::isfinite(s.disk_inner_edge_sma) || s.disk_inner_edge_sma <= 0) return 1;
    return std::clamp(std::sqrt(30 * AU / s.disk_inner_edge_sma), 1.0, 4.0);
}

double CosmicShore::calculate_escape_velocity(const Planet& p)
{
    // G uses grams and meters here; the stored radius is already in meters.
    if (!std::isfinite(p.mass) || p.mass <= 0 || !std::isfinite(p.volumetric_mean_radius)
        || p.volumetric_mean_radius <= 0) return NAN;
    return std::sqrt(2 * G * p.mass / p.volumetric_mean_radius);
}

double CosmicShore::calculate_unified_metric(Star& s, Planet& p)
{
    const double velocity = calculate_escape_velocity(p);
    const double flux = stellar_flux(s, p);
    if (!std::isfinite(velocity) || velocity <= 0 || !std::isfinite(flux) || flux <= 0
        || !std::isfinite(s.m_bol) || s.m_bol <= 0) return NAN;
    // Zahnle & Catling (2017), https://arxiv.org/abs/1702.03386 : classical v^4
    // shoreline anchored near Mars (5 km/s, 0.43 Earth flux). Their XUV proxy is
    // L^0.4/a^2 = bolometric_flux * L^-0.6. Positive values favor retention.
    return 4 * std::log10(velocity / 5000) - std::log10(flux / 0.43) + 0.6 * std::log10(s.m_bol);
}

AtmosphericInventory CosmicShore::default_inventory(const Star& s)
{
    AtmosphericInventory inventory;
    inventory.age_gyr = s.estimate_age_gyr();
    return inventory;
}

AtmosphericEstimate CosmicShore::estimate_atmosphere(Star& s, Planet& p)
{
    const auto inventory = default_inventory(s, p);
    auto estimate = estimate_atmosphere(s, p, inventory);
    estimate.diagnostics["default_inventory"] = inventory.to_json();
    estimate.diagnostics["stellar_age"] = s.age_diagnostics();
    estimate.diagnostics["elemental_inventory_prior"] = planet_formation_chemistry(s);
    if (!uses_gaseous_map(p.type))
    {
        const auto delivery = formation_delivery(s, p);
        auto& diagnostic = estimate.diagnostics["formation_inventory"];
        diagnostic["model"] = "chemical_delivery_scenario_v1";
        diagnostic["current_orbit_as_weak_birth_location_proxy"] = true;
        diagnostic["stellar_orbit_au"] = delivery.orbit_au;
        diagnostic["young_disc_snowline_proxy_au"] = delivery.snowline_au;
        diagnostic["outer_disc_prior_weight"] = delivery.outer_fraction;
        diagnostic["mass_delivery_factor"] = delivery.mass_factor;
        diagnostic["water_delivery_factor"] = delivery.water_factor;
        diagnostic["heavy_volatile_delivery_factor"] = delivery.heavy_factor;
        diagnostic["cold_body_ice_fraction_scenario"] = delivery.ice_fraction;
        diagnostic["nitrogen_source"] = delivery.cold ? "ice_ammonia_and_organics" : "reference_N_C_delivery_ratio";
        const auto& chemistry = estimate.diagnostics["elemental_inventory_prior"];
        if (!delivery.cold && chemistry.contains("nitrogen_prior"))
            diagnostic["nitrogen_source"] = chemistry["nitrogen_prior"].value("source", "reference_N_C_delivery_ratio");
        diagnostic["water_inventory_range"] = json::array({inventory.water_mass_fraction * .1,
            std::min(.7, inventory.water_mass_fraction * 10)});
        diagnostic["range_interpretation"] = "Volatile_poor_to_ice_delivery_alternatives;not_statistical_error_bars";
        diagnostic["surface_partition"] = "Interior_storage_scenario;all_surface_alternative_retained";
        diagnostic["all_surface_water_share_alternative"] = 1;
    }
    estimate.diagnostics["age_origin"] = s.fictional_origin.is_object()
        && s.fictional_origin.contains("age_gyr") && s.fictional_origin["age_gyr"].is_number()
        && s.fictional_origin["age_gyr"] == default_inventory(s).age_gyr
        ? "stellar_formation_scenario" : s.age_diagnostics().value("source", "stellar_age_estimate");
    return estimate;
}

AtmosphericInventory CosmicShore::default_inventory(Star& s, Planet& p)
{
    auto inventory = default_inventory(s);
    const json& chemistry = planet_formation_chemistry(s);
    const auto delivery = formation_delivery(s, p);
    if (chemistry.value("applicable", false))
    {
        inventory.water_mass_fraction = std::min(.5, inventory.water_mass_fraction * chemistry.value("water_inventory_scale", 1.0));
        inventory.co2_mass_fraction = std::min(.05, inventory.co2_mass_fraction * chemistry.value("carbon_inventory_scale", 1.0));
    }
    const double nitrogen_scale = chemistry.value("nitrogen_inventory_scale", chemistry.value("carbon_inventory_scale", 1.0));
    inventory.water_mass_fraction = std::min(.5, inventory.water_mass_fraction * delivery.mass_factor * delivery.water_factor);
    inventory.co2_mass_fraction = std::min(.05, inventory.co2_mass_fraction * delivery.heavy_factor);
    inventory.nitrogen_mass_fraction = std::min(.01, inventory.nitrogen_mass_fraction * delivery.heavy_factor * nitrogen_scale);
    if (delivery.cold)
    {
        // N2-equivalent nitrogen released from ammonia/organics in ice-rich interiors.
        // The accessible inventory is a Titan-scale scenario calibration, not bulk ice content.
        inventory.water_mass_fraction = delivery.ice_fraction;
        inventory.nitrogen_mass_fraction = std::min(.01, 1.35e-4 * delivery.ice_fraction / .51 * nitrogen_scale);
    }
    // Pressure-dependent ocean/mantle exchange can store substantial water at depth.
    // This simple partition is a scenario, inspired by Cowan & Abbot (2014),
    // https://arxiv.org/abs/1401.0720, rather than a unique interior inference.
    const double mass = std::isfinite(p.mass) && p.mass > 0 ? p.mass / earth_mass : 1;
    inventory.surface_water_share = delivery.cold ? .01 : 1 / (1 + 2 * std::sqrt(mass));
    // Normalize bulk water at Earth while allowing mass-dependent interior storage
    // to change the surface share on other planets.
    if (!delivery.cold)
        inventory.water_mass_fraction = std::min(.5, inventory.water_mass_fraction * 3);
    return inventory;
}

json CosmicShore::input_state(Star& s, Planet& p, const AtmosphericInventory& inventory)
{
    const auto number = [](double value) { return std::isfinite(value) ? json(value) : json(nullptr); };
    return json::array({"volatile_budget_v4", number(p.mass), number(p.volumetric_mean_radius), static_cast<int>(p.type),
        number(stellar_flux(s, p)), number(s.mass), number(s.metallicity), s.metallicity_ratio,
        s.is_main_sequence(), number(p.albedo), inventory.to_json(), s.has_disk, number(s.disk_inner_edge_sma)});
}

const json& CosmicShore::envelope_scenario(Star& s, Planet& p, const AtmosphericInventory& inventory)
{
    static const json unavailable = {{"applicable", false}};
    const double mass = p.mass / earth_mass;
    const double flux = stellar_flux(s, p);
    if (p.typeclass() != class_planet || !std::isfinite(mass) || mass < .1 || mass >= giant_mass_cutoff / earth_mass
        || !std::isfinite(flux) || flux <= 0 || !std::isfinite(inventory.age_gyr) || inventory.age_gyr < 0
        || !std::isfinite(inventory.xuv_scale) || inventory.xuv_scale < 0) return unavailable;
    const double core_radius = 1.02 * earth_radius * std::pow(mass, .27);
    // Initial fractions describe competing formation scenarios, not measured abundances.
    const double initial = std::clamp(.015 * std::pow(mass / 5, 1.5), 1e-5, .2);
    const auto envelope_radius = [&](double fraction, double age)
    {
        // Lopez & Fortney (2014), Eq. 4, https://arxiv.org/abs/1311.0329.
        return 2.06 * earth_radius * std::pow(mass, -.21) * std::pow(fraction / .05, .59)
            * std::pow(flux, .044) * std::pow(std::max(.1, age) / 5, -.18);
    };
    const double xuv_radius = std::max(2 * core_radius, core_radius + envelope_radius(initial, .1));
    const double stellar_mass = std::isfinite(s.mass) && s.mass > 0 ? s.mass / solar_mass : 1;
    const bool metal_used = std::isfinite(s.metallicity) && (s.metallicity_ratio == "[Fe/H]" || s.metallicity_ratio == "[M/H]")
        && stellar_mass >= .7 && stellar_mass <= 1.3 && s.is_main_sequence();
    const double saturation = std::clamp(.1 * std::pow(stellar_mass, -2.5)
        * (metal_used ? std::pow(10.0, std::clamp(.3 * s.metallicity, -6.0, 6.0)) : 1), .01, 5.0);
    // Energy-limited escape: eta*pi*R_XUV^3*integral(F_XUV dt)/(G*M^2).
    // Saturated L_XUV/L_bol=1e-3 and heating efficiency=0.1 are scenario choices.
    // A fixed expanded early radius is a conservative loss proxy, not a cooling integration.
    const double xuv_energy = 1361 * flux * 1e-3 * activity_integral(inventory.age_gyr, saturation)
        * 1e9 * oneyear * inventory.xuv_scale;
    const double mass_kg = p.mass * .001;
    const double energy_loss = .1 * _pi * std::pow(xuv_radius, 3) * xuv_energy / (G * 1000 * mass_kg * mass_kg);
    const double equilibrium = 255 * std::pow(flux, .25);
    const double binding = G * p.mass * .0023 / (8.314462618 * std::max(2.7, equilibrium) * xuv_radius);
    // Smooth compactness gate motivated by post-disc boil-off (Owen & Wu 2016).
    // https://arxiv.org/abs/1506.02049; the transition is an explicit approximation.
    const double bound = initial * transition((binding - 20) / 3);
    const double retained = std::max(0.0, bound - energy_loss);
    const double relative_error = std::isfinite(p.mass_error_upper) && std::isfinite(p.mass_error_lower)
        ? std::max(fabs(p.mass_error_upper), fabs(p.mass_error_lower)) / p.mass : NAN;
    const auto prior = planet_envelope_prior(s, mass, flux, relative_error);
    const double initial_score = prior.applicable ? prior.probability
        : transition(2 * std::log(mass / 4.37) - .5 * std::log1p(std::max(0.0, flux - 1) / 100));
    const double score = initial_score * retained / (retained + .001);
    const bool measured = p.radius_evidence == PlanetMeasurement::observed;
    const bool selected = score >= .5;
    const double present_radius = core_radius + envelope_radius(retained, inventory.age_gyr);
    const double base_pressure = G * p.mass * (p.mass * .001) * retained / (4 * _pi * std::pow(core_radius, 4));
    const bool apply = !p.lock_type && !measured && p.mass_evidence != PlanetMeasurement::unknown;
    const std::array<double, 9> inputs = {mass, flux, inventory.age_gyr, inventory.xuv_scale, stellar_mass,
        metal_used ? s.metallicity : 0, initial_score, measured ? 1.0 : 0.0, apply ? 1.0 : 0.0};
    static thread_local std::array<double, 9> previous{};
    static thread_local json cached;
    if (!cached.is_null() && previous == inputs) return cached;
    previous = inputs;
    cached = {{"applicable", true}, {"model", "primordial_envelope_v1"},
        {"initial_mass_fraction", initial}, {"retained_mass_fraction", retained}, {"escaped_mass_fraction", initial - retained},
        {"energy_limited_loss_fraction", energy_loss}, {"xuv_radius_assumed_m", xuv_radius},
        {"hydrogen_binding_parameter", binding}, {"initial_envelope_score", initial_score}, {"retained_envelope_score", score},
        {"core_radius_m", core_radius}, {"envelope_radius_m", present_radius}, {"base_pressure_pa", base_pressure},
        {"outcome", selected ? "mini_neptune" : "secondary_atmosphere"},
        {"selection_applied", apply},
        {"observed_radius_overrides_scenario", measured},
        {"alternatives", json::array({"compact_rock_and_secondary_volatiles", "rock_with_primordial_H_He"})},
        {"interpretation", "Relative_scenario_score_not_a_calibrated_probability"},
        {"limitations", "Unknown_disc_lifetime_and_initial_entropy;fixed_early_XUV_radius;no_core_powered_loss_or_tidal_escape_correction;radius_fit_extrapolated_outside_1_to_20_Earth_masses"}};
    return cached;
}

AtmosphericEstimate CosmicShore::estimate_atmosphere(Star& s, Planet& p, const AtmosphericInventory& inventory)
{
    AtmosphericEstimate result;
    result.diagnostics =
    {
        {"model", "volatile_budget_v4"}, {"inventory", inventory.to_json()},
        {"status", "invalid_input"},
        {"interpretation", "approximate_scenario_not_a_measured_pressure_or_calibrated_confidence_interval"}
    };
    AtmosphericInventory checked;
    if (!checked.from_json(result.diagnostics["inventory"])) return result;
    result.diagnostics["input_state"] = input_state(s, p, inventory);
    result.diagnostics["primordial_envelope"] = envelope_scenario(s, p, inventory);
    if (uses_gaseous_map(p.type))
    {
        result.valid = true;
        result.pressure = 1e5;
        result.diagnostics["model"] = "cloud_reference_v1";
        result.diagnostics["status"] = "conventional_1_bar_level";
        result.diagnostics["pressure_pa"] = result.pressure;
        return result;
    }
    const double velocity = calculate_escape_velocity(p);
    if (!std::isfinite(velocity) || velocity <= 0) return result;
    const double flux = stellar_flux(s, p);
    if (!std::isfinite(flux) || flux <= 0) return result;
    const bool cold = cold_volatile_regime(p, flux);
    const double planet_mass = p.mass / earth_mass;
    const double radius = p.volumetric_mean_radius / earth_radius;
    if (!std::isfinite(planet_mass) || !std::isfinite(radius) || planet_mass <= 0 || radius <= 0) return result;

    const bool stellar_mass_known = std::isfinite(s.mass) && s.mass > 0;
    const double stellar_mass = stellar_mass_known ? s.mass / solar_mass : 1;
    // Metallicity enters the activity history only, never as a pressure/inventory multiplier.
    // The 0.3 dex sensitivity is an explicit scenario assumption, not a fitted law.
    // See et al. (2025), https://arxiv.org/abs/2509.21276 motivates the direction for
    // solar-like stars; extrapolation to other stellar regimes is deliberately avoided.
    const bool metallicity_used = std::isfinite(s.metallicity)
        && (s.metallicity_ratio == "[Fe/H]" || s.metallicity_ratio == "[M/H]")
        && stellar_mass >= 0.7 && stellar_mass <= 1.3 && s.is_main_sequence();
    const double metal_activity = metallicity_used ? std::pow(10.0, std::clamp(0.3 * s.metallicity, -6.0, 6.0)) : 1;
    const double saturation_time = std::clamp(0.1 * std::pow(stellar_mass, -2.5) * metal_activity, 0.01, 5.0);
    const double dose = activity_integral(inventory.age_gyr, saturation_time)
        / activity_integral(reference_age, 0.1) * inventory.xuv_scale;
    const double mean_activity = inventory.age_gyr > 0 ? dose * reference_age / inventory.age_gyr : 0;

    // Effective heavy-gas escape and early degassing, calibrated only to the approximate
    // Venus/Earth/Mars pressure scales. These are not energy-limited hydrogen loss rates.
    // Interior/outgassing motivation: Hill et al., https://arxiv.org/abs/2605.00170 .
    double loss_rate = std::min(1e12, 0.035 * flux * mean_activity * std::pow(11200 / velocity, 6));
    double escape_temperature = 0, binding_parameter = 0;
    if (cold)
    {
        // Jeans-shaped molecular binding factor, with an effective historical heating proxy.
        // 10000 K and 1000/Gyr are scenario calibration constants, not present exobase measurements.
        // Titan's escape/outgassing history: https://arxiv.org/abs/2010.12342 .
        escape_temperature = std::max(2.7, 1e4 * std::sqrt(flux * mean_activity));
        binding_parameter = 0.028 * velocity * velocity / (2 * 8.314462618 * escape_temperature);
        loss_rate = mean_activity > 0 && std::isfinite(binding_parameter)
            ? std::exp(std::min(std::log(1e12), std::log(1000 * mean_activity)
                + 0.5 * std::log(escape_temperature / 1000) + std::log1p(binding_parameter) - binding_parameter)) : 0;
    }
    const double release_time = cold ? 0.1 : std::clamp(0.15 * std::sqrt(planet_mass), 0.01, 2.0);
    const double heavy_retention = released_and_retained(inventory.age_gyr, release_time, loss_rate);
    const double steam = transition((flux - 1.15) / 0.025);
    const double water_retention = released_and_retained(inventory.age_gyr, release_time,
        cold ? 0 : loss_rate * (1 + 500 * steam));
    const double original_water = inventory.water_mass_fraction * water_retention;
    const double deep_water = original_water * (1 - inventory.surface_water_share);
    std::array<double, 3> pools = {inventory.co2_mass_fraction * heavy_retention,
        inventory.nitrogen_mass_fraction * heavy_retention, original_water * inventory.surface_water_share};
    const double earth_column = G * earth_mass * (earth_mass * 0.001)
        / (4 * _pi * std::pow(earth_radius, 4));
    const double column = earth_column * std::pow(planet_mass, 2) / std::pow(radius, 4);
    if (!std::isfinite(column) || column <= 0 || !std::isfinite(loss_rate)) return result;
    struct Phase
    {
        std::array<double, 3> gas{}, available{};
        double climate = 0, carbon_air = 1, nitrogen_saturation = 0;
        bool nitrogen_frost = false;
    };
    const auto partition = [&](const std::array<double, 3>& retained)
    {
        Phase phase;
        const double temperate = transition((flux - 0.65) / 0.035) * (1 - steam);
        const double wet = temperate * retained[2] / (retained[2] + 1e-6);
        phase.carbon_air = std::exp(-3 * inventory.age_gyr * wet
            * inventory.weathering_efficiency * std::min(1.0, std::sqrt(planet_mass)));
        phase.climate = std::max(2.7, 255 * std::pow(flux, 0.25) + 33 * temperate + 450 * steam);
        double co2 = column * retained[0] * phase.carbon_air;
        double nitrogen = column * retained[1];
        double water = column * retained[2];
        phase.available = {co2, nitrogen, water};
        if (cold)
        {
            const double gravity = G * p.mass / std::pow(p.volumetric_mean_radius, 2);
            const double raw_tau = std::pow(std::min(nitrogen, pressure_ceiling) / 1.5e5, 2) * 1.35 / gravity;
            const double tau = 10 * raw_tau / (10 + raw_tau);
            phase.climate *= std::pow(1 + 0.75 * tau, 0.25);
            if (nitrogen > nitrogen_vapor_pressure(phase.climate)) phase.climate = nitrogen_frost_temperature(flux, inventory);
            if (inventory.cold_temperature_k > 0) phase.climate = inventory.cold_temperature_k;
            phase.nitrogen_frost = nitrogen > nitrogen_vapor_pressure(phase.climate);
        }
        const double water_saturation = (0.5 * temperate + steam) * vapor_pressure(phase.climate, 273.16, 611.657,
            phase.climate < 273.16 ? 51060 : 45050, 647.10);
        const double co2_saturation = vapor_pressure(phase.climate, 216.59, 518500,
            phase.climate < 216.59 ? 25000 : 16000, 304.13);
        phase.nitrogen_saturation = nitrogen_vapor_pressure(phase.climate);
        for (int iteration = 0; iteration < 24; ++iteration)
        {
            const double previous = co2 + nitrogen + water;
            condense(water, co2 + nitrogen, co2 / 0.044 + nitrogen / 0.028, 0.018, water_saturation);
            condense(co2, nitrogen + water, nitrogen / 0.028 + water / 0.018, 0.044, co2_saturation);
            condense(nitrogen, co2 + water, co2 / 0.044 + water / 0.018, 0.028, phase.nitrogen_saturation);
            if (previous - co2 - nitrogen - water <= 1e-10 * std::max(1.0, previous)) break;
        }
        phase.gas = {co2, nitrogen, water};
        return phase;
    };
    Phase phase = partition(pools);
    const double belt = debris_bombardment_factor(s);
    const double incoming = std::min(.01, 1e-5 * (belt - 1) * inventory.impact_flux_scale * (inventory.age_gyr / reference_age));
    std::array<double, 3> arrived{}, impact_lost{}, delivery_lost{}, delivery_retained{};
    auto& impacts = result.diagnostics["impacts"];
    impacts = {{"model", "excess_late_bombardment_v1"}, {"belt_factor", belt},
        {"excess_factor", belt - 1}, {"active", incoming > 0},
        {"impactor_mass_fraction", incoming}, {"reference_mass_fraction_per_4_5_gyr", 1e-5},
        {"mass_fraction_capped", incoming == .01},
        {"interpretation", "Excess_above_Solar_calibration;belt_proximity_is_not_a_measured_impact_flux"},
        {"assumptions", "Constant_excess_flux;comet_like_speeds;fixed_planet_mass;mid_history_phase_exposure;no_individual_giant_impacts"},
        {"source", "https://arxiv.org/abs/1910.10731"},
        {"calibration", "Heuristic_coefficients_not_the_published_impact_shoreline_fit"}};
    if (incoming > 0)
    {
        const auto formation = formation_delivery(s, p);
        // A comet-like encounter speed. q is approach speed / escape speed,
        // distinct from the gravitationally accelerated surface impact speed.
        const double q2 = std::min(1e6, G * stellar_mass * solar_mass
            / (formation.orbit_au * AU * velocity * velocity));
        const double capture = 1 / (1 + q2);
        const double erosion_efficiency = std::min(1.0, .03 * q2);
        const double capacity = incoming * erosion_efficiency;
        const std::array<double, 3> composition = {inventory.impactor_co2_fraction,
            inventory.impactor_nitrogen_fraction, inventory.impactor_water_fraction};
        auto middle = pools;
        for (int i = 0; i < 3; ++i)
        {
            arrived[i] = incoming * composition[i];
            middle[i] += .5 * arrived[i] * capture;
        }
        const Phase reference = partition(middle);
        std::array<double, 3> gas_mass{}, mobile_mass{};
        double gas_total = 0, mobile_total = 0;
        for (int i = 0; i < 3; ++i)
        {
            gas_mass[i] = reference.gas[i] / column;
            mobile_mass[i] = std::max(0.0, reference.available[i] / column - gas_mass[i]);
            gas_total += gas_mass[i];
            mobile_total += mobile_mass[i];
        }
        // Allocate a finite erosion mass capacity: 90% gas, 10% mobile condensate.
        // Carbonates and deep water are outside these exposed pools. No pressure
        // suppression from crater rendering enters the physical impact exposure.
        const double gas_dose = .9 * capacity / std::max(1e-30, gas_total);
        const double surface_dose = .1 * capacity / std::max(1e-30, mobile_total);
        const auto survival = [](double dose)
        {
            return dose > 1e-8 ? -std::expm1(-dose) / dose : 1 - .5 * dose;
        };
        const char* species[] = {"CO2", "N2", "H2O"};
        for (int i = 0; i < 3; ++i)
        {
            const double gas_share = middle[i] > 0 ? std::clamp(gas_mass[i] / middle[i], 0.0, 1.0) : 0;
            const double surface_share = middle[i] > 0
                ? std::clamp(mobile_mass[i] / middle[i], 0.0, 1 - gas_share) : 0;
            const double protected_share = std::max(0.0, 1 - gas_share - surface_share);
            impact_lost[i] = pools[i] * (gas_share * -std::expm1(-gas_dose)
                + surface_share * -std::expm1(-surface_dose));
            const double escape_dose = inventory.age_gyr * (i == 2
                ? (cold ? 0 : loss_rate * (1 + 500 * steam)) : loss_rate);
            // Continuous late delivery has a distribution of residence times;
            // do not expose every delivered molecule to a full system age.
            delivery_retained[i] = arrived[i] * capture * (protected_share
                + gas_share * survival(gas_dose + escape_dose)
                + surface_share * survival(surface_dose + (i == 2 ? escape_dose : 0)));
            delivery_lost[i] = arrived[i] - delivery_retained[i];
            pools[i] = std::max(0.0, pools[i] - impact_lost[i]) + delivery_retained[i];
            impacts["species"][species[i]] = {{"arrived_mass_fraction", arrived[i]},
                {"prompt_escape_mass_fraction", arrived[i] * (1 - capture)},
                {"later_escape_mass_fraction", std::max(0.0, delivery_lost[i] - arrived[i] * (1 - capture))},
                {"retained_delivery_mass_fraction", delivery_retained[i]},
                {"original_impact_escape_mass_fraction", impact_lost[i]},
                {"gas_exposure_share", gas_share}, {"mobile_condensate_share", surface_share},
                {"protected_share", protected_share}};
        }
        impacts["approach_over_escape_speed"] = std::sqrt(q2);
        impacts["impact_over_escape_speed"] = std::sqrt(1 + q2);
        impacts["impactor_capture_fraction"] = capture;
        impacts["erosion_mass_per_impactor_mass"] = erosion_efficiency;
        impacts["erosion_capacity_mass_fraction"] = capacity;
        impacts["atmospheric_turnover_dose"] = gas_dose;
        impacts["condensate_turnover_dose"] = surface_dose;
        phase = partition(pools);
    }
    const double co2 = phase.gas[0], nitrogen = phase.gas[1], water = phase.gas[2];
    const double carbon_air = phase.carbon_air, climate_k = phase.climate;
    const double nitrogen_saturation = phase.nitrogen_saturation;
    const bool nitrogen_frost = phase.nitrogen_frost;
    const auto& available_columns = phase.available;
    const double accessible_water = pools[2];
    const double pressure = co2 + nitrogen + water;
    if (!std::isfinite(pressure)) return result;
    result.valid = true;
    result.pressure = std::min(pressure_ceiling, pressure);
    result.co2_moles = co2 / 0.044;
    result.nitrogen_moles = nitrogen / 0.028;
    result.water_moles = water / 0.018;
    auto& diagnostic = result.diagnostics;
    diagnostic["status"] = "estimated";
    diagnostic["pressure_pa"] = result.pressure;
    diagnostic["uncapped_pressure_pa"] = pressure;
    diagnostic["pressure_capped"] = pressure > pressure_ceiling;
    diagnostic["flux_earth"] = flux;
    diagnostic["climate_proxy_k"] = climate_k;
    diagnostic["phase_available_column_pa"] = available_columns;
    diagnostic["climate_model"] = "Independent_phase_proxy_not_a_self_consistent_radiative_climate";
    diagnostic["age_origin"] = "scenario_inventory";
    diagnostic["stellar_mass_assumed"] = !stellar_mass_known;
    diagnostic["metallicity_used"] = metallicity_used;
    diagnostic["metallicity_activity_sensitivity_assumed"] = .3;
    diagnostic["xuv_dose_solar"] = dose;
    diagnostic["heavy_release_and_retention"] = heavy_retention;
    diagnostic["water_release_and_retention"] = water_retention;
    diagnostic["release_and_retention_stage"] = "Before_excess_late_bombardment";
    diagnostic["carbon_atmospheric_share"] = carbon_air;
    diagnostic["regime"] = cold ? "cold_volatiles" : "terrestrial";
    diagnostic["column_weight_pa"]["CO2"] = co2;
    diagnostic["column_weight_pa"]["N2"] = nitrogen;
    diagnostic["column_weight_pa"]["H2O"] = water;
    diagnostic["shoreline_metric"] = calculate_unified_metric(s, p);
    diagnostic["limitations"] = "Solar_System_scale_calibration;unknown_formation_and_rotation_history;secondary_inventory_and_primordial_H_He_are_alternative_scenarios;climate_proxy";
    if (cold)
    {
        result.diagnostics["cold_volatiles"] =
        {
            {"escape_temperature_proxy_k", escape_temperature}, {"nitrogen_binding_parameter", binding_parameter},
            {"effective_loss_rate_gyr", loss_rate}, {"release_time_gyr", release_time},
            {"nitrogen_frost", nitrogen_frost},
            {"nitrogen_saturation_pa", std::isfinite(nitrogen_saturation) ? json(nitrogen_saturation) : json(nullptr)},
            {"temperature_source", inventory.cold_temperature_k > 0 ? "scenario" : "energy_balance_proxy"},
            {"assumptions", "Titan_scale_nitrogen_inventory_and_effective_escape;N2_frost_energy_balance;no_explicit_methane_cycle_or_magnetospheric_sputtering"}
        };
    }
    // Conservation diagnostics keep the non-atmospheric reservoirs visible. The inferred
    // pressure ceiling is a rendering bound and does not destroy material in this ledger.
    const double mantle_share = std::exp(-inventory.age_gyr / release_time);
    result.diagnostics["reservoir_mass_fractions"] = json::object();
    const auto ledger = [&](int index, const char* species, double initial, double retained, double air_weight)
    {
        const double atmospheric = air_weight / column;
        result.diagnostics["reservoir_mass_fractions"][species] =
        {
            {"initial", initial}, {"delivered", arrived[index]},
            {"mantle", initial * mantle_share}, {"atmosphere", atmospheric},
            {"surface_storage", std::max(0.0, pools[index] + (index == 2 ? deep_water : 0) - atmospheric)},
            {"escaped", std::max(0.0, initial * (1 - mantle_share - retained))
                + impact_lost[index] + delivery_lost[index]}
        };
    };
    ledger(0, "CO2", inventory.co2_mass_fraction, heavy_retention, co2);
    ledger(1, "N2", inventory.nitrogen_mass_fraction, heavy_retention, nitrogen);
    ledger(2, "H2O", inventory.water_mass_fraction, water_retention, water);
    diagnostic["water_partition"] =
    {
        {"interior_recycled_mass_fraction", deep_water},
        {"surface_condensed_mass_fraction", std::max(0.0, accessible_water - water / column)},
        {"all_surface_condensed_upper_bound", diagnostic["reservoir_mass_fractions"]["H2O"]["surface_storage"]},
        {"all_surface_alternative", "Set_surface_water_share_to_1_and_recalculate_including_vapor_partition"},
        {"interpretation", "Subdivisions_of_retained_non_atmospheric_H2O;not_extra_reservoirs"}
    };
    diagnostic["structure_scenarios"] = json::array(
    {
        {{"scenario", "volatile_poor_rock"}, {"pressure_pa", 0}, {"condition", "Inefficient_delivery_or_late_removal"}},
        {{"scenario", "rock_with_secondary_atmosphere"}, {"pressure_pa", result.pressure},
            {"condition", "Adopted_inventory_escape_and_weathering"}},
        {{"scenario", "water_rich_interior_and_surface"},
            {"water_mass_fraction_range", json::array({inventory.water_mass_fraction,
                std::max(inventory.water_mass_fraction, .5)})},
            {"condition", "Ice_delivery_or_migration;interior_and_surface_partition_degenerate"}},
        {{"scenario", "primordial_H_He_envelope"}, {"condition", "Envelope_evolution_alternative"},
            {"diagnostic", "primordial_envelope"}}
    });
    diagnostic["structure_interpretation"] = "Conditional_alternatives;no_calibrated_composition_probabilities";
    if (metallicity_used) result.diagnostics["metallicity_dex"] = s.metallicity;
    return result;
}

json CosmicShore::seasonal_volatiles(const json& budget, const ThermalOrbit& orbit,
    double mean_k, double response_seconds, double flux_reference)
{
    if (!budget.is_object() || !budget.contains("phase_available_column_pa")
        || !budget["phase_available_column_pa"].is_array() || budget["phase_available_column_pa"].size() != 3
        || !std::isfinite(mean_k) || mean_k <= 0 || !std::isfinite(response_seconds)
        || response_seconds < 0 || !std::isfinite(orbit.eccentricity) || orbit.eccentricity < 0
        || orbit.eccentricity >= 1 || !std::isfinite(orbit.period) || orbit.period <= 0
        || !std::isfinite(orbit.mean_anomaly) || !std::isfinite(flux_reference) || flux_reference <= 0) return nullptr;
    std::array<double, 3> available{};
    for (size_t i = 0; i < available.size(); ++i)
    {
        if (!budget["phase_available_column_pa"][i].is_number()) return nullptr;
        available[i] = budget["phase_available_column_pa"][i].get<double>();
        if (!std::isfinite(available[i]) || available[i] < 0) return nullptr;
    }
    // Cache the periodic trajectory, not a time-step state: jumping simulation time or
    // opening another view cannot erase thermal/condensation history. No catalog-load work.
    constexpr size_t count = 256;
    struct Phase { double temperature = 0; std::array<double, 3> gas{}; };
    static thread_local std::array<Phase, count> curve{};
    static thread_local std::array<double, 9> previous{};
    static thread_local bool cached = false;
    static thread_local json summary;
    const std::array<double, 9> key = {available[0], available[1], available[2], orbit.eccentricity,
        orbit.period, mean_k, response_seconds, flux_reference, 1};
    if (!cached || previous != key)
    {
        const double step = orbit.period / count;
        const double tau = std::max(1e-6, response_seconds);
        const double gain = -std::expm1(-step / tau);
        std::array<double, count> heating{};
        double final_flux = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const double eccentric = orbit.eccentric_anomaly(2 * _pi * (i + .5) / count);
            heating[i] = flux_reference / std::pow(1 - orbit.eccentricity * std::cos(eccentric), 2);
            final_flux += gain * (heating[i] - final_flux);
        }
        double thermal_flux = final_flux / -std::expm1(-orbit.period / tau);
        std::array<std::array<double, 3>, count> equilibrium{};
        for (size_t i = 0; i < count; ++i)
        {
            const double middle_flux = thermal_flux + (heating[i] - thermal_flux) * -std::expm1(-.5 * step / tau);
            thermal_flux += gain * (heating[i] - thermal_flux);
            const double temperature = curve[i].temperature = std::max(2.7, mean_k * std::pow(std::max(0.0, middle_flux), .25));
            auto& q = equilibrium[i];
            q = available;
            const double water_saturation = vapor_pressure(temperature, 273.16, 611.657,
                temperature < 273.16 ? 51060 : 45050, 647.10);
            const double co2_saturation = vapor_pressure(temperature, 216.59, 518500,
                temperature < 216.59 ? 25000 : 16000, 304.13);
            const double nitrogen_saturation = nitrogen_vapor_pressure(temperature);
            for (int iteration = 0; iteration < 24; ++iteration)
            {
                const double old = q[0] + q[1] + q[2];
                condense(q[2], q[0] + q[1], q[0] / .044 + q[1] / .028, .018, water_saturation);
                condense(q[0], q[1] + q[2], q[1] / .028 + q[2] / .018, .044, co2_saturation);
                condense(q[1], q[0] + q[2], q[0] / .044 + q[2] / .018, .028, nitrogen_saturation);
                if (old - q[0] - q[1] - q[2] <= 1e-10 * std::max(1.0, old)) break;
            }
        }
        // Finite exchange gives distinct warming/cooling states at the same temperature.
        // Three-day condensation and thirty-day evaporation are explicit mixing scenarios,
        // not measured timescales or a substitute for cloud microphysics/latent-heat feedback.
        const double condensation_gain = -std::expm1(-step / (3 * 86400));
        const double evaporation_gain = -std::expm1(-step / (30 * 86400));
        double closure = 0;
        for (size_t species = 0; species < 3; ++species)
        {
            // The cycle map is monotone and contractive. Bracketing its fixed point
            // also works for short orbits whose exchange time spans many years.
            double low = 0, high = available[species];
            for (int iteration = 0; iteration < 48; ++iteration)
            {
                double gas = (low + high) * .5, change = 0;
                for (size_t i = 0; i < count; ++i)
                {
                    const double target = equilibrium[i][species];
                    const double fraction = target < gas ? condensation_gain : evaporation_gain;
                    const double delta = fraction * (target - gas);
                    gas += delta;
                    change += delta;
                }
                if (change > 0) low = (low + high) * .5;
                else high = (low + high) * .5;
            }
            double gas = (low + high) * .5;
            const double start = gas;
            for (size_t i = 0; i < count; ++i)
            {
                const double target = equilibrium[i][species];
                const double tau = target < gas ? 3 * 86400.0 : 30 * 86400.0;
                curve[i].gas[species] = gas + (target - gas) * -std::expm1(-.5 * step / tau);
                gas += (target - gas) * -std::expm1(-step / tau);
            }
            closure = std::max(closure, std::abs(gas - start) / std::max(1.0, available[species]));
        }
        double minimum = INFINITY, maximum = 0, frozen = 0, liquid = 0, steam = 0;
        for (const auto& phase : curve)
        {
            minimum = std::min(minimum, phase.temperature);
            maximum = std::max(maximum, phase.temperature);
            if (phase.temperature < 273.16) frozen += 1.0 / count;
            else if (phase.temperature < 647.10 && phase.gas[2] < available[2] * .999) liquid += 1.0 / count;
            if (available[2] > 0 && phase.gas[2] >= available[2] * .999) steam += 1.0 / count;
        }
        summary =
        {
            {"model", "periodic_thermal_and_condensation_scenario_v1"},
            {"minimum_temperature_k", minimum}, {"maximum_temperature_k", maximum},
            {"frozen_temperature_time_fraction", frozen}, {"condensed_liquid_time_fraction", liquid},
            {"all_accessible_water_vapor_time_fraction", steam}, {"periodic_closure_relative", closure},
            {"phase_samples", count}, {"condensation_seconds", 3 * 86400}, {"evaporation_seconds", 30 * 86400},
            {"interpretation", "Global_seasonal_scenario_not_local_weather_or_a_measured_liquid_ocean"},
            {"limitations", "Fixed_radiative_feedback_and_heat_capacity;no_latent_heat_or_ice_albedo_feedback;mean_reservoir_and_render_temperatures_remain_separate"}
        };
        previous = key;
        cached = true;
    }
    double phase = std::fmod(orbit.mean_anomaly / (2 * _pi), 1.0);
    if (phase < 0) phase += 1;
    const double index = phase * count - .5;
    const int low = static_cast<int>(std::floor(index));
    const double mix = index - low;
    const auto& a = curve[(low + static_cast<int>(count)) % count];
    const auto& b = curve[(low + 1 + static_cast<int>(count)) % count];
    json result = summary;
    result["current_temperature_k"] = a.temperature + mix * (b.temperature - a.temperature);
    double pressure = 0;
    const char* names[] = {"CO2", "N2", "H2O"};
    for (size_t species = 0; species < 3; ++species)
    {
        const double gas = a.gas[species] + mix * (b.gas[species] - a.gas[species]);
        result["gas_column_pa"][names[species]] = gas;
        result["condensed_column_pa"][names[species]] = std::max(0.0, available[species] - gas);
        pressure += gas;
    }
    result["uncapped_pressure_pa"] = pressure;
    result["pressure_pa"] = std::min(pressure_ceiling, pressure);
    return result;
}

SurfaceWaterEstimate CosmicShore::estimate_surface_water(const Planet& p, const json& budget,
    const std::vector<double>& heights)
{
    SurfaceWaterEstimate result;
    if (!uses_rocky_map(p.type) || p.type == waterworld || p.type == hycean
        || !std::isfinite(calculate_escape_velocity(p)) || !budget.is_object()
        || !budget.contains("inventory") || !budget.contains("reservoir_mass_fractions")) return result;
    AtmosphericInventory inventory;
    if (!inventory.from_json(budget["inventory"])) return result;
    const auto& reservoirs = budget["reservoir_mass_fractions"];
    if (!reservoirs.is_object() || !reservoirs.contains("H2O") || !reservoirs["H2O"].is_object()
        || !reservoirs["H2O"].contains("surface_storage") || !reservoirs["H2O"]["surface_storage"].is_number()) return result;
    const double condensed = reservoirs["H2O"]["surface_storage"].get<double>();
    const double supplied_water = inventory.water_mass_fraction + reservoirs["H2O"].value("delivered", 0.0);
    if (!std::isfinite(supplied_water) || supplied_water < inventory.water_mass_fraction
        || !std::isfinite(condensed) || condensed < 0 || condensed > supplied_water) return result;
    double surface_fraction = condensed * inventory.surface_water_share;
    if (budget.contains("water_partition") && budget["water_partition"].is_object())
        surface_fraction = budget["water_partition"].value("surface_condensed_mass_fraction", surface_fraction);
    if (!std::isfinite(surface_fraction) || surface_fraction < 0 || surface_fraction > condensed) return result;
    const double depth = surface_fraction * (p.mass * 0.001)
        / (4 * _pi * p.volumetric_mean_radius * p.volumetric_mean_radius * 1000);
    const double gravity = G * p.mass / (p.volumetric_mean_radius * p.volumetric_mean_radius);
    // Yield-limited relief scales approximately as 1/g. Actual tectonics and interior
    // water cycling are unknown: https://arxiv.org/abs/1401.0720 and
    // https://doi.org/10.1007/s11214-025-01264-5 . Rendering relief is deliberately
    // separate from physical basin relief; otherwise its shallow bump maps flood too easily.
    const double relief = std::min(0.05 * p.volumetric_mean_radius, inventory.basin_relief_m * 9.80665 / gravity);
    if (!std::isfinite(depth) || depth < 0 || !std::isfinite(relief) || relief < 0) return result;
    double mean = 0, deviation = 0;
    for (double height : heights)
    {
        if (!std::isfinite(height) || height < 0 || height > 1) return result;
        mean += height / heights.size();
    }
    for (double height : heights) deviation += std::pow(height - mean, 2) / heights.size();
    deviation = std::sqrt(deviation);
    double coverage = 0, level = 0, represented_depth = 0;
    if (depth > 0)
    {
        if (relief == 0 || (!heights.empty() && deviation < 1e-12))
        {
            coverage = 1;
            level = 1;
            represented_depth = depth;
        }
        else if (!heights.empty())
        {
            const double target = depth * deviation / relief;
            const auto range = std::minmax_element(heights.begin(), heights.end());
            double low = *range.first, high = *range.second + target;
            for (int iteration = 0; iteration < 64; ++iteration)
            {
                const double middle = low + (high - low) * 0.5;
                double volume = 0;
                for (double height : heights) volume += std::max(0.0, middle - height) / heights.size();
                if (volume < target) low = middle;
                else high = middle;
            }
            const double raw_level = low + (high - low) * 0.5;
            level = std::clamp(raw_level, 0.0, 1.0);
            for (double height : heights)
            {
                coverage += raw_level > height ? 1.0 / heights.size() : 0;
                represented_depth += std::max(0.0, raw_level - height) * relief / deviation / heights.size();
            }
        }
        else
        {
            // Gaussian hypsometry before terrain exists. E[max(z-X,0)] = z*Phi(z)+phi(z).
            double low = -12, high = std::max(12.0, depth / relief + 1);
            for (int iteration = 0; iteration < 80; ++iteration)
            {
                const double z = low + (high - low) * 0.5;
                const double cdf = 0.5 * std::erfc(-z / std::sqrt(2.0));
                const double density = std::exp(-0.5 * z * z) / std::sqrt(2 * _pi);
                if (relief * (z * cdf + density) < depth) low = z;
                else high = z;
            }
            const double z = low + (high - low) * 0.5;
            coverage = 0.5 * std::erfc(-z / std::sqrt(2.0));
            // Initial procedural height scale only; sampled terrain replaces this waterline.
            level = std::clamp(0.5 + 0.08 * z, 1e-6, 1.0);
            represented_depth = depth;
        }
    }
    if (!std::isfinite(level) || !std::isfinite(coverage) || !std::isfinite(represented_depth)) return result;
    result.valid = true;
    result.level = static_cast<float>(level);
    const json basis =
    {
        {"mass_g", p.mass}, {"radius_m", p.volumetric_mean_radius},
        {"condensed_mass_fraction", condensed}, {"surface_water_share", inventory.surface_water_share},
        {"basin_relief_m", inventory.basin_relief_m}
    };
    result.diagnostics =
    {
        {"model", "surface_water_v1"}, {"inventory", inventory.to_json()},
        {"condensed_mass_fraction", condensed}, {"surface_mass_fraction", surface_fraction},
        {"interior_storage_mass_fraction", condensed - surface_fraction},
        {"equivalent_depth_m", depth}, {"represented_depth_m", represented_depth},
        {"basin_relief_m", relief}, {"coverage_fraction", coverage}, {"level", result.level},
        {"terrain", heights.empty() ? "gaussian_prior" : "equal_area_samples"},
        {"terrain_samples", heights.size()}, {"coverage_includes_ice", true},
        {"water_density_kg_m3", 1000},
        {"basis", basis},
        {"interpretation", "scenario_estimate_with_assumed_basin_relief_and_surface_partition;not_a_liquid_ocean_detection"}
    };
    return result;
}
