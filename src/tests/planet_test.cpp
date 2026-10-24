#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/planet.h"
#include "../classes/orbit_spacing.h"
#include "../classes/cat.h"
#include "../classes/chemistry.h"
#include "../classes/shore.h"
#include "../visuals.h"
#include "../loaders.h"
#include "universe_fixture.h"

using namespace alienorum;
using json = nlohmann::json;

TEST(CosmicShoreTest, PressureSaturatesWithoutOverflowOrVacuumDiscontinuity)
{
    EXPECT_DOUBLE_EQ(CosmicShore::estimate_surface_pressure(-1), 0);
    EXPECT_DOUBLE_EQ(CosmicShore::estimate_surface_pressure(0), 503);
    EXPECT_NEAR(CosmicShore::estimate_surface_pressure(4), 5030000, 1e-6);
    double previous = 0;
    for (double metric : {5.0, 6.0, 100.0, 1000.0, std::numeric_limits<double>::infinity()})
    {
        const double pressure = CosmicShore::estimate_surface_pressure(metric);
        EXPECT_TRUE(std::isfinite(pressure));
        EXPECT_GE(pressure, previous);
        EXPECT_LE(pressure, 1e8);
        previous = pressure;
    }
    EXPECT_DOUBLE_EQ(previous, 1e8);
    EXPECT_DOUBLE_EQ(CosmicShore::estimate_surface_pressure(-INFINITY), 0);
    EXPECT_DOUBLE_EQ(CosmicShore::estimate_surface_pressure(NAN), 0);
}

class PlanetClassificationEvidenceTest : public UniverseFixture
{
protected:
    void TearDown() override
    {
        delete_the_universe();
        UniverseFixture::TearDown();
    }
};

TEST_F(PlanetClassificationEvidenceTest, PlutoSizedAsteroidLeavesTheSunWithEightPlanets)
{
    Star* sun = make_star("Sun");
    CatalogReader reader;
    ASSERT_EQ(reader.read_local_planets(cels, MAX_CELOBJS, sun), 8);
    EXPECT_EQ(sun->has_planets, 8);
    EXPECT_EQ(sun->pl_indices.size(), 8u);
    Planet* asteroid = new Planet();
    strcpy(asteroid->name, "Fictional Pluto-sized asteroid");
    asteroid->mass = .0022 * earth_mass;
    asteroid->volumetric_mean_radius = 1188300;
    asteroid->asteroid_no = 999999;
    asteroid->orbit = new Orbit();
    asteroid->orbit->center = sun;
    asteroid->orbit->semimajor_axis = 40 * AU;
    ASSERT_TRUE(append_cel(asteroid));
    asteroid->update_location(J2000_TIME_T);
    EXPECT_LT(asteroid->mass, planet_count_mass_cutoff);
    EXPECT_EQ(sun->has_planets, 8);
    EXPECT_EQ(sun->pl_indices.size(), 8u);
    // The mass cutoff also excludes a small unnamed body without an asteroid number.
    asteroid->asteroid_no = 0;
    asteroid->sync_host_statistics();
    EXPECT_EQ(sun->has_planets, 8);
    EXPECT_EQ(std::count(sun->pl_indices.begin(), sun->pl_indices.end(), asteroid->seqno), 0);
}

TEST_F(PlanetClassificationEvidenceTest, DelayedOrbitSetupRegistersOnePlanetAndTracksItsHost)
{
    Star* first = make_star("First host");
    Star* second = make_star("Second host");
    Planet* planet = new Planet();
    ASSERT_TRUE(append_cel(planet));
    EXPECT_EQ(first->has_planets, 0);
    planet->orbit = new Orbit();
    planet->orbit->center = first;
    planet->mass = earth_mass;
    ASSERT_TRUE(planet->set_habitable_orbit());
    EXPECT_EQ(first->has_planets, 1);
    EXPECT_EQ(first->has_hz_planets, 1);
    EXPECT_EQ(first->pl_indices, std::vector<int>{planet->seqno});
    planet->sync_host_statistics();
    planet->update_location(J2000_TIME_T);
    EXPECT_EQ(first->pl_indices.size(), 1u);
    planet->orbit->center = second;
    planet->update_location(J2000_TIME_T);
    EXPECT_EQ(first->has_planets, 0);
    EXPECT_EQ(first->has_hz_planets, 0);
    EXPECT_TRUE(first->pl_indices.empty());
    EXPECT_EQ(second->has_planets, 1);
    EXPECT_EQ(second->has_hz_planets, 1);
    planet->set_explicit_mass(planet_count_mass_cutoff * .5);
    EXPECT_EQ(second->has_planets, 0);
    planet->set_explicit_mass(planet_count_mass_cutoff);
    EXPECT_EQ(second->has_planets, 1);
    planet->deleted = true;
    planet->sync_host_statistics();
    EXPECT_EQ(second->has_planets, 0);
    EXPECT_EQ(second->has_hz_planets, 0);
}

TEST_F(PlanetClassificationEvidenceTest, OrbitEditsUpdateHzCountsWithoutFollowingOrbitalPhase)
{
    Star* host = make_star("Orbit-edit host");
    Planet* planet = make_planet(host, "Orbit-edit planet", AU);
    EXPECT_EQ(host->has_hz_planets, 1);
    planet->orbit->eccentricity = .7;
    planet->orbit->compute_period(planet->mass);
    for (double phase : {0.0, .25, .5, .75})
    {
        planet->update_location(J2000_TIME_T + phase * planet->orbit->period);
        EXPECT_EQ(host->has_hz_planets, 1);
    }
    planet->orbit->semimajor_axis = 4 * AU;
    planet->update_location(J2000_TIME_T);
    EXPECT_EQ(host->has_hz_planets, 0);
    planet->orbit->period = oneyear;
    planet->orbit->compute_semimajor_axis(planet->mass);
    planet->update_location(J2000_TIME_T);
    EXPECT_EQ(host->has_hz_planets, 1);
    planet->orbit->period *= 8; // A resonance selection also changes period before deriving SMA.
    planet->orbit->compute_semimajor_axis(planet->mass);
    planet->sync_host_statistics();
    EXPECT_EQ(host->has_hz_planets, 0);
    ASSERT_TRUE(planet->set_habitable_orbit());
    EXPECT_EQ(host->has_hz_planets, 1);
    host->absolute_magnitude = 0;
    planet->sync_host_statistics();
    EXPECT_EQ(host->has_hz_planets, 0);
    EXPECT_EQ(host->has_planets, 1);
}

TEST_F(PlanetClassificationEvidenceTest, InferredAtmosphereHonorsTheShorelineCeiling)
{
    Planet* world = make_planet(make_star(), "High retention", 0.2 * AU);
    world->user_added = true;
    world->mass = 200 * earth_mass;
    world->volumetric_mean_radius = 2 * earth_radius;
    ASSERT_GT(CosmicShore::calculate_unified_metric(*static_cast<Star*>(world->cenobj), *world), 6);
    world->apply_cosmic_shoreline();
    EXPECT_TRUE(std::isfinite(world->get_surface_pressure()));
    EXPECT_GT(world->get_surface_pressure(), 0);
    EXPECT_LE(world->get_surface_pressure(), 1e8);
}

TEST_F(PlanetClassificationEvidenceTest, ShorelinePreservesSuppliedPressureIncludingVacuumAcrossSaves)
{
    Planet* world = make_planet(make_star(), "Supplied atmosphere", 0.2 * AU);
    world->user_added = true;
    for (double pressure : {0.0, 1.0 * oneatm, 2e8})
    {
        world->ensure_atmosphere()->from_json({{"surface_pressure", pressure}});
        world->apply_cosmic_shoreline();
        EXPECT_DOUBLE_EQ(world->get_surface_pressure(), pressure);
        EXPECT_TRUE(world->atm->pressure_supplied);
        Planet restored;
        restored.from_json(world->to_json());
        ASSERT_NE(restored.atm, nullptr);
        EXPECT_TRUE(restored.atm->pressure_supplied);
        EXPECT_DOUBLE_EQ(restored.get_surface_pressure(), pressure);
    }
    Planet legacy;
    legacy.from_json({{"surface_pressure", 0}});
    ASSERT_NE(legacy.atm, nullptr);
    EXPECT_TRUE(legacy.atm->pressure_supplied);
    EXPECT_DOUBLE_EQ(legacy.get_surface_pressure(), 0);
    world->atm->pressure_supplied = false;
    world->apply_cosmic_shoreline();
    EXPECT_LE(world->get_surface_pressure(), 1e8);
    Planet inferred;
    inferred.from_json(world->to_json());
    ASSERT_NE(inferred.atm, nullptr);
    EXPECT_FALSE(inferred.atm->pressure_supplied);
}

TEST_F(PlanetClassificationEvidenceTest, EstimatedRadiusNeverBecomesObservedDensity)
{
    Planet* planet = make_planet(make_star(), "Estimated radius");
    planet->mass = 3 * earth_mass;
    planet->volumetric_mean_radius = 0;
    planet->mass_evidence = PlanetMeasurement::observed;
    planet->classify(false, false, true);
    EXPECT_EQ(planet->radius_evidence, PlanetMeasurement::estimated);
    EXPECT_FALSE(planet->classification_evidence["density_observed"].get<bool>());
    EXPECT_TRUE(planet->classification_evidence["density_estimated"].get<bool>());
    planet->mass = 20 * earth_mass;
    planet->classify(false, true, true);
    EXPECT_EQ(planet->type, ice_giant);
    EXPECT_EQ(planet->radius_evidence, PlanetMeasurement::estimated);
    EXPECT_FALSE(planet->classification_evidence["density_observed"].get<bool>());
    const json before = planet->classification_evidence;
    planet->classify(false, false, true);
    EXPECT_EQ(planet->classification_evidence, before);
    EXPECT_EQ(planet->atm, nullptr);
}

TEST_F(PlanetClassificationEvidenceTest, MissingPhysicalPropertiesCannotLeaveAStellarType)
{
    Star* host = make_star("Unmeasured planet host");
    for (Planet* body : {static_cast<Planet*>(new Planet()), static_cast<Planet*>(new Moon())})
    {
        body->orbit = new Orbit();
        body->orbit->center = host;
        body->orbit->semimajor_axis = AU;
        append_cel(body);
        EXPECT_EQ(body->type, rocky);
        body->classify();
        EXPECT_EQ(body->type, rocky);
        EXPECT_NE(body->typeclass(), class_star);
        EXPECT_EQ(body->classification_evidence["basis"], "insufficient_planetary_properties");
        EXPECT_EQ(body->atm, nullptr);
    }
}

TEST_F(PlanetClassificationEvidenceTest, MinimumMassAndUncertaintiesSurviveUniverseSaves)
{
    Planet* planet = make_planet(make_star(), "Minimum mass");
    planet->mass = 4 * earth_mass;
    planet->msini = planet->mass;
    planet->mass_error_upper = 0.3 * earth_mass;
    planet->mass_error_lower = -0.2 * earth_mass;
    planet->radius_error_upper = 1000;
    planet->radius_error_lower = -2000;
    planet->classify(false, true, true);
    EXPECT_EQ(planet->mass_evidence, PlanetMeasurement::minimum);
    EXPECT_TRUE(planet->classification_evidence["density_is_lower_bound"].get<bool>());
    EXPECT_FALSE(planet->classification_evidence["density_observed"].get<bool>());
    Planet restored;
    ASSERT_TRUE(restored.from_json(planet->to_json()));
    EXPECT_EQ(restored.mass_evidence, PlanetMeasurement::minimum);
    EXPECT_EQ(restored.radius_evidence, PlanetMeasurement::observed);
    EXPECT_DOUBLE_EQ(restored.mass_error_lower, -0.2 * earth_mass);
    EXPECT_DOUBLE_EQ(restored.radius_error_upper, 1000);
    EXPECT_EQ(restored.classification_evidence, planet->classification_evidence);
}

TEST_F(PlanetClassificationEvidenceTest, ShortPeriodLowMassBodiesDoNotBecomeHotJupiters)
{
    Star* star = make_star();
    Planet* planet = make_planet(star, "Close low-mass planet", 0.02 * AU);
    planet->mass = 10 * earth_mass;
    planet->volumetric_mean_radius = 4 * earth_radius;
    planet->orbit->period = 2 * oneday;
    planet->classify(false, true, true);
    EXPECT_EQ(planet->type, ice_giant);
    star->has_hot_jupiter = true;
    planet->classify(true, true, true);
    EXPECT_EQ(planet->type, ice_giant);
    EXPECT_EQ(planet->atm, nullptr);
}

TEST_F(PlanetClassificationEvidenceTest, OrbitalAncestryUsesTheActualHostAndHandlesCycles)
{
    Star* primary = make_star("Primary");
    Star* secondary = make_star("Secondary");
    secondary->make_companion_of(primary, 'B');
    Planet* planet = make_planet(secondary, "Secondary planet");
    Planet* moon = new Moon();
    moon->mass = lunar_mass;
    moon->volumetric_mean_radius = 1737400;
    moon->orbit = new Orbit();
    moon->orbit->center = planet;
    moon->orbit->semimajor_axis = 384400000;
    moon->orbit->period = 2 * oneday;
    append_cel(moon);
    moon->classify(false, true, true);
    EXPECT_EQ(moon->classification_evidence["host"], "Secondary");
    EXPECT_EQ(moon->type, rocky);
    planet->orbit->center = moon;
    moon->classify(false, false, true);
    EXPECT_TRUE(moon->classification_evidence["irradiation_earth"].is_null());
    EXPECT_EQ(moon->atm, nullptr);
}

