#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "../classes/giantreflectance.h"
#include "../classes/color.h"
#include "../classes/giantclouds.h"
#include "universe_fixture.h"
#include "../loaders.h"

using namespace alienorum;
using json = nlohmann::json;

TEST(GiantReflectanceTest, PublishedGridProducesFiniteDistinctTemperateAndColdFamilies)
{
    GiantReflectanceInput input;
    input.seed = 42;
    input.earth_masses = 318;
    input.equivalent_axis_au = 2;
    const auto temperate = estimate_giant_reflectance(input);
    ASSERT_TRUE(valid_giant_reflectance(temperate, temperate.at("BV")));
    EXPECT_NEAR(temperate.at("BV").get<double>(), .760, .01);
    input.equivalent_axis_au = 5;
    const auto cold = estimate_giant_reflectance(input);
    ASSERT_TRUE(cold.is_object());
    EXPECT_GT(std::abs(temperate.at("BV").get<double>() - cold.at("BV").get<double>()), .05);
    input.earth_masses = 17;
    const auto neptune = estimate_giant_reflectance(input);
    EXPECT_GT(std::abs(cold.at("VR").get<double>() - neptune.at("VR").get<double>()), .1);
    EXPECT_LT(neptune.at("neptune_weight").get<double>(), 1.00001);
    input.equivalent_axis_au = 100;
    const auto distant = estimate_giant_reflectance(input);
    EXPECT_TRUE(distant.at("distance_grid_clamped").get<bool>());
    EXPECT_TRUE(valid_giant_reflectance(distant, distant.at("BV")));
}

TEST(GiantReflectanceTest, ScenariosAreStableAndSuppliedZeroMethaneIsActiveData)
{
    GiantReflectanceInput input;
    input.seed = 42; input.earth_masses = 17; input.equivalent_axis_au = 5;
    const auto first = estimate_giant_reflectance(input);
    EXPECT_EQ(first, estimate_giant_reflectance(input));
    input.seed = 57;
    EXPECT_NE(first.at("bands"), estimate_giant_reflectance(input).at("bands"));
    input.seed = 42;
    input.methane_fraction = 0;
    const auto zero = estimate_giant_reflectance(input);
    EXPECT_TRUE(zero.at("methane_supplied").get<bool>());
    EXPECT_DOUBLE_EQ(zero.at("methane_absorption_scale"), 0);
    EXPECT_GT(zero.at("VR").get<double>(), first.at("VR").get<double>());
    input.methane_fraction = .1;
    EXPECT_LT(estimate_giant_reflectance(input).at("VR").get<double>(), zero.at("VR").get<double>());
    input.equivalent_axis_au = NAN;
    EXPECT_TRUE(estimate_giant_reflectance(input).is_null());
}

TEST(GiantReflectanceTest, StellarLightCompositionAndGravityContributeSeparately)
{
    GiantReflectanceInput input;
    input.seed = 42; input.earth_masses = 318; input.equivalent_axis_au = 2;
    const auto first = estimate_giant_reflectance(input);
    input.stellar_temperature_k = 3500;
    const auto red_star = estimate_giant_reflectance(input);
    EXPECT_GT(red_star.at("BV").get<double>(), first.at("BV").get<double>() + .3);
    input.stellar_temperature_k = 5778;
    input.stellar_metallicity_dex = .4;
    const auto metal = estimate_giant_reflectance(input);
    EXPECT_GT(metal.at("enrichment_solar").get<double>(), first.at("enrichment_solar").get<double>());
    input.carbon_relative_to_iron_dex = .3;
    const auto carbon = estimate_giant_reflectance(input);
    EXPECT_GT(carbon.at("methane_absorption_scale").get<double>(), metal.at("methane_absorption_scale").get<double>());
    input.gravity_m_s2 = 40;
    EXPECT_NE(estimate_giant_reflectance(input).at("cloud_shift"), carbon.at("cloud_shift"));
}

class GiantReflectancePlanetTest : public UniverseFixture
{
protected:
    void TearDown() override
    {
        delete_the_universe();
        UniverseFixture::TearDown();
    }
    Planet* giant(Star* host, cel_obj_type type = gas_giant)
    {
        Planet* body = make_planet(host, "Color model", 5 * AU);
        body->type = type;
        body->mass = type == ice_giant ? 17 * earth_mass : jupiter_mass;
        body->volumetric_mean_radius = type == ice_giant ? 25000000 : 70000000;
        body->rnd_seed = 42;
        return body;
    }
};

