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

// catalogs/Milky_Way.dat -- the file the loader actually reads -- is comma-separated, so this
// isn't guarding today's production format. It's here because a whitespace-separated export
// (boundary_roads.dat, an earlier/duplicate attempt at this same data) tripped the parser's old
// comma-only assumption in exactly this way: every coordinate silently failed to match, and the
// band stayed permanently empty. Keeping the parser tolerant of both costs nothing.
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

// =====================================================================
// Milky Way Texture and Coordinate Alignment Tests
// =====================================================================

TEST(MilkyWayBackdropTest, TextureFileExistsAndHasValidHeader)
{
    std::string path = "galaxies" _FILESLASH "Milky Way.jpg";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good()) << "Could not open " << path;

    // Verify JPEG SOI marker (0xFF 0xD8)
    unsigned char header[2];
    file.read((char*)header, 2);
    EXPECT_EQ(header[0], 0xFF);
    EXPECT_EQ(header[1], 0xD8);
}

TEST(MilkyWayBackdropTest, GalacticCenterAlignment)
{
    // Verify coordinate transformation aligns lon=0, lat=0 with Sagittarius A*
    double sgr_ra = galactic_center_RA_J2000;
    double sgr_dec = galactic_center_Decl_J2000;
    Point sgr_loc = Point::from_ra_dec(sgr_ra, sgr_dec, 8200.0, 0);

    Rotation pl = system_plane_from_incl_and_node(milky_way_inclination, milky_way_position_angle, sgr_loc);

    Point viewer_dir = rotate3D(sgr_loc, center, pl.v, pl.a);
    double gyaw = find_angle_along_vector(zaxis, viewer_dir, center, yaxis);

    Point pt = Point::from_ra_dec(0.0, 0.0, 1.0, 0);
    pt = rotate3D(pt, center, yaxis, gyaw);
    pt = rotate3D(pt, center, pl.v, -pl.a);
    pt.scale(1.0);

    double transformed_dec = asin(pt.y);
    double transformed_ra = atan2(-pt.x, pt.z);
    if (transformed_ra < 0)
    {
        transformed_ra += 2 * _pi;
    }

    // Must match Sgr A* coordinates within 0.1 degree
    EXPECT_NEAR(transformed_ra * fiftyseven, sgr_ra * fiftyseven, 0.1);
    EXPECT_NEAR(transformed_dec * fiftyseven, sgr_dec * fiftyseven, 0.1);
}

TEST(MilkyWayBackdropTest, GalacticLongitudeDirection)
{
    // +lon should point towards Crux / Alpha Centauri (approx RA 218 deg, Dec -61 deg)
    // -lon should point towards Cygnus / Aquila (approx RA 288 deg, Dec +11 deg)
    Point sgr_loc = Point::from_ra_dec(galactic_center_RA_J2000, galactic_center_Decl_J2000, 8200.0, 0);
    Rotation pl = system_plane_from_incl_and_node(milky_way_inclination, milky_way_position_angle, sgr_loc);
    Point viewer_dir = rotate3D(sgr_loc, center, pl.v, pl.a);
    double gyaw = find_angle_along_vector(zaxis, viewer_dir, center, yaxis);

    Point p_pos = Point::from_ra_dec(45.0 * fiftyseventh, 0.0, 1.0, 0);
    p_pos = rotate3D(p_pos, center, yaxis, gyaw);
    p_pos = rotate3D(p_pos, center, pl.v, -pl.a);
    p_pos.scale(1.0);

    double pos_dec = asin(p_pos.y) * fiftyseven;
    double pos_ra = atan2(-p_pos.x, p_pos.z) * fiftyseven;
    if (pos_ra < 0)
    {
        pos_ra += 360.0;
    }

    // Crux/Centaurus is in the southern hemisphere
    EXPECT_LT(pos_dec, -40.0);
    EXPECT_NEAR(pos_ra, 218.0, 5.0);

    Point p_neg = Point::from_ra_dec(-45.0 * fiftyseventh, 0.0, 1.0, 0);
    p_neg = rotate3D(p_neg, center, yaxis, gyaw);
    p_neg = rotate3D(p_neg, center, pl.v, -pl.a);
    p_neg.scale(1.0);

    double neg_dec = asin(p_neg.y) * fiftyseven;
    double neg_ra = atan2(-p_neg.x, p_neg.z) * fiftyseven;
    if (neg_ra < 0)
    {
        neg_ra += 360.0;
    }

    // Cygnus/Aquila is in the northern hemisphere
    EXPECT_GT(neg_dec, 5.0);
    EXPECT_NEAR(neg_ra, 288.0, 5.0);
}

TEST(MilkyWayBackdropTest, LatitudeFadeFunction)
{
    auto compute_fade = [](double lat_radians) -> double
    {
        double lat_deg = fabs(lat_radians) * fiftyseven;
        if (lat_deg <= 22.5)
        {
            return 1.0;
        }
        if (lat_deg >= 30.0)
        {
            return 0.0;
        }
        double t = (30.0 - lat_deg) / 7.5;
        return t * t * (3.0 - 2.0 * t);
    };

    // Center of the band: full brightness
    EXPECT_DOUBLE_EQ(compute_fade(0.0), 1.0);
    EXPECT_DOUBLE_EQ(compute_fade(20.0 * fiftyseventh), 1.0);
    EXPECT_DOUBLE_EQ(compute_fade(22.5 * fiftyseventh), 1.0);

    // Boundary of crop: completely faded
    EXPECT_NEAR(compute_fade(30.0 * fiftyseventh), 0.0, 1e-6);
    EXPECT_NEAR(compute_fade(-30.0 * fiftyseventh), 0.0, 1e-6);

    // Midway: smoothstep(0.5) = 0.5
    double mid = compute_fade(26.25 * fiftyseventh);
    EXPECT_NEAR(mid, 0.5, 1e-6);
}