TEST_F(PlanetClassificationEvidenceTest, ObservedDensityAndTypeLocksOutrankChemistry)
{
    Star* host = make_star();
    Planet* planet = make_planet(host, "Observed density");
    planet->mass = 8 * earth_mass;
    planet->volumetric_mean_radius = 1.6 * earth_radius;
    for (double metal : {-0.5, 0.0, 0.5})
    {
        host->metallicity = metal;
        host->metallicity_ratio = "[Fe/H]";
        host->metallicity_source = "NASA pscomppars";
        host->metallicity_error_upper = 0.05;
        host->metallicity_error_lower = -0.05;
        planet->classify(false, true, true);
        EXPECT_EQ(planet->type, rocky);
        EXPECT_FALSE(planet->classification_evidence["metallicity_applied"].get<bool>());
    }
    planet->lock_type = true;
    planet->type = waterworld;
    planet->classify(false, true, true);
    EXPECT_EQ(planet->type, waterworld);
    EXPECT_EQ(planet->classification_evidence["basis"], "explicit_type_lock");
}

TEST_F(PlanetClassificationEvidenceTest, SyntheticModelExercisesValidationAndApplicabilityGates)
{
    struct Directory
    {
        std::filesystem::path original = std::filesystem::current_path();
        std::filesystem::path temporary = std::filesystem::temp_directory_path() /
            ("alienorum_prior_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Directory()
        {
            std::filesystem::create_directories(temporary / "catalogs/NASA");
            std::filesystem::current_path(temporary);
        }
        ~Directory()
        {
            std::filesystem::current_path(original);
            std::filesystem::remove_all(temporary);
        }
    } directory;
    const json model =
    {
        {"schema", 1}, {"enabled", true}, {"improvement_95_percent_host_bootstrap", {0.01, 0.02}},
        {"baseline_coefficients", {-1.0, 0.0, 0.0}}, {"metallicity_coefficients", {-1.0, 0.0, 0.0, 4.0}},
        {"maximum_metallicity_error", 0.2}, {"maximum_mass_relative_error", 0.35},
        {"domain", {{"mass_earth", {1, 30}}, {"irradiation_earth", {0.1, 3000}},
            {"metallicity_dex", {-0.5, 0.5}}, {"temperature_k", {4900, 6300}}}}
    };
    std::ofstream("catalogs/NASA/classification_model.json") << model;
    Star* host = make_star();
    host->metallicity = 0.5;
    host->metallicity_ratio = "[Fe/H]";
    host->metallicity_source = "NASA pscomppars";
    host->metallicity_error_upper = 0.05;
    host->metallicity_error_lower = -0.05;
    Planet* planet = make_planet(host, "Prior case");
    planet->mass = 3 * earth_mass;
    planet->mass_evidence = PlanetMeasurement::observed;
    planet->mass_error_upper = 0.2 * earth_mass;
    planet->mass_error_lower = -0.2 * earth_mass;
    planet->radius_evidence = PlanetMeasurement::estimated;
    planet->classify(false, false, true);
    EXPECT_EQ(planet->type, ice_giant);
    EXPECT_TRUE(planet->classification_evidence["metallicity_applied"].get<bool>());
    const json first = planet->classification_evidence;
    planet->classify(false, false, true);
    EXPECT_EQ(planet->classification_evidence, first);
    planet->mass_evidence = PlanetMeasurement::estimated;
    planet->msini = 1.5 * earth_mass;
    for (double metal : {-0.5, 0.5})
    {
        host->metallicity = metal;
        planet->type = rocky;
        planet->classify(false, false, true);
        EXPECT_EQ(planet->mass_evidence, PlanetMeasurement::estimated);
        EXPECT_TRUE(planet->classification_evidence["mass_from_rv_inclination"].get<bool>());
        EXPECT_EQ(planet->classification_evidence["metallicity_model"], "validated_large_radius_prior");
        EXPECT_EQ(planet->type, metal < 0 ? rocky : ice_giant);
        EXPECT_EQ(planet->classification_evidence["metallicity_applied"].get<bool>(), metal > 0);
    }
    host->metallicity = .5;
    EXPECT_FALSE(planet_envelope_prior(*host, 3, 1, 0.5).applicable);
    EXPECT_FALSE(planet_envelope_prior(*host, 100, 1, 0.1).applicable);
    host->metallicity_ratio = "[M/H]";
    EXPECT_FALSE(planet_envelope_prior(*host, 3, 1, 0.1).applicable);
    host->metallicity_ratio = "[Fe/H]";
    host->metallicity_source = "XHIP V/137D C";
    EXPECT_FALSE(planet_envelope_prior(*host, 3, 1, 0.1).applicable);
}

struct SolarPrediction
{
    const char* name;
    cel_obj_type expected;
};

class SolarSystemClassificationTest : public UniverseFixture, public ::testing::WithParamInterface<SolarPrediction>
{
protected:
    std::map<std::string, json> physical_rows;
    std::map<std::string, CelestialObject*> objects;
    Star* sun = nullptr;

    void SetUp() override
    {
        UniverseFixture::SetUp();
        std::ifstream input("catalogs/planets.json");
        ASSERT_TRUE(input.good());
        const json catalog = json::parse(input);
        for (const auto& row : catalog) physical_rows[row.at("BODYNAME").get<std::string>()] = row;
        sun = make_star("Sun");
        objects["Sun"] = sun;
    }

    void TearDown() override
    {
        delete_the_universe();
        UniverseFixture::TearDown();
    }

    Planet* physical_body(const std::string& name)
    {
        auto found = objects.find(name);
        if (found != objects.end()) return dynamic_cast<Planet*>(found->second);
        const json& row = physical_rows.at(name);
        const std::string parent_name = row.at("CENTER_OF_ORBIT").get<std::string>();
        CelestialObject* parent = parent_name == "Sun" ? static_cast<CelestialObject*>(sun) : physical_body(parent_name);
        Planet* body = parent_name == "Sun" ? new Planet() : new Moon();
        snprintf(body->name, sizeof(body->name), "%s", name.c_str());
        // The source catalog uses kilograms; the engine stores mass in grams.
        body->mass = 1000 * row.at("Mass").get<double>();
        if (row.contains("VolMeanRad")) body->volumetric_mean_radius = row.at("VolMeanRad").get<double>();
        else
        {
            // The local catalog stores full ellipsoid diameters in kilometres.
            body->volumetric_mean_radius = 500 * cbrt(row.at("Width").get<double>() *
                row.at("Height").get<double>() * row.at("Depth").get<double>());
        }
        body->mass_evidence = PlanetMeasurement::observed;
        body->radius_evidence = PlanetMeasurement::observed;
        body->albedo = row.value("BondAlbedo", 0.0);
        body->temperature = row.value("SurfaceTemperature", 0.0);
        body->orbit = new Orbit();
        body->orbit->center = parent;
        body->orbit->semimajor_axis = row.at("SEMIMAJOR_AXIS").get<double>();
        body->orbit->period = row.at("OrbitPeriod").get<double>();
        body->cenobj = parent;
        // Read physical inputs only: catalog class labels, maps and atmospheres are excluded.
        body->lock_type = false;
        append_cel(body);
        objects[name] = body;
        return body;
    }
};

TEST_P(SolarSystemClassificationTest, PredictsFromPhysicalInputsAcrossStartingClasses)
{
    const SolarPrediction prediction = GetParam();
    Planet* body = physical_body(prediction.name);
    ASSERT_NE(body, nullptr);
    const double catalog_temperature = body->temperature;
    const double observed_mass = body->mass, observed_radius = body->volumetric_mean_radius;
    const std::vector<cel_obj_type> starting_classes =
    {
        rocky, icy, lavaworld, waterworld, hycean, ice_giant, gas_giant, hot_jupiter, clearskies
    };
    for (bool known_solar_iron : {false, true})
    {
        sun->metallicity = known_solar_iron ? 0.0 : NAN;
        sun->metallicity_ratio = "[Fe/H]";
        sun->metallicity_source = "Solar reference";
        sun->metallicity_error_upper = sun->metallicity_error_lower = 0;
        for (bool supplied_temperature : {false, true})
        {
            body->temperature = supplied_temperature ? catalog_temperature : 0;
            json common_evidence;
            for (cel_obj_type initial : starting_classes)
            {
                SCOPED_TRACE(::testing::Message() << prediction.name << ", initial=" << static_cast<int>(initial)
                    << ", solar_iron=" << known_solar_iron << ", stored_temperature=" << supplied_temperature);
                body->type = initial;
                body->classify();
                EXPECT_EQ(body->type, prediction.expected) << body->classification_evidence.dump();
                EXPECT_TRUE(body->classification_evidence["density_observed"].get<bool>());
                EXPECT_FALSE(body->classification_evidence["metallicity_applied"].get<bool>());
                EXPECT_EQ(body->classification_evidence["host"], "Sun");
                EXPECT_EQ(body->atm, nullptr);
                EXPECT_DOUBLE_EQ(body->mass, observed_mass);
                EXPECT_DOUBLE_EQ(body->volumetric_mean_radius, observed_radius);
                if (common_evidence.is_null()) common_evidence = body->classification_evidence;
                EXPECT_EQ(body->classification_evidence, common_evidence);
                body->classify();
                EXPECT_EQ(body->type, prediction.expected);
                EXPECT_EQ(body->classification_evidence, common_evidence);
                EXPECT_EQ(body->atm, nullptr);
            }
        }
    }
}

// Expected broad physical classes are independent of any labels in planets.json.
// Europa and the other icy moons can contain substantial rock as well as ice.
INSTANTIATE_TEST_SUITE_P(SolarPredictions, SolarSystemClassificationTest, ::testing::Values(
    SolarPrediction{"Mercury", rocky}, SolarPrediction{"Venus", rocky},
    SolarPrediction{"Earth", rocky}, SolarPrediction{"Mars", rocky},
    SolarPrediction{"Jupiter", gas_giant}, SolarPrediction{"Saturn", gas_giant},
    SolarPrediction{"Uranus", ice_giant}, SolarPrediction{"Neptune", ice_giant},
    SolarPrediction{"Moon", rocky}, SolarPrediction{"Phobos", rocky}, SolarPrediction{"Deimos", rocky},
    SolarPrediction{"Io", rocky}, SolarPrediction{"Europa", icy}, SolarPrediction{"Ganymede", icy},
    SolarPrediction{"Callisto", icy}, SolarPrediction{"Enceladus", icy}, SolarPrediction{"Titan", icy},
    SolarPrediction{"Triton", icy}),
    [](const ::testing::TestParamInfo<SolarPrediction>& info) { return std::string(info.param.name); });

class LavaMapTest : public UniverseFixture
{
    protected:
    Planet *world = nullptr;

    void SetUp() override
    {
        UniverseFixture::SetUp();
        world = make_planet(make_star(), "Lava texture test", 0.02 * AU);
        world->type = lavaworld;
        world->temperature = 1600.0;
        world->sidereal_rotational_period = world->orbit->period;
        world->rnd_seed = 173;
        world->fictitious_map_height = 32;
        world->ring_status_known = true;
        world->surf_map = new Map(world);
        world->night_map = new Map(world);
    }

    void TearDown() override
    {
        delete_the_universe();
        UniverseFixture::TearDown();
    }

    void generate()
    {
        world->surf_map->generate_lava_map(world);
        world->night_map->generate_lava_map(world, true);
    }

    double hemisphere_brightness(Map *map, bool day)
    {
        double sum = 0.0, area = 0.0;
        for (int latitude = -75; latitude <= 75; latitude += 15)
        {
            const double lat = latitude / fiftyseven;
            for (int longitude = -75; longitude <= 75; longitude += 15)
            {
                const double lon = longitude / fiftyseven + (day ? 0.0 : _pi);
                const RGB3 rgb = map->color_at(lat, lon);
                sum += cos(lat) * (0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b);
                area += cos(lat);
            }
        }
        return sum / area;
    }
};

TEST_F(LavaMapTest, DayPaletteOutshinesNightAtEveryLongitude)
{
    generate();
    ASSERT_TRUE(world->surf_map->is_complete());
    ASSERT_TRUE(world->night_map->is_complete());
    ASSERT_TRUE(world->surf_map->has_bump_data());

    const double hot_day = hemisphere_brightness(world->surf_map, true);
    const double cool_night = hemisphere_brightness(world->night_map, false);
    EXPECT_GT(hot_day, 2.0 * cool_night);
    EXPECT_GT(cool_night, 0.0);
    for (int longitude = -180; longitude < 180; longitude += 15)
    {
        const double lon = longitude / fiftyseven;
        const RGB3 day = world->surf_map->color_at(0.0, lon);
        const RGB3 night = world->night_map->color_at(0.0, lon);
        EXPECT_GT(day.r, 2 * night.r);
        EXPECT_GT(day.g, 2 * night.g);
    }
}

TEST_F(LavaMapTest, HotSurfacesRetainDarkCrustAndOrangeFractures)
{
    for (double temperature : {1600.0, 3200.0, 6000.0, 10000.0})
    {
        world->temperature = temperature;
        generate();
        const size_t count = world->surf_map->get_width() * world->surf_map->get_height();
        std::vector<unsigned char> pixels(count * 4);
        world->surf_map->export_rgba(pixels.data());
        size_t dark = 0, molten = 0, pale = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto *rgb = &pixels[i * 4];
            if (rgb[0] < 100 && rgb[1] < 65) ++dark;
            if (rgb[0] > 180 && rgb[0] > 1.2 * rgb[1]) ++molten;
            if (rgb[0] > 220 && rgb[1] > 180 && rgb[2] > 80) ++pale;
        }
        EXPECT_GT(dark, count / 4) << temperature;
        EXPECT_GT(molten, count / 100) << temperature;
        EXPECT_EQ(pale, 0u) << temperature;
    }
}

