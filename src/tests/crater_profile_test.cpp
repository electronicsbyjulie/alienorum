#include <gtest/gtest.h>
#include "../classes/craterprofile.h"
#include "../classes/planet.h"
#include "crater_lighting_fixture.h"

using namespace alienorum;

TEST(CraterProfileRegression, DepthTracksDiameterAndLargeCratersBecomeProportionallyShallower)
{
    EXPECT_NEAR(CraterProfile::depth(1000), 196, 1);
    EXPECT_GT(CraterProfile::depth(100000), 3500);
    EXPECT_LT(CraterProfile::depth(100000), 5000);
    EXPECT_LT(CraterProfile::depth(100000) / 100000, CraterProfile::depth(1000) / 1000);
    EXPECT_GT(CraterProfile::transition_diameter(1.62, false), CraterProfile::transition_diameter(9.81, false));
    EXPECT_LT(CraterProfile::transition_diameter(1.62, true), CraterProfile::transition_diameter(1.62, false));
}

TEST(CraterProfileRegression, BowlWallAndRimJoinWithoutAHeightOrSlopeJump)
{
    constexpr double epsilon = 1e-5;
    for (double peak : {0.0, .3})
    {
        const auto profile = [peak](double radius) { return CraterProfile::elevation(radius, 1, .2, .2, peak); };
        EXPECT_LT(profile(0), -.6);
        EXPECT_GT(profile(1), .19);
        EXPECT_NEAR(profile(1 - epsilon), profile(1 + epsilon), 1e-6);
        const double left = (profile(1) - profile(1 - epsilon)) / epsilon;
        const double right = (profile(1 + epsilon) - profile(1)) / epsilon;
        EXPECT_NEAR(left, right, .002);
    }
}

TEST(CraterProfileRegression, EjectaAlbedoDoesNotPaintEveryRimBright)
{
    EXPECT_DOUBLE_EQ(CraterProfile::rim_albedo(1, .2, 0, 1), 1);
    EXPECT_DOUBLE_EQ(CraterProfile::rim_albedo(1, .2, 1, 0), 1);
    EXPECT_LE(CraterProfile::rim_albedo(1, .2, 1, 1), 1.025);
}

TEST(CraterProfileRegression, StampedLargeBasinsHaveReliefBeyondTheBackgroundNoiseAmplitude)
{
    Planet planet;
    planet.mass = 7.342e25;
    planet.volumetric_mean_radius = 1737000;
    planet.type = rocky;
    planet.ensure_atmosphere()->surface_pressure = 0;
    planet.rng.seed(123456);
    CraterLightingMap map(256, false, true);
    const double scale = planet.estimate_bump_scale();
    map.stamp_craters(&planet, scale);
    std::vector<float> heights(map.get_width() * map.get_height());
    map.export_bump(heights.data());
    EXPECT_LT(*std::min_element(heights.begin(), heights.end()), -1.5 * scale);
    EXPECT_GT(*std::max_element(heights.begin(), heights.end()), .1 * scale);
}
