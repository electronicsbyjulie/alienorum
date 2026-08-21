#include <gtest/gtest.h>
#include <nlohmann/json.hpp> // Assuming nlohmann JSON is used
#include "../classes/star.h" // Replace with your actual header file name

using namespace alienorum;
using json = nlohmann::json;

// =====================================================================
// Star Initialization & Basic Property Tests
// =====================================================================

TEST(StarTest, DefaultConstructorInitializesCorrectly)
{
    Star s;

    // Check initial kinematics
    EXPECT_DOUBLE_EQ(s.proper_motion_RA, 0.0);
    EXPECT_DOUBLE_EQ(s.proper_motion_decl, 0.0);
    EXPECT_DOUBLE_EQ(s.radial_velocity, 0.0);
    EXPECT_DOUBLE_EQ(s.parallax, 0.0);

    // Check catalog defaults
    EXPECT_EQ(s.BayerGrkno, -1);
    EXPECT_EQ(s.FlamsteedNo, -1);
    EXPECT_EQ(s.GouldNo, -1);
    EXPECT_EQ(s.HR, 0);
    EXPECT_EQ(s.HD, 0);
    EXPECT_EQ(s.HIP, 0);

    // Check boolean flags
    EXPECT_FALSE(s.is_eclipsing_binary);
    EXPECT_FALSE(s.is_orbit_multiple);
    EXPECT_FALSE(s.has_custom_name);
    EXPECT_FALSE(s.has_disk);
    EXPECT_EQ(s.has_planets, 0);

    // Multi-system pointer should be null initially
    EXPECT_EQ(s.multisys, nullptr);
}

// =====================================================================
// Constellation and Naming Tests
// =====================================================================

TEST(StarTest, ConstellationMatching)
{
    Star s;
    // Note: Assuming constellation char array is standard 3-letter IAU abbreviation + null terminator
    s.constellation[0] = 'O';
    s.constellation[1] = 'r';
    s.constellation[2] = 'i';
    s.constellation[3] = '\0';

    EXPECT_TRUE(s.matches_constellation("Ori"));
    EXPECT_FALSE(s.matches_constellation("Tau"));
}

TEST(StarTest, NameGeneration)
{
    Star s;
    s.gotta_be_named_something();
    // Assuming this function generates a fallback name if none exists.
    // E.g., populating s.alienorumid or similar.
    EXPECT_FALSE(s.alienorumid.empty());
}

// =====================================================================
// Astrophysical Calculations & Estimations
// =====================================================================

TEST(StarTest, DegenerateRadiusCalculation)
{
    // 1 Solar Mass in kg (approx 1.989 x 10^30)
    double solar_mass_kg = 1.989e30;

    // White dwarf radius for 1 solar mass should be roughly Earth-sized (~6.4e6 meters)
    double radius = Star::degenerate_radius(solar_mass_kg);

    // Using EXPECT_NEAR since astrophysical calculations are approximations
    EXPECT_NEAR(radius, 6.4e6, 1.0e6); // Tolerance of 1,000 km
}

TEST(StarTest, TemperatureFromBV)
{
    // BV index of 0.0 generally correlates to ~10,000K (A0V star like Vega)
    double temp = Star::temperature_from_BV(0.0);
    EXPECT_NEAR(temp, 10000.0, 500.0);

    // BV index of 0.65 correlates roughly to our Sun (~5,778K)
    double l_sun_temp = Star::temperature_from_BV(0.65);
    EXPECT_NEAR(l_sun_temp, 5778.0, 300.0);
}

TEST(StarTest, LimbDarkeningCoefficients)
{
    Star s;
    // Assuming spectral type / temperature is set prior to testing
    // s.spectral_type = "G2V";
    double a, b;
    s.limb_darkening_coefficients(a, b);

    // Verify that coefficients fall into standard expected bounds (0.0 to 1.0)
    EXPECT_GE(a, 0.0);
    EXPECT_LE(a, 1.0);
    EXPECT_GE(b, -1.0); // b can sometimes be negative in non-linear models
    EXPECT_LE(b, 1.0);
}

// =====================================================================
// Multiple Star Systems (StarMulti)
// =====================================================================

TEST(StarMultiTest, AddAndRetrieveMembers)
{
    StarMulti system;
    Star primary;
    Star secondary;

    system.add_member(&primary, 'A');
    system.add_member(&secondary, 'B');

    EXPECT_EQ(system.num_members(), 2);

    // Verify correct retrieval
    EXPECT_EQ(system.get_member('A'), &primary);
    EXPECT_EQ(system.get_member('B'), &secondary);

    // Verify member checking
    EXPECT_EQ(system.is_member(&primary), 'A');
    EXPECT_EQ(system.is_member(&secondary), 'B');

    // Test for non-existent member
    Star rogue_star;
    EXPECT_EQ(system.is_member(&rogue_star), 0); // Assuming 0 means not found
    EXPECT_EQ(system.get_member('C'), nullptr);
}

TEST(StarMultiTest, NextAvailableComponent)
{
    StarMulti system;
    Star star1, star2;

    system.add_member(&star1, 'A');
    EXPECT_EQ(system.next_available(), 'B');

    system.add_member(&star2, 'B');
    EXPECT_EQ(system.next_available(), 'C');
}

TEST(StarMultiTest, MergeSystems)
{
    StarMulti sys1;
    Star sA, sB;
    sys1.add_member(&sA, 'A');
    sys1.add_member(&sB, 'B');

    StarMulti sys2;
    Star sC;
    sys2.add_member(&sC, 'C'); // Assuming 'C' doesn't conflict, or merge resolves it

    sys1.merge(&sys2);

    EXPECT_GE(sys1.num_members(), 3);
    EXPECT_EQ(sys1.get_member('C'), &sC);
}

TEST(StarTest, MakeCompanionOf)
{
    Star primary;
    Star secondary;

    secondary.make_companion_of(&primary, 'B');

    // Assuming make_companion_of initializes multisys on primary if necessary
    ASSERT_NE(primary.multisys, nullptr);
    EXPECT_EQ(primary.multisys->get_member('A'), &primary);
    EXPECT_EQ(primary.multisys->get_member('B'), &secondary);
    EXPECT_EQ(secondary.get_component(), 'B');
}

// =====================================================================
// Serialization (JSON)
// =====================================================================

TEST(StarTest, JsonSerializationRoundTrip)
{
    Star original;
    original.HD = 12345;
    original.HIP = 67890;
    original.apparent_magnitude = 4.5;
    original.has_planets = 2;

    json j = original.to_json();

    Star deserialized;
    bool success = deserialized.from_json(j);

    EXPECT_TRUE(success);
    EXPECT_EQ(deserialized.HD, 12345);
    EXPECT_EQ(deserialized.HIP, 67890);
    EXPECT_DOUBLE_EQ(deserialized.apparent_magnitude, 4.5);
    EXPECT_EQ(deserialized.has_planets, 2);
    EXPECT_TRUE(deserialized.is_sunlike());
}

// =====================================================================
// Global Helpers / Enums
// =====================================================================

TEST(StellarRegimeTest, DeterminesRegimeCorrectly)
{
    Star main_seq;
    // Assuming setting m_bol and mass correctly resolves to regime_stellar
    // You must mock the state that dictates the regime based on celestial.h

    EXPECT_EQ(stellar_regime(&main_seq), regime_stellar);

    // For a simulated white dwarf
    // main_seq.spectral_type = "DA";
    // EXPECT_EQ(stellar_regime(&main_seq), regime_degenerate);
}