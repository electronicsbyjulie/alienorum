#include <gtest/gtest.h>
#include <cmath>
#include "../classes/cons.h"
#include "../classes/star.h"
#include "../classes/exocons.h"
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

    // Alpha Mensae in consline.dat defines custom Orion and Taurus (< 30 defined)
    EXPECT_EQ(men_alpha_cons.size(), 2u);

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

    ASSERT_TRUE(men_alpha_cons.count("Ori") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Ori"], "Bet Ori"));
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Ori"], "Alp Ori"));

    ASSERT_TRUE(men_alpha_cons.count("Tau") > 0);
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Tau"], "Alp Tau"));
    EXPECT_TRUE(has_line_with_star(men_alpha_cons["Tau"], "Eta Tau"));
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

TEST_F(ConstellationTest, UpsilonAndromedaeConstellations_LoadAndContainExpectedStars)
{
    read_cons_lines();

    std::map<std::string, const Constellation*> ups_cons;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Upsilon Andromedae")
        {
            ups_cons[c.abbrev] = &c;
        }
    }

    EXPECT_EQ(ups_cons.size(), 88u);

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

    // Verify Sun in Centaurus
    ASSERT_TRUE(ups_cons.count("Cen") > 0);
    EXPECT_TRUE(has_line_with_star(ups_cons["Cen"], "Sun"));

    // Verify Upsilon Andromedae is not in Andromeda
    ASSERT_TRUE(ups_cons.count("And") > 0);
    EXPECT_FALSE(has_line_with_star(ups_cons["And"], "Ups And"));
    EXPECT_FALSE(has_line_with_star(ups_cons["And"], "Upsilon Andromedae"));
    EXPECT_FALSE(has_line_with_star(ups_cons["And"], "50 And"));
    EXPECT_FALSE(has_line_with_star(ups_cons["And"], "50  And"));
}

// =====================================================================
// Procedural Constellation Generation Tests
// =====================================================================

class ExoConsTest : public ::testing::Test
{
    protected:
    static void SetUpTestSuite()
    {
        std::remove("exocons.dat");
        ExoConsGenerator::reset();
        if (cels)
        {
            delete[] cels;
            cels = nullptr;
        }
        cels = new CelestialObject*[MAX_CELOBJS];
        memset(cels, 0, MAX_CELOBJS * sizeof(CelestialObject*));
        abort_load = false;
        load_stuff();
    }

    static void TearDownTestSuite()
    {
        std::remove("exocons.dat");
        ExoConsGenerator::reset();
        if (cels)
        {
            delete[] cels;
            cels = nullptr;
        }
    }

    void SetUp() override
    {
        std::remove("exocons.dat");
        ExoConsGenerator::reset();
        constellations.clear();
        read_cons_lines();
    }

    void TearDown() override
    {
        std::remove("exocons.dat");
        ExoConsGenerator::reset();
        constellations.clear();
        read_cons_lines();
    }

    static Star* get_star(const std::string& name)
    {
        int idx = find_object(name.c_str(), true);
        if (idx >= 0 && cels[idx])
        {
            return (Star*)cels[idx];
        }
        return nullptr;
    }
};

