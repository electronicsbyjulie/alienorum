#include <cstring>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp> 
#include "../classes/galaxy.h"
#include "../classes/planet.h"
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

// TODO: This is not passing - if you are submitting a PR and seeing this fail, your code didn't break it.
TEST(StarTest, TemperatureFromBV)
{
    Star sun;
    sun.estimate_BV(sun_temp);

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
    double a, b;
    s.limb_darkening_coefficients(a, b);

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

    StarMulti* sys2 = new StarMulti();
    Star sC;
    sys2->add_member(&sC, 'C'); // Assuming 'C' doesn't conflict, or merge resolves it

    sys1.merge(sys2);

    EXPECT_GE(sys1.num_members(), 3);

    EXPECT_EQ(sys1.get_member('C'), &sC);
}

TEST(StarTest, MakeCompanionOf)
{
    Star primary;
    Star secondary;

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

    EXPECT_EQ(deserialized.has_planets, 2);

    Star with_hz;
    with_hz.has_planets = 4;
    with_hz.has_hz_planets = 1;
    Star restored_hz;
    EXPECT_TRUE(restored_hz.from_json(with_hz.to_json()));
    EXPECT_EQ(restored_hz.has_planets, 4);
    EXPECT_EQ(restored_hz.has_hz_planets, 1);

    Star planetless;
    EXPECT_FALSE(planetless.to_json().contains("has_planets"));
}

// =====================================================================
// Global Helpers / Enums
// =====================================================================

TEST(StellarRegimeTest, DeterminesRegimeCorrectly)
{
    Star sun;
    sun.temperature = sun_temp;
    sun.absolute_magnitude = 4.83;
    sun.mass = solar_mass;
    EXPECT_EQ(stellar_regime(&sun), regime_stellar);

    Star white_dwarf;
    white_dwarf.temperature = 25000;
    white_dwarf.absolute_magnitude = 11.18;
    white_dwarf.mass = 1.02 * solar_mass;
    EXPECT_EQ(stellar_regime(&white_dwarf), regime_degenerate);

    // A brown dwarf, decided on its mass: below about 0.075 solar masses hydrogen does not burn.
    Star brown_dwarf;
    brown_dwarf.temperature = 1300;
    brown_dwarf.absolute_magnitude = 19.0;
    brown_dwarf.mass = 0.05 * solar_mass;
    EXPECT_EQ(stellar_regime(&brown_dwarf), regime_substellar);

    // And with no mass stated, on its temperature alone.
    Star cold_and_unweighed;
    cold_and_unweighed.temperature = 1000;
    cold_and_unweighed.absolute_magnitude = 20.0;
    EXPECT_EQ(stellar_regime(&cold_and_unweighed), regime_substellar);

    Star red_dwarf;
    red_dwarf.temperature = 2600;
    red_dwarf.absolute_magnitude = 16.0;
    red_dwarf.mass = 0.1 * solar_mass;
    EXPECT_EQ(stellar_regime(&red_dwarf), regime_stellar);

    // Nothing that is not a star has a regime, including nothing at all.
    Planet planet;
    Galaxy galaxy_obj;
    EXPECT_EQ(stellar_regime(&planet), regime_none);
    EXPECT_EQ(stellar_regime(&galaxy_obj), regime_none);
    EXPECT_EQ(stellar_regime(nullptr), regime_none);

    // A star with nothing filled in at all is a star, not a crash and not a white dwarf.
    Star unknown;
    EXPECT_EQ(stellar_regime(&unknown), regime_stellar);
}

// =====================================================================
// Naming and identity
// =====================================================================

TEST(StarTest, MatchesConstellationCaseInsensitively)
{
    Star s;
    strcpy(s.constellation, "Ori");

    EXPECT_TRUE(s.matches_constellation("Ori"));
    EXPECT_TRUE(s.matches_constellation("ORI"));
    EXPECT_TRUE(s.matches_constellation("ori"));
    EXPECT_FALSE(s.matches_constellation("Tau"));
    EXPECT_FALSE(s.matches_constellation("Or"));

    // A star with no constellation matches none of them.
    Star nowhere;
    EXPECT_FALSE(nowhere.matches_constellation("Ori"));
}