TEST_F(LavaMapTest, MapsShareReliefAfterRecoloringAndResizing)
{
    generate();
    const unsigned int first_generation = world->night_map->gen;
    std::vector<float> original(64 * 32), regenerated(64 * 32);
    world->surf_map->export_bump(original.data());
    world->surf_map->_map_resample_bump_regen_rocky(world);
    world->surf_map->export_bump(regenerated.data());
    EXPECT_EQ(original, regenerated);
    EXPECT_NE(world->night_map->gen, first_generation);
    const auto extremes = std::minmax_element(original.begin(), original.end());
    EXPECT_LT(*extremes.first, *extremes.second);

    world->fictitious_map_height = 64;
    world->surf_map->_map_resample_bump_regen_rocky(world);
    ASSERT_EQ(world->surf_map->get_height(), 64u);
    ASSERT_EQ(world->night_map->get_height(), 64u);
    ASSERT_EQ(world->night_map->get_width(), 128u);
    std::vector<float> day_relief(128 * 64), night_relief(128 * 64);
    world->surf_map->export_bump(day_relief.data());
    world->night_map->export_bump(night_relief.data());
    EXPECT_EQ(day_relief, night_relief);
    EXPECT_GT(hemisphere_brightness(world->surf_map, true),
        2.0 * hemisphere_brightness(world->night_map, false));
}

TEST_F(LavaMapTest, HigherTemperatureIncreasesGlowForTheSameTerrain)
{
    generate();
    const double initial = hemisphere_brightness(world->night_map, true);
    world->temperature = 2100.0;
    world->night_map->generate_lava_map(world, true);
    EXPECT_GT(hemisphere_brightness(world->night_map, true), initial);

    world->sidereal_rotational_period = world->orbit->period * 0.2;
    generate();
    EXPECT_GT(hemisphere_brightness(world->surf_map, true),
        hemisphere_brightness(world->night_map, true));
}

TEST_F(LavaMapTest, LoaderRefreshesNightAfterSurfaceReplacement)
{
    generate();
    const unsigned int first_generation = world->night_map->gen;
    delete world->surf_map;
    world->surf_map = nullptr;
    world->ignore_map_files = true;
    world->fictitious_map_height = 48;
    load_textures(world);
    ASSERT_TRUE(world->surf_map->is_complete());
    ASSERT_TRUE(world->night_map->is_complete());
    EXPECT_EQ(world->surf_map->get_height(), 48u);
    EXPECT_EQ(world->night_map->get_height(), 48u);
    EXPECT_NE(world->night_map->gen, first_generation);
    EXPECT_GT(hemisphere_brightness(world->surf_map, true),
        2.0 * hemisphere_brightness(world->night_map, false));
}

// =====================================================================
// Planet Initialization & Lifecycle Tests
// =====================================================================

TEST(PlanetTest, DefaultInitialization)
{
    Planet p;
    
    // Check initialized variables
    EXPECT_DOUBLE_EQ(p.albedo, 0.0);
    EXPECT_DOUBLE_EQ(p.msini, 0.0);
    EXPECT_DOUBLE_EQ(p.opposition_surge, 0.0);
    EXPECT_DOUBLE_EQ(p.amt_lit, 0.0);
    EXPECT_DOUBLE_EQ(p.J2, 0.0);
    EXPECT_DOUBLE_EQ(p.ring_radius, 0.0);
    EXPECT_EQ(p.asteroid_no, 0);
    EXPECT_FALSE(p.lock_type);
    
    // Check that pointers start null
    EXPECT_EQ(p.atm, nullptr);
}

// =====================================================================
// Atmosphere Pointer Lifecycle & Accessors
// =====================================================================

TEST(PlanetTest, NullAtmosphereAccessors)
{
    Planet airless_world;
    
    // Ensure atm is null initially
    ASSERT_EQ(airless_world.atm, nullptr);
    
    // Accessors must safely return 0 without segfaulting when atm is null
    EXPECT_DOUBLE_EQ(airless_world.get_surface_pressure(), 0.0);
    EXPECT_DOUBLE_EQ(airless_world.get_atmospheric_tau(), 0.0);
    EXPECT_DOUBLE_EQ(airless_world.get_particulates(), 0.0);
}

TEST(PlanetTest, EnsureAtmosphereInitialization)
{
    Planet world;
    
    Atmosphere* created_atm = world.ensure_atmosphere();
    
    // Must return a valid pointer and assign it to the member
    ASSERT_NE(created_atm, nullptr);
    EXPECT_EQ(world.atm, created_atm);
    
    // Calling it again should return the exact same pointer
    Atmosphere* second_call = world.ensure_atmosphere();
    EXPECT_EQ(created_atm, second_call);
}

TEST(PlanetTest, ValidAtmosphereAccessors)
{
    Planet world;
    Atmosphere* atm = world.ensure_atmosphere();

    // get_atmospheric_tau() derives tau from the real composition rather than trusting a bare
    // hand-set field (a zero tau from a real, mostly-inert composition has to be trusted rather
    // than mistaken for "no data" -- see RealCompositionWithZeroTauIsTrusted), so give it a
    // composition to derive from and check against the same formula it uses internally.
    atm->surface_pressure = 50000.0;
    atm->particulates = 0.8;
    AtmosphereComposition* comp = atm->ensure_composition();
    comp->CO2_portion = 1.0;
    double expected_tau = atmospheric_tau(50000.0 * 0.000009869, 1.0, 0,0,0,0,0,0,0,0,0,0);

    EXPECT_DOUBLE_EQ(world.get_surface_pressure(), 50000.0);
    EXPECT_DOUBLE_EQ(world.get_atmospheric_tau(), expected_tau);
    EXPECT_DOUBLE_EQ(world.get_particulates(), 0.8);
}

// =====================================================================
// Serialization (JSON)
// =====================================================================

TEST(PlanetTest, JsonSerializationRoundTrip)
{
    Planet original;
    original.albedo = 0.35; // Roughly Earth
    original.opposition_surge = 0.5;
    original.asteroid_no = 42;
    original.lock_type = true;
    original.msini = 1.23e27;
    
    // Instantiate atmosphere to ensure it serializes
    Atmosphere* atm = original.ensure_atmosphere();
    atm->surface_pressure = 100000.0;
    
    json j = original.to_json();
    
    Planet restored;
    bool success = restored.from_json(j);
    
    EXPECT_TRUE(success);
    EXPECT_DOUBLE_EQ(restored.albedo, 0.35);
    EXPECT_DOUBLE_EQ(restored.opposition_surge, 0.5);
    EXPECT_DOUBLE_EQ(restored.msini, 1.23e27);

    EXPECT_EQ(restored.asteroid_no, 42);
    EXPECT_TRUE(restored.lock_type);

    // Neither is written when it has nothing to say, so an ordinary planet's entry is unchanged.
    Planet major_planet;
    json jmp = major_planet.to_json();
    EXPECT_FALSE(jmp.contains("asteroid_no"));
    EXPECT_FALSE(jmp.contains("lock_type"));
    EXPECT_FALSE(jmp.contains("msini"));
    
    // Ensure the atmosphere was recreated and populated
    ASSERT_NE(restored.atm, nullptr);
    EXPECT_DOUBLE_EQ(restored.get_surface_pressure(), 100000.0);
}

// =====================================================================
// Planet Atmospheric Math Tests
// =====================================================================

TEST(PlanetMathTest, EstimateSurfaceTemperature)
{
    Planet world;
    Atmosphere* atm = world.ensure_atmosphere();
    
    // We mock equilibrium temperature for this test to be isolated.
    // Assuming equilibrium_temperature() returns something based on distance/albedo.
    // For this test, let's assume equilibrium_temperature() evaluates to 255.0 K (Earth-like).
    // (If equilibrium_temperature is derived, you may set distance/albedo here)
    
    // Case 1: No atmospheric tau (greenhouse factor should be 1.0)
    atm->tau = 0.0;
    double t_surf_no_gh = world.estimate_surface_temperature();
    // T_surf should equal T_eq
    EXPECT_DOUBLE_EQ(t_surf_no_gh, world.equilibrium_temperature());

    // Case 2: With atmospheric tau
    atm->tau = 1.0; 
    // greenhouse_factor = 1.0 + (0.75 * 1.0 * 2.4) = 2.8
    // T_surf = T_eq * pow(2.8, 0.25) ≈ T_eq * 1.2936
    double expected_t_surf = world.equilibrium_temperature() * std::pow(2.8, 0.25);
    double t_surf_with_gh = world.estimate_surface_temperature();
    
    EXPECT_NEAR(t_surf_with_gh, expected_t_surf, 0.1);
}

TEST(PlanetMathTest, EstimateScaleHeight)
{
    Planet earth;
    Atmosphere* atm = earth.ensure_atmosphere();
    
    // Case 1: Airless body (Pressure <= 0)
    atm->surface_pressure = 0.0;
    EXPECT_DOUBLE_EQ(earth.estimate_scale_height(), 0.0);

    // Case 2: Earth-like parameters
    atm->surface_pressure = 101325.0; // 1 atm in Pa
    // M will fall back to 0.0289644 since we haven't instantiated atm->comp
    
    // Note: This test's exact expected value depends heavily on what 
    // estimate_surface_temperature() and estimate_surface_gravity() evaluate to.
    double t_surf = earth.estimate_surface_temperature();
    double g = earth.estimate_surface_gravity();
    
    // Only test if the prerequisite functions return valid non-zero values
    if (t_surf > 0 && g > 0)
    {
        double expected_height = 8.314462618 * t_surf / (0.0289644 * g);
        EXPECT_NEAR(earth.estimate_scale_height(), expected_height, 1.0);
    }
}

// =====================================================================
// AstorbRow Struct Tests
// =====================================================================

TEST(AstorbRowTest, InitializationAndAssignment)
{
    AstorbRow asteroid;
    
    // Default initialization checks
    EXPECT_EQ(asteroid.diam, 0.0f);
    EXPECT_EQ(asteroid.sma, 0.0f);
    EXPECT_EQ(asteroid.incl, 0.0f);
    EXPECT_EQ(asteroid.cel, nullptr);
    
    // Assignment checks
    asteroid.number = 1;
    asteroid.name = "Ceres";
    asteroid.diam = 939.4f;
    asteroid.sma = 2.769f;
    asteroid.incl = 10.59f;
    
    Planet ceres_planet;
    asteroid.cel = &ceres_planet;
    
    EXPECT_EQ(asteroid.number, 1);
    EXPECT_EQ(asteroid.name, "Ceres");
    EXPECT_FLOAT_EQ(asteroid.diam, 939.4f);
    EXPECT_FLOAT_EQ(asteroid.sma, 2.769f);
    EXPECT_FLOAT_EQ(asteroid.incl, 10.59f);
    EXPECT_EQ(asteroid.cel, &ceres_planet);
}// =====================================================================
// AtmosphereComposition Math & Integrity Tests
// =====================================================================

TEST(AtmosphereCompositionTest, EnforceIntegrityNormalizesExcess)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Set an impossible atmosphere summing to 2.0
    comp.O2_portion = 1.5;
    comp.N2_portion = 0.5;

    comp.enforce_integrity();

    // Should scale down by 1/2.0 (multiplier = 0.5)
    EXPECT_DOUBLE_EQ(comp.O2_portion, 0.75);
    EXPECT_DOUBLE_EQ(comp.N2_portion, 0.25);
    EXPECT_DOUBLE_EQ(comp.H2_portion, 0.0);
}

TEST(AtmosphereCompositionTest, EnforceIntegrityAllowsPartialAtmospheres)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Set a partial atmosphere summing to 0.5
    comp.O2_portion = 0.2;
    comp.N2_portion = 0.3;

    comp.enforce_integrity();

    // Should remain entirely untouched since total <= 1.0
    EXPECT_DOUBLE_EQ(comp.O2_portion, 0.2);
    EXPECT_DOUBLE_EQ(comp.N2_portion, 0.3);
}

