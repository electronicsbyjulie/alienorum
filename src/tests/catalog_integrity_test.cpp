#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "universe_fixture.h"

using namespace alienorum;

class CondensedCatalogReader : public CatalogReader
{
    public:
    using CatalogReader::write_condensed_star_cat_line;
};

class CondensedCatalogTest : public UniverseFixture
{
    protected:
    std::filesystem::path original_directory, directory;
    std::vector<Constellation> saved_constellations;
    Star **saved_hd = nullptr, **saved_hip = nullptr;
    std::map<std::string, Star*> saved_dm;
    bool saved_abort = false;

    void SetUp() override
    {
        UniverseFixture::SetUp();
        original_directory = std::filesystem::current_path();
        directory = std::filesystem::temp_directory_path()
            / ("alienorum_condensed_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory / "catalogs");
        std::filesystem::current_path(directory);
        saved_constellations = std::move(constellations);
        constellations.clear();
        Constellation oph;
        oph.name = "Ophiuchus";
        oph.abbrev = "Oph";
        oph.genitive = "Ophiuchi";
        constellations.push_back(oph);
        Constellation ser;
        ser.name = "Serpens";
        ser.abbrev = "Ser";
        ser.genitive = "Serpentis";
        constellations.push_back(ser);
        saved_hd = hdcache;
        saved_hip = hipcache;
        saved_dm = std::move(dmcache);
        dmcache.clear();
        hdcache = hipcache = nullptr;
        saved_abort = abort_load;
        abort_load = false;
    }

    void TearDown() override
    {
        delete_the_universe();
        delete[] hdcache;
        delete[] hipcache;
        hdcache = saved_hd;
        hipcache = saved_hip;
        dmcache = std::move(saved_dm);
        constellations = std::move(saved_constellations);
        abort_load = saved_abort;
        std::filesystem::current_path(original_directory);
        std::filesystem::remove_all(directory);
        UniverseFixture::TearDown();
    }

    static void put(std::string &row, int start, int end, const std::string &value)
    {
        ASSERT_LE(value.size(), static_cast<size_t>(end - start + 1));
        row.replace(start - 1, end - start + 1, value + std::string(end - start + 1 - value.size(), ' '));
    }

    static std::string primary_row()
    {
        // Literal columns are an independent oracle, rather than writer output.
        std::string row(455, ' ');
        put(row, 1, 14, "2c Oph 1");
        put(row, 16, 54, "Rasalhague");
        put(row, 56, 65, "17:35:07.1");
        put(row, 67, 75, "+12:40:17");
        put(row, 77, 82, "+02.08");
        put(row, 84, 98, "A5III");
        put(row, 100, 105, "+00.15");
        put(row, 107, 112, "+00.12");
        put(row, 114, 119, "159561");
        put(row, 121, 126, "86032");
        put(row, 128, 132, "6556");
        put(row, 134, 140, "4321");
        put(row, 142, 145, "55");
        put(row, 146, 152, "Alp Oph");
        put(row, 154, 168, "GJ 681");
        put(row, 178, 188, "1.6912e-14");
        put(row, 190, 200, "-3.4200e-14");
        put(row, 202, 212, "1.3000e+04");
        put(row, 214, 223, "3.3859e-07");
        put(row, 224, 235, "4.6701e+01");
        put(row, 238, 243, "+01.30");
        put(row, 245, 254, "1.8600e+00");
        put(row, 257, 266, "2.5850e+00");
        put(row, 268, 274, "8200");
        put(row, 276, 285, "12.345");
        put(row, 439, 440, "BD");
        put(row, 442, 445, "+12");
        put(row, 447, 452, "3252");
        put(row, 454, 454, "Y");
        return row;
    }

    static std::string solar_row()
    {
        std::string row = primary_row();
        put(row, 1, 14, "-26");
        put(row, 16, 54, "Sun");
        put(row, 77, 82, "-26.72");
        for (auto range : {std::pair<int, int>(114, 176), {439, 454}})
        {
            put(row, range.first, range.second, "");
        }
        return row;
    }

    void write_rows(const std::vector<std::string> &rows)
    {
        std::ofstream file("catalogs/soles_alienorum.dat", std::ios::binary);
        file << solar_row() << '\n';
        for (const std::string &row : rows)
        {
            file << row << '\n';
        }
    }

    static std::string written_row(Star *star)
    {
        FILE *file = std::tmpfile();
        if (!file)
        {
            return "";
        }
        CondensedCatalogReader reader;
        reader.write_condensed_star_cat_line(file, star);
        std::rewind(file);
        char line[1024];
        const bool read = std::fgets(line, sizeof(line), file) != nullptr;
        std::fclose(file);
        return read ? line : "";
    }
};

TEST_F(CondensedCatalogTest, IndependentFixturePreservesEveryIdentityAndPhotometricField)
{
    write_rows({primary_row()});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_EQ(star->alienorumid, "2c Oph 1");
    EXPECT_STREQ(star->name, "Rasalhague");
    EXPECT_EQ(star->origname, "Rasalhague");
    EXPECT_EQ(star->HD, 159561);
    EXPECT_EQ(star->HIP, 86032);
    EXPECT_EQ(star->HR, 6556);
    EXPECT_EQ(star->SB9, 4321);
    EXPECT_EQ(star->FlamsteedNo, 55);
    EXPECT_EQ(star->BayerGrkno, 0);
    EXPECT_STREQ(star->Bayer, "Alp Oph");
    EXPECT_STREQ(star->Gliese, "GJ 681");
    EXPECT_TRUE(star->has_custom_name);
    EXPECT_EQ(star->namelen, strlen(star->name));
    EXPECT_EQ(hdcache[159561], star);
    EXPECT_EQ(hipcache[86032], star);
    EXPECT_DOUBLE_EQ(star->apparent_magnitude, 2.08);
    EXPECT_DOUBLE_EQ(star->absolute_magnitude, 1.30);
    EXPECT_STREQ(star->spectral_type, "A5III");
    EXPECT_DOUBLE_EQ(star->BV_color, 0.15);
    EXPECT_DOUBLE_EQ(star->UB_color, 0.12);
    EXPECT_DOUBLE_EQ(star->proper_motion_RA, 1.6912e-14);
    EXPECT_DOUBLE_EQ(star->proper_motion_decl, -3.4200e-14);
    EXPECT_DOUBLE_EQ(star->radial_velocity, 13000);
    EXPECT_DOUBLE_EQ(star->parallax, 3.3859e-7);
    EXPECT_NEAR(star->distance / light_year, 46.701, 1e-10);
    EXPECT_NEAR(star->mass / solar_mass, 1.86, 1e-12);
    EXPECT_NEAR(star->volumetric_mean_radius / solar_radius, 2.585, 1e-12);
    EXPECT_DOUBLE_EQ(star->temperature, 8200);
    EXPECT_NEAR(star->sidereal_rotational_period / oneday, 12.345, 1e-10);
    EXPECT_STREQ(star->Bonn_survey, "BD");
    EXPECT_EQ(star->Bonn_survey_declination, 12);
    EXPECT_EQ(star->Bonn_survey_sequential, 3252);
}

TEST_F(CondensedCatalogTest, BlankBayerUsesFlamsteedOrGouldRatherThanAlpha)
{
    std::string flamsteed = primary_row();
    put(flamsteed, 16, 54, "55 Ophiuchi");
    put(flamsteed, 146, 152, "");
    put(flamsteed, 454, 454, "");
    write_rows({flamsteed});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_EQ(star->BayerGrkno, -1);
    star->rename_from_Bayer_Flamsteed();
    EXPECT_STREQ(star->name, "55 Ophiuchi");
    star->FlamsteedNo = 0;
    star->GouldNo = 187;
    strcpy(star->Gouldcons, "Oph");
    star->Gliese[0] = 0;
    star->rename_from_Bayer_Flamsteed();
    EXPECT_STREQ(star->name, "187 G. Oph");
}

TEST_F(CondensedCatalogTest, NumberedBayerAndCustomNamesSurviveCacheRoundTrip)
{
    std::string numbered = primary_row();
    put(numbered, 146, 152, "Alp2Oph");
    write_rows({numbered});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_EQ(star->BayerGrkno, 102);
    star->rename_from_Bayer_Flamsteed();
    EXPECT_STREQ(star->name, "Rasalhague");
    const std::string encoded = written_row(star);
    ASSERT_EQ(encoded.size(), 456u);
    EXPECT_EQ(encoded.substr(127, 5).find("6556"), 0u);
    EXPECT_EQ(std::stoi(encoded.substr(133, 7)), 4321);
    EXPECT_EQ(encoded.substr(145, 7), "Alp2Oph");
    EXPECT_EQ(encoded[453], 'Y');
    delete_the_universe();
    write_rows({encoded.substr(0, 455)});
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    star = (Star*)cels[1];
    EXPECT_EQ(star->HR, 6556);
    EXPECT_EQ(star->SB9, 4321);
    EXPECT_EQ(star->BayerGrkno, 102);
    EXPECT_STREQ(star->name, "Rasalhague");
    EXPECT_TRUE(star->has_custom_name);
}

TEST_F(CondensedCatalogTest, MultiStarComponentsRetainDistinctIdsAndNestedParents)
{
    std::string secondary = primary_row();
    put(secondary, 1, 14, "2c Oph 1 B");
    put(secondary, 16, 54, "Secondary");
    put(secondary, 114, 119, "159562");
    put(secondary, 121, 126, "86033");
    put(secondary, 128, 132, "6557");
    put(secondary, 146, 152, "Bet Oph");
    put(secondary, 287, 300, "2c Oph 1");
    put(secondary, 302, 312, "365.25");
    put(secondary, 315, 326, "1.5");
    put(secondary, 328, 334, "0.25");
    put(secondary, 341, 348, "45.0");
    put(secondary, 350, 357, "180.0");
    put(secondary, 359, 372, "2.451545e+06");
    put(secondary, 374, 381, "60.0");
    put(secondary, 383, 390, "120.0");
    std::string tertiary = secondary;
    put(tertiary, 1, 14, "2c Oph 1 C");
    put(tertiary, 16, 54, "Tertiary");
    put(tertiary, 114, 119, "159563");
    put(tertiary, 121, 126, "86034");
    put(tertiary, 128, 132, "6558");
    put(tertiary, 287, 300, "2c Oph 1 B");
    write_rows({primary_row(), secondary, tertiary});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 4);
    Star *a = (Star*)cels[1], *b = (Star*)cels[2], *c = (Star*)cels[3];
    ASSERT_NE(b->orbit, nullptr);
    ASSERT_NE(c->orbit, nullptr);
    EXPECT_EQ(b->orbit->center, a);
    EXPECT_EQ(c->orbit->center, b);
    EXPECT_EQ(b->HD, 159562);
    EXPECT_EQ(c->HD, 159563);
    EXPECT_EQ(hipcache[86034], c);
    EXPECT_EQ(b->BayerGrkno, 1);
    EXPECT_NEAR(b->orbit->period / oneday, 365.25, 1e-10);
    EXPECT_NEAR(b->orbit->semimajor_axis / AU, 1.5, 1e-12);
    EXPECT_DOUBLE_EQ(b->orbit->eccentricity, 0.25);
    EXPECT_NEAR(b->orbit->arg_periapsis * fiftyseven, 45, 1e-10);
    EXPECT_NEAR(b->orbit->heliocentric_inclination * fiftyseven, 60, 1e-10);
    EXPECT_NEAR(b->orbit->heliocentric_node * fiftyseven, 120, 1e-10);
    EXPECT_EQ(b->origcenname, a->name);
    EXPECT_EQ(c->origcenname, b->name);
}

