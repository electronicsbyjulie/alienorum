#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "../classes/stellar_sources.h"
#include "universe_fixture.h"

using namespace alienorum;

TEST(StellarSourceTableTest, QuotedFieldsPreserveBlankCellsAndEmbeddedSeparators)
{
    const auto fields = stellar_table_fields("\"HD 100\"\t\t\"A\tB\"\t\"quote\"\"mark\"\t");
    ASSERT_EQ(fields.size(), 5u);
    EXPECT_EQ(fields[0], "HD 100");
    EXPECT_EQ(fields[1], "");
    EXPECT_EQ(fields[2], "A\tB");
    EXPECT_EQ(fields[3], "quote\"mark");
    EXPECT_EQ(fields[4], "");
    EXPECT_THROW(stellar_table_fields("\"unterminated"), std::runtime_error);
}

TEST(StellarSourceTableTest, RegistryCoversEachFieldSpecificReference)
{
    std::set<std::string> names;
    for (const auto &source : stellar_reference_sources())
    {
        EXPECT_TRUE(names.insert(source.name).second);
        EXPECT_EQ(source.url.rfind("https://", 0), 0u);
    }
    EXPECT_EQ(names.count("SIMBAD"), 1u);
    EXPECT_EQ(names.count("Mermilliod"), 1u);
    EXPECT_EQ(names.count("Skiff"), 1u);
    EXPECT_EQ(names.count("WDS"), 1u);
    EXPECT_EQ(names.count("IAU"), 1u);
    EXPECT_EQ(names.count("Gaia_DR3"), 1u);
    EXPECT_EQ(names.count("ORB6"), 1u);
    EXPECT_EQ(names.count("SBX_orbits"), 1u);
    EXPECT_EQ(names.count("DEBCat"), 1u);
    EXPECT_NE(stellar_identifier_key("HD 100 A"), stellar_identifier_key("HD 100 B"));
}

