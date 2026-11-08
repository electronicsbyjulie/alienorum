#include <filesystem>
#include <fstream>
#include <chrono>
#include <gtest/gtest.h>
#include "../globals.h"
#include "../inputs.h"
#include "../loaders.h"
#include "../classes/cloudclimate.h"
#include "../classes/thermalclimate.h"
#include "universe_fixture.h"

using namespace alienorum;

TEST(IceEnergyTest, MeltingSpendsLatentHeatBeforeWarmingTheOcean)
{
    IceColumn column;
    column.depth = 10;
    column.salinity = 0;
    column.albedo_visibility = 0;
    const IceForcing sunlight{100, 0, 0};
    const double initial = -IceColumn::ice_density * IceColumn::latent_per_kg;
    const double energy = column.advance(initial, sunlight, 86400);
    EXPECT_NEAR(energy - initial, 8640000, .001);
    const auto ice = column.state(energy, sunlight);
    EXPECT_NEAR(ice.temperature, column.freezing(), 1e-9);
    EXPECT_NEAR(ice.thickness, 1 - 8640000 / (IceColumn::ice_density * IceColumn::latent_per_kg), 1e-9);
    const double warm = column.advance(initial, sunlight, -initial / 100 + 86400);
    EXPECT_NEAR(warm, 8640000, .001);
    EXPECT_NEAR(column.state(warm, sunlight).temperature, column.freezing() + warm / column.water_capacity(), 1e-9);
}

TEST(IceEnergyTest, ThickIceInsulatesAndTheImplicitStepConservesEnergy)
{
    IceColumn column;
    column.depth = 100;
    column.salinity = 0;
    const IceForcing cooling{0, 100, 10};
    const double thin = -.1 * IceColumn::ice_density * IceColumn::latent_per_kg;
    const double thick = 10 * thin;
    const double thin_next = column.advance(thin, cooling, 86400);
    const double thick_next = column.advance(thick, cooling, 86400);
    EXPECT_GT(thin - thin_next, thick - thick_next);
    EXPECT_NEAR(thin_next - thin, 86400 * column.state(thin_next, cooling).net_flux, .01);
    EXPECT_LT(column.state(thin_next, cooling).temperature, column.freezing());
}

TEST(IceEnergyTest, TransportedHeatIsNotReflectedByIce)
{
    IceColumn column;
    column.albedo_visibility = 1;
    const IceForcing heat{0, 0, 0, 100};
    const double initial = -IceColumn::ice_density * IceColumn::latent_per_kg;
    EXPECT_DOUBLE_EQ(column.state(initial, heat).absorbed, 100);
    EXPECT_DOUBLE_EQ(column.state(0, heat).absorbed, 100);
    EXPECT_NEAR(column.advance(initial, heat, 86400) - initial, 8640000, .001);
    const IceForcing sunlight{100, 0, 0, 100};
    EXPECT_GT(column.state(initial, sunlight).absorbed, 100);
    EXPECT_LT(column.state(initial, sunlight).absorbed, column.state(0, sunlight).absorbed);
}

TEST(IceEnergyTest, EarthLikeIntermediateLatitudeFormsAndCompletelyMeltsIce)
{
    IceColumn column;
    column.atmospheric_capacity = 101325 / 9.81 * 1005;
    ThermalOrbit orbit;
    orbit.period = 31558149.7635;
    orbit.eccentricity = .0167086;
    orbit.pole_v = std::sin(23.45 * IceSeason::pi / 180);
    IceSeason season(column, orbit, 288, 47.52, 240, 4 * 240 / 288.0, 1 / 1.1, 1, true, false, 3.14);
    double least = INFINITY, most = -INFINITY, cover = 0;
    for (int phase = 0; phase < 128; ++phase)
    {
        const double anomaly = phase * 2 * IceSeason::pi / 128;
        const double energy = season.energy_at(.815, 0, anomaly);
        const auto state = column.state(energy, season.forcing_at(.815, 0, anomaly));
        least = std::min(least, energy); most = std::max(most, energy);
        cover = std::max(cover, state.cover);
    }
    EXPECT_LT(least, 0);
    EXPECT_GT(most, 0);
    EXPECT_GT(cover, .5);
    bool resolved_cycle = false;
    for (const auto& curves : season.histories)
    for (const auto& curve : curves)
    {
        const auto bounds = std::minmax_element(curve.energy.begin(), curve.energy.end());
        resolved_cycle = resolved_cycle || (*bounds.first < 0 && *bounds.second > 0);
    }
    EXPECT_TRUE(resolved_cycle);
}