TEST_F(CondensedCatalogTest, VariabilityAndEclipseFieldsSurviveRoundTrip)
{
    std::string variable = primary_row();
    put(variable, 392, 403, "12.3456");
    put(variable, 405, 410, "+01.50");
    put(variable, 412, 417, "+03.00");
    put(variable, 419, 435, "2451545.125");
    put(variable, 437, 437, "E");
    write_rows({variable});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_TRUE(star->is_eclipsing_binary);
    EXPECT_NEAR(star->variability_period / oneday, 12.3456, 1e-10);
    EXPECT_DOUBLE_EQ(star->minmag, 1.5);
    EXPECT_DOUBLE_EQ(star->maxmag, 3.0);
    EXPECT_DOUBLE_EQ(star->epoch_max_brightness, 2451545.125);
    const std::string encoded = written_row(star);
    EXPECT_EQ(encoded[436], 'E');
    EXPECT_EQ(std::stod(encoded.substr(404, 6)), 1.5);
    EXPECT_EQ(std::stod(encoded.substr(411, 6)), 3.0);
}

TEST_F(CondensedCatalogTest, NegativeZeroAstrometryCannotOverflowIntoAdjacentColumns)
{
    write_rows({primary_row()});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    star->proper_motion_RA = star->proper_motion_decl = star->radial_velocity = -0.0;
    const std::string encoded = written_row(star);
    ASSERT_EQ(encoded.size(), 456u);
    for (int start : {178, 190, 202})
    {
        EXPECT_EQ(encoded.substr(start - 1, 11), "-0.0000e+00");
        EXPECT_EQ(encoded[start + 10], ' ');
    }
    EXPECT_NEAR(std::stod(encoded.substr(213, 10)), star->parallax, 1e-12);
}

