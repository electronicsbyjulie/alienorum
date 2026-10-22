#include <cmath>
#include <fstream>
#include <iostream>
#include "planet.h"
#include "star.h"

using namespace alienorum;

static double number(const json& row, const char* key)
{
    auto it = row.find(key);
    return it != row.end() && it->is_number() ? it->get<double>() : NAN;
}

static std::string text(const json& row, const char* key)
{
    auto it = row.find(key);
    return it != row.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static std::string label(cel_obj_type type)
{
    switch (type)
    {
        case rocky: return "rocky";
        case icy: return "icy";
        case lavaworld: return "lavaworld";
        case ice_giant: return "ice_giant";
        case gas_giant: return "gas_giant";
        case hot_jupiter: return "hot_jupiter";
        case clearskies: return "clearskies";
        case waterworld: return "waterworld";
        default: return "other";
    }
}

int main()
{
    std::ifstream input("catalogs/NASA/classification_benchmark.json");
    if (!input) return 1;
    const json snapshot = json::parse(input);
    json audit =
    {
        {"schema", 1}, {"source", "NASA pscomppars"},
        {"comparison", "Legacy decision rules on identical fixed physical inputs with HZ=false; color and atmosphere synthesis excluded"},
        {"class_counts", json::object()}, {"transitions", json::object()}, {"changed", json::array()},
        {"planets", 0}, {"metallicity_adjustments", 0}
    };
    for (const auto& row : snapshot["rows"])
    {
        const double measured_mass = number(row, "pl_bmasse"), radius = number(row, "pl_rade");
        if (!(measured_mass > 0) && !(radius > 0)) continue;
        Star host;
        host.mass = solar_mass;
        host.temperature = number(row, "st_teff");
        if (!(host.temperature > 0)) host.temperature = sun_temp;
        const std::string spectrum = text(row, "st_spectype");
        snprintf(host.spectral_type, sizeof(host.spectral_type), "%s", spectrum.c_str());
        host.metallicity = number(row, "st_met");
        host.metallicity_error_upper = number(row, "st_meterr1");
        host.metallicity_error_lower = number(row, "st_meterr2");
        host.metallicity_ratio = text(row, "st_metratio");
        host.metallicity_source = "NASA pscomppars";
        double distance = number(row, "pl_orbsmax");
        if (!(distance > 0)) distance = 1;
        double luminosity = number(row, "st_lum");
        if (!std::isfinite(luminosity))
        {
            const double flux = number(row, "pl_insol");
            luminosity = flux > 0 ? log10(flux * distance * distance) : 0;
        }
        host.absolute_magnitude = 4.74 - 2.5 * luminosity - Star::bolometric_correction(host.temperature);
        Planet planet;
        if (measured_mass > 0) planet.mass = measured_mass * earth_mass;
        if (radius > 0) planet.volumetric_mean_radius = radius * earth_radius;
        const std::string provenance = text(row, "pl_bmassprov");
        planet.mass_evidence = provenance == "Mass" ? PlanetMeasurement::observed
            : provenance == "Msini" ? PlanetMeasurement::minimum : PlanetMeasurement::estimated;
        if (number(row, "pl_bmasselim") != 0 && std::isfinite(number(row, "pl_bmasselim")))
            planet.mass_evidence = PlanetMeasurement::estimated;
        planet.radius_evidence = radius > 0 ? PlanetMeasurement::observed : PlanetMeasurement::unknown;
        if (number(row, "pl_radelim") != 0 && std::isfinite(number(row, "pl_radelim")))
            planet.radius_evidence = PlanetMeasurement::estimated;
        planet.mass_error_upper = number(row, "pl_bmasseerr1") * earth_mass;
        planet.mass_error_lower = number(row, "pl_bmasseerr2") * earth_mass;
        planet.temperature = number(row, "pl_eqt");
        if (!std::isfinite(planet.temperature)) planet.temperature = 0;
        planet.orbit = new Orbit();
        planet.orbit->center = &host;
        planet.orbit->semimajor_axis = distance * AU;
        planet.orbit->period = number(row, "pl_orbper") * oneday;
        if (!std::isfinite(planet.orbit->period)) planet.orbit->compute_period(planet.mass);
        planet.classify(false, false, true);
        const std::string current = label(planet.type);
        audit["class_counts"][current] = audit["class_counts"].value(current, 0) + 1;
        audit["planets"] = audit["planets"].get<int>() + 1;
        if (planet.classification_evidence.value("metallicity_applied", false))
            audit["metallicity_adjustments"] = audit["metallicity_adjustments"].get<int>() + 1;
        const double legacy_mass = measured_mass > 0 ? measured_mass : 0;
        const double density = measured_mass > 0 && radius > 0
            ? measured_mass * earth_mass / sphere_volume(radius * earth_radius) * 1e-6 : 0;
        const bool observed = measured_mass > 0 && radius > 0;
        const double equilibrium = number(planet.classification_evidence, "equilibrium_temperature_k");
        const double temperature = planet.temperature > 0 ? planet.temperature : equilibrium;
        cel_obj_type legacy;
        if (legacy_mass < rocky_mass_cutoff / earth_mass ||
            (legacy_mass < jupiter_mass / earth_mass && observed && density > rocky_density_cutoff))
        {
            if (observed && temperature < water_freezing && density > 0 && density < rocky_density_cutoff) legacy = icy;
            else legacy = temperature > lava_T_cutoff ? lavaworld : temperature > water_freezing ? rocky : icy;
        }
        else if (planet.orbit->period < 10 * oneday) legacy = hot_jupiter;
        else if (equilibrium >= 350 && equilibrium < 800) legacy = clearskies;
        else if (legacy_mass < giant_mass_cutoff / earth_mass && (!observed || density > giant_density_cutoff)) legacy = ice_giant;
        else legacy = gas_giant;
        if (legacy != planet.type)
        {
            const std::string transition = label(legacy) + " -> " + current;
            audit["transitions"][transition] = audit["transitions"].value(transition, 0) + 1;
            audit["changed"].push_back(
            {
                {"planet", row.value("pl_name", "")}, {"legacy", label(legacy)}, {"current", current},
                {"basis", planet.classification_evidence.value("basis", "")}
            });
        }
    }
    std::ofstream output("catalogs/NASA/classification_change_audit.json");
    output << audit.dump(2) << '\n';
    std::cout << "Audited " << audit["planets"] << " planets; " << audit["changed"].size()
              << " changed decision-tree results; " << audit["metallicity_adjustments"] << " metallicity adjustments.\n";
    return output ? 0 : 1;
}