TEST_F(GiantReflectancePlanetTest, AutomaticColorsAndFullPrecisionProvenanceSurviveReadableSaves)
{
    Planet* body = giant(make_star());
    body->set_color_from_type(false);
    ASSERT_TRUE(body->has_spectral_giant_color());
    const auto before = body->to_json();
    const auto saved = Serialization::output_record(before);
    const auto expanded = Serialization::expand_output_record(saved);
    Planet restored;
    ASSERT_TRUE(restored.from_json(expanded));
    ASSERT_TRUE(restored.has_spectral_giant_color());
    EXPECT_EQ(restored.giant_color_model, body->giant_color_model);
    EXPECT_DOUBLE_EQ(restored.BV_color, body->BV_color);
    EXPECT_DOUBLE_EQ(restored.reflected_vr_color(), body->reflected_vr_color());
    restored.set_color_from_type(false);
    EXPECT_EQ(restored.giant_color_model, body->giant_color_model);
    const auto original_color = Color::color_from_planet(0, body);
    const auto saved_color = Color::color_from_planet(0, &restored);
    EXPECT_DOUBLE_EQ(original_color.red, saved_color.red);
    EXPECT_DOUBLE_EQ(original_color.blue, saved_color.blue);
    EXPECT_EQ(body->to_json(), before);
}

TEST_F(GiantReflectancePlanetTest, AuthoredColorsIncludingZeroAndQuestionMarksTakePriority)
{
    Planet* body = giant(make_star());
    body->set_color_from_type(false);
    ASSERT_TRUE(body->has_spectral_giant_color());
    for (const auto& supplied : {json(0), json("0?"), json(1.15), json("1.15?")})
    {
        auto record = body->to_json();
        record["BV_color"] = supplied;
        Planet restored;
        ASSERT_TRUE(restored.from_json(record));
        EXPECT_TRUE(restored.lock_bv_color);
        EXPECT_FALSE(restored.has_spectral_giant_color());
        const double color = restored.BV_color;
        restored.set_color_from_type(false);
        EXPECT_DOUBLE_EQ(restored.BV_color, color);
        EXPECT_FALSE(restored.to_json().contains("giant_color_model"));
    }
    body->lock_bv_color = true;
    body->BV_color = .72;
    body->set_color_from_type(false);
    EXPECT_DOUBLE_EQ(body->BV_color, .72);
    EXPECT_FALSE(body->has_spectral_giant_color());
}

TEST_F(GiantReflectancePlanetTest, EstimatedTemperatureDoesNotCreateAColorFeedbackLoop)
{
    Star* host = make_star();
    Planet* body = giant(host);
    body->set_color_from_type(false);
    const auto first = body->giant_color_model;
    ASSERT_TRUE(first.is_object());
    for (double bv : {-1.0, 0.0, 2.0})
    {
        body->BV_color = bv;
        body->albedo = .9;
        body->set_color_from_type(false);
        EXPECT_EQ(body->giant_color_model, first);
    }
    body->temperature = 250;
    body->temperature_supplied = true;
    body->set_color_from_type(false);
    EXPECT_NE(body->giant_color_model.at("equivalent_axis_au"), first.at("equivalent_axis_au"));
    EXPECT_TRUE(body->giant_color_model.at("temperature_supplied").get<bool>());
}

TEST_F(GiantReflectancePlanetTest, EstimatedStellarCarbonAndSuppliedMethaneParticipate)
{
    Star* host = make_star();
    host->metallicity = .1;
    host->metallicity_ratio = "[Fe/H]";
    host->stellar_abundances = {{"Example", {{"elements",
        {{"C", {{"dex", ".4?"}, {"ratio", "[C/Fe]"}}},
         {"Fe", {{"dex", ".1?"}, {"ratio", "[Fe/H]"}}}}}}}};
    Planet* body = giant(host, ice_giant);
    body->ensure_atmosphere()->pressure_supplied = true;
    body->atm->ensure_composition()->CH4_portion = 0;
    body->atm->particulates = 0;
    body->set_color_from_type(false);
    ASSERT_TRUE(body->has_spectral_giant_color());
    EXPECT_TRUE(body->giant_color_model.at("stellar_carbon_used").get<bool>());
    EXPECT_TRUE(body->giant_color_model.at("methane_supplied").get<bool>());
    EXPECT_DOUBLE_EQ(body->giant_color_model.at("methane_absorption_scale"), 0);
    EXPECT_DOUBLE_EQ(body->giant_color_model.at("haze_scenario"), 0);
    EXPECT_EQ(host->stellar_abundances["Example"]["elements"]["C"]["dex"], ".4?");
}