TEST_F(CondensedCatalogTest, BayerConstellationAndIndexesFollowDesignationRatherThanSkyBin)
{
    std::string row = primary_row();
    put(row, 16, 54, "Alpha Serpentis");
    put(row, 146, 152, "Alp Ser");
    put(row, 454, 454, "");
    write_rows({row});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_STREQ(star->constellation, "Ser");
    EXPECT_EQ(constellations[1].Bayer_stars[0], star);
    star->rename_from_Bayer_Flamsteed();
    EXPECT_STREQ(star->name, "Alpha Serpentis");
}

TEST_F(CondensedCatalogTest, NegativeOrbitalEpochKeepsTheEntireExponent)
{
    std::string secondary = primary_row();
    put(secondary, 1, 14, "2c Oph 1 B");
    put(secondary, 16, 54, "Secondary");
    put(secondary, 114, 119, "159562");
    put(secondary, 121, 126, "86033");
    put(secondary, 287, 300, "2c Oph 1");
    put(secondary, 302, 312, "365.25");
    put(secondary, 315, 326, "1.5");
    put(secondary, 359, 373, "-4.09111277e+09");
    put(secondary, 374, 381, "60.0");
    write_rows({primary_row(), secondary});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 3);
    Star *star = (Star*)cels[2];
    ASSERT_NE(star->orbit, nullptr);
    EXPECT_DOUBLE_EQ(star->orbit->epoch, -4.09111277e9);
    EXPECT_NEAR(star->orbit->heliocentric_inclination * fiftyseven, 60, 1e-10);
    const std::string encoded = written_row(star);
    EXPECT_EQ(std::stod(encoded.substr(358, 15)), -4.09111277e9);
}

