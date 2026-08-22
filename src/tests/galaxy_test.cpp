#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include "../classes/galaxy.h"

using namespace alienorum;
using json = nlohmann::json;

// =====================================================================
// Galaxy Initialization & Default State Tests
// =====================================================================

TEST(GalaxyTest, ConstructorSetsCorrectTypes)
{
    Galaxy g;
    
    // Verify base class classification overrides
    EXPECT_EQ(g.typeclass(), class_galaxy);
    EXPECT_EQ(g.type, galaxy);
    
    // Verify default parameters
    EXPECT_FALSE(g.T_known);
    EXPECT_DOUBLE_EQ(g.morphological_T, 0.0);
    EXPECT_DOUBLE_EQ(g.angular_diameter, 0.0);
    EXPECT_DOUBLE_EQ(g.axis_ratio, 1.0);
    EXPECT_FALSE(g.position_angle_known);
    EXPECT_DOUBLE_EQ(g.position_angle, 0.0);
    EXPECT_DOUBLE_EQ(g.inclination, 0.0);
    EXPECT_DOUBLE_EQ(g.apparent_magnitude, 0.0);
    EXPECT_DOUBLE_EQ(g.radial_velocity, 0.0);
    EXPECT_EQ(g.PGC, 0);
    EXPECT_STREQ(g.morph_type, "");
}

// =====================================================================
// Galaxy JSON Serialization Tests
// =====================================================================

TEST(GalaxyTest, JsonSerialization_ConditionalFields)
{
    Galaxy original;
    // Leave default values untouched to verify they are omitted from JSON
    
    json j = original.to_json();
    
    // Defaults should not write keys to keep catalogs clean
    EXPECT_FALSE(j.contains("morphological_T"));
    EXPECT_FALSE(j.contains("angular_diameter"));
    EXPECT_FALSE(j.contains("axis_ratio")); // axis_ratio == 1 is omitted
    EXPECT_FALSE(j.contains("position_angle"));
    EXPECT_FALSE(j.contains("PGC"));
    EXPECT_FALSE(j.contains("morph_type"));
}

TEST(GalaxyTest, JsonSerialization_RoundTripWithValues)
{
    Galaxy original;
    original.morphological_T = 3.0; // Sb galaxy
    original.T_known = true;
    original.angular_diameter = 0.001;
    original.axis_ratio = 0.5;
    original.position_angle = 1.57;
    original.position_angle_known = true;
    original.inclination = 1.04;
    original.apparent_magnitude = 11.2;
    original.radial_velocity = 550000.0;
    original.PGC = 2557;
    snprintf(original.morph_type, sizeof(original.morph_type), ".SAS3..");

    json j = original.to_json();
    
    Galaxy restored;
    bool success = restored.from_json(j);
    
    EXPECT_TRUE(success);
    EXPECT_TRUE(restored.T_known);
    EXPECT_DOUBLE_EQ(restored.morphological_T, 3.0);
    EXPECT_DOUBLE_EQ(restored.angular_diameter, 0.001);
    EXPECT_DOUBLE_EQ(restored.axis_ratio, 0.5);
    EXPECT_TRUE(restored.position_angle_known);
    EXPECT_DOUBLE_EQ(restored.position_angle, 1.57);
    EXPECT_DOUBLE_EQ(restored.inclination, 1.04);
    EXPECT_DOUBLE_EQ(restored.apparent_magnitude, 11.2);
    EXPECT_DOUBLE_EQ(restored.radial_velocity, 550000.0);
    EXPECT_EQ(restored.PGC, 2557);
    EXPECT_STREQ(restored.morph_type, ".SAS3..");
}

// =====================================================================
// GalaxyBand Parser Tests
// =====================================================================

TEST(GalaxyBandTest, LoadDatFile_ParsesCorrectly)
{
    // Create a temporary dummy .dat file for testing the parser
    std::string filename = "test_galaxy_band.dat";
    {
        std::ofstream fs(filename);
        fs << "# This is a comment line\n";
        fs << "N\n";
        fs << "12.5,45.2\n";
        fs << "13.0,46.1\n";
        fs << "S\n";
        fs << "-10.2,-30.5\n";
    }

    GalaxyBand band;
    int items_read = band.load_dat_file(filename);
    
    // Clean up temporary file immediately
    std::remove(filename.c_str());

    // Should read 3 valid data points (2 North, 1 South)
    EXPECT_EQ(items_read, 3);
    
    ASSERT_EQ(band.road1_gra.size(), 2);
    EXPECT_DOUBLE_EQ(band.road1_gra[0], 12.5);
    EXPECT_DOUBLE_EQ(band.road1_gdecl[0], 45.2);
    EXPECT_DOUBLE_EQ(band.road1_dist[0], 0.0); // Initialized to 0 by parser

    ASSERT_EQ(band.road2_gra.size(), 1);
    EXPECT_DOUBLE_EQ(band.road2_gra[0], -10.2);
    EXPECT_DOUBLE_EQ(band.road2_gdecl[0], -30.5);
    EXPECT_DOUBLE_EQ(band.road2_dist[0], 0.0);
}

TEST(GalaxyBandTest, LoadDatFile_HandlesMissingFile)
{
    GalaxyBand band;
    int items_read = band.load_dat_file("nonexistent_file.dat");
    
    EXPECT_EQ(items_read, 0);
    EXPECT_TRUE(band.road1_gra.empty());
    EXPECT_TRUE(band.road2_gra.empty());
}
// The real export this feeds on (boundary_roads.dat) is whitespace-separated, not comma-separated
// -- the parser insisted on a comma on every line, so every coordinate in the real file silently
// failed to match and the band stayed permanently empty regardless of anything else being right.
TEST(GalaxyBandTest, LoadDatFile_AcceptsWhitespaceSeparatedValues)
{
    std::string filename = "test_galaxy_band_whitespace.dat";
    {
        std::ofstream fs(filename);
        fs << "N\n";
        fs << "-3.14055 0.076969\n";
        fs << "-3.1374 0.0764454\n";
        fs << "S\n";
        fs << "3.08295 -0.015708\n";
    }

    GalaxyBand band;
    int items_read = band.load_dat_file(filename);
    std::remove(filename.c_str());

    EXPECT_EQ(items_read, 3);
    ASSERT_EQ(band.road1_gra.size(), 2);
    EXPECT_DOUBLE_EQ(band.road1_gra[0], -3.14055);
    EXPECT_DOUBLE_EQ(band.road1_gdecl[0], 0.076969);
    ASSERT_EQ(band.road2_gra.size(), 1);
    EXPECT_DOUBLE_EQ(band.road2_gra[0], 3.08295);
    EXPECT_DOUBLE_EQ(band.road2_gdecl[0], -0.015708);
}