TEST_F(GiantReflectancePlanetTest, HotPlanetPrescriptionsAndRockyWorldsStaySeparate)
{
    Planet* body = giant(make_star());
    body->type = clearskies;
    body->set_color_from_type(false);
    EXPECT_FALSE(body->has_spectral_giant_color());
    EXPECT_TRUE(body->giant_color_model.is_null());
    body->type = hot_jupiter;
    body->set_color_from_type(false);
    EXPECT_FALSE(body->has_spectral_giant_color());
    body->type = rocky;
    body->set_color_from_type(false);
    EXPECT_DOUBLE_EQ(body->BV_color, 1);
}

TEST_F(GiantReflectancePlanetTest, TextureColorsRetainInputAcrossPlanetFamilies)
{
    const bool previous_randomize = randomize_txgen;
    struct Restore
    {
        bool value;
        ~Restore() { randomize_txgen = value; }
    } restore{previous_randomize};
    randomize_txgen = false;
    for (double correction : {0.0, -.6244})
    {
        bv_correction = correction;
        for (auto type : {gas_giant, ice_giant, clearskies, hot_jupiter, rocky})
        {
            for (double input : (type == rocky ? std::vector<double>{.8, 1.1, 1.4}
                : std::vector<double>{.1, .7, 1.3}))
            {
                for (bool synchronous : {false, true})
                {
                    SCOPED_TRACE(::testing::Message() << "type=" << type << " BV=" << input
                        << " synchronous=" << synchronous);
                    Planet* body = giant(make_star(), type);
                    body->BV_color = input;
                    body->has_water = 0;
                    body->has_water_supplied = true;
                    body->has_sim_life = false;
                    body->temperature = type == rocky ? 320 : 1000;
                    body->temperature_supplied = true;
                    body->ensure_atmosphere()->surface_pressure = type == rocky ? 0 : oneatm;
                    body->atm->pressure_supplied = true;
                    body->fictitious_map_height = 32;
                    body->sidereal_rotational_period = synchronous ? body->orbit->period : oneday;
                    Map* map = new Map(body);
                    if (type == rocky)
                    {
                        body->surf_map = map;
                        map->generate_rocky_map(body);
                    }
                    else
                    {
                        body->cloud_map = map;
                        map->generate_gas_giant_map(body);
                    }
                    ASSERT_TRUE(map->is_complete());
                    ASSERT_FALSE(map->texture_color_exempt(body));
                    EXPECT_NEAR(map->texture_bv(body), input, .02);
                    EXPECT_DOUBLE_EQ(body->BV_color, map->texture_bv(body));
                }
            }
        }
    }
}

class PhotometryTestMap : public Map
{
public:
    PhotometryTestMap(RGB3 day, RGB3 night, double wet_fraction = 0, bool frozen = false)
    {
        image_width = 20;
        image_height = 4;
        allocated = image_width * image_height;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        surface_materials.assign(allocated, SurfaceMaterial::ground);
        procedurally_generated = true;
        for (unsigned long y = 0; y < image_height; ++y)
        {
            for (unsigned long x = 0; x < image_width; ++x)
            {
                const unsigned long i = y * image_width + x;
                const RGB3 color = x >= 5 && x < 15 ? day : night;
                red_data[i] = color.r;
                green_data[i] = color.g;
                blue_data[i] = color.b;
                if (x < std::round(image_width * wet_fraction))
                    surface_materials[i] = frozen && x % 2 ? SurfaceMaterial::ice : SurfaceMaterial::water;
            }
        }
        touch_gen();
    }
};

