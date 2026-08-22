#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/moon.h"

using namespace alienorum;
using json = nlohmann::json;

// Helper macro to compare Rotation objects
#define EXPECT_ROTATION_EQ(rot1, rot2) \
    EXPECT_DOUBLE_EQ((rot1).a, (rot2).a); \
    EXPECT_DOUBLE_EQ((rot1).v.x, (rot2).v.x); \
    EXPECT_DOUBLE_EQ((rot1).v.y, (rot2).v.y); \
    EXPECT_DOUBLE_EQ((rot1).v.z, (rot2).v.z)

// =====================================================================
// Moon Initialization Tests
// =====================================================================

TEST(MoonTest, ConstructorSetsCorrectClass)
{
    Moon m;
    
    // Verify the constructor set the class internally
    EXPECT_EQ(m.typeclass(), class_moon);
    
    // Check initialized triaxial dimensions
    EXPECT_DOUBLE_EQ(m.height, 0.0);
    EXPECT_DOUBLE_EQ(m.width, 0.0);
    EXPECT_DOUBLE_EQ(m.depth, 0.0);
    EXPECT_FALSE(m.major_moon);
    EXPECT_EQ(m.orbit_type, ot_Laplace);
}

// =====================================================================
// Moon Serialization (JSON) & Unit Scaling Tests
// =====================================================================

TEST(MoonTest, JsonSerialization_ScalesMetersToKilometers)
{
    Moon original;
    
    // Set internally in meters (e.g., 1500 km)
    original.height = 1500000.0;
    original.width = 1600000.0;
    original.depth = 1450000.0;

    json j = original.to_json();
    
    // Verify the JSON object holds the values in kilometers
    EXPECT_DOUBLE_EQ(j["height"], 1500.0);
    EXPECT_DOUBLE_EQ(j["width"], 1600.0);
    EXPECT_DOUBLE_EQ(j["depth"], 1450.0);

    Moon restored;
    bool success = restored.from_json(j);
    
    EXPECT_TRUE(success);
    
    // Verify the restored object converted them back to meters
    EXPECT_DOUBLE_EQ(restored.height, 1500000.0);
    EXPECT_DOUBLE_EQ(restored.width, 1600000.0);
    EXPECT_DOUBLE_EQ(restored.depth, 1450000.0);
}

TEST(MoonTest, JsonSerialization_IgnoresNearZeroValues)
{
    Moon original;
    
    // Set to extremely small values (assuming zero_isnt_really_zero is a small epsilon like 1e-6)
    original.height = 0.0;
    original.width = 0.000000001; 
    original.depth = 0.0;

    json j = original.to_json();
    
    // The keys should not exist in the JSON output
    EXPECT_FALSE(j.contains("height"));
    EXPECT_FALSE(j.contains("width"));
    EXPECT_FALSE(j.contains("depth"));
}

// =====================================================================
// Moon Laplace Plane Guards & Logic Tests
// =====================================================================

TEST(MoonTest, LaplacePlane_MissingHierarchy)
{
    Moon orphan_moon;
    
    // With no orbit or parent planet, it should safely return its own orbital plane
    Rotation expected = orphan_moon.location.orbital_plane;
    Rotation actual = orphan_moon.get_Laplace_plane(); // Assuming friend class access
    
    EXPECT_ROTATION_EQ(actual, expected);
}

TEST(MoonTest, LaplacePlane_OrbitTypeOverrides)
{
    CelestialObject star;
    Planet planet;
    Moon moon;
    
    Orbit planet_orbit;
    planet_orbit.center = &star;
    planet.orbit = &planet_orbit;
    
    Orbit moon_orbit;
    moon_orbit.center = &planet;
    moon.orbit = &moon_orbit;

    // Distinctive mock rotations to trace
    planet.location.equatorial_plane.a = 1.23;
    planet.location.orbital_plane.a = 4.56;

    // Equatorial override
    moon.orbit_type = ot_equatorial;
    Rotation actual_eq = moon.get_Laplace_plane();
    EXPECT_DOUBLE_EQ(actual_eq.a, 1.23);

    // Ecliptic override
    moon.orbit_type = ot_ecliptic;
    Rotation actual_ecl = moon.get_Laplace_plane();
    EXPECT_DOUBLE_EQ(actual_ecl.a, 4.56);
}

TEST(MoonTest, LaplacePlane_TidalLockGuard)
{
    CelestialObject star;
    Planet planet;
    Moon moon;
    
    Orbit planet_orbit;
    planet_orbit.center = &star;
    planet.orbit = &planet_orbit;
    
    Orbit moon_orbit;
    moon_orbit.center = &planet;
    moon.orbit = &moon_orbit;
    
    moon.orbit_type = ot_Laplace;
    planet.location.equatorial_plane.a = 7.89; 

    // Simulate tidal lock between planet and moon
    planet.sidereal_rotational_period = 3600.0;
    moon_orbit.period = 3600.0;
    
    Rotation actual = moon.get_Laplace_plane();
    
    // Should bypass mass/Laplace math and return the planet's equatorial plane
    EXPECT_DOUBLE_EQ(actual.a, 7.89);
}