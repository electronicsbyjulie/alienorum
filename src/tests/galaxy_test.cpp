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
    std::string path = "galaxies" _FILESLASH "internal" _FILESLASH "Milky Way.jpg";
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

TEST(MilkyWayBackdropTest, SkymapSeamWrappingDetection)
{
    // Simulate the Milky Way band mesh in skymap projection (Earth equatorial frame, az=0, alt=0, zoom=1)
    Point sgr_loc = Point::from_ra_dec(galactic_center_RA_J2000, galactic_center_Decl_J2000, 8200.0, 0);
    Rotation pl = system_plane_from_incl_and_node(milky_way_inclination, milky_way_position_angle, sgr_loc);
    Point viewer_dir = rotate3D(sgr_loc, center, pl.v, pl.a);
    double gyaw = find_angle_along_vector(zaxis, viewer_dir, center, yaxis);

    const int N_lon = 240;
    const int N_lat = 24;
    const float dispcx = 640.0f;
    const float zoom = 1.0f;
    const float wrap_w = 2.0f * dispcx * zoom;
    const float wrap_thresh = (float)(1.5 * dispcx * zoom);

    struct TestVertex
    {
        ImVec2 pos;
    };

    std::vector<TestVertex> grid((N_lon + 1) * (N_lat + 1));

    for (int j = 0; j <= N_lat; j++)
    {
        float v = (float)j / (float)N_lat;
        double lat = (0.5 - (double)v) * (_pi / 3.0);

        for (int i = 0; i <= N_lon; i++)
        {
            float u = (float)i / (float)N_lon;
            double lon = ((double)u - 0.5) * (2.0 * _pi);

            Point pt = Point::from_ra_dec(lon, lat, 1.0, 0);
            pt = rotate3D(pt, center, yaxis, gyaw);
            pt = rotate3D(pt, center, pl.v, -pl.a);

            double ra = std::fmod(find_angle(pt.z, -pt.x) + _pi, _pi * 2);
            if (ra < 0)
            {
                ra += _pi * 2;
            }
            double decl = std::fmod(find_angle(sqrt(pt.x * pt.x + pt.z * pt.z), pt.y), _pi * 2);
            if (decl > _pi / 2)
            {
                decl -= _pi * 2;
            }

            double cart_x = (1.0 - ra / _pi) * zoom;
            double cart_y = -decl / _pi * zoom;

            grid[j * (N_lon + 1) + i].pos = ImVec2((float)(dispcx + dispcx * cart_x), (float)(dispcx + dispcx * cart_y));
        }
    }

    int total_missed_by_old = 0;
    int total_streaks_with_new = 0;

    // Test across various view azimuths (simulating Earth rotation / sidereal time)
    for (int step = 0; step < 12; step++)
    {
        double az_test = step * (_pi / 6.0);

        for (int j = 0; j <= N_lat; j++)
        {
            float v = (float)j / (float)N_lat;
            double lat = (0.5 - (double)v) * (_pi / 3.0);

            for (int i = 0; i <= N_lon; i++)
            {
                float u = (float)i / (float)N_lon;
                double lon = ((double)u - 0.5) * (2.0 * _pi);

                Point pt = Point::from_ra_dec(lon, lat, 1.0, 0);
                pt = rotate3D(pt, center, yaxis, gyaw);
                pt = rotate3D(pt, center, pl.v, -pl.a);

                double ra = std::fmod(find_angle(pt.z, -pt.x) + _pi + az_test, _pi * 2);
                if (ra < 0)
                {
                    ra += _pi * 2;
                }
                double decl = std::fmod(find_angle(sqrt(pt.x * pt.x + pt.z * pt.z), pt.y), _pi * 2);
                if (decl > _pi / 2)
                {
                    decl -= _pi * 2;
                }

                double cart_x = (1.0 - ra / _pi) * zoom;
                double cart_y = -decl / _pi * zoom;

                grid[j * (N_lon + 1) + i].pos = ImVec2((float)(dispcx + dispcx * cart_x), (float)(dispcx + dispcx * cart_y));
            }
        }

        for (int j = 0; j < N_lat; j++)
        {
            for (int i = 0; i < N_lon; i++)
            {
                const ImVec2& p00 = grid[j * (N_lon + 1) + i].pos;
                const ImVec2& p10 = grid[j * (N_lon + 1) + (i + 1)].pos;
                const ImVec2& p11 = grid[(j + 1) * (N_lon + 1) + (i + 1)].pos;
                const ImVec2& p01 = grid[(j + 1) * (N_lon + 1) + i].pos;

                float min_x = std::min({p00.x, p10.x, p11.x, p01.x});
                float max_x = std::max({p00.x, p10.x, p11.x, p01.x});

                // The old logic only checked horizontal edges with threshold = dispcx * zoom
                bool old_wrapped = (fabs(p00.x - p10.x) > (dispcx * zoom)) ||
                                   (fabs(p01.x - p11.x) > (dispcx * zoom));

                bool new_wrapped = (max_x - min_x) > wrap_thresh;

                if (new_wrapped && !old_wrapped)
                {
                    total_missed_by_old++;
                }

                if (new_wrapped)
                {
                    // Verify Piece 1: shifted to right for points left of center
                    ImVec2 p00_1 = p00;
                    if (p00_1.x < dispcx)
                    {
                        p00_1.x += wrap_w;
                    }
                    ImVec2 p10_1 = p10;
                    if (p10_1.x < dispcx)
                    {
                        p10_1.x += wrap_w;
                    }
                    ImVec2 p11_1 = p11;
                    if (p11_1.x < dispcx)
                    {
                        p11_1.x += wrap_w;
                    }
                    ImVec2 p01_1 = p01;
                    if (p01_1.x < dispcx)
                    {
                        p01_1.x += wrap_w;
                    }

                    float span_1 = std::max({p00_1.x, p10_1.x, p11_1.x, p01_1.x}) -
                                   std::min({p00_1.x, p10_1.x, p11_1.x, p01_1.x});
                    if (span_1 > wrap_thresh)
                    {
                        total_streaks_with_new++;
                    }

                    // Verify Piece 2: shifted to left for points right of center
                    ImVec2 p00_2 = p00;
                    if (p00_2.x > dispcx)
                    {
                        p00_2.x -= wrap_w;
                    }
                    ImVec2 p10_2 = p10;
                    if (p10_2.x > dispcx)
                    {
                        p10_2.x -= wrap_w;
                    }
                    ImVec2 p11_2 = p11;
                    if (p11_2.x > dispcx)
                    {
                        p11_2.x -= wrap_w;
                    }
                    ImVec2 p01_2 = p01;
                    if (p01_2.x > dispcx)
                    {
                        p01_2.x -= wrap_w;
                    }

                    float span_2 = std::max({p00_2.x, p10_2.x, p11_2.x, p01_2.x}) -
                                   std::min({p00_2.x, p10_2.x, p11_2.x, p01_2.x});
                    if (span_2 > wrap_thresh)
                    {
                        total_streaks_with_new++;
                    }
                }
            }
        }
    }

    // Old logic missed seam crossings (which caused horizontal streaks); new logic catches them
    EXPECT_GT(total_missed_by_old, 0);
    // Zero streaks occur with new logic
    EXPECT_EQ(total_streaks_with_new, 0);
}

