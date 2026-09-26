#include <gtest/gtest.h>
#include <cmath>
#include "../classes/cons.h"
#include "../classes/star.h"
#include "../loaders.h"

using namespace alienorum;

// =====================================================================
// Constellation & Identification Fixture
// =====================================================================

class ConstellationTest : public ::testing::Test
{
    protected:
    void SetUp() override
    {
        // Clear global constellations before each test
        constellations.clear();
        
        // Setup a safe, small cels array for fill_alienorum_ids to iterate over
        // Assuming cels is a global CelestialObject** null-terminated array
        cels = new CelestialObject*[10];
        for (int i = 0; i < 10; i++) cels[i] = nullptr;
    }

    void TearDown() override
    {
        // Clean up any stars we allocated into cels
        for (int i = 0; i < 10; i++)
        {
            if (cels[i])
            {
                delete cels[i];
                cels[i] = nullptr;
            }
        }
        delete[] cels;
        cels = nullptr;
        
        constellations.clear();
    }
};

// =====================================================================
// Perimeter Building Tests
// =====================================================================

TEST_F(ConstellationTest, BuildPerimeter_OrdersVerticesAndFindsCenter)
{
    Constellation cons;
    
    // Add points out of order (e.g., a square added as corners 1, 3, 2, 4)
    ConsBoundary b1; b1.RA = 0.0; b1.decl = 0.0;
    ConsBoundary b2; b2.RA = 0.2; b2.decl = 0.2; // Diagonal to b1
    ConsBoundary b3; b3.RA = 0.0; b3.decl = 0.2; // Adjacent to b1 and b2
    ConsBoundary b4; b4.RA = 0.2; b4.decl = 0.0; // Adjacent to b1 and b2
    
    cons.bounds.push_back(b1);
    cons.bounds.push_back(b2);
    cons.bounds.push_back(b3);
    cons.bounds.push_back(b4);
    
    cons.build_constellation_perimeter();
    
    ASSERT_EQ(cons.bounds.size(), 4);
    
    // The algorithm starts at the first element (b1 at 0,0)
    EXPECT_DOUBLE_EQ(cons.bounds[0].RA, 0.0);
    EXPECT_DOUBLE_EQ(cons.bounds[0].decl, 0.0);
    
    // The next closest point to (0,0) should be either (0.0, 0.2) or (0.2, 0.0)
    // It should NEVER jump to the diagonal (0.2, 0.2) next.
    bool jumped_diagonal = (cons.bounds[1].RA == 0.2 && cons.bounds[1].decl == 0.2);
    EXPECT_FALSE(jumped_diagonal);
    
    // Center should be roughly in the middle of the bounding box
    EXPECT_NEAR(cons.RA_center, 0.1, 0.05);
    EXPECT_NEAR(cons.decl_center, 0.1, 0.05);
}

// =====================================================================
// Star Identification & Polygon Math Tests
// =====================================================================

TEST_F(ConstellationTest, IdentifyCons_PolarFallback)
{
    // Create Ursa Minor and Octans with no bounds to force the fallback
    Constellation umi;
    umi.name = "Ursa Minor";
    umi.abbrev = "UMi";
    
    Constellation oct;
    oct.name = "Octans";
    oct.abbrev = "Oct";
    
    constellations.push_back(umi);
    constellations.push_back(oct);
    
    Star northern_star;
    northern_star.declination = 1.0; // Positive (North)
    
    Star southern_star;
    southern_star.declination = -1.0; // Negative (South)
    
    Constellation* found_north = identify_cons_of_star(&northern_star);
    Constellation* found_south = identify_cons_of_star(&southern_star);
    
    ASSERT_NE(found_north, nullptr);
    ASSERT_NE(found_south, nullptr);
    
    EXPECT_EQ(found_north->abbrev, "UMi");
    EXPECT_EQ(found_south->abbrev, "Oct");
}

