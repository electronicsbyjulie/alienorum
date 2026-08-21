#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/celestial.h" // Replace with your actual header name

using namespace alienorum;
using json = nlohmann::json;

// =====================================================================
// Global / Helper Tests
// =====================================================================

TEST(CelestialHelperTest, UsesRockyMapEvaluation)
{
    EXPECT_TRUE(uses_rocky_map(rocky));
    EXPECT_TRUE(uses_rocky_map(icy));
    EXPECT_TRUE(uses_rocky_map(waterworld));
    EXPECT_TRUE(uses_rocky_map(lavaworld));

    // Counter-cases
    EXPECT_FALSE(uses_rocky_map(gas_giant));
    EXPECT_FALSE(uses_rocky_map(star));
    EXPECT_FALSE(uses_rocky_map(galaxy));
}

// =====================================================================
// Atmosphere & AtmosphereComposition Tests
// =====================================================================

TEST(AtmosphereTest, CompositionEnforcesIntegrity)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Set some ridiculous values
    comp.O2_portion = 1.5;
    comp.N2_portion = -0.5;

    // Assuming enforce_integrity() clamps values between 0.0 and 1.0
    // and normalizes the sum to 1.0
    comp.enforce_integrity();

    EXPECT_GE(comp.O2_portion, 0.0);
    EXPECT_GE(comp.N2_portion, 0.0);
    EXPECT_LE(comp.O2_portion, 1.0);

    // The total of all portions should equal 1.0
    double total = comp.H2_portion + comp.He_portion + comp.N2_portion +
                   comp.O2_portion + comp.CO2_portion; // ... include all
    EXPECT_NEAR(total, 1.0, 1e-5);
}

TEST(AtmosphereTest, ManagesCompositionPointer)
{
    CelestialObject dummy;
    Atmosphere atm(&dummy);

    EXPECT_EQ(atm.comp, nullptr); // Should start null

    AtmosphereComposition* ensured = atm.ensure_composition();
    ASSERT_NE(ensured, nullptr);
    EXPECT_EQ(atm.comp, ensured);
    EXPECT_EQ(ensured->cel, &dummy);

    // Ensure calling it again returns the same pointer and doesn't leak
    AtmosphereComposition* ensured_again = atm.ensure_composition();
    EXPECT_EQ(ensured, ensured_again);
}

// =====================================================================
// Orbit Tests
// =====================================================================

TEST(OrbitTest, OpenVersusClosedOrbits)
{
    Orbit closed_orbit;
    closed_orbit.eccentricity = 0.016; // Earth-like
    EXPECT_FALSE(closed_orbit.is_open());

    Orbit parabolic_orbit;
    parabolic_orbit.eccentricity = 1.0;
    EXPECT_TRUE(parabolic_orbit.is_open());

    Orbit hyperbolic_orbit;
    hyperbolic_orbit.eccentricity = 1.5; // Oumuamua style
    EXPECT_TRUE(hyperbolic_orbit.is_open());
}

// =====================================================================
// Map Tests
// =====================================================================

TEST(MapTest, GenerationCounterTracksUpdates)
{
    Map m;
    EXPECT_EQ(m.gen, 0); // Reserved for "untouched"

    m.touch_gen();
    unsigned int first_gen = m.gen;
    EXPECT_NE(first_gen, 0);

    m.touch_gen();
    EXPECT_NE(m.gen, first_gen); // Must increment/change
}

TEST(MapTest, DataPresenceFlags)
{
    Map m;
    // Before loading anything
    EXPECT_FALSE(m.has_bump_data());
    EXPECT_FALSE(m.has_rgb_data());
    EXPECT_EQ(m.get_width(), 0);
    EXPECT_EQ(m.get_height(), 0);
}

// =====================================================================
// CelestialObject Tests
// =====================================================================

TEST(CelestialObjectTest, DefaultInitialization)
{
    CelestialObject cel;

    EXPECT_EQ(cel.seqno, -1);
    EXPECT_FALSE(cel.deleted);
    EXPECT_EQ(cel.type, star);
    EXPECT_EQ(cel.typeclass(), class_unknown);

    // Pointers should be null
    EXPECT_EQ(cel.surf_map, nullptr);
    EXPECT_EQ(cel.cloud_map, nullptr);
    EXPECT_EQ(cel.orbit, nullptr);
    EXPECT_EQ(cel.cenobj, nullptr);
}

TEST(CelestialObjectTest, TidalLockingEvaluation)
{
    CelestialObject star;
    CelestialObject planet;
    Orbit planet_orbit;

    // Link them
    planet.orbit = &planet_orbit;
    planet_orbit.center = &star;

    // Case 1: Perfectly tidally locked
    planet_orbit.period = 3600.0;
    planet.sidereal_rotational_period = 3600.0;
    EXPECT_TRUE(planet.is_tidal_locked());

    // Case 2: Within 1% tolerance (from the inline header logic)
    planet.sidereal_rotational_period = 3630.0; // ~0.8% diff
    EXPECT_TRUE(planet.is_tidal_locked());

    // Case 3: Not tidally locked (Earth-like, fast rotation relative to orbit)
    planet.sidereal_rotational_period = 100.0;
    EXPECT_FALSE(planet.is_tidal_locked());

    // Case 4: No orbit (Rogue planet or Star)
    planet.orbit = nullptr;
    EXPECT_FALSE(planet.is_tidal_locked());
}

