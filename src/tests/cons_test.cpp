#include <gtest/gtest.h>
#include <cmath>
#include "../classes/cons.h"
#include "../classes/star.h"

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