TEST(StarTest, IsSunlikeReadsTheSpectralType)
{
    Star s;
    s.absolute_magnitude = 4.83;

    // The Sun itself, and the range either side of it that counts as sunlike.
    strcpy(s.spectral_type, "G2V");
    EXPECT_TRUE(s.is_sunlike());
    strcpy(s.spectral_type, "F8V");
    EXPECT_TRUE(s.is_sunlike());
    strcpy(s.spectral_type, "K2V");
    EXPECT_TRUE(s.is_sunlike());

    // Too hot, too cool, and not on the main sequence at all.
    strcpy(s.spectral_type, "A0V");
    EXPECT_FALSE(s.is_sunlike());
    strcpy(s.spectral_type, "M5V");
    EXPECT_FALSE(s.is_sunlike());
    strcpy(s.spectral_type, "K5V");
    EXPECT_FALSE(s.is_sunlike());
    strcpy(s.spectral_type, "G2III");
    EXPECT_FALSE(s.is_sunlike()) << "a giant is not sunlike whatever its color";

    // Nothing stated at all.
    Star blank;
    EXPECT_FALSE(blank.is_sunlike());
}

TEST(StarTest, ComponentLettersAndUnlinking)
{
    Star primary, secondary, tertiary;
    primary.set_component('A', &primary);
    secondary.make_companion_of(&primary, 'B');
    tertiary.make_companion_of(&primary, 'C');

    ASSERT_NE(primary.multisys, nullptr);
    EXPECT_EQ(primary.multisys, secondary.multisys) << "one system, shared by its members";
    EXPECT_EQ(primary.multisys, tertiary.multisys);
    EXPECT_EQ(primary.get_component(), 'A');
    EXPECT_EQ(secondary.get_component(), 'B');
    EXPECT_EQ(tertiary.get_component(), 'C');
    EXPECT_EQ(primary.multisys->num_members(), 3);
    EXPECT_EQ(primary.multisys->next_available(), 'D');

    // A companion is put in orbit around the primary; the primary is in orbit around nothing.
    ASSERT_NE(secondary.orbit, nullptr);
    EXPECT_EQ(secondary.orbit->center, &primary);
    EXPECT_EQ(primary.orbit, nullptr);

    StarMulti* system = primary.multisys;
    system->unlink();
    EXPECT_EQ(primary.multisys, nullptr);
    EXPECT_EQ(secondary.multisys, nullptr);
    EXPECT_EQ(tertiary.multisys, nullptr);
    EXPECT_EQ(system->num_members(), 0);
    delete system;
}

TEST(StarTest, IsMainSequenceDetection)
{
    Star s;

    strcpy(s.spectral_type, "G5 V");
    EXPECT_TRUE(s.is_main_sequence());

    strcpy(s.spectral_type, "G1V...");
    EXPECT_TRUE(s.is_main_sequence());

    strcpy(s.spectral_type, "F8V");
    EXPECT_TRUE(s.is_main_sequence());

    strcpy(s.spectral_type, "dM3");
    EXPECT_TRUE(s.is_main_sequence());

    strcpy(s.spectral_type, "F8IV");
    EXPECT_FALSE(s.is_main_sequence());

    strcpy(s.spectral_type, "G8VI");
    EXPECT_FALSE(s.is_main_sequence());

    strcpy(s.spectral_type, "K1III");
    EXPECT_FALSE(s.is_main_sequence());

    strcpy(s.spectral_type, "B2Ia");
    EXPECT_FALSE(s.is_main_sequence());

    strcpy(s.spectral_type, "");
    EXPECT_FALSE(s.is_main_sequence());
}

TEST(StarTest, ExpectedMainSequenceAbsmag)
{
    Star::load_main_seq_dat();

    Star sun;
    strcpy(sun.spectral_type, "G2V");
    EXPECT_NEAR(sun.expected_main_sequence_absmag(), 4.83, 0.05);

    Star k2_106;
    strcpy(k2_106.spectral_type, "G5 V");
    EXPECT_NEAR(k2_106.expected_main_sequence_absmag(), 4.99, 0.05);

    Star hd7229;
    strcpy(hd7229.spectral_type, "G1V...");
    EXPECT_NEAR(hd7229.expected_main_sequence_absmag(), 4.66, 0.05);
}