TEST_F(GiantReflectancePlanetTest, PhotometryDecodesGammaAndSelectsTheDaylightHemisphere)
{
    Planet* body = giant(make_star());
    body->sidereal_rotational_period = body->orbit->period;
    PhotometryTestMap map(RGB3(180, 160, 120), RGB3(40, 80, 200));
    // Without a location update, climate's default sun points along negative X.
    const double expected = default_gamma * log(160.0 / 120) * invlogmagnbase - bv_correction;
    EXPECT_NEAR(map.texture_bv(body), expected, 1e-12);
    body->sidereal_rotational_period = oneday;
    const double green = pow(160.0 / 255, default_gamma) + pow(80.0 / 255, default_gamma);
    const double blue = pow(120.0 / 255, default_gamma) + pow(200.0 / 255, default_gamma);
    EXPECT_NEAR(map.texture_bv(body), log(green / blue) * invlogmagnbase - bv_correction, 1e-12);
}

TEST_F(GiantReflectancePlanetTest, WaterAndIceTogetherAtTwentyPercentOrLifeExemptColorMatching)
{
    Planet* body = giant(make_star());
    body->has_sim_life = false;
    PhotometryTestMap dry(RGB3(180, 160, 120), RGB3(180, 160, 120), .15, true);
    PhotometryTestMap wet(RGB3(180, 160, 120), RGB3(180, 160, 120), .20, true);
    EXPECT_FALSE(dry.texture_color_exempt(body));
    EXPECT_TRUE(wet.texture_color_exempt(body));
    body->has_sim_life = true;
    EXPECT_TRUE(dry.texture_color_exempt(body));
    const double original = wet.texture_bv(body);
    wet.calibrate_texture_bv(body, original + .1);
    EXPECT_DOUBLE_EQ(wet.texture_bv(body), original);
}

TEST_F(GiantReflectancePlanetTest, RepeatedRefreshAndRegenerationKeepColorsStable)
{
    struct Restore
    {
        bool value = randomize_txgen;
        ~Restore() { randomize_txgen = value; }
    } restore;
    randomize_txgen = false;
    bv_correction = -.6244;
    for (auto type : {gas_giant, ice_giant, clearskies, hot_jupiter, rocky})
    {
        for (double input : {.85, 1.2})
        {
            for (bool moon : {false, true})
            {
                if (moon && type != rocky) continue;
                SCOPED_TRACE(::testing::Message() << "type=" << type << " BV=" << input << " moon=" << moon);
                Planet* body;
                if (moon)
                {
                    Moon* satellite = new Moon;
                    satellite->type = type;
                    satellite->mass = .01 * earth_mass;
                    satellite->volumetric_mean_radius = 1700000;
                    satellite->orbit = new Orbit;
                    satellite->orbit->center = giant(make_star());
                    satellite->orbit->semimajor_axis = 4e8;
                    satellite->orbit->compute_period(satellite->mass);
                    satellite->cenobj = satellite->orbit->center;
                    satellite->albedo = .3;
                    satellite->rnd_seed = 42;
                    append_cel(satellite);
                    body = satellite;
                }
                else body = giant(make_star(), type);
                body->BV_color = input;
                body->preserve_loaded_parameters = true;
                body->has_water = 0;
                body->has_water_supplied = true;
                body->has_sim_life = false;
                body->temperature = 320;
                body->temperature_supplied = true;
                body->ensure_atmosphere()->surface_pressure = 0;
                body->atm->pressure_supplied = true;
                body->fictitious_map_height = 32;
                body->sidereal_rotational_period = input > 1 ? body->orbit->period : oneday;
                body->ignore_map_files = true;
                for (int pass = 0; pass < 8; ++pass)
                {
                    SCOPED_TRACE(pass);
                    Map* map = uses_gaseous_map(type) ? body->cloud_map : body->surf_map;
                    if (map && type == rocky && pass % 2)
                    {
                        map->_map_resample_bump_regen_rocky(body);
                    }
                    else
                    {
                        delete body->surf_map;
                        delete body->cloud_map;
                        body->surf_map = body->cloud_map = nullptr;
                        body->ignore_map_files = true;
                        body->preserve_loaded_parameters = pass == 0;
                        body->rnd_seed = 42 + pass * 17;
                        load_textures(body);
                    }
                    map = uses_gaseous_map(type) ? body->cloud_map : body->surf_map;
                    ASSERT_NE(map, nullptr);
                    ASSERT_TRUE(map->is_complete());
                    EXPECT_NEAR(body->BV_color, input, .03);
                    EXPECT_DOUBLE_EQ(body->BV_color, map->texture_bv(body));
                }
            }
        }
    }
}