TEST(AtmosphereCompositionTest, MeanMolarMassCalculation)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Case 1: Empty atmosphere falls back to Earth air constant
    EXPECT_DOUBLE_EQ(comp.mean_molar_mass(), 0.0289644);

    // Case 2: Pure Oxygen atmosphere
    comp.O2_portion = 1.0;
    EXPECT_DOUBLE_EQ(comp.mean_molar_mass(), 0.031998);

    // Case 3: Mixed atmosphere (50% H2, 50% He)
    // Note: It normalizes based on the total present in the calculation
    comp.O2_portion = 0.0;
    comp.H2_portion = 0.5;
    comp.He_portion = 0.5;
    
    // Expected: (0.5 * 0.002016 + 0.5 * 0.004003) / 1.0 = 0.0030095
    EXPECT_NEAR(comp.mean_molar_mass(), 0.0030095, 1e-7);
}

// =====================================================================
// AtmosphereComposition Generator Tests
// =====================================================================

TEST(AtmosphereCompositionTest, GeneratorsProduceValidIntegrity)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Test Gas Giant Generation
    comp.generate_fictitious_gas_giant();
    
    // Sum all portions to verify leftover calculation and integrity enforcement
    double total_gas = comp.H2_portion + comp.He_portion + comp.N2_portion + 
                       comp.O2_portion + comp.O3_portion + comp.CO2_portion + 
                       comp.CH4_portion + comp.SO2_portion + comp.H2O_portion + 
                       comp.H2S_portion + comp.HCN_portion + comp.NH3_portion + 
                       comp.C2H6_portion + comp.N2O_portion + comp.CO_portion + 
                       comp.Ar_portion;
                       
    // Should be extremely close to 1.0
    EXPECT_NEAR(total_gas, 1.0, 1e-5);
    
    // Gas giants should have Hydrogen as the leftover (dominant) gas
    EXPECT_GT(comp.H2_portion, 0.5);

    // Test Fictitious Routing (Venusian / Rocky)
    AtmosphereComposition comp_rocky(&dummy);
    comp_rocky.generate_fictitious_for_planet(rocky);
    
    // Rocky routing delegates to Venusian, where CO2 is the leftover (dominant)
    EXPECT_GT(comp_rocky.CO2_portion, 0.5);
}

TEST(AtmosphereCompositionTest, HabitableGenerationLogic)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);
    
    comp.generate_fictitious_habitable();
    
    // Nitrogen is the leftover gas for habitable worlds
    EXPECT_GT(comp.N2_portion, 0.0);
    
    // Enforce integrity should have capped the total at 1.0
    double total_habitable = comp.H2_portion + comp.He_portion + comp.N2_portion + 
                             comp.O2_portion + comp.O3_portion + comp.CO2_portion + 
                             comp.CH4_portion + comp.SO2_portion + comp.H2O_portion + 
                             comp.H2S_portion + comp.HCN_portion + comp.NH3_portion + 
                             comp.C2H6_portion + comp.N2O_portion + comp.CO_portion + 
                             comp.Ar_portion;
                             
    EXPECT_NEAR(total_habitable, 1.0, 1e-5);
}
// =====================================================================
// Photometry
// =====================================================================

// Every planet in the sky is drawn at a brightness these produce, and the numbers themselves are
// empirical fits that nobody can check by eye. What can be checked is their shape: brightest at
// opposition, falling away either side of it, symmetric between waxing and waning, and never
// negative or greater than one.

TEST(PlanetPhotometryTest, PhaseBrightnessPeaksAtOpposition)
{
    Planet p;
    p.type = rocky;
    p.albedo = 0.3;

    double full = p.phase_brightness(0);
    EXPECT_NEAR(full, 1.0, 1e-9) << "at opposition the whole lit disc faces us";

    // Falling away from opposition, all the way to new.
    double previous = full;
    for (double alpha = 0.1; alpha <= _pi; alpha += 0.1)
    {
        double here = p.phase_brightness(alpha);
        EXPECT_LE(here, previous + 1e-12) << " at phase angle " << alpha;
        EXPECT_GE(here, 0) << " at phase angle " << alpha;
        EXPECT_LE(here, 1.0 + 1e-12) << " at phase angle " << alpha;
        previous = here;
    }

    // A new planet shows us nothing at all, or as near as makes no difference.
    EXPECT_LT(p.phase_brightness(_pi), 0.01);
}

TEST(PlanetPhotometryTest, PhaseBrightnessIsSymmetricAndWraps)
{
    Planet p;
    p.type = rocky;
    p.albedo = 0.3;

    // Waxing and waning at the same width are the same brightness.
    for (double alpha = 0.2; alpha < _pi; alpha += 0.4)
        EXPECT_NEAR(p.phase_brightness(alpha), p.phase_brightness(-alpha), 1e-12)
            << " at phase angle " << alpha;

    // And a phase angle that has gone round the whole way is the same as one that has not.
    EXPECT_NEAR(p.phase_brightness(0.7), p.phase_brightness(0.7 + _pi*2), 1e-12);
    EXPECT_NEAR(p.phase_brightness(0.7), p.phase_brightness(0.7 - _pi*2), 1e-12);
}

TEST(PlanetPhotometryTest, CloudierWorldsFadeMoreGently)
{
    // A bare regolith world dims sharply off opposition -- the shadows between its grains open
    // up -- while a cloud deck scatters much more like a smooth ball and holds its brightness.
    Planet bare;
    bare.type = rocky;
    bare.albedo = 0.15;

    Planet shrouded;
    shrouded.type = gas_giant;
    shrouded.albedo = 0.5;

    EXPECT_LE(bare.cloud_deck_fraction(), shrouded.cloud_deck_fraction());

    double half_phase = _pi/2;
    EXPECT_LE(bare.phase_brightness(half_phase), shrouded.phase_brightness(half_phase));

    // The slope parameter is the IAU G, which lives between 0 and 1.
    EXPECT_GE(bare.phase_slope_parameter(), 0.0);
    EXPECT_LE(bare.phase_slope_parameter(), 1.0);
    EXPECT_GE(shrouded.cloud_deck_fraction(), 0.0);
    EXPECT_LE(shrouded.cloud_deck_fraction(), 1.0);
}

// =====================================================================
// The habitable zone
// =====================================================================

class PlanetHabitabilityTest : public UniverseFixture {};

TEST_F(PlanetHabitabilityTest, EarthIsInTheZoneAndItsNeighboursAreNot)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    EXPECT_TRUE(earth->is_in_con_HZ()) << "one AU from a G2V star is the definition of the zone";

    Planet* mercury = make_planet(sun, "Too Close", 0.387 * AU);
    EXPECT_FALSE(mercury->is_in_con_HZ());

    Planet* jupiter = make_planet(sun, "Too Far", 5.2 * AU);
    EXPECT_FALSE(jupiter->is_in_con_HZ());

    // A planet in orbit around nothing has no zone to be in, rather than an answer by accident.
    Planet rogue;
    rogue.mass = earth_mass;
    EXPECT_FALSE(rogue.is_in_con_HZ());

    delete_the_universe();
}

TEST_F(PlanetHabitabilityTest, TheZoneFollowsTheStar)
{
    // A cooler star's zone is closer in. Rather than assert where it is -- which is a claim about
    // the Kopparapu coefficients, not about this code -- walk a planet outwards from each star
    // and find the band, then compare the two bands.
    auto find_zone = [this](Star* s, double& inner, double& outer)
    {
        Planet* wanderer = make_planet(s, "Wanderer", AU);
        inner = outer = 0;
        for (double au = 0.02; au < 10.0; au *= 1.05)
        {
            wanderer->orbit->semimajor_axis = au * AU;
            if (wanderer->is_in_con_HZ())
            {
                if (!inner) inner = au;
                outer = au;
            }
        }
    };

    Star* sun = make_star("Sol");
    double sun_inner = 0, sun_outer = 0;
    find_zone(sun, sun_inner, sun_outer);

    // The conservative zone's inner edge sits almost exactly at the Earth's orbit in this model --
    // the runaway greenhouse limit is about 0.99 AU -- so the band is checked to a step of the
    // scan rather than to a sharp figure. That the Earth itself is inside it is asserted above,
    // at exactly one AU, where it can be checked without any sampling at all.
    ASSERT_GT(sun_inner, 0) << "a G2V star has a habitable zone";
    EXPECT_LT(sun_inner, 1.1);
    EXPECT_GT(sun_outer, 1.1) << "and it reaches out past the Earth";

    Star* cool = make_star("Red Dwarf");
    cool->temperature = 3200;
    cool->absolute_magnitude = 11.0;
    strcpy(cool->spectral_type, "M2V");
    double cool_inner = 0, cool_outer = 0;
    find_zone(cool, cool_inner, cool_outer);

    ASSERT_GT(cool_inner, 0) << "a red dwarf has one too, much closer in";
    EXPECT_LT(cool_outer, sun_inner) << "the whole of it is inside the Sun's inner edge";

    delete_the_universe();
}

// =====================================================================
// Deriving what a catalog did not state
// =====================================================================

TEST(PlanetEstimationTest, RadiusFromMass)
{
    // An exoplanet is often massed and not measured. One Earth mass should come back at about one
    // Earth radius, and a heavier world should be larger -- but not proportionally so, since rock
    // compresses under its own weight.
    Planet earth;
    earth.mass = earth_mass;
    earth.type = rocky;
    earth.estimate_radius();
    EXPECT_NEAR(earth.volumetric_mean_radius, earth_radius, earth_radius * 0.25);

    Planet heavy;
    heavy.mass = 5 * earth_mass;
    heavy.type = rocky;
    heavy.estimate_radius();
    EXPECT_GT(heavy.volumetric_mean_radius, earth.volumetric_mean_radius);
    EXPECT_LT(heavy.volumetric_mean_radius, 5 * earth.volumetric_mean_radius);

    // It is a derivation and not a fallback: it overwrites whatever radius the body had, which is
    // why the callers check first whether they have a measured one. Worth pinning, since the name
    // reads like it might defer to a value already there.
    Planet measured;
    measured.mass = earth_mass;
    measured.type = rocky;
    measured.volumetric_mean_radius = 1234567;
    measured.estimate_radius();
    EXPECT_NE(measured.volumetric_mean_radius, 1234567);
    EXPECT_NEAR(measured.volumetric_mean_radius, earth.volumetric_mean_radius, 1);

    // A mass that is not a number cannot give a radius that is; rather than pass the poison on to
    // everything that divides by a radius, it settles for Earth's.
    Planet nonsense;
    nonsense.mass = INFINITY;
    nonsense.type = gas_giant;
    nonsense.estimate_radius();
    EXPECT_TRUE(std::isfinite(nonsense.volumetric_mean_radius));
}

class PlanetAtmosphereTest : public UniverseFixture {};

TEST_F(PlanetAtmosphereTest, ScaleHeightRequiresAnAtmosphere)
{
    Star* sun = make_star("Sol");

    // No atmosphere at all: nothing to have a height, whatever the star is doing.
    Planet* airless = make_planet(sun, "Airless", AU);
    EXPECT_DOUBLE_EQ(airless->estimate_scale_height(), 0);

    // An atmosphere of no pressure is the same answer by a different route -- the object exists,
    // but there is nothing in it.
    Planet* vacuum = make_planet(sun, "Vacuum", AU);
    vacuum->ensure_atmosphere()->surface_pressure = 0;
    EXPECT_DOUBLE_EQ(vacuum->estimate_scale_height(), 0);

    // An Earth-sized world an AU out, with an atmosphere: a few kilometres, as ours is 8.5.
    Planet* warm = make_planet(sun, "Warm", AU);
    warm->ensure_atmosphere()->surface_pressure = oneatm;
    double earthlike = warm->estimate_scale_height();
    EXPECT_GT(earthlike, 0);
    EXPECT_GT(earthlike, 1000) << "a scale height of less than a kilometre is not an atmosphere";
    EXPECT_LT(earthlike, 100000);

    // Moved in towards the star, the same world is hotter, and a hotter atmosphere is puffier.
    warm->orbit->semimajor_axis = 0.5 * AU;
    double closer_in = warm->estimate_scale_height();
    EXPECT_GT(closer_in, earthlike) << "a hotter atmosphere is puffier";

    // Back out, and made heavier at the same radius: stronger gravity holds it down.
    warm->orbit->semimajor_axis = AU;
    EXPECT_NEAR(warm->estimate_scale_height(), earthlike, earthlike * 1e-9)
        << "and it comes back to where it was when the distance does";
    warm->mass = 5 * earth_mass;
    EXPECT_LT(warm->estimate_scale_height(), earthlike) << "stronger gravity holds it down";

    delete_the_universe();
}