TEST(StarTest, MainSequenceAbsmagOutlierCheck)
{
    Star::load_main_seq_dat();

    Star sun;
    strcpy(sun.spectral_type, "G2V");
    sun.absolute_magnitude = 4.83;
    EXPECT_FALSE(sun.is_mseq_absmag_outlier(1.5));

    Star k2_106;
    strcpy(k2_106.spectral_type, "G5 V");
    k2_106.absolute_magnitude = 10.0;
    double diff = 0;
    EXPECT_TRUE(k2_106.is_mseq_absmag_outlier(1.5, &diff));
    EXPECT_NEAR(diff, 5.01, 0.1);

    Star hd7229;
    strcpy(hd7229.spectral_type, "G1V...");
    hd7229.absolute_magnitude = 1.15;
    EXPECT_TRUE(hd7229.is_mseq_absmag_outlier(1.5, &diff));
    EXPECT_NEAR(diff, -3.51, 0.1);
}

TEST(StarTest, CorrectMainSequenceK2106)
{
    Star::load_main_seq_dat();

    Star k2_106;
    strcpy(k2_106.name, "K2-106");
    strcpy(k2_106.spectral_type, "G5 V");
    k2_106.apparent_magnitude = 12.1;
    k2_106.distance = 246.1 * parsec;
    k2_106.distance_known = true;
    k2_106.absolute_magnitude = 10.0;
    k2_106.temperature = 5617;

    EXPECT_TRUE(k2_106.correct_main_sequence_absmag(1.5));
    EXPECT_NEAR(k2_106.absolute_magnitude, 5.15, 0.05);
    EXPECT_STREQ(k2_106.spectral_type, "G5 V");
}

TEST(StarTest, CorrectMainSequenceHD7229)
{
    Star::load_main_seq_dat();

    Star hd7229;
    strcpy(hd7229.name, "HD7229");
    strcpy(hd7229.spectral_type, "G1V...");
    hd7229.apparent_magnitude = 6.24;
    hd7229.distance = 104.38 * parsec;
    hd7229.distance_known = true;
    hd7229.absolute_magnitude = 1.15;
    hd7229.temperature = 4800;

    EXPECT_TRUE(hd7229.correct_main_sequence_absmag(1.5));
    EXPECT_NEAR(hd7229.absolute_magnitude, 1.15, 0.01);
    EXPECT_STREQ(hd7229.spectral_type, "G1III...");
    EXPECT_GT(hd7229.volumetric_mean_radius, 5.0 * solar_radius);
}

TEST(StarTest, AuditAndCorrectMainSequenceStars)
{
    Star::load_main_seq_dat();

    Star normal_sun;
    strcpy(normal_sun.name, "Sol");
    strcpy(normal_sun.spectral_type, "G2V");
    normal_sun.apparent_magnitude = -26.74;
    normal_sun.distance = AU;
    normal_sun.distance_known = true;
    normal_sun.absolute_magnitude = 4.83;

    Star k2_106;
    strcpy(k2_106.name, "K2-106");
    strcpy(k2_106.spectral_type, "G5 V");
    k2_106.apparent_magnitude = 12.1;
    k2_106.distance = 246.1 * parsec;
    k2_106.distance_known = true;
    k2_106.absolute_magnitude = 10.0;
    k2_106.temperature = 5617;

    Star hd7229;
    strcpy(hd7229.name, "HD7229");
    strcpy(hd7229.spectral_type, "G1V...");
    hd7229.apparent_magnitude = 6.24;
    hd7229.distance = 104.38 * parsec;
    hd7229.distance_known = true;
    hd7229.absolute_magnitude = 1.15;
    hd7229.temperature = 4800;

    CelestialObject* stars[] = {&normal_sun, &k2_106, &hd7229, nullptr};

    int corrected = Star::audit_and_correct_main_sequence_stars(stars, 1.5);
    EXPECT_EQ(corrected, 2);

    EXPECT_NEAR(normal_sun.absolute_magnitude, 4.83, 0.01);
    EXPECT_STREQ(normal_sun.spectral_type, "G2V");

    EXPECT_NEAR(k2_106.absolute_magnitude, 5.15, 0.05);
    EXPECT_STREQ(k2_106.spectral_type, "G5 V");

    EXPECT_NEAR(hd7229.absolute_magnitude, 1.15, 0.01);
    EXPECT_STREQ(hd7229.spectral_type, "G1III...");
}