TEST_F(ConstellationTest, IdentifyCons_PointInPolygon)
{
    // This test will help you verify your new point-in-polygon math once rewritten!
    Constellation square_cons;
    square_cons.name = "Test Square";
    square_cons.abbrev = "Tsq";
    
    // Create a square constellation from RA 0.1 to 0.3, Decl 0.1 to 0.3
    ConsBoundary b;
    b.RA = 0.1; b.decl = 0.1; square_cons.bounds.push_back(b);
    b.RA = 0.3; b.decl = 0.1; square_cons.bounds.push_back(b);
    b.RA = 0.3; b.decl = 0.3; square_cons.bounds.push_back(b);
    b.RA = 0.1; b.decl = 0.3; square_cons.bounds.push_back(b);
    
    square_cons.RA_center = 0.2;
    square_cons.decl_center = 0.2;
    
    constellations.push_back(square_cons);
    
    Star inside_star;
    inside_star.right_ascension = 0.2;
    inside_star.declination = 0.2;
    
    Star outside_star;
    outside_star.right_ascension = 0.5;
    outside_star.declination = 0.5;
    
    Constellation* found_inside = identify_cons_of_star(&inside_star);
    Constellation* found_outside = identify_cons_of_star(&outside_star);
    
    // If the polygon math is working, it should catch the inside star
    ASSERT_NE(found_inside, nullptr);
    EXPECT_EQ(found_inside->abbrev, "Tsq");
    
    // The outside star should miss the polygon and hit the polar fallback
    // Since we didn't add UMi/Oct to the global vector, it will return nullptr
    EXPECT_EQ(found_outside, nullptr);
}

// =====================================================================
// Alienorum ID Generation Tests
// =====================================================================

TEST_F(ConstellationTest, FillAlienorumIds_FormatsCorrectly)
{
    // Setup a dummy constellation
    Constellation cons;
    cons.name = "Orion";
    cons.abbrev = "Ori";
    
    // Give it bounds so identify_cons_of_star finds it
    ConsBoundary b;
    b.RA = 0.0; b.decl = 0.0; cons.bounds.push_back(b);
    b.RA = 1.0; b.decl = 0.0; cons.bounds.push_back(b);
    b.RA = 1.0; b.decl = 1.0; cons.bounds.push_back(b);
    b.RA = 0.0; b.decl = 1.0; cons.bounds.push_back(b);
    cons.RA_center = 0.5;
    cons.decl_center = 0.5;
    
    constellations.push_back(cons);
    
    // Setup a star right in the middle
    Star* s1 = new Star();
    EXPECT_EQ(s1->typeclass(), class_star);
    s1->right_ascension = 0.5;
    s1->declination = 0.5;
    s1->apparent_magnitude = 2.4; 
    s1->seqno = 1; // Must be > 0 so it doesn't get treated as the Sun
    s1->variability_period = 0; // Not variable
    
    // Assuming estimate_temperature() defaults to something yielding 'w' (white, ~6000K-7300K) 
    // or 'y' (yellow, ~5300K-6000K) if uninitialized. We will just check that it appends Ori.
    
    cels[0] = s1;
    
    ConsBins bins = fill_alienorum_ids();
    
    // The star's internal alienorumid should have been set
    EXPECT_FALSE(s1->alienorumid.empty());
    
    // Check for the expected formatting components
    // floor(2.4) = 2. Should contain "2", a color code, and "Ori"
    EXPECT_TRUE(s1->alienorumid.find("2") != std::string::npos);
    EXPECT_TRUE(s1->alienorumid.find("Ori") != std::string::npos);
}

// =====================================================================
// Tau Ceti Constellation Vantage Tests
// =====================================================================