class CondensedCatalogReader : public CatalogReader
{
    public:
    using CatalogReader::write_condensed_star_cat_line;
    using CatalogReader::resolve_or_create_exostar;
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

TEST_F(CondensedCatalogTest, FaintStandaloneBlankAlienorumIdPreservesCatalogIdentity)
{
    std::string row = primary_row();
    put(row, 1, 14, "");
    put(row, 16, 54, "HIP 86032");
    put(row, 77, 82, "+12.50");
    put(row, 142, 152, "");
    write_rows({row});
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_TRUE(star->alienorumid.empty());
    EXPECT_STREQ(star->name, "HIP 86032");
    EXPECT_EQ(star->HIP, 86032);
    EXPECT_EQ(hipcache[86032], star);
    EXPECT_EQ(star->BayerGrkno, -1);
    EXPECT_DOUBLE_EQ(star->apparent_magnitude, 12.5);
    EXPECT_EQ(star->orbit, nullptr);
    EXPECT_EQ(written_row(star).substr(0, 14), std::string(14, ' '));
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

TEST_F(CondensedCatalogTest, BrightStarBlankBayerAndSharedDmKeepDistinctIdentities)
{
    make_star("Sun");
    std::filesystem::create_directories("catalogs/BSC");
    std::ofstream file("catalogs/BSC/catalog", std::ios::binary);
    for (int index = 0; index < 2; ++index)
    {
        std::string row(197, ' ');
        put(row, 1, 4, std::to_string(6375 + index));
        put(row, 5, 7, std::to_string(88 + index));
        put(row, 12, 14, "Oph");
        put(row, 15, 16, "BD");
        put(row, 17, 19, "-10");
        put(row, 20, 25, "4445");
        put(row, 26, 31, std::to_string(155078 + index));
        put(row, 76, 77, "17");
        put(row, 78, 79, "10");
        put(row, 80, 83, index == 0 ? "00.7" : "02.7");
        put(row, 84, 84, "-");
        put(row, 85, 86, "10");
        put(row, 87, 88, "24");
        put(row, 89, 90, "51");
        put(row, 103, 107, "5.43");
        put(row, 110, 114, "0.47");
        put(row, 116, 120, "0.02");
        put(row, 128, 147, "F5IV");
        put(row, 162, 166, "0.024");
        file << row << '\n';
    }
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_BrightStars_catalog(cels, MAX_CELOBJS), 2);
    ASSERT_NE(hdcache[155078], nullptr);
    ASSERT_NE(hdcache[155079], nullptr);
    EXPECT_NE(hdcache[155078], hdcache[155079]);
    for (int index = 0; index < 2; ++index)
    {
        Star *star = hdcache[155078 + index];
        EXPECT_EQ(star->BayerGrkno, -1);
        EXPECT_STREQ(star->Bayer, "");
        EXPECT_EQ(star->HR, 6375 + index);
        EXPECT_EQ(star->FlamsteedNo, 88 + index);
        star->rename_from_Bayer_Flamsteed();
        EXPECT_EQ(std::string(star->name), std::to_string(88 + index) + " Ophiuchi");
        EXPECT_DOUBLE_EQ(star->apparent_magnitude, 5.43);
        EXPECT_DOUBLE_EQ(star->BV_color, 0.47);
        EXPECT_STREQ(star->spectral_type, "F5IV");
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

TEST_F(CondensedCatalogTest, VariableIdsShareOneSequenceAcrossMagnitudeBins)
{
    Star *sun = make_star("Sun");
    sun->apparent_magnitude = -26.72;
    ConsBoundary boundary;
    constellations[0].bounds.push_back(boundary);
    Star *first = make_star("First variable");
    Star *second = make_star("Second variable");
    Star *fixed = make_star("Constant star");
    for (int index = 1; index < ncelobjs; ++index)
    {
        Star *star = (Star*)cels[index];
        strcpy(star->constellation, "Oph");
        star->temperature = 5778;
        star->apparent_magnitude = index == 1 ? 4.2 : 5.2;
    }
    first->variability_period = second->variability_period = oneday;
    first->minmag = second->minmag = 4;
    first->maxmag = second->maxmag = 6;
    ConsBins bins = fill_alienorum_ids();
    EXPECT_EQ(first->alienorumid, "Vy Oph 1");
    EXPECT_EQ(second->alienorumid, "Vy Oph 2");
    EXPECT_EQ(fixed->alienorumid, "5y Oph 2");
    EXPECT_EQ(sun->alienorumid, "-26");
    EXPECT_EQ(bins[&constellations[0]][5]['y'].size(), 2u);
    const std::string original = first->alienorumid;
    fill_alienorum_ids();
    EXPECT_EQ(first->alienorumid, original);
}

TEST_F(CondensedCatalogTest, CatalogWriterEmitsEachStarOnceAcrossRepeatedFamilies)
{
    Star *sun = make_star("Sun");
    sun->alienorumid = "-26";
    Star *primary = make_star("Primary");
    primary->alienorumid = "2y Oph 1";
    primary->set_component('A', primary);
    Star *secondary = make_star("Secondary");
    secondary->set_component('C', primary);
    secondary->alienorumid = "2y Oph 1 C";
    ConsBins bins;
    bins[&constellations[0]][2]['y'].push_back(primary);
    bins[&constellations[0]][2]['y'].push_back(primary);
    bins[&constellations[0]][3]['y'].push_back(secondary);
    CatalogReader reader;
    EXPECT_EQ(reader.write_condensed_star_cat(bins), 3);
    std::ifstream file("catalogs/soles_alienorum.dat");
    std::string line;
    std::set<std::string> identities;
    int rows = 0;
    while (std::getline(file, line))
    {
        EXPECT_EQ(line.size(), 455u);
        EXPECT_TRUE(identities.insert(trim(line.substr(0, 14))).second);
        rows++;
    }
    EXPECT_EQ(rows, 3);
}

TEST_F(CondensedCatalogTest, UnknownMagnitudesRemainBlankAndReloadAsUnknown)
{
    Star *star = make_star("Unknown photometry");
    star->alienorumid = "2y Oph 1";
    star->apparent_magnitude = 1e29;
    star->absolute_magnitude = std::numeric_limits<double>::infinity();
    std::string encoded = written_row(star);
    ASSERT_EQ(encoded.size(), 456u);
    EXPECT_EQ(encoded.substr(76, 6), std::string(6, ' '));
    EXPECT_EQ(encoded.substr(237, 6), std::string(6, ' '));
    star->absolute_magnitude = 0;
    EXPECT_EQ(written_row(star).substr(237, 6), std::string(6, ' '));
    star->apparent_magnitude = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(written_row(star).substr(76, 6), std::string(6, ' '));
    delete_the_universe();
    std::vector<std::string> rows;
    rows.push_back(encoded.substr(0, 455));
    write_rows(rows);
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *restored = (Star*)cels[1];
    EXPECT_EQ(restored->apparent_magnitude, 1e29);
    EXPECT_EQ(restored->absolute_magnitude, 1e29);
}

TEST_F(CondensedCatalogTest, BlankHipparcosVariableNamesPreserveExistingNames)
{
    make_star("Sun");
    Star *first = make_star("HD218999");
    first->HIP = 100;
    first->namelen = strlen(first->name);
    Star *second = make_star("HIP101");
    second->HIP = 101;
    std::filesystem::create_directories("catalogs/Hipparcos");
    std::ofstream("catalogs/Hipparcos/hip_main.dat").close();
    std::ofstream("catalogs/Hipparcos/h_dm_com.dat").close();
    std::ofstream("catalogs/Hipparcos/hip_dm_o.dat").close();
    std::ofstream file("catalogs/Hipparcos/hip_va_1.dat");
    const int identifiers[] =
    {
        100, 101, 102, 120417
    };
    for (int hip : identifiers)
    {
        std::string row(104, ' ');
        put(row, 1, 6, std::to_string(hip));
        put(row, 25, 30, "EA");
        put(row, 34, 39, "7.620");
        put(row, 43, 48, "7.670");
        put(row, 57, 68, "0.67619");
        if (hip == 101)
        {
            put(row, 93, 104, "V123_Oph");
        }
        file << row << '\n';
    }
    file.close();
    CatalogReader reader;
    reader.read_Hipparcos_catalog(cels, MAX_CELOBJS);
    EXPECT_STREQ(first->name, "HD218999");
    EXPECT_EQ(first->namelen, strlen(first->name));
    EXPECT_STREQ(second->name, "V123 Oph");
    EXPECT_TRUE(first->is_eclipsing_binary);
    EXPECT_NEAR(first->variability_period / oneday, 0.67619, 1e-10);
}

TEST_F(CondensedCatalogTest, GlieseComponentMarkerAndMissingDmAreNormalizedAtImport)
{
    std::filesystem::create_directories("catalogs/Gliese");
    std::filesystem::create_directories("catalogs/CNS5");
    std::ofstream("catalogs/CNS5/cns5.dat").close();
    std::ofstream file("catalogs/Gliese/catalog.dat");
    for (int index = 0; index < 3; ++index)
    {
        std::string row(166, ' ');
        put(row, 1, 8, index == 0 ? "Sun" : "Wo 1255");
        if (index > 0)
        {
            put(row, 9, 10, index == 1 ? "A" : "-");
            put(row, 13, 14, "20");
            put(row, 16, 17, "53");
            put(row, 19, 20, "03");
            put(row, 22, 22, "+");
            put(row, 23, 24, "74");
            put(row, 26, 29, "51.6");
        }
        put(row, 55, 66, "G8V");
        put(row, 68, 73, index == 0 ? "-26.72" : "7.81");
        put(row, 76, 80, "0.69");
        put(row, 109, 114, "33.0");
        put(row, 122, 126, "5.36");
        if (index == 2)
        {
            put(row, 154, 155, "BD");
        }
        file << row << '\n';
    }
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_Gliese_catalog(cels, MAX_CELOBJS), 3);
    Star *star = (Star*)cels[2];
    EXPECT_STREQ(star->Gliese, "GJ 1255 D");
    EXPECT_EQ(star->get_component(), 'D');
    EXPECT_STREQ(star->Bonn_survey, "");
    EXPECT_TRUE(dmcache.empty());
    EXPECT_TRUE(bonn_survey_key("BD", 0, 0).empty());
    EXPECT_TRUE(bonn_survey_key("BD", 91, 1).empty());
    EXPECT_TRUE(bonn_survey_key("XX", 0, 1).empty());
    EXPECT_EQ(bonn_survey_key("BD", -4, 4384), "BD-04 04384");
}

TEST_F(CondensedCatalogTest, Sb9UnknownPhotometryStaysUnknownAndZeroIsMeasured)
{
    make_star("Sun");
    Star *primary = make_star("HD10000");
    primary->HD = 10000;
    primary->absolute_magnitude = -5;
    std::filesystem::create_directories("catalogs/SB9");
    std::ofstream("catalogs/SB9/orbits.dat").close();
    std::ofstream file("catalogs/SB9/main.dat");
    const char *magnitudes[] =
    {
        "-", "0.000", "99.000"
    };
    for (int index = 0; index < 3; ++index)
    {
        std::string row(132, ' ');
        put(row, 1, 4, std::to_string(1001 + index));
        put(row, 36, 42, std::string("A") + static_cast<char>('B' + index));
        put(row, 52, 57, magnitudes[index]);
        put(row, 93, 102, "G2V");
        put(row, 104, 132, "HD 10000");
        file << row << '\n';
    }
    file.close();
    CatalogReader reader;
    reader.read_SB9_catalog(cels, MAX_CELOBJS);
    ASSERT_NE(primary->multisys, nullptr);
    Star *unknown = primary->multisys->get_member('B');
    Star *zero = primary->multisys->get_member('C');
    Star *placeholder = primary->multisys->get_member('D');
    ASSERT_NE(unknown, nullptr);
    ASSERT_NE(zero, nullptr);
    ASSERT_NE(placeholder, nullptr);
    EXPECT_EQ(unknown->apparent_magnitude, 1e29);
    EXPECT_EQ(unknown->absolute_magnitude, 1e29);
    EXPECT_DOUBLE_EQ(zero->apparent_magnitude, 0);
    EXPECT_NEAR(zero->absolute_magnitude, 0, 1e-10);
    EXPECT_EQ(placeholder->absolute_magnitude, 1e29);
}

TEST_F(CondensedCatalogTest, NegativeHipparcosFitCannotCreateOrOverwriteAnOrbit)
{
    make_star("Sun");
    Star *primary = make_star("Beta Trianguli");
    primary->HIP = 10064;
    std::string row(100, ' ');
    put(row, 1, 6, "10064");
    put(row, 8, 17, "31.3870");
    put(row, 19, 29, "7729.0700");
    put(row, 31, 38, "-1.56");
    CatalogReader reader;
    int offset = ncelobjs;
    EXPECT_FALSE(reader.load_Hipparcos_orbit_line(row.c_str(), cels, MAX_CELOBJS, offset));
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(primary->multisys, nullptr);
    put(row, 8, 17, "1e308");
    put(row, 31, 38, "1.56");
    EXPECT_FALSE(reader.load_Hipparcos_orbit_line(row.c_str(), cels, MAX_CELOBJS, offset));
    EXPECT_EQ(primary->multisys, nullptr);
    put(row, 8, 17, "31.3870");
    put(row, 31, 38, "-1.56");
    primary->set_component('A', primary);
    Star *secondary = make_star("Known companion");
    secondary->make_companion_of(primary, 'B');
    ASSERT_NE(secondary->orbit, nullptr);
    secondary->orbit->semimajor_axis = AU;
    secondary->orbit->period = oneyear;
    offset = ncelobjs;
    EXPECT_FALSE(reader.load_Hipparcos_orbit_line(row.c_str(), cels, MAX_CELOBJS, offset));
    EXPECT_DOUBLE_EQ(secondary->orbit->semimajor_axis, AU);
    EXPECT_DOUBLE_EQ(secondary->orbit->period, oneyear);
    EXPECT_EQ(ncelobjs, 3);
}

TEST_F(CondensedCatalogTest, ExoplanetHostIdentifiersSurviveUnspacedNames)
{
    make_star("Sun");
    hdcache = new Star*[MAX_HD + 1]();
    hipcache = new Star*[MAX_HIP + 1]();
    CondensedCatalogReader reader;
    ExoRow row;
    row.hostname = "HD110317 A";
    row.ra = 190;
    row.dec = -13;
    row.sy_dist = 80;
    row.sy_vmag = 5.96;
    row.st_spectype = "G2V";
    row.st_mass = 1;
    row.st_rad = 1;
    row.st_teff = 5778;
    bool was_new = false;
    Star *star = reader.resolve_or_create_exostar(row, false, &was_new);
    ASSERT_NE(star, nullptr);
    EXPECT_TRUE(was_new);
    EXPECT_EQ(star->HD, 110317);
    EXPECT_EQ(hdcache[110317], star);
    row.hostname = "HIP102970";
    star = reader.resolve_or_create_exostar(row, false, &was_new);
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(star->HIP, 102970);
    EXPECT_EQ(hipcache[102970], star);
}

TEST_F(CondensedCatalogTest, SourceNamedOrbitCompanionRetainsItsOwnHdIdentifier)
{
    make_star("Sun");
    Star *primary = make_star("HD10000");
    primary->HD = 10000;
    hdcache = new Star*[MAX_HD + 1]();
    hdcache[10000] = primary;
    std::string row(260, ' ');
    put(row, 1, 23, "HD10000");
    put(row, 25, 47, "HD10001");
    put(row, 49, 63, "365.25d");
    put(row, 101, 111, "1AU");
    put(row, 113, 123, "0.1");
    put(row, 161, 175, "G2V");
    put(row, 177, 191, "5.0");
    put(row, 193, 203, "1.0");
    put(row, 205, 215, "1.0");
    put(row, 229, 238, "5778");
    std::ofstream file("catalogs/star_orbits.dat");
    file << row << '\n';
    file.close();
    CatalogReader reader;
    reader.read_star_orbits_dat(cels);
    ASSERT_NE(hdcache[10001], nullptr);
    Star *secondary = hdcache[10001];
    EXPECT_EQ(secondary->HD, 10001);
    EXPECT_STREQ(secondary->name, "HD10001");
    ASSERT_NE(secondary->orbit, nullptr);
    EXPECT_EQ(secondary->orbit->center, primary);
    EXPECT_EQ(hdcache[10000], primary);
    EXPECT_EQ(std::stoi(written_row(secondary).substr(113, 6)), 10001);
}

TEST_F(CondensedCatalogTest, MagnitudesFollowFinalDistanceAndPreserveApparentZero)
{
    Star *star = make_star();
    star->distance = 100 * parsec;
    star->apparent_magnitude = 0;
    star->absolute_magnitude = 12;
    EXPECT_TRUE(star->reconcile_visual_magnitudes());
    EXPECT_DOUBLE_EQ(star->apparent_magnitude, 0);
    EXPECT_NEAR(star->absolute_magnitude, -5, 1e-12);
    EXPECT_FALSE(star->reconcile_visual_magnitudes());
    star->distance = 10 * parsec;
    EXPECT_TRUE(star->reconcile_visual_magnitudes());
    EXPECT_DOUBLE_EQ(star->absolute_magnitude, 0);
}

TEST_F(CondensedCatalogTest, AbsentApparentMagnitudeIsDerivedFromAbsoluteZero)
{
    Star *star = make_star();
    star->distance = 100 * parsec;
    star->absolute_magnitude = 0;
    EXPECT_TRUE(star->reconcile_visual_magnitudes());
    EXPECT_NEAR(star->apparent_magnitude, 5, 1e-12);
    EXPECT_DOUBLE_EQ(star->absolute_magnitude, 0);
    EXPECT_FALSE(star->reconcile_visual_magnitudes());
}

TEST_F(CondensedCatalogTest, InvalidDistanceOrUnknownPhotometryIsNotInvented)
{
    Star *star = make_star();
    star->apparent_magnitude = 4;
    star->distance = 0;
    EXPECT_FALSE(star->reconcile_visual_magnitudes());
    star->distance = -parsec;
    EXPECT_FALSE(star->reconcile_visual_magnitudes());
    star->distance = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(star->reconcile_visual_magnitudes());
    star->distance = 10 * parsec;
    star->apparent_magnitude = star->absolute_magnitude = 1e29;
    EXPECT_FALSE(star->reconcile_visual_magnitudes());
    EXPECT_DOUBLE_EQ(star->apparent_magnitude, 1e29);
    EXPECT_DOUBLE_EQ(star->absolute_magnitude, 1e29);
}

TEST_F(CondensedCatalogTest, OrbitOverridesPreserveAbsentFieldsAndObservedZeroColors)
{
    make_star("Sun");
    Star *primary = make_star("HD10000");
    Star *secondary = make_star("HD10001");
    primary->HD = 10000;
    secondary->HD = 10001;
    hdcache = new Star*[MAX_HD + 1]();
    hdcache[10000] = primary;
    hdcache[10001] = secondary;
    secondary->BV_color = secondary->UB_color = 0;
    secondary->has_catalog_BV = secondary->has_catalog_UB = true;
    const double mass = secondary->mass;
    const double radius = secondary->volumetric_mean_radius;
    std::string row(260, ' ');
    put(row, 1, 23, "HD10000");
    put(row, 25, 47, "HD10001");
    put(row, 49, 63, "365.25d");
    put(row, 101, 111, "1AU");
    put(row, 177, 191, "0.0");
    put(row, 229, 238, "3054");
    std::ofstream file("catalogs/star_orbits.dat");
    file << row << '\n';
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 1);
    EXPECT_STREQ(secondary->spectral_type, "G2V");
    EXPECT_DOUBLE_EQ(secondary->mass, mass);
    EXPECT_DOUBLE_EQ(secondary->volumetric_mean_radius, radius);
    EXPECT_DOUBLE_EQ(secondary->absolute_magnitude, 0);
    EXPECT_DOUBLE_EQ(secondary->temperature, 3054);
    EXPECT_DOUBLE_EQ(secondary->BV_color, 0);
    EXPECT_DOUBLE_EQ(secondary->UB_color, 0);
    const std::string encoded = written_row(secondary);
    EXPECT_EQ(encoded.substr(99, 6), "+00.00");
    EXPECT_EQ(encoded.substr(106, 6), "+00.00");
}

TEST_F(CondensedCatalogTest, InvalidOrbitParametersDoNotReplacePhysicalMeasurements)
{
    make_star("Sun");
    Star *primary = make_star("HD10000");
    Star *secondary = make_star("HD10001");
    primary->HD = 10000;
    secondary->HD = 10001;
    hdcache = new Star*[MAX_HD + 1]();
    hdcache[10000] = primary;
    hdcache[10001] = secondary;
    secondary->BV_color = 0.49;
    secondary->UB_color = 0.08;
    const double mass = secondary->mass;
    const double radius = secondary->volumetric_mean_radius;
    const double temperature = secondary->temperature;
    std::string row(260, ' ');
    put(row, 1, 23, "HD10000");
    put(row, 25, 47, "HD10001");
    put(row, 49, 63, "365.25d");
    put(row, 101, 111, "1AU");
    put(row, 193, 203, "nan");
    put(row, 205, 215, "-1");
    put(row, 229, 238, "inf");
    std::ofstream file("catalogs/star_orbits.dat");
    file << row << '\n';
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 1);
    EXPECT_DOUBLE_EQ(secondary->mass, mass);
    EXPECT_DOUBLE_EQ(secondary->volumetric_mean_radius, radius);
    EXPECT_DOUBLE_EQ(secondary->temperature, temperature);
    EXPECT_DOUBLE_EQ(secondary->BV_color, 0.49);
    EXPECT_DOUBLE_EQ(secondary->UB_color, 0.08);
}