// estimate_habitability() and classify() both used to reach for ensure_atmosphere() just to read
// from it, which default-constructs surface_pressure at one bar -- so any Solar System moon, or
// Mercury, or an asteroid, picked up a full Earth atmosphere the instant something merely asked
// "could this support life" or "what kind of body is this," which happens on every texture draw
// (generate_rocky_map() calls estimate_habitability()) and every catalog load (classify()). Real
// bodies must only get an atmosphere from catalogs/planets.json, the edit dialog, or (for
// exoplanets and Shift+A-created bodies) an explicit apply_cosmic_shoreline() call -- never as a
// side effect of being asked about.
TEST_F(PlanetAtmosphereTest, QueryingADryMoonDoesNotGiveItAnAtmosphere)
{
    Star* sun = make_star("Sol");                 // seqno 0 in a fresh universe, same as the real Sun
    Planet* earthlike_planet = make_planet(sun, "Earth", AU);

    // A body orbiting that planet, standing in for a real Moon: make_planet() takes only a Star*,
    // so this is wired up by hand the same way it does internally.
    Planet* moon = new Planet();
    strcpy(moon->name, "Moon");
    moon->namelen = 0;
    moon->mass = lunar_mass;
    moon->volumetric_mean_radius = 1737400;
    moon->orbit = new Orbit();
    moon->orbit->center = earthlike_planet;
    moon->orbit->center_name = earthlike_planet->name;
    moon->orbit->semimajor_axis = 384400000;
    moon->orbit->compute_period(moon->mass);
    moon->cenobj = earthlike_planet;
    append_cel(moon);

    ASSERT_EQ(moon->atm, nullptr) << "sanity check: starts genuinely airless, like the real Moon";

    moon->estimate_habitability();
    EXPECT_EQ(moon->atm, nullptr) << "querying habitability must not manufacture an atmosphere";

    moon->classify();
    EXPECT_EQ(moon->atm, nullptr) << "classify() must not manufacture an atmosphere either";

    EXPECT_DOUBLE_EQ(moon->get_surface_pressure(), 0.0);
    EXPECT_DOUBLE_EQ(moon->estimate_scale_height(), 0.0);

    delete_the_universe();
}

// The flip side: a genuine exoplanet host (anything but the Sun itself) is exactly who
// apply_cosmic_shoreline() is for, and it has to still work after classify() stopped doing its own
// separate copy of this.
TEST_F(PlanetAtmosphereTest, ApplyCosmicShorelineStillWorksForARealExoplanetHost)
{
    make_star("Not The Sun");                      // burns seqno 0 so the host below isn't mistaken for it
    Star* host = make_star("Hot Star");
    host->temperature = 6500;
    Planet* hot_jupiter = make_planet(host, "Scorcher", 0.02 * AU);
    hot_jupiter->mass = jupiter_mass;
    hot_jupiter->volumetric_mean_radius = jupiter_radius;

    ASSERT_EQ(hot_jupiter->atm, nullptr);
    hot_jupiter->apply_cosmic_shoreline();
    EXPECT_NE(hot_jupiter->atm, nullptr)
        << "a real exoplanet host must still be able to give its planet an atmosphere";

    delete_the_universe();
}

// get_atmospheric_tau() used to treat "computed tau of zero" as a proxy for "no composition data,"
// and would invent a random fictitious atmosphere to replace it. But a real, fully-specified
// atmosphere that's mostly inert gas -- Earth's N2/O2/Ar, none of which are greenhouse gases this
// model tracks -- legitimately computes to zero tau too. That's a real answer, not a missing one,
// and it must not get overwritten.
TEST_F(PlanetAtmosphereTest, RealCompositionWithZeroTauIsTrusted)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    Atmosphere* eatm = earth->ensure_atmosphere();
    eatm->surface_pressure = oneatm;
    AtmosphereComposition* ecomp = eatm->ensure_composition();
    ecomp->N2_portion = 0.78;
    ecomp->O2_portion = 0.21;
    ecomp->Ar_portion = 0.01;

    EXPECT_DOUBLE_EQ(earth->get_atmospheric_tau(), 0.0);

    // The composition object itself, and the real gas fractions on it, must survive untouched --
    // not be silently replaced by ensure_composition()->generate_fictitious_for_planet().
    EXPECT_EQ(eatm->comp, ecomp);
    EXPECT_DOUBLE_EQ(ecomp->N2_portion, 0.78);
    EXPECT_DOUBLE_EQ(ecomp->O2_portion, 0.21);
    EXPECT_DOUBLE_EQ(ecomp->Ar_portion, 0.01);
    EXPECT_DOUBLE_EQ(ecomp->CO2_portion, 0.0) << "no fictitious greenhouse gas should have been invented";

    // A planet with pressure but genuinely no composition at all is the case this fallback exists
    // for, and should still get one invented for it.
    Planet* sketched = make_planet(sun, "Sketched", AU);
    sketched->ensure_atmosphere()->surface_pressure = oneatm;
    EXPECT_EQ(sketched->atm->comp, nullptr);
    sketched->get_atmospheric_tau();
    EXPECT_NE(sketched->atm->comp, nullptr) << "a planet with no composition data at all should still get one made up";

    delete_the_universe();
}

// Real bodies, real mass/radius/orbit/pressure/composition, checked against the estimate_
// scale_height() formula (H = RT/(Mg), g converted from the Earth-Gs estimate_surface_gravity()
// returns to m/s2 -- see the fix in Planet::estimate_scale_height()). Bounds are deliberately
// loose: equilibrium_temperature() runs through a fitted Bond-albedo model and an Eddington-gray
// greenhouse approximation, neither of which promises to land on the textbook surface temperature
// (real scale heights: Earth ~8.5 km, Venus ~15.9 km, Mars ~11.1 km, Jupiter ~27 km). What these
// bounds do pin down is the order of magnitude: a units bug like the one this guards against (the
// pre-fix code was off by the 9.80665 m/s2 -> Earth-Gs conversion) inflates every one of these by
// roughly 9.8x, which blows straight through the upper bound on all four.
TEST_F(PlanetAtmosphereTest, ScaleHeightIsSaneForRealPlanets)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    earth->albedo = 0.306;
    Atmosphere* eatm = earth->ensure_atmosphere();
    eatm->surface_pressure = oneatm;
    AtmosphereComposition* ecomp = eatm->ensure_composition();
    ecomp->N2_portion = 0.78;
    ecomp->O2_portion = 0.21;
    ecomp->Ar_portion = 0.01;

    Planet* venus = make_planet(sun, "Venus", 0.723 * AU);
    venus->mass = 0.815 * earth_mass;
    venus->volumetric_mean_radius = 0.9499 * earth_radius;
    venus->albedo = 0.75;
    Atmosphere* vatm = venus->ensure_atmosphere();
    vatm->surface_pressure = 92 * oneatm;
    AtmosphereComposition* vcomp = vatm->ensure_composition();
    vcomp->CO2_portion = 0.965;
    vcomp->N2_portion = 0.035;

    Planet* mars = make_planet(sun, "Mars", 1.524 * AU);
    mars->mass = 0.1074 * earth_mass;
    mars->volumetric_mean_radius = 0.532 * earth_radius;
    mars->albedo = 0.25;
    Atmosphere* matm = mars->ensure_atmosphere();
    matm->surface_pressure = 610;
    AtmosphereComposition* mcomp = matm->ensure_composition();
    mcomp->CO2_portion = 0.95;
    mcomp->N2_portion = 0.027;
    mcomp->Ar_portion = 0.019;
    mcomp->O2_portion = 0.0013;

    Planet* jupiter = make_planet(sun, "Jupiter", 5.2 * AU);
    jupiter->mass = jupiter_mass;
    jupiter->volumetric_mean_radius = jupiter_radius;
    jupiter->albedo = 0.343;
    Atmosphere* jatm = jupiter->ensure_atmosphere();
    jatm->surface_pressure = oneatm;
    AtmosphereComposition* jcomp = jatm->ensure_composition();
    jcomp->H2_portion = 0.864;
    jcomp->He_portion = 0.136;

    struct Expectation { Planet* p; double min_m; double max_m; };
    Expectation expectations[] =
    {
        { earth,   2000, 30000 },
        { venus,   1000, 30000 },   // at the cloud deck (~1 bar), not Venus's 92-atm ground -- cooler, thinner
        { mars,    2000, 30000 },
        { jupiter, 2000, 60000 },
    };

    for (auto& e : expectations)
    {
        double H = e.p->estimate_scale_height();
        EXPECT_GT(H, e.min_m) << e.p->name << ": scale height implausibly thin";
        EXPECT_LT(H, e.max_m) << e.p->name << ": scale height implausibly puffy -- check for a units regression";

        // The cap in estimate_scale_height() should never be the thing holding a normal planet
        // down: a real world's scale height is a small fraction of its own radius.
        EXPECT_LT(H, e.p->volumetric_mean_radius * 0.05)
            << e.p->name << ": within the cap, but by coincidence rather than because it's tiny by nature";
    }

    delete_the_universe();
}

// Uranus and Neptune have no ground to give get_surface_pressure() a value, and it is often left
// at the default zero for exactly that reason. That must not read as "no atmosphere": both are
// gas/ice giants, so cloud_deck_fraction() is 1 regardless of what surface_pressure holds, and
// estimate_scale_height() should fall back to the same 1-bar cloud-top reference a gas giant with
// a populated pressure field gets, rather than returning 0 and rendering no haze at all.
// End to end, through equilibrium_temperature() and the Eddington-gray greenhouse: the three
// terrestrial atmospheres this app actually carries, ordered the way the real ones are. Venus is
// the case the CO2-CO2 collisional term in atmospheric_tau() exists for -- without it its 92 bar
// of CO2 only reached tau 5, and the surface came out near 430 K instead of somewhere in the 700s.
//
// The bounds are wide on purpose, and Venus is not pinned to its textbook 737 K. Two approximations
// stand between tau and the number here: estimate_bond_albedo() runs the geometric albedo through
// a fitted model that returns about 0.73 for Venus where the measured Bond albedo is 0.76 (worth
// tens of K on its own, and note that planets.json carries the measured value but cat.cpp never
// loads it), and a single-band gray atmosphere is a crude stand-in for real radiative transfer.
// What these bounds do pin down is that Venus lands in the right regime rather than hundreds of
// K below it, and that Earth and Mars did not move when it got there.
TEST_F(PlanetAtmosphereTest, DenseCO2GreenhouseIsSaneForRealPlanets)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    earth->albedo = 0.367;
    Atmosphere* eatm = earth->ensure_atmosphere();
    eatm->surface_pressure = oneatm;
    AtmosphereComposition* ecomp = eatm->ensure_composition();
    ecomp->N2_portion = 0.78;
    ecomp->O2_portion = 0.21;
    ecomp->Ar_portion = 0.0093;
    ecomp->CO2_portion = 0.00042;
    ecomp->H2O_portion = 0.0025;
    ecomp->CH4_portion = 1.9e-6;

    // Straight out of planets.json: 9.3e6 Pa, 96.5% CO2 with the trace SO2, H2O and CO.
    Planet* venus = make_planet(sun, "Venus", 0.723 * AU);
    venus->mass = 0.815 * earth_mass;
    venus->volumetric_mean_radius = 0.9499 * earth_radius;
    venus->albedo = 0.689;
    Atmosphere* vatm = venus->ensure_atmosphere();
    vatm->surface_pressure = 9.3e6;
    AtmosphereComposition* vcomp = vatm->ensure_composition();
    vcomp->CO2_portion = 0.965;
    vcomp->N2_portion = 0.035;
    vcomp->SO2_portion = 1.5e-4;
    vcomp->H2O_portion = 2e-5;
    vcomp->CO_portion = 1.7e-5;

    Planet* mars = make_planet(sun, "Mars", 1.524 * AU);
    mars->mass = 0.1074 * earth_mass;
    mars->volumetric_mean_radius = 0.5320 * earth_radius;
    mars->albedo = 0.17;
    Atmosphere* matm = mars->ensure_atmosphere();
    matm->surface_pressure = 0.00636 * oneatm;
    AtmosphereComposition* mcomp = matm->ensure_composition();
    mcomp->CO2_portion = 0.9532;
    mcomp->N2_portion = 0.027;
    mcomp->Ar_portion = 0.016;

    double t_earth = earth->estimate_surface_temperature();
    double t_venus = venus->estimate_surface_temperature();
    double t_mars  = mars->estimate_surface_temperature();

    // Venus: hot enough to melt lead, which is the whole point of the collisional term.
    EXPECT_GT(t_venus, 650.0) << "the dense-CO2 greenhouse has collapsed back to a band-only model";
    EXPECT_LT(t_venus, 900.0) << "the collisional coefficient has run away";
    EXPECT_GT(venus->get_atmospheric_tau(), 55.0);

    // Earth and Mars sit far below the pressure where collisional absorption bites, so both keep
    // the modest greenhouse they had before: a few tens of K above equilibrium, and no more.
    EXPECT_NEAR(t_earth, 277.0, 25.0);
    EXPECT_NEAR(t_mars,  245.0, 25.0);
    EXPECT_LT(earth->get_atmospheric_tau(), 1.0);
    EXPECT_LT(mars->get_atmospheric_tau(),  1.0);

    // Each still warmer than it would be bare, and in the right order relative to each other.
    EXPECT_GT(t_earth, earth->equilibrium_temperature());
    EXPECT_GT(t_mars,  mars->equilibrium_temperature());
    EXPECT_GT(t_venus, t_earth);
    EXPECT_GT(t_earth, t_mars);

    delete_the_universe();
}