TEST_F(ConstellationTest, TauCetiConstellations_LoadAndContainExpectedStars)
{
    read_cons_lines();

    std::map<std::string, const Constellation*> tau_ceti_cons;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Tau Ceti")
        {
            tau_ceti_cons[c.abbrev] = &c;
        }
    }

    EXPECT_EQ(tau_ceti_cons.size(), 88u);

    auto has_line_with_star = [](const Constellation* cons, const std::string& star) -> bool
    {
        if (!cons)
        {
            return false;
        }
        for (const auto& line : cons->lines)
        {
            if (line.starnamea == star || line.starnameb == star)
            {
                return true;
            }
        }
        return false;
    };

    // Check key constellations and displaced stars
    ASSERT_TRUE(tau_ceti_cons.count("Boo") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Boo"], "Sun"));

    ASSERT_TRUE(tau_ceti_cons.count("Leo") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Leo"], "Alp CMa"));
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Leo"], "Alp CMi"));

    ASSERT_TRUE(tau_ceti_cons.count("Vir") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Vir"], "Alp1Cen"));

    ASSERT_TRUE(tau_ceti_cons.count("Cnc") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Cnc"], "Eps Eri"));

    ASSERT_TRUE(tau_ceti_cons.count("Her") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Her"], "Alp Lyr"));
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Her"], "Alp Aql"));

    ASSERT_TRUE(tau_ceti_cons.count("CMi") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["CMi"], "Omi2Eri"));

    ASSERT_TRUE(tau_ceti_cons.count("Car") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Car"], "82 Eridani"));

    ASSERT_TRUE(tau_ceti_cons.count("Gem") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Gem"], "Pi 3Ori"));

    ASSERT_TRUE(tau_ceti_cons.count("Cep") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Cep"], "Eta Cas"));

    ASSERT_TRUE(tau_ceti_cons.count("Ara") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Ara"], "Del Pav"));

    ASSERT_TRUE(tau_ceti_cons.count("Aps") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Aps"], "Bet Hyi"));

    ASSERT_TRUE(tau_ceti_cons.count("Lup") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Lup"], "Eps Ind"));

    ASSERT_TRUE(tau_ceti_cons.count("Cap") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Cap"], "Alp PsA"));

    ASSERT_TRUE(tau_ceti_cons.count("Oct") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Oct"], "Zet Tuc"));

    ASSERT_TRUE(tau_ceti_cons.count("Ser") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Ser"], "Iot Ser"));

    ASSERT_TRUE(tau_ceti_cons.count("Crv") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Crv"], "Gam Crv"));
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Crv"], "Bet Crv"));

    ASSERT_TRUE(tau_ceti_cons.count("Crt") > 0);
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Crt"], "Alp Crt"));
    EXPECT_TRUE(has_line_with_star(tau_ceti_cons["Crt"], "Del Crt"));
}

// =====================================================================
// 82 Eridani Constellation Vantage Tests
// =====================================================================

TEST_F(ConstellationTest, Eridani82Constellations_LoadAndContainExpectedStars)
{
    read_cons_lines();

    std::map<std::string, const Constellation*> eri_82_cons;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "82 Eridani")
        {
            eri_82_cons[c.abbrev] = &c;
        }
    }

    EXPECT_EQ(eri_82_cons.size(), 88u);

    auto has_line_with_star = [](const Constellation* cons, const std::string& star) -> bool
    {
        if (!cons)
        {
            return false;
        }
        for (const auto& line : cons->lines)
        {
            if (line.starnamea == star || line.starnameb == star)
            {
                return true;
            }
        }
        return false;
    };

    // Check key constellations and displaced stars
    ASSERT_TRUE(eri_82_cons.count("Boo") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Boo"], "Sun"));

    ASSERT_TRUE(eri_82_cons.count("CVn") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["CVn"], "Alp CMa"));

    ASSERT_TRUE(eri_82_cons.count("UMa") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["UMa"], "Alp CMi"));

    ASSERT_TRUE(eri_82_cons.count("CrB") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["CrB"], "Alp1Cen"));

    ASSERT_TRUE(eri_82_cons.count("UMi") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["UMi"], "Eps Eri"));

    ASSERT_TRUE(eri_82_cons.count("Dra") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Dra"], "Tau Cet"));

    ASSERT_TRUE(eri_82_cons.count("Ara") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Ara"], "Bet Hyi"));

    ASSERT_TRUE(eri_82_cons.count("Equ") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Equ"], "Alp PsA"));

    ASSERT_TRUE(eri_82_cons.count("Oph") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Oph"], "Del Pav"));

    ASSERT_TRUE(eri_82_cons.count("CMi") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["CMi"], "Gam Lep"));

    ASSERT_TRUE(eri_82_cons.count("Aur") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Aur"], "Pi 3Ori"));

    ASSERT_TRUE(eri_82_cons.count("Crv") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Crv"], "Gam Crv"));
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Crv"], "Bet Crv"));

    ASSERT_TRUE(eri_82_cons.count("Crt") > 0);
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Crt"], "Alp Crt"));
    EXPECT_TRUE(has_line_with_star(eri_82_cons["Crt"], "Del Crt"));

    ASSERT_TRUE(eri_82_cons.count("Eri") > 0);
    EXPECT_FALSE(has_line_with_star(eri_82_cons["Eri"], "82 Eridani"));
}

TEST_F(ConstellationTest, AlphaMensaeConstellations_LoadAndContainExpectedStars)
{
    read_cons_lines();

    std::map<std::string, const Constellation*> men_alpha_cons;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Alpha Mensae")
        {
            men_alpha_cons[c.abbrev] = &c;
        }
    }

    EXPECT_EQ(men_alpha_cons.size(), 88u);

    auto has_line_with_star = [](const Constellation* cons, const std::string& star) -> bool
    {
        if (!cons)
        {
            return false;
        }
        for (const auto& line : cons->lines)
        {
            if (line.starnamea == star || line.starnameb == star)
            {
                return true;
            }
        }
        return false;
    };

    // Check key constellations and displaced stars
    ASSERT_TRUE(men_alpha_cons.count("Dra") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Dra"], "Sun"));
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Dra"], "Alp1Cen"));

    ASSERT_TRUE(men_alpha_cons.count("UMi") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["UMi"], "Alp CMa"));

    ASSERT_TRUE(men_alpha_cons.count("Cyg") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Cyg"], "Bet Hyi"));

    ASSERT_TRUE(men_alpha_cons.count("Cas") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Cas"], "82G Eri"));

    ASSERT_TRUE(men_alpha_cons.count("Lyr") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Lyr"], "Del Pav"));

    ASSERT_TRUE(men_alpha_cons.count("Peg") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Peg"], "Zet Tuc"));

    ASSERT_TRUE(men_alpha_cons.count("Aql") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Aql"], "Gam Pav"));

    ASSERT_TRUE(men_alpha_cons.count("UMa") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["UMa"], "Alp CMi"));

    ASSERT_TRUE(men_alpha_cons.count("Aur") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Aur"], "Gam Lep"));

    ASSERT_TRUE(men_alpha_cons.count("Lac") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Lac"], "Alp PsA"));

    ASSERT_TRUE(men_alpha_cons.count("Per") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Per"], "Del Eri"));

    ASSERT_TRUE(men_alpha_cons.count("Eri") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Eri"], "Zet Dor"));

    ASSERT_TRUE(men_alpha_cons.count("Cam") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Cam"], "Pi 3Ori"));

    ASSERT_TRUE(men_alpha_cons.count("Equ") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Equ"], "Del Cap"));

    ASSERT_TRUE(men_alpha_cons.count("Men") > 0);
    EXPECT_FALSE(has_line_with_star(men_alpha_cons["Men"], "Alp Men"));
}

TEST_F(ConstellationTest, ReloadStuff_RepeatedCalls_DoNotCrashOrLeak)
{
    EXPECT_NO_THROW(reload_stuff());
    EXPECT_FALSE(splash);
    EXPECT_FALSE(is_reloading);

    EXPECT_NO_THROW(reload_stuff());
    EXPECT_FALSE(splash);
    EXPECT_FALSE(is_reloading);
}

TEST_F(ConstellationTest, Rho1CancriConstellations_LoadAndContainExpectedStars)
{
    read_cons_lines();

    std::map<std::string, const Constellation*> rho1_cons;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Rho 1 Cancri")
        {
            rho1_cons[c.abbrev] = &c;
        }
    }

    EXPECT_EQ(rho1_cons.size(), 88u);

    auto has_line_with_star = [](const Constellation* cons, const std::string& star) -> bool
    {
        if (!cons)
        {
            return false;
        }
        for (const auto& line : cons->lines)
        {
            if (line.starnamea == star || line.starnameb == star)
            {
                return true;
            }
        }
        return false;
    };

    // Verify Sun in Capricornus
    ASSERT_TRUE(rho1_cons.count("Cap") > 0);
    EXPECT_TRUE(has_line_with_star(rho1_cons["Cap"], "Sun"));

    // Verify Pollux (Bet Gem) in Cetus
    ASSERT_TRUE(rho1_cons.count("Cet") > 0);
    EXPECT_TRUE(has_line_with_star(rho1_cons["Cet"], "Bet Gem"));

    // Verify Castor (Alp Gem) in Taurus
    ASSERT_TRUE(rho1_cons.count("Tau") > 0);
    EXPECT_TRUE(has_line_with_star(rho1_cons["Tau"], "Alp Gem"));

    // Verify Capella (Alp Aur) in Andromeda
    ASSERT_TRUE(rho1_cons.count("And") > 0);
    EXPECT_TRUE(has_line_with_star(rho1_cons["And"], "Alp Aur"));

    // Verify Arcturus (Alp Boo) in Ophiuchus
    ASSERT_TRUE(rho1_cons.count("Oph") > 0);
    EXPECT_TRUE(has_line_with_star(rho1_cons["Oph"], "Alp Boo"));

    // Verify Sirius (Alp CMa) and Procyon (Alp CMi) in Piscis Austrinus
    ASSERT_TRUE(rho1_cons.count("PsA") > 0);
    EXPECT_TRUE(has_line_with_star(rho1_cons["PsA"], "Alp CMa"));
    EXPECT_TRUE(has_line_with_star(rho1_cons["PsA"], "Alp CMi"));

    // Verify Rho 1 Cnc is not in Cancer
    ASSERT_TRUE(rho1_cons.count("Cnc") > 0);
    EXPECT_FALSE(has_line_with_star(rho1_cons["Cnc"], "Rho 1 Cnc"));
    EXPECT_FALSE(has_line_with_star(rho1_cons["Cnc"], "Rho1Cnc"));
    EXPECT_FALSE(has_line_with_star(rho1_cons["Cnc"], "55 Cnc"));
}

