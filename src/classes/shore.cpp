#include "shore.h"
#include <algorithm>
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
        {"xuv_scale", &AtmosphericInventory::xuv_scale, INFINITY}
    };
    for (const Field& field : fields)
    {
        if (!value.contains(field.key)) continue;
        if (!value[field.key].is_number()) return false;
        const double number = value[field.key].get<double>();
        if (!std::isfinite(number) || number < 0 || number > field.maximum) return false;
        parsed.*(field.member) = number;
    }
    if (parsed.co2_mass_fraction + parsed.nitrogen_mass_fraction + parsed.water_mass_fraction > 1) return false;
    *this = parsed;
    return true;
}

json AtmosphericInventory::to_json() const
{
    return
    {
        {"age_gyr", age_gyr}, {"co2_mass_fraction", co2_mass_fraction},
        {"nitrogen_mass_fraction", nitrogen_mass_fraction}, {"water_mass_fraction", water_mass_fraction},
        {"weathering_efficiency", weathering_efficiency}, {"xuv_scale", xuv_scale}
    };
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
    if (s.fictional_origin.is_object() && s.fictional_origin.contains("age_gyr")
        && s.fictional_origin["age_gyr"].is_number())
    {
        const double age = s.fictional_origin["age_gyr"].get<double>();
        if (std::isfinite(age) && age >= 0) inventory.age_gyr = age;
    }
    return inventory;
}

AtmosphericEstimate CosmicShore::estimate_atmosphere(Star& s, Planet& p)
{
    auto estimate = estimate_atmosphere(s, p, default_inventory(s));
    estimate.diagnostics["age_origin"] = s.fictional_origin.is_object()
        && s.fictional_origin.contains("age_gyr") && s.fictional_origin["age_gyr"].is_number()
        && s.fictional_origin["age_gyr"] == default_inventory(s).age_gyr
        ? "stellar_formation_scenario" : "assumed_4.5_gyr";
    return estimate;
}

