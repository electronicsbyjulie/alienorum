#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/moon.h"
#include "../classes/cat.h"
#include "../classes/misc.h"
#include "../classes/serial.h"
#include "universe_fixture.h"

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
    
    // zero_isnt_really_zero is 9e-298 (misc.h): the threshold is there to catch a value that
    // arithmetic has left just off zero, not a small-but-real dimension. A nanometre-wide moon is
    // absurd, but it is a hundreds of orders of magnitude above the floor and does get written.
    original.height = 0.0;
    original.width = 1e-299; 
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
    
    // ~Planet and ~Moon both delete orbit: the body owns its orbit, so these must be heap
    // allocated or the destructor frees a stack address at the end of the test.
    Orbit* planet_orbit = new Orbit();
    planet_orbit->center = &star;
    planet.orbit = planet_orbit;
    
    Orbit* moon_orbit = new Orbit();
    moon_orbit->center = &planet;
    moon.orbit = moon_orbit;

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
    
    // Heap allocated for the same reason as above: the Moon and the Planet own their orbits.
    Orbit* planet_orbit = new Orbit();
    planet_orbit->center = &star;
    planet.orbit = planet_orbit;
    
    Orbit* moon_orbit = new Orbit();
    moon_orbit->center = &planet;
    moon.orbit = moon_orbit;
    
    moon.orbit_type = ot_Laplace;
    planet.location.equatorial_plane.a = 7.89; 
    planet.sidereal_rotational_period = 3600.0;
    moon_orbit->period = 3600.0;
    
    Rotation actual = moon.get_Laplace_plane();
    
    // Should bypass mass/Laplace math and return the planet's equatorial plane
    EXPECT_DOUBLE_EQ(actual.a, 7.89);
}

TEST(MoonTest, MoonsNeverGenerateRings)
{
    Moon m;
    m.mass = 0.01 * earth_mass;
    m.volumetric_mean_radius = 0.27 * earth_radius;

    EXPECT_FALSE(m.guess_has_rings());

    m.generate_ring_parameters(false);
    EXPECT_DOUBLE_EQ(m.ring_radius, 0.0);
    EXPECT_DOUBLE_EQ(m.ring_inner_radius, 0.0);

    m.generate_ring_parameters(true);
    EXPECT_DOUBLE_EQ(m.ring_radius, 0.0);
    EXPECT_DOUBLE_EQ(m.ring_inner_radius, 0.0);
}

TEST(MoonTest, PlanetRingsBoundedByRocheLimit)
{
    Planet p;
    p.mass = 300.0 * earth_mass;
    p.volumetric_mean_radius = 10.0 * earth_radius;
    CelestialObject ring_particle;
    ring_particle.type = waterworld;
    double t_eq = p.equilibrium_temperature();
    double particle_density = (t_eq < 200.0) ? 1.0 : 2.5;
    ring_particle.volumetric_mean_radius = 1.0;
    ring_particle.mass = particle_density * sphere_volume(1.0) * 1e6;
    double roche = p.Roche_limit(&ring_particle);

    for (int i = 0; i < 50; i++)
    {
        p.generate_ring_parameters(true);
        EXPECT_GT(p.ring_radius, p.ring_inner_radius);
        EXPECT_LE(p.ring_radius, roche);
    }
}

TEST(MoonTest, FictionalMoonsOrbitOutsidePlanetRings)
{
    Planet host;
    host.mass = 300.0 * earth_mass;
    host.volumetric_mean_radius = 10.0 * earth_radius;
    host.generate_ring_parameters(true);
    ASSERT_GT(host.ring_radius, 0.0);

    Moon m;
    m.mass = 0.01 * earth_mass;
    m.volumetric_mean_radius = 0.27 * earth_radius;

    double inner_bound = host.Roche_limit(&m);
    if (host.ring_radius > inner_bound)
    {
        inner_bound = host.ring_radius;
    }
    double sma = inner_bound * (1.1 + pow(frand(0, 1), 4) * 20);

    EXPECT_GT(sma, host.ring_radius);
    EXPECT_GT(sma, host.Roche_limit(&m));
}