TEST_F(PlanetAtmosphereTest, IceGiantWithNoSurfacePressureStillGetsAHaze)
{
    Star* sun = make_star("Sol");

    Planet* neptune = make_planet(sun, "Neptune", 30.1 * AU);
    neptune->type = ice_giant;
    neptune->lock_type = true;
    neptune->mass = 17.15 * earth_mass;
    neptune->volumetric_mean_radius = 3.865 * earth_radius;
    neptune->albedo = 0.29;
    Atmosphere* natm = neptune->ensure_atmosphere();
    natm->surface_pressure = 0;                          // exactly the gap this test guards against
    AtmosphereComposition* ncomp = natm->ensure_composition();
    ncomp->H2_portion = 0.80;
    ncomp->He_portion = 0.19;
    ncomp->CH4_portion = 0.01;

    EXPECT_DOUBLE_EQ(neptune->get_surface_pressure(), 0.0) << "sanity check on the test's own setup";
    EXPECT_DOUBLE_EQ(neptune->cloud_deck_fraction(), 1.0);

    double H = neptune->estimate_scale_height();
    EXPECT_GT(H, 0) << "a gas/ice giant must get a haze from the cloud-top reference pressure "
        "even when surface_pressure was never given a value";
    EXPECT_LT(H, neptune->volumetric_mean_radius * 0.05 * 1.0000001);

    delete_the_universe();
}

// A world with no measured composition or temperature -- exactly the shape of a directly-imaged,
// wide-orbit exoplanet like COCONUTS-2 b, whose orbital archive gives a period and semimajor axis
// but nothing about its atmosphere -- can still send T or g to extremes through the estimation
// pipeline (a bad greenhouse tau, an underestimated gravity). Whatever the upstream cause, the
// rendered glow must never be allowed to swallow the planet: confirm the absolute cap holds even
// when the raw RT/(Mg) value would clearly blow past it.
TEST_F(PlanetAtmosphereTest, ScaleHeightCapHoldsForAPathologicalPlanet)
{
    Star* sun = make_star("Sol");

    // Cranked far hotter than any real greenhouse effect would produce, on a low-gravity world:
    // T high and g low both push H up, so this is a stress case, not a realistic one.
    Planet* monster = make_planet(sun, "Monster", 0.05 * AU);
    monster->mass = 0.01 * earth_mass;
    monster->volumetric_mean_radius = 5 * earth_radius;
    monster->albedo = 0.0;
    Atmosphere* matm = monster->ensure_atmosphere();
    matm->surface_pressure = oneatm;
    AtmosphereComposition* mcomp = matm->ensure_composition();
    mcomp->H2_portion = 1.0;

    double H = monster->estimate_scale_height();
    EXPECT_GT(H, 0);
    EXPECT_LE(H, monster->volumetric_mean_radius * 0.05 * 1.0000001)
        << "the cap must hold regardless of how extreme the upstream temperature/gravity estimate gets";

    delete_the_universe();
}

TEST(PlanetEstimationTest, SurfaceGravityAndDensityAgree)
{
    // Two derivations of the same body from the same two numbers; they have to be consistent
    // with each other, and with the Earth, which is where both are calibrated.
    Planet earth;
    earth.mass = earth_mass;
    earth.volumetric_mean_radius = earth_radius;

    EXPECT_NEAR(earth.estimate_surface_gravity(), 1.0, 1e-9);
    EXPECT_NEAR(earth.density(), 5.51, 0.05);

    // Same density, bigger world: gravity goes up with the radius.
    Planet bigger;
    bigger.volumetric_mean_radius = 2 * earth_radius;
    bigger.mass = earth_mass * 8;                       // eight times the volume, same density
    EXPECT_NEAR(bigger.density(), earth.density(), 1e-9);
    EXPECT_NEAR(bigger.estimate_surface_gravity(), 2.0, 1e-9);
}

// =====================================================================
// Exoplanet Inclination Reconciliation Tests
// =====================================================================

TEST(ExoplanetInclinationTest, GJ876_PreservesSuppliedInclinations)
{
    Star host;
    snprintf(host.name, sizeof(host.name), "GJ 876");

    Planet pb, pc, pd, pe;
    snprintf(pd.name, sizeof(pd.name), "GJ 876 d");
    snprintf(pc.name, sizeof(pc.name), "GJ 876 c");
    snprintf(pb.name, sizeof(pb.name), "GJ 876 b");
    snprintf(pe.name, sizeof(pe.name), "GJ 876 e");

    pd.orbit = new Orbit();
    pc.orbit = new Orbit();
    pb.orbit = new Orbit();
    pe.orbit = new Orbit();

    pd.orbit->period = 1.9378 * oneday;
    pc.orbit->period = 30.088 * oneday;
    pb.orbit->period = 61.116 * oneday;
    pe.orbit->period = 124.69 * oneday;

    pd.mass = 6.8 * earth_mass;
    pc.mass = 227.0 * earth_mass;
    pb.mass = 730.0 * earth_mass;
    pe.mass = 15.0 * earth_mass;

    pd.type = rocky;
    pc.type = gas_giant;
    pb.type = gas_giant;
    pe.type = gas_giant;

    std::vector<Planet*> planets = { &pd, &pc, &pb, &pe };
    // b has 101.0 deg from Gaia DR3, while d, c, e have 59.0 deg from dynamical fits
    std::vector<double> pincls = 
    {
        59.0 * fiftyseventh,
        59.0 * fiftyseventh,
        101.0 * fiftyseventh,
        59.0 * fiftyseventh
    };
    std::vector<double> pnodes = { 0, 0, 0, 0 };

    CatalogReader::reconcile_exoplanet_inclinations(&host, planets, pincls, pnodes);

    const double expected[] = {59, 59, 101, 59};
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR(pincls[i] * fiftyseven, expected[i], 1e-8);
    }
}

TEST(ExoplanetInclinationTest, HD3167_PreservesSuppliedRVInclinations)
{
    Star host;
    snprintf(host.name, sizeof(host.name), "HD 3167");

    Planet pb, pd, pc, pe;
    snprintf(pb.name, sizeof(pb.name), "HD 3167 b");
    snprintf(pd.name, sizeof(pd.name), "HD 3167 d");
    snprintf(pc.name, sizeof(pc.name), "HD 3167 c");
    snprintf(pe.name, sizeof(pe.name), "HD 3167 e");

    pb.orbit = new Orbit();
    pd.orbit = new Orbit();
    pc.orbit = new Orbit();
    pe.orbit = new Orbit();

    pb.orbit->period = 0.9596 * oneday;
    pd.orbit->period = 8.509 * oneday;
    pc.orbit->period = 29.845 * oneday;
    pe.orbit->period = 102.09 * oneday;

    double mass_d_orig = 6.9 * earth_mass;
    double mass_e_orig = 8.5 * earth_mass;

    pb.mass = 5.0 * earth_mass;
    pd.mass = mass_d_orig;
    pc.mass = 9.8 * earth_mass;
    pe.mass = mass_e_orig;
    pd.msini = mass_d_orig * sin(39.0 * fiftyseventh);
    pe.msini = mass_e_orig * sin(41.0 * fiftyseventh);

    pb.type = rocky;
    pd.type = rocky;
    pc.type = ice_giant;
    pe.type = ice_giant;

    std::vector<Planet*> planets = { &pb, &pd, &pc, &pe };
    // b & c are transiting (83.4, 89.5 deg), d & e are unconstrained RV posteriors (39.0, 41.0 deg)
    std::vector<double> pincls =
    {
        83.4 * fiftyseventh,
        39.0 * fiftyseventh,
        89.5 * fiftyseventh,
        41.0 * fiftyseventh
    };
    std::vector<double> pnodes = { 0, 0, 0, 0 };

    CatalogReader::reconcile_exoplanet_inclinations(&host, planets, pincls, pnodes);

    EXPECT_NEAR(pincls[1] * fiftyseven, 39, 1e-8);
    EXPECT_NEAR(pincls[3] * fiftyseven, 41, 1e-8);
    EXPECT_DOUBLE_EQ(pd.mass, mass_d_orig);
    EXPECT_DOUBLE_EQ(pe.mass, mass_e_orig);
}

TEST(ExoplanetInclinationTest, PreservesSuppliedSupplementaryPlanes)
{
    Star host;
    snprintf(host.name, sizeof(host.name), "PDS 70");

    Planet pb, pc, pe;
    snprintf(pb.name, sizeof(pb.name), "PDS 70 b");
    snprintf(pc.name, sizeof(pc.name), "PDS 70 c");
    snprintf(pe.name, sizeof(pe.name), "PDS 70 e");

    pb.orbit = new Orbit();
    pc.orbit = new Orbit();
    pe.orbit = new Orbit();

    pb.orbit->period = 43800.0 * oneday;
    pc.orbit->period = 80300.0 * oneday;
    pe.orbit->period = 120000.0 * oneday;

    pb.mass = 3.0 * jupiter_mass;
    pc.mass = 2.0 * jupiter_mass;
    pe.mass = 1.0 * jupiter_mass;

    pb.type = gas_giant;
    pc.type = gas_giant;
    pe.type = gas_giant;

    std::vector<Planet*> planets = { &pb, &pc, &pe };
    // b & c published as ~131 deg, e published as 49.5 deg (180 - 130.5 deg)
    std::vector<double> pincls =
    {
        131.0 * fiftyseventh,
        130.5 * fiftyseventh,
        49.5 * fiftyseventh
    };
    std::vector<double> pnodes =
    {
        30.0 * fiftyseventh,
        30.0 * fiftyseventh,
        30.0 * fiftyseventh
    };

    CatalogReader::reconcile_exoplanet_inclinations(&host, planets, pincls, pnodes);

    const double expected[] = {131, 130.5, 49.5};
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_NEAR(pincls[i] * fiftyseven, expected[i], 1e-8);
        EXPECT_NEAR(pnodes[i] * fiftyseven, 30, 1e-8);
    }
}

TEST(ExoplanetInclinationTest, PreservesHotJupiterExemption)
{
    Star host;
    snprintf(host.name, sizeof(host.name), "WASP-107");

    Planet pb, pc;
    snprintf(pb.name, sizeof(pb.name), "WASP-107 b");
    snprintf(pc.name, sizeof(pc.name), "WASP-107 c");

    pb.orbit = new Orbit();
    pc.orbit = new Orbit();

    pb.orbit->period = 5.72 * oneday;
    pc.orbit->period = 1088.0 * oneday;

    pb.mass = 0.12 * jupiter_mass;
    pb.volumetric_mean_radius = 0.94 * jupiter_radius;
    pb.inclination_error_upper = 30 * fiftyseventh;
    pb.inclination_error_lower = -30 * fiftyseventh;
    pc.mass = 0.36 * jupiter_mass;
    pc.inclination_error_upper = 0.5 * fiftyseventh;
    pc.inclination_error_lower = -0.5 * fiftyseventh;

    pb.type = hot_jupiter;
    pc.type = gas_giant;

    std::vector<Planet*> planets = { &pb, &pc };
    std::vector<double> pincls =
    {
        89.5 * fiftyseventh,
        45.0 * fiftyseventh
    };
    std::vector<double> pnodes = { 0, 0 };

    CatalogReader::reconcile_exoplanet_inclinations(&host, planets, pincls, pnodes);

    // The hot jupiter b must preserve its distinct orbital inclination
    EXPECT_NEAR(pincls[0] * fiftyseven, 89.5, 0.1);
}

TEST(ExoplanetInclinationTest, TwoDegreeCutoffDoesNotDependOnReferencePlaneOrPlanetType)
{
    for (double reference : {10.0, 90.0, 170.0})
    {
        for (bool hot : {false, true})
        {
            Star host;
            host.has_disk = true;
            host.disk_heliocen_inclination = reference * fiftyseventh;
            Planet low, boundary, polar, retrograde;
            polar.transiting = true;
            low.type = hot ? hot_jupiter : rocky;
            std::vector<Planet*> planets = {&low, &boundary, &polar, &retrograde};
            std::vector<double> inclinations = {0.4 * fiftyseventh, 2 * fiftyseventh, half_pi, _pi};
            std::vector<double> nodes = {0, 0.2, 0.3, 0.4};
            CatalogReader::reconcile_exoplanet_inclinations(&host, planets, inclinations, nodes);
            // A confirmed transit outranks the disk for an unknown planetary plane (E07).
            EXPECT_NEAR(inclinations[0] * fiftyseven, 90, 1e-8);
            EXPECT_NEAR(inclinations[1] * fiftyseven, 2, 1e-8);
            EXPECT_DOUBLE_EQ(inclinations[2], half_pi);
            EXPECT_DOUBLE_EQ(inclinations[3], _pi);
            EXPECT_DOUBLE_EQ(nodes[1], 0.2);
            EXPECT_DOUBLE_EQ(nodes[2], 0.3);
            EXPECT_DOUBLE_EQ(nodes[3], 0.4);
        }
    }
    Planet single;
    std::vector<Planet*> planets = {&single};
    std::vector<double> inclinations = {0.4 * fiftyseventh}, nodes = {0};
    CatalogReader::reconcile_exoplanet_inclinations(nullptr, planets, inclinations, nodes);
    EXPECT_DOUBLE_EQ(inclinations[0], 0);
}