TEST_F(GiantReflectancePlanetTest, BlueBareRockRetainsNaturalPigmentsAndCorrectsItsPointColor)
{
    Planet* body = giant(make_star(), rocky);
    body->BV_color = -.2;
    body->has_water = 0;
    body->has_water_supplied = true;
    body->has_sim_life = false;
    body->temperature = 320;
    body->temperature_supplied = true;
    body->ensure_atmosphere()->surface_pressure = 0;
    body->atm->pressure_supplied = true;
    body->fictitious_map_height = 32;
    body->surf_map = new Map(body);
    body->surf_map->generate_rocky_map(body);
    ASSERT_TRUE(body->surf_map->is_complete());
    EXPECT_GE(body->BV_color, .78);
    EXPECT_DOUBLE_EQ(body->BV_color, body->surf_map->texture_bv(body));
}

TEST_F(GiantReflectancePlanetTest, WetAndLivingTexturesStillReplaceTheFinalColor)
{
    Planet* body = giant(make_star(), rocky);
    body->has_sim_life = true;
    body->BV_color = -.5;
    body->lock_bv_color = true;
    body->surf_map = new PhotometryTestMap(RGB3(180, 160, 120), RGB3(180, 160, 120), .4, true);
    ASSERT_TRUE(body->surf_map->texture_color_exempt(body));
    body->update_texture_color();
    EXPECT_DOUBLE_EQ(body->BV_color, body->surf_map->texture_bv(body));
    EXPECT_GT(body->BV_color, 0);
    delete body->surf_map;
    body->surf_map = new Map(body);
    const double previous = body->BV_color;
    body->update_texture_color();
    EXPECT_DOUBLE_EQ(body->BV_color, previous);
}

TEST_F(GiantReflectancePlanetTest, SpectralGiantTexturesRetainProvenanceAcrossRegenerationAndSaves)
{
    for (auto type : {gas_giant, ice_giant})
    {
        for (double axis : {2.0, 5.0})
        {
            Planet* body = giant(make_star(), type);
            body->orbit->semimajor_axis = axis * AU;
            body->set_color_from_type(false);
            ASSERT_TRUE(body->has_spectral_giant_color());
            const auto original = body->giant_color_model;
            body->preserve_loaded_parameters = true;
            body->fictitious_map_height = 32;
            body->sidereal_rotational_period = oneday;
            double first = NAN;
            for (int pass = 0; pass < 3; ++pass)
            {
                delete body->cloud_map;
                body->cloud_map = new Map(body);
                body->cloud_map->generate_gas_giant_map(body);
                ASSERT_TRUE(body->cloud_map->is_complete());
                EXPECT_TRUE(body->has_spectral_giant_color());
                EXPECT_DOUBLE_EQ(body->BV_color, body->cloud_map->texture_bv(body));
                EXPECT_EQ(body->giant_color_model["bands"], original["bands"]);
                if (!pass) first = body->BV_color;
                else EXPECT_NEAR(body->BV_color, first, .02);
                const Color actual = Color::color_from_planet(0, body);
                Color expected = body->cloud_map->mean_texture_color(body);
                EXPECT_NEAR(actual.blue / actual.green, expected.blue / expected.green, 1e-12);
                Planet restored;
                ASSERT_TRUE(restored.from_json(body->to_json()));
                EXPECT_TRUE(restored.has_spectral_giant_color());
                EXPECT_DOUBLE_EQ(restored.BV_color, body->BV_color);
                EXPECT_DOUBLE_EQ(restored.reflected_vr_color(), body->reflected_vr_color());
            }
        }
    }
}

TEST_F(GiantReflectancePlanetTest, ColdIceCrustCountsAsIceWithoutLiquidWater)
{
    Planet* body = giant(make_star(), icy);
    body->BV_color = .6;
    body->temperature = 90;
    body->temperature_supplied = true;
    body->has_water = 0;
    body->has_water_supplied = true;
    body->has_sim_life = false;
    body->preserve_loaded_parameters = true;
    body->ensure_atmosphere()->surface_pressure = 0;
    body->atm->pressure_supplied = true;
    body->fictitious_map_height = 32;
    body->surf_map = new Map(body);
    body->surf_map->generate_rocky_map(body);
    ASSERT_TRUE(body->surf_map->is_complete());
    EXPECT_TRUE(body->surf_map->texture_color_exempt(body));
    EXPECT_DOUBLE_EQ(body->BV_color, body->surf_map->texture_bv(body));
}

