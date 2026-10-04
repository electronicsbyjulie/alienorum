#include <gtest/gtest.h>
#include <cmath>
#include "../classes/cons.h"
#include "../classes/star.h"
#include "../classes/exocons.h"
#include "../loaders.h"
#include "../visuals.h"

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
        num_reg_cons = 0;
        
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
        num_reg_cons = 0;
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
// Heliocentric Constellation Vantage Tests
// =====================================================================

TEST_F(ConstellationTest, HeliocentricConstellations_LoadAndContainExpectedStars)
{
    read_cons_lines();

    std::map<std::string, const Constellation*> sun_cons;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Sun" || c.vantage_name.empty())
        {
            sun_cons[c.abbrev] = &c;
        }
    }

    // Verify all 88 standard IAU constellations are loaded for the heliocentric vantage
    for (int i = 0; i < EXOCONS_NUM_IAU_CONSTELLATIONS; i++)
    {
        EXPECT_TRUE(sun_cons.count(iau_constellations[i].abbrev) > 0)
            << "Missing IAU constellation: " << iau_constellations[i].abbrev;
    }

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

    // Check key constellations and expected stars from consline.dat
    ASSERT_TRUE(sun_cons.count("Ori") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["Ori"], "Bet Ori"));
    EXPECT_TRUE(has_line_with_star(sun_cons["Ori"], "Alp Ori"));

    ASSERT_TRUE(sun_cons.count("UMa") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["UMa"], "Alp UMa"));
    EXPECT_TRUE(has_line_with_star(sun_cons["UMa"], "Eta UMa"));

    ASSERT_TRUE(sun_cons.count("Cas") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["Cas"], "Alp Cas"));
    EXPECT_TRUE(has_line_with_star(sun_cons["Cas"], "Bet Cas"));

    ASSERT_TRUE(sun_cons.count("Cru") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["Cru"], "Alp1Cru"));
    EXPECT_TRUE(has_line_with_star(sun_cons["Cru"], "Bet Cru"));

    ASSERT_TRUE(sun_cons.count("Lyr") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["Lyr"], "Alp Lyr"));

    ASSERT_TRUE(sun_cons.count("CMa") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["CMa"], "Alp CMa"));

    ASSERT_TRUE(sun_cons.count("Cyg") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["Cyg"], "Alp Cyg"));

    ASSERT_TRUE(sun_cons.count("Sco") > 0);
    EXPECT_TRUE(has_line_with_star(sun_cons["Sco"], "Alp Sco"));
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

// =====================================================================
// Procedural Constellation Generation Tests
// =====================================================================

class ExoConsTest : public ::testing::Test
{
    protected:
    static void SetUpTestSuite()
    {
        // DO NOT delete exocons.dat - this is deliberately a user-modifiable cache file that the user may wish to customize for their setup.
        if (file_exists("exocons.dat")) std::rename("exocons.dat", "exocons.tstbak.dat");
        ExoConsGenerator::reset();
        if (cels)
        {
            delete[] cels;
            cels = nullptr;
        }
        cels = new CelestialObject*[MAX_CELOBJS];
        memset(cels, 0, MAX_CELOBJS * sizeof(CelestialObject*));
        abort_load = false;
        noexo = true;
        load_stuff();
    }

    static void TearDownTestSuite()
    {
        std::remove("exocons.dat");
        // Restore exocons.dat - this is deliberately a user-modifiable cache file that the user may wish to customize for their setup.
        if (file_exists("exocons.tstbak.dat")) std::rename("exocons.tstbak.dat", "exocons.dat");
        ExoConsGenerator::reset();
        if (cels)
        {
            delete[] cels;
            cels = nullptr;
        }
    }

    void SetUp() override
    {
        // DO NOT delete exocons.dat - this is deliberately a user-modifiable cache file that the user may wish to customize for their setup.
        if (file_exists("exocons.dat")) std::rename("exocons.dat", "exocons.tstbak.dat");
        ExoConsGenerator::reset();
        constellations.clear();
        num_reg_cons = 0;
        read_cons_lines();
    }