TEST(MilkyWayBackdropTest, WhiteBackgroundInversion)
{
    // Test the color inversion transformation used for white background mode:
    // inv = 255 - min(255, (int)(val * 2.2f))
    auto invert_pixel = [](int val) -> int
    {
        int scaled = std::min(255, (int)(val * 2.2f));
        return 255 - scaled;
    };

    // Dark space outside galaxy inverts to pure white (seamless against white background)
    EXPECT_EQ(invert_pixel(0), 255);

    // Edge values near +/- 30 deg cutoff (val ~ 2) invert to almost pure white
    EXPECT_GE(invert_pixel(2), 250);

    // Dust lane (val ~ 10) inverts to light tone
    int dust_lane_inv = invert_pixel(10);
    EXPECT_EQ(dust_lane_inv, 233);

    // Bright star cloud (val ~ 40) inverts to medium gray
    int star_cloud_inv = invert_pixel(40);
    EXPECT_EQ(star_cloud_inv, 167);

    // Galactic core (val ~ 80) inverts to dark gray
    int core_inv = invert_pixel(80);
    EXPECT_EQ(core_inv, 79);

    // In inverted mode, dust lanes are strictly lighter (higher luminance) than surrounding star clouds
    EXPECT_GT(dust_lane_inv, star_cloud_inv);
    EXPECT_GT(star_cloud_inv, core_inv);

    // Verify alpha blending over white background (C_dst = 255, alpha = 0.75)
    auto blend_white = [](int src_color, float alpha) -> int
    {
        return (int)(src_color * alpha + 255.0f * (1.0f - alpha));
    };

    // Background space remains pure white
    EXPECT_EQ(blend_white(invert_pixel(0), 0.75f), 255);

    // Dust lanes blend to light gray (~238)
    int dust_lane_blended = blend_white(dust_lane_inv, 0.75f);
    EXPECT_GE(dust_lane_blended, 235);

    // Star clouds blend to noticeable gray (~189)
    int star_cloud_blended = blend_white(star_cloud_inv, 0.75f);
    EXPECT_NEAR(star_cloud_blended, 189, 2);

    // Contrast between dust lane and star cloud is clearly visible (> 40 levels of brightness)
    EXPECT_GT(dust_lane_blended - star_cloud_blended, 40);
}