TEST(IceEnergyTest, OceanTransportWarmsTheIceBaseWithoutAddingEnergy)
{
    IceColumn column;
    const double ice = -2 * IceColumn::ice_density * IceColumn::latent_per_kg;
    const IceForcing atmosphere{0, 150, 3.3, 70, 0};
    const IceForcing ocean{0, 150, 3.3, 30, 40};
    EXPECT_DOUBLE_EQ(column.state(0, atmosphere).net_flux, column.state(0, ocean).net_flux);
    EXPECT_DOUBLE_EQ(column.state(-column.latent(), atmosphere).net_flux,
        column.state(-column.latent(), ocean).net_flux);
    const auto surface = column.state(ice, atmosphere), bottom = column.state(ice, ocean);
    EXPECT_LT(bottom.temperature, column.freezing());
    EXPECT_LT(surface.net_flux, 0);
    EXPECT_GT(bottom.net_flux, 0);
    const double updated = column.advance(ice, ocean, 86400);
    EXPECT_NEAR(updated - ice, 86400 * column.state(updated, ocean).net_flux, .01);
}

TEST(IceEnergyTest, LongEccentricYearMeltsAndRefreezesIceOverADeepOcean)
{
    IceColumn column;
    column.depth = 1000;
    column.atmospheric_capacity = 101325 / 9.81 * 1005;
    ThermalOrbit orbit;
    orbit.eccentricity = .16;
    orbit.period = 636.13 * 86400;
    IceSeason season(column, orbit, 271, 0, 200, 3, 1 / 1.1, 1, true, false);
    double least = INFINITY, most = 0;
    for (int phase = 0; phase < 128; ++phase)
    {
        const double anomaly = phase * 2 * IceSeason::pi / 128;
        const auto state = column.state(season.energy_at(0, 0, anomaly), season.forcing_at(0, 0, anomaly));
        least = std::min(least, state.thickness); most = std::max(most, state.thickness);
    }
    EXPECT_DOUBLE_EQ(least, 0);
    EXPECT_GT(most, .5);
    EXPECT_TRUE(season.converged);
}

TEST(IceEnergyTest, ReflectionAndFiniteWaterLimitAlterTheEnergyBalance)
{
    IceColumn column;
    column.depth = .05;
    const IceForcing sunlight{200, 190, 3};
    const auto water = column.state(0, sunlight);
    const auto frozen = column.state(-column.latent(), sunlight);
    EXPECT_LT(frozen.absorbed, water.absorbed);
    EXPECT_NEAR(frozen.thickness, column.depth * 1000 / 917, 1e-12);
    EXPECT_LT(column.freezing(.5), column.freezing());
    EXPECT_LT(column.state(-2 * column.latent(), sunlight).temperature, frozen.temperature);
    column.albedo_visibility = 0;
    EXPECT_DOUBLE_EQ(column.state(-column.latent(), sunlight).absorbed, water.absorbed);
}