TEST_F(CondensedCatalogTest, MissingAndOversizedCatalogIdsCannotIndexOutsideCaches)
{
    std::string unknown = primary_row();
    put(unknown, 114, 119, "");
    put(unknown, 121, 126, "");
    std::string invalid = primary_row();
    put(invalid, 1, 14, "3c Oph 2");
    put(invalid, 114, 119, "359084");
    put(invalid, 121, 126, "120417");
    write_rows({unknown, invalid});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 3);
    EXPECT_EQ(hdcache[0], nullptr);
    EXPECT_EQ(hipcache[0], nullptr);
    EXPECT_EQ(hdcache[MAX_HD], nullptr);
    EXPECT_EQ(hipcache[MAX_HIP], nullptr);
}

TEST_F(CondensedCatalogTest, EveryGreekLetterAndNumberedVariantHasTheCorrectIndex)
{
    const char *abbreviations[] =
    {
        "Alp", "Bet", "Gam", "Del", "Eps", "Zet", "Eta", "The", "Iot", "Kap", "Lam", "Mu ",
        "Nu ", "Xi ", "Omi", "Pi ", "Rho", "Sig", "Tau", "Ups", "Phi", "Chi", "Psi", "Ome"
    };
    for (int index = 0; index < 24; ++index)
    {
        SCOPED_TRACE(abbreviations[index]);
        std::string plain = std::string(abbreviations[index]) + " Oph";
        std::string numbered = std::string(abbreviations[index]) + "1Oph";
        EXPECT_EQ(grkno_from_abbrev(plain.c_str()), index);
        EXPECT_EQ(grkno_from_abbrev(numbered.c_str()), 100 + index * 10 + 1);
    }
    for (const char *blank : {"", "    ", "\t", "9   ", "-   "})
    {
        EXPECT_EQ(grkno_from_abbrev(blank), -1);
    }
}