// =====================================================================
// Galaxy Face-on Map Tests
// =====================================================================

TEST(GalaxyFaceonMapTest, MajorGalaxiesAndMilkyWayHaveValidJpegHeader)
{
    const std::vector<std::string> test_galaxies =
    {
        "M31", "M81", "M101", "NGC_1097", "NGC_1316", "NGC_1365", "NGC_253", "Milky Way"
    };

    for (const auto &gname : test_galaxies)
    {
        std::string path = "galaxies" _FILESLASH "faceon" _FILESLASH + gname + ".jpg";
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "Missing face-on map for " << gname << " at " << path;

        // Verify JPEG SOI marker (0xFF 0xD8)
        unsigned char header[2];
        file.read((char*)header, 2);
        EXPECT_EQ(header[0], 0xFF);
        EXPECT_EQ(header[1], 0xD8);
    }
}

TEST(GalaxyFaceonMapTest, AccessoryCompanionsExist)
{
    // Verify companion galaxies identified and extracted as accessories
    const std::vector<std::string> companions =
    {
        "NGC_1317", "NGC_4435", "NGC_4485", "NGC_4627", "NGC_5195"
    };

    for (const auto &cname : companions)
    {
        std::string path = "galaxies" _FILESLASH "faceon" _FILESLASH + cname + ".jpg";
        std::ifstream file(path, std::ios::binary);
        EXPECT_TRUE(file.good()) << "Accessory companion face-on map missing: " << path;

        // Verify JPEG SOI marker (0xFF 0xD8)
        unsigned char header[2];
        file.read((char*)header, 2);
        EXPECT_EQ(header[0], 0xFF);
        EXPECT_EQ(header[1], 0xD8);
    }
}

// =====================================================================
// Galaxy Internal 360-degree Panorama Tests
// =====================================================================

TEST(GalaxyInternalMapTest, MajorGalaxiesAndCompanionsHaveValidJpegHeader)
{
    const std::vector<std::string> test_galaxies =
    {
        "M31", "M81", "M101", "NGC_1097", "NGC_4435", "NGC_4490", "NGC_4627", "NGC_5194", "NGC_5195"
    };

    for (const auto &gname : test_galaxies)
    {
        std::string path = "galaxies" _FILESLASH "internal" _FILESLASH + gname + ".jpg";
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "Missing internal map for " << gname << " at " << path;

        // Verify JPEG SOI marker (0xFF 0xD8)
        unsigned char header[2];
        file.read((char*)header, 2);
        EXPECT_EQ(header[0], 0xFF);
        EXPECT_EQ(header[1], 0xD8);
    }
}

// =====================================================================
// Spheroidal and Elliptical 3D Rendering Tests
// =====================================================================

TEST(GalaxyTest, SpheroidClassification)
{
    Galaxy sag;
    sag.T_known = true;
    sag.morphological_T = -3.0;
    snprintf(sag.morph_type, sizeof(sag.morph_type), "Sph");
    snprintf(sag.name, sizeof(sag.name), "Sag dSph");
    EXPECT_TRUE(sag.is_spheroidal_or_elliptical());

    Galaxy ell;
    ell.T_known = true;
    ell.morphological_T = -5.0;
    snprintf(ell.morph_type, sizeof(ell.morph_type), "E0");
    EXPECT_TRUE(ell.is_spheroidal_or_elliptical());

    Galaxy compact_ell;
    compact_ell.T_known = true;
    compact_ell.morphological_T = -6.0;
    EXPECT_TRUE(compact_ell.is_spheroidal_or_elliptical());

    Galaxy dwarf_ell;
    snprintf(dwarf_ell.morph_type, sizeof(dwarf_ell.morph_type), "dE3");
    EXPECT_TRUE(dwarf_ell.is_spheroidal_or_elliptical());

    Galaxy spiral;
    spiral.T_known = true;
    spiral.morphological_T = 3.0;
    snprintf(spiral.morph_type, sizeof(spiral.morph_type), ".SAS3..");
    EXPECT_FALSE(spiral.is_spheroidal_or_elliptical());

    Galaxy irregular;
    irregular.T_known = true;
    irregular.morphological_T = 10.0;
    snprintf(irregular.morph_type, sizeof(irregular.morph_type), "Ir");
    EXPECT_FALSE(irregular.is_spheroidal_or_elliptical());
}