TEST(IceEnergyTest, EccentricOrbitFreezesAndThawsWithoutAxialSeasons)
{
    IceColumn column;
    column.depth = .05;
    column.salinity = 0;
    ThermalOrbit orbit;
    orbit.eccentricity = .2;
    orbit.period = 31557600;
    IceSeason season(column, orbit, 273.15, 0, 200, 3, 1, 1, true, false);
    EXPECT_TRUE(season.converged);
    EXPECT_GT(season.annual_liquid_fraction, 0);
    EXPECT_LT(season.annual_liquid_fraction, 1);
    const double peri = season.energy_at(0, 0, 0);
    const double apo = season.energy_at(0, 0, IceSeason::pi);
    EXPECT_GT(peri, 0);
    EXPECT_LT(apo, 0);
    EXPECT_DOUBLE_EQ(peri, season.energy_at(0, 0, 0));
    EXPECT_DOUBLE_EQ(apo, season.energy_at(0, 0, -IceSeason::pi));
}

TEST(IceEnergyTest, RegionalHistoriesBreakUpIceMarginsBeforeColouring)
{
    IceColumn column;
    column.depth = .05;
    column.salinity = 0;
    ThermalOrbit orbit;
    orbit.period = 31557600;
    LocalClimate climate;
    climate.regional_k = 4;
    IceSeason season(column, orbit, 273.15, 30, 200, 3, 1, 1, true, false, climate.regional_k);
    bool mixed_margin = false;
    for (int row = 1; row < 100; ++row)
    {
        const double z = row / 100.0, radius = std::sqrt(1 - z * z);
        bool frozen = false, liquid = false;
        for (int longitude = 0; longitude < 180; ++longitude)
        {
            const double angle = longitude * 2 * IceSeason::pi / 180;
            const double regional = climate.regional_temperature(radius * std::cos(angle), radius * std::sin(angle), z);
            const double energy = season.energy_at(z, angle, 0, regional);
            frozen = frozen || energy < 0;
            liquid = liquid || energy > 0;
            EXPECT_DOUBLE_EQ(energy, season.energy_at(z, angle, 0, regional));
        }
        mixed_margin = mixed_margin || (frozen && liquid);
    }
    EXPECT_TRUE(mixed_margin);
    EXPECT_LT(season.forcing_at(.5, 0, 0, 3).outgoing_at_freezing,
        season.forcing_at(.5, 0, 0, -3).outgoing_at_freezing);
    const double z = .5, radius = std::sqrt(1 - z * z);
    EXPECT_NEAR(climate.regional_temperature(-radius, 1e-12, z),
        climate.regional_temperature(-radius, -1e-12, z), 1e-8);
}

class CurrentConditionsTest : public UniverseFixture
{
    double original_time = simnow;
    bool original_abort = abort_load.load(), original_done = done;
    int original_where = whereami;
    ViewMode original_view = view_mode;

    protected:
    Star* host = nullptr;
    void SetUp() override
    {
        UniverseFixture::SetUp();
        simnow = J2000_TIME_T;
        abort_load = false;
        done = false;
        host = make_star();
        host->distance = 0;
        host->update_location(simnow);
    }
    void TearDown() override
    {
        wait_for_texture_tasks();
        publish_regenerated_clouds();
        delete_the_universe();
        simnow = original_time;
        abort_load = original_abort;
        done = original_done;
        whereami = original_where;
        view_mode = original_view;
        UniverseFixture::TearDown();
    }
    Planet* world(double pressure = 0, bool ocean = false)
    {
        Planet* result = make_planet(host, "Thermal fixture");
        result->type = ocean ? waterworld : rocky;
        result->BV_color = .65;
        result->albedo = .3;
        result->orbit->period = oneyear;
        result->sidereal_rotational_period = oneday;
        result->has_water = ocean ? 1 : 0;
        result->ensure_atmosphere()->surface_pressure = pressure;
        result->atm->ensure_composition()->N2_portion = 1;
        result->update_location(simnow);
        return result;
    }
    void phase(Planet* planet, double fraction)
    {
        simnow = J2000_TIME_T + fraction * planet->orbit->period;
        planet->update_location(simnow);
    }
};