TEST(CelestialObjectTest, DensityCalculation)
{
    CelestialObject cel;
    // Set to roughly Earth mass and radius (in grams and meters per your header)
    cel.mass = 5.972e27;
    cel.volumetric_mean_radius = 6.371e6;

    // Density should be roughly 5514 kg/m^3
    // Note: You will have to check if your density() function returns kg/m^3 or g/cm^3
    double calculated_density = cel.density();
    EXPECT_GT(calculated_density, 0.0);
}

TEST(CelestialObjectTest, JsonSerializationRoundTrip)
{
    CelestialObject original;
    original.mass = 1.989e33; // Solar mass in grams
    original.temperature = 5778;
    original.type = gas_giant;
    original.user_added = true;

    json j = original.to_json();

    CelestialObject restored;
    bool success = restored.from_json(j);

    EXPECT_TRUE(success);
    EXPECT_DOUBLE_EQ(restored.mass, 1.989e33);
    EXPECT_DOUBLE_EQ(restored.temperature, 5778);
    EXPECT_EQ(restored.type, gas_giant);
    EXPECT_TRUE(restored.user_added);
}

// =====================================================================
// Math Implementations
// =====================================================================

TEST(CelestialMathTest, DistanceFromMagnitudes)
{
    // The sun at 10 parsecs has apparent mag == absolute mag
    double dist = CelestialObject::distance_from_magnitudes(4.83, 4.83);
    
    // Result should be exactly 10 parsecs
    // Adjust "parsec * 10" below if your 'parsec' constant is scoped differently
    EXPECT_NEAR(dist, parsec * 10.0, 1.0);

    // Sirius: Apparent -1.46, Absolute 1.43
    // Distance modulus m - M = -2.89. Distance should be ~2.64 parsecs.
    double dist_sirius = CelestialObject::distance_from_magnitudes(-1.46, 1.43);
    EXPECT_NEAR(dist_sirius, parsec * 2.642, parsec * 0.01);
}

TEST(CelestialMathTest, DensityCalculation)
{
    CelestialObject earth;
    
    // Set to Earth's mass in grams and radius in meters
    earth.mass = 5.9722e27;
    earth.volumetric_mean_radius = 6.371e6; 
    
    double actual_density = earth.density();
    
    // Earth's average density is roughly 5.514 g/cm^3
    EXPECT_NEAR(actual_density, 5.514, 0.05);

    // Edge case: zero radius or mass
    earth.volumetric_mean_radius = 0;
    EXPECT_DOUBLE_EQ(earth.density(), 0.0);
}

TEST(CelestialMathTest, HillSphereRadius)
{
    CelestialObject sun;
    sun.mass = 1.989e33; // grams

    CelestialObject earth;
    earth.mass = 5.9722e27; // grams
    
    Orbit earth_orbit;
    earth_orbit.center = &sun;
    earth_orbit.semimajor_axis = 1.496e11; // 1 AU in meters
    earth_orbit.eccentricity = 0.0167;     // Earth eccentricity

    earth.orbit = &earth_orbit;

    double hill_radius = earth.Hill_sphere_radius();
    
    // Earth's Hill sphere is roughly 1.47 million km (1.47e9 meters).
    // Because your formula uses periapsis (1-e), it will be slightly smaller (~1.44e9).
    EXPECT_NEAR(hill_radius, 1.44e9, 1e7);
    
    // Edge case: Unbound orbit
    earth_orbit.eccentricity = 1.5;
    EXPECT_DOUBLE_EQ(earth.Hill_sphere_radius(), 0.0);
}

TEST(CelestialMathTest, RocheLimit)
{
    CelestialObject earth;
    earth.mass = 5.9722e27; // grams
    earth.volumetric_mean_radius = 6.371e6; // meters
    
    // Using a fluid satellite (e.g., a waterworld)
    CelestialObject fluid_moon;
    fluid_moon.mass = 7.342e25; // Moon mass in grams
    fluid_moon.volumetric_mean_radius = 1.737e6; // Moon radius in meters
    fluid_moon.type = waterworld; 
    
    double roche_fluid = earth.Roche_limit(&fluid_moon);
    
    // Fluid Roche limit for Earth-Moon densities is roughly 18,000 km (1.8e7 meters)
    EXPECT_NEAR(roche_fluid, 1.83e7, 1e6);
    
    // Using a rigid satellite (e.g., rocky)
    CelestialObject rigid_moon;
    rigid_moon.mass = 7.342e25; 
    rigid_moon.volumetric_mean_radius = 1.737e6;
    rigid_moon.type = rocky;
    
    double roche_rigid = earth.Roche_limit(&rigid_moon);
    
    // Rigid Roche limit should be significantly smaller (constant is ~1.26 vs ~2.44)
    EXPECT_LT(roche_rigid, roche_fluid);
    
    // Null orbiter defaults to rigid lunar density
    double roche_null = earth.Roche_limit(nullptr);
    EXPECT_GT(roche_null, 0.0);
}

TEST(CelestialMathTest, EstimateSurfaceGravity)
{
    CelestialObject earth;
    // Set to exactly 1 Earth mass and 1 Earth radius
    // Modify earth_mass and earth_radius below to match your actual variables
    earth.mass = earth_mass; 
    earth.volumetric_mean_radius = earth_radius;
    
    double gravity = earth.estimate_surface_gravity();
    
    // Should be exactly 1.0 Gs
    EXPECT_DOUBLE_EQ(gravity, 1.0);
    
    // Planet with 2x mass and same radius should have 2.0 Gs
    CelestialObject heavy_planet;
    heavy_planet.mass = earth_mass * 2.0;
    heavy_planet.volumetric_mean_radius = earth_radius;
    
    EXPECT_DOUBLE_EQ(heavy_planet.estimate_surface_gravity(), 2.0);
}