    void TearDown() override
    {
        std::remove("exocons.dat");
        // Restore exocons.dat - this is deliberately a user-modifiable cache file that the user may wish to customize for their setup.
        if (file_exists("exocons.tstbak.dat")) std::rename("exocons.tstbak.dat", "exocons.dat");
        ExoConsGenerator::reset();
        constellations.clear();
        num_reg_cons = 0;
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

TEST_F(ExoConsTest, ToBeGenerated_AcceptanceCriteria)
{
    // 47 Ursae Majoris: far from Sun (> 10 ly), no existing constellations -> to be generated
    Star* uma47 = get_star("47 Ursae Majoris");
    if (!uma47)
    {
        uma47 = get_star("47 UMa");
    }
    ASSERT_NE(uma47, nullptr);
    std::string vname_uma;
    EXPECT_TRUE(ExoConsGenerator::check_should_generate_for(uma47, vname_uma));

    // GJ 86: far from Sun (> 10 ly), no existing constellations -> to be generated
    Star* gj86 = get_star("GJ 86");
    ASSERT_NE(gj86, nullptr);
    std::string vname_gj86;
    EXPECT_TRUE(ExoConsGenerator::check_should_generate_for(gj86, vname_gj86));

    // GJ 67: within 10 l.y. of Upsilon Andromedae (which has >= 30 constellations defined)
    // Uses Upsilon Andromedae's lines without generating new constellations
    Star* gj67 = get_star("GJ 67");
    ASSERT_NE(gj67, nullptr);
    Star* ups = get_star("Upsilon Andromedae");
    if (!ups)
    {
        ups = get_star("Ups And");
    }
    ASSERT_NE(ups, nullptr);
    EXPECT_LT(gj67->location.distance_to(ups->location), light_year * 10.0);

    std::vector<Constellation> ups_generated;
    ExoConsGenerator::generate_constellations(ups, ups_generated);
    EXPECT_GE(ups_generated.size(), 30u);
    for (const auto& c : ups_generated)
    {
        constellations.push_back(c);
    }

    std::string vname_gj67;
    EXPECT_FALSE(ExoConsGenerator::check_should_generate_for(gj67, vname_gj67));

    // Alpha Centauri: within 10 l.y. of Sun (4.37 l.y.)
    // Uses heliocentric lines and does not generate new constellations
    Star* alp_cen = get_star("Alp1Cen");
    if (!alp_cen)
    {
        alp_cen = get_star("Alpha Centauri");
    }
    ASSERT_NE(alp_cen, nullptr);
    std::string vname_ac;
    EXPECT_FALSE(ExoConsGenerator::check_should_generate_for(alp_cen, vname_ac));

    // Barnard's Star: within 10 l.y. of Sun (5.96 l.y.)
    // Uses heliocentric lines and does not generate new constellations
    Star* barnard = get_star("Barnard's Star");
    ASSERT_NE(barnard, nullptr);
    std::string vname_bs;
    EXPECT_FALSE(ExoConsGenerator::check_should_generate_for(barnard, vname_bs));

    // Sirius: within 10 l.y. of Sun (8.6 l.y.)
    // Uses heliocentric lines and does not generate new constellations
    Star* sirius = get_star("Sirius");
    if (!sirius)
    {
        sirius = get_star("Alp CMa");
    }
    ASSERT_NE(sirius, nullptr);
    std::string vname_sirius;
    EXPECT_FALSE(ExoConsGenerator::check_should_generate_for(sirius, vname_sirius));

    // Alpha Mensae: has only 2 constellations defined in consline.dat (< 30)
    // Retains custom shapes and generates constellations from stars not already joined
    Star* alp_men = get_star("Alpha Mensae");
    if (!alp_men)
    {
        alp_men = get_star("Alp Men");
    }
    ASSERT_NE(alp_men, nullptr);
    std::string vname_men;
    EXPECT_TRUE(ExoConsGenerator::check_should_generate_for(alp_men, vname_men));
}

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
    num_reg_cons = 0;
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

TEST_F(ExoConsTest, NearbyVantageWithin10LightYears_SuppressesExoconsGeneration)
{
    Star* tau_cet = get_star("Tau Ceti");
    if (!tau_cet)
    {
        tau_cet = get_star("tau Cet");
    }
    ASSERT_NE(tau_cet, nullptr);

    Star* eps_eri = get_star("Epsilon Eridani");
    if (!eps_eri)
    {
        eps_eri = get_star("eps Eri");
    }
    ASSERT_NE(eps_eri, nullptr);

    // Verify physical distance between Tau Ceti and Epsilon Eridani is within 10 light years
    double dist = tau_cet->location.distance_to(eps_eri->location);
    EXPECT_LT(dist, light_year * 10.0);
    EXPECT_GT(dist, light_year * 0.1);

    // Generate constellations for Tau Ceti and save to exocons.dat
    ExoConsGenerator::generate_all_synchronous(tau_cet);

    // Verify exocons.dat has Tau Ceti constellations
    constellations.clear();
    num_reg_cons = 0;
    read_cons_lines();

    int tau_cet_count = 0;
    for (const auto& c : constellations)
    {
        if (c.vantage_name == "Tau Ceti" || c.vantage_name == "tau Cet"
            || (!std::isnan(c.vantage.x) && c.vantage.distance_to(tau_cet->location) < light_year * 0.1))
        {
            tau_cet_count++;
        }
    }
    EXPECT_GE(tau_cet_count, 50);

    // When at Epsilon Eridani, check_should_generate_for must return false because a nearby star (Tau Ceti)
    // within 10 light years already has constellations generated.
    std::string vname;
    bool should_gen = ExoConsGenerator::check_should_generate_for(eps_eri, vname);
    EXPECT_FALSE(should_gen);

    // Also verify when cache_cons_lines has executed
    cache_cons_lines();

    // After caching, vantage_name must remain intact and generation must still be suppressed
    should_gen = ExoConsGenerator::check_should_generate_for(eps_eri, vname);
    EXPECT_FALSE(should_gen);

    // Verify update_frame does not initiate generation for Epsilon Eridani
    whereami = find_object("Epsilon Eridani", true);
    mycenobj = eps_eri;
    here = eps_eri->location;
    ExoConsGenerator::update_frame();
    EXPECT_FALSE(ExoConsGenerator::get_is_generating());
}

TEST_F(ExoConsTest, BrightStarsJoinedAndNormalSizedConstellationsFormed)
{
    Star* uma47 = get_star("47 Ursae Majoris");
    if (!uma47)
    {
        uma47 = get_star("47 UMa");
    }
    ASSERT_NE(uma47, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(uma47, generated);

    // Acceptance criterion: at least 50 constellations form
    EXPECT_GE(generated.size(), 50u);

    std::unordered_set<Star*> lined_stars;
    for (const auto& c : generated)
    {
        // Little clusters get connected to form normal sized constellations (at least 3 lines)
        EXPECT_GE(c.lines.size(), 3u);

        for (const auto& cl : c.lines)
        {
            ASSERT_NE(cl.a, nullptr);
            ASSERT_NE(cl.b, nullptr);
            lined_stars.insert(cl.a);
            lined_stars.insert(cl.b);
        }
    }

    // Verify no star brighter than magnitude 3 gets ignored for line joining
    CelestialLocation vantage_loc = uma47->location;
    CelestialObject* local_cenobj = uma47->cenobj ? uma47->cenobj : uma47;
    int bright_count = 0;
    int unconnected_bright_count = 0;

    for (int i = 0; cels[i]; i++)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star)
        {
            continue;
        }
        Star* s = (Star*)cels[i];
        if (s == uma47 || s == local_cenobj || s->cenobj == local_cenobj || s->cenobj == uma47)
        {
            continue;
        }
        if (s->cenobj && s->cenobj != s && s->cenobj->typeclass() == class_star)
        {
            Star* primary = (Star*)s->cenobj;
            if (primary->viewer_magnitude(vantage_loc) <= s->viewer_magnitude(vantage_loc))
            {
                continue;
            }
        }
        double mag = s->viewer_magnitude(vantage_loc);
        if (std::isnan(mag) || std::isinf(mag))
        {
            continue;
        }
        if (mag < 3.0)
        {
            bright_count++;
            if (!lined_stars.count(s))
            {
                unconnected_bright_count++;
                printf("DEBUG unconnected: %s (mag %.2f)\n", s->name, mag);
            }
        }
    }

    EXPECT_GT(bright_count, 50);
    EXPECT_EQ(unconnected_bright_count, 0);
}

TEST_F(ExoConsTest, PasserbyLinesConnectToImpingingStars)
{
    Star* uma47 = get_star("47 Ursae Majoris");
    if (!uma47)
    {
        uma47 = get_star("47 UMa");
    }
    ASSERT_NE(uma47, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(uma47, generated);

    EXPECT_GE(generated.size(), 50u);

    struct LineInfo
    {
        Star* a;
        Star* b;
        Point ua;
        Point ub;
    };
    std::vector<LineInfo> all_lines;
    std::unordered_set<Star*> lined_stars;

    Point vantage_pt = uma47->location;
    CelestialLocation vantage_loc = uma47->location;

    for (const auto& c : generated)
    {
        EXPECT_GE(c.lines.size(), 3u);
        for (const auto& cl : c.lines)
        {
            ASSERT_NE(cl.a, nullptr);
            ASSERT_NE(cl.b, nullptr);
            lined_stars.insert(cl.a);
            lined_stars.insert(cl.b);

            Point pa = (Point)cl.a->location - vantage_pt;
            Point pb = (Point)cl.b->location - vantage_pt;
            Point ua = pa * (1.0 / pa.magnitude());
            Point ub = pb * (1.0 / pb.magnitude());
            all_lines.push_back({cl.a, cl.b, ua, ub});
        }
    }

    std::vector<std::pair<Star*, Point>> impinging_stars;
    for (int i = 0; cels[i]; i++)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        Star* s = (Star*)cels[i];
        if (s == uma47 || s == uma47->cenobj || s->cenobj == uma47) continue;

        if (s->cenobj && s->cenobj != s && s->cenobj->typeclass() == class_star)
        {
            Star* primary = (Star*)s->cenobj;
            if (primary->viewer_magnitude(vantage_loc) <= s->viewer_magnitude(vantage_loc))
            {
                continue;
            }
        }

        double mag = s->viewer_magnitude(vantage_loc);
        if (std::isnan(mag) || std::isinf(mag)) continue;

        bool is_joined = lined_stars.count(s) > 0;
        bool is_bright = (mag < 4.0);

        if (is_joined || is_bright)
        {
            Point rel = (Point)s->location - vantage_pt;
            if (rel.magnitude() > 1e-9)
            {
                impinging_stars.push_back(std::make_pair(s, rel * (1.0 / rel.magnitude())));
            }
        }
    }

    int near_miss_count = 0;
    for (const auto& l : all_lines)
    {
        for (const auto& is : impinging_stars)
        {
            if (is.first == l.a || is.first == l.b) continue;

            double dist_deg = 0.0;
            if (ExoConsGenerator::point_near_arc(l.ua, l.ub, is.second, 1.5, &dist_deg, 0.8))
            {
                near_miss_count++;
                printf("DEBUG near miss: %s -- %s near %s (dist %.2f)\n", l.a->name, l.b->name, is.first->name, dist_deg);
            }
        }
    }

    EXPECT_EQ(near_miss_count, 0);
}

TEST_F(ExoConsTest, HamalConstellationsFormConnectedShapesWithoutStraySingleLines)
{
    Star* hamal = get_star("Hamal");
    if (!hamal)
    {
        hamal = get_star("Alp Ari");
    }
    ASSERT_NE(hamal, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(hamal, generated);

    EXPECT_GE(generated.size(), 50u);

    int isolated_single_lines_count = 0;
    const Constellation* and_cons = nullptr;

    for (const auto& c : generated)
    {
        EXPECT_GE(c.lines.size(), 3u);
        if (c.abbrev == "And")
        {
            and_cons = &c;
        }

        std::unordered_map<Star*, std::vector<Star*>> adj;
        for (const auto& cl : c.lines)
        {
            adj[cl.a].push_back(cl.b);
            adj[cl.b].push_back(cl.a);
        }

        std::unordered_set<Star*> visited;
        for (const auto& pair : adj)
        {
            if (visited.count(pair.first))
            {
                continue;
            }
            int comp_stars = 0;
            std::vector<Star*> q;
            q.push_back(pair.first);
            visited.insert(pair.first);

            std::vector<Star*> comp_star_list;
            while (!q.empty())
            {
                Star* curr = q.back();
                q.pop_back();
                comp_stars++;
                comp_star_list.push_back(curr);

                for (Star* nbr : adj[curr])
                {
                    if (!visited.count(nbr))
                    {
                        visited.insert(nbr);
                        q.push_back(nbr);
                    }
                }
            }

            // A 2-star component with 1 line is an isolated single-line hair-trimming
            if (comp_stars <= 2)
            {
                std::cout << "DEBUG isolated single line in " << c.abbrev << ": ";
                for (Star* s : comp_star_list)
                {
                    std::cout << (s->name[0] ? s->name : "unnamed") << " ";
                }
                std::cout << std::endl;
                isolated_single_lines_count++;
            }
        }
    }


    EXPECT_EQ(isolated_single_lines_count, 0);

    // Verify Mirach is part of Andromeda and in a connected component with at least 3 lines
    Star* mirach = get_star("Mirach");
    if (!mirach)
    {
        mirach = get_star("Bet And");
    }
    ASSERT_NE(mirach, nullptr);
    ASSERT_NE(and_cons, nullptr);

    bool mirach_in_and = false;
    int mirach_comp_stars = 0;

    std::unordered_map<Star*, std::vector<Star*>> and_adj;
    for (const auto& cl : and_cons->lines)
    {
        and_adj[cl.a].push_back(cl.b);
        and_adj[cl.b].push_back(cl.a);
        if (cl.a == mirach || cl.b == mirach)
        {
            mirach_in_and = true;
        }
    }

    EXPECT_TRUE(mirach_in_and);

    if (mirach_in_and)
    {
        std::unordered_set<Star*> visited;
        std::vector<Star*> q;
        q.push_back(mirach);
        visited.insert(mirach);

        while (!q.empty())
        {
            Star* curr = q.back();
            q.pop_back();
            mirach_comp_stars++;

            for (Star* nbr : and_adj[curr])
            {
                if (!visited.count(nbr))
                {
                    visited.insert(nbr);
                    q.push_back(nbr);
                }
            }
        }
    }

    // Mirach should be part of a connected shape with at least 4 stars (>= 3 lines)
    EXPECT_GE(mirach_comp_stars, 4);
}

TEST_F(ExoConsTest, TauCetiNearLinesAndReassignment)
{
    Star* tau_cet = get_star("Tau Ceti");
    if (!tau_cet)
    {
        tau_cet = get_star("tau Cet");
    }
    ASSERT_NE(tau_cet, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(tau_cet, generated);

    EXPECT_GE(generated.size(), 50u);

    Star* alp_aql = get_star("Alp Aql");
    if (!alp_aql)
    {
        alp_aql = get_star("Altair");
    }
    Star* alp1_her = get_star("Alp1Her");
    if (!alp1_her)
    {
        alp1_her = get_star("Ras Algethi");
    }
    Star* her_95 = get_star("95 Her");
    Star* nu_oph = get_star("Nu Oph");
    Star* zet_sct = get_star("Zet Sct");
    Star* mu_oph = get_star("Mu Oph");

    bool has_altair_alp1her = false;
    bool has_altair_95her = false;
    bool has_nuoph_zetsct = false;
    bool has_nuoph_muoph = false;

    for (const auto& c : generated)
    {
        for (const auto& cl : c.lines)
        {
            if (alp_aql && alp1_her)
            {
                if ((cl.a == alp_aql && cl.b == alp1_her) || (cl.a == alp1_her && cl.b == alp_aql))
                {
                    has_altair_alp1her = true;
                }
            }
            if (alp_aql && her_95)
            {
                if ((cl.a == alp_aql && cl.b == her_95) || (cl.a == her_95 && cl.b == alp_aql))
                {
                    has_altair_95her = true;
                }
            }
            if (nu_oph && zet_sct)
            {
                if ((cl.a == nu_oph && cl.b == zet_sct) || (cl.a == zet_sct && cl.b == nu_oph))
                {
                    has_nuoph_zetsct = true;
                }
            }
            if (nu_oph && mu_oph)
            {
                if ((cl.a == nu_oph && cl.b == mu_oph) || (cl.a == mu_oph && cl.b == nu_oph))
                {
                    has_nuoph_muoph = true;
                }
            }
        }
    }


    // Phase B: Long diagonal lines to Altair are divorced and eliminated
    EXPECT_FALSE(has_altair_alp1her);
    EXPECT_FALSE(has_altair_95her);

    // Phase C: Spurious cross-constellation horizontal bridge line Nu Oph - Zet Sct is removed
    EXPECT_FALSE(has_nuoph_zetsct);

    // Nu Oph connects to natural neighbor Mu Oph
    if (nu_oph && mu_oph)
    {
        EXPECT_TRUE(has_nuoph_muoph);
    }
}

TEST_F(ExoConsTest, NearestVantageSelectionAlpCen)
{
    Star* alp_cen = get_star("Alp1Cen");
    if (!alp_cen)
    {
        alp_cen = get_star("Alpha Centauri");
    }
    ASSERT_NE(alp_cen, nullptr);

    std::string vname;
    bool should_gen = ExoConsGenerator::check_should_generate_for(alp_cen, vname);
    EXPECT_FALSE(should_gen);

    // Distance to Sol must be within 10 light years
    Star* sun = (Star*)cels[0];
    ASSERT_NE(sun, nullptr);
    double dist_to_sun = alp_cen->location.distance_to(sun->location);
    EXPECT_LT(dist_to_sun, light_year * 10.0);

    // Distance to other defined exocons vantage (e.g. Tau Ceti) must be > 10 light years
    Star* tau_cet = get_star("Tau Ceti");
    if (!tau_cet)
    {
        tau_cet = get_star("tau Cet");
    }
    if (tau_cet)
    {
        double dist_to_tau = alp_cen->location.distance_to(tau_cet->location);
        EXPECT_GT(dist_to_tau, light_year * 10.0);
    }
}

TEST_F(ExoConsTest, GenerateConstellationsDurationBenchmark)
{
    Star* uma47 = get_star("47 Ursae Majoris");
    if (!uma47)
    {
        uma47 = get_star("47 UMa");
    }
    ASSERT_NE(uma47, nullptr);

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(uma47, generated);
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_GE(generated.size(), 50u);
    EXPECT_LT(elapsed_ms, 5000);
}