TEST_F(CondensedCatalogTest, CondensedZeroColorsSurviveTemperatureOverrideAndWriting)
{
    std::string row = primary_row();
    put(row, 100, 105, "+00.00");
    put(row, 107, 112, "+00.00");
    std::vector<std::string> rows;
    rows.push_back(row);
    write_rows(rows);
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    Star *star = (Star*)cels[1];
    EXPECT_TRUE(star->has_catalog_BV);
    EXPECT_TRUE(star->has_catalog_UB);
    const std::string encoded = written_row(star);
    EXPECT_EQ(encoded.substr(99, 6), "+00.00");
    EXPECT_EQ(encoded.substr(106, 6), "+00.00");
}

TEST_F(CondensedCatalogTest, GaiaDistanceUpdateRetainsUnknownCompanionBrightness)
{
    make_star("Sun");
    Star *primary = make_star("HIP100");
    primary->HIP = 100;
    strcpy(primary->spectral_type, "DA");
    primary->absolute_magnitude = 12;
    primary->set_component('A', primary);
    Star *secondary = make_star("HIP100 B");
    secondary->make_companion_of(primary, 'B');
    secondary->absolute_magnitude = 13;
    Star *third = make_star("HIP100 C");
    third->make_companion_of(primary, 'C');
    third->apparent_magnitude = 0;
    std::filesystem::create_directories("catalogs/GAIA");
    std::ofstream file("catalogs/GAIA/catalog.dat");
    file << "100|123456|0|0|0|0|0|10|0|0|0\n";
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.apply_gaia_astrometry(cels), 1);
    EXPECT_NEAR(primary->distance / parsec, 100, 1e-12);
    EXPECT_DOUBLE_EQ(primary->absolute_magnitude, 12);
    EXPECT_DOUBLE_EQ(secondary->absolute_magnitude, 13);
    EXPECT_DOUBLE_EQ(secondary->apparent_magnitude, 1e29);
    EXPECT_NEAR(third->absolute_magnitude, -5, 1e-12);
    EXPECT_TRUE(secondary->reconcile_visual_magnitudes());
    EXPECT_NEAR(secondary->apparent_magnitude, 18, 1e-12);
}

TEST_F(CondensedCatalogTest, HipparcosOrbitFitPreservesExistingApparentMagnitude)
{
    make_star("Sun");
    Star *primary = make_star("HIP100");
    primary->HIP = 100;
    primary->set_component('A', primary);
    Star *secondary = make_star("HIP100 B");
    secondary->make_companion_of(primary, 'B');
    secondary->apparent_magnitude = 0;
    std::string row(100, ' ');
    put(row, 1, 6, "100");
    put(row, 8, 17, "31.3870");
    put(row, 19, 29, "7729.0700");
    put(row, 31, 38, "1.56");
    CatalogReader reader;
    int offset = ncelobjs;
    ASSERT_TRUE(reader.load_Hipparcos_orbit_line(row.c_str(), cels, MAX_CELOBJS, offset));
    EXPECT_DOUBLE_EQ(secondary->apparent_magnitude, 0);
}
