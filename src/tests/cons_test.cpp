#include <gtest/gtest.h>
#include <cmath>
#include <iomanip>
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
    static void SetUpTestSuite()
    {
        if (file_exists("exocons.dat"))
        {
            if (!file_exists("exocons.tstbak.dat"))
            {
                std::rename("exocons.dat", "exocons.tstbak.dat");
            }
            else
            {
                std::remove("exocons.dat");
            }
        }
    }

    static void TearDownTestSuite()
    {
        if (file_exists("exocons.tstbak.dat"))
        {
            if (!file_exists("exocons.dat"))
            {
                std::rename("exocons.tstbak.dat", "exocons.dat");
            }
        }
    }

    void SetUp() override
    {
        // Clear global constellations before each test
        constellations.clear();
        num_reg_cons = 0;
        
        // Reload imports explicit stellar systems, so it requires the application-sized array.
        cels = new CelestialObject*[MAX_CELOBJS]();
        ncelobjs = 0;
    }

    void TearDown() override
    {
        // Clean up any stars we allocated into cels
        for (int i = 0; cels[i]; i++)
        {
            if (cels[i])
            {
                delete cels[i];
                cels[i] = nullptr;
            }
        }
        delete[] cels;
        cels = nullptr;
        ncelobjs = 0;
        
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
    // Polar catalog boundaries determine membership, independent of names.
    Constellation umi;
    umi.name = "Synthetic northern cap";
    umi.abbrev = "NCap";
    Constellation oct;
    oct.name = "Synthetic southern cap";
    oct.abbrev = "SCap";
    for (double ra : {0.0, 2 * _pi / 3, 4 * _pi / 3, 2 * _pi})
    {
        ConsBoundary north;
        north.RA = ra;
        north.decl = 88 * fiftyseventh;
        umi.bounds.push_back(north);
        ConsBoundary south = north;
        south.decl = -north.decl;
        oct.bounds.push_back(south);
    }

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
    
    EXPECT_EQ(found_north->abbrev, "NCap");
    EXPECT_EQ(found_south->abbrev, "SCap");
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
    const auto& definitions = iau_constellation_definitions();
    ASSERT_EQ(definitions.size(), EXOCONS_NUM_IAU_CONSTELLATIONS);
    for (const auto& entry : definitions)
    {
        EXPECT_TRUE(sun_cons.count(entry.abbrev) > 0) << "Missing IAU constellation: " << entry.abbrev;
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
        // Constellation has scientific/custom name, abbreviation, and genitive
        EXPECT_FALSE(c.name.empty());
        EXPECT_FALSE(c.abbrev.empty());
        EXPECT_FALSE(c.genitive.empty());

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
            double angle_deg = acos(std::max(-1.0, std::min(1.0, cos_ang))) * fiftyseven;
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
            double angle_deg = acos(std::max(-1.0, std::min(1.0, cos_ang))) * fiftyseven;
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

    // Test saving exocons without writing a bunch of stuff to exocons.dat
    std::vector<Constellation> sample_cons;
    sample_cons.push_back(generated[0]);
    ExoConsGenerator::save_to_exocons_file("47 Ursae Majoris", sample_cons);

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
    EXPECT_EQ(uma_cons_count, 1);
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

    // Generate constellations for Tau Ceti synchronously into memory
    ExoConsGenerator::generate_all_synchronous(tau_cet);

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
                const double separation = find_3D_angle(s->location, primary->location, vantage_loc) * fiftyseven;
                if (separation < 0.1 || primary->viewer_magnitude(vantage_loc) <= s->viewer_magnitude(vantage_loc))
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
            const double separation = find_3D_angle(s->location, primary->location, vantage_loc) * fiftyseven;
            if (separation < 0.1 || primary->viewer_magnitude(vantage_loc) <= s->viewer_magnitude(vantage_loc))
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

    for (const auto& c : generated)
    {
        EXPECT_GE(c.lines.size(), 3u);

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
    const Constellation* mirach_cons = nullptr;
    for (const auto& c : generated)
    {
        for (const auto& cl : c.lines)
        {
            if (cl.a == mirach || cl.b == mirach)
            {
                mirach_cons = &c;
                break;
            }
        }
        if (mirach_cons)
        {
            break;
        }
    }
    ASSERT_NE(mirach_cons, nullptr);

    int mirach_comp_stars = 0;
    std::unordered_map<Star*, std::vector<Star*>> mirach_adj;
    for (const auto& cl : mirach_cons->lines)
    {
        mirach_adj[cl.a].push_back(cl.b);
        mirach_adj[cl.b].push_back(cl.a);
    }

    std::unordered_set<Star*> visited;
    std::vector<Star*> q;
    q.push_back(mirach);
    visited.insert(mirach);

    while (!q.empty())
    {
        Star* curr = q.back();
        q.pop_back();
        mirach_comp_stars++;

        for (Star* nbr : mirach_adj[curr])
        {
            if (!visited.count(nbr))
            {
                visited.insert(nbr);
                q.push_back(nbr);
            }
        }
    }

    // Mirach should be part of a connected shape with at least 4 stars (>= 3 lines)
    EXPECT_GE(mirach_comp_stars, 4);
}

TEST_F(ExoConsTest, TauCetiPlacesSunAndArcturusInSameConstellation)
{
    Star* tau_cet = get_star("Tau Ceti");
    if (!tau_cet) tau_cet = get_star("tau Cet");
    ASSERT_NE(tau_cet, nullptr);
    Star* sun = get_star("Sun");
    if (!sun) sun = get_star("Sol");
    ASSERT_NE(sun, nullptr);
    Star* arcturus = get_star("Arcturus");
    if (!arcturus) arcturus = get_star("Alp Boo");
    ASSERT_NE(arcturus, nullptr);

    std::vector<Constellation> generated;
    ExoConsGenerator::generate_constellations(tau_cet, generated);
    EXPECT_GE(generated.size(), 50u);

    bool shared_constellation = false;
    for (const auto& constellation : generated)
    {
        bool has_sun = false;
        bool has_arcturus = false;
        for (const auto& line : constellation.lines)
        {
            has_sun = has_sun || line.a == sun || line.b == sun;
            has_arcturus = has_arcturus || line.a == arcturus || line.b == arcturus;
        }
        shared_constellation = shared_constellation || (has_sun && has_arcturus);
    }
    EXPECT_TRUE(shared_constellation) << "The Sun and Arcturus should share a constellation from Tau Ceti.";
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
TEST_F(ExoConsTest, LineSeparationFromOtherConstellations)
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

    Point vantage_pt = uma47->location;

    // Collect connected stars per constellation
    std::vector<std::unordered_set<Star*>> connected_stars(generated.size());
    for (size_t i = 0; i < generated.size(); i++)
    {
        for (const auto& cl : generated[i].lines)
        {
            connected_stars[i].insert(cl.a);
            connected_stars[i].insert(cl.b);
        }
    }

    // Verify: Lines should not connect any star that is less than half the line length
    // from a connected star of a different constellation.
    for (size_t i = 0; i < generated.size(); i++)
    {
        for (const auto& cl : generated[i].lines)
        {
            Point pa = (Point)cl.a->location - vantage_pt;
            Point pb = (Point)cl.b->location - vantage_pt;
            double len_a = pa.magnitude();
            double len_b = pb.magnitude();
            if (len_a < 1e-9 || len_b < 1e-9)
            {
                continue;
            }
            Point ua = pa * (1.0 / len_a);
            Point ub = pb * (1.0 / len_b);
            double cos_ang = ua.x * ub.x + ua.y * ub.y + ua.z * ub.z;
            double len_deg = acos(std::max(-1.0, std::min(1.0, cos_ang))) * fiftyseven;
            double half_len = 0.5 * len_deg;

            for (size_t j = 0; j < generated.size(); j++)
            {
                if (i == j)
                {
                    continue;
                }
                for (Star* other : connected_stars[j])
                {
                    Point po = (Point)other->location - vantage_pt;
                    double len_o = po.magnitude();
                    if (len_o < 1e-9)
                    {
                        continue;
                    }
                    Point uo = po * (1.0 / len_o);

                    double cos_a = ua.x * uo.x + ua.y * uo.y + ua.z * uo.z;
                    double cos_b = ub.x * uo.x + ub.y * uo.y + ub.z * uo.z;
                    double dist_a = acos(std::max(-1.0, std::min(1.0, cos_a))) * fiftyseven;
                    double dist_b = acos(std::max(-1.0, std::min(1.0, cos_b))) * fiftyseven;

                    EXPECT_GE(dist_a, half_len - 1e-4);
                    EXPECT_GE(dist_b, half_len - 1e-4);
                }
            }
        }
    }
}

TEST_F(ExoConsTest, HeliocentricComparison_QuantifySimilarityWithConsline)
{
    Star* sun = (Star*)cels[0];
    if (!sun || strcmp(sun->name, "Sun") != 0)
    {
        sun = get_star("Sun");
    }
    if (!sun)
    {
        sun = get_star("Sol");
    }
    ASSERT_NE(sun, nullptr);

    // Resolve star pointers for loaded constellations
    cache_cons_lines();

    Point sun_pt = sun->location;

    // 1. Separate reference heliocentric constellations (from consline.dat) from others
    std::vector<Constellation> reference_cons;
    std::vector<Constellation> non_heliocentric;
    for (const auto& c : constellations)
    {
        if (c.vantage_name.empty() || c.vantage_name == "Sun" || c.vantage_name == "Sol")
        {
            reference_cons.push_back(c);
        }
        else
        {
            non_heliocentric.push_back(c);
        }
    }

    ASSERT_FALSE(reference_cons.empty());

    // 2. Wipe heliocentric constellations so generate_constellations starts from a blank slate at [0,0,0]
    constellations = non_heliocentric;

    // 3. Generate procedural constellations for the Solar System ([0,0,0])
    std::vector<Constellation> generated_cons;
    ExoConsGenerator::generate_constellations(sun, generated_cons);

    // 4. Restore original constellations
    constellations = reference_cons;
    for (const auto& c : non_heliocentric)
    {
        constellations.push_back(c);
    }

    // 5. Quantify similarity and variance

    // Helper: calculate angular span of a constellation (degrees)
    auto get_cons_span_deg = [](const Constellation& c, const Point& vantage) -> double
    {
        std::vector<Point> uvec;
        for (const auto& cl : c.lines)
        {
            if (cl.a && cl.b)
            {
                Point pa = (Point)cl.a->location - vantage;
                Point pb = (Point)cl.b->location - vantage;
                double la = pa.magnitude();
                double lb = pb.magnitude();
                if (la > 1e-9)
                {
                    uvec.push_back(pa * (1.0 / la));
                }
                if (lb > 1e-9)
                {
                    uvec.push_back(pb * (1.0 / lb));
                }
            }
        }
        double max_deg = 0.0;
        for (size_t i = 0; i < uvec.size(); i++)
        {
            for (size_t j = i + 1; j < uvec.size(); j++)
            {
                double dot = uvec[i].x * uvec[j].x + uvec[i].y * uvec[j].y + uvec[i].z * uvec[j].z;
                dot = std::max(-1.0, std::min(1.0, dot));
                double deg = acos(dot) * fiftyseven;
                if (deg > max_deg)
                {
                    max_deg = deg;
                }
            }
        }
        return max_deg;
    };

    // Helper: calculate length of a line in degrees
    auto get_line_len_deg = [](Star* a, Star* b, const Point& vantage) -> double
    {
        Point pa = (Point)a->location - vantage;
        Point pb = (Point)b->location - vantage;
        double la = pa.magnitude();
        double lb = pb.magnitude();
        if (la < 1e-9 || lb < 1e-9)
        {
            return 0.0;
        }
        Point ua = pa * (1.0 / la);
        Point ub = pb * (1.0 / lb);
        double dot = ua.x * ub.x + ua.y * ub.y + ua.z * ub.z;
        dot = std::max(-1.0, std::min(1.0, dot));
        return acos(dot) * fiftyseven;
    };

    // Collect Reference sets
    std::set<std::pair<Star*, Star*>> ref_lines;
    std::unordered_set<Star*> ref_stars;
    std::vector<double> ref_spans;
    std::vector<double> ref_line_lengths;

    for (const auto& c : reference_cons)
    {
        double span = get_cons_span_deg(c, sun_pt);
        if (span > 0.0)
        {
            ref_spans.push_back(span);
        }
        for (const auto& cl : c.lines)
        {
            if (cl.a && cl.b)
            {
                Star* sa = std::min(cl.a, cl.b);
                Star* sb = std::max(cl.a, cl.b);
                ref_lines.insert({sa, sb});
                ref_stars.insert(cl.a);
                ref_stars.insert(cl.b);
                ref_line_lengths.push_back(get_line_len_deg(cl.a, cl.b, sun_pt));
            }
        }
    }

    // Diagnostic check: test Pass 1, 2, 3 included stars vs reference_cons
    struct StarCand { Star* s; double mag; Point u; std::string orig_cons; };
    std::vector<StarCand> all_cands;
    for (int i = 0; cels[i]; i++)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        Star* s = (Star*)cels[i];
        if (s == sun || s->cenobj == sun) continue;
        double mag = s->viewer_magnitude(sun->location);
        if (std::isnan(mag) || std::isinf(mag) || mag > 8.0) continue;
        Point rel = (Point)s->location - sun_pt;
        if (rel.magnitude() < 1e-9) continue;
        all_cands.push_back({s, mag, rel * (1.0 / rel.magnitude()), ExoConsGenerator::extract_star_cons_abbrev(s)});
    }

    std::vector<StarCand> inc_cands;
    std::unordered_set<Star*> inc_set;
    for (const auto& c : all_cands)
    {
        if (c.mag < 4.0)
        {
            inc_cands.push_back(c);
            inc_set.insert(c.s);
        }
    }
    double cos4 = cos(4.0 * fiftyseventh);
    for (int iter = 0; iter < 2; iter++)
    {
        for (const auto& c : all_cands)
        {
            if (c.mag < 5.0 && !inc_set.count(c.s))
            {
                for (const auto& inc : inc_cands)
                {
                    if ((c.u.x * inc.u.x + c.u.y * inc.u.y + c.u.z * inc.u.z) >= cos4)
                    {
                        inc_cands.push_back(c);
                        inc_set.insert(c.s);
                        break;
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, std::unordered_set<Star*>> inc_by_cons;
    for (const auto& c : inc_cands)
    {
        inc_by_cons[c.orig_cons].insert(c.s);
    }

    double diag_sum_jaccard = 0.0;
    int diag_count = 0;
    for (const auto& rc : reference_cons)
    {
        std::string expected_abbr = rc.abbrev;
        if (expected_abbr == "Ser1" || expected_abbr == "Ser2")
        {
            expected_abbr = "Ser";
        }
        std::unordered_set<Star*> unique_rc_stars;
        for (const auto& cl : rc.lines)
        {
            if (cl.a && cl.b)
            {
                unique_rc_stars.insert(cl.a);
                unique_rc_stars.insert(cl.b);
            }
        }
        if (unique_rc_stars.empty()) continue;

        const auto& c_stars = inc_by_cons[expected_abbr];
        int shared = 0;
        for (Star* s : c_stars)
        {
            if (unique_rc_stars.count(s)) shared++;
        }
        int un = (int)(unique_rc_stars.size() + c_stars.size()) - shared;
        double jac = (un > 0) ? (double)shared / un : 0.0;
        diag_sum_jaccard += jac;
        diag_count++;
    }
    // Check line separation on reference_cons
    int ref_line_sep_violations = 0;
    for (size_t i = 0; i < reference_cons.size(); i++)
    {
        for (const auto& cl : reference_cons[i].lines)
        {
            if (!cl.a || !cl.b) continue;
            Point pa = (Point)cl.a->location - sun_pt;
            Point pb = (Point)cl.b->location - sun_pt;
            Point ua = pa * (1.0 / pa.magnitude());
            Point ub = pb * (1.0 / pb.magnitude());
            double len_deg = acos(std::max(-1.0, std::min(1.0, ua.x*ub.x + ua.y*ub.y + ua.z*ub.z))) * fiftyseven;
            double half_len = 0.5 * len_deg;

            for (size_t j = 0; j < reference_cons.size(); j++)
            {
                if (i == j) continue;
                for (const auto& other_cl : reference_cons[j].lines)
                {
                    for (Star* other : {other_cl.a, other_cl.b})
                    {
                        if (!other) continue;
                        Point po = (Point)other->location - sun_pt;
                        Point uo = po * (1.0 / po.magnitude());
                        double dist_a = acos(std::max(-1.0, std::min(1.0, ua.x*uo.x + ua.y*uo.y + ua.z*uo.z))) * fiftyseven;
                        double dist_b = acos(std::max(-1.0, std::min(1.0, ub.x*uo.x + ub.y*uo.y + ub.z*uo.z))) * fiftyseven;
                        if (dist_a < half_len - 1e-4 || dist_b < half_len - 1e-4)
                        {
                            ref_line_sep_violations++;
                        }
                    }
                }
            }
        }
    }
    std::cout << "DIAGNOSTIC: reference_cons has " << ref_line_sep_violations << " line separation violations!" << std::endl;

    for (const std::string& target_abbr : {"Ori", "Cas", "CrB", "Del", "UMa"})
    {
        for (const auto& rc : reference_cons)
        {
            if (rc.abbrev != target_abbr) continue;
            std::unordered_set<Star*> unique_rc_stars;
            for (const auto& cl : rc.lines)
            {
                if (cl.a && cl.b)
                {
                    unique_rc_stars.insert(cl.a);
                    unique_rc_stars.insert(cl.b);
                }
            }
            const auto& c_stars = inc_by_cons[target_abbr];
            std::cout << "\n--- Constellation " << target_abbr << " ---" << std::endl;
            std::cout << "Ref stars (" << unique_rc_stars.size() << "): ";
            for (Star* s : unique_rc_stars) std::cout << (s->name[0] ? s->name : s->Bayer) << " (" << s->viewer_magnitude(sun->location) << "), ";
            std::cout << "\nInc stars (" << c_stars.size() << "): ";
            for (Star* s : c_stars) std::cout << (s->name[0] ? s->name : s->Bayer) << " (" << s->viewer_magnitude(sun->location) << "), ";
            std::cout << std::endl;
        }
    }

    // Collect Generated sets
    std::set<std::pair<Star*, Star*>> gen_lines;
    std::unordered_set<Star*> gen_stars;
    std::vector<double> gen_spans;
    std::vector<double> gen_line_lengths;

    for (const auto& c : generated_cons)
    {
        double span = get_cons_span_deg(c, sun_pt);
        if (span > 0.0)
        {
            gen_spans.push_back(span);
        }
        for (const auto& cl : c.lines)
        {
            if (cl.a && cl.b)
            {
                Star* sa = std::min(cl.a, cl.b);
                Star* sb = std::max(cl.a, cl.b);
                gen_lines.insert({sa, sb});
                gen_stars.insert(cl.a);
                gen_stars.insert(cl.b);
                gen_line_lengths.push_back(get_line_len_deg(cl.a, cl.b, sun_pt));
            }
        }
    }

    // Line overlap
    int matching_lines = 0;
    for (const auto& l : gen_lines)
    {
        if (ref_lines.count(l))
        {
            matching_lines++;
        }
    }

    // Star overlap
    int shared_stars = 0;
    for (Star* s : gen_stars)
    {
        if (ref_stars.count(s))
        {
            shared_stars++;
        }
    }

    // Line stats
    double line_recall = ref_lines.empty() ? 0.0 : (matching_lines * 100.0) / ref_lines.size();
    double line_precision = gen_lines.empty() ? 0.0 : (matching_lines * 100.0) / gen_lines.size();
    int line_union = (int)(ref_lines.size() + gen_lines.size()) - matching_lines;
    double line_jaccard = (line_union > 0) ? (matching_lines * 100.0) / line_union : 0.0;

    // Star stats
    double star_recall = ref_stars.empty() ? 0.0 : (shared_stars * 100.0) / ref_stars.size();
    double star_precision = gen_stars.empty() ? 0.0 : (shared_stars * 100.0) / gen_stars.size();
    int star_union = (int)(ref_stars.size() + gen_stars.size()) - shared_stars;
    double star_jaccard = (star_union > 0) ? (shared_stars * 100.0) / star_union : 0.0;

    // Span stats
    auto compute_stats = [](const std::vector<double>& v, double& min_v, double& max_v, double& mean_v, double& std_v)
    {
        if (v.empty())
        {
            min_v = max_v = mean_v = std_v = 0.0;
            return;
        }
        min_v = 1e9;
        max_v = -1e9;
        double sum = 0.0;
        for (double x : v)
        {
            if (x < min_v)
            {
                min_v = x;
            }
            if (x > max_v)
            {
                max_v = x;
            }
            sum += x;
        }
        mean_v = sum / v.size();
        double sum_sq = 0.0;
        for (double x : v)
        {
            sum_sq += (x - mean_v) * (x - mean_v);
        }
        std_v = sqrt(sum_sq / v.size());
    };

    double ref_min_span, ref_max_span, ref_mean_span, ref_std_span;
    compute_stats(ref_spans, ref_min_span, ref_max_span, ref_mean_span, ref_std_span);

    double gen_min_span, gen_max_span, gen_mean_span, gen_std_span;
    compute_stats(gen_spans, gen_min_span, gen_max_span, gen_mean_span, gen_std_span);

    double ref_min_len, ref_max_len, ref_mean_len, ref_std_len;
    compute_stats(ref_line_lengths, ref_min_len, ref_max_len, ref_mean_len, ref_std_len);

    double gen_min_len, gen_max_len, gen_mean_len, gen_std_len;
    compute_stats(gen_line_lengths, gen_min_len, gen_max_len, gen_mean_len, gen_std_len);

    // Distribution of generated spans:
    int gen_span_under_15 = 0;
    int gen_span_15_to_20 = 0;
    int gen_span_over_20 = 0;
    for (double s : gen_spans)
    {
        if (s < 15.0)
        {
            gen_span_under_15++;
        }
        else if (s <= 20.0)
        {
            gen_span_15_to_20++;
        }
        else
        {
            gen_span_over_20++;
        }
    }

    // Distribution of reference spans:
    int ref_span_under_15 = 0;
    int ref_span_15_to_20 = 0;
    int ref_span_over_20 = 0;
    for (double s : ref_spans)
    {
        if (s < 15.0)
        {
            ref_span_under_15++;
        }
        else if (s <= 20.0)
        {
            ref_span_15_to_20++;
        }
        else
        {
            ref_span_over_20++;
        }
    }

    // Match each reference constellation to best generated constellation
    struct MatchRecord
    {
        std::string ref_abbrev;
        std::string ref_name;
        int ref_star_count = 0;
        std::string gen_abbrev;
        int shared_stars = 0;
        int shared_lines = 0;
        double jaccard = 0.0;
    };
    std::vector<MatchRecord> match_records;
    double sum_best_jaccard = 0.0;

    for (const auto& rc : reference_cons)
    {
        std::unordered_set<Star*> rc_stars;
        std::set<std::pair<Star*, Star*>> rc_lines;
        for (const auto& cl : rc.lines)
        {
            if (cl.a && cl.b)
            {
                rc_stars.insert(cl.a);
                rc_stars.insert(cl.b);
                rc_lines.insert({std::min(cl.a, cl.b), std::max(cl.a, cl.b)});
            }
        }
        if (rc_stars.empty())
        {
            continue;
        }

        double best_jac = 0.0;
        int best_shared_stars = 0;
        int best_shared_lines = 0;
        std::string best_gen_name = "(none)";

        for (const auto& genc : generated_cons)
        {
            int c_shared_stars = 0;
            std::unordered_set<Star*> genc_stars;
            for (const auto& cl : genc.lines)
            {
                if (cl.a && cl.b)
                {
                    genc_stars.insert(cl.a);
                    genc_stars.insert(cl.b);
                }
            }
            for (Star* s : genc_stars)
            {
                if (rc_stars.count(s))
                {
                    c_shared_stars++;
                }
            }
            int un = (int)(rc_stars.size() + genc_stars.size()) - c_shared_stars;
            double jac = (un > 0) ? (double)c_shared_stars / un : 0.0;

            if (jac > best_jac)
            {
                best_jac = jac;
                best_shared_stars = c_shared_stars;
                best_gen_name = genc.abbrev + " (" + genc.name + ")";
                best_shared_lines = 0;
                for (const auto& cl : genc.lines)
                {
                    if (cl.a && cl.b && rc_lines.count({std::min(cl.a, cl.b), std::max(cl.a, cl.b)}))
                    {
                        best_shared_lines++;
                    }
                }
            }
        }

        match_records.push_back({rc.abbrev, rc.name, (int)rc_stars.size(), best_gen_name, best_shared_stars, best_shared_lines, best_jac});
        sum_best_jaccard += best_jac;
    }

    double mean_cons_jaccard = match_records.empty() ? 0.0 : (sum_best_jaccard / match_records.size()) * 100.0;

    // Sort match records by Jaccard descending to highlight closest matches
    std::sort(match_records.begin(), match_records.end(), [](const MatchRecord& a, const MatchRecord& b)
    {
        return a.jaccard > b.jaccard;
    });

    CelestialLocation sun_loc = sun->location;
    // Magnitude stats
    int ref_mag_u3 = 0, ref_mag_34 = 0, ref_mag_45 = 0, ref_mag_56 = 0, ref_mag_o6 = 0;
    for (Star* s : ref_stars)
    {
        double m = s->viewer_magnitude(sun_loc);
        if (m < 3.0)
        {
            ref_mag_u3++;
        }
        else if (m < 4.0)
        {
            ref_mag_34++;
        }
        else if (m < 5.0)
        {
            ref_mag_45++;
        }
        else if (m < 6.0)
        {
            ref_mag_56++;
        }
        else
        {
            ref_mag_o6++;
        }
    }
    int gen_mag_u3 = 0, gen_mag_34 = 0, gen_mag_45 = 0, gen_mag_56 = 0, gen_mag_o6 = 0;
    for (Star* s : gen_stars)
    {
        double m = s->viewer_magnitude(sun_loc);
        if (m < 3.0)
        {
            gen_mag_u3++;
        }
        else if (m < 4.0)
        {
            gen_mag_34++;
        }
        else if (m < 5.0)
        {
            gen_mag_45++;
        }
        else if (m < 6.0)
        {
            gen_mag_56++;
        }
        else
        {
            gen_mag_o6++;
        }
    }

    // Print detailed metrics report
    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  HELIOCENTRIC EXOCONS SIMILARITY & VARIANCE REPORT (consline.dat vs Generated) " << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Constellation Counts:       Reference = " << reference_cons.size()
              << ", Generated = " << generated_cons.size() << std::endl;
    std::cout << "  Unique Constellation Stars: Reference = " << ref_stars.size()
              << ", Generated = " << gen_stars.size() << std::endl;
    std::cout << "    Ref Stars by Mag:         <3.0: " << ref_mag_u3 << " | 3-4: " << ref_mag_34
              << " | 4-5: " << ref_mag_45 << " | 5-6: " << ref_mag_56 << " | >6: " << ref_mag_o6 << std::endl;
    std::cout << "    Gen Stars by Mag:         <3.0: " << gen_mag_u3 << " | 3-4: " << gen_mag_34
              << " | 4-5: " << gen_mag_45 << " | 5-6: " << gen_mag_56 << " | >6: " << gen_mag_o6 << std::endl;
    std::cout << "  Unique Lines:               Reference = " << ref_lines.size()
              << ", Generated = " << gen_lines.size() << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  [Star Overlap]" << std::endl;
    std::cout << "    Shared Stars:             " << shared_stars << std::endl;
    std::cout << "    Star Recall (Ref stars):  " << star_recall << "%" << std::endl;
    std::cout << "    Star Precision:           " << star_precision << "%" << std::endl;
    std::cout << "    Star Jaccard Index:       " << star_jaccard << "%" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  [Line Overlap]" << std::endl;
    std::cout << "    Exactly Matched Lines:    " << matching_lines << std::endl;
    std::cout << "    Line Recall:              " << line_recall << "%" << std::endl;
    std::cout << "    Line Precision:           " << line_precision << "%" << std::endl;
    std::cout << "    Line Jaccard Index:       " << line_jaccard << "%" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  [Constellation Angular Span (degrees)]" << std::endl;
    std::cout << "    Reference (IAU):          Mean = " << ref_mean_span << " deg (std = " << ref_std_span << "), Range = [" << ref_min_span << " .. " << ref_max_span << "] deg" << std::endl;
    std::cout << "      Distribution:           <15 deg: " << ref_span_under_15 << " | 15-20 deg: " << ref_span_15_to_20 << " | >20 deg: " << ref_span_over_20 << std::endl;
    std::cout << "    Generated Exocons:        Mean = " << gen_mean_span << " deg (std = " << gen_std_span << "), Range = [" << gen_min_span << " .. " << gen_max_span << "] deg" << std::endl;
    std::cout << "      Distribution:           <15 deg: " << gen_span_under_15 << " | 15-20 deg: " << gen_span_15_to_20 << " | >20 deg: " << gen_span_over_20 << std::endl;
    std::cout << "    Span Variance / Delta:    Mean diff = " << (gen_mean_span - ref_mean_span) << " deg" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  [Line Length (degrees)]" << std::endl;
    std::cout << "    Reference (IAU):          Mean = " << ref_mean_len << " deg, Range = [" << ref_min_len << " .. " << ref_max_len << "] deg" << std::endl;
    std::cout << "    Generated Exocons:        Mean = " << gen_mean_len << " deg, Range = [" << gen_min_len << " .. " << gen_max_len << "] deg" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  [Per-Constellation Best Match Similarity]" << std::endl;
    std::cout << "    Average Best Match Jaccard across all Reference constellations: " << mean_cons_jaccard << "%" << std::endl;
    std::cout << "    Top 15 Most Similar Constellations:" << std::endl;
    int top_n = std::min(15, (int)match_records.size());
    for (int i = 0; i < top_n; i++)
    {
        const auto& mr = match_records[i];
        std::cout << "      " << std::setw(3) << mr.ref_abbrev << " (" << std::setw(14) << std::left << mr.ref_name << std::right << ")"
                  << " -> " << std::setw(22) << std::left << mr.gen_abbrev << std::right
                  << "  Shared Stars: " << mr.shared_stars << "/" << mr.ref_star_count
                  << "  Shared Lines: " << mr.shared_lines
                  << "  Jaccard: " << (mr.jaccard * 100.0) << "%" << std::endl;
    }
    std::cout << "================================================================================\n" << std::endl;

    SUCCEED();
}