TEST(ThermalHistoryTest, EccentricOrbitFluxAverageAndMemoryHavePhysicalLimits)
{
    ThermalOrbit orbit;
    orbit.period = 365.25 * 86400;
    orbit.eccentricity = .6;
    const ThermalHistory immediate(orbit, 1);
    const ThermalHistory buffered(orbit, 1000 * orbit.period);
    EXPECT_NEAR(immediate.global_flux, 6.25, 1e-5);
    EXPECT_NEAR(buffered.global_flux, 1 / std::sqrt(1 - .36), .002);
    orbit.mean_anomaly = 3.141592653589793;
    EXPECT_NEAR(ThermalHistory(orbit, 1).global_flux, 1 / 2.56, 1e-5);
    EXPECT_LT(std::abs(ThermalHistory(orbit, 1000 * orbit.period).global_flux - buffered.global_flux), .005);
    for (double e : {0.0, .9, .99})
    {
        orbit.eccentricity = e;
        for (double tau : {1.0, orbit.period, orbit.period * 100})
        {
            const auto history = ThermalHistory(orbit, tau);
            EXPECT_TRUE(std::isfinite(history.global_flux));
            EXPECT_GT(history.global_flux, 0);
        }
    }
}

TEST_F(CurrentConditionsTest, PrecessingOceanPublishesSeasonalIceWhileTheClockAdvances)
{
    Planet* planet = world(oneatm, true);
    planet->temperature = 288;
    planet->temperature_supplied = true;
    planet->obliquity = 23.45 * _pi / 180;
    planet->precession = 2 * _pi / (25791 * oneyear);
    planet->orbit->eccentricity = .0167086;
    LocalClimate climate;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do
    {
        simnow += 60;
        planet->orbit->eccentricity += 1e-11;
        planet->orbit->period += .0001;
        planet->update_location(simnow);
        climate = planet->current_local_climate(true);
        if (climate.ice_season) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    ASSERT_TRUE(climate.ice_season);
    EXPECT_FALSE(climate.ice_pending);
    const auto history = climate.ice_season;
    double thin = INFINITY, thick = 0;
    for (int phase = 0; phase < 32; ++phase)
    {
        const double anomaly = phase * 2 * _pi / 32, z = std::sin(75 * _pi / 180);
        const auto state = history->column.state(history->energy_at(z, 0, anomaly), history->forcing_at(z, 0, anomaly));
        thin = std::min(thin, state.thickness);
        thick = std::max(thick, state.thickness);
    }
    EXPECT_GT(thick, .001);
    EXPECT_GT(thick - thin, .01);
    simnow += 3600;
    planet->orbit->eccentricity += 1e-9;
    planet->orbit->period += .01;
    planet->update_location(simnow);
    const auto later = planet->current_local_climate(true);
    EXPECT_EQ(later.ice_season, history);
    EXPECT_NE(later.ice_mean_anomaly, climate.ice_mean_anomaly);
    EXPECT_DOUBLE_EQ(planet->temperature, 288);
}

TEST_F(CurrentConditionsTest, CurrentDistanceChangesReadoutsWithoutChangingTextureClimate)
{
    Planet* planet = world();
    planet->orbit->eccentricity = .5;
    phase(planet, 0);
    const double texture = planet->est_local_temp(.3, .2);
    const auto periapsis = planet->current_local_climate();
    ASSERT_TRUE(periapsis.current_conditions);
    phase(planet, .5);
    const auto apoapsis = planet->current_local_climate();
    EXPECT_GT(periapsis.current_mean_k, apoapsis.current_mean_k * 1.4);
    EXPECT_NEAR(periapsis.instantaneous_flux / apoapsis.instantaneous_flux, 9, .001);
    EXPECT_DOUBLE_EQ(planet->est_local_temp(.3, .2), texture);
}

TEST_F(CurrentConditionsTest, OceansAndAtmosphericColumnsDampAndDelayOrbitalHeating)
{
    Planet* land = world();
    Planet* ocean = world(20 * oneatm, true);
    land->orbit->eccentricity = ocean->orbit->eccentricity = .5;
    phase(land, 0);
    const double land_peri = land->current_local_climate().current_mean_k;
    phase(ocean, 0);
    const double ocean_peri = ocean->current_local_climate().current_mean_k;
    phase(ocean, .1);
    const double ocean_after = ocean->current_local_climate().current_mean_k;
    phase(land, .5);
    const double land_apo = land->current_local_climate().current_mean_k;
    phase(ocean, .5);
    const double ocean_apo = ocean->current_local_climate().current_mean_k;
    EXPECT_LT(std::abs(ocean_peri - ocean_apo), (land_peri - land_apo) * .2);
    EXPECT_GT(ocean_after, ocean_peri);
}

TEST_F(CurrentConditionsTest, ObliquityReversesSeasonalHeatingBetweenHemispheres)
{
    Planet* planet = world(1000);
    planet->obliquity = .7;
    phase(planet, .25);
    const double first = planet->est_current_local_temp(1, 0) - planet->est_current_local_temp(-1, 0);
    phase(planet, .75);
    const double second = planet->est_current_local_temp(1, 0) - planet->est_current_local_temp(-1, 0);
    EXPECT_LT(first * second, 0);
    EXPECT_GT(std::abs(first), 5);
    EXPECT_GT(std::abs(second), 5);
}

TEST_F(CurrentConditionsTest, DiurnalHeatingIncludesAfternoonLagAndOceanBuffering)
{
    Planet* planet = world(oneatm);
    auto climate = planet->current_local_climate();
    ASSERT_TRUE(climate.current_conditions);
    climate.regional_k = climate.dry_belt_k = 0;
    const double longitude = std::atan2(-climate.sun_y, -climate.sun_x);
    const auto temperature = [&](double hour)
    {
        return climate.temperature(-std::cos(longitude + hour), -std::sin(longitude + hour), 0, 1);
    };
    EXPECT_GT(temperature(1), temperature(-1));
    const double land_range = temperature(1) - temperature(-2);
    climate.water_fraction = 1;
    const double ocean_range = temperature(1) - temperature(-2);
    EXPECT_GT(land_range, 0);
    EXPECT_LT(std::abs(ocean_range), std::abs(land_range));
}

TEST_F(CurrentConditionsTest, SameEpochEditsInvalidateClimateAndExplicitDrySurfacesStayDry)
{
    Planet* planet = world(oneatm, true);
    const auto wet = planet->current_local_climate();
    EXPECT_EQ(wet.water_fraction, 1);
    planet->has_water_supplied = true;
    planet->has_water = 0;
    const auto dry = planet->current_local_climate();
    EXPECT_EQ(dry.water_fraction, 0);
    planet->atm->surface_pressure *= 10;
    const auto dense = planet->current_local_climate();
    EXPECT_GT(dense.land_diurnal_seconds, dry.land_diurnal_seconds);
    EXPECT_DOUBLE_EQ(planet->current_local_climate().land_diurnal_seconds, dense.land_diurnal_seconds);
}

TEST_F(CurrentConditionsTest, TimeJumpsAreDeterministicAndSuppliedMeansRetainLegacyReadouts)
{
    Planet* planet = world(oneatm);
    planet->orbit->eccentricity = .4;
    phase(planet, .123);
    const double first = planet->est_current_local_temp(.4, .7);
    phase(planet, .77);
    planet->est_current_local_temp(-.2, -1);
    phase(planet, .123);
    EXPECT_DOUBLE_EQ(planet->est_current_local_temp(.4, .7), first);
    const json saved = planet->to_json();
    EXPECT_FALSE(saved["temperature_supplied"].get<bool>());
    Planet restored;
    ASSERT_TRUE(restored.from_json(saved));
    EXPECT_FALSE(restored.has_supplied_mean_temperature());
    Serialization::SaveContext context;
    const auto compact = Serialization::compact_record(saved, context);
    Planet compact_restored;
    ASSERT_TRUE(compact_restored.from_json(Serialization::expand_record(compact, context.header)));
    EXPECT_FALSE(compact_restored.has_supplied_mean_temperature());
    planet->temperature = 288;
    planet->temperature_supplied = true;
    EXPECT_FALSE(planet->current_local_climate().current_conditions);
    EXPECT_DOUBLE_EQ(planet->est_current_local_temp(.4, .7), planet->est_local_temp(.4, .7));
    EXPECT_TRUE(planet->current_local_climate(true).current_conditions);
    Planet legacy;
    ASSERT_TRUE(legacy.from_json({{"temperature", 288}, {"mass", earth_mass}, {"volumetric_mean_radius", earth_radius}}));
    EXPECT_TRUE(legacy.has_supplied_mean_temperature());
}

TEST_F(CurrentConditionsTest, ProceduralCloudsUseCurrentClimateAndPublishAtomically)
{
    Planet* planet = world(oneatm);
    planet->atm->comp->N2_portion = .99;
    planet->atm->comp->H2O_portion = .01;
    planet->fictitious_map_height = 16;
    planet->orbit->eccentricity = .4;
    phase(planet, 0);
    const auto peri = CloudClimate::prepare(*planet);
    phase(planet, .5);
    const auto apo = CloudClimate::prepare(*planet);
    EXPECT_GT(peri.surface_k, apo.surface_k);
    planet->orbit->eccentricity = 0;
    phase(planet, 0);
    planet->cloud_map = new Map(planet); // Cached textures can lack procedural provenance.
    Map* original = planet->cloud_map;
    generating_fic_texture = false;
    ASSERT_TRUE(request_cloud_regeneration(planet));
    EXPECT_EQ(planet->cloud_map, original);
    EXPECT_FALSE(request_cloud_regeneration(planet));
    wait_for_texture_tasks();
    EXPECT_FALSE(generating_fic_texture);
    publish_regenerated_clouds();
    ASSERT_NE(planet->cloud_map, nullptr);
    EXPECT_TRUE(planet->cloud_map->is_complete());
    EXPECT_TRUE(planet->cloud_map->procedurally_generated);
    EXPECT_EQ(planet->weather_revision, 1u);
    planet->atm->comp->H2O_portion = 0;
    planet->atm->comp->N2_portion = 1;
    ASSERT_TRUE(request_cloud_regeneration(planet));
    wait_for_texture_tasks();
    EXPECT_FALSE(generating_fic_texture);
    publish_regenerated_clouds();
    EXPECT_EQ(planet->cloud_map, nullptr);
}

TEST_F(CurrentConditionsTest, RefreshShortcutCannotClearEarthCloudCooldown)
{
    Planet* earth = world(oneatm);
    strcpy(earth->name, "Earth");
    earth->home_world = true;
    earth->static_clouds = false;
    earth->cloud_map_url = "invalid://earth-cloud-test";
    auto original = std::make_unique<Map>(earth);
    earth->cloud_map = original.release();
    struct RestoreDirectory
    {
        std::filesystem::path original = std::filesystem::current_path();
        std::filesystem::path temporary;
        ~RestoreDirectory()
        {
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore;
    const auto temporary = std::filesystem::temp_directory_path() / ("alienorum_cloud_gate_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    restore.temporary = temporary;
    std::filesystem::create_directories(temporary / "maps");
    std::filesystem::current_path(temporary);
    const std::string path = "maps/Earth_clouds.attempt";
    { std::ofstream out(path); out << std::time(nullptr) << '\n'; }
    const auto modified = std::filesystem::last_write_time(path);
    Map* before = earth->cloud_map;
    whereami = earth->seqno;
    view_mode = vm_sunclock;
    process_key_cmd_ctrl_char('C');
    ASSERT_TRUE(request_cloud_regeneration(earth));
    wait_for_texture_tasks();
    publish_regenerated_clouds();
    EXPECT_EQ(earth->cloud_map, before);
    EXPECT_EQ(std::filesystem::last_write_time(path), modified);
    EXPECT_FALSE(std::filesystem::exists("maps/Earth_clouds.jpg"));
    EXPECT_EQ(earth->weather_revision, 0u);
}