AtmosphericEstimate CosmicShore::estimate_atmosphere(Star& s, Planet& p, const AtmosphericInventory& inventory)
{
    AtmosphericEstimate result;
    result.diagnostics =
    {
        {"model", "volatile_budget_v1"}, {"inventory", inventory.to_json()},
        {"status", "invalid_input"},
        {"interpretation", "approximate_scenario_not_a_measured_pressure_or_calibrated_confidence_interval"}
    };
    AtmosphericInventory checked;
    if (!checked.from_json(inventory.to_json())) return result;
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
    const double loss_rate = std::min(1e12, 0.035 * flux * mean_activity * std::pow(11200 / velocity, 6));
    const double release_time = std::clamp(0.15 * std::sqrt(planet_mass), 0.01, 2.0);
    const double heavy_retention = released_and_retained(inventory.age_gyr, release_time, loss_rate);
    const double steam = transition((flux - 1.15) / 0.025);
    const double water_retention = released_and_retained(inventory.age_gyr, release_time, loss_rate * (1 + 500 * steam));
    const double water_reservoir = inventory.water_mass_fraction * water_retention;

    // A fixed reference albedo avoids interpreting Venus's bright clouds as proof of an
    // ocean climate. This smooth climate proxy is independent of cached/rendered temperature,
    // procedural composition, and previous pressure estimates, so repeated calls are stable.
    const double temperate = transition((flux - 0.65) / 0.035) * (1 - steam);
    const double wet = temperate * water_reservoir / (water_reservoir + 1e-6);
    const double carbon_air = std::exp(-3 * inventory.age_gyr * wet
        * inventory.weathering_efficiency * std::min(1.0, std::sqrt(planet_mass)));
    const double climate_k = std::max(2.7, 255 * std::pow(flux, 0.25) + 33 * temperate + 450 * steam);
    // Convert retained mass fractions to atmospheric column weights, in Pa.
    const double earth_column = G * earth_mass * (earth_mass * 0.001)
        / (4 * _pi * std::pow(earth_radius, 4));
    const double column = earth_column * std::pow(planet_mass, 2) / std::pow(radius, 4);
    if (!std::isfinite(column) || column <= 0 || !std::isfinite(loss_rate)) return result;
    double co2 = column * inventory.co2_mass_fraction * heavy_retention * carbon_air;
    double nitrogen = column * inventory.nitrogen_mass_fraction * heavy_retention;
    double water = column * water_reservoir;
    // Cold trapping limits water in cold, dry climates; humidity is a 50% scenario prior.
    const double water_saturation = (0.5 * temperate + steam) * vapor_pressure(climate_k, 273.16, 611.657,
        climate_k < 273.16 ? 51060 : 45050, 647.10);
    const double co2_saturation = vapor_pressure(climate_k, 216.59, 518500,
        climate_k < 216.59 ? 25000 : 16000, 304.13);
    const double nitrogen_saturation = vapor_pressure(climate_k, 77.36, oneatm,
        climate_k < 63.15 ? 6280 : 5560, 126.19);
    // Alternating caps account for changes in the mixture while condensation proceeds.
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const double previous = co2 + nitrogen + water;
        condense(water, co2 + nitrogen, co2 / 0.044 + nitrogen / 0.028, 0.018, water_saturation);
        condense(co2, nitrogen + water, nitrogen / 0.028 + water / 0.018, 0.044, co2_saturation);
        condense(nitrogen, co2 + water, co2 / 0.044 + water / 0.018, 0.028, nitrogen_saturation);
        if (previous - co2 - nitrogen - water <= 1e-10 * std::max(1.0, previous)) break;
    }
    const double pressure = co2 + nitrogen + water;
    if (!std::isfinite(pressure)) return result;
    result.valid = true;
    result.pressure = std::min(pressure_ceiling, pressure);
    result.co2_moles = co2 / 0.044;
    result.nitrogen_moles = nitrogen / 0.028;
    result.water_moles = water / 0.018;
    result.diagnostics.update
    (
        {
            {"status", "estimated"}, {"pressure_pa", result.pressure}, {"uncapped_pressure_pa", pressure},
            {"pressure_capped", pressure > pressure_ceiling}, {"flux_earth", flux},
            {"climate_proxy_k", climate_k}, {"age_origin", "scenario_inventory"},
            {"stellar_mass_assumed", !stellar_mass_known}, {"metallicity_used", metallicity_used},
            {"metallicity_activity_sensitivity_assumed", 0.3}, {"xuv_dose_solar", dose},
            {"heavy_release_and_retention", heavy_retention}, {"water_release_and_retention", water_retention},
            {"carbon_atmospheric_share", carbon_air},
            {"column_weight_pa", {{"CO2", co2}, {"N2", nitrogen}, {"H2O", water}}},
            {"shoreline_metric", calculate_unified_metric(s, p)},
            {"limitations", "Solar_System_scale_calibration;unknown_formation_and_rotation_history;no_biology_or_primordial_H_He;climate_proxy"}
        }
    );
    // Conservation diagnostics keep the non-atmospheric reservoirs visible. The inferred
    // pressure ceiling is a rendering bound and does not destroy material in this ledger.
    const double mantle_share = std::exp(-inventory.age_gyr / release_time);
    result.diagnostics["reservoir_mass_fractions"] = json::object();
    const auto ledger = [&](const char* species, double initial, double retained, double air_weight)
    {
        const double atmospheric = air_weight / column;
        result.diagnostics["reservoir_mass_fractions"][species] =
        {
            {"initial", initial}, {"mantle", initial * mantle_share}, {"atmosphere", atmospheric},
            {"surface_storage", std::max(0.0, initial * retained - atmospheric)},
            {"escaped", std::max(0.0, initial * (1 - mantle_share - retained))}
        };
    };
    ledger("CO2", inventory.co2_mass_fraction, heavy_retention, co2);
    ledger("N2", inventory.nitrogen_mass_fraction, heavy_retention, nitrogen);
    ledger("H2O", inventory.water_mass_fraction, water_retention, water);
    if (metallicity_used) result.diagnostics["metallicity_dex"] = s.metallicity;
    return result;
}