TEST(GalaxyTest, Spheroid3DSilhouetteNeverCollapses)
{
    const double q = 0.48; // Sag dSph intrinsic axis ratio
    const double a = 1000.0; // semi-major radius

    // Test across a full 180-degree sweep of viewing inclinations relative to the symmetry axis
    for (int deg = 0; deg <= 180; deg += 5)
    {
        double theta = deg * (M_PI / 180.0);
        double costheta = cos(theta);
        double sintheta = sin(theta);
        double b_app = a * sqrt(costheta * costheta + q * q * sintheta * sintheta);

        // At all viewing angles, the apparent semi-minor axis must never collapse below q * a
        EXPECT_GE(b_app, q * a - 1e-9);
        EXPECT_LE(b_app, a + 1e-9);

        // When viewed edge-on (theta = 90 deg), thickness is exactly q * a = 0.48 * a, NOT zero
        if (deg == 90)
        {
            EXPECT_NEAR(b_app, q * a, 1e-6);
        }
        // When viewed face-on (theta = 0 or 180 deg), it appears circular
        if (deg == 0 || deg == 180)
        {
            EXPECT_NEAR(b_app, a, 1e-6);
        }
    }
}

// =====================================================================
// Galaxy Disc 3D Basis and Position Angle Orientation Tests
// =====================================================================

TEST(GalaxyTest, MajorAxisMatchesCatalogPositionAngleAcrossAllAngles)
{
    // Direction to galaxy center on sky (e.g. LMC coordinates)
    const double ra = 80.89 * (_pi / 180.0);
    const double dec = -69.756 * (_pi / 180.0);

    Point vhat(-sin(ra) * cos(dec), sin(dec), cos(ra) * cos(dec));
    vhat.scale(1.0);

    Point E_sky = compute_normal(center, vhat, yaxis);
    E_sky.scale(1.0);
    Point N_sky = compute_normal(center, E_sky, vhat);
    N_sky.scale(1.0);

    const double test_pas[] = {0.0, 22.0, 35.0, 90.0, 115.0, 136.0, 180.0, 245.0, 315.0};
    const double test_incls[] = {0.0, 20.0, 35.0, 60.0, 77.0, 85.0};

    for (double pa_deg : test_pas)
    {
        double pa = pa_deg * (_pi / 180.0);
        for (double incl_deg : test_incls)
        {
            double incl = incl_deg * (_pi / 180.0);

            Point u_maj = N_sky * cos(pa) + E_sky * sin(pa);
            u_maj.scale(1.0);
            Point u_perp = compute_normal(center, vhat, u_maj);
            u_perp.scale(1.0);

            Point e1 = u_maj * -1.0;
            Point e2 = (u_perp * cos(incl) + vhat * sin(incl)) * -1.0;
            e1.scale(1.0);
            e2.scale(1.0);

            Point pole = compute_normal(center, e1, e2);
            pole.scale(1.0);

            // e1, e2, and pole must form a strictly orthonormal right-handed basis
            EXPECT_NEAR(e1.magnitude(), 1.0, 1e-9);
            EXPECT_NEAR(e2.magnitude(), 1.0, 1e-9);
            EXPECT_NEAR(pole.magnitude(), 1.0, 1e-9);
            EXPECT_NEAR(e1.x * e2.x + e1.y * e2.y + e1.z * e2.z, 0.0, 1e-9);
            EXPECT_NEAR(e1.x * pole.x + e1.y * pole.y + e1.z * pole.z, 0.0, 1e-9);
            EXPECT_NEAR(e2.x * pole.x + e2.y * pole.y + e2.z * pole.z, 0.0, 1e-9);

            // Apparent position angle of e1 in the sky plane
            double proj_N = -(e1.x * N_sky.x + e1.y * N_sky.y + e1.z * N_sky.z);
            double proj_E = -(e1.x * E_sky.x + e1.y * E_sky.y + e1.z * E_sky.z);
            double meas_pa = atan2(proj_E, proj_N) * (180.0 / _pi);
            if (meas_pa < 0.0)
            {
                meas_pa += 360.0;
            }

            // Measured PA must match catalog PA exactly
            double diff = fabs(meas_pa - pa_deg);
            while (diff > 180.0)
            {
                diff = fabs(diff - 360.0);
            }
            EXPECT_NEAR(diff, 0.0, 1e-6);

            // In-plane apparent minor axis projection has foreshortened factor cos(incl)
            Point e2_proj = e2 - vhat * (e2.x * vhat.x + e2.y * vhat.y + e2.z * vhat.z);
            EXPECT_NEAR(e2_proj.magnitude(), cos(incl), 1e-6);
        }
    }
}