TEST_F(ExoConsTest, Generate47UrsaeMajoris_CriteriaVerification)
{
    Star* uma47 = get_star("47 Ursae Majoris");
    if (!uma47)
    {
        uma47 = get_star("47 UMa");
    }
    ASSERT_NE(uma47, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(uma47, generated);

    // Acceptance criterion: at least 50 new constellations form
    EXPECT_GE(generated.size(), 50u);

    // Collect all lines and lined star unit vectors
    struct LineSeg
    {
        Point u1;
        Point u2;
    };
    std::vector<LineSeg> all_segments;
    std::vector<Point> lined_dirs;

    Point vantage_pt = uma47->location;

    for (const auto& c : generated)
    {
        // Must be named after an IAU constellation
        const auto* def = ExoConsGenerator::find_iau_def(c.abbrev);
        EXPECT_NE(def, nullptr);
        EXPECT_EQ(c.name, def->name);
        EXPECT_EQ(c.genitive, def->genitive);

        for (const auto& cl : c.lines)
        {
            ASSERT_NE(cl.a, nullptr);
            ASSERT_NE(cl.b, nullptr);
            // Must not be gravitationally bound to local system
            EXPECT_NE(cl.a, uma47);
            EXPECT_NE(cl.b, uma47);
            EXPECT_NE(cl.a->cenobj, uma47);
            EXPECT_NE(cl.b->cenobj, uma47);

            Point pa = (Point)cl.a->location - vantage_pt;
            Point pb = (Point)cl.b->location - vantage_pt;
            Point ua = pa * (1.0 / pa.magnitude());
            Point ub = pb * (1.0 / pb.magnitude());

            // Lines must not extend more than 15 degrees
            double cos_ang = ua.x * ub.x + ua.y * ub.y + ua.z * ub.z;
            double angle_deg = acos(std::max(-1.0, std::min(1.0, cos_ang))) * 180.0 / _pi;
            EXPECT_LE(angle_deg, 15.01);

            all_segments.push_back({ua, ub});
            lined_dirs.push_back(ua);
            lined_dirs.push_back(ub);
        }
    }

    // Lines must not cross other lines
    int crossing_count = 0;
    for (size_t i = 0; i < all_segments.size(); ++i)
    {
        for (size_t j = i + 1; j < all_segments.size(); ++j)
        {
            if (ExoConsGenerator::arcs_intersect(all_segments[i].u1, all_segments[i].u2,
                                                all_segments[j].u1, all_segments[j].u2))
            {
                crossing_count++;
            }
        }
    }
    EXPECT_EQ(crossing_count, 0);

    // Sky coverage: at least 80% within 5 degrees of a lined star
    double coverage = ExoConsGenerator::calculate_sky_coverage(lined_dirs, 1000);
    EXPECT_GE(coverage, 0.80);
}

TEST_F(ExoConsTest, GenerateGJ86_CausesAtLeast50Constellations)
{
    Star* gj86 = get_star("GJ 86");
    ASSERT_NE(gj86, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(gj86, generated);

    // Acceptance criterion: at least 50 new constellations form
    EXPECT_GE(generated.size(), 50u);

    // Line criteria
    Point vantage_pt = gj86->location;
    std::vector<std::pair<Point, Point>> all_segs;
    std::vector<Point> lined_dirs;

    for (const auto& c : generated)
    {
        for (const auto& cl : c.lines)
        {
            Point pa = (Point)cl.a->location - vantage_pt;
            Point pb = (Point)cl.b->location - vantage_pt;
            Point ua = pa * (1.0 / pa.magnitude());
            Point ub = pb * (1.0 / pb.magnitude());

            double cos_ang = ua.x * ub.x + ua.y * ub.y + ua.z * ub.z;
            double angle_deg = acos(std::max(-1.0, std::min(1.0, cos_ang))) * 180.0 / _pi;
            EXPECT_LE(angle_deg, 15.01);

            all_segs.push_back({ua, ub});
            lined_dirs.push_back(ua);
            lined_dirs.push_back(ub);
        }
    }

    int crossing_count = 0;
    for (size_t i = 0; i < all_segs.size(); ++i)
    {
        for (size_t j = i + 1; j < all_segs.size(); ++j)
        {
            if (ExoConsGenerator::arcs_intersect(all_segs[i].first, all_segs[i].second,
                                                all_segs[j].first, all_segs[j].second))
            {
                crossing_count++;
            }
        }
    }
    EXPECT_EQ(crossing_count, 0);

    double coverage = ExoConsGenerator::calculate_sky_coverage(lined_dirs, 1000);
    EXPECT_GE(coverage, 0.80);
}

TEST_F(ExoConsTest, AlphaMensae_RetainsCustomShapesAndGeneratesUnusedStars)
{
    Star* alp_men = get_star("Alpha Mensae");
    if (!alp_men)
    {
        alp_men = get_star("Alp Men");
    }
    ASSERT_NE(alp_men, nullptr);

    // Verify existing constellations for Alpha Mensae are Orion and Taurus
    int existing_men_cons_count = 0;
    std::unordered_set<Star*> existing_stars;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Alpha Mensae")
        {
            existing_men_cons_count++;
            for (const auto& cl : c.lines)
            {
                if (cl.a)
                {
                    existing_stars.insert(cl.a);
                }
                if (cl.b)
                {
                    existing_stars.insert(cl.b);
                }
            }
        }
    }
    EXPECT_EQ(existing_men_cons_count, 2);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(alp_men, generated);

    // Must generate constellations for unused stars
    EXPECT_GT(generated.size(), 0u);
    EXPECT_GE(existing_men_cons_count + (int)generated.size(), 50);

    // Must not reuse stars already lined in existing Orion and Taurus
    for (const auto& c : generated)
    {
        EXPECT_NE(c.abbrev, "Ori");
        EXPECT_NE(c.abbrev, "Tau");
        for (const auto& cl : c.lines)
        {
            EXPECT_EQ(existing_stars.count(cl.a), 0u);
            EXPECT_EQ(existing_stars.count(cl.b), 0u);
        }
    }
}

TEST_F(ExoConsTest, FileCachingAndReload_MatchesConslineFormat)
{
    Star* uma47 = get_star("47 Ursae Majoris");
    if (!uma47)
    {
        uma47 = get_star("47 UMa");
    }
    ASSERT_NE(uma47, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(uma47, generated);
    ASSERT_GE(generated.size(), 50u);

    // Save to exocons.dat
    ExoConsGenerator::save_to_exocons_file("47 Ursae Majoris", generated);

    // Verify exocons.dat exists and has proper format
    std::ifstream file("exocons.dat");
    ASSERT_TRUE(file.is_open());

    bool found_header = false;
    bool found_cons = false;
    bool found_line = false;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line[0] == ':')
        {
            if (line == ":47 Ursae Majoris")
            {
                found_header = true;
            }
        }
        else if (!line.empty() && line[0] == '~')
        {
            found_cons = true;
        }
        else if (!line.empty() && line.find(',') != std::string::npos)
        {
            found_line = true;
        }
    }
    file.close();

    EXPECT_TRUE(found_header);
    EXPECT_TRUE(found_cons);
    EXPECT_TRUE(found_line);

    // Verify read_cons_lines loads both consline.dat and exocons.dat
    constellations.clear();
    read_cons_lines();

    int uma_cons_count = 0;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "47 Ursae Majoris")
        {
            uma_cons_count++;
        }
    }
    EXPECT_GE(uma_cons_count, 50);
}

TEST_F(ExoConsTest, ProgressiveFrameGeneration_ExecutesSmoothly)
{
    Star* gj86 = get_star("GJ 86");
    ASSERT_NE(gj86, nullptr);
    whereami = find_object("GJ 86", true);
    mycenobj = gj86;

    // Test synchronous vs progressive frame generation
    ExoConsGenerator::start_generation_for(gj86);
    for (int frame = 0; frame < 100; ++frame)
    {
        ExoConsGenerator::update_frame();
    }

    // After frames finish, constellations for GJ 86 are added
    int gj86_count = 0;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "GJ 86" || c.vantage.distance_to(gj86->location) < light_year * 0.1)
        {
            gj86_count++;
        }
    }
    EXPECT_GE(gj86_count, 50);
}