TEST(ExoplanetInclinationTest, BroadEstimatesRequireAConflictingPrecisePlanet)
{
    for (double precise : {0.0, 60.0, 86.45})
    {
        Star host;
        host.has_disk = true;
        host.disk_heliocen_inclination = 90 * fiftyseventh;
        Planet first, second, reference;
        reference.inclination_error_upper = 0.5 * fiftyseventh;
        reference.inclination_error_lower = -0.5 * fiftyseventh;
        first.inclination_error_upper = 34 * fiftyseventh;
        first.inclination_error_lower = -24 * fiftyseventh;
        second.inclination_error_upper = 32 * fiftyseventh;
        second.inclination_error_lower = -23 * fiftyseventh;
        std::vector<Planet*> planets = {&first, &second};
        std::vector<double> inclinations = {39 * fiftyseventh, 41 * fiftyseventh};
        std::vector<double> nodes = {0, 0};
        if (precise)
        {
            planets.push_back(&reference);
            inclinations.push_back(precise * fiftyseventh);
            nodes.push_back(0);
        }
        CatalogReader::reconcile_exoplanet_inclinations(&host, planets, inclinations, nodes);
        EXPECT_NEAR(inclinations[0] * fiftyseven, precise > 73 ? precise : 39, 1e-8);
        EXPECT_NEAR(inclinations[1] * fiftyseven, precise > 73 ? precise : 41, 1e-8);
    }
}

TEST(PlanetTest, SystemViewRingsQueueCorrectly)
{
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    dispcx = 960;
    dispcy = 540;

    Planet p;
    strcpy(p.name, "Saturn");
    p.volumetric_mean_radius = 58232000;
    p.ring_radius = 1.3678e8;
    p.drawnx = 500;
    p.drawny = 540;
    p.drawnxmin = 450;
    p.drawnxmax = 550;

    ViewMode old_mode = view_mode;
    view_mode = vm_system;

    // Must execute cleanly without crash or infinite recursion
    draw_ring_gpu(&p, 50.0);

    // Also test fallback when arad is not passed explicitly
    draw_ring_gpu(&p);

    view_mode = old_mode;
    ImGui::DestroyContext(ctx);
}

TEST(PlanetTest, ObliqueViewNearPlanet_NotCulledWhenLimbInFrustum)
{
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    dispcx = 960;
    dispcy = 540;
    zoom = 1.0;

    Planet earth;
    strcpy(earth.name, "Earth");
    earth.volumetric_mean_radius = 6371000.0;
    earth.seqno = 3;
    earth.looked_for_maps = true;
    whereami = -1;
    view_mode = vm_spaceship;

    // Observer at distance 6800 km from center of Earth.
    // Earth is positioned at (0, -6800000, 0) relative to viewer.
    earth.tmprel = Point(0, -6800000.0, 0);

    // Tilt look direction up by 5 degrees above the horizontal plane (altitude = 5 * pi / 180),
    // looking away from Earth's center (which is at -90 degrees, nadir).
    // The angle between look direction and Earth's center is 95 degrees (> 90 degrees),
    // so the center of Earth has camera_space.z < 0.
    azimuth = 0.0;
    azimuth_correction = 0.0;
    altitude = 5.0 * fiftyseventh;

    // Verify Cartesian2D of center produces sentinel -1e29 because cz < 0
    Point rel = earth.tmprel;
    Cartesian2D cart(rel, azimuth + azimuth_correction, altitude, zoom);
    EXPECT_LT(cart.x, -1e20);
    EXPECT_LT(cart.y, -1e20);

    // Camera-space geometry
    Point cel_azrot = rotate3D(to_viewer_plane(earth.tmprel), center, yaxis, -(azimuth + azimuth_correction));
    Point camera_space = rotate3D(cel_azrot, center, xaxis, altitude);
    EXPECT_LT(camera_space.z, 0.0);

    double d = camera_space.magnitude();
    double obj_r = earth.get_equatorial_radius() * 1.25;
    EXPECT_GT(d, obj_r * 0.5);

    double alpha = asin(std::min(1.0, obj_r / d));
    double cos_theta = std::max(-1.0, std::min(1.0, camera_space.z / d));
    double theta = acos(cos_theta);
    double theta_min = std::max(0.0, theta - alpha);

    double r_diag = sqrt(dispcx * dispcx + dispcy * dispcy) / (dispcx * zoom);
    double max_half_fov = atan(r_diag) + 0.2;

    // The limb of Earth must still be inside the view frustum
    EXPECT_LE(theta_min, max_half_fov);

    // Render sphere impostor via GPU path
    double arad = asin(std::min(1.0, earth.volumetric_mean_radius / d));
    int res = draw_sphere_gpu(&earth, arad);
    EXPECT_GT(res, 0);
    EXPECT_TRUE(earth.onscreen);
    EXPECT_LE(earth.drawnymin, 1080.0f);
    EXPECT_GE(earth.drawnymax, 0.0f);

    ImGui::DestroyContext(ctx);
}


TEST_F(PlanetClassificationEvidenceTest, NewStellarOrbitsUseTheConservativeZone)
{
    for (double temp : {3000.0, static_cast<double>(sun_temp), 7200.0})
    {
        Star* host = make_star();
        host->temperature = temp;
        Planet* body = make_planet(host);
        body->orbit->semimajor_axis = 1e8;
        body->orbit->period = 7 * oneday;
        EXPECT_TRUE(body->set_habitable_orbit());
        EXPECT_TRUE(body->is_in_con_HZ());
        const double period = body->orbit->period;
        body->orbit->compute_period(body->mass);
        EXPECT_DOUBLE_EQ(period, body->orbit->period);
    }
}

TEST(OrbitalSpacingTest, ScreensCloseCrossingAndMassiveOrbits)
{
    EXPECT_TRUE(orbital_spacing_warning(AU, 0, earth_mass, AU, 0, earth_mass, solar_mass));
    EXPECT_TRUE(orbital_spacing_warning(AU, 0, earth_mass, 1.02 * AU, 0, earth_mass, solar_mass));
    EXPECT_FALSE(orbital_spacing_warning(AU, 0, earth_mass, 1.52 * AU, 0, earth_mass, solar_mass));
    EXPECT_TRUE(orbital_spacing_warning(AU, 0.4, earth_mass, 1.52 * AU, 0.2, earth_mass, solar_mass));
    EXPECT_TRUE(orbital_spacing_warning(AU, 0, jupiter_mass, 1.2 * AU, 0, jupiter_mass, solar_mass));
    EXPECT_FALSE(orbital_spacing_warning(AU, 0, earth_mass, 1.2 * AU, 0, earth_mass, solar_mass));
    EXPECT_FALSE(orbital_spacing_warning(0, 0, earth_mass, AU, 0, earth_mass, solar_mass));
}

TEST(OrbitalSpacingTest, ResonanceCandidatesKeepTheSimplestRatiosAndStopAtTheCap)
{
    struct Candidate { int m, n, sibling; };
    std::vector<int> siblings(1000);
    int calls = 0;
    const auto build = [&](int sibling, int m, int n, Candidate& candidate)
    {
        ++calls;
        candidate = {m, n, sibling};
        return true;
    };
    const auto capped = simplest_resonance_candidates<Candidate>(siblings, build);
    ASSERT_EQ(capped.size(), resonance_candidate_limit);
    EXPECT_EQ(calls, resonance_candidate_limit);
    for (const auto& candidate : capped) EXPECT_EQ(candidate.m + candidate.n, 5);
    siblings = {1, 2};
    const auto all = simplest_resonance_candidates<Candidate>(siblings, build, 1000);
    const auto limited = simplest_resonance_candidates<Candidate>(siblings, build, 12);
    ASSERT_GT(all.size(), limited.size());
    ASSERT_EQ(limited.size(), 12u);
    for (size_t i = 1; i < limited.size(); ++i)
    {
        EXPECT_LE(limited[i-1].m + limited[i-1].n, limited[i].m + limited[i].n);
    }
    for (size_t i = limited.size(); i < all.size(); ++i)
    {
        EXPECT_LE(limited.back().m + limited.back().n, all[i].m + all[i].n);
    }
    EXPECT_TRUE(simplest_resonance_candidates<Candidate>(siblings, build, 0).empty());
}

TEST(OrbitalSpacingTest, RejectedResonancesDoNotConsumeTheCandidateLimit)
{
    struct Candidate { int m, n, sibling; };
    const std::vector<int> siblings = {0, 1, 2, 3};
    const auto candidates = simplest_resonance_candidates<Candidate>(siblings,
        [](int sibling, int m, int n, Candidate& candidate)
        {
            if (sibling % 2 == 0) return false;
            candidate = {m, n, sibling};
            return true;
        }, 10);
    ASSERT_EQ(candidates.size(), 10u);
    for (const auto& candidate : candidates) EXPECT_EQ(candidate.sibling % 2, 1);
}

TEST_F(PlanetClassificationEvidenceTest, ColdAtmospheresCondenseAndConverge)
{
    Planet* world = make_planet(make_star());
    Atmosphere* air = world->ensure_atmosphere();
    AtmosphereComposition* gases = air->ensure_composition();
    air->surface_pressure = oneatm;
    gases->N2_portion = 0.99;
    gases->CO2_portion = 0.01;
    const double cold = world->condense_atmosphere(35, 35);
    EXPECT_LT(air->surface_pressure, 1.0);
    EXPECT_DOUBLE_EQ(gases->CO2_portion, 0);
    EXPECT_NEAR(cold, 35, 0.1);
    const double pressure = air->surface_pressure;
    EXPECT_NEAR(world->condense_atmosphere(35, cold), cold, 1e-6);
    EXPECT_NEAR(air->surface_pressure, pressure, 1e-6);
    world->condense_atmosphere(2, 2);
    EXPECT_DOUBLE_EQ(air->surface_pressure, 0);
    EXPECT_DOUBLE_EQ(gases->N2_portion, 0);
    EXPECT_DOUBLE_EQ(air->tau, 0);
}

TEST_F(PlanetClassificationEvidenceTest, CondensationRetainsWarmAirAndRespectsPartialPressure)
{
    Planet* world = make_planet(make_star());
    Atmosphere* air = world->ensure_atmosphere();
    AtmosphereComposition* gases = air->ensure_composition();
    air->surface_pressure = oneatm;
    gases->N2_portion = 0.78;
    gases->O2_portion = 0.21;
    gases->Ar_portion = 0.01;
    EXPECT_NEAR(world->condense_atmosphere(288, 288), 288, 1e-6);
    EXPECT_NEAR(air->surface_pressure, oneatm, 1e-6);
    gases->N2_portion = 0.999;
    gases->O2_portion = gases->Ar_portion = 0;
    gases->CO2_portion = 0.001;
    world->condense_atmosphere(180, 180);
    EXPECT_NEAR(air->surface_pressure, oneatm, 1e-6);
    EXPECT_NEAR(gases->CO2_portion, 0.001, 1e-9);
}

TEST_F(PlanetClassificationEvidenceTest, CondensationRecalculatesGreenhouseCooling)
{
    Planet* world = make_planet(make_star());
    Atmosphere* air = world->ensure_atmosphere();
    AtmosphereComposition* gases = air->ensure_composition();
    air->surface_pressure = 5 * oneatm;
    gases->CO2_portion = 1;
    const double final_k = world->condense_atmosphere(100, 170);
    EXPECT_LT(final_k, 110);
    EXPECT_LT(air->surface_pressure, 100);
    const double pressure = air->surface_pressure;
    EXPECT_NEAR(world->condense_atmosphere(100, final_k), final_k, 1e-6);
    EXPECT_NEAR(air->surface_pressure, pressure, 1e-6);
}

TEST_F(PlanetClassificationEvidenceTest, TemperatureQueriesApplyColdLimitsWithoutRestoringFrozenGases)
{
    Planet* world = make_planet(make_star(), "Frozen atmosphere", 100 * AU);
    Atmosphere* air = world->ensure_atmosphere();
    air->surface_pressure = oneatm;
    air->ensure_composition()->N2_portion = 1;
    EXPECT_TRUE(std::isfinite(world->estimate_surface_temperature()));
    EXPECT_LT(air->surface_pressure, 0.1);
    world->condense_atmosphere(2, 2);
    air->surface_pressure = oneatm;
    EXPECT_DOUBLE_EQ(world->condense_atmosphere(2, 2), 2);
    EXPECT_DOUBLE_EQ(air->surface_pressure, 0);
}

