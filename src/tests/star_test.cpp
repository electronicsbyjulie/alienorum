#include <gtest/gtest.h>
#include <nlohmann/json.hpp> 
#include "../classes/star.h" 

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
    // gotta_be_named_something() fills in name, working down the catalog designations a nameless
    // star might carry: Bayer/Flamsteed, then HD, HIP, SAO, the Bonn survey, and SB9. It does not
    // touch alienorumid -- that is assigned by constellation in cons.cpp, over the whole array at
    // once, because the numbering runs in order of brightness within each constellation.
    Star s;
    s.HD = 12345;
    s.gotta_be_named_something();
    EXPECT_STREQ(s.name, "HD12345");
    EXPECT_EQ(s.origname, std::string("HD12345"));

    // HIP is next in line when there is no HD number.
    Star hip_only;
    hip_only.HIP = 67890;
    hip_only.gotta_be_named_something();
    EXPECT_STREQ(hip_only.name, "HIP67890");

    // A star already named is left alone.
    Star named;
    strcpy(named.name, "Vega");
    named.HD = 172167;
    named.gotta_be_named_something();
    EXPECT_STREQ(named.name, "Vega");
}

// =====================================================================
// Astrophysical Calculations & Estimations
// =====================================================================

TEST(StarTest, DegenerateRadiusCalculation)
{
    // Grams, like CelestialObject::mass and the solar_mass constant it is divided by -- the one
    // caller, in celestial.cpp, hands it cel->mass straight.
    double one_solar_mass = 1.989e33;

    // White dwarf radius for 1 solar mass should be roughly Earth-sized (~6.4e6 meters)
    double radius = Star::degenerate_radius(one_solar_mass);

    // Using EXPECT_NEAR since astrophysical calculations are approximations
    EXPECT_NEAR(radius, 6.4e6, 1.0e6); // Tolerance of 1,000 km

    // Degenerate matter runs the mass-radius relation backwards: the heavier it is, the smaller.
    EXPECT_LT(Star::degenerate_radius(1.2 * one_solar_mass), radius);
    EXPECT_GT(Star::degenerate_radius(0.6 * one_solar_mass), radius);

    // At and past the Chandrasekhar limit the radius is pinned rather than allowed to reach zero,
    // and a mass of zero or less falls back to the observed peak instead of dividing by it.
    EXPECT_GT(Star::degenerate_radius(2.0 * one_solar_mass), 0.0);
    EXPECT_DOUBLE_EQ(Star::degenerate_radius(0.0), Star::degenerate_radius(0.6 * solar_mass));
}

TEST(StarTest, TemperatureFromBV)
{
    // Both estimate_BV() and its inverse work in pure Planck colors, which sit nowhere near the
    // photometric B-V scale until bv_correction shifts them onto it. That global is calibrated at
    // load time against the Sun's catalog color (loaders.cpp), and is zero in a test binary, which
    // no test can be allowed to depend on -- so do here what the loader does there.
    Star sun;
    sun.estimate_BV(sun_temp);                      // uncorrected, since bv_correction is still 0
    bv_correction = sun.BV_color - 0.65;            // the Sun is B-V 0.65 by definition

    // With that in place the two are exact inverses of one another, which is the contract
    // temperature_from_BV() states and the only thing about it that does not depend on the
    // calibration: a black body is not a stellar atmosphere, and the absolute scale it gives at
    // the blue end is off by thousands of Kelvins (B-V 0.0 lands near 14,000 K, not Vega's 9,600).
    EXPECT_NEAR(Star::temperature_from_BV(0.65), sun_temp, 1.0);

    double T;
    for (T = 3000; T <= 30000; T += 1000)
    {
        Star s;
        s.estimate_BV(T);
        EXPECT_NEAR(Star::temperature_from_BV(s.BV_color), T, T*1e-6) << " at T = " << T;
    }

    // And it is monotonic the right way round: bluer is hotter.
    EXPECT_GT(Star::temperature_from_BV(0.0), Star::temperature_from_BV(0.65));
    EXPECT_GT(Star::temperature_from_BV(0.65), Star::temperature_from_BV(1.5));

    // Out past either end of the bisection's bracket it saturates instead of running away.
    EXPECT_DOUBLE_EQ(Star::temperature_from_BV(99.0), 1000.0);
    EXPECT_DOUBLE_EQ(Star::temperature_from_BV(-99.0), 200000.0);

    bv_correction = 0;                              // leave the global as we found it
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

    // merge() adopts the system it is handed -- it steals the member array and deletes the object
    // itself on the way out -- so the argument has to be one that can be deleted. Every StarMulti
    // in the program proper is new'd by Star::set_component().
    StarMulti* sys2 = new StarMulti();
    Star sC;
    sys2->add_member(&sC, 'C'); // Assuming 'C' doesn't conflict, or merge resolves it

    sys1.merge(sys2);

    EXPECT_GE(sys1.num_members(), 3);

    // The merged members are packed down into consecutive slots, so a component letter survives
    // only as far as the count of members before it does: here A, B, and C stay A, B, and C.
    EXPECT_EQ(sys1.get_member('C'), &sC);
}

TEST(StarTest, MakeCompanionOf)
{
    Star primary;
    Star secondary;

    // make_companion_of() enrolls the companion, not the primary: every caller in cat.cpp opens
    // with this line, so the primary is 'A' of its own system before any B is hung off it. Leave
    // it out and the system has no 'A', which gotta_be_named_something() reads as "this star is
    // not the primary" and returns early, leaving the whole system unnamed.
    primary.set_component('A', &primary);

    secondary.make_companion_of(&primary, 'B');

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
    strcpy(original.spectral_type, "G2V");

    json j = original.to_json();

    Star deserialized;
    bool success = deserialized.from_json(j);

    EXPECT_TRUE(success);
    EXPECT_EQ(deserialized.HD, 12345);
    EXPECT_EQ(deserialized.HIP, 67890);
    EXPECT_DOUBLE_EQ(deserialized.apparent_magnitude, 4.5);

    // is_sunlike() is read off the spectral type, which does round-trip.
    EXPECT_STREQ(deserialized.spectral_type, "G2V");
    EXPECT_TRUE(deserialized.is_sunlike());

    // The planet tallies are saved rather than counted back up from the file, so a star keeps
    // them even when the planets themselves are not being loaded this session -- with noexo, say.
    // Serialization::load_all() leaves the counting to the stars whose entry stated nothing.
    EXPECT_EQ(deserialized.has_planets, 2);

    Star with_hz;
    with_hz.has_planets = 4;
    with_hz.has_hz_planets = 1;
    Star restored_hz;
    EXPECT_TRUE(restored_hz.from_json(with_hz.to_json()));
    EXPECT_EQ(restored_hz.has_planets, 4);
    EXPECT_EQ(restored_hz.has_hz_planets, 1);

    // A star with no planets writes no tally, which is what tells load_all() to do the counting
    // for it -- as every file written before the tallies were saved does for every star in it.
    Star planetless;
    EXPECT_FALSE(planetless.to_json().contains("has_planets"));
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