TEST_F(CondensedCatalogTest, HipparcosIcrsPositionsAreNotPrecessedAsAnEquinox)
{
    make_star("Sun");
    std::filesystem::create_directories("catalogs/Hipparcos");
    std::ofstream file("catalogs/Hipparcos/hip_main.dat", std::ios::binary);
    for (int index = 0; index < 2; ++index)
    {
        std::string row(450, ' ');
        put(row, 9, 14, std::to_string(100 + index));
        put(row, 18, 28, "01 00 00.00");
        put(row, 30, 40, "+60 00 00.0");
        put(row, 42, 46, "5.00");
        put(row, 52, 63, index == 0 ? "15.00000000" : "0.00000000");
        put(row, 65, 76, index == 0 ? "60.00000000" : "0.00000000");
        put(row, 80, 86, "50.00");
        put(row, 88, 95, "0.00");
        put(row, 97, 104, "0.00");
        put(row, 246, 251, "0.650");
        put(row, 391, 396, std::to_string(10000 + index));
        put(row, 436, 447, "G2V");
        file << row << '\n';
    }
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 2);
    ASSERT_NE(hipcache[100], nullptr);
    EXPECT_NEAR(hipcache[100]->right_ascension * fiftyseven, 15.0, 1e-10);
    EXPECT_NEAR(hipcache[100]->declination * fiftyseven, 60.0, 1e-10);
    ASSERT_NE(hipcache[101], nullptr);
    EXPECT_DOUBLE_EQ(hipcache[101]->right_ascension, 0.0);
    EXPECT_DOUBLE_EQ(hipcache[101]->declination, 0.0);
    EXPECT_DOUBLE_EQ(hipcache[100]->epoch, J2000);
}

TEST_F(CondensedCatalogTest, HipparcosProperMotionAdvancesTheEpochWithCosDeclination)
{
    make_star("Sun");
    std::filesystem::create_directories("catalogs/Hipparcos");
    std::string row(450, ' ');
    put(row, 9, 14, "100");
    put(row, 18, 28, "01 00 00.00");
    put(row, 30, 40, "+60 00 00.0");
    put(row, 42, 46, "5.00");
    put(row, 52, 63, "15.00000000");
    put(row, 65, 76, "60.00000000");
    put(row, 80, 86, "50.00");
    put(row, 88, 95, "1000.00");
    put(row, 97, 104, "0.00");
    put(row, 246, 251, "0.650");
    put(row, 391, 396, "10000");
    put(row, 436, 447, "G2V");
    std::ofstream file("catalogs/Hipparcos/hip_main.dat", std::ios::binary);
    file << row << '\n';
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 1);
    Star *star = hipcache[100];
    ASSERT_NE(star, nullptr);
    // 1 arcsec/year in mu_alpha*cos(delta), 8.75 years, cos(60 deg)=0.5.
    const double shift_arcsec = (star->right_ascension * fiftyseven - 15.0) * 3600;
    EXPECT_NEAR(shift_arcsec, 17.5, 0.01);
    EXPECT_NEAR(star->proper_motion_RA * oneyear * fiftyseven * 3600, 2.0, 1e-8);
}