TEST(StarTest, HotOBStarsTurnedDown)
{
    Star::load_main_seq_dat();

    // 10 Lacertae: O9V, V = 4.89, d = 324.6 pc, M_V = -2.67
    Star lac10;
    strcpy(lac10.name, "10 Lacertae");
    strcpy(lac10.spectral_type, "O9V");
    lac10.apparent_magnitude = 4.89;
    lac10.distance = 324.6 * parsec;
    lac10.distance_known = true;
    lac10.absolute_magnitude = -2.67;
    lac10.temperature = 33300;

    // Sigma Orionis: O9.5V..., V = 3.77, d = 352.1 pc, M_V = -3.96
    Star sigOri;
    strcpy(sigOri.name, "Sigma Orionis");
    strcpy(sigOri.spectral_type, "O9.5V...");
    sigOri.apparent_magnitude = 3.77;
    sigOri.distance = 352.1 * parsec;
    sigOri.distance_known = true;
    sigOri.absolute_magnitude = -3.96;
    sigOri.temperature = 32000;

    // 23 Orionis: B1V, V = 4.99, d = 295.0 pc, M_V = -2.36
    Star ori23;
    strcpy(ori23.name, "23 Orionis");
    strcpy(ori23.spectral_type, "B1V");
    ori23.apparent_magnitude = 4.99;
    ori23.distance = 295.0 * parsec;
    ori23.distance_known = true;
    ori23.absolute_magnitude = -2.36;
    ori23.temperature = 26000;

    // HD193322: O9V, V = 5.83, d = 476.2 pc, M_V = -2.56
    Star hd193322;
    strcpy(hd193322.name, "HD193322");
    strcpy(hd193322.spectral_type, "O9V");
    hd193322.apparent_magnitude = 5.83;
    hd193322.distance = 476.2 * parsec;
    hd193322.distance_known = true;
    hd193322.absolute_magnitude = -2.56;
    hd193322.temperature = 33300;

    EXPECT_TRUE(lac10.is_hot_ob_star());
    EXPECT_TRUE(sigOri.is_hot_ob_star());
    EXPECT_TRUE(ori23.is_hot_ob_star());
    EXPECT_TRUE(hd193322.is_hot_ob_star());

    // Expected main sequence absmag should be disabled (>= 1e28) for hot O and B stars
    EXPECT_GT(lac10.expected_main_sequence_absmag(), 1e28);
    EXPECT_GT(sigOri.expected_main_sequence_absmag(), 1e28);
    EXPECT_GT(ori23.expected_main_sequence_absmag(), 1e28);
    EXPECT_GT(hd193322.expected_main_sequence_absmag(), 1e28);

    // They must NOT be flagged as outliers
    EXPECT_FALSE(lac10.is_mseq_absmag_outlier());
    EXPECT_FALSE(sigOri.is_mseq_absmag_outlier());
    EXPECT_FALSE(ori23.is_mseq_absmag_outlier());
    EXPECT_FALSE(hd193322.is_mseq_absmag_outlier());

    // Correction must not touch them
    EXPECT_FALSE(lac10.correct_main_sequence_absmag());
    EXPECT_FALSE(sigOri.correct_main_sequence_absmag());
    EXPECT_FALSE(ori23.correct_main_sequence_absmag());
    EXPECT_FALSE(hd193322.correct_main_sequence_absmag());

    EXPECT_NEAR(lac10.absolute_magnitude, -2.67, 0.001);
    EXPECT_NEAR(sigOri.absolute_magnitude, -3.96, 0.001);
    EXPECT_NEAR(ori23.absolute_magnitude, -2.36, 0.001);
    EXPECT_NEAR(hd193322.absolute_magnitude, -2.56, 0.001);
}

TEST(StarTest, NoStarMadeBrighterThanCatalog)
{
    Star::load_main_seq_dat();

    // A main sequence star with measured distance and apparent magnitude that is fainter than textbook
    // (e.g. reddened / interstellar extinction in the galactic plane).
    Star s;
    strcpy(s.name, "ExtinctedDwarf");
    strcpy(s.spectral_type, "G2V");
    s.apparent_magnitude = 10.0;
    s.distance = 50.0 * parsec; // distance modulus = 5 * log10(5) = 3.50 -> m_obs = 6.50
    s.distance_known = true;
    s.absolute_magnitude = 6.50; // textbook G2V is 4.83, but star is extincted to 6.50
    s.temperature = 5778;

    // It should not be forced brighter to 4.83, because that would make apparent magnitude 4.83 + 3.50 = 8.33 (brighter than catalog 10.0)!
    EXPECT_FALSE(s.correct_main_sequence_absmag());
    EXPECT_NEAR(s.absolute_magnitude, 6.50, 0.001);

    // Double check effective apparent magnitude:
    double effective_appmag = s.absolute_magnitude + 5.0 * (log10(s.distance / (parsec * 10.0)));
    EXPECT_GE(effective_appmag, s.apparent_magnitude - 0.01);
}