TEST_F(GiantReflectancePlanetTest, ObservedColorsSurviveBothSaveFormatsAndTextureReplacement)
{
    for (bool moon : {false, true})
    {
        for (double measured : {0.0, .823456789012345})
        {
            std::unique_ptr<Planet> original(moon ? static_cast<Planet*>(new Moon) : new Planet);
            original->type = rocky;
            original->BV_color = measured;
            original->is_bv_observed = true;
            for (bool readable : {false, true})
            {
                SCOPED_TRACE(::testing::Message() << "moon=" << moon << " BV=" << measured << " readable=" << readable);
                json record = original->to_json();
                if (readable) record = Serialization::expand_output_record(Serialization::output_record(record));
                std::unique_ptr<Planet> restored(moon ? static_cast<Planet*>(new Moon) : new Planet);
                ASSERT_TRUE(restored->from_json(record));
                ASSERT_TRUE(restored->is_bv_observed);
                EXPECT_DOUBLE_EQ(restored->BV_color, measured);
                restored->surf_map = new PhotometryTestMap(RGB3(220, 170, 110), RGB3(220, 170, 110));
                ASSERT_NE(restored->surf_map->texture_bv(restored.get()), measured);
                restored->update_texture_color();
                EXPECT_DOUBLE_EQ(restored->BV_color, measured);
                restored->set_color_from_type(false);
                EXPECT_DOUBLE_EQ(restored->BV_color, measured);
            }
        }
    }
}

TEST_F(GiantReflectancePlanetTest, SavedRenderingCopiesCannotPublishOverObservedPhotometry)
{
    Planet* body = giant(make_star(), rocky);
    body->BV_color = .823456789012345;
    body->is_bv_observed = true;
    body->lock_bv_color = false;
    body->preserve_loaded_parameters = true;
    body->has_water = 0;
    body->has_water_supplied = true;
    body->temperature = 320;
    body->temperature_supplied = true;
    body->ensure_atmosphere()->surface_pressure = 0;
    body->atm->pressure_supplied = true;
    body->fictitious_map_height = 32;
    const double measured = body->BV_color;
    body->surf_map = new Map(body);
    body->surf_map->generate_rocky_map(body);
    ASSERT_TRUE(body->surf_map->is_complete());
    EXPECT_DOUBLE_EQ(body->BV_color, measured);
    EXPECT_TRUE(render_saved_planet(body, [](CelestialObject* working)
    {
        EXPECT_TRUE(working->is_bv_observed);
        working->BV_color = -.3;
    }));
    EXPECT_DOUBLE_EQ(body->BV_color, measured);
    body->type = gas_giant;
    EXPECT_FALSE(body->estimate_cool_giant_color());
    EXPECT_DOUBLE_EQ(body->BV_color, measured);
}

TEST_F(GiantReflectancePlanetTest, ObservedStatusResetsOnReloadAndAcceptsUncertainZero)
{
    Planet body;
    body.is_bv_observed = true;
    ASSERT_TRUE(body.from_json({{"BV_color", .9}}));
    EXPECT_FALSE(body.is_bv_observed);
    ASSERT_TRUE(body.from_json({{"BV_color", "0?"}, {"is_bv_observed", true}}));
    EXPECT_TRUE(body.is_bv_observed);
    EXPECT_DOUBLE_EQ(body.BV_color, 0);
    ASSERT_TRUE(body.from_json({{"BV_color", "?"}, {"is_bv_observed", true}}));
    EXPECT_FALSE(body.is_bv_observed);
}

TEST_F(GiantReflectancePlanetTest, SuppliedSurfaceOrCloudTexturesRetainTheOriginalColor)
{
    Planet* body = giant(make_star(), rocky);
    body->BV_color = .123;
    body->surf_map = new PhotometryTestMap(RGB3(180, 160, 120), RGB3(180, 160, 120));
    body->cloud_map = new PhotometryTestMap(RGB3(220, 220, 220), RGB3(220, 220, 220));
    body->transparent_clouds = false;
    for (bool supplied_surface : {false, true})
    {
        body->surf_map->procedurally_generated = !supplied_surface;
        body->cloud_map->procedurally_generated = supplied_surface;
        body->update_texture_color();
        EXPECT_DOUBLE_EQ(body->BV_color, .123);
    }
}