TEST_F(PlanetClassificationEvidenceTest, LocalClimateHasColdPolesWarmDesertsAndFiniteVacuum)
{
    Planet* world = make_planet(make_star());
    world->temperature = 288;
    world->sidereal_rotational_period = oneday;
    world->ensure_atmosphere()->surface_pressure = oneatm;
    world->has_water = 0.71;
    const double desert = world->est_local_temp(25 * fiftyseventh, 0);
    const double north = world->est_local_temp(half_pi, 0);
    const double south = world->est_local_temp(-half_pi, 0);
    EXPECT_GT(desert, 290);
    EXPECT_LT(north, 270);
    EXPECT_LT(south, 270);
    EXPECT_NEAR(north, south, 1e-8);
    world->atm->surface_pressure = 0;
    for (double lat : {-half_pi, 0.0, half_pi})
    {
        const double vacuum = world->est_local_temp(lat, 0);
        EXPECT_TRUE(std::isfinite(vacuum));
        EXPECT_GE(vacuum, 2.7);
    }
}

TEST_F(PlanetClassificationEvidenceTest, LocalClimateUsesEarthBumpMapInMeters)
{
    if (!std::filesystem::exists("maps/Earth_surf.jpg") || !std::filesystem::exists("maps/Earth_bump.png"))
        GTEST_SKIP() << "Earth map images are absent";
    Planet* world = make_planet(make_star(), "Earth climate benchmark");
    world->temperature = 288;
    world->sidereal_rotational_period = oneday;
    world->ensure_atmosphere()->surface_pressure = oneatm;
    world->has_water = 0.71;
    world->surf_map = new Map(world);
    ASSERT_TRUE(world->surf_map->load_from_jpeg("maps/Earth_surf.jpg"));
    ASSERT_TRUE(world->surf_map->load_from_png("maps/Earth_bump.png", true));
    const double sahara = world->est_local_temp(25 * fiftyseventh, 0);
    const double arctic = world->est_local_temp(85 * fiftyseventh, 0);
    const double antarctic = world->est_local_temp(-80 * fiftyseventh, 0);
    const double himalaya = world->est_local_temp(28 * fiftyseventh, 87 * fiftyseventh);
    EXPECT_GT(sahara, 290);
    EXPECT_LT(arctic, 270);
    EXPECT_LT(antarctic, 260);
    EXPECT_LT(himalaya, sahara - 15);
    const LocalClimate climate = world->local_climate();
    const double lat = 25 * fiftyseventh;
    EXPECT_NEAR(sahara, climate.temperature(-cos(lat), 0, sin(lat),
        world->surf_map->elevation_at_bilinear(lat, 0)), 1e-9);
    EXPECT_NEAR(world->est_local_temp(lat, -_pi), world->est_local_temp(lat, _pi), 1e-8);
}

TEST(LocalClimateTest, LockedDaysAreWarmAndFrostBoundariesAreGradual)
{
    LocalClimate climate;
    climate.stellar_locked = true;
    EXPECT_GT(climate.temperature(-1, 0, 0, 0), climate.temperature(1, 0, 0, 0));
    climate.redistribution = 0;
    EXPECT_TRUE(std::isfinite(climate.temperature(1, 0, 0, 0)));
    EXPECT_GE(climate.temperature(1, 0, 0, 0), 2.7);
    EXPECT_DOUBLE_EQ(LocalClimate::frost_fraction(270.15), 1);
    EXPECT_NEAR(LocalClimate::frost_fraction(273.15), 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(LocalClimate::frost_fraction(276.15), 0);
    EXPECT_NEAR(LocalClimate::frost_fraction(273.15 - 1e-6),
        LocalClimate::frost_fraction(273.15 + 1e-6), 1e-6);
    climate.stellar_locked = false;
    EXPECT_LT(climate.temperature(-1, 0, 0, 2000), climate.temperature(-1, 0, 0, 0) - 12);
    // Longitude-dependent climate variation bends the frost line on a sphere.
    const double latitude = 0.75;
    EXPECT_GT(std::abs(climate.temperature(cos(latitude), 0, sin(latitude), 0)
        - climate.temperature(0, cos(latitude), sin(latitude), 0)), 0.1);
}

TEST_F(PlanetClassificationEvidenceTest, GeneratedOceanUsesTheSameClimateAsTheReadout)
{
    Planet* world = make_planet(make_star(), "Ocean climate", 0.8 * AU);
    world->type = waterworld;
    world->sidereal_rotational_period = oneday;
    world->ensure_atmosphere()->ensure_composition()->N2_portion = 1;
    world->fictitious_map_height = 64;
    world->rnd_seed = 173;
    world->ring_status_known = true;
    world->surf_map = new Map(world);
    const bool saved_randomize = randomize_txgen;
    randomize_txgen = false;
    world->surf_map->generate_rocky_map(world);
    randomize_txgen = saved_randomize;
    ASSERT_TRUE(world->surf_map->is_complete());
    const RGB3 equator = world->surf_map->color_at(0, 0);
    const RGB3 north = world->surf_map->color_at(80 * fiftyseventh, 0);
    const RGB3 south = world->surf_map->color_at(-80 * fiftyseventh, 0);
    EXPECT_GT(world->est_local_temp(0, 0), water_freezing);
    EXPECT_LT(world->est_local_temp(80 * fiftyseventh, 0), water_freezing);
    EXPECT_LT(world->est_local_temp(-80 * fiftyseventh, 0), water_freezing);
    EXPECT_GT(equator.b, equator.r + 30);
    EXPECT_GT(north.r, equator.r + 50);
    EXPECT_GT(south.r, equator.r + 50);
    EXPECT_EQ(world->surf_map->material_at(0, 0), SurfaceMaterial::water);
    EXPECT_EQ(world->surf_map->material_at(80 * fiftyseventh, 0), SurfaceMaterial::ground);
    EXPECT_EQ(world->surf_map->material_at(-80 * fiftyseventh, 0), SurfaceMaterial::ground);
}

TEST_F(PlanetClassificationEvidenceTest, ExplicitRadiusEditsOverrideAnEstimatedEnvelope)
{
    Planet* body = make_planet(make_star());
    body->set_explicit_mass(6 * earth_mass);
    body->estimate_radius();
    body->classify(false, true, true);
    ASSERT_EQ(body->type, ice_giant);
    ASSERT_EQ(body->radius_evidence, PlanetMeasurement::estimated);
    body->set_explicit_radius(0.1 * earth_radius);
    body->classify(false, true, true);
    EXPECT_EQ(body->type, rocky);
    EXPECT_EQ(body->radius_evidence, PlanetMeasurement::observed);
    EXPECT_TRUE(body->classification_evidence["density_observed"].get<bool>());
    EXPECT_GT(body->classification_evidence["density_g_cm3"].get<double>(), 1000);
    EXPECT_DOUBLE_EQ(body->volumetric_mean_radius, 0.1 * earth_radius);
    body->classify();
    EXPECT_EQ(body->type, rocky);
}

TEST_F(PlanetClassificationEvidenceTest, SuppliedRadiiConstrainAdoptedMassesWithoutChangingTheirProvenance)
{
    Star* host = make_star("Synthetic host");
    Planet* body = make_planet(host, "Synthetic planet");
    body->mass = 8 * earth_mass;
    body->temperature = 280;
    for (PlanetMeasurement evidence : {PlanetMeasurement::estimated, PlanetMeasurement::unknown})
    {
        body->mass_evidence = evidence;
        for (double metal : {-.5, .5})
        {
            host->metallicity = metal;
            host->metallicity_ratio = "[Fe/H]";
            for (double radius : {3.0, 1.79, .00001})
            {
                SCOPED_TRACE(radius);
                body->set_explicit_radius(radius * earth_radius);
                body->classify(false, false, true);
                EXPECT_EQ(body->type, radius == 3.0 ? ice_giant : rocky);
                EXPECT_EQ(body->mass_evidence, evidence);
                EXPECT_FALSE(body->classification_evidence["density_observed"].get<bool>());
                EXPECT_TRUE(body->classification_evidence["density_estimated"].get<bool>());
                EXPECT_FALSE(body->classification_evidence["metallicity_applied"].get<bool>());
                EXPECT_DOUBLE_EQ(body->volumetric_mean_radius, radius * earth_radius);
            }
        }
    }
}

TEST_F(PlanetClassificationEvidenceTest, SuppliedRadiusUsesTheAdoptedRvMassAndRetainsTheMinimum)
{
    Planet* body = make_planet(make_star(), "Inclination-derived mass");
    body->mass = 8 * earth_mass;
    body->msini = 6 * earth_mass;
    body->mass_evidence = PlanetMeasurement::estimated;
    body->temperature = 280;
    for (double radius : {1.79, .00001})
    {
        body->set_explicit_radius(radius * earth_radius);
        body->classify(false, true, true);
        EXPECT_EQ(body->type, rocky);
        EXPECT_EQ(body->mass_evidence, PlanetMeasurement::estimated);
        EXPECT_FALSE(body->classification_evidence["density_observed"].get<bool>());
        EXPECT_FALSE(body->classification_evidence["density_is_lower_bound"].get<bool>());
        EXPECT_EQ(body->classification_evidence["density_constraint_basis"], "adopted_mass_and_observed_radius");
        EXPECT_DOUBLE_EQ(body->classification_evidence["density_constraint_g_cm3"].get<double>(),
            body->classification_evidence["density_g_cm3"].get<double>());
        EXPECT_NEAR(body->classification_evidence["minimum_density_g_cm3"].get<double>(),
            .75 * body->classification_evidence["density_g_cm3"].get<double>(),
            body->classification_evidence["density_g_cm3"].get<double>() * 1e-12);
        Planet restored;
        ASSERT_TRUE(restored.from_json(body->to_json()));
        EXPECT_EQ(restored.classification_evidence, body->classification_evidence);
    }
    // A weak RV lower bound does not replace the adopted mass used by the physical model.
    body->msini = earth_mass;
    body->set_explicit_radius(2 * earth_radius);
    body->classify(false, true, true);
    EXPECT_EQ(body->type, rocky);
    EXPECT_LT(body->classification_evidence["minimum_density_g_cm3"].get<double>(), 1);
    EXPECT_GT(body->classification_evidence["density_constraint_g_cm3"].get<double>(), 5);
}

TEST_F(PlanetClassificationEvidenceTest, ExplicitMassEditsClearTheOldMinimumAndErrors)
{
    Planet* body = make_planet(make_star());
    body->mass_evidence = PlanetMeasurement::minimum;
    body->msini = body->mass;
    body->mass_error_lower = body->mass_error_upper = 0.1 * body->mass;
    body->radius_error_lower = body->radius_error_upper = 0.1 * body->volumetric_mean_radius;
    body->set_explicit_mass(6 * earth_mass);
    body->set_explicit_radius(1.5 * earth_radius);
    body->classify(false, true, true);
    EXPECT_EQ(body->type, rocky);
    EXPECT_EQ(body->mass_evidence, PlanetMeasurement::observed);
    EXPECT_DOUBLE_EQ(body->msini, 0);
    EXPECT_TRUE(std::isnan(body->mass_error_lower));
    EXPECT_TRUE(std::isnan(body->radius_error_upper));
    EXPECT_FALSE(body->classification_evidence["density_is_lower_bound"].get<bool>());
}

TEST_F(PlanetClassificationEvidenceTest, StoredColdTemperaturesAlsoCondenseAtmospheres)
{
    Planet* world = make_planet(make_star());
    world->temperature = 35;
    Atmosphere* air = world->ensure_atmosphere();
    air->surface_pressure = oneatm;
    AtmosphereComposition* gases = air->ensure_composition();
    gases->N2_portion = 0.99;
    gases->CO2_portion = 0.01;
    EXPECT_LE(world->estimate_surface_temperature(), 35);
    EXPECT_LT(air->surface_pressure, 1);
    EXPECT_DOUBLE_EQ(gases->CO2_portion, 0);
    const double temperature = world->temperature;
    const double pressure = air->surface_pressure;
    EXPECT_NEAR(world->estimate_surface_temperature(), temperature, 1e-7);
    EXPECT_NEAR(air->surface_pressure, pressure, 1e-7);
}

TEST_F(PlanetClassificationEvidenceTest, WarmObservedTemperatureSurvivesPhaseChecks)
{
    Planet* world = make_planet(make_star());
    world->temperature = 288;
    AtmosphereComposition* gases = world->ensure_atmosphere()->ensure_composition();
    gases->N2_portion = 0.78;
    gases->O2_portion = 0.21;
    gases->Ar_portion = 0.01;
    EXPECT_DOUBLE_EQ(world->estimate_surface_temperature(), 288);
    EXPECT_DOUBLE_EQ(world->get_surface_pressure(), oneatm);
}

TEST_F(PlanetClassificationEvidenceTest, IncompleteAbundancesCannotPreserveAnAnonymousColdAtmosphere)
{
    Planet* world = make_planet(make_star());
    world->temperature = 2;
    AtmosphereComposition* gases = world->ensure_atmosphere()->ensure_composition();
    gases->N2_portion = 0.9;
    gases->CO2_portion = 0.05;
    EXPECT_TRUE(std::isfinite(world->estimate_surface_temperature()));
    EXPECT_DOUBLE_EQ(world->get_surface_pressure(), 0);
    EXPECT_DOUBLE_EQ(gases->N2_portion, 0);
    EXPECT_DOUBLE_EQ(gases->CO2_portion, 0);
}
