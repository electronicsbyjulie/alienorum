#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "../classes/giantreflectance.h"
#include "../classes/color.h"
#include "../classes/giantclouds.h"
#include "universe_fixture.h"

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
