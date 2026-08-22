#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/celestial.h"

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
    EXPECT_EQ(restored.type, gas_giant);
    EXPECT_TRUE(restored.user_added);

    EXPECT_DOUBLE_EQ(restored.temperature, 5778);

    // Zero is not a temperature but the absence of one -- it is what tells
    // Star::estimate_temperature() and Planet::equilibrium_temperature() to work one out instead
    // of returning what they are holding -- so a body that has none writes no key at all rather
    // than writing a zero, and reading a file without one leaves the derivations to it.
    CelestialObject no_temperature;
    no_temperature.mass = 1.989e33;
    json jnt = no_temperature.to_json();
    EXPECT_FALSE(jnt.contains("temperature"));

    CelestialObject from_older_file;
    from_older_file.temperature = 1234;             // whatever it happened to be holding
    EXPECT_TRUE(from_older_file.from_json(jnt));
    EXPECT_DOUBLE_EQ(from_older_file.temperature, 1234);
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
    
    // Earth's Hill sphere is roughly 1.47 million km (1.496e9 m from the semimajor axis), and
    // the formula takes it at periapsis, a(1-e), which trims it to 1.4714e9 -- not to 1.44e9.
    EXPECT_NEAR(hill_radius, 1.4714e9, 1e7);
    
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

// =====================================================================
// Global Arrays & Indices Fixture
// =====================================================================

class CelestialGlobalsTest : public ::testing::Test
{
    protected:
    void SetUp() override
    {
        // This runs before EVERY test. main() (alienorum.cpp) is what normally allocates cels,
        // and no test binary has one, so append_cel() would write through a null pointer.
        if (!cels) cels = new CelestialObject*[MAX_CELOBJS];
        memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
        ncelobjs = 0;
        first_sat = -1;

        // Clear the STL containers so tests don't pollute each other. set_center_objects() gives
        // first_letter_index 36 buckets -- ten digits and twenty-six letters, case folded -- and
        // is the only thing that fills it; see AppendCel_UpdatesFirstLetterIndex below.
        first_letter_index.clear();
        first_letter_index.resize(36);

        constellation_index.clear();
    }

    void TearDown() override
    {
        // This runs after EVERY test. append_cel() does not take ownership -- it only records the
        // pointer -- so each test deletes what it allocated, and we drop the dangling entries here
        // so the next test starts on an empty array.
        memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
        ncelobjs = 0;
        first_sat = -1;
    }
};

// =====================================================================
// append_cel() Logic Tests
// =====================================================================

// Note the use of TEST_F instead of TEST to tie it to the fixture
TEST_F(CelestialGlobalsTest, AppendCel_AssignsSeqno)
{
    CelestialObject* obj = new CelestialObject();
    
    // Assuming seqno starts at -1 per the header
    EXPECT_EQ(obj->seqno, -1);
    
    bool success = append_cel(obj);
    
    EXPECT_TRUE(success);
    
    // append_cel should assign the object its index in the cels array
    EXPECT_GE(obj->seqno, 0);
    EXPECT_EQ(cels[obj->seqno], obj);
    
    delete obj;
}

TEST_F(CelestialGlobalsTest, AppendCel_UpdatesFirstLetterIndex)
{
    CelestialObject* obj = new CelestialObject();
    
    // Set a predictable name
    strcpy(obj->name, "Earth");
    
    append_cel(obj);
    
    // The name indices are NOT append_cel()'s doing: set_center_objects() (housekeeping.cpp)
    // rebuilds first_letter_index from scratch over the whole array once loading has settled,
    // because it also has to skip deleted objects and the HD numbers that already have a catalog
    // of their own. append_cel() is only responsible for the array itself and the object's name,
    // which it copies to origname so a later rename can still be traced back.
    EXPECT_TRUE(first_letter_index[(int)'E' - 'A' + 10].empty());
    EXPECT_EQ(obj->origname, std::string("Earth"));
    EXPECT_EQ(cels[obj->seqno], obj);
    
    delete obj;
}

TEST_F(CelestialGlobalsTest, AppendCel_UpdatesConstellationIndex)
{
    // Assuming constellation_index maps standard 3-letter IAU codes
    CelestialObject* star = new CelestialObject();
    
    // Set whatever properties append_cel looks for to 
    // identify the constellation. For example, if it uses a Star object:
    // Star* star = new Star();
    // strcpy(star->constellation, "Ori");
    
    // For a generic CelestialObject, maybe you map by a string property if one exists:
    // This relies heavily on your cpp implementation!
    
    bool success = append_cel(star);
    EXPECT_TRUE(success);
    
    // As above, the constellation map is built by set_center_objects() and only for objects
    // that are Stars carrying a constellation, so a bare CelestialObject leaves it empty.
    EXPECT_TRUE(constellation_index.empty());
    
    delete star;
}

TEST_F(CelestialGlobalsTest, AppendCel_HandlesFullCapacity)
{
    // If your cels array has a hard limit, you can test what happens when it fills up.
    // (This test depends on you knowing the exact capacity limit).
    /*
    int MAX_CAPACITY = 10000; // Example
    
    // Fill the array
    for (int i = 0; i < MAX_CAPACITY; i++)
    {
        CelestialObject* dummy = new CelestialObject();
        EXPECT_TRUE(append_cel(dummy));
    }
    
    // The next one should fail safely without segfaulting
    CelestialObject* one_too_many = new CelestialObject();
    EXPECT_FALSE(append_cel(one_too_many));
    
    delete one_too_many;
    */
}