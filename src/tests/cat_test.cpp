#include <cstring>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <set>
#include <vector>
#include <stdexcept>
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "../classes/cons.h"
#include "../classes/sscimport.h"
#include "../classes/shore.h"
#include "../classes/exoplanetstatus.h"
#include "../loaders.h"
#include "../globals.h"
#include "../housekeeping.h"
#include "universe_fixture.h"
#include "../classes/browncatalog.h"

using namespace alienorum;

// The catalog readers are column-oriented: every field is a fixed range of characters in a line of
// a file, transcribed from a ReadMe, and a range that is off by one silently reads the neighbouring
// field's digits. Nothing else in the program can tell you that has happened -- an asteroid with
// the wrong semimajor axis is still an asteroid -- so the parsers are tested here against real
// lines, copied out of the catalogs verbatim. No file is opened and nothing is downloaded: both
// loaders take the line as an argument, which is how read_astorb_catalog() and
// read_comets_catalog() call them once they have one in hand.

class CatalogParsingTest : public UniverseFixture
{
    protected:
    // Both loaders hang what they build off cels[0], the Sun.
    void SetUp() override
    {
        UniverseFixture::SetUp();
        make_star("Sol");
    }
};

class StellarRotationTest : public CatalogParsingTest
{
    protected:
    std::filesystem::path original, temporary;
    Star **saved_hd = nullptr, **saved_hip = nullptr;
    bool saved_baked = false;
    std::vector<Star*> hd, hip;

    void SetUp() override
    {
        CatalogParsingTest::SetUp();
        Star::load_main_seq_dat();
        original = std::filesystem::current_path();
        temporary = std::filesystem::temp_directory_path() / ("alienorum_rotation_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporary / "catalogs/StellarRotation");
        std::filesystem::current_path(temporary);
        saved_hd = hdcache;
        saved_hip = hipcache;
        saved_baked = CatalogReader::condensed_star_orbits_loaded;
        hd.resize(MAX_HD + 1, nullptr);
        hip.resize(MAX_HIP + 1, nullptr);
        hdcache = hd.data();
        hipcache = hip.data();
    }

    void TearDown() override
    {
        delete_the_universe();
        hdcache = saved_hd;
        hipcache = saved_hip;
        CatalogReader::condensed_star_orbits_loaded = saved_baked;
        std::filesystem::current_path(original);
        std::filesystem::remove_all(temporary);
        CatalogParsingTest::TearDown();
    }

    Star* target(int identifier)
    {
        Star* object = make_star(("HD " + std::to_string(identifier)).c_str());
        object->HD = identifier;
        hd[identifier] = object;
        object->right_ascension = 1.2;
        object->declination = -0.4;
        object->update_location(simnow);
        return object;
    }

    std::string row(const char* host, const char* period, const char* inclination = "", const char* node = "")
    {
        std::string value(100, ' ');
        value.replace(0, strlen(host), host);
        value.replace(24, 18, "(stellar rotation)");
        value.replace(48, strlen(period), period);
        value.replace(64, strlen(node), node);
        value.replace(76, strlen(inclination), inclination);
        return value;
    }
};

TEST_F(StellarRotationTest, HipparcosAndTychoPropagateInIcrsAndKeepZeroCoordinates)
{
    std::filesystem::create_directories("catalogs/Hipparcos");
    const auto row = [](bool tycho, const std::string& hip, const std::string& dec)
    {
        std::string line(450, ' ');
        line.replace(tycho ? 210 : 8, hip.size(), hip);
        line.replace(41, 4, "5.00");
        line.replace(51, 3, "0.0");
        line.replace(64, dec.size(), dec);
        line.replace(79, 4, "10.0");
        line.replace(87, 4, "1000");
        line.replace(96, 2, "0?");
        line.replace(245, 4, "0.65");
        return line + '\n';
    };
    std::ofstream("catalogs/Hipparcos/hip_main.dat") << row(false, "12345", "60") << row(false, "12346", "0");
    std::ofstream("catalogs/Hipparcos/tyc_main.dat") << row(true, "12347", "60");

    CatalogReader reader;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 2);
    ASSERT_EQ(reader.read_Tycho_catalog(cels, MAX_CELOBJS), 1);
    for (const int id : {12345, 12346, 12347})
    {
        const Star* star = hipcache[id];
        ASSERT_NE(star, nullptr);
        const double factor = id == 12346 ? 1 : 2;
        EXPECT_NEAR(star->right_ascension * fiftyseven, 8.75 * factor / 3600, 1e-10);
        EXPECT_NEAR(star->proper_motion_RA * fiftyseven * oneyear, factor / 3600, 1e-10);
        EXPECT_DOUBLE_EQ(star->proper_motion_decl, 0);
    }
}

TEST_F(StellarRotationTest, HipparcosVariabilityImportSkipsRowsWithoutLoadedStars)
{
    std::filesystem::create_directories("catalogs/Hipparcos");
    std::string main_row(450, ' ');
    main_row.replace(8, 5, "12345");
    main_row.replace(41, 4, "5.00");
    main_row.replace(51, 3, "0.0");
    main_row.replace(64, 3, "0.0");
    main_row.replace(79, 4, "10.0");
    main_row.replace(435, 3, "G2V");
    std::ofstream("catalogs/Hipparcos/hip_main.dat") << main_row << '\n';
    std::ofstream{"catalogs/Hipparcos/h_dm_com.dat"};
    std::ofstream{"catalogs/Hipparcos/hip_dm_o.dat"};
    const auto variable_row = [](const std::string& hip)
    {
        std::string row(105, ' ');
        row.replace(0, hip.size(), hip);
        row.replace(24, 4, "DSCT");
        row.replace(56, 3, "0.5");
        row.replace(92, 12, "Test Cepheid");
        return row + '\n';
    };
    std::ofstream("catalogs/Hipparcos/hip_va_1.dat")
        << variable_row("12346") << variable_row("999999") << variable_row("12345");

    CatalogReader reader;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 1);
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(hipcache[12346], nullptr);
    const auto* star = hipcache[12345];
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(star->variability_type, "DSCT");
    EXPECT_DOUBLE_EQ(star->variability_period, .5 * oneday);
    EXPECT_STREQ(star->name, "Test Cepheid");
}

TEST_F(StellarRotationTest, CondensedWriterKeepsVerifiedIdentityWhenOnlyTheAliasWasImported)
{
    std::ofstream("starname.dat") << "# Duplicate star \"GJ 4365\" \"WD2347+292\"\n"
        << "# Duplicate star \"GJ 4366\" \"WDJ235501.23+290402.34\"\n";
    Star* dwarf = target(12345);
    std::strcpy(dwarf->name, "GJ 4365");
    std::strcpy(dwarf->Gliese, dwarf->name);
    std::strcpy(dwarf->spectral_type, "DA9");
    dwarf->apparent_magnitude = 15.72;
    Star* gaia_dwarf = target(12346);
    std::strcpy(gaia_dwarf->name, "GJ 4366");
    std::strcpy(gaia_dwarf->Gliese, gaia_dwarf->name);
    std::strcpy(gaia_dwarf->spectral_type, "DA8");

    CatalogReader reader;
    ASSERT_EQ(reader.write_condensed_star_cat({}), 3);
    delete_the_universe();
    ASSERT_EQ(reader.read_condensed_star_cat(), 3);

    const int canonical = find_object("WD2347+292", true, 9e29, 1);
    ASSERT_GE(canonical, 0);
    EXPECT_STREQ(cels[canonical]->name, "WD2347+292");
    EXPECT_EQ(find_object("GJ 4365", true, 9e29, 1), canonical);
    EXPECT_DOUBLE_EQ(static_cast<Star*>(cels[canonical])->apparent_magnitude, 15.72);
    const int gaia = find_object("WDJ235501.23+290402.34", true, 9e29, 1);
    ASSERT_GE(gaia, 0);
    EXPECT_STREQ(cels[gaia]->name, "WDJ235501.23+290402.34");
    EXPECT_EQ(find_object("GJ 4366", true, 9e29, 1), gaia);
}

TEST_F(StellarRotationTest, DurchmusterungZeroZonesHaveDistinctKeysAndRoundTrip)
{
    EXPECT_EQ(bonn_survey_key("BD", 0, 120, '-'), "BD-00 00120");
    EXPECT_EQ(bonn_survey_key("BD", 0, 120, '+'), "BD+00 00120");
    Star* object = target(12345);
    strcpy(object->Bonn_survey, "BD");
    object->Bonn_survey_sign = '-';
    object->Bonn_survey_sequential = 120;
    const auto line = CatalogReader::format_condensed_star_line(object);
    EXPECT_EQ(line.substr(441, 4), "-00 ");
    std::ofstream out("catalogs/soles_alienorum.dat");
    out << CatalogReader::format_condensed_star_line(static_cast<Star*>(cels[0])) << line;
    out.close();
    delete_the_universe();
    CatalogReader reader;
    ASSERT_TRUE(reader.read_condensed_star_cat());
    EXPECT_EQ(static_cast<Star*>(cels[1])->Bonn_survey_sign, '-');
}

TEST_F(StellarRotationTest, AuthoredBondAlbedoIncludesZeroAndSurvivesSave)
{
    std::ofstream("catalogs/planets.json") << R"([
        {"BODYNAME":"Calibration","CENTER_OF_ORBIT":"Sol","Mass":5.9722e24,
         "VolMeanRad":6371000,"SEMIMAJOR_AXIS":149597870700,"ABSMG":5,"BondAlbedo":"0?"}
    ])";
    CatalogReader reader;
    ASSERT_EQ(reader.read_local_planets(cels, MAX_CELOBJS, cels[0]), 1);
    Planet* world = static_cast<Planet*>(cels[1]);
    EXPECT_DOUBLE_EQ(world->estimate_bond_albedo(), 0);
    Planet restored;
    ASSERT_TRUE(restored.from_json(world->to_json()));
    EXPECT_DOUBLE_EQ(restored.estimate_bond_albedo(), 0);
}

TEST_F(StellarRotationTest, CatalogJulianYear2000StartsAtNoon)
{
    std::ofstream("catalogs/planets.json") << R"([
        {"BODYNAME":"Epoch test","CENTER_OF_ORBIT":"Sol","Epoch":2000,"SEMIMAJOR_AXIS":1}
    ])";
    CatalogReader reader;
    ASSERT_EQ(reader.read_local_planets(cels, MAX_CELOBJS, cels[0]), 1);
    ASSERT_NE(cels[1]->orbit, nullptr);
    EXPECT_DOUBLE_EQ(cels[1]->orbit->epoch, 2451545.0);
    EXPECT_DOUBLE_EQ(cels[1]->epoch, 2451545.0);
    EXPECT_EQ(from_iso_string("2000-01-01T00:00:00"), J2000_TIME_T);
}

TEST_F(StellarRotationTest, CensusPhotometryReplacesAnOldDefaultAndPreservesMeasuredZero)
{
    Star* dwarf = target(12345);
    dwarf->stellar_parameters = {{"census_component", true}};
    dwarf->mass = .07 * solar_mass;
    dwarf->volumetric_mean_radius = jupiter_radius;
    dwarf->temperature = 0;
    dwarf->spectral_type[0] = '\0';
    dwarf->absolute_magnitude = 0;
    Star* measured = target(12346);
    measured->absolute_magnitude = 0;

    CatalogReader reader;
    ASSERT_EQ(reader.write_condensed_star_cat({}), 3);
    delete_the_universe();
    ASSERT_EQ(reader.read_condensed_star_cat(), 3);
    ASSERT_NE(hdcache[12345], nullptr);
    ASSERT_NE(hdcache[12346], nullptr);
    EXPECT_GT(hdcache[12345]->absolute_magnitude, 12);
    EXPECT_LT(hdcache[12345]->absolute_magnitude, 60);
    EXPECT_DOUBLE_EQ(hdcache[12346]->absolute_magnitude, 0);
}

TEST_F(StellarRotationTest, TauCetiCatalogBeltFeedsTheVolatileBudget)
{
    std::filesystem::copy_file(original / "catalogs/star_orbits.dat", "catalogs/star_orbits.dat");
    Star* host = target(10700);
    Planet* world = make_planet(host);
    const auto before = CosmicShore::estimate_atmosphere(*host, *world);
    CatalogReader reader;
    reader.read_star_orbits_dat(cels);
    ASSERT_TRUE(host->has_disk);
    EXPECT_DOUBLE_EQ(host->disk_inner_edge_sma, 1.5e12);
    const auto after = CosmicShore::estimate_atmosphere(*host, *world);
    ASSERT_TRUE(after.valid);
    EXPECT_TRUE(after.diagnostics["impacts"]["active"].get<bool>());
    EXPECT_NE(after.pressure, before.pressure);
}

TEST_F(StellarRotationTest, MiraClassImportsEvenWhenHipparcosAlreadySuppliedThePeriod)
{
    Star* star = target(14386);
    hip[10826] = star;
    star->variability_period = 333.8 * oneday;
    star->epoch_max_brightness = 2450000;
    star->minmag = 3;
    star->maxmag = 7;
    std::filesystem::create_directories("catalogs/GCVS");
    std::string cross(60, ' ');
    cross.replace(0, 9, "Hip 10826");
    cross[32] = '=';
    cross.replace(33, 7, "omi Cet");
    std::ofstream("catalogs/GCVS/data") << cross << '\n';
    std::string entry(130, ' ');
    entry.replace(8, 7, "omi Cet");
    entry[41] = 'M';
    // Classification is useful even if this catalog has no epoch or fitted curve.
    std::ofstream("catalogs/GCVS/gcvs_cat.dat") << entry << '\n';
    CatalogReader reader;
    EXPECT_EQ(reader.read_GCVS_catalog(cels), 0);
    EXPECT_EQ(star->variability_type, "M");
    EXPECT_TRUE(star->is_mira_variable());
    EXPECT_DOUBLE_EQ(star->variability_period, 333.8 * oneday);
    EXPECT_DOUBLE_EQ(star->epoch_max_brightness, 2450000);
    EXPECT_DOUBLE_EQ(star->minmag, 3);
    EXPECT_DOUBLE_EQ(star->maxmag, 7);
}

TEST_F(StellarRotationTest, VisualCatalogLettersDoNotJoinExplicitSystems)
{
    std::filesystem::copy_file(original / "catalogs/stellar_systems.json", "catalogs/stellar_systems.json");
    std::filesystem::create_directories("catalogs/BSC");
    std::ofstream catalog("catalogs/BSC/catalog");
    for (const auto& entry : std::vector<std::pair<int, char>>{{37020, 'A'}, {37022, 'C'}})
    {
        std::string line(197, ' ');
        line.replace(4, 10, " 41The1Ori");
        line.replace(25, 5, std::to_string(entry.first));
        line[49] = entry.second;
        line.replace(75, 2, "05");
        line.replace(77, 2, "35");
        line.replace(79, 4, "16.0");
        line[83] = '-';
        line.replace(84, 2, "05");
        line.replace(86, 2, "23");
        line.replace(88, 2, "20");
        line.replace(102, 5, " 6.00");
        line.replace(127, 3, "B1V");
        catalog << line << '\n';
    }
    catalog.close();
    CatalogReader reader;
    EXPECT_EQ(reader.read_BrightStars_catalog(cels, MAX_CELOBJS), 2);
    ASSERT_NE(hd[37020], nullptr);
    ASSERT_NE(hd[37022], nullptr);
    EXPECT_FALSE(hd[37020]->orbit);
    EXPECT_FALSE(hd[37022]->orbit);
    EXPECT_FALSE(hd[37020]->multisys);
    EXPECT_FALSE(hd[37022]->multisys);
    std::filesystem::create_directories("catalogs/Hipparcos");
    std::ofstream("catalogs/Hipparcos/hip_main.dat").close();
    hip[26220] = hd[37020];
    hd[37020]->HIP = 26220;
    std::ofstream hip_components("catalogs/Hipparcos/h_dm_com.dat");
    for (char component : {'A', 'D'})
    {
        std::string line(250, ' ');
        line.replace(0, 10, "05353-0524");
        line.replace(37, 2, " 2");
        line[40] = component;
        line.replace(42, 5, "26220");
        hip_components << line << '\n';
    }
    hip_components.close();
    const int objects_before = ncelobjs;
    reader.read_Hipparcos_catalog(cels, MAX_CELOBJS);
    EXPECT_EQ(ncelobjs, objects_before);
    EXPECT_FALSE(hd[37020]->multisys);
    std::filesystem::create_directories("catalogs/CCDM");
    std::ofstream ccdm("catalogs/CCDM/ccdm.dat");
    for (const auto& entry : std::vector<std::pair<int, char>>{{37020, 'A'}, {37022, 'B'}})
    {
        std::string line(140, ' ');
        line.replace(1, 10, "05353-0524");
        line[12] = entry.second;
        line.replace(98, 5, std::to_string(entry.first));
        ccdm << line << '\n';
    }
    ccdm.close();
    EXPECT_EQ(reader.read_CCDM_catalog(cels, MAX_CELOBJS), 0);
    EXPECT_FALSE(hd[37020]->orbit);
    EXPECT_FALSE(hd[37022]->orbit);
}

TEST_F(StellarRotationTest, OrionSystemsKeepIdentityHierarchyAndPlanesAcrossReload)
{
    std::filesystem::copy_file(original / "catalogs/stellar_systems.json", "catalogs/stellar_systems.json");
    std::ofstream("catalogs/star_orbits.dat") << "# Explicit system test\n";
    CatalogReader reader;
    const int imported = reader.read_stellar_systems(cels);
    EXPECT_GT(imported, 25);
    const int total = ncelobjs;
    std::map<std::string, double> initial_magnitudes;
    for (int i = 1; cels[i]; ++i) initial_magnitudes[cels[i]->name] = static_cast<Star*>(cels[i])->apparent_magnitude;
    EXPECT_EQ(reader.read_stellar_systems(cels), imported);
    EXPECT_EQ(ncelobjs, total);
    for (int i = 1; cels[i]; ++i)
    {
        EXPECT_NEAR(static_cast<Star*>(cels[i])->apparent_magnitude, initial_magnitudes.at(cels[i]->name), 1e-12);
    }
    hd[37020]->BayerGrkno = 7;
    hd[37020]->FlamsteedNo = 41;
    strcpy(hd[37020]->constellation, "Ori");
    strcpy(hd[37020]->Bayer, "The1Ori");
    const auto check = [&]()
    {
        std::set<StarMulti*> groups;
        for (int group = 1; group <= 2; ++group)
        {
            for (char letter = 'A'; letter <= (group == 1 ? 'I' : 'C'); ++letter)
            {
                const std::string alias = "The" + std::to_string(group) + "Ori " + letter;
                const int index = find_object(alias.c_str(), true, 9e29, 1);
                ASSERT_GE(index, 0) << alias;
                Star* root = static_cast<Star*>(cels[index]);
                EXPECT_FALSE(root->orbit) << root->name;
                EXPECT_TRUE(groups.insert(root->multisys).second) << root->name;
                root->rename_from_Bayer_Flamsteed();
                EXPECT_EQ(root->name, root->stellar_system.at("name").get<std::string>());
            }
        }
        const auto named = [](const char* name) -> Star*
        {
            const int index = find_object(name, true, 9e29, 1);
            return index >= 0 ? static_cast<Star*>(cels[index]) : nullptr;
        };
        Star* a3 = named("Theta1 Orionis A3");
        Star* b2 = named("Theta1 Orionis B2");
        Star* b3 = named("Theta1 Orionis B3");
        Star* c2 = named("Theta1 Orionis C2");
        Star* d3 = named("Theta1 Orionis D3");
        ASSERT_NE(a3, nullptr);
        ASSERT_NE(b2, nullptr);
        ASSERT_NE(b3, nullptr);
        ASSERT_NE(c2, nullptr);
        ASSERT_NE(d3, nullptr);
        ASSERT_NE(a3->orbit, nullptr);
        ASSERT_NE(b3->orbit, nullptr);
        EXPECT_EQ(b3->orbit->center, b2);
        EXPECT_EQ(b3->multisys, b2->multisys);
        EXPECT_NEAR(a3->orbit->period / oneday, 65.4333, 1e-8);
        EXPECT_NEAR(a3->orbit->heliocentric_inclination * fiftyseven, 87.090, 1e-8);
        EXPECT_TRUE(a3->stellar_system.at("orbit").at("node_assumed"));
        EXPECT_FALSE(a3->known_poles);
        EXPECT_NEAR(c2->orbit->heliocentric_node * fiftyseven, 27.9, 1e-8);
        EXPECT_NEAR(d3->orbit->heliocentric_inclination * fiftyseven, 160, 1e-8);
        EXPECT_TRUE(c2->known_poles);
        EXPECT_FALSE(b3->known_poles);
        EXPECT_TRUE(b3->stellar_system.at("orbit").at("sma_estimated"));
        Star* a = named("The1Ori A");
        Star* c = named("The1Ori C");
        const double distance = c->distance;
        c->make_companion_of(a, 'Z');
        EXPECT_FALSE(c->orbit);
        EXPECT_NEAR(a->right_ascension * fiftyseven, 83.81603095833333, 1e-12);
        EXPECT_NEAR(a->declination * fiftyseven, -5.387317805555556, 1e-12);
        EXPECT_DOUBLE_EQ(c->distance, distance);
        EXPECT_NE(c->multisys, a->multisys);
        for (const auto& calibration : std::vector<std::pair<std::string, double>>
            {{"Theta1 Orionis A", 6.72}, {"Theta1 Orionis B", 7.90}, {"Theta1 Orionis C", 5.13},
             {"Theta1 Orionis D", 6.70}, {"Theta1 Orionis E", 11.40}})
        {
            double flux = 0;
            for (int i = 1; cels[i]; ++i)
            {
                Star* member = static_cast<Star*>(cels[i]);
                if (member->stellar_system.value("system", "") != calibration.first) continue;
                flux += pow(10.0, -.4 * member->apparent_magnitude);
                EXPECT_TRUE(member->apparent_magnitude_estimated);
                EXPECT_TRUE(member->stellar_system.at("photometry").at("v_estimated"));
            }
            EXPECT_NEAR(-2.5 * log10(flux), calibration.second, .006);
        }
        Star* b1 = named("Theta1 Orionis B1");
        Star* b6 = named("Theta1 Orionis B6");
        ASSERT_NE(b1, nullptr);
        ASSERT_NE(b6, nullptr);
        EXPECT_LT(b1->apparent_magnitude, b6->apparent_magnitude);
        EXPECT_LT(b1->apparent_magnitude, b2->apparent_magnitude);
        EXPECT_LT(b1->apparent_magnitude, b3->apparent_magnitude);
        EXPECT_LT(c->apparent_magnitude, c2->apparent_magnitude);
        EXPECT_EQ(b6->stellar_system.at("photometry").at("relative_measurement").at("band"), "K");
        Star* a2 = named("Theta1 Orionis A2");
        ASSERT_NE(a2, nullptr);
        EXPECT_DOUBLE_EQ(a2->temperature, 6500);
        EXPECT_TRUE(a2->stellar_system.at("temperature_estimated"));
        Star restored;
        restored.from_json(b3->to_json());
        EXPECT_EQ(restored.stellar_system, b3->stellar_system);
    };
    check();
    ASSERT_TRUE(reader.bake_condensed_star_orbits());
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    EXPECT_EQ(reader.read_condensed_star_cat(), total);
    check();
    std::fstream output("orion-universe.json", std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(output, cels, false));
    output.close();
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    std::fstream input("orion-universe.json", std::ios::in);
    ASSERT_TRUE(Serialization::load_all(input, cels, MAX_CELOBJS, false));
    EXPECT_EQ(ncelobjs, total);
    check();
}

TEST_F(StellarRotationTest, TrapeziumPrimaryGeometryMatchesTheWikipediaPhotograph)
{
    std::filesystem::copy_file(original / "catalogs/stellar_systems.json", "catalogs/stellar_systems.json");
    std::ofstream("catalogs/star_orbits.dat") << "# Explicit system test\n";
    CatalogReader reader;
    ASSERT_EQ(reader.read_stellar_systems(cels), 28);
    const auto root = [](char letter)
    {
        const std::string name = std::string("The1Ori ") + letter;
        const int index = find_object(name.c_str(), true, 9e29, 1);
        return index >= 0 ? static_cast<Star*>(cels[index]) : nullptr;
    };
    const auto check = [&]()
    {
        Star* c = root('C');
        ASSERT_NE(c, nullptr);
        const CelestialLocation origin;
        const double azimuth = -c->RA_as_radians(origin, azimuth_correction);
        const double altitude = c->Decl_as_radians(origin);
        struct RestoreView
        {
            ViewMode saved = view_mode;
            ~RestoreView() { view_mode = saved; }
        } restore;
        view_mode = vm_spaceship;
        std::map<char, Cartesian2D> projected;
        for (char letter : {'A', 'B', 'C', 'D', 'E'})
        {
            Star* object = root(letter);
            ASSERT_NE(object, nullptr);
            EXPECT_FALSE(object->orbit);
            projected[letter] = Cartesian2D(Point(object->location), azimuth, altitude, 1);
        }
        // Approximate stellar-core centers measured in the 296x326 Wikipedia photograph:
        // https://en.wikipedia.org/wiki/File:TrapeziumStars.jpg
        // C and D establish rotation and scale; A, B, and E independently verify the figure.
        const std::map<char, Cartesian2D> photograph =
        {
            {'A', {187.35, 136.45}}, {'B', {174.95, 111.97}}, {'C', {156.51, 162.44}},
            {'D', {120.54, 140.84}}, {'E', {191.33, 123.22}}
        };
        const Cartesian2D baseline(projected.at('D').x - projected.at('C').x, projected.at('D').y - projected.at('C').y);
        const Cartesian2D image_baseline(photograph.at('D').x - photograph.at('C').x, photograph.at('D').y - photograph.at('C').y);
        const double denominator = baseline.x * baseline.x + baseline.y * baseline.y;
        const double a = (baseline.x * image_baseline.x + baseline.y * image_baseline.y) / denominator;
        const double b = (baseline.x * image_baseline.y - baseline.y * image_baseline.x) / denominator;
        for (char letter : {'A', 'B', 'E'})
        {
            const Cartesian2D offset(projected.at(letter).x - projected.at('C').x, projected.at(letter).y - projected.at('C').y);
            const Cartesian2D aligned(photograph.at('C').x + a * offset.x - b * offset.y,
                photograph.at('C').y + b * offset.x + a * offset.y);
            EXPECT_LT(std::hypot(aligned.x - photograph.at(letter).x, aligned.y - photograph.at(letter).y), 2.0) << letter;
        }
    };
    check();
    ASSERT_TRUE(reader.bake_condensed_star_orbits());
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    ASSERT_EQ(reader.read_condensed_star_cat(), 29);
    check();
}

TEST_F(StellarRotationTest, SystemPolesRequireBothAnglesIncludingExplicitZeros)
{
    std::ofstream file("catalogs/star_orbits.dat");
    for (int index = 0; index < 5; ++index)
    {
        target(99980 + index)->known_poles = true;
        const char* inclinations[] = {"", "90", "", "0", "90"};
        const char* nodes[] = {"", "", "0", "0", "25"};
        std::string line = row(("HD" + std::to_string(99980 + index)).c_str(), "",
            inclinations[index], nodes[index]);
        line.replace(24, 23, "(system inclination)   ");
        file << line << '\n';
    }
    file.close();
    CatalogReader reader;
    reader.read_star_orbits_dat(cels);
    for (int index = 0; index < 5; ++index)
        EXPECT_EQ(hd[99980 + index]->known_poles, index >= 3) << index;
    ASSERT_TRUE(reader.bake_condensed_star_orbits());
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    ASSERT_EQ(reader.read_condensed_star_cat(), 6);
    for (int index = 0; index < 5; ++index)
        EXPECT_EQ(hd[99980 + index]->known_poles, index >= 3) << index;
}

TEST_F(StellarRotationTest, PartialRotationPolesAndCompanionOrbitsAreNotKnown)
{
    Star* primary = target(99970);
    Star* companion = target(99971);
    companion->make_companion_of(primary, 'B');
    CatalogReader reader;
    for (const auto& angles : std::vector<std::pair<std::string, std::string>>
        {{"", ""}, {"90", ""}, {"", "0"}, {"0", "0"}, {"21?", "250.87?"}, {"0?", "0?"}})
    {
        std::string orbit_row = row("HD99970", "2d", angles.first.c_str(), angles.second.c_str());
        orbit_row.replace(24, 23, "HD99971                ");
        std::ofstream("catalogs/star_orbits.dat") << orbit_row << '\n';
        reader.read_star_orbits_dat(cels);
        const bool complete = !angles.first.empty() && !angles.second.empty();
        EXPECT_EQ(primary->known_poles, complete);
        EXPECT_EQ(companion->known_poles, complete);
        std::ofstream("catalogs/star_orbits.dat") <<
            row("HD99970", "3d", angles.first.c_str(), angles.second.c_str()) << '\n';
        reader.read_star_orbits_dat(cels);
        EXPECT_EQ(primary->known_poles, complete);
    }
}

TEST_F(StellarRotationTest, EstimatedAnglesAndUnitsSurviveCatalogReload)
{
    Star* primary = target(43834);
    Star* companion = target(99971);
    Star* disk = target(99972);
    std::string companion_row = row("HD43834", "2y?", "21?", "250.87?");
    companion_row.replace(24, 23, "HD99971                ");
    companion_row.resize(240, ' ');
    companion_row.replace(100, 7, "11.8AU?");
    std::string disk_row = row("HD99972", "", "60?", "45?");
    disk_row.replace(24, 23, "(debris disk)          ");
    disk_row.resize(120, ' ');
    disk_row.replace(100, 6, "2.5AU?");
    std::ofstream("catalogs/star_orbits.dat") << row("HD43834", "30d?", "21?", "250.87?") << '\n'
        << companion_row << '\n' << disk_row << '\n';
    CatalogReader reader;
    reader.read_star_orbits_dat(cels);
    EXPECT_TRUE(primary->known_poles);
    EXPECT_TRUE(companion->known_poles);
    EXPECT_TRUE(disk->known_poles);
    EXPECT_NEAR(companion->orbit->heliocentric_inclination * fiftyseven, 21, 1e-10);
    EXPECT_NEAR(companion->orbit->heliocentric_node * fiftyseven, 250.87, 1e-10);
    EXPECT_DOUBLE_EQ(companion->orbit->period, 2 * oneyear);
    EXPECT_DOUBLE_EQ(companion->orbit->semimajor_axis, 11.8 * AU);
    EXPECT_DOUBLE_EQ(disk->disk_inner_edge_sma, 2.5 * AU);
    EXPECT_DOUBLE_EQ(primary->sidereal_rotational_period, 30 * oneday);
    EXPECT_TRUE(primary->stellar_rotation.at("estimated"));
    ASSERT_TRUE(reader.bake_condensed_star_orbits());
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    ASSERT_EQ(reader.read_condensed_star_cat(), 4);
    ASSERT_NE(hd[43834], nullptr);
    ASSERT_NE(hd[99971], nullptr);
    ASSERT_NE(hd[99972], nullptr);
    EXPECT_TRUE(hd[43834]->known_poles);
    EXPECT_TRUE(hd[99971]->known_poles);
    EXPECT_TRUE(hd[99972]->known_poles);
    EXPECT_DOUBLE_EQ(hd[99971]->orbit->period, 2 * oneyear);
    EXPECT_DOUBLE_EQ(hd[99971]->orbit->semimajor_axis, 11.8 * AU);
    EXPECT_DOUBLE_EQ(hd[99972]->disk_inner_edge_sma, 2.5 * AU);
}

TEST_F(StellarRotationTest, SscNumericAnnotationsMatchUnmarkedValues)
{
    const std::string path = "annotated.ssc";
    std::ofstream file(path);
    file << "\"Plain\" \"Sol\"\n{\n Radius 6000\n Mass 1\n EllipticalOrbit\n"
        << " { SemiMajorAxis 1 Period 2 Inclination 21 AscendingNode 250.87 }\n}\n"
        << "\"Annotated\" \"Sol\"\n{\n Radius \"6000?\"\n Mass 1?\n EllipticalOrbit\n"
        << " { SemiMajorAxis 1? Period 2? Inclination 21? AscendingNode 250.87? }\n}\n";
    file.close();
    SSCImport importer;
    ASSERT_TRUE(importer.read(path));
    ASSERT_EQ(importer.report.bodies_added, 2);
    Planet* plain = static_cast<Planet*>(cels[1]);
    Planet* annotated = static_cast<Planet*>(cels[2]);
    EXPECT_DOUBLE_EQ(annotated->mass, plain->mass);
    EXPECT_DOUBLE_EQ(annotated->volumetric_mean_radius, plain->volumetric_mean_radius);
    ASSERT_NE(plain->orbit, nullptr);
    ASSERT_NE(annotated->orbit, nullptr);
    EXPECT_DOUBLE_EQ(annotated->orbit->semimajor_axis, plain->orbit->semimajor_axis);
    EXPECT_DOUBLE_EQ(annotated->orbit->period, plain->orbit->period);
    EXPECT_DOUBLE_EQ(annotated->orbit->heliocentric_inclination, plain->orbit->heliocentric_inclination);
    EXPECT_DOUBLE_EQ(annotated->orbit->heliocentric_node, plain->orbit->heliocentric_node);
}

TEST_F(StellarRotationTest, PeriodOnlyRowsAndExplicitZeroPolesSurviveBothSaveFormats)
{
    Star* unknown = target(99990);
    Star* pole_on = target(99991);
    Star* preserved = target(99992);
    preserved->known_poles = preserved->lock_equatorial_plane = true;
    preserved->location.equatorial_plane = system_plane_from_incl_and_node(0.7, 0.2,
        preserved->location.system_center - cels[0]->location.system_center);
    const Rotation original_plane = preserved->location.equatorial_plane;
    std::ofstream file("catalogs/star_orbits.dat");
    file << row("HD99990", "0.123456d?") << '\n'
         << row("HD99991", "2y", "0", "0") << '\n'
         << row("HD99992", "8d") << '\n'
         << row("HD99992", "", "40", "23") << '\n';
    file.close();
    CatalogReader reader;
    reader.read_star_orbits_dat(cels);
    EXPECT_DOUBLE_EQ(unknown->sidereal_rotational_period, 0.123456 * oneday);
    EXPECT_FALSE(unknown->known_poles);
    EXPECT_FALSE(unknown->lock_equatorial_plane);
    EXPECT_EQ(unknown->rot_heliocen_incl, 0);
    const Rotation fallback = system_plane_from_incl_and_node(_pi / 2, 0,
        unknown->location.system_center - cels[0]->location.system_center);
    EXPECT_NEAR(unknown->location.equatorial_plane.a, fallback.a, 1e-12);
    EXPECT_NEAR((unknown->location.equatorial_plane.v - fallback.v).magnitude(), 0, 1e-12);
    EXPECT_TRUE(pole_on->known_poles);
    EXPECT_TRUE(pole_on->lock_equatorial_plane);
    EXPECT_DOUBLE_EQ(pole_on->rot_heliocen_incl, 0);
    EXPECT_DOUBLE_EQ(pole_on->sidereal_rotational_period, 2 * oneyear);
    EXPECT_DOUBLE_EQ(preserved->sidereal_rotational_period, 8 * oneday);
    EXPECT_NE(preserved->location.equatorial_plane.a, original_plane.a);
    std::ofstream("catalogs/star_orbits.dat") << row("HD99992", "9d") << '\n';
    const Rotation measured = preserved->location.equatorial_plane;
    reader.read_star_orbits_dat(cels);
    EXPECT_DOUBLE_EQ(preserved->location.equatorial_plane.a, measured.a);
    EXPECT_DOUBLE_EQ(preserved->sidereal_rotational_period, 9 * oneday);
    Star restored;
    restored.from_json(unknown->to_json());
    EXPECT_EQ(restored.stellar_rotation, unknown->stellar_rotation);
    EXPECT_DOUBLE_EQ(restored.sidereal_rotational_period, unknown->sidereal_rotational_period);
    EXPECT_FALSE(restored.known_poles);
    ASSERT_TRUE(reader.bake_condensed_star_orbits());
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    ASSERT_EQ(reader.read_condensed_star_cat(), 4);
    ASSERT_NE(hd[99990], nullptr);
    EXPECT_DOUBLE_EQ(hd[99990]->sidereal_rotational_period, 0.123456 * oneday);
    EXPECT_TRUE(hd[99990]->stellar_rotation.at("estimated"));
    EXPECT_FALSE(hd[99990]->known_poles);
    EXPECT_FALSE(hd[99990]->lock_equatorial_plane);
    EXPECT_NEAR(hd[99990]->location.equatorial_plane.a, fallback.a, 1e-12);
}

TEST_F(StellarRotationTest, CatalogRejectsConflictingIdentitiesAndAuthoredValuesWin)
{
    Star* first = target(1835);
    first->HIP = 1803;
    Star* second = target(99990);
    second->HIP = 99991;
    // Actual CDS row: HD at 25, HIP at 32, period at 144, reference at 150.
    const std::string actual = "HD 1835                   1835   1803    17.3 BE Cet    00225174-1212340 00 22 51.79 -12 12 34.0    Field  20.   6.39 1.54 G2    B95 29.15 V99  7.78 D96  1.10 1.03  1.11 5687 0.76 -4.43";
    std::ofstream("catalogs/StellarRotation/catalog.dat") << actual << '\n';
    CatalogReader reader;
    first->sidereal_rotational_period = 11 * oneday;
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 0);
    EXPECT_DOUBLE_EQ(first->sidereal_rotational_period, 11 * oneday);
    first->sidereal_rotational_period = 25 * oneday;
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 1);
    EXPECT_DOUBLE_EQ(first->sidereal_rotational_period, 7.78 * oneday);
    EXPECT_FALSE(first->known_poles);
    EXPECT_EQ(first->stellar_rotation.at("reference"), "D96");
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 1);
    EXPECT_EQ(ncelobjs, 3);
    std::string alternate_period = actual;
    alternate_period.replace(143, 5, " 8.00");
    std::ofstream("catalogs/StellarRotation/catalog.dat") << actual << '\n' << alternate_period << '\n';
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 0);
    EXPECT_DOUBLE_EQ(first->sidereal_rotational_period, 7.78 * oneday);
    alternate_period.replace(143, 5, "  nan");
    std::ofstream("catalogs/StellarRotation/catalog.dat") << alternate_period << '\n';
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 0);
    std::string conflict = actual;
    conflict.replace(31, 6, " 99991");
    std::ofstream("catalogs/StellarRotation/catalog.dat") << conflict << '\n';
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 0);
    EXPECT_EQ(ncelobjs, 3);
    EXPECT_DOUBLE_EQ(second->sidereal_rotational_period, 0);
    std::ofstream("catalogs/StellarRotation/catalog.dat") << actual << '\n';
    std::ofstream("catalogs/star_orbits.dat") << row("HD1835", "6d?") << '\n';
    reader.read_star_orbits_dat(cels);
    EXPECT_EQ(reader.read_stellar_rotation_catalog(cels), 0);
    EXPECT_DOUBLE_EQ(first->sidereal_rotational_period, 6 * oneday);
    EXPECT_TRUE(first->stellar_rotation.at("estimated"));
}

TEST_F(CatalogParsingTest, DISABLED_StarOrbitCatalogTiming)
{
    Star::load_main_seq_dat();
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    struct RestoreCaches
    {
        Star **hd, **hip;
        std::vector<Constellation> definitions;
        int count;
        std::map<std::string, Star*> dm;
        ~RestoreCaches()
        {
            hdcache = hd;
            hipcache = hip;
            constellations = std::move(definitions);
            num_reg_cons = count;
            dmcache = std::move(dm);
        }
    } restore{hdcache, hipcache, std::move(constellations), num_reg_cons, std::move(dmcache)};
    constellations.clear();
    num_reg_cons = 0;
    dmcache.clear();
    read_cons_lines();
    hdcache = hd.data();
    hipcache = hip.data();
    delete_the_universe();
    CatalogReader reader;
    ASSERT_GT(reader.read_condensed_star_cat(), 100000);
    const auto start = std::chrono::steady_clock::now();
    const int count = reader.read_star_orbits_dat(cels);
    std::cout << "Applied " << count << " orbit rows to " << ncelobjs << " objects in "
        << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
        << " seconds" << std::endl;
    EXPECT_GT(count, 400);
    // Compare every catalog identifier with the original scan after applying
    // the rows, including absent names and companions added during the import.
    ExactStarSearchIndex index;
    std::set<std::string> queries;
    std::ifstream source("catalogs/star_orbits.dat");
    std::string line;
    while (std::getline(source, line))
    {
        if (line.empty() || line[0] == '#' || line.size() < 47) continue;
        queries.insert(trim(line.substr(0, 23)));
        queries.insert(trim(line.substr(24, 23)));
    }
    for (const auto &query : queries)
    {
        if (query.empty() || query[0] == '(') continue;
        EXPECT_EQ(find_object(query.c_str(), true, 9e29, 1, &index),
            find_object(query.c_str(), true, 9e29, 1)) << query;
    }
    delete_the_universe();
}

TEST(TextureTaskTest, ShutdownWaitsBeyondFiveSecondsAndRejectsNewWork)
{
    wait_for_texture_tasks();
    struct RestoreAbort
    {
        bool value = abort_load.load();
        ~RestoreAbort() { abort_load = value; }
    } restore;
    abort_load = false;
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<bool> finished{false};
    EXPECT_TRUE(spawn_texture_task([gate, &finished]()
    {
        gate.wait();
        finished = true;
    }));
    EXPECT_EQ(texture_loads_pending.load(), 1);
    abort_load = true;
    EXPECT_FALSE(spawn_texture_task([]() {}));
    auto shutdown = std::async(std::launch::async, []() { wait_for_texture_tasks(); });
    const auto status = shutdown.wait_for(std::chrono::milliseconds(5200));
    const bool finished_before_release = finished.load();
    release.set_value();
    shutdown.get();
    EXPECT_EQ(status, std::future_status::timeout);
    EXPECT_FALSE(finished_before_release);
    EXPECT_TRUE(finished.load());
    EXPECT_EQ(texture_loads_pending.load(), 0);
}

TEST(TextureTaskTest, BurstConcurrencyIsBoundedAndAcceptedJobsSurviveCancellation)
{
    wait_for_texture_tasks();
    struct RestoreAbort
    {
        bool value = abort_load.load();
        ~RestoreAbort() { abort_load = value; }
    } restore;
    struct Counters
    {
        std::atomic<int> running{0}, peak{0}, completed{0};
        std::atomic<bool> signaled{false};
    };
    auto counters = std::make_shared<Counters>();
    auto started = std::make_shared<std::promise<void>>();
    auto first = started->get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    abort_load = false;
    for (int index = 0; index < 16; ++index)
    {
        EXPECT_TRUE(spawn_texture_task([counters, started, gate]()
        {
            const int active = ++counters->running;
            int previous = counters->peak.load();
            while (previous < active && !counters->peak.compare_exchange_weak(previous, active)) {}
            if (!counters->signaled.exchange(true)) started->set_value();
            gate.wait();
            --counters->running;
            ++counters->completed;
        }));
    }
    EXPECT_EQ(first.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(texture_loads_pending.load(), 16);
    EXPECT_GE(counters->peak.load(), 1);
    EXPECT_LE(counters->peak.load(), 4);
    abort_load = true;
    EXPECT_FALSE(spawn_texture_task([]() {}));
    release.set_value();
    wait_for_texture_tasks();
    EXPECT_EQ(counters->completed.load(), 16);
    EXPECT_EQ(counters->running.load(), 0);
    EXPECT_EQ(texture_loads_pending.load(), 0);
    std::cout << "Texture peak concurrency: " << counters->peak.load() << " for 16 jobs.\n";
}

TEST(TextureTaskTest, FailedTasksReleaseTheirTicketsAndWorkersContinue)
{
    wait_for_texture_tasks();
    struct RestoreAbort
    {
        bool value = abort_load.load();
        ~RestoreAbort() { abort_load = value; }
    } restore;
    abort_load = false;
    auto completed = std::make_shared<std::atomic<bool>>(false);
    testing::internal::CaptureStderr();
    EXPECT_TRUE(spawn_texture_task([]() { throw std::runtime_error("Synthetic texture failure"); }));
    EXPECT_TRUE(spawn_texture_task([completed]() { *completed = true; }));
    wait_for_texture_tasks();
    const std::string diagnostic = testing::internal::GetCapturedStderr();
    EXPECT_NE(diagnostic.find("Synthetic texture failure"), std::string::npos);
    EXPECT_TRUE(completed->load());
    EXPECT_EQ(texture_loads_pending.load(), 0);
}

TEST_F(CatalogParsingTest, LoadsNamedHDCompanionsAndFullJulianEpochs)
{
    Star::load_main_seq_dat();
    std::vector<Star*> test_cache(MAX_HD + 1, nullptr);
    struct RestoreContext
    {
        std::filesystem::path directory;
        std::filesystem::path temporary;
        Star **cache;

        ~RestoreContext()
        {
            hdcache = cache;
            std::filesystem::current_path(directory);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreContext restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_orbit_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache
    };
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    hdcache = test_cache.data();
    Star *primary = make_star("Orbit Test A");
    primary->HD = 99990;
    hdcache[99990] = primary;
    Star *massive_primary = make_star("V373 Cas");
    massive_primary->HD = 99992;
    hdcache[99992] = massive_primary;
    Star *unmeasured = make_star("Unknown Test C");
    unmeasured->absolute_magnitude = 0;
    unmeasured->make_companion_of(massive_primary, 'C');

    const auto row = [](const char *center, const char *member, const char *period,
                        const char *axis, const char *epoch)
    {
        std::string line(238, ' ');
        line.replace(0, strlen(center), center);
        line.replace(24, strlen(member), member);
        line.replace(48, strlen(period), period);
        line.replace(64, 2, "45");
        line.replace(76, 2, "60");
        line.replace(88, 3, "120");
        line.replace(100, strlen(axis), axis);
        line.replace(112, 3, "0.2");
        line.replace(124, 1, "0");
        line.replace(144, strlen(epoch), epoch);
        line.replace(160, 3, "M0V");
        line.replace(192, 3, "0.6");
        line.replace(204, 3, "0.6");
        line.replace(216, 4, "0.08");
        line.replace(228, 4, "3800");
        return line;
    };
    std::ofstream file("catalogs/star_orbits.dat");
    file << row("HD99990", "HD99991", "2.5d", "0.2AU", "JD2451544.512345") << '\n';
    file << row("HD99991", "Orbit Test C", "12.75y", "0.04s", "2026.123456") << '\n';
    std::string hot = row("HD99990", "Hot Test D", "195.458d", "1.55AU", "JD2449983.23");
    hot.replace(160, 15, 15, ' ');
    hot.replace(192, 3, "9.3");
    hot.replace(204, 3, "5.8");
    hot.replace(216, 11, 11, ' ');
    hot.replace(228, 4, "22750");
    file << hot << '\n';
    std::string massive = hot;
    massive.replace(0, 7, "HD99992");
    massive.replace(24, 23, "V373 Cas B             ");
    massive.replace(192, 11, "14.2       ");
    massive.replace(204, 11, 11, ' ');
    massive.replace(228, 5, "26800");
    file << massive << '\n';
    std::string measured = hot;
    measured.replace(24, 23, "Measured Test E        ");
    measured.replace(176, 15, "0              ");
    file << measured << '\n';
    std::string compact = hot;
    compact.replace(24, 23, "Compact Test F         ");
    compact.replace(160, 15, "WD             ");
    compact.replace(192, 11, "1.22       ");
    compact.replace(204, 11, "0.005145896");
    compact.replace(228, 5, "225000");
    file << compact << '\n';
    std::string radius_only(205, ' ');
    radius_only.replace(0, 7, "HD99992");
    radius_only.replace(24, 14, "Unknown Test C");
    radius_only.replace(160, 4, "B2 V");
    radius_only[204] = '4';
    file << radius_only << '\n';
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 7);
    Star *secondary = hdcache[99991];
    ASSERT_NE(secondary, nullptr);
    ASSERT_NE(secondary->orbit, nullptr);
    EXPECT_EQ(secondary->orbit->center, primary);
    EXPECT_EQ(secondary->HD, 99991u);
    EXPECT_NEAR(secondary->orbit->epoch, 2451544.512345, 1e-8);
    EXPECT_DOUBLE_EQ(secondary->orbit->period, 2.5 * oneday);
    EXPECT_DOUBLE_EQ(secondary->orbit->semimajor_axis, 0.2 * AU);
    const int child_index = find_object("Orbit Test C", true);
    ASSERT_GE(child_index, 0);
    Star *child = static_cast<Star*>(cels[child_index]);
    ASSERT_NE(child->orbit, nullptr);
    EXPECT_EQ(child->orbit->center, secondary);
    EXPECT_DOUBLE_EQ(child->orbit->period, 12.75 * oneyear);
    EXPECT_NEAR(child->orbit->semimajor_axis, 0.04 * (AU / parsec) * secondary->distance, 1e-4);
    EXPECT_NEAR(child->orbit->epoch, J2000 + (2026.123456 - 2000) * (oneyear / oneday), 1e-8);
    EXPECT_EQ(secondary->get_component(), 'B');
    EXPECT_EQ(child->get_component(), 'C');
    const int hot_index = find_object("Hot Test D", true);
    ASSERT_GE(hot_index, 0);
    Star *hot_star = static_cast<Star*>(cels[hot_index]);
    const double luminosity = 5.8 * 5.8 * pow(22750.0 / sun_temp, 4);
    const double expected_magnitude = 4.74 - log(luminosity) * invlogmagnbase
        - Star::bolometric_correction(22750);
    EXPECT_NEAR(hot_star->absolute_magnitude, expected_magnitude, 1e-9);
    EXPECT_DOUBLE_EQ(hot_star->temperature, 22750);
    const int massive_index = find_object("V373 Cas B", true);
    ASSERT_GE(massive_index, 0);
    Star *massive_star = static_cast<Star*>(cels[massive_index]);
    EXPECT_GT(massive_star->absolute_magnitude, -4);
    EXPECT_LT(massive_star->absolute_magnitude, -2);
    const int measured_index = find_object("Measured Test E", true);
    ASSERT_GE(measured_index, 0);
    EXPECT_DOUBLE_EQ(cels[measured_index]->absolute_magnitude, 0);
    const int compact_index = find_object("Compact Test F", true);
    ASSERT_GE(compact_index, 0);
    EXPECT_GT(cels[compact_index]->absolute_magnitude, 8);
    EXPECT_LT(cels[compact_index]->absolute_magnitude, 12);
    EXPECT_DOUBLE_EQ(unmeasured->volumetric_mean_radius, 4 * solar_radius);
    EXPECT_TRUE(std::isfinite(unmeasured->absolute_magnitude));
    EXPECT_GT(unmeasured->absolute_magnitude, -3);
    EXPECT_LT(unmeasured->absolute_magnitude, 0);
    strcpy(secondary->name, "Preferred Companion");
    secondary->has_custom_name = false;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 7);
    EXPECT_STREQ(secondary->name, "Preferred Companion");
    EXPECT_FALSE(secondary->has_custom_name);
    secondary->has_custom_name = true;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 7);
    EXPECT_STREQ(secondary->name, "Preferred Companion");
    EXPECT_TRUE(secondary->has_custom_name);
    EXPECT_NEAR(hot_star->absolute_magnitude, expected_magnitude, 1e-9);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, StarnameEntryPreserves82Eridani)
{
    Star *star = make_star("GJ 139");
    star->HD = 20794;
    star->HIP = 15510;
    strcpy(star->Gliese, "GJ 139");
    strcpy(star->constellation, "Eri");
    strcpy(star->Gouldcons, "Eri");
    star->GouldNo = 82;
    CatalogReader reader;
    EXPECT_GT(reader.read_starname_dat(cels), 0);
    EXPECT_STREQ(star->name, "82 Eridani");
    EXPECT_TRUE(star->has_custom_name);
    star->assign_identifier_name();
    EXPECT_STREQ(star->name, "82 Eridani");
    delete_the_universe();
}

TEST_F(StellarRotationTest, StarNamingIndexTracksNewAliasesAndRetainsAmbiguousMatches)
{
    Star* first = make_star("Original target");
    Star* duplicate_a = make_star("Duplicate A");
    Star* duplicate_b = make_star("Duplicate B");
    duplicate_a->HD = duplicate_b->HD = 101;
    const auto record = [](const std::string& name, const std::string& selector, const std::string& hd)
    {
        std::string line(49, ' ');
        line.replace(0, name.size(), name);
        line.replace(25, hd.size(), hd);
        return line + selector + "\n";
    };
    std::ofstream output("starname.dat");
    output << "# BEGIN IAU WGSN\n"
        << record("First label", "Original target", "")
        << record("Second label", "First label", "")
        << record("Third label", "Second label", "102")
        << record("Ambiguous label", "", "101");
    output.close();
    CatalogReader reader;
    EXPECT_EQ(reader.read_starname_dat(cels), 1);
    EXPECT_STREQ(first->name, "First label");
    EXPECT_EQ(first->HD, 102u);
    EXPECT_EQ(hdcache[102], first);
    EXPECT_NE(std::find(first->catalog_aliases.begin(), first->catalog_aliases.end(), "Third label"),
        first->catalog_aliases.end());
    EXPECT_STREQ(duplicate_a->name, "Duplicate A");
    EXPECT_STREQ(duplicate_b->name, "Duplicate B");
}

TEST_F(CatalogParsingTest, OrbitImportsRejectAbsentPrimariesWithSimilarAliases)
{
    std::ifstream source("catalogs/star_orbits.dat");
    ASSERT_TRUE(source.good());
    std::string line, rows;
    while (std::getline(source, line))
    {
        if (line.compare(0, 4, "AB7 ") == 0 || line.compare(0, 4, "AB8 ") == 0)
        {
            rows += line + '\n';
        }
    }
    ASSERT_FALSE(rows.empty());
    struct RestoreDirectory
    {
        std::filesystem::path original, temporary;
        ~RestoreDirectory()
        {
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_orbit_alias_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
    };
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    std::ofstream("catalogs/star_orbits.dat") << rows;
    Star *nearby = make_star("Epsilon Tauri");
    nearby->local_name = "Ain";
    nearby->distance = 145.8 * light_year;
    const int original_count = ncelobjs;
    CatalogReader reader;
    EXPECT_EQ(reader.read_star_orbits_dat(cels), 0);
    EXPECT_EQ(ncelobjs, original_count);
    EXPECT_EQ(nearby->multisys, nullptr);
    EXPECT_DOUBLE_EQ(nearby->mass, solar_mass);
    // Exact aliases still work, and a similar companion name stays a separate object.
    Star *other = make_star("AC7 B");
    std::string exact_row = rows.substr(0, rows.find('\n') + 1);
    exact_row.replace(0, 23, 23, ' ');
    exact_row.replace(0, 3, "Ain");
    std::ofstream("catalogs/star_orbits.dat") << exact_row;
    EXPECT_EQ(reader.read_star_orbits_dat(cels), 1);
    EXPECT_EQ(ncelobjs, original_count + 2);
    EXPECT_EQ(other->orbit, nullptr);
    EXPECT_DOUBLE_EQ(other->mass, solar_mass);
    const int companion_index = find_object("AB7 B", true, 9e29, 1);
    ASSERT_GE(companion_index, 0);
    Star *companion = static_cast<Star*>(cels[companion_index]);
    ASSERT_NE(companion->orbit, nullptr);
    EXPECT_EQ(companion->orbit->center, nearby);
    EXPECT_NE(companion, other);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, NuOrionisOrbitsRejectDistantNamesAndPreserveBothSystems)
{
    std::ifstream source("catalogs/star_orbits.dat");
    ASSERT_TRUE(source.good());
    std::string line, rows, variable_row;
    while (std::getline(source, line))
    {
        if (line.compare(0, 7, "HD41753") == 0 || line.compare(0, 7, "HD37061") == 0)
        {
            rows += line + '\n';
            if (variable_row.empty() && line.compare(0, 7, "HD37061") == 0) variable_row = line;
        }
    }
    ASSERT_FALSE(variable_row.empty());
    struct RestoreContext
    {
        std::filesystem::path original, temporary;
        Star **cache;
        ~RestoreContext()
        {
            hdcache = cache;
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_nu_orionis_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache
    };
    std::vector<Star*> cache(MAX_HD + 1, nullptr);
    hdcache = cache.data();
    Star *greek = make_star("Nu Orionis");
    greek->HD = 41753;
    greek->right_ascension = 6.1345 * 15 * fiftyseventh;
    greek->declination = 14.77 * fiftyseventh;
    greek->distance = 603.6 * light_year;
    hdcache[41753] = greek;
    greek->set_component('A', greek);
    Star *companion = make_star("Nu Ori B");
    companion->make_companion_of(greek, 'B');
    Star *variable = make_star("HD37061");
    variable->HD = 37061;
    variable->right_ascension = 5.5992 * 15 * fiftyseventh;
    variable->declination = -5.26 * fiftyseventh;
    variable->distance = 1355.7 * light_year;
    hdcache[37061] = variable;
    const double original_declination = greek->declination;
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    std::string conflicting_row = variable_row;
    conflicting_row.replace(24, 23, 23, ' ');
    conflicting_row.replace(24, 8, "Nu Ori B");
    std::ofstream("catalogs/star_orbits.dat") << conflicting_row << '\n';
    CatalogReader reader;
    EXPECT_EQ(reader.read_star_orbits_dat(cels), 0);
    EXPECT_EQ(companion->orbit->center, greek);
    EXPECT_EQ(variable->multisys, nullptr);
    std::ofstream("catalogs/star_orbits.dat") << rows;
    EXPECT_EQ(reader.read_star_orbits_dat(cels), 3);
    EXPECT_NE(greek->multisys, variable->multisys);
    EXPECT_EQ(companion->orbit->center, greek);
    EXPECT_DOUBLE_EQ(greek->declination, original_declination);
    ASSERT_NE(variable->multisys, nullptr);
    ASSERT_NE(variable->multisys->get_member('B'), nullptr);
    ASSERT_NE(variable->multisys->get_member('C'), nullptr);
    EXPECT_EQ(variable->multisys->get_member('B')->orbit->center, variable);
    EXPECT_EQ(variable->multisys->get_member('C')->orbit->center, variable);
    EXPECT_DOUBLE_EQ(variable->declination, -5.26 * fiftyseventh);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, XiUrsaeMajorisCachedCompanionIsReusedAcrossOrbitImports)
{
    std::ifstream source("catalogs/star_orbits.dat");
    ASSERT_TRUE(source.good());
    std::string line, row;
    while (std::getline(source, line))
    {
        if (line.compare(0, 7, "HD98231") == 0 && line.substr(24, 23).find("HD98231 C") == 0)
        {
            row = line;
            break;
        }
    }
    ASSERT_FALSE(row.empty());
    struct RestoreContext
    {
        std::filesystem::path original, temporary;
        Star **cache;
        ~RestoreContext()
        {
            hdcache = cache;
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_xi_companion_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache
    };
    std::vector<Star*> cache(MAX_HD + 1, nullptr);
    hdcache = cache.data();
    Star *primary = make_star("Xi Ursae Majoris");
    primary->HD = 98231;
    hdcache[98231] = primary;
    primary->set_component('A', primary);
    Star *secondary = make_star("Xi Ursae Majoris B");
    secondary->make_companion_of(primary, 'B');
    Star *cached = make_star("HD98231 C");
    cached->orbit = new Orbit();
    cached->orbit->center = primary;
    const int original_count = ncelobjs;
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    std::ofstream("catalogs/star_orbits.dat") << row << '\n';
    CatalogReader reader;
    for (int pass = 0; pass < 2; pass++)
    {
        EXPECT_EQ(reader.read_star_orbits_dat(cels), 1);
        EXPECT_EQ(ncelobjs, original_count);
        EXPECT_EQ(primary->multisys->get_member('C'), cached);
        EXPECT_EQ(primary->multisys->get_member('D'), nullptr);
        EXPECT_EQ(cached->orbit->center, primary);
        EXPECT_STREQ(cached->name, "Xi Ursae Majoris C");
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, PublishedColorTargetsImportWithoutDownloadedAstrometry)
{
    Star::load_main_seq_dat();
    struct RestoreDirectory
    {
        std::filesystem::path original, temporary;
        ~RestoreDirectory()
        {
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreDirectory restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_color_import_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
    };
    std::filesystem::create_directories(restore.temporary / "catalogs/WD");
    std::filesystem::copy_file("catalogs/WD_colors.dat", restore.temporary / "catalogs/WD_colors.dat");
    std::filesystem::current_path(restore.temporary);
    std::ofstream("catalogs/WD/catalog.dat").close();
    std::ofstream("catalogs/WD/names.dat").close();
    CatalogReader reader;
    const int imported = reader.read_WD_catalog(cels, MAX_CELOBJS, false);
    ASSERT_GT(imported, 0);
    const int original_count = ncelobjs;
    std::ifstream input("catalogs/WD_colors.dat");
    std::string line;
    int verified = 0;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#' || line.find("gaia_dr3=") == std::string::npos) continue;
        std::istringstream row(line);
        std::string label, name, token;
        row >> label >> name;
        uint64_t source = 0;
        double distance = 0;
        while (row >> token)
        {
            if (token.compare(0, 9, "gaia_dr3=") == 0) source = std::stoull(token.substr(9));
            if (token.compare(0, 12, "distance_pc=") == 0) distance = std::stod(token.substr(12));
        }
        Star *star = nullptr;
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i]->typeclass() == class_star && static_cast<Star*>(cels[i])->gaia_source_id == source)
            {
                ASSERT_EQ(star, nullptr) << name;
                star = static_cast<Star*>(cels[i]);
            }
        }
        ASSERT_NE(star, nullptr) << name;
        EXPECT_EQ(star->gaia_wd_name, name);
        EXPECT_NEAR(star->distance / parsec, distance, 1e-8);
        EXPECT_TRUE(star->apply_published_wd_color()) << name;
        ++verified;
    }
    EXPECT_EQ(verified, imported);
    EXPECT_EQ(reader.read_WD_catalog(cels, MAX_CELOBJS, false), imported);
    EXPECT_EQ(ncelobjs, original_count);
    struct Writer : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    };
    Writer writer;
    std::map<uint64_t, std::string> original_ids;
    FILE *cache = fopen("catalogs/soles_alienorum.dat", "w");
    ASSERT_NE(cache, nullptr);
    for (int i = 1; cels[i]; ++i)
    {
        Star *star = static_cast<Star*>(cels[i]);
        star->alienorumid = "WD" + std::to_string(i);
        original_ids[star->gaia_source_id] = star->gaia_wd_name;
        EXPECT_TRUE(star->WD.empty());
        writer.write_condensed_star_cat_line(cache, star);
    }
    fclose(cache);
    delete_the_universe();
    make_star("Sol");
    ASSERT_EQ(reader.read_condensed_star_cat(), imported);
    for (int i = 1; cels[i]; ++i)
    {
        Star *star = static_cast<Star*>(cels[i]);
        EXPECT_EQ(original_ids.at(star->gaia_source_id), star->gaia_wd_name);
        EXPECT_TRUE(star->WD.empty());
        EXPECT_TRUE(star->apply_published_wd_color());
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, WhiteDwarfsMergeMeasurementsAndSurviveReload)
{
    // Initialize published identities before switching to the fixture catalog.
    wd_color_data_revision();
    Star::load_main_seq_dat();
    std::vector<Star*> test_hd(MAX_HD + 1, nullptr);
    std::vector<Star*> test_hip(MAX_HIP + 1, nullptr);
    struct RestoreContext
    {
        std::filesystem::path directory, temporary;
        Star **hd, **hip;
        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            std::filesystem::current_path(directory);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreContext restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_wd_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache, hipcache
    };
    std::filesystem::create_directories(restore.temporary / "catalogs" / "WD");
    std::filesystem::current_path(restore.temporary);
    hdcache = test_hd.data();
    hipcache = test_hip.data();
    Star *primary = make_star("Existing primary");
    primary->HD = 99990;
    hdcache[99990] = primary;
    Star *companion = make_star("Existing WD B");
    companion->spectral_type[0] = '\0';
    companion->apparent_magnitude = 12;
    companion->absolute_magnitude = 12;
    companion->temperature = 25000;
    companion->volumetric_mean_radius = 0.009 * solar_radius;
    primary->set_component('A', primary);
    companion->set_component('B', primary);
    companion->orbit->period = 12 * oneyear;
    companion->orbit->semimajor_axis = 3 * AU;
    const double original_ra = companion->right_ascension;
    Star *coordinate_match = make_star("Coordinate identity");
    strcpy(coordinate_match->spectral_type, "DA3");
    coordinate_match->apparent_magnitude = 15.2;
    convert_to_J2000(12 * 15 * fiftyseventh, 10 * fiftyseventh, 1950,
        coordinate_match->right_ascension, coordinate_match->declination, true);
    Star *spectrum_only = make_star("Spectrum only");
    strcpy(spectrum_only->spectral_type, "DQ6");
    spectrum_only->apparent_magnitude = 1e29;
    spectrum_only->absolute_magnitude = INFINITY;
    Star *known_id = make_star("Known WD identity");
    known_id->WD = "WD1700+100";
    strcpy(known_id->spectral_type, "DA3");
    const double known_ra = known_id->right_ascension;
    const auto row = [](const char *id)
    {
        std::string line(250, ' ');
        line.replace(1, strlen(id), id);
        line.replace(12, 2, "10");
        line.replace(15, 2, "00");
        line.replace(18, 2, "00");
        line.replace(21, 1, "+");
        line.replace(22, 2, "10");
        line.replace(25, 4, "00.0");
        line.replace(30, 3, "DA2");
        return line;
    };
    const auto alias = [](const char *name, const char *id)
    {
        std::string line(26, ' ');
        line.replace(0, strlen(name), name);
        line.replace(14, 2, "WD");
        line.replace(16, strlen(id), id);
        return line;
    };
    std::vector<std::string> rows;
    std::string measured = row("1000+100");
    measured.replace(72, 6, "14.000");
    measured.replace(81, 6, " 0.000");
    measured.replace(91, 6, "-0.900");
    measured.replace(152, 3, " 25");
    measured.replace(161, 6, "1.0000");
    measured.replace(169, 5, " 90.0");
    measured.replace(179, 7, "  20.00");
    measured.replace(199, 7, " 0.0500");
    measured.replace(206, 4, " 1.0");
    rows.push_back(measured);
    std::string extra = row("1000+100");
    extra.replace(72, 6, "18.000");
    extra.replace(78, 1, ":");
    extra.replace(199, 7, " 0.0100");
    extra.replace(206, 4, " 9.0");
    rows.push_back(extra);
    std::string known = row("1001+100");
    known.replace(12, 17, 17, ' '); // No position: the exact component identity remains usable.
    known.replace(72, 6, "16.000");
    rows.push_back(known);
    std::string blue = row("1002+100");
    blue.replace(30, 3, "DOZ");
    blue.replace(145, 5, "80.00"); // DO entry stores temperature here, not magnitude.
    blue.replace(72, 6, "15.000");
    blue.replace(79, 2, " B"); // B-band light is not visual photometry.
    blue.replace(199, 7, " 0.0200");
    rows.push_back(blue);
    std::string split = measured;
    split.replace(1, 10, "1000+100.1");
    split.replace(12, 2, "11");
    rows.push_back(split);
    std::string positional = row("1200+100");
    positional.replace(12, 2, "12");
    positional.replace(72, 6, "15.100");
    rows.push_back(positional);
    std::string unidentified_light = row("1300+100");
    unidentified_light.replace(12, 17, 17, ' ');
    rows.push_back(unidentified_light);
    // McCook-Sion WD1328+648 is Gaia WDJ133001.17+643523.69. The older
    // catalog has only SDSS photometry, so Gaia supplies its distance and estimated V.
    std::string gaia_only = row("1328+648");
    gaia_only.replace(30, 3, "DZ:");
    rows.push_back(gaia_only);
    rows.push_back(row("1410+100"));
    rows.push_back(row("1510+100"));
    rows.push_back(row("1610+100"));
    rows.push_back(row("1610+100.1"));
    std::string known_record = row("1700+1000");
    known_record.replace(72, 6, "15.000");
    known_record.replace(199, 7, " 0.0500");
    rows.push_back(known_record);
    std::ofstream source("catalogs/WD/catalog.dat");
    for (const auto &line : rows)
    {
        source << line << '\n';
    }
    source << "malformed\n";
    source.close();
    std::ofstream names("catalogs/WD/names.dat");
    names << alias("HD00099990 B", "1001+1000") << '\n';
    names << alias("HD99990", "1000+100") << '\n'; // System alias must not replace its normal primary.
    names << alias("Spectrum only", "1300+100") << '\n';
    names << alias("1330011+643523", "1328+648") << '\n';
    names << alias("1410010+100000", "1410+100") << '\n';
    names << alias("1510010+100000", "1510+100") << '\n';
    names << alias("1610010+100000", "1610+100") << '\n';
    names << alias("1610010+100000", "1610+100.1") << '\n';
    names.close();
    std::ofstream gaia_file("catalogs/WD/gaia_edr3.tsv");
    gaia_file << "WDJname\tGaiaEDR3\tPlx\te_Plx\tpmRA\tpmDE\tGmag\tBPmag\tRPmag\n";
    gaia_file << "WDJ133001.17+643523.69\t1666074615061406336\t11.5812\t0.7812\t-110.630\t-23.300\t20.6447\t21.2053\t19.8128\n";
    gaia_file << "WDJ141001.00+100000.00\t1001\t-2\t0.1\t0\t0\t15\t15.2\t14.8\n";
    gaia_file << "WDJ141001.01+100000.00\t1002\t2\t1\t0\t0\t15\t15.2\t14.8\n";
    gaia_file << "WDJ151001.00+100000.00\t2001\t10\t0.1\t0\t0\t15\t15.2\t14.8\n";
    gaia_file << "WDJ151001.01+100000.00\t2002\t10\t0.1\t0\t0\t15\t15.2\t14.8\n";
    gaia_file << "WDJ161001.00+100000.00\t3001\t10\t0.1\t0\t0\t15\t15.2\t14.8\n";
    gaia_file.close();
    // This target shares only its Gaia source with the legacy record.
    // It must enrich that record rather than compete with a duplicate.
    std::ofstream colors("catalogs/WD_colors.dat");
    colors << "WD1330+6435 WDJ133001.17+643523.69 #DDFFB8 C4_observed_spectrum "
           << "gaia_dr3=1666074615061406336 distance_pc=" << std::setprecision(16) << 1000 / 11.5812
           << " gaia_g=20.6447 gaia_bp=21.2053 gaia_rp=19.8128 pmra_masyr=-110.630 pmdec_masyr=-23.300\n";
    colors << "WD1610+1000 WDJ161001.00+100000.00 #FFFFFF C4_observed_spectrum "
           << "gaia_dr3=3001 distance_pc=100 gaia_g=15\n";
    colors.close();
    Star *neighbor = make_star("Nearby unrelated WD");
    strcpy(neighbor->spectral_type, "DA");
    neighbor->right_ascension = ((16 + 10.0 / 60 + 1.0 / 3600) * 15 + 30.0 / 3600) * fiftyseventh;
    neighbor->declination = 10 * fiftyseventh;
    neighbor->apparent_magnitude = 15;
    CatalogReader reader;
    ASSERT_EQ(reader.read_WD_catalog(cels, MAX_CELOBJS), 9);
    EXPECT_EQ(find_object("WD1700+100", true), known_id->seqno);
    EXPECT_EQ(find_object("WD1700+1000", true), known_id->seqno);
    EXPECT_DOUBLE_EQ(known_id->right_ascension, known_ra);
    const int gaia_star_index = find_object("WD J1330+6435", true);
    ASSERT_GE(gaia_star_index, 0);
    Star *gaia_star = static_cast<Star*>(cels[gaia_star_index]);
    EXPECT_EQ(gaia_star->WD, "WD1328+648");
    EXPECT_EQ(gaia_star->wd_temperature_source, "assumed");
    EXPECT_EQ(gaia_star->gaia_source_id, 1666074615061406336ULL);
    EXPECT_NEAR(gaia_star->distance / parsec, 1000 / 11.5812, 1e-9);
    EXPECT_DOUBLE_EQ(gaia_star->gaia_g_magnitude, 20.6447);
    EXPECT_TRUE(gaia_star->apparent_magnitude_estimated);
    EXPECT_GT(gaia_star->apparent_magnitude, gaia_star->gaia_g_magnitude);
    EXPECT_EQ(gaia_star->HIP, 0u);
    EXPECT_NEAR(gaia_star->proper_motion_RA * cos(gaia_star->declination) * oneyear * fiftyseven * 3600 * 1000, -110.630, 1e-9);
    const int low_precision_index = find_object("WD1410+100", true);
    ASSERT_GE(low_precision_index, 0);
    Star *low_precision = static_cast<Star*>(cels[low_precision_index]);
    EXPECT_EQ(low_precision->gaia_source_id, 1002u);
    EXPECT_NEAR(low_precision->distance / parsec, 500, 1e-9);
    EXPECT_EQ(find_object("WD1510+100", true), -1);
    EXPECT_EQ(find_object("WD1610+100", true), -1);
    EXPECT_EQ(find_object("WD1610+100.1", true), -1);
    const int pinned_index = find_object("WDJ161001.00+100000.00", true);
    ASSERT_GE(pinned_index, 0);
    EXPECT_EQ(static_cast<Star*>(cels[pinned_index])->gaia_source_id, 3001u);
    EXPECT_NEAR(cels[pinned_index]->distance / parsec, 100, 1e-9);
    EXPECT_EQ(neighbor->gaia_source_id, 0u);
    EXPECT_TRUE(neighbor->WD.empty());
    EXPECT_TRUE(std::isinf(spectrum_only->absolute_magnitude));
    EXPECT_STREQ(primary->spectral_type, "G2V");
    EXPECT_TRUE(primary->WD.empty());
    EXPECT_EQ(companion->WD, "WD1001+100");
    EXPECT_STREQ(companion->spectral_type, "DA2");
    EXPECT_DOUBLE_EQ(companion->apparent_magnitude, 12);
    EXPECT_DOUBLE_EQ(companion->absolute_magnitude, 12);
    EXPECT_DOUBLE_EQ(companion->right_ascension, original_ra);
    EXPECT_DOUBLE_EQ(companion->orbit->semimajor_axis, 3 * AU);
    EXPECT_EQ(coordinate_match->WD, "WD1200+100");
    EXPECT_DOUBLE_EQ(coordinate_match->apparent_magnitude, 15.2);
    const int index = find_object("WD 1000+100", true);
    ASSERT_GE(index, 0);
    Star *added = static_cast<Star*>(cels[index]);
    EXPECT_NEAR(added->distance / parsec, 20, 1e-10);
    EXPECT_EQ(added->wd_temperature_source, "catalog");
    EXPECT_DOUBLE_EQ(added->temperature, 25000);
    EXPECT_DOUBLE_EQ(added->radial_velocity, 20000);
    EXPECT_DOUBLE_EQ(added->BV_color, 0);
    EXPECT_NEAR(added->absolute_magnitude, 14 - 5 * log10(2), 1e-10);
    EXPECT_GT(added->volumetric_mean_radius, 0.001 * solar_radius);
    EXPECT_LT(added->volumetric_mean_radius, 0.1 * solar_radius);
    EXPECT_FALSE(added->is_main_sequence());
    EXPECT_GT(added->proper_motion_RA, 0);
    EXPECT_EQ(find_object("WD1002+100", true), -1);
    EXPECT_GE(find_object("WD1000+100.1", true), 0);
    EXPECT_EQ(find_object("WD1000+100.2", true), -1);
    const int before_count = ncelobjs;
    std::reverse(rows.begin(), rows.end());
    source.open("catalogs/WD/catalog.dat");
    for (const auto &line : rows)
    {
        source << line << '\n';
    }
    source.close();
    EXPECT_EQ(reader.read_WD_catalog(cels, MAX_CELOBJS), 9);
    EXPECT_TRUE(std::isinf(spectrum_only->absolute_magnitude));
    EXPECT_EQ(ncelobjs, before_count);
    EXPECT_DOUBLE_EQ(added->apparent_magnitude, 14);
    EXPECT_NEAR(added->distance / parsec, 20, 1e-10);
    EXPECT_EQ(find_object("wd 1000+100", true), index);
    Planet *planet = make_planet(added, "WD 1000+100 b");
    EXPECT_EQ(find_object(planet->name, false), planet->seqno);
    Star json_copy;
    json_copy.from_json(added->to_json());
    EXPECT_EQ(json_copy.WD, added->WD);
    EXPECT_DOUBLE_EQ(json_copy.volumetric_mean_radius, added->volumetric_mean_radius);
    json_copy.from_json(gaia_star->to_json());
    EXPECT_EQ(json_copy.gaia_source_id, gaia_star->gaia_source_id);
    EXPECT_EQ(json_copy.gaia_wd_name, gaia_star->gaia_wd_name);
    EXPECT_DOUBLE_EQ(json_copy.gaia_g_magnitude, gaia_star->gaia_g_magnitude);
    EXPECT_TRUE(json_copy.apparent_magnitude_estimated);
    struct Writer : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    };
    Writer writer;
    Color model_before;
    std::string model_source;
    ASSERT_TRUE(added->resolve_wd_color(model_before, &model_source));
    EXPECT_NE(model_source.find("approx"), std::string::npos);
    added->alienorumid = "WD fixture";
    const double radius = added->volumetric_mean_radius;
    FILE *cache = fopen("catalogs/soles_alienorum.dat", "w");
    ASSERT_NE(cache, nullptr);
    writer.write_condensed_star_cat_line(cache, added);
    gaia_star->alienorumid = "Gaia fixture";
    const uint64_t color_revision = wd_color_data_revision();
    gaia_star->wd_color_revision = color_revision;
    writer.write_condensed_star_cat_line(cache, gaia_star);
    fclose(cache);
    delete_the_universe();
    make_star("Sol");
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    const int gaia_reload = find_object("WD J1330+6435", true);
    ASSERT_GE(gaia_reload, 0);
    Star *cached_gaia = static_cast<Star*>(cels[gaia_reload]);
    EXPECT_EQ(cached_gaia->gaia_source_id, 1666074615061406336ULL);
    EXPECT_NEAR(cached_gaia->distance / parsec, 1000 / 11.5812, 0.005);
    EXPECT_NEAR(cached_gaia->gaia_g_magnitude, 20.6447, 1e-9);
    EXPECT_TRUE(cached_gaia->apparent_magnitude_estimated);
    EXPECT_EQ(cached_gaia->wd_temperature_source, "assumed");
    EXPECT_EQ(cached_gaia->wd_color_revision, color_revision);
    const int reloaded = find_object("WD1000+100", true);
    ASSERT_GE(reloaded, 0);
    EXPECT_NEAR(cels[reloaded]->volumetric_mean_radius, radius, radius * 0.0001);
    EXPECT_DOUBLE_EQ(static_cast<Star*>(cels[reloaded])->temperature, 25000);
    EXPECT_EQ(static_cast<Star*>(cels[reloaded])->wd_temperature_source, "catalog");
    EXPECT_DOUBLE_EQ(static_cast<Star*>(cels[reloaded])->BV_color, 0);
    Color model_after;
    ASSERT_TRUE(static_cast<Star*>(cels[reloaded])->resolve_wd_color(model_after, &model_source));
    EXPECT_NEAR(model_after.red, model_before.red, 1e-5);
    EXPECT_NEAR(model_after.green, model_before.green, 1e-5);
    EXPECT_NEAR(model_after.blue, model_before.blue, 1e-5);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, MissingCompanionMagnitudesSurviveLoadingAndBrightnessCorrection)
{
    Star::load_main_seq_dat();
    std::vector<Star*> test_hd(MAX_HD + 1, nullptr);
    std::vector<Star*> test_hip(MAX_HIP + 1, nullptr);
    struct RestoreContext
    {
        std::filesystem::path directory;
        std::filesystem::path temporary;
        Star **hd;
        Star **hip;

        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            std::filesystem::current_path(directory);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreContext restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_missing_magnitude_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache,
        hipcache
    };
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    hdcache = test_hd.data();
    hipcache = test_hip.data();
    struct RoundtripReader : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    };
    RoundtripReader reader;
    Star primary;
    strcpy(primary.name, "Catalog primary");
    strcpy(primary.spectral_type, "G2V");
    primary.alienorumid = "Fixture A";
    primary.HD = 99990;
    primary.distance = 20 * parsec;
    primary.apparent_magnitude = 5;
    primary.absolute_magnitude = 5 - 5 * log10(2);
    primary.right_ascension = 1;
    primary.declination = 0.2;
    FILE *file = fopen("catalogs/soles_alienorum.dat", "w");
    ASSERT_NE(file, nullptr);
    reader.write_condensed_star_cat_line(file, &primary);
    const auto write_companion = [&](const char *id, const char *name, unsigned int hd,
                                     double apparent, double absolute)
    {
        Star companion;
        strcpy(companion.name, name);
        strcpy(companion.spectral_type, "M3V");
        companion.alienorumid = id;
        companion.HD = hd;
        companion.distance = 200 * parsec;
        companion.right_ascension = 1;
        companion.declination = 0.2;
        companion.apparent_magnitude = apparent;
        companion.absolute_magnitude = absolute;
        companion.orbit = new Orbit();
        companion.orbit->center = &primary;
        companion.orbit->period = oneyear;
        companion.orbit->semimajor_axis = AU;
        reader.write_condensed_star_cat_line(file, &companion);
    };
    write_companion("Fixture B", "Unknown magnitude B", 99991, 1e29, 12);
    write_companion("Fixture C", "Unknown magnitude C", 99992, 1e29, 15);
    write_companion("Fixture D", "Measured zero D", 99993, 0, 12);
    write_companion("Fixture E", "Unknown photometry E", 99994, 1e29, 1e29);
    write_companion("Fixture F", "Measured absolute zero F", 99995, 1e29, 0);
    write_companion("Fixture G", "Estimated absolute G", 99996, 1e29, 10.5);
    write_companion("Fixture H", "Legacy unknown absolute H", 99997, 1e29, 1e29);
    fclose(file);
    std::vector<std::string> rows;
    std::ifstream input("catalogs/soles_alienorum.dat");
    std::string row;
    while (std::getline(input, row))
    {
        rows.push_back(row);
    }
    input.close();
    ASSERT_EQ(rows.size(), 8u);
    ASSERT_EQ(rows[1].substr(76, 6), "+     ");
    ASSERT_EQ(rows[3].substr(76, 6), "+00.00");
    rows[2].replace(76, 6, "      ");
    ASSERT_EQ(rows[4].substr(237, 6), "+     ");
    ASSERT_EQ(rows[5].substr(237, 6), "+00.00");
    rows[7].replace(237, 6, "      ");
    std::ofstream legacy("catalogs/soles_alienorum.dat");
    for (const auto &record : rows)
    {
        legacy << record << '\n';
    }
    legacy.close();

    // Exercise the real writer, both missing-field formats, the loader, and the correction pass.
    for (int pass = 0; pass < 2; pass++)
    {
        Star *sun = static_cast<Star*>(cels[0]);
        sun->distance = 0;
        sun->update_location(simnow);
        ASSERT_EQ(reader.read_condensed_star_cat(), 8);
        Star *unknown_b = hdcache[99991];
        Star *unknown_c = hdcache[99992];
        Star *measured_zero = hdcache[99993];
        ASSERT_NE(unknown_b, nullptr);
        ASSERT_NE(unknown_c, nullptr);
        ASSERT_NE(measured_zero, nullptr);
        EXPECT_DOUBLE_EQ(unknown_b->apparent_magnitude, 1e29);
        EXPECT_DOUBLE_EQ(unknown_c->apparent_magnitude, 1e29);
        EXPECT_DOUBLE_EQ(measured_zero->apparent_magnitude, 0);
        set_center_objects();
        set_center_objects();
        EXPECT_DOUBLE_EQ(unknown_b->absolute_magnitude, 12);
        EXPECT_DOUBLE_EQ(unknown_c->absolute_magnitude, 15);
        EXPECT_GT(unknown_b->viewer_magnitude(sun->location), 10);
        EXPECT_GT(unknown_c->viewer_magnitude(sun->location), 10);
        EXPECT_NEAR(measured_zero->viewer_magnitude(sun->location), 0, 1e-5);
        for (unsigned int hd : {99994u, 99997u})
        {
            ASSERT_NE(hdcache[hd], nullptr);
            EXPECT_DOUBLE_EQ(hdcache[hd]->absolute_magnitude, 1e29);
            EXPECT_DOUBLE_EQ(hdcache[hd]->apparent_magnitude, 1e29);
            EXPECT_GT(hdcache[hd]->viewer_magnitude(sun->location), 99);
        }
        ASSERT_NE(hdcache[99995], nullptr);
        EXPECT_DOUBLE_EQ(hdcache[99995]->absolute_magnitude, 0);
        ASSERT_NE(hdcache[99996], nullptr);
        EXPECT_DOUBLE_EQ(hdcache[99996]->absolute_magnitude, 10.5);
        if (pass == 0)
        {
            FILE *roundtrip = fopen("catalogs/soles_alienorum.dat", "w");
            ASSERT_NE(roundtrip, nullptr);
            for (unsigned int hd = 99990; hd <= 99997; hd++)
            {
                reader.write_condensed_star_cat_line(roundtrip, hdcache[hd]);
            }
            fclose(roundtrip);
            delete_the_universe();
            std::fill(test_hd.begin(), test_hd.end(), nullptr);
            std::fill(test_hip.begin(), test_hip.end(), nullptr);
            make_star("Sol");
        }
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, TaurusPathUsesXiAndSurvivesRebuildingConstellations)
{
    Star::load_main_seq_dat();
    std::vector<Star*> test_hd(MAX_HD + 1, nullptr);
    std::vector<Star*> test_hip(MAX_HIP + 1, nullptr);
    struct RestoreContext
    {
        std::filesystem::path directory;
        std::filesystem::path temporary;
        Star **hd;
        Star **hip;
        std::vector<Constellation> definitions;
        int count;

        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            constellations = std::move(definitions);
            num_reg_cons = count;
            std::filesystem::current_path(directory);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreContext restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_taurus_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache,
        hipcache,
        std::move(constellations),
        num_reg_cons
    };
    constellations.clear();
    num_reg_cons = 0;
    read_cons_lines();
    std::vector<Constellation> taurus;
    for (const auto &cons : constellations)
    {
        if (cons.abbrev == "Tau" && (cons.vantage_name.empty() || cons.vantage_name == "Sun" || cons.vantage_name == "Sol"))
        {
            taurus.push_back(cons);
        }
    }
    ASSERT_EQ(taurus.size(), 1u);
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::ifstream source("catalogs/soles_alienorum.dat");
    ASSERT_TRUE(source.good());
    std::ofstream subset(restore.temporary / "catalogs/soles_alienorum.dat");
    std::string line;
    int copied = 0;
    while (std::getline(source, line))
    {
        if (line.size() < 119)
        {
            continue;
        }
        const int hd = std::stoi("0" + trim(line.substr(113, 6)));
        if (hd == 25204 || hd == 21754 || hd == 21364 || hd == 21120 || hd == 28100)
        {
            subset << line << '\n';
            copied++;
        }
    }
    subset.close();
    ASSERT_EQ(copied, 5);
    constellations = taurus;
    // Exercise only the reported path; the other Taurus lines reference stars outside this subset.
    constellations[0].lines.clear();
    for (const auto &path : taurus[0].lines)
    {
        if ((path.starnamea == "Lam Tau" && path.starnameb == "5   Tau") ||
            (path.starnamea == "5   Tau" && path.starnameb == "Xi  Tau") ||
            (path.starnamea == "Xi  Tau" && path.starnameb == "Omi Tau"))
        {
            constellations[0].lines.push_back(path);
        }
    }
    ASSERT_EQ(constellations[0].lines.size(), 3u);
    std::filesystem::current_path(restore.temporary);
    hdcache = test_hd.data();
    hipcache = test_hip.data();
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 5);
    Star *xi = hdcache[21364];
    ASSERT_NE(xi, nullptr);
    EXPECT_EQ(constellations[0].Bayer_stars.count(13), 1u);
    EXPECT_EQ(constellations[0].Flamsteed_stars.count(5), 1u);
    EXPECT_EQ(find_object("Xi Tau", true), xi->seqno);
    const auto check_path = [&]()
    {
        cache_cons_lines();
        EXPECT_EQ(constellations[0].lines[0].a, hdcache[25204]);
        EXPECT_EQ(constellations[0].lines[0].b, hdcache[21754]);
        EXPECT_EQ(constellations[0].lines[1].a, hdcache[21754]);
        EXPECT_EQ(constellations[0].lines[1].b, xi);
        EXPECT_EQ(constellations[0].lines[2].a, xi);
        EXPECT_EQ(constellations[0].lines[2].b, hdcache[21120]);
    };
    check_path();
    // Reloading constellation definitions discards the lookup maps without reloading stars.
    constellations[0].Bayer_stars.clear();
    constellations[0].Flamsteed_stars.clear();
    check_path();
    delete_the_universe();
}

TEST_F(CatalogParsingTest, HipparcosCombinedLightPreservesResolvedCompanionMagnitude)
{
    Star::load_main_seq_dat();
    std::ifstream source("catalogs/Hipparcos/hip_main.dat");
    std::string row;
    while (std::getline(source, row))
    {
        if (row.size() >= 396 && trim(row.substr(8, 6)) == "88404")
        {
            break;
        }
    }
    ASSERT_GE(row.size(), 396u);
    ASSERT_EQ(trim(row.substr(8, 6)), "88404");
    std::vector<Star*> test_hd(MAX_HD + 1, nullptr);
    std::vector<Star*> test_hip(MAX_HIP + 1, nullptr);
    struct RestoreContext
    {
        std::filesystem::path directory;
        std::filesystem::path temporary;
        Star **hd;
        Star **hip;
        decltype(dmcache) dm;

        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            dmcache = std::move(dm);
            std::filesystem::current_path(directory);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreContext restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_companion_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache,
        hipcache,
        std::move(dmcache)
    };
    dmcache.clear();
    std::filesystem::create_directories(restore.temporary / "catalogs/Hipparcos");
    std::ofstream file(restore.temporary / "catalogs/Hipparcos/hip_main.dat");
    file << row << '\n';
    file.close();
    std::filesystem::current_path(restore.temporary);
    hdcache = test_hd.data();
    hipcache = test_hip.data();
    Star *primary = make_star("Tau Ophiuchi");
    primary->HD = 164765;
    primary->distance = 56.234 * light_year;
    Star *companion = make_star("Tau Ophiuchi B");
    companion->HD = 164764;
    companion->HR = 6733;
    companion->make_companion_of(primary, 'B');
    companion->apparent_magnitude = 5.94;
    CatalogReader reader;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 1);
    EXPECT_DOUBLE_EQ(companion->apparent_magnitude, 5.94);
    companion->apparent_magnitude = 0;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 1);
    EXPECT_DOUBLE_EQ(companion->apparent_magnitude, 0);
    companion->apparent_magnitude = 1e29;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 1);
    EXPECT_DOUBLE_EQ(companion->apparent_magnitude, 4.77);
    // A multi-component entry can also carry resolved photometry; only H43 marks combined light.
    row[272] = ' ';
    std::ofstream resolved("catalogs/Hipparcos/hip_main.dat");
    resolved << row << '\n';
    resolved.close();
    companion->apparent_magnitude = 5.94;
    ASSERT_EQ(reader.read_Hipparcos_catalog(cels, MAX_CELOBJS), 1);
    EXPECT_DOUBLE_EQ(companion->apparent_magnitude, 4.77);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, CachedTauOphiuchiBMatchesResolvedBSCPhotometry)
{
    std::ifstream bsc("catalogs/BSC/catalog");
    std::string row;
    double resolved_magnitude = 1e29;
    while (std::getline(bsc, row))
    {
        if (row.size() >= 107 && trim(row.substr(25, 6)) == "164764")
        {
            resolved_magnitude = std::stod(row.substr(102, 5));
            break;
        }
    }
    ASSERT_DOUBLE_EQ(resolved_magnitude, 5.94);
    std::ifstream cached("catalogs/soles_alienorum.dat");
    bool found = false;
    double companion_absolute = 1e29;
    double primary_distance = 0;
    while (std::getline(cached, row))
    {
        if (row.size() >= 243 && trim(row.substr(113, 6)) == "164765")
        {
            primary_distance = std::stod(row.substr(223, 12)) * light_year;
        }
        if (row.size() >= 119 && trim(row.substr(113, 6)) == "164764")
        {
            EXPECT_DOUBLE_EQ(std::stod(row.substr(76, 6)), resolved_magnitude);
            companion_absolute = std::stod(row.substr(237, 6));
            found = true;
        }
    }
    EXPECT_TRUE(found);
    ASSERT_GT(primary_distance, 0);
    EXPECT_NEAR(companion_absolute + 5 * log10(primary_distance / (10 * parsec)), resolved_magnitude, 0.011);
}

// Line 1 of catalogs/astorb/astorb.dat: (1) Ceres.
static const char astorb_ceres[] =
    "     1 Ceres              L.H. Wasserman   3.53  0.15 0.72 848.4 G?      0   0   0   0   0"
    "   0 80351 6661 20210327 226.970751  73.737001  80.268790 10.588056 0.07830817   2.7657784"
    "8 20210311 9.5E-03  6.1E-06 20210428 2.1E-02 20211126 2.5E-02 20270106 2.5E-02 20270106*20"
    "210321";

// Line 2 of catalogs/comets/comets.dat: 19P/Borrelly.
static const char comets_borrelly[] =
    " 0001 27/07/2023 19P                  P/Borrelly                     P. Rocher 2459611.5 1"
    "   4178  0.57 10/06/2001-17/05/2023 +5.17845622005350E-0001 +1.13550075064959E+0000 +3.859"
    "26601003796E-0001 -1.52131972629668E-0002 +2.94536456033094E-0003 +1.14388314216908E-0002 "
    "+1.21805833861709E-0009 -3.90572186443250E-0010 -1.90270625793716E-0010 +2.45961232436633E"
    "+0006 +1.30627991735647E+0000 +6.37644740779076E-0001 +3.51916453503980E+0002 +7.424703303"
    "83701E+0001 +2.93047065198311E+0001 10.39 10.00  5.00 12.80  5.00  5.00";

TEST_F(CatalogParsingTest, AsteroidDensityAndOsculationDateUseInternalUnits)
{
    AstorbRow row;
    row.number = 1;
    row.name = "Ceres";
    row.diam = 848.4;
    row.incl = 10.588056;
    row.sma = 2.76577848;
    std::string raw(astorb_ceres);
    ASSERT_TRUE(CatalogReader::load_asteroid(&row, raw.data()));
    EXPECT_DOUBLE_EQ(row.cel->orbit->epoch, 2459300.5);
    EXPECT_NEAR(row.cel->density(), 1.853, 1e-9);
}

TEST_F(CatalogParsingTest, CometBulkDensityUsesGrams)
{
    CometRow row;
    row.code = "19P";
    std::string raw(comets_borrelly);
    ASSERT_TRUE(CatalogReader::load_comet(&row, raw.data()));
    ASSERT_NE(row.cel, nullptr);
    EXPECT_NEAR(row.cel->density(), .6, 1e-9);
}

TEST_F(CatalogParsingTest, LoadsAnAsteroidFromItsAstorbLine)
{
    // read_astorb_catalog() fills these three from the same line before it calls load_asteroid(),
    // which is an undocumented half of the contract: pass a buffer and the diameter, inclination
    // and semimajor axis are expected to be filled in already, because the loader only reads them
    // itself on the path where it goes looking for the line.
    AstorbRow row;
    row.number = 1;
    row.name = "Ceres";
    row.diam = 848.4;
    row.incl = 10.588056;
    row.sma = 2.76577848;

    char buffer[1024];
    strcpy(buffer, astorb_ceres);
    ASSERT_TRUE(CatalogReader::load_asteroid(&row, buffer));
    ASSERT_NE(row.cel, nullptr);

    Planet* p = row.cel;
    EXPECT_STREQ(p->name, "Ceres");
    EXPECT_EQ(p->asteroid_no, 1);
    EXPECT_EQ(p->type, rocky);
    EXPECT_EQ(p->typeclass(), class_planet);

    // Columns 43-47: absolute magnitude 3.53. Compared to float precision, not double: the
    // loader parses it into a float, as AstorbRow does with the diameter, inclination and
    // semimajor axis below -- about seven significant figures, which for a rock a few hundred
    // million kilometres away is a great deal more than is known about it.
    EXPECT_NEAR(p->absolute_magnitude, 3.53, 1e-6);

    // Columns 55-58: B-V 0.72, which is stated here rather than being the 0.71 default.
    EXPECT_NEAR(p->BV_color, 0.72, 1e-9);
    EXPECT_TRUE(p->is_bv_observed);

    // The diameter is a diameter and the radius is half of it, in metres: 848.4 km across.
    EXPECT_NEAR(p->volumetric_mean_radius, 848.4 * 500, 1e-6);
    EXPECT_GT(p->mass, 0);

    // It orbits the Sun, at two and three quarter AU, tilted ten and a half degrees.
    ASSERT_NE(p->orbit, nullptr);
    EXPECT_EQ(p->orbit->center, cels[0]);
    EXPECT_NEAR(p->orbit->semimajor_axis, 2.76577848 * AU, AU * 1e-6);      // float, as above
    EXPECT_NEAR(p->orbit->inclination, 10.588056 * fiftyseventh, 1e-7);
    EXPECT_NEAR(p->orbit->eccentricity, 0.07830817, 1e-9);                  // this one is a double

    // Columns 116-125 and 127-136: the mean anomaly and argument of perihelion, in degrees.
    EXPECT_NEAR(p->orbit->mean_anomaly, 226.970751 * fiftyseventh, 1e-9);
    EXPECT_NEAR(p->orbit->arg_periapsis, 73.737001 * fiftyseventh, 1e-9);
    EXPECT_NEAR(p->orbit->ascending_node, 80.268790 * fiftyseventh, 1e-9);

    // Columns 107-114: the epoch of osculation, 2021 March 27, as a Julian date.
    EXPECT_GT(p->orbit->epoch, 2459000);
    EXPECT_LT(p->orbit->epoch, 2459400);

    // Both loaders append what they build to the universe themselves -- which is worth knowing,
    // since it means a caller must not delete the object it gets back. The fixture owns it now.
    EXPECT_EQ(cels[p->seqno], p);
    EXPECT_EQ(ncelobjs, 2) << "the Sun and the asteroid";
    delete_the_universe();
}

TEST_F(CatalogParsingTest, LoadsACometFromItsCometsLine)
{
    CometRow row;
    row.code = "19P";

    char buffer[1024];
    strcpy(buffer, comets_borrelly);
    ASSERT_TRUE(CatalogReader::load_comet(&row, buffer));
    ASSERT_NE(row.cel, nullptr);

    Comet* c = row.cel;
    EXPECT_EQ(c->designation, "19P");
    EXPECT_EQ(c->typeclass(), class_comet);
    EXPECT_EQ(c->type, icy_tailed);
    EXPECT_NE(std::string(c->name).find("Borrelly"), std::string::npos)
        << "got name: " << c->name;

    ASSERT_NE(c->orbit, nullptr);
    EXPECT_EQ(c->orbit->center, cels[0]);

    // A short-period comet: closed orbit, perihelion inside the asteroid belt, and an eccentricity
    // between a circle and a parabola.
    EXPECT_GT(c->orbit->eccentricity, 0.5);
    EXPECT_LT(c->orbit->eccentricity, 1.0);
    EXPECT_FALSE(c->orbit->is_open());
    EXPECT_GT(c->orbit->periapsis_distance, 0.5 * AU);
    EXPECT_LT(c->orbit->periapsis_distance, 2.0 * AU);
    EXPECT_GT(c->orbit->semimajor_axis, AU);

    // The epoch is the perihelion passage itself, which is what lets the mean anomaly be zero.
    EXPECT_DOUBLE_EQ(c->orbit->mean_anomaly, 0);
    EXPECT_DOUBLE_EQ(c->epoch, c->orbit->T_periapsis);
    EXPECT_GT(c->orbit->T_periapsis, 2400000);

    // The angles are in radians once parsed, and inside the range angles live in.
    EXPECT_GE(c->orbit->inclination, 0);
    EXPECT_LE(c->orbit->inclination, _pi);
    EXPECT_GE(c->orbit->ascending_node, 0);
    EXPECT_LE(c->orbit->ascending_node, _pi*2);

    // The light curve: a real comet has a total-magnitude set, and R1 is the steep exponent that
    // makes it a comet rather than a rock.
    double h, sr, sd;
    c->light_curve_parameters(h, sr, sd);
    EXPECT_GT(sr, 0);
    EXPECT_GT(sd, 0);

    EXPECT_EQ(cels[c->seqno], c) << "load_comet() appends what it builds, as load_asteroid() does";
    delete_the_universe();
}

TEST_F(CatalogParsingTest, RefusesALineItCannotMatch)
{
    // Asked for a comet whose code is not in the line it was handed, with no file to fall back on
    // -- load_comet() only searches when it is given no buffer at all.
    CometRow row;
    row.code = "999P";
    char buffer[1024];
    strcpy(buffer, comets_borrelly);

    // It builds from the line it is given regardless of the code, which is the caller's business:
    // read_comets_catalog() matches the line first and then hands it over.
    ASSERT_TRUE(CatalogReader::load_comet(&row, buffer));
    EXPECT_EQ(row.cel->designation, "19P") << "it parses the line it was handed, not the code asked for";

    delete_the_universe();
}

TEST_F(CatalogParsingTest, FindsTheCatalogDirectories)
{
    // Read-only, against whatever this working tree actually has.
    CatalogReader cr;
    std::vector<std::string> found = cr.find_catalogs("catalogs");
    EXPECT_GT(found.size(), 0u) << "there is a catalogs directory in the working tree";

    // And a directory that is not there is an empty answer, not a crash.
    std::vector<std::string> none = cr.find_catalogs("no_such_directory_as_this_one");
    EXPECT_EQ(none.size(), 0u);
}

TEST_F(CatalogParsingTest, CondensedFieldsAndWhiteDwarfMetadataUseStaticColumns)
{
    struct RestoreContext
    {
        std::filesystem::path original, temporary;
        Star **hd, **hip;
        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_static_columns_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache, hipcache
    };
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    hdcache = hd.data();
    hipcache = hip.data();
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    Star *primary = static_cast<Star*>(cels[0]);
    primary->alienorumid = "primary";
    primary->HD = 98231;
    primary->HIP = 55203;
    primary->HR = 4374;
    primary->SB9 = 1256;
    primary->proper_motion_RA = -1.2345e-7;
    primary->proper_motion_decl = 2.3456e-8;
    primary->radial_velocity = -23000;
    primary->parallax = 1.2345e-7;
    primary->absolute_magnitude = 5.09;
    primary->has_custom_name = true;
    Star *companion = make_star("Metadata companion");
    companion->alienorumid = "companion";
    companion->make_companion_of(primary, 'B');
    companion->orbit->period = 12.3456 * oneday;
    companion->orbit->semimajor_axis = 2.34567 * AU;
    companion->orbit->eccentricity = 0.12345;
    companion->orbit->arg_periapsis = 123.4567 * fiftyseventh;
    companion->orbit->mean_anomaly = 234.5678 * fiftyseventh;
    companion->orbit->epoch = 2461234.5678;
    companion->orbit->heliocentric_inclination = 45.6789 * fiftyseventh;
    companion->orbit->heliocentric_node = 156.7890 * fiftyseventh;
    companion->variability_period = 3.45678 * oneday;
    companion->minmag = -0.12;
    companion->maxmag = 1.23;
    companion->epoch_max_brightness = 2451234.5678;
    companion->is_eclipsing_binary = true;
    strcpy(companion->Bonn_survey, "BD");
    companion->Bonn_survey_declination = -2;
    companion->Bonn_survey_sequential = 5958;
    companion->WD = "WD1008+290";
    companion->add_catalog_alias("LHS2229");
    companion->add_catalog_alias("WD1011+2845");
    companion->gaia_wd_name = "WDJ101102.96+284506.66";
    companion->gaia_source_id = 18446744073709551615ULL;
    companion->gaia_g_magnitude = 20.6447;
    companion->apparent_magnitude_estimated = true;
    companion->wd_color_valid = true;
    companion->wd_color_red = 123;
    companion->wd_color_green = 45;
    companion->wd_color_blue = 255;
    companion->wd_color_source = "observed spectrum fixture";
    companion->wd_temperature_source = "catalog fixture";
    companion->wd_color_revision = 18446744073709551615ULL;
    struct Writer : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    } writer;
    FILE *file = fopen("catalogs/soles_alienorum.dat", "wb");
    ASSERT_NE(file, nullptr);
    writer.write_condensed_star_cat_line(file, primary);
    writer.write_condensed_star_cat_line(file, companion);
    fclose(file);
    std::ifstream input("catalogs/soles_alienorum.dat");
    std::string row;
    ASSERT_TRUE(static_cast<bool>(std::getline(input, row)));
    EXPECT_GT(row.size(), 655u);
    EXPECT_EQ(row.substr(127, 6), "4374  ");
    EXPECT_EQ(row.substr(134, 6), "1256  ");
    ASSERT_TRUE(static_cast<bool>(std::getline(input, row)));
    EXPECT_GT(row.size(), 655u);
    EXPECT_EQ(row.substr(468, 3), "SC2");
    EXPECT_EQ(row.substr(513, 20), "18446744073709551615");
    EXPECT_EQ(row.substr(635, 20), "18446744073709551615");
    delete_the_universe();
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    primary = static_cast<Star*>(cels[0]);
    companion = static_cast<Star*>(cels[1]);
    EXPECT_EQ(primary->HR, 4374u);
    EXPECT_EQ(primary->SB9, 1256u);
    EXPECT_EQ(primary->HIP, 55203u);
    EXPECT_DOUBLE_EQ(primary->proper_motion_RA, -1.2345e-7);
    EXPECT_DOUBLE_EQ(primary->proper_motion_decl, 2.3456e-8);
    EXPECT_DOUBLE_EQ(primary->radial_velocity, -23000);
    EXPECT_DOUBLE_EQ(primary->parallax, 1.2345e-7);
    ASSERT_NE(companion->orbit, nullptr);
    EXPECT_EQ(companion->orbit->center, primary);
    EXPECT_NEAR(companion->orbit->period / oneday, 12.3456, 1e-6);
    EXPECT_NEAR(companion->orbit->semimajor_axis / AU, 2.34567, 1e-6);
    EXPECT_NEAR(companion->orbit->eccentricity, 0.12345, 1e-6);
    EXPECT_NEAR(companion->orbit->arg_periapsis * fiftyseven, 123.4567, 1e-5);
    EXPECT_NEAR(companion->orbit->mean_anomaly * fiftyseven, 234.5678, 1e-5);
    EXPECT_NEAR(companion->orbit->epoch, 2461234.5678, 0.01);
    EXPECT_NEAR(companion->orbit->heliocentric_inclination * fiftyseven, 45.6789, 1e-5);
    EXPECT_NEAR(companion->orbit->heliocentric_node * fiftyseven, 156.7890, 1e-5);
    EXPECT_NEAR(companion->variability_period / oneday, 3.45678, 1e-6);
    EXPECT_DOUBLE_EQ(companion->minmag, -0.12);
    EXPECT_DOUBLE_EQ(companion->maxmag, 1.23);
    EXPECT_NEAR(companion->epoch_max_brightness, 2451234.5678, 1e-4);
    EXPECT_TRUE(companion->is_eclipsing_binary);
    EXPECT_EQ(companion->Bonn_survey_declination, -2);
    EXPECT_EQ(companion->Bonn_survey_sequential, 5958u);
    EXPECT_EQ(companion->WD, "WD1008+290");
    EXPECT_EQ(find_object("LHS 2229", true), companion->seqno);
    EXPECT_EQ(find_object("WD1011+2845", true), companion->seqno);
    EXPECT_EQ(companion->gaia_wd_name, "WDJ101102.96+284506.66");
    EXPECT_EQ(companion->gaia_source_id, 18446744073709551615ULL);
    EXPECT_DOUBLE_EQ(companion->gaia_g_magnitude, 20.6447);
    EXPECT_TRUE(companion->apparent_magnitude_estimated);
    EXPECT_TRUE(companion->wd_color_valid);
    EXPECT_EQ(companion->wd_color_red, 123);
    EXPECT_EQ(companion->wd_color_green, 45);
    EXPECT_EQ(companion->wd_color_blue, 255);
    EXPECT_EQ(companion->wd_color_source, "observed spectrum fixture");
    EXPECT_EQ(companion->wd_temperature_source, "catalog fixture");
    EXPECT_EQ(companion->wd_color_revision, 18446744073709551615ULL);
    file = tmpfile();
    ASSERT_NE(file, nullptr);
    primary->alienorumid.assign(15, 'x');
    EXPECT_THROW(writer.write_condensed_star_cat_line(file, primary), std::runtime_error);
    EXPECT_EQ(ftell(file), 0);
    fclose(file);

    // Older catalogs carry the center identifier but no multiple-system extension.
    // Their companion membership must also be available to the planet-plane fallback.
    input.close();
    std::ifstream baked("catalogs/soles_alienorum.dat");
    std::vector<std::string> legacy_rows;
    while (std::getline(baked, row)) legacy_rows.push_back(row.substr(0, 655));
    baked.close();
    std::ofstream legacy("catalogs/soles_alienorum.dat");
    for (const auto& record : legacy_rows) legacy << record << '\n';
    legacy.close();
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    ASSERT_EQ(reader.read_condensed_star_cat(), 2);
    primary = static_cast<Star*>(cels[0]);
    companion = static_cast<Star*>(cels[1]);
    ASSERT_NE(primary->multisys, nullptr);
    EXPECT_EQ(primary->multisys->get_member('B'), companion);
    ASSERT_NE(companion->orbit, nullptr);
    EXPECT_EQ(companion->orbit->center, primary);
    EXPECT_FALSE(CatalogReader::condensed_star_orbits_loaded);
    delete_the_universe();
}

TEST_F(StellarRotationTest, LuhmanOrbitMatchesExistingCatalogNames)
{
    const std::string primary_name = "WISE J104915.57-531906.1 A";
    const std::string companion_name = "WISE J104915.57-531906.1 B";
    std::ifstream source(original / "catalogs/star_orbits.dat");
    ASSERT_TRUE(source);
    std::string row;
    while (std::getline(source, row))
    {
        if (row.find(primary_name) != std::string::npos && row.find(companion_name) != std::string::npos) break;
    }
    ASSERT_TRUE(source);
    std::ofstream("catalogs/star_orbits.dat") << row << '\n';

    Star* primary = make_star(primary_name.c_str());
    primary->right_ascension = 2.832856649815536;
    primary->declination = -0.9305772685722147;
    primary->distance = 1.996 * parsec;
    primary->update_location(simnow);
    CatalogReader reader;
    EXPECT_EQ(reader.read_star_orbits_dat(cels), 0);
    ASSERT_EQ(ncelobjs, 2);

    Star* companion = make_star(companion_name.c_str());
    companion->right_ascension = primary->right_ascension;
    companion->declination = primary->declination;
    companion->distance = primary->distance;
    companion->mass = .027 * solar_mass;
    companion->volumetric_mean_radius = .10045 * solar_radius;
    companion->temperature = 1261;
    companion->update_location(simnow);
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 1);
    ASSERT_EQ(ncelobjs, 3);
    ASSERT_NE(companion->orbit, nullptr);
    EXPECT_EQ(companion->orbit->center, primary);
    EXPECT_DOUBLE_EQ(companion->mass, .027 * solar_mass);
    EXPECT_DOUBLE_EQ(companion->volumetric_mean_radius, .10045 * solar_radius);
    EXPECT_DOUBLE_EQ(companion->temperature, 1261);
    EXPECT_DOUBLE_EQ(companion->orbit->semimajor_axis, 3.52 * AU);
    EXPECT_DOUBLE_EQ(companion->orbit->period, 26.55 * oneyear);
    EXPECT_DOUBLE_EQ(companion->orbit->eccentricity, .344);
    EXPECT_DOUBLE_EQ(companion->orbit->epoch, 2458141.415);
    EXPECT_EQ(find_object(primary_name.c_str(), true, 9e29, 1), primary->seqno);
    EXPECT_EQ(find_object(companion_name.c_str(), true, 9e29, 1), companion->seqno);
}

TEST_F(CatalogParsingTest, BakedStarOrbitsPreservePlanesAndBlankIdentifierLinks)
{
    struct RestoreContext
    {
        std::filesystem::path original, temporary;
        Star **hd, **hip;
        bool baked;
        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            CatalogReader::condensed_star_orbits_loaded = baked;
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_baked_orbits_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache, hipcache, CatalogReader::condensed_star_orbits_loaded
    };
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    hdcache = hd.data();
    hipcache = hip.data();
    Star::load_main_seq_dat();
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    Star* primary = make_star("Baked primary");
    primary->HD = 99990;
    primary->alienorumid = "Fixture A";
    primary->right_ascension = 1;
    primary->declination = 0.3;
    primary->update_location(simnow);
    hdcache[99990] = primary;
    const auto row = [](const char* host, const char* member, const char* inclination)
    {
        std::string line(240, ' ');
        line.replace(0, strlen(host), host);
        line.replace(24, strlen(member), member);
        line.replace(48, 2, "2y");
        line.replace(64, 2, "43");
        line.replace(76, strlen(inclination), inclination);
        line.replace(88, 2, "12");
        line.replace(100, 3, "2AU");
        line.replace(112, 3, "0.2");
        line.replace(144, 16, "JD2451544.512345 ");
        line.replace(160, 3, "M0V");
        line.replace(176, 2, "10");
        line.replace(192, 3, "0.6");
        line.replace(204, 3, "0.6");
        line.replace(228, 4, "3800");
        return line;
    };
    std::ofstream source("catalogs/star_orbits.dat");
    source << row("HD99990", "HD99991", "60") << '\n';
    source << row("HD99991", "Nested companion", "30") << '\n';
    source << row("HD99990", "(system inclination)", "50") << '\n';
    source << row("HD99990", "(stellar rotation)", "68") << '\n';
    source << row("HD99992", "(system inclination)", "42") << '\n';
    source.close();
    CatalogReader reader;
    CatalogReader::condensed_star_orbits_loaded = false;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 2);
    Star* companion = hdcache[99991];
    ASSERT_NE(companion, nullptr);
    ASSERT_EQ(ncelobjs, 4);
    Star* nested = static_cast<Star*>(cels[3]);
    companion->add_catalog_alias("Baked alias");
    EXPECT_TRUE(companion->alienorumid.empty());
    EXPECT_TRUE(nested->alienorumid.empty());
    const Rotation system_plane = primary->location.local_system_plane;
    const Rotation equatorial_plane = primary->location.equatorial_plane;
    const double rotation = primary->sidereal_rotational_period;
    const double epoch = companion->orbit->epoch;
    Star* unlinked = make_star("Unlinked orbit");
    unlinked->orbit = new Orbit();
    unlinked->orbit->center_name = "Unavailable primary";
    unlinked->orbit->period = 12 * oneyear;
    ASSERT_TRUE(reader.bake_condensed_star_orbits());
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    ASSERT_EQ(reader.read_condensed_star_cat(), 5);
    EXPECT_TRUE(CatalogReader::condensed_star_orbits_loaded);
    unlinked = static_cast<Star*>(cels[4]);
    ASSERT_NE(unlinked->orbit, nullptr);
    EXPECT_EQ(unlinked->orbit->center, nullptr);
    EXPECT_EQ(unlinked->orbit->center_name, "Unavailable primary");
    EXPECT_DOUBLE_EQ(unlinked->orbit->period, 12 * oneyear);
    primary = hdcache[99990];
    companion = hdcache[99991];
    nested = static_cast<Star*>(cels[3]);
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(companion, nullptr);
    ASSERT_NE(companion->orbit, nullptr);
    ASSERT_NE(nested->orbit, nullptr);
    EXPECT_EQ(companion->orbit->center, primary);
    EXPECT_EQ(nested->orbit->center, companion);
    EXPECT_EQ(companion->get_component(), 'B');
    EXPECT_EQ(nested->get_component(), 'C');
    EXPECT_EQ(find_object("Baked alias", true), companion->seqno);
    EXPECT_DOUBLE_EQ(companion->orbit->epoch, epoch);
    EXPECT_DOUBLE_EQ(primary->sidereal_rotational_period, rotation);
    EXPECT_TRUE(primary->has_disk);
    EXPECT_NEAR(primary->disk_heliocen_inclination * fiftyseven, 50, 1e-10);
    EXPECT_NEAR(primary->rot_heliocen_incl * fiftyseven, 68, 1e-10);
    EXPECT_TRUE(primary->known_poles);
    EXPECT_TRUE(primary->lock_system_plane);
    EXPECT_TRUE(primary->lock_equatorial_plane);
    EXPECT_NEAR(primary->location.local_system_plane.a, system_plane.a, 1e-12);
    EXPECT_NEAR((primary->location.local_system_plane.v - system_plane.v).magnitude(), 0, 1e-12);
    EXPECT_NEAR(primary->location.equatorial_plane.a, equatorial_plane.a, 1e-12);
    EXPECT_NEAR((primary->location.equatorial_plane.v - equatorial_plane.v).magnitude(), 0, 1e-12);

    const int first_host = ncelobjs;
    Star* late_host = make_star("Late host");
    late_host->HD = 99992;
    hdcache[99992] = late_host;
    EXPECT_EQ(reader.read_star_orbits_dat(cels, first_host), 0);
    EXPECT_TRUE(late_host->has_disk);
    EXPECT_NEAR(late_host->disk_heliocen_inclination * fiftyseven, 42, 1e-10);
    EXPECT_DOUBLE_EQ(primary->sidereal_rotational_period, rotation);
    EXPECT_EQ(ncelobjs, first_host + 1);
    delete_the_universe();
    std::fill(hd.begin(), hd.end(), nullptr);
    std::fill(hip.begin(), hip.end(), nullptr);
    std::ofstream("catalogs/star_orbits.dat", std::ios::app) << "# Updated source\n";
    ASSERT_EQ(reader.read_condensed_star_cat(), 5);
    EXPECT_FALSE(CatalogReader::condensed_star_orbits_loaded);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, CondensedStarCatalogNameIsStable)
{
    CatalogReader cr;
    std::string name = cr.get_condensed_starcat_name();
    EXPECT_GT(name.size(), 0u);
    EXPECT_EQ(name, cr.get_condensed_starcat_name());
}

class TestCatalogReader : public CatalogReader
{
public:
    using CatalogReader::apply_exoplanet_names;
    using CatalogReader::resolve_or_create_exostar;
    using CatalogReader::add_exoplanet_from_row;
    using CatalogReader::exorow_from_json;
    using CatalogReader::exorow_has_position;
    using CatalogReader::read_exoplanet_host_positions;
    using CatalogReader::apply_exoplanet_host_position;
    using CatalogReader::exorow_read_line;
    using CatalogReader::exorow_write_line;
    using CatalogReader::accept_exoplanet_source;
    using CatalogReader::merge_exoplanet_metallicity;
    using CatalogReader::apply_exoplanet_metallicity;
    using CatalogReader::apply_stellar_abundances;
};

TEST_F(CatalogParsingTest, TransitMidpointPlacesPlanetBetweenObserverAndHost)
{
    Star* host = make_star("Transit host");
    host->right_ascension = .84;
    host->declination = .2;
    host->distance = 10 * parsec;
    host->update_location(simnow);
    ExoRow row;
    row.pl_name = "Transit host b";
    row.hostname = "Transit host";
    row.transiting = true;
    row.pl_orbincl = 90;
    row.pl_orbper = 7.3;
    row.pl_orbeccen = .2;
    row.pl_orblper = 75;
    row.pl_tranmid = 2459001.1234;

    TestCatalogReader reader;
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
    reader.apply_exoplanet_names(planets);
    auto* planet = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
    ASSERT_TRUE(planet->orbit->epoch_is_transit);
    const double epoch = row.pl_tranmid - host->distance / (speed_of_light * oneday);
    EXPECT_NEAR(planet->orbit->epoch, epoch, 1e-9);
    const double date = J2000_TIME_T + (epoch - J2000) * oneday;
    host->update_location(date);
    planet->update_location(date);
    auto displacement = cels[0]->location - host->location;
    Point toward_observer(displacement);
    toward_observer.scale(1);
    Point toward_planet = planet->location.local_position - host->location.local_position;
    toward_planet.scale(1);
    EXPECT_LT((toward_observer - toward_planet).magnitude(), 1e-6);
}

TEST_F(CatalogParsingTest, MetalPoorRvCatalogPlanetsUseChemistryAfterCacheAndInclinationUpdates)
{
    Star::load_main_seq_dat();
    std::ifstream input("catalogs/exoplanets.json");
    ASSERT_TRUE(input.good());
    const json catalog = json::parse(input);
    for (const char* target : {"tau Cet f", "82 Eri d"})
    {
        auto found = std::find_if(catalog.begin(), catalog.end(), [&](const json& row)
        {
            return row.value("pl_name", "") == target;
        });
        ASSERT_NE(found, catalog.end());
        for (bool cached : {false, true})
        {
            if (!cels[0]) make_star("Sun");
            const bool tau = std::string(target) == "tau Cet f";
            Star* host = make_star(tau ? "Tau Ceti" : "82 Eridani");
            host->HD = tau ? 10700 : 20794;
            host->temperature = 0;
            host->BV_color = .72;
            host->absolute_magnitude = tau ? 5.68 : 5.35;
            strcpy(host->spectral_type, "G8V");
            host->has_disk = true;
            host->disk_heliocen_inclination = (tau ? 35 : 50) * fiftyseventh;
            host->metallicity = tau ? -.52 : -.42;
            host->metallicity_ratio = "[Fe/H]";
            host->metallicity_source = "NASA pscomppars";
            host->metallicity_error_upper = .02;
            host->metallicity_error_lower = -.02;
            TestCatalogReader reader;
            reader.apply_stellar_abundances(host);
            ASSERT_TRUE(host->stellar_abundances.is_object());
            bool valid = false;
            ExoRow row = reader.exorow_from_json(*found, &valid);
            ASSERT_TRUE(valid);
            ASSERT_FALSE(row.transiting);
            ASSERT_FALSE(row.radius_observed);
            if (cached)
            {
                FILE* file = std::tmpfile();
                ASSERT_NE(file, nullptr);
                reader.exorow_write_line(file, row);
                rewind(file);
                ExoRow restored;
                ASSERT_TRUE(reader.exorow_read_line(file, restored));
                fclose(file);
                row = restored;
            }
            std::map<int, std::vector<int>> planets;
            unsigned int count = 0;
            ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
            reader.apply_exoplanet_names(planets);
            ASSERT_EQ(count, 1u);
            Planet* body = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
            EXPECT_EQ(body->type, rocky) << target;
            EXPECT_EQ(body->radius_evidence, PlanetMeasurement::estimated);
            EXPECT_FALSE(body->radius_estimate_supplied);
            EXPECT_TRUE(body->classification_evidence["catalog_radius"].is_object());
            EXPECT_NEAR(body->mass / earth_mass, tau ? 6.8618 : 7.5977, .001);
            EXPECT_TRUE(body->classification_evidence["metallicity_applied"].get<bool>());
            EXPECT_TRUE(body->classification_evidence["formation_chemistry"]["applicable"].get<bool>());
            EXPECT_EQ(body->classification_evidence["metallicity_model"], "cool_formation_envelope_prior");
            EXPECT_FALSE(body->classification_evidence["density_observed"].get<bool>());
            EXPECT_FALSE(body->classification_evidence.contains("large_radius_probability"));
            ASSERT_NE(body->atm, nullptr);
            EXPECT_DOUBLE_EQ(body->surface_water_model["basis"]["mass_g"].get<double>(), body->mass);
            const double radius = body->volumetric_mean_radius;
            host->metallicity = 0;
            host->stellar_abundances = nullptr;
            body->classify(false, false, true);
            EXPECT_EQ(body->type, ice_giant);
            EXPECT_GT(body->volumetric_mean_radius, radius);
            delete_the_universe();
        }
    }
}

TEST_F(CatalogParsingTest, RadiusMeasurementProvenanceSurvivesTheDerivedCache)
{
    TestCatalogReader reader;
    for (const char* provenance : {"", "Observed", "M-R relationship"})
    {
        for (bool transit : {false, true})
        {
            bool valid = false;
            ExoRow row = reader.exorow_from_json({{"pl_name", "Example b"}, {"hostname", "Example"},
                {"pl_orbper", 30}, {"pl_rade", "2.04?"}, {"pl_radprov", provenance}, {"tran_flag", transit}}, &valid);
            ASSERT_TRUE(valid);
            const bool observed = transit || std::string(provenance) == "Observed";
            row.st_age = 9.25;
            row.st_ageerr1 = 1.5;
            row.st_ageerr2 = -2;
            row.st_agelim = 0;
            EXPECT_EQ(row.radius_observed, observed);
            EXPECT_DOUBLE_EQ(row.pl_rade, 2.04);
            FILE* file = std::tmpfile();
            ASSERT_NE(file, nullptr);
            reader.exorow_write_line(file, row);
            rewind(file);
            ExoRow restored;
            ASSERT_TRUE(reader.exorow_read_line(file, restored));
            fclose(file);
            EXPECT_EQ(restored.radius_observed, observed);
            EXPECT_DOUBLE_EQ(restored.pl_rade, 2.04);
            EXPECT_DOUBLE_EQ(restored.st_age, 9.25);
            EXPECT_DOUBLE_EQ(restored.st_ageerr1, 1.5);
            EXPECT_DOUBLE_EQ(restored.st_ageerr2, -2);
            EXPECT_DOUBLE_EQ(restored.st_agelim, 0);
        }
    }
}

TEST_F(CatalogParsingTest, RenamedPlanetLettersMergeByOrbitWithoutMixingPhysicalSolutions)
{
    json rows = json::array(
    {
        {{"pl_name", "82 Eri c"}, {"hostname", "82 Eri"}, {"source_catalog", "EU"},
            {"ra", 49.99}, {"dec", -43.06}, {"pl_orbper", 89.68}, {"pl_msinie", 2.98}},
        {{"pl_name", "82 Eri d"}, {"hostname", "82 Eri"}, {"source_catalog", "EU"},
            {"ra", 49.99}, {"dec", -43.06}, {"pl_orbper", 648.2}, {"pl_msinie", 2.98},
            {"pl_msinij", .01831}, {"pl_bmasse", 4.8}},
        {{"pl_name", "GJ 139 f"}, {"hostname", "GJ 139"}, {"source_catalog", "NASA"},
            {"ra", 49.99}, {"dec", -43.06}, {"pl_orbper", 647.6}, {"pl_msinie", 5.82},
            {"pl_msinij", .01831177}, {"pl_bmasse", 5.82}, {"pl_bmassprov", "Msini"}}
    });
    CatalogReader::dedup_planets(rows);
    ASSERT_EQ(rows.size(), 2u);
    for (const auto& row : rows)
    {
        if (row["pl_orbper"].get<double>() < 100) EXPECT_DOUBLE_EQ(row["pl_msinie"], 2.98);
        else
        {
            EXPECT_DOUBLE_EQ(row["pl_orbper"], 647.6);
            EXPECT_NEAR(row["pl_msinie"].get<double>(), 5.82, .001);
            EXPECT_DOUBLE_EQ(row["pl_bmasse"], 5.82);
            ASSERT_EQ(row["source_measurements"].size(), 2u);
        }
    }
    const json once = rows;
    CatalogReader::dedup_planets(rows);
    EXPECT_EQ(rows, once);
}

TEST_F(CatalogParsingTest, PhysicalParameterConsensusRejectsOneOutlierAndPreservesEverySource)
{
    for (const std::string family : {"pl_bmass", "pl_msini", "pl_rad"})
    {
        const double conversion = family == "pl_rad" ? 71492000.0 / 6378100 : 317.8284065946748;
        for (int missing : {-1, 0, 1, 2})
        {
            json nasa = {{"source_catalog", "NASA"}, {family + "e", 2.0}, {family + "j", 2.02 / conversion},
                {family + "eerr1", .2}, {family + "eerr2", -.1}};
            json eu = {{"source_catalog", "EU"}, {family + "e", 20.0}, {family + "j", 1.99 / conversion}};
            if (missing == 0) nasa.erase(family + "e");
            if (missing == 1) nasa.erase(family + "j");
            if (missing == 2) eu.erase(family + "j");
            json rows = json::array({{{"pl_name", "Host b"}, {"hostname", "Host"}, {"pl_orbper", 10},
                {"source_measurements", json::array({nasa, eu})}}});
            CatalogReader::dedup_planets(rows);
            ASSERT_EQ(rows.size(), 1u);
            const json row = rows[0];
            const double expected = missing == -1 ? (2 + 2.02 + 1.99) / 3
                : missing == 0 ? (2.02 + 1.99) / 2 : missing == 1 ? (2 + 1.99) / 2 : (2 + 2.02) / 2;
            EXPECT_NEAR(row[family + "e"].get<double>(), expected, 1e-12);
            EXPECT_NEAR(row[family + "j"].get<double>() * conversion, expected, 1e-12);
            EXPECT_EQ(row["source_measurements"], json::array({nasa, eu}));
            EXPECT_EQ(row["physical_parameter_consensus"][family]["selection"], "majority_mean");
            if (missing != 0) EXPECT_DOUBLE_EQ(row[family + "eerr1"].get<double>(), .2);
            const json first = rows;
            CatalogReader::dedup_planets(rows);
            EXPECT_EQ(rows, first);
            bool valid = false;
            const ExoRow parsed = TestCatalogReader::exorow_from_json(row, &valid);
            ASSERT_TRUE(valid);
            const double actual = family == "pl_rad" ? parsed.pl_rade
                : family == "pl_msini" ? parsed.pl_msinie : parsed.pl_bmasse;
            EXPECT_NEAR(actual, expected, 1e-12);
        }
    }
}

TEST_F(CatalogParsingTest, PhysicalParameterDisagreementsUseExplicitSourcePrecedence)
{
    const double conversion = 317.8284065946748;
    for (int available : {2, 4})
    {
        json nasa = {{"source_catalog", "NASA"}, {"pl_bmasse", 4.0}, {"pl_bmassj", 8.0 / conversion}};
        json eu = {{"source_catalog", "EU"}, {"pl_bmasse", 20.0}, {"pl_bmassj", 16.0 / conversion}};
        if (available == 2)
        {
            nasa.erase("pl_bmassj");
            eu.erase("pl_bmasse");
        }
        json rows = json::array({{{"pl_name", "Host b"}, {"hostname", "Host"}, {"pl_orbper", 10},
            {"source_measurements", json::array({nasa, eu})}}});
        CatalogReader::dedup_planets(rows);
        EXPECT_DOUBLE_EQ(rows[0]["pl_bmasse"].get<double>(), 4);
        EXPECT_EQ(rows[0]["physical_parameter_consensus"]["pl_bmass"]["selection"], "source_precedence");
    }
    make_star("Sun");
    Star* host = make_star("Host");
    ExoRow row;
    row.pl_name = "Host b";
    row.pl_orbper = 10;
    row.pl_bmasse = 10;
    row.pl_radj = 1;
    row.radius_observed = true;
    std::map<int, std::vector<int>> planets;
    unsigned count = 0;
    TestCatalogReader reader;
    ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
    auto* planet = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
    EXPECT_NEAR(planet->volumetric_mean_radius, 71492000, 1e-6);
}

TEST_F(CatalogParsingTest, InclinationReconciliationRefreshesTheFinalMassAndVolatileBudget)
{
    Star::load_main_seq_dat();
    make_star("Sun");
    Star* host = make_star("Anonymous disk host");
    host->has_disk = true;
    host->disk_heliocen_inclination = 50 * fiftyseventh;
    Planet* body = make_planet(host);
    body->mass = body->msini = 3 * earth_mass;
    body->mass_evidence = PlanetMeasurement::minimum;
    body->estimate_radius();
    body->apply_cosmic_shoreline();
    const double previous = body->get_surface_pressure();
    std::vector<Planet*> bodies{body};
    std::vector<double> inclinations{0}, nodes{0};
    CatalogReader::reconcile_exoplanet_inclinations(host, bodies, inclinations, nodes);
    EXPECT_DOUBLE_EQ(body->mass, body->msini / sin(50 * fiftyseventh));
    EXPECT_EQ(body->mass_evidence, PlanetMeasurement::estimated);
    EXPECT_DOUBLE_EQ(body->surface_water_model["basis"]["mass_g"].get<double>(), body->mass);
    EXPECT_NE(body->get_surface_pressure(), previous);
    const json once = body->atm->pressure_model;
    CatalogReader::reconcile_exoplanet_inclinations(host, bodies, inclinations, nodes);
    EXPECT_EQ(body->atm->pressure_model, once);
}

TEST_F(CatalogParsingTest, ExoplanetJsonAcceptsAnnotatedMeasurements)
{
    json source = {{"pl_name", "Estimated b"}, {"hostname", "Estimated"}, {"pl_orbper", "30?"},
        {"st_mass", "1.25?"}, {"st_teff", "5800?"}, {"pl_orbincl", "0?"}};
    bool valid = false;
    const ExoRow row = TestCatalogReader::exorow_from_json(source, &valid);
    ASSERT_TRUE(valid);
    EXPECT_DOUBLE_EQ(row.pl_orbper, 30);
    EXPECT_DOUBLE_EQ(row.st_mass, 1.25);
    EXPECT_DOUBLE_EQ(row.st_teff, 5800);
    EXPECT_DOUBLE_EQ(row.pl_orbincl, 0);
}

class StellarDownloadTest : public CatalogParsingTest
{
protected:
    std::filesystem::path original, temporary;
    void SetUp() override
    {
        CatalogParsingTest::SetUp();
        original = std::filesystem::current_path();
        temporary = std::filesystem::temp_directory_path() /
            ("alienorum_source_download_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporary / "catalogs");
        std::filesystem::current_path(temporary);
        abort_load = false;
        // Source-download fixtures have current empty classification feeds by default.
        std::ofstream("catalogs/exoplanet_rejections_nasa.json") << "[]";
        std::ofstream("catalogs/exoplanet_rejections_eu.json") << "[]";
    }
    void TearDown() override
    {
        abort_load = false;
        delete_the_universe();
        std::filesystem::current_path(original);
        std::filesystem::remove_all(temporary);
        CatalogParsingTest::TearDown();
    }
    class Reader : public CatalogReader
    {
    public:
        std::atomic<int> requests{0};
        bool fail = false;
        bool post_unavailable = false;
    protected:
        bool fetch_stellar_catalog_json(const std::string& url, const std::string& form, json& result) override
        {
            ++requests;
            if (fail) return false;
            if (post_unavailable && !form.empty()) return false;
            if (url.find("exoplanetarchive") != std::string::npos)
                result = json::array({{{"hostname", "Target"}, {"st_met", 0.0}, {"st_metratio", "[Fe/H]"}}});
            else if (url.find("voparis") != std::string::npos)
                result = json::array({{{"hostname", "EU host"}, {"st_met", 0.2}}});
            else if (url.find("data/?") != std::string::npos)
                result = {{"values", json::array({{{"name", "HD 10"}}})}};
            else if (url.find("composition?") != std::string::npos)
            {
                result = json::array();
                for (const char* element : {"Fe", "Mg", "Si", "C", "O"})
                    result.push_back(
                    {
                        {"name", "HD 10"}, {"all_names", {"HD 10", "HIP 100"}}, {"element", element},
                        {"solarnorm", "asplund09"}, {"median_value", 0.1}, {"plusminus", 0.03}
                    });
            }
            else if (url.find("gea.esac") != std::string::npos)
                result = json::array({{{"source_id", uint64_t(1234567890123456789)}, {"original_ext_source_id", "101"}}});
            else if ((url + form).find("137D") != std::string::npos)
                result = json::array({{{"HIP", 101}, {"[Fe/H]", -0.4}, {"q_[Fe/H]", "B"}, {"e_[Fe/H]", 9.99}}});
            else
                result =
                {
                    {"metadata", json::array({{{"name", "GaiaDR3"}}, {{"name", "flagSp"}}, {{"name", "flagSpFit"}},
                        {{"name", "flagRed"}}, {{"name", "SNRpxCCD3"}}, {{"name", "[Fe/H]"}}, {{"name", "f_[Fe/H]"}}})},
                    {"data", json::array({json::array({uint64_t(1234567890123456789), 0, 0, 0, 50, -0.3, 0})})}
                };
            return true;
        }
    };
};

TEST(ExoplanetStatusTest, ParsesQuotedEuClassificationsAndRejectsMalformedExports)
{
    const json rows = parse_eu_planet_status("name,planet_status,alternate_names\r\n"
        "\"Example b\",Retracted,\"Alias b, Other b\"\r\n"
        "Example c,Controversial,\"Quoted \"\"name\"\" c\"\r\n");
    ASSERT_TRUE(rows.is_array());
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0]["alt_target_name"], "Alias b, Other b");
    EXPECT_TRUE(rejected_exoplanet_status(rows[0]));
    EXPECT_FALSE(rejected_exoplanet_status(rows[1]));
    EXPECT_TRUE(parse_eu_planet_status("<html>Service unavailable</html>").is_null());
    EXPECT_TRUE(parse_eu_planet_status("name,planet_status\nb,Retracted,extra\n").is_null());
}

TEST(ExoplanetStatusTest, StatusProjectionPreservesNestedAliasesAndClassifications)
{
    const json catalog = json::array(
    {
        {{"pl_name", "Survey b"}, {"hostname", nullptr}, {"pl_orbper", "12?"},
            {"catalog_aliases", json::array({"Alias b"})},
            {"source_measurements", json::array(
            {
                {{"pl_name", "Original b"}, {"alternate_names", "Alias b, Other b"},
                    {"koi_disposition", "FALSE POSITIVE"}, {"pl_rade", 2.5}}
            })}},
        {{"pl_name", "Survey c"}, {"hostname", "Survey"}, {"planet_status", "Candidate"}}
    });
    std::istringstream input(catalog.dump());
    const json projected = read_exoplanet_status_catalog(input);
    ASSERT_EQ(projected.size(), catalog.size());
    EXPECT_TRUE(projected[0].at("hostname").is_null());
    EXPECT_FALSE(projected[0].contains("pl_orbper"));
    EXPECT_FALSE(projected[0]["source_measurements"][0].contains("pl_rade"));
    ExoplanetRejections full, compact;
    for (const auto& row : catalog) full.add(row);
    for (const auto& row : projected) compact.add(row);
    full.resolve();
    compact.resolve();
    for (const char* name : {"Survey b", "Alias b", "Other b", "Original b", "Survey c"})
    {
        const json row = {{"pl_name", name}};
        EXPECT_EQ(compact.excludes(row), full.excludes(row)) << name;
    }
    std::istringstream malformed("[{\"pl_name\":\"Broken b\",\"pl_orbper\":oops}]");
    EXPECT_TRUE(read_exoplanet_status_catalog(malformed).is_discarded());
}

TEST(ExoplanetStatusTest, NasaRefutationsExcludeOtherSectionsAndExpandGroupedPlanets)
{
    const json rows = parse_nasa_planet_status(
        "<a name=\"not_peered\"></a><li>Unconfirmed b</li>"
        "<a name=\"refuted\"></a><ul><li>Old b (<a href=\"https://ui.adsabs.harvard.edu/paper\">Paper</a>)</li></ul>"
        "<li>Broken b (<a href=\"https://ui.adsabs.harvard.edu/paper\">Paper)</a></li>"
        "<a name=\"false_pos\"></a><ul><li><a href=\"overview\">Group b &amp; c</a> (Paper)</li>"
        "<li><a href=\"overview\">HD 10700 e</a> (a.k.a. tau Cet e) &amp; HD 102365 b (Paper)</li></ul>"
        "<a name=\"reinstated\"></a><li>Restored b</li>");
    ASSERT_TRUE(rows.is_array());
    std::set<std::string> names;
    for (const auto& row : rows) names.insert(row.at("pl_name"));
    EXPECT_EQ(names, (std::set<std::string>{"Old b", "Broken b", "Group b", "Group c", "HD 10700 e", "tau Cet e", "HD 102365 b"}));
    EXPECT_TRUE(parse_nasa_planet_status("<html>Maintenance</html>").is_null());
}

TEST(ExoplanetStatusTest, RejectionTraversesAliasesAcrossBothSourcesWithoutRejectingOtherPlanets)
{
    for (const char* provider : {"NASA", "EU"})
    {
        ExoplanetRejections rejected;
        rejected.add({{"pl_name", "Survey b"}, {"planet_status", " reTRACTED "}, {"source_catalog", provider}});
        rejected.add({{"pl_name", "Survey b"}, {"alt_target_name", "Alias b"}});
        rejected.add({{"pl_name", "Other b"}, {"alt_target_name", "Alias b"}});
        rejected.resolve();
        EXPECT_TRUE(rejected.excludes({{"pl_name", "Other-b"}, {"planet_status", "Confirmed"}}));
        EXPECT_FALSE(rejected.excludes({{"pl_name", "Survey c"}, {"planet_status", "Candidate"}}));
        EXPECT_FALSE(rejected.excludes({{"pl_name", "Survey d"}, {"planet_status", "Controversial"}}));
    }
    for (const char* status : {"False Positive", "FALSE_POSITIVE", "False Positive Planet", "FPP", "FP", "Refuted"})
        EXPECT_TRUE(rejected_exoplanet_status({{"planet_status", status}}));
    EXPECT_FALSE(rejected_exoplanet_status({{"planet_status", nullptr}}));
    EXPECT_EQ(exoplanet_status_key("42 Dra Ab"), exoplanet_status_key("42 Dra b"));
    EXPECT_EQ(exoplanet_status_key("42 Dra A b"), exoplanet_status_key("42 Dra b"));
    EXPECT_NE(exoplanet_status_key("42 Dra B b"), exoplanet_status_key("42 Dra b"));
}

TEST(ExoplanetStatusTest, ReusedLetterKeepsAConfirmedOrbitAndRejectsTheOtherSignal)
{
    const auto orbit = [](const char* name, const char* aliases, double period)
    {
        return json{{"pl_name", name}, {"alt_target_name", aliases}, {"pl_orbper", period},
            {"ra", 50.0}, {"dec", -43.0}, {"sy_dist", 6.0}};
    };
    const auto published = orbit("Survey c", "HD 20794 c", 89.68);
    auto confirmed = orbit("HD 20794 d", "", 89.69);
    confirmed["ra"] = 50.0164;
    confirmed["dec"] = -42.997;
    const auto false_positive = orbit("Survey legacy c", "HD 20794 c", 40.11);
    ExoplanetRejections rejections;
    rejections.add({{"pl_name", "HD 20794 c"}, {"planet_status", "False Positive Planet"}}, "NASA");
    rejections.add(published, "EU");
    rejections.add(false_positive, "EU");
    rejections.resolve();
    EXPECT_TRUE(rejections.excludes(published));
    rejections.add(confirmed, "NASA");
    rejections.resolve();
    EXPECT_FALSE(rejections.excludes(published));
    EXPECT_FALSE(rejections.excludes(confirmed));
    EXPECT_TRUE(rejections.excludes(false_positive));
    EXPECT_TRUE(rejections.excludes({{"pl_name", "HD 20794 c"}}));
}

TEST_F(StellarDownloadTest, ProviderRowsRecoverThe82EridaniOrbits)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    ASSERT_FALSE(std::filesystem::exists("exoname.dat"));
    radio_silence = false;
    class ProviderReader : public CatalogReader
    {
    protected:
        bool fetch_stellar_catalog_json(const std::string& url, const std::string&, json& rows) override
        {
            const bool nasa = url.find("exoplanetarchive") != std::string::npos;
            if (!nasa && url.find("voparis") == std::string::npos) return false;
            // The two source snapshots use different letters and rounded positions.
            json host = nasa
                ? json{{"hostname", "HD 20794"}, {"hd_name", "HD 20794"}, {"hip_name", "HIP 15510"},
                    {"ra", 49.9997666}, {"dec", -43.0666533}, {"sy_dist", 6.00278}, {"sy_vmag", 4.26},
                    {"st_lum", -.16311}, {"st_mass", .79}, {"st_rad", .93}, {"st_teff", 5368}, {"st_spectype", "G6 V"}}
                : json{{"hostname", "82 Eri"}, {"ra", 49.9833333}, {"dec", -43.0697222}, {"sy_dist", 6.06},
                    {"sy_vmag", 4.26}, {"st_mass", .813}, {"st_rad", .9}, {"st_teff", 5490}, {"st_spectype", "G8V"}};
            rows = json::array();
            const auto orbit = [&](const char* name, const char* aliases, double period, double axis,
                double eccentricity, double minimum_mass)
            {
                json row = host;
                row.update({{"pl_name", name}, {"alt_target_name", aliases}, {"pl_orbper", period},
                    {"pl_orbsmax", axis}, {"pl_orbeccen", eccentricity}, {"pl_msinie", minimum_mass}});
                rows.push_back(std::move(row));
            };
            if (nasa)
            {
                orbit("HD 20794 b", "", 18.314, .1257, .064, 2.15);
                orbit("HD 20794 d", "", 89.68, .3625, .077, 2.98);
                orbit("HD 20794 e", "", 147.02, .509, .29, 4.77);
                orbit("HD 20794 f", "", 647.6, 1.3541, .45, 5.82);
            }
            else
            {
                orbit("82 Eri b", "e Eri b#HD 20794 b", 18.313, .1257, .057, 2.149);
                orbit("82 Eri c", "HD 20794 c#e Eri c", 89.68, .3625, .077, 2.98);
                orbit("82 Eri d", "e Eri d#HD 20794 d", 648.2, 1.354, .47, 5.81);
                orbit("82 Eri e", "e Eri e#HD 20794 e", 147.02, .509, .29, 4.77);
            }
            return true;
        }
    };
    std::ofstream("catalogs/exoplanet_rejections_nasa.json") << json::array({
        {{"pl_name", "HD 20794 c"}, {"planet_status", "False Positive Planet"}, {"source_catalog", "NASA"}}
    });
    ProviderReader reader;
    reader.load_exoplanets_from_tap(true);
    ASSERT_EQ(reader.load_exoplanets_from_tap(false), 4u);

    const double periods[] = {18.314, 89.68, 647.6};
    std::ifstream input("catalogs/exoplanets.json");
    const auto merged = json::parse(input, nullptr, false);
    ASSERT_TRUE(merged.is_array());
    for (double period : periods)
    {
        const auto row = std::find_if(merged.begin(), merged.end(), [period](const auto& entry)
        {
            return std::abs(entry.value("pl_orbper", 0.0) - period) < .001;
        });
        ASSERT_NE(row, merged.end()) << period;
        std::set<std::string> providers;
        for (const auto& source : row->at("source_measurements")) providers.insert(source.at("source_catalog"));
        EXPECT_EQ(providers, (std::set<std::string>{"EU", "NASA"})) << period;
    }

    const auto check_orbits = [&]()
    {
        const CelestialObject* host = nullptr;
        std::array<const Planet*, 3> planets{};
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i]->deleted || cels[i]->typeclass() != class_planet) continue;
            auto* planet = static_cast<Planet*>(cels[i]);
            ASSERT_NE(planet->orbit, nullptr);
            const double period = planet->orbit->period / oneday;
            EXPECT_GT(std::abs(period - 40.114), 4.0);
            for (size_t j = 0; j < planets.size(); ++j)
            {
                if (std::abs(period - periods[j]) >= .001) continue;
                ASSERT_EQ(planets[j], nullptr) << period;
                planets[j] = planet;
                if (!host) host = planet->orbit->center;
                ASSERT_NE(host, nullptr);
                EXPECT_EQ(planet->orbit->center, host);
                EXPECT_FALSE(planet->catalog_candidate);
                if (j == planets.size() - 1)
                {
                    EXPECT_NEAR(planet->orbit->semimajor_axis / AU, 1.3541, .0001);
                    EXPECT_NEAR(planet->orbit->eccentricity, .45, .001);
                    EXPECT_TRUE(planet->is_in_con_HZ());
                }
            }
        }
        for (size_t j = 0; j < planets.size(); ++j) EXPECT_NE(planets[j], nullptr) << periods[j];
    };
    check_orbits();
    delete_the_universe();
    make_star("Sol");
    radio_silence = true;
    CatalogReader cached;
    cached.load_exoplanets_from_tap(true);
    ASSERT_EQ(cached.load_exoplanets_from_tap(false), 4u);
    check_orbits();
}

TEST_F(StellarDownloadTest, CandidateExportRetainsMeasuredOrbitsAndRespectsRejections)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = false;
    class ProviderReader : public CatalogReader
    {
    protected:
        bool fetch_stellar_catalog_json(const std::string& url, const std::string&, json& rows) override
        {
            if (url.find("/catalog/csv/") == std::string::npos) return false;
            rows = parse_eu_planet_status("name,planet_status,star_name,alternate_names,orbital_period,ra,dec,star_distance\n"
                "Survey host b,Candidate,Survey host,,1928,217,-62,1.295\n"
                "Survey host c,Candidate,Survey host,Alternate c,22,217,-62,1.295\n"
                "Survey host d,Retracted,Survey host,,35,217,-62,1.295\n");
            return rows.is_array();
        }
    };
    std::filesystem::remove("catalogs/exoplanet_rejections_eu.json");
    std::ofstream("catalogs/exoplanet_rejections_nasa.json") << parse_nasa_planet_status(
        "<a name=\"refuted\"></a><ul><li>Alternate c</li></ul><a name=\"false_pos\"></a>"
        "<a name=\"reinstated\"></a><li>Survey host b</li>");
    auto* host = make_star("Survey host");
    ProviderReader reader;
    ASSERT_EQ(reader.load_exoplanets_from_tap(false), 1u);
    ASSERT_EQ(host->pl_indices.size(), 1u);
    const auto* planet = static_cast<const Planet*>(cels[host->pl_indices.front()]);
    EXPECT_EQ(planet->catalog_designation, "Survey host b");
    EXPECT_TRUE(planet->catalog_candidate);
    EXPECT_NEAR(planet->orbit->period / oneday, 1928, 1e-9);
}

TEST_F(StellarDownloadTest, LegacyCsvRespectsRejectionsThroughProviderAliases)
{
    std::ofstream("catalogs/exoplanet_rejections_nasa.json") << json::array({
        {{"pl_name", "Rejection alias c"}, {"planet_status", "False Positive Planet"}}
    });
    std::ofstream("catalogs/exoplanets_eu.json") << json::array({
        {{"pl_name", "Legacy host c"}, {"alt_target_name", "Other designation c#Rejection alias c"}}
    });
    std::ofstream file("catalogs/PSCompPars_fixture.csv");
    const std::vector<std::string> columns = {"pl_name", "hostname", "hd_name", "pl_orbper", "pl_orbsmax",
        "pl_rade", "pl_bmasse", "pl_orbeccen", "pl_orbincl", "pl_orbtper", "pl_orblper", "pl_trueobliq",
        "st_spectype", "st_rad", "st_mass", "st_teff", "ra", "dec", "sy_dist", "sy_vmag"};
    for (const auto& column : columns) file << "# COLUMN " << column << ": fixture\n";
    for (const auto& column : columns) file << column << ',';
    file << '\n';
    for (const char* name : {"Legacy host c", "Legacy host b"})
        file << name << ",Legacy host,HD 0,10,0.1,1,1,0,5,2451545,0,0,G2V,1,1,5772,15,10,10,8\n";
    file.close();
    auto* host = make_star("Legacy host");
    host->cenobj = host;
    const bool saved_have_exo = have_exo;
    CatalogReader reader;
    const int loaded = reader.read_exoplanets_catalog(cels, MAX_CELOBJS);
    have_exo = saved_have_exo;
    EXPECT_EQ(loaded, 1);
    EXPECT_GE(find_object("Legacy host b", false, 9e29, 0), 0);
    EXPECT_LT(find_object("Legacy host c", false, 9e29, 0), 0);
}

TEST_F(StellarDownloadTest, RejectionsFromEitherProviderApplyToJsonAndDerivedCaches)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = true;
    for (bool nasa : {false, true})
    {
        for (bool derived : {false, true})
        {
            SCOPED_TRACE(nasa);
            SCOPED_TRACE(derived);
            delete_the_universe();
            make_star("Sol");
            Star* host = make_star("Status host");
            const json rows = json::array({
                {{"pl_name", "Status host b"}, {"hostname", "Status host"}, {"alt_target_name", "Rejected alias b"},
                    {"pl_orbper", 10}, {"ra", 10}, {"dec", 20}, {"sy_dist", 10}, {"source_catalog", nasa ? "EU" : "NASA"}},
                {{"pl_name", "Status host c"}, {"hostname", "Status host"}, {"pl_orbper", 20},
                    {"ra", 10}, {"dec", 20}, {"sy_dist", 10}, {"planet_status", "Candidate"}}
            });
            std::ofstream("catalogs/exoplanets.json") << rows;
            for (const char* path : {"catalogs/exoplanets_stars.dat", "catalogs/exoplanets_planets.dat"})
                std::filesystem::remove(path);
            const json veto = json::array({{{"pl_name", "Rejected alias b"}, {"planet_status", "Retracted"}}});
            std::ofstream("catalogs/exoplanet_rejections_nasa.json") << (nasa ? veto : json::array());
            std::ofstream("catalogs/exoplanet_rejections_eu.json") << (nasa ? json::array() : veto);
            if (derived)
            {
                FILE* file = fopen("catalogs/exoplanets_planets.dat", "wb");
                ASSERT_NE(file, nullptr);
                fputs("# EXO_TRANSIT7\n", file);
                for (const auto& source : rows)
                {
                    bool valid;
                    const auto row = TestCatalogReader::exorow_from_json(source, &valid);
                    ASSERT_TRUE(valid);
                    TestCatalogReader::exorow_write_line(file, row);
                }
                fclose(file);
            }
            CatalogReader reader;
            EXPECT_EQ(reader.load_exoplanets_from_tap(false), 1u);
            ASSERT_EQ(host->pl_indices.size(), 1u);
            EXPECT_STREQ(cels[host->pl_indices.front()]->name, "Status host c");
        }
    }
}

TEST_F(StellarDownloadTest, StatusRefreshRetainsFailedProviderAndVetoesFreshPlanetRows)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = false;
    class StatusReader : public CatalogReader
    {
    protected:
        bool fetch_stellar_catalog_json(const std::string& url, const std::string&, json& result) override
        {
            if (url.find("removed_targets.html") != std::string::npos)
            {
                result = json::array({{{"pl_name", "NASA rejected b"}, {"planet_status", "False Positive Planet"}}});
                return true;
            }
            if (url.find("/catalog/csv/") != std::string::npos) return false;
            const bool nasa = url.find("exoplanetarchive") != std::string::npos;
            result = json::array({
                {{"pl_name", nasa ? "Status host b" : "Status host d"}, {"hostname", "Status host"},
                    {"alt_target_name", nasa ? "EU rejected b" : "NASA rejected b"},
                    {"pl_orbper", nasa ? 10 : 40}, {"ra", 10}, {"dec", 20}, {"sy_dist", 10}},
                {{"pl_name", nasa ? "Status host c" : "Status host e"}, {"hostname", "Status host"},
                    {"pl_orbper", nasa ? 20 : 50}, {"ra", 10}, {"dec", 20}, {"sy_dist", 10},
                    {"planet_status", "Controversial"}}
            });
            return true;
        }
    };
    std::filesystem::remove("catalogs/exoplanet_rejections_nasa.json");
    const json old_eu = json::array({{{"pl_name", "EU rejected b"}, {"planet_status", "Retracted"}}});
    const char* path = "catalogs/exoplanet_rejections_eu.json";
    std::ofstream(path) << old_eu;
    const auto week_old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(168);
    std::filesystem::last_write_time(path, week_old);
    StatusReader reader;
    reader.load_exoplanets_from_tap(true);
    json persisted, statuses;
    std::ifstream("catalogs/exoplanets.json") >> persisted;
    ASSERT_EQ(persisted.size(), 2u);
    std::set<std::string> names;
    for (const auto& row : persisted) names.insert(row.at("pl_name"));
    EXPECT_EQ(names, (std::set<std::string>{"Status host c", "Status host e"}));
    std::ifstream(path) >> statuses;
    EXPECT_EQ(statuses, old_eu);
    EXPECT_EQ(std::filesystem::last_write_time(path), week_old);
}

TEST_F(StellarDownloadTest, DownloadsMissingSourcesAndAppliesThemBeforeCondensation)
{
    Star* target = make_star("Target");
    target->HD = 10;
    target->HIP = 100;
    Star* eu = make_star("EU host");
    Star* compiled = make_star("Compiled");
    compiled->HIP = 101;
    Reader reader;
    reader.prepare_stellar_chemistry();
    EXPECT_EQ(reader.requests, 8);
    EXPECT_DOUBLE_EQ(target->metallicity, 0);
    EXPECT_DOUBLE_EQ(eu->metallicity, 0.2);
    EXPECT_DOUBLE_EQ(compiled->metallicity, -0.4);
    EXPECT_TRUE(std::isnan(compiled->metallicity_error_upper));
    ASSERT_TRUE(target->stellar_abundances.contains("Hypatia"));
    ASSERT_TRUE(compiled->stellar_abundances.contains("GALAH DR4"));
    for (const char* path :
        {"NASA/stellar_metallicity.json", "ExoplanetEU/stellar_metallicity.json", "XHIP/stellar_metallicity.json",
         "Hypatia/stellar_abundances.json", "GALAH/stellar_abundances.json", "GaiaDR3/hipparcos_crossmatch.json"})
        EXPECT_TRUE(std::filesystem::exists(std::filesystem::path("catalogs") / path));
    reader.prepare_stellar_chemistry();
    EXPECT_EQ(reader.requests, 8);
}

TEST_F(StellarDownloadTest, EitherCondensedFormSuppressesAllSourceDownloads)
{
    for (const char* path : {"catalogs/soles_alienorum.dat", "catalogs/soles_alienorum.dat.gz"})
    {
        std::ofstream(path) << "fixture";
        Reader reader;
        reader.prepare_stellar_chemistry();
        EXPECT_EQ(reader.requests, 0);
        std::filesystem::remove(path);
    }
}

TEST_F(StellarDownloadTest, TapQueriesCanFallBackToGet)
{
    Star* target = make_star("Target");
    target->HD = 10;
    target->HIP = 100;
    Star* compiled = make_star("Compiled");
    compiled->HIP = 101;
    Reader reader;
    reader.post_unavailable = true;
    reader.prepare_stellar_chemistry();
    EXPECT_EQ(reader.requests, 13);
    EXPECT_DOUBLE_EQ(target->metallicity, 0);
    EXPECT_DOUBLE_EQ(compiled->metallicity, -0.4);
    EXPECT_TRUE(target->stellar_abundances.contains("Hypatia"));
    EXPECT_TRUE(compiled->stellar_abundances.contains("GALAH DR4"));
}

TEST_F(StellarDownloadTest, FailureAndCancellationDoNotPublishPartialSnapshots)
{
    std::filesystem::remove("catalogs/exoplanet_rejections_nasa.json");
    std::filesystem::remove("catalogs/exoplanet_rejections_eu.json");
    Reader reader;
    reader.fail = true;
    reader.prepare_stellar_chemistry();
    EXPECT_GT(reader.requests, 0);
    EXPECT_TRUE(std::filesystem::is_empty("catalogs"));
    const int requests = reader.requests;
    abort_load = true;
    reader.prepare_stellar_chemistry();
    EXPECT_EQ(reader.requests, requests);
}

TEST_F(CatalogParsingTest, AbundanceMatchingRejectsUnresolvedBinaryIdentifiers)
{
    struct Directory
    {
        std::filesystem::path original = std::filesystem::current_path();
        std::filesystem::path temporary = std::filesystem::temp_directory_path() /
            ("alienorum_abundance_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Directory()
        {
            std::filesystem::current_path(original);
            std::filesystem::remove_all(temporary);
        }
    } directory;
    std::filesystem::create_directories(directory.temporary / "catalogs/Hypatia");
    std::filesystem::current_path(directory.temporary);
    const json table =
    {
        {"schema", 1}, {"source", "Hypatia"}, {"solar_scale", "asplund09"},
        {"solar", {{"Fe", 7.5}}}, {"reference", "fixture"},
        {"rows", json::array(
        {
            {{"name", "Component A"}, {"hd", 12}, {"elements", {{"Fe",
                {{"dex", 0.1}, {"error", 0.05}, {"ratio", "[Fe/H]"}, {"quality", "accepted"}}}}}},
            {{"name", "Unique"}, {"hd", 13}, {"elements", {{"Fe",
                {{"dex", -0.2}, {"error", 0.03}, {"ratio", "[Fe/H]"}, {"quality", "accepted"}}}}}}
        })}
    };
    std::ofstream("catalogs/Hypatia/stellar_abundances.json") << table;
    Star* first = make_star("Component A");
    Star* second = make_star("Component B");
    Star* unique = make_star("Unique");
    first->HD = second->HD = 12;
    unique->HD = 13;
    TestCatalogReader reader;
    reader.apply_stellar_abundances(first);
    reader.apply_stellar_abundances(second);
    reader.apply_stellar_abundances(unique);
    EXPECT_FALSE(first->stellar_abundances.is_object());
    EXPECT_FALSE(second->stellar_abundances.is_object());
    ASSERT_TRUE(unique->stellar_abundances.contains("Hypatia"));
    EXPECT_DOUBLE_EQ(unique->metallicity, -0.2);
    EXPECT_DOUBLE_EQ(unique->metallicity_error_upper, 0.03);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, StellarMetallicitySurvivesCatalogAndUniverseRoundTrips)
{
    struct RestoreContext
    {
        std::filesystem::path original, temporary;
        Star **hd, **hip;
        ~RestoreContext()
        {
            hdcache = hd;
            hipcache = hip;
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_metallicity_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        hdcache, hipcache
    };
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    hdcache = hd.data();
    hipcache = hip.data();
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    Star* measured = static_cast<Star*>(cels[0]);
    measured->metallicity = -0.42;
    measured->metallicity_error_upper = 0.08;
    measured->metallicity_error_lower = -0.06;
    measured->metallicity_ratio = "[Fe/H]";
    measured->metallicity_source = "NASA pscomppars";
    measured->stellar_age = {{"value_gyr", 8.25}, {"source", "published_example"}, {"estimated", false}};
    measured->stellar_abundances = {{"Hypatia", {{"solar_scale", "asplund09"}, {"elements", json::object()}}}};
    measured->add_catalog_alias("Measured abundance host");
    Star* solar = make_star("Solar abundance");
    solar->metallicity = 0;
    solar->metallicity_ratio = "[M/H]";
    solar->metallicity_source = "NASA pscomppars";
    Star* unknown = make_star("Unknown abundance");
    struct Writer : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    } writer;
    FILE* file = fopen("catalogs/soles_alienorum.dat", "wb");
    ASSERT_NE(file, nullptr);
    writer.write_condensed_star_cat_line(file, measured);
    writer.write_condensed_star_cat_line(file, solar);
    writer.write_condensed_star_cat_line(file, unknown);
    fclose(file);
    const json saved = measured->to_json();
    const json saved_abundances = measured->stellar_abundances;
    Star restored;
    ASSERT_TRUE(restored.from_json(saved));
    EXPECT_DOUBLE_EQ(restored.metallicity, -0.42);
    EXPECT_EQ(restored.metallicity_ratio, "[Fe/H]");
    EXPECT_DOUBLE_EQ(restored.metallicity_error_lower, -0.06);
    EXPECT_EQ(restored.metallicity_source, "NASA pscomppars");
    EXPECT_EQ(restored.stellar_age, measured->stellar_age);
    EXPECT_FALSE(unknown->to_json().contains("stellar_metallicity"));
    ASSERT_TRUE(restored.from_json(unknown->to_json()));
    EXPECT_TRUE(std::isnan(restored.metallicity));
    EXPECT_TRUE(restored.metallicity_source.empty());
    delete_the_universe();
    CatalogReader reader;
    ASSERT_EQ(reader.read_condensed_star_cat(), 3);
    measured = static_cast<Star*>(cels[0]);
    solar = static_cast<Star*>(cels[1]);
    unknown = static_cast<Star*>(cels[2]);
    EXPECT_DOUBLE_EQ(measured->metallicity, -0.42);
    EXPECT_DOUBLE_EQ(measured->metallicity_error_upper, 0.08);
    EXPECT_DOUBLE_EQ(measured->metallicity_error_lower, -0.06);
    EXPECT_EQ(measured->metallicity_ratio, "[Fe/H]");
    EXPECT_EQ(measured->metallicity_source, "NASA pscomppars");
    EXPECT_DOUBLE_EQ(measured->estimate_age_gyr(), 8.25);
    EXPECT_EQ(measured->stellar_abundances, saved_abundances);
    EXPECT_EQ(measured->catalog_aliases, std::vector<std::string>({"Measured abundance host"}));
    EXPECT_DOUBLE_EQ(solar->metallicity, 0);
    EXPECT_EQ(solar->metallicity_ratio, "[M/H]");
    EXPECT_TRUE(std::isnan(solar->metallicity_error_upper));
    EXPECT_TRUE(std::isnan(unknown->metallicity));
    delete_the_universe();
}

TEST_F(CatalogParsingTest, PublishedAgeSupplementCoversTheCatalogAndMatchesIdentifiers)
{
    std::ifstream input("catalogs/stellar_ages.json");
    ASSERT_TRUE(input.good());
    const json ages = json::parse(input);
    ASSERT_GT(ages.at("rows").size(), 15000u);
    Star* tau = make_star("Unrelated display label");
    tau->HD = 10700;
    tau->HIP = 8102;
    tau->stellar_age = json::object();
    CatalogReader reader;
    reader.apply_stellar_abundances(tau);
    EXPECT_DOUBLE_EQ(tau->estimate_age_gyr(), 7.63);
    EXPECT_EQ(tau->stellar_age["source"], "Pagano_2015_Tau_Ceti");
    EXPECT_GE(tau->stellar_age["alternatives"].size(), 4u);
    tau->stellar_age = {{"value_gyr", 1.23}, {"source", "XHIP_V_137D"}, {"estimated", true}};
    reader.apply_stellar_abundances(tau);
    EXPECT_DOUBLE_EQ(tau->estimate_age_gyr(), 7.63);
    bool retained = false;
    for (const auto& alternative : tau->stellar_age["alternatives"])
        if (alternative.value("value_gyr", -1.0) == 1.23) retained = true;
    EXPECT_TRUE(retained);
    Star* supplied = make_star("82 Eridani");
    supplied->HD = 20794;
    supplied->stellar_age = {{"value_gyr", 8.75}, {"source", "authored"}};
    reader.apply_stellar_abundances(supplied);
    EXPECT_DOUBLE_EQ(supplied->estimate_age_gyr(), 8.75);
    Star* conflicting = make_star("Conflicting identifiers");
    conflicting->HIP = 8102;
    conflicting->HD = 20794;
    reader.apply_stellar_abundances(conflicting);
    EXPECT_FALSE(conflicting->stellar_age.is_object());
}

TEST_F(StellarRotationTest, AgeIndexRetainsEqualRowsAndRejectsConflictingIdentifiers)
{
    const json repeated = {{"hip", 42}, {"hd", 98}, {"name", "Indexed age"},
        {"value_gyr", 2.5}, {"source", "Published age"}};
    std::ofstream("catalogs/stellar_ages.json") << json({{"rows", json::array(
    {
        repeated, repeated,
        {{"name", "Ambiguous age"}, {"value_gyr", 2.0}, {"source", "First source"}},
        {{"name", "Ambiguous age"}, {"value_gyr", 3.0}, {"source", "Second source"}}
    })}});
    Star* matched = make_star("Indexed age");
    matched->HIP = 42;
    matched->HD = 98;
    Star* conflicting = make_star("Conflicting age");
    conflicting->HIP = 42;
    conflicting->HD = 99;
    Star* ambiguous = make_star("Ambiguous age");
    CatalogReader reader;
    reader.apply_stellar_abundances(matched);
    reader.apply_stellar_abundances(conflicting);
    reader.apply_stellar_abundances(ambiguous);
    EXPECT_DOUBLE_EQ(matched->stellar_age.at("value_gyr").get<double>(), 2.5);
    EXPECT_EQ(matched->stellar_age.at("source"), "Published age");
    EXPECT_FALSE(conflicting->stellar_age.is_object());
    EXPECT_FALSE(ambiguous->stellar_age.is_object());
}

TEST_F(CatalogParsingTest, ExoplanetMetallicityKeepsSourceScaleAndErrorsTogether)
{
    json eu =
    {
        {"pl_name", "Metal host b"}, {"hostname", "Metal host"}, {"pl_orbper", 30},
        {"st_met", 0.2}, {"st_metratio", "[Fe/H]"}, {"st_met_source", "exoplanet.eu"}
    };
    const json nasa =
    {
        {"st_met", 0.0}, {"st_metratio", "[M/H]"}, {"st_met_source", "NASA pscomppars"},
        {"st_meterr1", 0.03}, {"st_meterr2", -0.05}, {"st_metlim", 0}
    };
    TestCatalogReader::merge_exoplanet_metallicity(eu, nasa);
    EXPECT_EQ(eu["st_met"], 0.0);
    EXPECT_EQ(eu["st_metratio"], "[M/H]");
    bool valid = false;
    ExoRow row = TestCatalogReader::exorow_from_json(eu, &valid);
    ASSERT_TRUE(valid);
    FILE* cache = tmpfile();
    ASSERT_NE(cache, nullptr);
    TestCatalogReader::exorow_write_line(cache, row);
    rewind(cache);
    ExoRow cached;
    ASSERT_TRUE(TestCatalogReader::exorow_read_line(cache, cached));
    fclose(cache);
    EXPECT_DOUBLE_EQ(cached.st_met, 0);
    EXPECT_DOUBLE_EQ(cached.st_meterr1, 0.03);
    EXPECT_DOUBLE_EQ(cached.st_meterr2, -0.05);
    EXPECT_EQ(cached.st_metratio, "[M/H]");
    EXPECT_EQ(cached.st_met_source, "NASA pscomppars");
    EXPECT_TRUE(std::isnan(cached.pl_bmasseerr1));
    Star* host = static_cast<Star*>(cels[0]);
    host->metallicity = -0.4;
    host->metallicity_source = "XHIP V/137D B";
    row.st_met = 0.2;
    row.st_met_source = "exoplanet.eu";
    TestCatalogReader::apply_exoplanet_metallicity(row, host);
    EXPECT_DOUBLE_EQ(host->metallicity, 0.2);
    TestCatalogReader::apply_exoplanet_metallicity(cached, host);
    row.st_met = 0.8;
    row.st_met_source = "exoplanet.eu";
    TestCatalogReader::apply_exoplanet_metallicity(row, host);
    EXPECT_DOUBLE_EQ(host->metallicity, 0);
    EXPECT_EQ(host->metallicity_ratio, "[M/H]");
    eu["st_metlim"] = 1;
    EXPECT_TRUE(std::isnan(TestCatalogReader::exorow_from_json(eu, &valid).st_met));
    json missing = {{"st_met", nullptr}};
    TestCatalogReader::merge_exoplanet_metallicity(missing, nasa);
    EXPECT_EQ(missing["st_met_source"], "NASA pscomppars");
}

TEST_F(CatalogParsingTest, ExoplanetNamesMatchPrimaryComponentsAndConstellationCase)
{
    struct NamedPlanet
    {
        const char *designation;
        const char *friendly;
    };
    const NamedPlanet examples[] =
    {
        {"14 And b", "Spe"}, {"18 Del b", "Arion"},
        {"55 Cnc b", "Galileo"}, {"55 Cnc c", "Brahe"},
        {"55 Cnc d", "Lipperhey"}, {"55 Cnc e", "Janssen"},
        {"55 Cnc f", "Harriot"}, {"8 UMi b", "Halla"},
        {"HAT-P-14 b", "Sissi"}, {"HAT-P-23 b", "Jebus"},
        {"HAT-P-3 b", "Teberda"}, {"HD 100655 b", "Sazum"},
        {"HD 102956 b", "Isagel"}, {"HD 30856 b", "Nakanbé"},
        {"HD 86081 b", "Santamasa"}, {"WASP-19 b", "Banksia"},
        {"WASP-71 b", "Tanzanite"}, {"WASP-72 b", "Cuptor"},
        {"ups And b", "Saffar"}, {"ups And c", "Samh"},
        {"ups And d", "Majriti"}, {"55 Cnc A b", "Galileo"}
    };
    TestCatalogReader reader;
    for (const auto& example : examples)
    {
        SCOPED_TRACE(example.designation);
        const std::string designation = example.designation;
        Star *host = make_star(designation.substr(0, designation.find_last_of(' ')).c_str());
        Planet *planet = make_planet(host, example.designation);
        reader.apply_exoplanet_names({{host->seqno, {planet->seqno}}});
        EXPECT_STREQ(planet->name, example.friendly);
        snprintf(host->name, sizeof(host->name), "Renamed host");
        planet->refresh_host_display_designation();
        EXPECT_STREQ(planet->name, example.friendly);
        EXPECT_EQ(planet->display_designation, std::string("Renamed host ") + designation.back());
        if (designation == "55 Cnc e")
        {
            EXPECT_EQ(planet->type, lavaworld);
            EXPECT_DOUBLE_EQ(planet->temperature, 3771.0);
        }
        delete_the_universe();
        make_star("Sol");
    }

    Star *secondary = make_star("55 Cnc B");
    Planet *planet = make_planet(secondary, "55 Cnc Bb");
    reader.apply_exoplanet_names({{secondary->seqno, {planet->seqno}}});
    EXPECT_STREQ(planet->name, "55 Cnc B b");
    delete_the_universe();
}

TEST_F(CatalogParsingTest, PlanetDisplayDesignationsFollowPreferredHostAcrossSourceAliasesAndReloads)
{
    Star* host = make_star("alf Test");
    host->HD = 98765;
    host->HIP = 54321;
    snprintf(host->Gliese, sizeof(host->Gliese), "GJ 9999");
    host->add_catalog_alias("HD 98765");
    host->add_catalog_alias("HIP 54321");
    host->add_catalog_alias("GJ 9999");
    const char* sources[] = {"HD 98765 b", "HIP 54321 c", "GJ 9999 d"};
    std::map<int, std::vector<int>> planets;
    for (int i = 0; i < 3; ++i)
    {
        Planet* planet = make_planet(host, sources[i], (i + 1) * AU);
        planets[host->seqno].push_back(planet->seqno);
    }
    TestCatalogReader reader;
    for (int pass = 0; pass < 3; ++pass)
    {
        reader.apply_exoplanet_names(planets);
        for (int i = 0; i < 3; ++i)
        {
            Planet* planet = static_cast<Planet*>(cels[planets[host->seqno][i]]);
            const std::string display = std::string(host->name) + " " + char('b' + i);
            EXPECT_STREQ(planet->name, display.c_str());
            EXPECT_EQ(planet->display_designation, display);
            EXPECT_EQ(planet->catalog_designation, sources[i]);
            EXPECT_EQ(find_object(sources[i], false), planet->seqno);
            EXPECT_EQ(find_object(display.c_str(), false), planet->seqno);
            Planet restored;
            restored.from_json(planet->to_json());
            EXPECT_EQ(restored.catalog_designation, sources[i]);
            EXPECT_EQ(restored.display_designation, display);
        }
        snprintf(host->name, sizeof(host->name), "Preferred host");
        for (int i = 0; i < 3; ++i)
        {
            Planet* planet = static_cast<Planet*>(cels[planets[host->seqno][i]]);
            planet->refresh_host_display_designation();
            EXPECT_STREQ(planet->name, (std::string("Preferred host ") + char('b' + i)).c_str());
        }
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, CatalogSourcesPreserveNamesLettersAndPeriods)
{
    for (bool nasa : {false, true})
    {
        for (char letter : {'b', 'c', 'd', 'e'})
        {
            const std::string name = std::string("Synthetic host ") + letter;
            json row = {{"hostname", "Synthetic host"}, {"pl_name", name}, {"pl_orbper", 25.0}};
            ASSERT_TRUE(TestCatalogReader::accept_exoplanet_source(row, nasa));
            EXPECT_EQ(row["hostname"], "Synthetic host");
            EXPECT_EQ(row["pl_name"], name);
            EXPECT_DOUBLE_EQ(row["pl_orbper"], 25);
            EXPECT_EQ(row["source_catalog"], nasa ? "NASA" : "EU");
            const json accepted = row;
            ASSERT_TRUE(TestCatalogReader::accept_exoplanet_source(row, nasa));
            EXPECT_EQ(row, accepted);
        }
    }
}

TEST_F(CatalogParsingTest, UnknownPlanetInclinationsUseStellarCompanionPlane)
{
    Star::load_main_seq_dat();
    for (bool planets_on_secondary : {false, true})
    {
        for (bool known_planet : {false, true})
        {
            if (!cels[0]) make_star("Sol");
            Star* primary = make_star("Binary primary");
            primary->right_ascension = 4.191;
            primary->declination = 0.581;
            primary->distance = 17.5 * parsec;
            primary->update_location(simnow);
            Star* companion = make_star("Binary secondary");
            companion->make_companion_of(primary, 'B');
            companion->orbit->heliocentric_inclination = 103.82 * fiftyseventh;
            companion->orbit->heliocentric_node = 91.51 * fiftyseventh;
            companion->orbit->period = 78 * oneday;
            companion->orbit->semimajor_axis = 0.2 * AU;
            Star* host = planets_on_secondary ? companion : primary;
            TestCatalogReader reader;
            std::map<int, std::vector<int>> planets;
            unsigned int count = 0;
            const char* names[] = {"Binary planet b", "Binary planet c", "Binary planet d"};
            const double periods[] = {39.8438, 102.19, 12.949};
            for (int i = 0; i < 3; ++i)
            {
                ExoRow row;
                row.pl_name = names[i];
                row.hostname = host->name;
                row.pl_orbper = periods[i];
                row.pl_msinij = i == 0 ? 1.093 : 0.02;
                row.pl_bmassj = row.pl_msinij;
                row.pl_orbincl = i == 0 ? (known_planet ? 90 : 0.4) : NAN;
                row.transiting = known_planet && i == 0;
                ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
            }
            reader.apply_exoplanet_names(planets);
            for (size_t i = 0; i < host->pl_indices.size(); ++i)
            {
                Planet* planet = static_cast<Planet*>(cels[host->pl_indices[i]]);
                planet->update_location(simnow);
                double inclination = 0, node = 0;
                incl_and_node_from_system_plane(planet->location.orbital_plane, inclination, node,
                    host->location.system_center - cels[0]->location.system_center);
                EXPECT_NEAR(inclination * fiftyseven, known_planet ? 90 : 103.82, 1e-5);
                if (!known_planet || i != 0)
                {
                    EXPECT_NEAR(node * fiftyseven, 91.51, 1e-5);
                }
                EXPECT_LT(planet->mass, 2 * jupiter_mass);
            }
            delete_the_universe();
        }
    }
}

TEST_F(CatalogParsingTest, UnknownPlanesFollowTransitDiskMeasurementRotationCompanionPrecedence)
{
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        Star* host = make_star("Hierarchy host");
        host->rot_heliocen_incl = 40 * fiftyseventh;
        host->rot_heliocen_node = 20 * fiftyseventh;
        host->obliquity = 5 * fiftyseventh;
        host->equinox = 7 * fiftyseventh;
        host->has_disk = scenario == 1 || scenario == 3;
        host->disk_heliocen_inclination = 60 * fiftyseventh;
        host->disk_heliocen_node = 30 * fiftyseventh;
        Star* companion = make_star("Hierarchy companion");
        companion->make_companion_of(host, 'B');
        companion->orbit->heliocentric_inclination = 110 * fiftyseventh;
        companion->orbit->heliocentric_node = 70 * fiftyseventh;
        std::map<int, std::vector<int>> planets;
        TestCatalogReader reader;
        unsigned int count = 0;
        for (int i = 0; i < 2; ++i)
        {
            ExoRow row;
            row.hostname = host->name;
            row.pl_name = std::string(host->name) + " " + char('b' + i);
            row.pl_orbper = 20 * (i + 1);
            row.pl_msinie = 2;
            row.pl_orbincl = i == 0 && scenario > 0 ? (scenario == 3 ? 85 : 45) : NAN;
            row.transiting = scenario == 3 && i == 0;
            ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
        }
        const double expected[] = {40, 60, 45, 85};
        const double expected_node[] = {20, 30, 55, 80};
        if (scenario >= 2)
        {
            Orbit* measured = cels[planets[host->seqno][0]]->orbit;
            measured->heliocentric_inclination = measured->inclination;
            measured->heliocentric_node = expected_node[scenario] * fiftyseventh;
        }
        for (int pass = 0; pass < 3; ++pass)
        {
            reader.apply_exoplanet_names(planets);
            Planet* inferred = static_cast<Planet*>(cels[planets[host->seqno][1]]);
            EXPECT_NEAR(inferred->orbit->heliocentric_inclination * fiftyseven, expected[scenario], 1e-6);
            EXPECT_NEAR(inferred->orbit->heliocentric_node * fiftyseven, expected_node[scenario], 1e-6);
            EXPECT_NEAR(inferred->mass / earth_mass, 2 / sin(expected[scenario] * fiftyseventh), 1e-5);
        }
        delete_the_universe();
    }
}

TEST_F(CatalogParsingTest, BroadRVInclinationsYieldToTransitsAndHotJupitersKeepTheirPlanes)
{
    Star::load_main_seq_dat();
    Star* host = make_star("Precision test host");
    host->right_ascension = 0.15;
    host->declination = 0.08;
    host->distance = 47 * parsec;
    host->update_location(simnow);
    TestCatalogReader reader;
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    const double inclinations[] = {83.4, 89.5, 39, 41, 39, NAN, 41};
    const double periods[] = {0.96, 29.84, 8.4, 96.63, 5, 6, 120};
    const double masses[] = {4.84, 11.41, 7.3, 14.9, 318, 318, 20};
    const double minimum_masses[] = {4.84, 11.41, 4.545, 9.789, 200, 318, 13};
    const double radii[] = {1.6, 2.98, 2.61, 3.97, 11.2, 11.2, 4};
    const double upper_errors[] = {NAN, NAN, 34, 32, 34, NAN, 0.5};
    const double lower_errors[] = {NAN, NAN, -24, -23, -24, NAN, -0.5};
    for (int i = 0; i < 7; ++i)
    {
        json source =
        {
            {"pl_name", "Precision planet " + std::to_string(i)}, {"hostname", host->name},
            {"pl_orbper", periods[i]}, {"pl_bmasse", masses[i]}, {"pl_msinie", minimum_masses[i]},
            {"pl_rade", radii[i]}, {"pl_orbincl", inclinations[i]},
            {"pl_orbinclerr1", upper_errors[i]}, {"pl_orbinclerr2", lower_errors[i]}
        };
        bool ok = false;
        ExoRow row = reader.exorow_from_json(source, &ok);
        ASSERT_TRUE(ok);
        FILE* cache = tmpfile();
        ASSERT_NE(cache, nullptr);
        reader.exorow_write_line(cache, row);
        rewind(cache);
        ExoRow restored;
        ASSERT_TRUE(reader.exorow_read_line(cache, restored));
        fclose(cache);
        if (std::isfinite(upper_errors[i]))
        {
            EXPECT_DOUBLE_EQ(restored.pl_orbinclerr1, upper_errors[i]);
            EXPECT_DOUBLE_EQ(restored.pl_orbinclerr2, lower_errors[i]);
        }
        ASSERT_TRUE(reader.add_exoplanet_from_row(restored, host, planets, count));
    }
    reader.apply_exoplanet_names(planets);
    const double expected[] = {83.4, 89.5, 86.45, 86.45, 39, 86.45, 41};
    for (int i = 0; i < 7; ++i)
    {
        Planet* planet = static_cast<Planet*>(cels[host->pl_indices[i]]);
        planet->update_location(simnow);
        double inclination = 0, node = 0;
        incl_and_node_from_system_plane(planet->location.orbital_plane, inclination, node,
            host->location.system_center - cels[0]->location.system_center);
        EXPECT_NEAR(inclination * fiftyseven, expected[i], 1e-5);
    }
    EXPECT_EQ(static_cast<Planet*>(cels[host->pl_indices[4]])->type, hot_jupiter);
    EXPECT_EQ(static_cast<Planet*>(cels[host->pl_indices[5]])->type, hot_jupiter);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, CachedHD3167BroadInclinationsYieldToTransitReference)
{
    FILE* cache = fopen("catalogs/exoplanets_planets.dat", "r");
    if (!cache) GTEST_SKIP() << "Exoplanet catalog cache is absent";
    std::vector<ExoRow> rows;
    TestCatalogReader reader;
    ExoRow row;
    while (reader.exorow_read_line(cache, row))
    {
        if (row.hostname == "HD 3167") rows.push_back(row);
    }
    fclose(cache);
    ASSERT_EQ(rows.size(), 4u);
    Star::load_main_seq_dat();
    Star* host = make_star("HD 3167");
    host->right_ascension = rows[0].ra * fiftyseventh;
    host->declination = rows[0].dec * fiftyseventh;
    host->distance = rows[0].sy_dist * parsec;
    host->update_location(simnow);
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    for (const ExoRow& cached : rows)
    {
        ASSERT_TRUE(reader.add_exoplanet_from_row(cached, host, planets, count));
    }
    reader.apply_exoplanet_names(planets);
    int checked = 0;
    for (int index : host->pl_indices)
    {
        Planet* planet = static_cast<Planet*>(cels[index]);
        if (strcmp(planet->name, "HD 3167 d") && strcmp(planet->name, "HD 3167 e")) continue;
        planet->update_location(simnow);
        double inclination = 0, node = 0;
        incl_and_node_from_system_plane(planet->location.orbital_plane, inclination, node,
            host->location.system_center - cels[0]->location.system_center);
        EXPECT_GE(inclination * fiftyseven, 85.0) << planet->name;
        EXPECT_LE(inclination * fiftyseven, 90.0) << planet->name;
        ++checked;
    }
    EXPECT_EQ(checked, 2);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, CachedPublishedWhiteDwarfIsSearchable)
{
    struct RestoreCaches
    {
        Star** hd = hdcache;
        Star** hip = hipcache;
        std::map<std::string, Star*> dm = std::move(dmcache);
        bool orbits_loaded = CatalogReader::condensed_star_orbits_loaded;
        ~RestoreCaches()
        {
            hdcache = hd;
            hipcache = hip;
            dmcache = std::move(dm);
            CatalogReader::condensed_star_orbits_loaded = orbits_loaded;
        }
    } restore;
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    hdcache = hd.data();
    hipcache = hip.data();
    dmcache.clear();
    CatalogReader reader;
    const std::string path = reader.get_condensed_starcat_name();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "Condensed catalog is absent";
    delete_the_universe();
    Star::load_main_seq_dat();
    ASSERT_GT(reader.read_condensed_star_cat(), 100000);
    // The complete rebuild also contains the distinct B1950 WD1330+643.
    // Address the color publication's target by its unambiguous J2000 identity.
    const int found = find_object("WDJ133001.17+643523.69", true);
    ASSERT_GE(found, 0);
    Star* star = static_cast<Star*>(cels[found]);
    EXPECT_EQ(star->gaia_wd_name, "WDJ133001.17+643523.69");
    EXPECT_EQ(star->WD, "WD1328+648");
    EXPECT_GT(star->distance, 0);
    EXPECT_TRUE(star->distance_known);
    EXPECT_TRUE(star->apply_published_wd_color());
    EXPECT_EQ(star->wd_color_red, 0xdd);
    EXPECT_EQ(star->wd_color_green, 0xff);
    EXPECT_EQ(star->wd_color_blue, 0xb8);
    const int canonical = find_object("WD1330+643", true);
    ASSERT_GE(canonical, 0);
    EXPECT_NE(canonical, found);
    EXPECT_EQ(static_cast<Star*>(cels[canonical])->WD, "WD1330+643");
    EXPECT_EQ(static_cast<Star*>(cels[canonical])->gaia_wd_name, "WDJ133215.94+640656.24");
    delete_the_universe();
}

TEST_F(CatalogParsingTest, InitialExoplanetPassReusesDistantSupplementNamesAndAliases)
{
    Star* original = make_star("TRAPPIST-1");
    original->add_catalog_alias("2MASS J23062928-0502285");
    original->add_catalog_alias("EPIC 246199087");
    for (int index = 0; index < 20; ++index)
        make_star(("Other catalog entry " + std::to_string(index)).c_str());
    const int count = ncelobjs;
    TestCatalogReader reader;
    ExactStarSearchIndex lookup;
    for (const char* name : {"TRAPPIST-1", "2MASS J23062928-0502285", "EPIC 246199087"})
    {
        ExoRow row;
        row.hostname = name;
        row.ra = 346.623;
        row.dec = -5.041;
        row.sy_dist = 12.4;
        bool fresh = true;
        EXPECT_EQ(reader.resolve_or_create_exostar(row, false, &fresh, &lookup), original);
        EXPECT_FALSE(fresh);
        EXPECT_EQ(ncelobjs, count);
        EXPECT_EQ(reader.resolve_or_create_exostar(row, false, &fresh), original);
        EXPECT_FALSE(fresh);
        EXPECT_EQ(ncelobjs, count);
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, TrappistSupplementHostReceivesItsCachedPlanetsWithoutAnotherStar)
{
    load_brown_dwarf_supplement();
    const int original = find_object("TRAPPIST-1", true, 9e29, 0);
    ASSERT_GE(original, 0);
    ExactStarSearchIndex lookup;
    TestCatalogReader reader;
    FILE* input = fopen("catalogs/exoplanets_planets.dat", "r");
    ASSERT_NE(input, nullptr);
    ExoRow row;
    unsigned int planets = 0;
    std::map<int, std::vector<int>> planet_ids;
    while (reader.exorow_read_line(input, row))
    {
        if (row.hostname != "TRAPPIST-1") continue;
        bool fresh = true;
        Star* host = reader.resolve_or_create_exostar(row, false, &fresh, &lookup);
        EXPECT_EQ(host, cels[original]);
        EXPECT_FALSE(fresh);
        reader.add_exoplanet_from_row(row, host, planet_ids, planets, &lookup);
    }
    fclose(input);
    EXPECT_GE(planets, 7u);
    int stars = 0, companions = 0;
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->typeclass() == class_star && !strcmp(cels[index]->name, "TRAPPIST-1")) ++stars;
        if (cels[index]->typeclass() == class_planet && cels[index]->orbit
            && cels[index]->orbit->center == cels[original]) ++companions;
    }
    EXPECT_EQ(stars, 1);
    EXPECT_GE(companions, 7);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, UltracoolComponentAliasesReusePublishedOrbitTargets)
{
    load_brown_dwarf_supplement();
    const int legacy_inner = find_object("GJ 569 C", true, 9e29, 0);
    const int canonical_inner = find_object("Gl 569 Bb", true, 9e29, 0);
    ASSERT_GE(canonical_inner, 0);
    EXPECT_EQ(legacy_inner, canonical_inner);
    const int legacy_wide = find_object("SDSS J1416+1348 B", true, 9e29, 0);
    const int canonical_wide = find_object("ULAS J1416+1348", true, 9e29, 0);
    ASSERT_GE(canonical_wide, 0);
    EXPECT_EQ(legacy_wide, canonical_wide);
    EXPECT_NE(legacy_wide, find_object("SDSS J1416+1348", true, 9e29, 0));
    delete_the_universe();
}

TEST_F(CatalogParsingTest, InitialExoplanetPassKeepsSimilarUnrelatedHostNamesDistinct)
{
    Star* original = make_star("TRAPPIST-1");
    TestCatalogReader reader;
    ExactStarSearchIndex lookup;
    ExoRow row;
    row.hostname = "TRAPPIST-2";
    row.ra = 100;
    row.dec = 20;
    row.sy_dist = 100;
    bool fresh = false;
    Star* other = reader.resolve_or_create_exostar(row, false, &fresh, &lookup);
    EXPECT_NE(other, original);
    EXPECT_TRUE(fresh);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, WhiteDwarfHostReloadRetainsTheOriginalObject)
{
    // Synthetic measurements isolate the host identity from atmospheric and orbital models.
    ExoRow row;
    row.hostname = "WD 0806-661";
    row.st_spectype = "DA";
    row.st_mass = 0.6;
    row.st_rad = 0.012;
    row.st_teff = 10000;
    row.st_lum = -3;
    row.sy_dist = 20;
    row.ra = 120;
    row.dec = -30;
    TestCatalogReader reader;
    ExactStarSearchIndex first_lookup;
    bool was_new = false;
    Star* original = reader.resolve_or_create_exostar(row, true, &was_new, &first_lookup);
    ASSERT_NE(original, nullptr);
    ASSERT_TRUE(was_new);
    const int count = ncelobjs;
    for (int pass = 0; pass < 3; ++pass)
    {
        ExactStarSearchIndex lookup;
        Star* reloaded = reader.resolve_or_create_exostar(row, true, &was_new, &lookup);
        EXPECT_EQ(reloaded, original);
        EXPECT_FALSE(was_new);
        EXPECT_EQ(ncelobjs, count);
    }
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &was_new), original);
    EXPECT_FALSE(was_new);
    EXPECT_EQ(ncelobjs, count);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, ImportedHostRetainsAllSourceIdentifiers)
{
    TestCatalogReader reader;
    ExoRow row;
    row.hostname = "GJ 345.6";
    row.hd_name = "HD 123456";
    row.hip_name = "HIP 45678";
    row.sy_dist = 10;
    row.ra = 30;
    row.dec = 20;
    bool fresh = false;
    Star* host = reader.resolve_or_create_exostar(row, true, &fresh);
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(fresh);
    EXPECT_EQ(host->HD, 123456u);
    EXPECT_EQ(host->HIP, 45678u);
    ExactStarSearchIndex lookup;
    for (const char* alias : {"GJ 345.6", "HD 123456", "HIP 45678"})
    {
        EXPECT_EQ(find_object(alias, true, 9e29, 1, &lookup), host->seqno);
    }
    row.hostname = "Alternate Primary";
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &fresh, &lookup), host);
    EXPECT_FALSE(fresh);
    EXPECT_EQ(find_object("Alternate Primary", true), host->seqno);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, Moa2010WhiteDwarfHostGetsACompactRadiusWithoutCatalogRadius)
{
    ExoRow row;
    row.hostname = "MOA-2010-BLG-477L";
    row.pl_name = "MOA-2010-BLG-477L b";
    row.st_spectype = "WD";
    row.st_mass = 0.53;
    row.st_teff = 5950;
    row.sy_dist = 2300;
    row.ra = 271.5291667;
    row.dec = -31.4544444;
    TestCatalogReader reader;
    bool fresh = false;
    Star *host = reader.resolve_or_create_exostar(row, true, &fresh);
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(fresh);
    EXPECT_EQ(stellar_regime(host), regime_degenerate);
    EXPECT_EQ(host->cenobj, host);
    EXPECT_EQ(host->namelen, strlen(host->name));
    EXPECT_GT(host->volumetric_mean_radius, 1e6);
    EXPECT_LT(host->volumetric_mean_radius, 2e7);
    EXPECT_GT(host->absolute_magnitude, 10);
    host->volumetric_mean_radius = 0;
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &fresh), host);
    EXPECT_GT(host->volumetric_mean_radius, 0);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, MoaHostPublicationWaitsForTheActiveNavigationFrame)
{
    ExoRow row;
    row.hostname = "MOA-2010-BLG-477L";
    row.st_spectype = "WD";
    row.st_mass = 0.53;
    row.st_teff = 5950;
    row.sy_dist = 2300;
    row.ra = 271.5291667;
    row.dec = -31.4544444;
    TestCatalogReader reader;
    std::unique_lock<std::recursive_mutex> frame(catalog_mutex);
    std::promise<void> started;
    auto ready = started.get_future();
    auto import = std::async(std::launch::async, [&]()
    {
        started.set_value();
        bool fresh = false;
        return reader.resolve_or_create_exostar(row, true, &fresh);
    });
    ready.wait();
    const auto status = import.wait_for(std::chrono::milliseconds(50));
    const int count_during_frame = ncelobjs;
    frame.unlock();
    Star *host = import.get();
    EXPECT_EQ(status, std::future_status::timeout);
    EXPECT_EQ(count_during_frame, 1);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(cels[host->seqno], host);
    EXPECT_EQ(host->cenobj, host);
    EXPECT_GT(host->volumetric_mean_radius, 0);
    EXPECT_GT(host->location.system_center.magnitude(), 0);
    row.pl_name = "MOA-2010-BLG-477L b";
    row.pl_bmassj = 1.43;
    row.pl_orbsmax = 2.8;
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    frame.lock();
    std::promise<void> planet_started;
    auto planet_ready = planet_started.get_future();
    auto planet_import = std::async(std::launch::async, [&]()
    {
        planet_started.set_value();
        return reader.add_exoplanet_from_row(row, host, planets, count);
    });
    planet_ready.wait();
    const auto planet_status = planet_import.wait_for(std::chrono::milliseconds(50));
    const bool empty_during_frame = host->pl_indices.empty();
    frame.unlock();
    EXPECT_TRUE(planet_import.get());
    EXPECT_EQ(planet_status, std::future_status::timeout);
    EXPECT_TRUE(empty_during_frame);
    EXPECT_EQ(count, 1u);
    ASSERT_EQ(host->pl_indices.size(), 1u);
    EXPECT_EQ(cels[host->pl_indices.front()]->orbit->center, host);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, SourceSelectionUsesDispositionRatherThanHostNames)
{
    for (bool nasa : {false, true})
    {
        json row = {{"hostname", "Synthetic host"}, {"pl_name", "Synthetic host b"}, {"pl_orbper", 7.0}};
        ASSERT_TRUE(TestCatalogReader::accept_exoplanet_source(row, nasa));
        EXPECT_EQ(row["hostname"], "Synthetic host");
        row["koi_disposition"] = "FALSE POSITIVE";
        EXPECT_FALSE(TestCatalogReader::accept_exoplanet_source(row, nasa));
        row.erase("koi_disposition");
        row["planet_status"] = "Retracted";
        EXPECT_FALSE(TestCatalogReader::accept_exoplanet_source(row, nasa));
    }
}

TEST_F(CatalogParsingTest, HostPositionSupplementRecoversAndCachesAnIdentifiedStar)
{
    auto positions = TestCatalogReader::read_exoplanet_host_positions("catalogs/exoplanet_host_positions.dat");
    ASSERT_EQ(positions.size(), 52u);

    const ExoRow& component_a = positions.at("2MASS J18452562+4339300");
    EXPECT_NEAR(component_a.ra, 281.3566609376, 1e-9);
    EXPECT_NEAR(component_a.dec, 43.6584094391, 1e-9);
    EXPECT_NEAR(component_a.sy_dist, 331.56723, 1e-5);

    ExoRow row;
    row.hostname = "CoRoT-33";
    row.pl_name = "CoRoT-33 b";
    row.ra = 279.641666678;
    row.dec = 5.624722241;
    ASSERT_TRUE(TestCatalogReader::apply_exoplanet_host_position(row, positions));
    EXPECT_NEAR(row.sy_dist, 651.35693, 1e-5);
    EXPECT_NEAR(row.ra, 279.6413, 0.001);
    EXPECT_NEAR(row.dec, 5.6247, 0.001);

    TestCatalogReader reader;
    bool was_new = false;
    Star* host = reader.resolve_or_create_exostar(row, false, &was_new);
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(was_new);
    EXPECT_TRUE(host->distance_known);
    EXPECT_NEAR(host->distance / parsec, row.sy_dist, 1e-5);
    EXPECT_NEAR(host->right_ascension, row.ra * fiftyseventh, 1e-12);
    EXPECT_NEAR(host->declination, row.dec * fiftyseventh, 1e-12);

    FILE* cache = tmpfile();
    ASSERT_NE(cache, nullptr);
    TestCatalogReader::exorow_write_line(cache, row);
    rewind(cache);
    ExoRow restored;
    bool read = TestCatalogReader::exorow_read_line(cache, restored);
    fclose(cache);
    ASSERT_TRUE(read);
    EXPECT_EQ(restored.hostname, row.hostname);
    EXPECT_TRUE(TestCatalogReader::exorow_has_position(restored));
    EXPECT_NEAR(restored.sy_dist, row.sy_dist, 1e-4);

    row.sy_dist = 700;
    EXPECT_FALSE(TestCatalogReader::apply_exoplanet_host_position(row, positions));
    EXPECT_EQ(row.sy_dist, 700);
    row.hostname = "Unidentified host";
    row.sy_dist = NAN;
    EXPECT_FALSE(TestCatalogReader::apply_exoplanet_host_position(row, positions));
    EXPECT_TRUE(std::isnan(row.sy_dist));
}

TEST_F(CatalogParsingTest, HostPositionSupplementRejectsInvalidMeasurements)
{
    auto path = std::filesystem::temp_directory_path() / "alienorum_host_positions_test.dat";
    {
        std::ofstream file(path);
        file << "# Test measurements\n"
             << "Valid origin|0|0|12\n"
             << "Invalid distance|1|2|-3\n"
             << "Invalid RA|360|2|3\n"
             << "Invalid Dec|1|91|3\n"
             << "Malformed value|1x|2|3\n"
             << "Missing value|1|2|\n";
    }
    auto positions = TestCatalogReader::read_exoplanet_host_positions(path.string().c_str());
    std::filesystem::remove(path);
    ASSERT_EQ(positions.size(), 1u);
    ExoRow row;
    row.hostname = "Valid origin";
    ASSERT_TRUE(TestCatalogReader::apply_exoplanet_host_position(row, positions));
    EXPECT_TRUE(TestCatalogReader::exorow_has_position(row));
    bool was_new = false;
    TestCatalogReader reader;
    Star* host = reader.resolve_or_create_exostar(row, false, &was_new);
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(host->distance_known);
    EXPECT_EQ(host->right_ascension, 0);
    EXPECT_EQ(host->declination, 0);
}

TEST_F(CatalogParsingTest, HostStarConsecutiveCachingDoesNotConflateDistinctStars)
{
    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD + 1]();
    }
    if (!hipcache)
    {
        hipcache = new Star*[MAX_HIP + 1]();
    }

    Star* tau_cet = make_star("tau Cet");
    tau_cet->HD = 10700;
    hdcache[10700] = tau_cet;

    Star* tau_gem = make_star("tau Gem");
    tau_gem->HD = 54719;
    hdcache[54719] = tau_gem;

    Star* star_81cet = make_star("81 Cet");
    star_81cet->HD = 16400;
    hdcache[16400] = star_81cet;

    Star* star_82eri = make_star("82 Eri");
    star_82eri->HD = 20794;
    hdcache[20794] = star_82eri;

    Star* cnc_a = make_star("55 Cnc A");
    Star* cnc_b = make_star("GJ 324 B");
    cnc_b->add_catalog_alias("55 Cnc B");

    std::string last_hostname;
    Star* last_host_star = nullptr;
    TestCatalogReader tcr;

    auto process_host = [&](const ExoRow& row) -> Star*
    {
        Star* host_star = nullptr;
        bool was_new = false;
        if (!row.hostname.empty() && row.hostname == last_hostname && last_host_star)
        {
            host_star = last_host_star;
        }
        else
        {
            host_star = tcr.resolve_or_create_exostar(row, false, &was_new);
            last_host_star = host_star;
            last_hostname = row.hostname;
        }
        return host_star;
    };

    // 1. tau Cet (7 chars) -> tau Gem (7 chars)
    ExoRow row_cet;
    row_cet.hostname = "tau Cet";
    row_cet.hd_name = "HD 10700";
    Star* resolved_cet = process_host(row_cet);
    EXPECT_EQ(resolved_cet, tau_cet);

    ExoRow row_gem;
    row_gem.hostname = "tau Gem";
    row_gem.hd_name = "HD 54719";
    Star* resolved_gem = process_host(row_gem);
    EXPECT_EQ(resolved_gem, tau_gem);
    EXPECT_NE(resolved_gem, tau_cet);

    // 2. 81 Cet (6 chars) -> 82 Eri (6 chars)
    ExoRow row_81cet;
    row_81cet.hostname = "81 Cet";
    row_81cet.hd_name = "HD 16400";
    Star* resolved_81cet = process_host(row_81cet);
    EXPECT_EQ(resolved_81cet, star_81cet);

    ExoRow row_82eri;
    row_82eri.hostname = "82 Eri";
    row_82eri.hd_name = "HD 20794";
    Star* resolved_82eri = process_host(row_82eri);
    EXPECT_EQ(resolved_82eri, star_82eri);
    EXPECT_NE(resolved_82eri, star_81cet);

    // 3. 55 Cnc A (8 chars) -> 55 Cnc B (8 chars)
    ExoRow row_cnca;
    row_cnca.hostname = "55 Cnc A";
    Star* resolved_cnca = process_host(row_cnca);
    EXPECT_EQ(resolved_cnca, cnc_a);

    ExoRow row_cncb;
    row_cncb.hostname = "55 Cnc B";
    Star* resolved_cncb = process_host(row_cncb);
    EXPECT_EQ(resolved_cncb, cnc_b);
    EXPECT_NE(resolved_cncb, cnc_a);

    hdcache[10700] = nullptr;
    hdcache[54719] = nullptr;
    hdcache[16400] = nullptr;
    hdcache[20794] = nullptr;
    delete_the_universe();
}

TEST_F(CatalogParsingTest, IndexedExoplanetHostsStayDistinctAndPublishNewStars)
{
    Star::load_main_seq_dat();
    Star *existing = make_star("Kepler-123");
    existing->add_catalog_alias("Existing host alias");
    ExactStarSearchIndex lookup;
    TestCatalogReader reader;
    ExoRow row;
    row.hostname = "Kepler-1234";
    row.ra = 10;
    row.dec = 20;
    row.sy_dist = 10;
    bool was_new = false;
    Star *added = reader.resolve_or_create_exostar(row, true, &was_new, &lookup);
    ASSERT_NE(added, nullptr);
    EXPECT_NE(added, existing);
    EXPECT_TRUE(was_new);
    EXPECT_STREQ(added->name, "Kepler-1234");
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &was_new, &lookup), added);
    EXPECT_FALSE(was_new);

    row.hostname = "Existing host alias";
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &was_new, &lookup), existing);
    EXPECT_FALSE(was_new);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, RejectedHostsDoNotLeaveDeletedPointersInCatalogLookups)
{
    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD + 1]();
    }
    if (!hipcache)
    {
        hipcache = new Star*[MAX_HIP + 1]();
    }
    auto check_rejection = [&](const char* name, Star** cache)
    {
        Star* original = cache[99999];
        cache[99999] = nullptr;
        ExoRow row;
        row.hostname = name;
        row.ra = 10;
        row.dec = 20;
        bool was_new = false;
        TestCatalogReader reader;
        EXPECT_EQ(reader.resolve_or_create_exostar(row, false, &was_new), nullptr);
        EXPECT_EQ(cache[99999], nullptr);
        row.sy_dist = 10;
        Star* recovered = reader.resolve_or_create_exostar(row, false, &was_new);
        EXPECT_NE(recovered, nullptr);
        EXPECT_EQ(cache[99999], recovered);
        EXPECT_TRUE(was_new);
        cache[99999] = original;
    };
    check_rejection("HD 99999", hdcache);
    check_rejection("HIP 99999", hipcache);
}

TEST_F(CatalogParsingTest, ExoplanetPassesShareCatalogSnapshot)
{
    struct RestoreContext
    {
        std::filesystem::path original, temporary;
        bool offline;
        ~RestoreContext()
        {
            radio_silence = offline;
            std::filesystem::current_path(original);
            std::filesystem::remove_all(temporary);
        }
    } restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_tap_snapshot_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
        radio_silence
    };
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    radio_silence = true;
    Star* host = make_star("Snapshot host");
    std::ofstream("catalogs/exoplanet_host_positions.dat").close();
    const json catalog = json::array(
    {
        {{"pl_name", "Snapshot host b"}, {"hostname", "Snapshot host"},
            {"pl_orbper", 365.25}, {"pl_orbsmax", 1.0}, {"pl_bmasse", 1.0},
            {"pl_rade", 1.0}, {"ra", 10.0}, {"dec", 20.0}, {"sy_dist", 10.0}}
    });
    std::ofstream("catalogs/exoplanets.json") << catalog;
    CatalogReader reader;
    reader.load_exoplanets_from_tap(true);

    // The planet pass must use the host pass's data even if persistence fails or changes.
    std::ofstream("catalogs/exoplanets.json") << "invalid catalog";
    EXPECT_EQ(reader.load_exoplanets_from_tap(), 1u);
    ASSERT_EQ(host->pl_indices.size(), 1u);
    EXPECT_STREQ(cels[host->pl_indices.front()]->name, "Snapshot host b");
    delete_the_universe();
}

TEST_F(StellarDownloadTest, RecentExoplanetFilesSuppressRefreshAcrossBothPasses)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = false;
    class DownloadReader : public CatalogReader
    {
    public:
        int requests = 0;
    protected:
        bool fetch_stellar_catalog_json(const std::string&, const std::string&, json&) override
        {
            ++requests;
            return false;
        }
    };
    const char* paths[] =
    {
        "catalogs/exoplanets.json", "catalogs/exoplanets_stars.dat", "catalogs/exoplanets_planets.dat"
    };
    // The legacy JSON omits transit metadata; both derived files require local reconstruction.
    const json saved = json::array({{{"pl_name", "Snapshot host b"}, {"hostname", "Snapshot host"},
        {"pl_orbper", 20}, {"pl_orbsmax", 0.2}, {"pl_bmasse", 1}, {"pl_rade", 1},
        {"sy_dist", 10}, {"ra", 10}, {"dec", 20}, {"source_catalog", "NASA"}}});
    for (const char* fresh : paths)
    {
        for (int age_minutes : {13 * 60, 24 * 60 - 1, -60})
        {
            for (bool stars_first : {false, true})
            {
                SCOPED_TRACE(std::string(fresh) + " age=" + std::to_string(age_minutes) +
                    " stars_first=" + std::to_string(stars_first));
                delete_the_universe();
                make_star("Sol");
                Star* host = make_star("Snapshot host");
                std::ofstream(paths[0]) << saved;
                for (int i : {1, 2}) std::ofstream(paths[i]) << "# EXO_TRANSIT2\n";
                const auto now = std::filesystem::file_time_type::clock::now();
                for (const char* path : paths)
                {
                    std::filesystem::last_write_time(path, now - std::chrono::hours(48));
                }
                std::filesystem::last_write_time(fresh, now - std::chrono::minutes(age_minutes));
                // A newer supplement invalidates derived data without allowing a TAP refresh.
                std::ofstream("catalogs/exoplanet_host_positions.dat").close();
                DownloadReader reader;
                reader.load_exoplanets_from_tap(stars_first);
                EXPECT_EQ(reader.requests, 0);
                reader.load_exoplanets_from_tap(!stars_first);
                EXPECT_EQ(reader.requests, 0);
                EXPECT_EQ(host->pl_indices.size(), 1u);
            }
        }
    }
}

TEST_F(StellarDownloadTest, RecentDerivedExoplanetCachesWorkWithoutTheJsonSource)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = false;
    class DownloadReader : public TestCatalogReader
    {
    public:
        int requests = 0;
    protected:
        bool fetch_stellar_catalog_json(const std::string&, const std::string&, json&) override
        {
            ++requests;
            return false;
        }
    };
    for (bool stars_only : {false, true})
    {
        SCOPED_TRACE(stars_only);
        delete_the_universe();
        make_star("Sol");
        Star* host = make_star("Snapshot host");
        ExoRow row;
        row.hostname = "Snapshot host";
        row.pl_name = "Snapshot host b";
        row.pl_orbper = 20;
        row.pl_orbsmax = .2;
        row.pl_bmasse = row.pl_rade = 1;
        row.sy_dist = 10;
        row.ra = 10;
        row.dec = 20;
        const char* path = stars_only ? "catalogs/exoplanets_stars.dat" : "catalogs/exoplanets_planets.dat";
        FILE* cache = fopen(path, "wb");
        ASSERT_NE(cache, nullptr);
        fputs("# EXO_TRANSIT7\n", cache);
        TestCatalogReader::exorow_write_line(cache, row);
        fclose(cache);
        DownloadReader reader;
        reader.load_exoplanets_from_tap(stars_only);
        EXPECT_EQ(reader.requests, 0);
        EXPECT_EQ(host->pl_indices.size(), stars_only ? 0u : 1u);
        EXPECT_FALSE(std::filesystem::exists("catalogs/exoplanets.json"));
        std::filesystem::remove(path);
    }
}

TEST_F(StellarDownloadTest, ExoplanetRefreshStartsOnlyAfterAllFilesExpire)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = false;
    class DownloadReader : public CatalogReader
    {
    public:
        int requests = 0;
    protected:
        bool fetch_stellar_catalog_json(const std::string&, const std::string&, json&) override
        {
            ++requests;
            return false;
        }
    };
    const char* paths[] =
    {
        "catalogs/exoplanets.json", "catalogs/exoplanets_stars.dat", "catalogs/exoplanets_planets.dat"
    };
    for (int age_hours : {24, 25, 0})
    {
        SCOPED_TRACE(age_hours);
        const auto now = std::filesystem::file_time_type::clock::now();
        for (const char* path : paths)
        {
            if (age_hours)
            {
                std::ofstream(path) << "[]";
                std::filesystem::last_write_time(path, now - std::chrono::hours(age_hours));
            }
            else std::filesystem::remove(path);
        }
        DownloadReader reader;
        reader.load_exoplanets_from_tap(true);
        EXPECT_EQ(reader.requests, 4);
        reader.load_exoplanets_from_tap(false);
        EXPECT_EQ(reader.requests, 4);
    }
}

TEST_F(StellarDownloadTest, ExoplanetRefreshCombinesFreshAndSavedSources)
{
    struct OfflineContext
    {
        bool original = radio_silence;
        ~OfflineContext() { radio_silence = original; }
    } context;
    radio_silence = false;
    class DownloadReader : public CatalogReader
    {
    public:
        int scenario = 0, requests = 0;
    protected:
        bool fetch_stellar_catalog_json(const std::string& url, const std::string& form, json& result) override
        {
            ++requests;
            const bool nasa = url.find("exoplanetarchive") != std::string::npos;
            if ((scenario == 0 && nasa) || (scenario == 1 && !nasa)) return false;
            if (scenario == 5 && form.empty()) return false;
            if (scenario == 3)
            {
                abort_load = true;
                return false;
            }
            if (nasa)
            {
                result = json::array({{{"pl_name", "NASA host b"}, {"hostname", "NASA host"},
                    {"pl_orbper", 10}, {"sy_dist", 10}, {"ra", 10}, {"dec", 20}}});
            }
            else
            {
                result =
                {
                    {"metadata", json::array({{{"name", "pl_name"}}, {{"name", "hostname"}},
                        {{"name", "pl_orbper"}}, {{"name", "sy_dist"}}, {{"name", "ra"}}, {{"name", "dec"}},
                        {{"name", "alt_target_name"}, {"datatype", "char"}}})},
                    {"data", json::array({json::array({"EU host b", "EU host", 30, 20, 20, 30, nullptr})})}
                };
                if (scenario == 2) result["data"][0].erase(result["data"][0].end() - 1);
            }
            return true;
        }
    };
    const json saved = json::array({{{"pl_name", "Saved host b"}, {"hostname", "Saved host"},
        {"pl_orbper", 20}, {"sy_dist", 10}, {"ra", 10}, {"dec", 20}, {"source_catalog", "NASA"}}});
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        SCOPED_TRACE(scenario);
        abort_load = false;
        std::filesystem::remove("catalogs/exoplanets_nasa.json");
        std::filesystem::remove("catalogs/exoplanets_eu.json");
        std::ofstream("catalogs/exoplanets.json") << saved;
        std::filesystem::last_write_time("catalogs/exoplanets.json",
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(24));
        std::filesystem::remove("catalogs/exoplanets_stars.dat");
        DownloadReader reader;
        reader.scenario = scenario;
        reader.load_exoplanets_from_tap(true);
        json persisted;
        std::ifstream("catalogs/exoplanets.json") >> persisted;
        if (scenario == 3) EXPECT_EQ(persisted, saved);
        else
        {
            ASSERT_EQ(persisted.size(), scenario == 1 || scenario == 2 ? 1u : 2u);
            std::set<std::string> sources;
            for (const auto& row : persisted) sources.insert(row["source_catalog"]);
            EXPECT_EQ(sources, (scenario == 1 || scenario == 2
                ? std::set<std::string>{"NASA"} : std::set<std::string>{"NASA", "EU"}));
            for (const auto& row : persisted)
                EXPECT_EQ(row.at("pl_name"), scenario == 0 && row.at("source_catalog") == "NASA"
                    ? "Saved host b" : row.at("source_catalog") == "NASA" ? "NASA host b" : "EU host b");
        }
        EXPECT_FALSE(std::filesystem::exists("catalogs/exoplanets.json.tmp"));
        if (scenario == 4)
        {
            std::filesystem::remove("catalogs/exoplanets.json");
            std::filesystem::remove("catalogs/exoplanets_stars.dat");
            DownloadReader recovery;
            recovery.scenario = 3;
            recovery.load_exoplanets_from_tap(true);
            EXPECT_EQ(recovery.requests, 0);
            json rebuilt;
            std::ifstream("catalogs/exoplanets.json") >> rebuilt;
            EXPECT_EQ(rebuilt, persisted);
        }
        abort_load = false;
        delete_the_universe();
        make_star("Sol");
    }
}

TEST_F(StellarDownloadTest, SourceRefreshUsesIndependentAgesAndRetainsOldMeasurements)
{
    struct RestoreNetwork
    {
        bool saved = radio_silence;
        ~RestoreNetwork() { radio_silence = saved; }
    } restore;
    radio_silence = false;
    class DownloadReader : public CatalogReader
    {
    public:
        bool fail_nasa = false;
        std::vector<std::string> requests;
    protected:
        bool fetch_stellar_catalog_json(const std::string& url, const std::string&, json& result) override
        {
            const bool nasa = url.find("exoplanetarchive") != std::string::npos;
            requests.push_back(nasa ? "NASA" : "EU");
            if (nasa == fail_nasa) return false;
            result = json::array({{{"pl_name", nasa ? "NASA fresh b" : "EU fresh b"},
                {"hostname", nasa ? "NASA fresh" : "EU fresh"}, {"pl_orbper", 10},
                {"sy_dist", 10}, {"ra", 10}, {"dec", 20}}});
            return true;
        }
    };
    for (bool fail_nasa : {false, true})
    {
        SCOPED_TRACE(fail_nasa);
        const json old_nasa = json::array({{{"pl_name", "NASA old b"}, {"hostname", "NASA old"},
            {"pl_orbper", 20}, {"sy_dist", 10}, {"ra", 11}, {"dec", 21}}});
        const json old_eu = json::array({{{"pl_name", "EU old b"}, {"hostname", "EU old"},
            {"pl_orbper", 30}, {"sy_dist", 10}, {"ra", 12}, {"dec", 22}}});
        std::ofstream("catalogs/exoplanets_nasa.json") << old_nasa;
        std::ofstream("catalogs/exoplanets_eu.json") << old_eu;
        const auto week_old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(168);
        for (const char* path : {"catalogs/exoplanets_nasa.json", "catalogs/exoplanets_eu.json"})
            std::filesystem::last_write_time(path, week_old);
        // A recent combined catalog must not conceal an old failed source.
        std::ofstream("catalogs/exoplanets.json") << "[]";
        DownloadReader reader;
        reader.fail_nasa = fail_nasa;
        reader.load_exoplanets_from_tap(true);
        json combined, retained;
        std::ifstream("catalogs/exoplanets.json") >> combined;
        ASSERT_EQ(combined.size(), 2u);
        std::set<std::string> names;
        for (const auto& row : combined) names.insert(row.at("pl_name"));
        EXPECT_EQ(names, (fail_nasa ? std::set<std::string>{"NASA old b", "EU fresh b"}
            : std::set<std::string>{"NASA fresh b", "EU old b"}));
        const char* failed = fail_nasa ? "catalogs/exoplanets_nasa.json" : "catalogs/exoplanets_eu.json";
        std::ifstream(failed) >> retained;
        EXPECT_EQ(retained, fail_nasa ? old_nasa : old_eu);
        EXPECT_EQ(std::filesystem::last_write_time(failed), week_old);
        DownloadReader retry;
        retry.fail_nasa = fail_nasa;
        retry.load_exoplanets_from_tap(true);
        EXPECT_EQ(retry.requests, (std::vector<std::string>{fail_nasa ? "NASA" : "EU", fail_nasa ? "NASA" : "EU"}));
        delete_the_universe();
        make_star("Sol");
    }
}

TEST_F(CatalogParsingTest, TestLoadExoplanetsFromTap)
{
    CatalogReader cr;
    radio_silence = true;

    if (!hdcache) hdcache = new Star*[MAX_HD + 1]();

    Star* star_82eri = make_star("82 Eri");
    star_82eri->HD = 20794;
    hdcache[20794] = star_82eri;

    Star* star_epseri = make_star("eps Eri");
    star_epseri->HD = 22049;
    hdcache[22049] = star_epseri;

    // Force the refresh path, then exercise the resulting cache on the second pass.
    auto supplement_time = std::filesystem::last_write_time("catalogs/exoplanet_host_positions.dat");
    const char* cache_paths[] =
    {
        "catalogs/exoplanets_stars.dat", "catalogs/exoplanets_planets.dat"
    };
    for (const char* path : cache_paths)
    {
        if (std::filesystem::exists(path))
        {
            std::filesystem::last_write_time(path, supplement_time - std::chrono::seconds(1));
        }
    }

    cr.load_exoplanets_from_tap(true);
    auto positions = TestCatalogReader::read_exoplanet_host_positions("catalogs/exoplanet_host_positions.dat");
    ASSERT_EQ(positions.size(), 52u);
    std::map<std::string, ExoRow> cached_hosts;
    FILE* star_cache = fopen("catalogs/exoplanets_stars.dat", "rb");
    ASSERT_NE(star_cache, nullptr);
    ExoRow cached_row;
    while (TestCatalogReader::exorow_read_line(star_cache, cached_row))
    {
        cached_hosts[cached_row.hostname] = cached_row;
    }
    fclose(star_cache);
    ASSERT_TRUE(cached_hosts.count("KOI-5"));
    EXPECT_TRUE(cached_hosts.at("KOI-5").pl_name.empty());
    EXPECT_NEAR(cached_hosts.at("KOI-5").sy_vmag, 11.713, 1e-5);
    for (const auto& entry : positions)
    {
        SCOPED_TRACE(entry.first);
        ASSERT_TRUE(cached_hosts.count(entry.first));
        EXPECT_NEAR(cached_hosts[entry.first].sy_dist, entry.second.sy_dist, 1e-3);
        Star* recovered = nullptr;
        for (int i = 0; i < ncelobjs; ++i)
        {
            if (cels[i]->typeclass() == class_star && entry.first == cels[i]->name)
            {
                recovered = static_cast<Star*>(cels[i]);
                break;
            }
        }
        ASSERT_NE(recovered, nullptr);
        EXPECT_TRUE(recovered->distance_known);
        EXPECT_NEAR(recovered->distance / parsec, entry.second.sy_dist, 1e-3);
    }
    int star_count = ncelobjs;
    cr.load_exoplanets_from_tap(true);
    EXPECT_EQ(ncelobjs, star_count);
    cr.read_star_orbits_dat(cels);
    unsigned int nexo = cr.load_exoplanets_from_tap();
    EXPECT_GT(nexo, 5000u);
    json unified;
    std::ifstream("catalogs/exoplanets.json") >> unified;
    for (const auto& row : unified)
    {
        if (row.value("hostname", "") == "KOI-5")
        {
            EXPECT_EQ(row.value("source_catalog", ""), "NASA");
        }
    }
    for (int i = 0; i < ncelobjs; ++i)
    {
        EXPECT_STRNE(cels[i]->name, "KOI-5 b");
    }

    // Use an imaging planet present in the source snapshot for physical checks.
    // Inspect source designations directly, independently of display names and search filters.
    auto imported_planet = [](const char* designation)
    {
        for (int i = 0; i < ncelobjs; ++i)
        {
            if (cels[i]->typeclass() == class_planet &&
                static_cast<Planet*>(cels[i])->catalog_designation == designation) return i;
        }
        return -1;
    };
    int idx_imaging = imported_planet("HIP 65426 b");
    EXPECT_GE(idx_imaging, 0);
    if (idx_imaging >= 0)
    {
        ASSERT_EQ(cels[idx_imaging]->typeclass(), class_planet);
        Planet* imaging = static_cast<Planet*>(cels[idx_imaging]);
        ASSERT_NE(imaging->cenobj, nullptr);
        EXPECT_STREQ(imaging->cenobj->name, "HIP 65426");
        ASSERT_NE(imaging->orbit, nullptr);
        EXPECT_NEAR(imaging->orbit->semimajor_axis / AU, 92.0, 0.1);
        EXPECT_GT(imaging->mass, 0);
    }

    EXPECT_GE(imported_planet("WD 0806-661 b"), 0);

    int idx_b = find_object("PDS 70 b", false);
    EXPECT_GE(idx_b, 0);

    int idx_c = find_object("PDS 70 c", false);
    EXPECT_GE(idx_c, 0);

    // Verify AEgir (eps Eri b) was matched from exoname.dat
    int idx_aegir = find_object("AEgir", false);
    EXPECT_GE(idx_aegir, 0);
    if (idx_aegir >= 0)
    {
        Planet* pa = (Planet*)cels[idx_aegir];
        ASSERT_NE(pa, nullptr);
        EXPECT_STREQ(pa->name, "AEgir");
    }

    // Verify 82 Eridani system
    int idx_82b = find_object("82 Eri b", false);
    EXPECT_GE(idx_82b, 0);
    if (idx_82b >= 0)
    {
        Planet* p = (Planet*)cels[idx_82b];
        ASSERT_NE(p->orbit, nullptr);
        Star* s = (Star*)p->cenobj;
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->has_disk);
        EXPECT_NEAR(s->planets_heliocen_inclination, s->disk_heliocen_inclination, 0.01);
        p->update_location(simnow);
        double czincl = 0, cznode = 0;
        incl_and_node_from_system_plane(p->location.orbital_plane, czincl, cznode, s->location.system_center);
        EXPECT_NEAR(czincl, s->disk_heliocen_inclination, 0.02);
        EXPECT_NEAR(p->orbit->period / oneday, 18.3, 0.5);
        EXPECT_NEAR(p->mass / earth_mass, 2.81, 0.2);
    }

    int idx_82c = find_object("82 Eri c", false);
    EXPECT_GE(idx_82c, 0);
    if (idx_82c >= 0)
    {
        Planet* p = (Planet*)cels[idx_82c];
        ASSERT_NE(p->orbit, nullptr);
        Star* s = (Star*)p->cenobj;
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->has_disk);
        p->update_location(simnow);
        double czincl = 0, cznode = 0;
        incl_and_node_from_system_plane(p->location.orbital_plane, czincl, cznode, s->location.system_center);
        EXPECT_NEAR(czincl, s->disk_heliocen_inclination, 0.02);
        EXPECT_NEAR(p->orbit->period / oneday, 89.7, 1.0);
        // Nari et al. (2025): minimum mass 2.98 Earth masses; disk inclination 50 degrees.
        EXPECT_NEAR(p->msini / earth_mass, 2.98, 0.01);
        EXPECT_NEAR(p->mass / earth_mass, 3.89, 0.02);
    }

    int idx_82e = find_object("82 Eri e", false);
    EXPECT_GE(idx_82e, 0);
    if (idx_82e >= 0)
    {
        Planet* p = (Planet*)cels[idx_82e];
        ASSERT_NE(p->orbit, nullptr);
        Star* s = (Star*)p->cenobj;
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->has_disk);
        p->update_location(simnow);
        double czincl = 0, cznode = 0;
        incl_and_node_from_system_plane(p->location.orbital_plane, czincl, cznode, s->location.system_center);
        EXPECT_NEAR(czincl, s->disk_heliocen_inclination, 0.02);
        EXPECT_NEAR(p->orbit->period / oneday, 147.0, 1.0);
        EXPECT_NEAR(p->mass / earth_mass, 6.23, 0.2);
    }

    int idx_82d = find_object("82 Eri d", false);
    EXPECT_GE(idx_82d, 0);
    if (idx_82d >= 0)
    {
        Planet* p = (Planet*)cels[idx_82d];
        ASSERT_NE(p->orbit, nullptr);
        Star* s = (Star*)p->cenobj;
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(s->has_disk);
        p->update_location(simnow);
        double czincl = 0, cznode = 0;
        incl_and_node_from_system_plane(p->location.orbital_plane, czincl, cznode, s->location.system_center);
        EXPECT_NEAR(czincl, s->disk_heliocen_inclination, 0.02);
        EXPECT_NEAR(p->orbit->period / oneday, 648.0, 5.0);
        EXPECT_NEAR(p->mass / earth_mass, 7.60, 0.2);
    }

    for (const char* letter : {"b", "c", "e", "d"})
    {
        const std::string source = std::string("GJ 139 ") + letter;
        const int planet = find_object(source.c_str(), false, 9e29, 0);
        EXPECT_GE(planet, 0);
        for (const char* alias : {"82 Eri", "HD 20794"})
        {
            const std::string query = std::string(alias) + " " + letter;
            EXPECT_EQ(find_object(query.c_str(), false, 9e29, 0), planet);
            EXPECT_EQ(find_object(query.c_str(), false), planet);
        }
    }
    hdcache[20794] = nullptr;
    hdcache[22049] = nullptr;
    delete_the_universe();
}

TEST_F(CatalogParsingTest, DedupPlanetsPreservesBinaryCompanions)
{
    // Test 1: Binary star with planet around member A and member B (identical orbit parameters)
    // MUST NOT MERGE!
    json binary_planets = json::array();

    json pl_a;
    pl_a["pl_name"] = "BinaryTest A b";
    pl_a["hostname"] = "BinaryTest A";
    pl_a["ra"] = 120.0;
    pl_a["dec"] = -30.0;
    pl_a["pl_orbper"] = 100.0;
    pl_a["pl_orbsmax"] = 1.0;
    binary_planets.push_back(pl_a);

    json pl_b;
    pl_b["pl_name"] = "BinaryTest B b";
    pl_b["hostname"] = "BinaryTest B";
    pl_b["ra"] = 120.0001;
    pl_b["dec"] = -30.0001;
    pl_b["pl_orbper"] = 100.0;
    pl_b["pl_orbsmax"] = 1.0;
    binary_planets.push_back(pl_b);

    CatalogReader::dedup_planets(binary_planets);
    EXPECT_EQ(binary_planets.size(), 2u);

    // Test 2: Binary star with unspecified orbits around member A and member B
    json binary_unspec = json::array();
    json unspec_a;
    unspec_a["pl_name"] = "2MASS J1450-7841 A";
    unspec_a["hostname"] = "2MASS J1450-7841 A";
    unspec_a["ra"] = 222.67;
    unspec_a["dec"] = -78.69;
    binary_unspec.push_back(unspec_a);

    json unspec_b;
    unspec_b["pl_name"] = "2MASS J1450-7841 B";
    unspec_b["hostname"] = "2MASS J1450-7841 B";
    unspec_b["ra"] = 222.67;
    unspec_b["dec"] = -78.69;
    binary_unspec.push_back(unspec_b);

    CatalogReader::dedup_planets(binary_unspec);
    EXPECT_EQ(binary_unspec.size(), 2u);

    // Test 3: Duplicate planet across catalogs (e.g. EU vs NASA) with differing identifiers
    json dup_planets = json::array();
    json pl_eu;
    pl_eu["pl_name"] = "Alpha Cet b";
    pl_eu["hostname"] = "Alpha Cet";
    pl_eu["ra"] = 45.0;
    pl_eu["dec"] = 10.0;
    pl_eu["pl_orbper"] = 50.0;
    pl_eu["pl_orbsmax"] = 0.5;
    dup_planets.push_back(pl_eu);

    json pl_nasa;
    pl_nasa["pl_name"] = "HD 12345 b";
    pl_nasa["hostname"] = "HD 12345";
    pl_nasa["hd_name"] = "HD 12345";
    pl_nasa["ra"] = 45.0002;
    pl_nasa["dec"] = 10.0001;
    pl_nasa["pl_orbper"] = 50.5; // within 15%
    pl_nasa["pl_orbsmax"] = 0.505;
    dup_planets.push_back(pl_nasa);

    CatalogReader::dedup_planets(dup_planets);
    EXPECT_EQ(dup_planets.size(), 1u);
    EXPECT_EQ(dup_planets[0]["pl_name"], "Alpha Cet b");
    EXPECT_EQ(dup_planets[0]["hd_name"], "HD 12345");
}

TEST_F(CatalogParsingTest, SpheroidGalaxyHasCatalogInclinationAndSpheroidType)
{
    CatalogReader cr;
    int n = cr.read_UNGC_catalog(cels, MAX_CELOBJS);
    ASSERT_GT(n, 0);

    Galaxy* sag = nullptr;
    for (int i = 0; cels[i]; i++)
    {
        if (!strcmp(cels[i]->name, "Sag dSph"))
        {
            sag = (Galaxy*)cels[i];
            break;
        }
    }

    ASSERT_NE(sag, nullptr);
    EXPECT_DOUBLE_EQ(sag->axis_ratio, 0.48);
    EXPECT_NEAR(sag->inclination, half_pi, 1e-4);
    EXPECT_DOUBLE_EQ(sag->morphological_T, -3.0);
    EXPECT_TRUE(sag->is_spheroidal_or_elliptical());
}

TEST_F(CatalogParsingTest, BackgroundAstorbLoaderLoadsAndLinks)
{
    start_astorb_background_load();
    EXPECT_TRUE(astorb_loading.load() || astorb_loaded.load());

    auto start_time = std::chrono::steady_clock::now();
    while (astorb_loading.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count();
        if (elapsed > 10)
        {
            break;
        }
    }

    if (astorb_thread.joinable())
    {
        astorb_thread.join();
    }

    EXPECT_TRUE(astorb_loaded.load());
    EXPECT_FALSE(astorb_loading.load());
    EXPECT_GT(astorb.size(), 1000000u);
    EXPECT_FLOAT_EQ(astorb_load_progress.load(), 1.0f);

    bool found_ceres = false;
    for (size_t i = 0; i < std::min((size_t)10, astorb.size()); i++)
    {
        if (astorb[i].number == 1)
        {
            EXPECT_EQ(astorb[i].name, "Ceres");
            EXPECT_GT(astorb[i].sma, 2.5f);
            found_ceres = true;
            break;
        }
    }
    EXPECT_TRUE(found_ceres);

    Planet* p = new Planet();
    p->asteroid_no = 1;
    strcpy(p->name, "Ceres");
    append_cel(p);

    link_astorb_with_cels();

    found_ceres = false;
    for (size_t i = 0; i < std::min((size_t)10, astorb.size()); i++)
    {
        if (astorb[i].number == 1)
        {
            EXPECT_EQ(astorb[i].cel, p);
            found_ceres = true;
            break;
        }
    }
    EXPECT_TRUE(found_ceres);
}

TEST_F(StellarDownloadTest, ApplyGaiaAstrometryUpdatesDistancesAndLuminosityClass)
{
    std::filesystem::copy_file(original / "catalogs/mainseq.dat", "catalogs/mainseq.dat");
    Star::load_main_seq_dat();
    std::filesystem::create_directories("catalogs/GAIA");
    {
        std::ofstream catalog("catalogs/GAIA/hgca_edr3.dat");
        catalog << "1|1001|||0|||5.4151||0|0\n"
                << "8|1008|||0|||1.5220||0|0\n";
    }
    CatalogReader cr;
    CelestialObject* test_cels[3] = { nullptr, nullptr, nullptr };

    Star* s1 = new Star();
    s1->HIP = 1;
    strcpy(s1->name, "HIP 1");
    s1->apparent_magnitude = 9.0;
    strcpy(s1->spectral_type, "F5V");
    s1->distance = 100.0 * parsec;
    test_cels[0] = s1;

    Star* s2 = new Star();
    s2->HIP = 8;
    strcpy(s2->name, "HIP 8");
    s2->apparent_magnitude = 4.0;
    strcpy(s2->spectral_type, "G2V");
    s2->distance = 10.0 * parsec;
    test_cels[1] = s2;

    Star unknown;
    unknown.absolute_magnitude = -3.5;
    unknown.make_companion_of(s1, 'B');
    Star measured_zero;
    measured_zero.apparent_magnitude = 0;
    measured_zero.make_companion_of(s1, 'C');

    int updated = cr.apply_gaia_astrometry(test_cels);
    EXPECT_EQ(updated, 2);

    double expected_dist_pc = 1000.0 / 5.4151;
    EXPECT_NEAR(s1->distance / parsec, expected_dist_pc, 0.5);
    EXPECT_TRUE(s1->distance_known);
    EXPECT_GT(s1->absolute_magnitude, 0.0);
    EXPECT_DOUBLE_EQ(unknown.absolute_magnitude, -3.5);
    EXPECT_DOUBLE_EQ(unknown.distance, s1->distance);
    EXPECT_NEAR(measured_zero.absolute_magnitude, -5 * log10(expected_dist_pc / 10), 0.001);

    // s2 (HIP 8) is at ~657 pc, M_V = -5.09 -> promoted to Supergiant Class I
    EXPECT_STREQ(s2->spectral_type, "G2I");

    delete s1;
    delete s2;
}

TEST_F(CatalogParsingTest, CondensedStarCatalogLoadsWithBlankAlienorumIds)
{
    CatalogReader cr;
    int num_loaded = cr.read_condensed_star_cat();
    EXPECT_GT(num_loaded, 100000);

    int blank_count = 0;
    int populated_count = 0;

    for (int i = 1; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            Star *s = (Star*)cels[i];
            // Reconstructed companions and remnants can lack an observed visual magnitude.
            if (s->apparent_magnitude >= 99)
            {
                EXPECT_DOUBLE_EQ(s->apparent_magnitude, 1e29);
                EXPECT_TRUE(std::isfinite(s->absolute_magnitude));
                continue;
            }
            if (s->alienorumid.empty())
            {
                blank_count++;
                if (!s->orbit && !s->variability_period && s->apparent_magnitude > 0.0)
                {
                    EXPECT_GE(s->apparent_magnitude, 10.0 - 0.001);
                }
            }
            else
            {
                populated_count++;
                if (!s->orbit && !s->variability_period)
                {
                    EXPECT_LT(s->apparent_magnitude, 10.0 + 0.001);
                }
            }
        }
    }

    read_cons_lines();
    Star* atlas = nullptr;
    Star* atlas_b = nullptr;
    for (int i = 0; cels[i] && i < MAX_CELOBJS; i++)
    {
        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (s->alienorumid == "3b Tau 2")
            {
                atlas = s;
            }
            if (s->alienorumid == "3b Tau 2 B")
            {
                atlas_b = s;
            }
        }
    }
    ASSERT_NE(atlas, nullptr);
    ASSERT_NE(atlas_b, nullptr);

    // Visiting Atlas in simulation must keep its Scorpius system plane locked
    mycenobj = atlas;
    whereami = atlas->seqno;
    atlas->update_location(simnow);
    atlas_b->update_location(simnow);

    EXPECT_TRUE(atlas->lock_system_plane);
    EXPECT_TRUE(atlas->lock_equatorial_plane);
    EXPECT_TRUE(atlas_b->lock_system_plane);
    EXPECT_TRUE(atlas_b->lock_equatorial_plane);

    // In Atlas's viewer plane, 27 Tau B's declination must be 0
    here = atlas->location;
    double b_dec = atlas_b->Decl_as_radians(here);
    EXPECT_NEAR(b_dec, 0.0, 1e-6);

    delete_the_universe();
}

// Line 44 of catalogs/Hipparcos/hip_dm_o.dat: Atlas (HIP 17847).
static const char hip_atlas_orbit[] =
    " 17847|  290.6598|  8305.6949|    4.23|0.0000|  0.00|106.72|111.54|  8.5642|  26.6108| 0.97|      |      | 25.02| 38.48|   | |111111110011|525549449401429329439470333497375483595461299556480454315439457515462440474413527568450450450450450450450450450450450450450450450450450537524464320471315486230450450358420471535483436 12308450450456";

TEST_F(CatalogParsingTest, HipparcosBinaryAtlasAndCompanionSharePlanes)
{
    CatalogReader cr;
    Star* atlas = make_star("Atlas");
    atlas->HIP = 17847;
    // Coordinates for Atlas (27 Tau) from Hipparcos
    atlas->right_ascension = 57.29054669 * fiftyseventh;
    atlas->declination = 24.05352412 * fiftyseventh;
    atlas->distance = 431.0 * light_year;
    atlas->location.system_center = Point::from_ra_dec(atlas->right_ascension, atlas->declination, atlas->distance);

    int offset = ncelobjs;
    ASSERT_TRUE(cr.load_Hipparcos_orbit_line(hip_atlas_orbit, cels, MAX_CELOBJS, offset));

    ASSERT_NE(atlas->multisys, nullptr);
    Star* atlas_b = atlas->multisys->get_member('B');
    ASSERT_NE(atlas_b, nullptr);

    // Visiting Atlas in simulation
    mycenobj = atlas;
    whereami = atlas->seqno;
    atlas->update_location(simnow);
    atlas_b->update_location(simnow);

    EXPECT_TRUE(atlas->known_poles);
    EXPECT_TRUE(atlas_b->known_poles);
    EXPECT_TRUE(atlas->lock_system_plane);
    EXPECT_TRUE(atlas->lock_equatorial_plane);
    EXPECT_TRUE(atlas_b->lock_system_plane);
    EXPECT_TRUE(atlas_b->lock_equatorial_plane);

    // Primary and companion must share the system plane
    EXPECT_NEAR(atlas->location.local_system_plane.a, atlas_b->location.local_system_plane.a, 1e-6);
    EXPECT_NEAR(atlas->location.local_system_plane.v.x, atlas_b->location.local_system_plane.v.x, 1e-6);
    EXPECT_NEAR(atlas->location.local_system_plane.v.y, atlas_b->location.local_system_plane.v.y, 1e-6);
    EXPECT_NEAR(atlas->location.local_system_plane.v.z, atlas_b->location.local_system_plane.v.z, 1e-6);

    // Primary and companion must share the orbital plane
    EXPECT_NEAR(atlas->location.orbital_plane.a, atlas_b->location.orbital_plane.a, 1e-6);
    EXPECT_NEAR(atlas->location.orbital_plane.v.x, atlas_b->location.orbital_plane.v.x, 1e-6);
    EXPECT_NEAR(atlas->location.orbital_plane.v.y, atlas_b->location.orbital_plane.v.y, 1e-6);
    EXPECT_NEAR(atlas->location.orbital_plane.v.z, atlas_b->location.orbital_plane.v.z, 1e-6);

    // Primary and companion must share equatorial pole orientation
    Point primary_pole = rotate3D(yaxis, center, atlas->location.equatorial_plane.v, atlas->location.equatorial_plane.a);
    Point comp_pole = rotate3D(yaxis, center, atlas_b->location.equatorial_plane.v, atlas_b->location.equatorial_plane.a);

    EXPECT_NEAR(primary_pole.x, comp_pole.x, 1e-4);
    EXPECT_NEAR(primary_pole.y, comp_pole.y, 1e-4);
    EXPECT_NEAR(primary_pole.z, comp_pole.z, 1e-4);

    // Verify pole points to Phoenix (RA ~ 27.96 deg / 01h 52m, Dec ~ -44.12 deg)
    double pole_ra = std::fmod(find_angle(primary_pole.z, -primary_pole.x) + _pi, _pi * 2);
    double pole_dec = find_angle(sqrt(primary_pole.x * primary_pole.x + primary_pole.z * primary_pole.z), primary_pole.y);
    if (pole_dec > half_pi)
    {
        pole_dec -= _pi * 2;
    }
    EXPECT_NEAR(pole_ra * fiftyseven, 27.96, 0.5);
    EXPECT_NEAR(pole_dec * fiftyseven, -44.12, 0.5);

    // Companion's pole must point to Phoenix and NOT Ursa Minor (+Y axis, Dec +90 deg)
    EXPECT_LT(comp_pole.y, 0.0);
    EXPECT_NEAR(comp_pole.y, primary_pole.y, 1e-4);

    // Viewed from Atlas in local view plane, 27 Tau B's orbit must lie on zero declination line
    here = atlas->location;
    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    for (int j = 0; j < 10; j++)
    {
        atlas_b->update_location(simnow + j * 86400 * 30);
        CelestialLocation orbrel = atlas_b->location - here;
        Point rel = rotate3D(Point(orbrel), center, viewer_plane.v, -viewer_plane.a);
        double dec = atlas_b->Decl_as_radians(here);

        EXPECT_NEAR(dec, 0.0, 1e-6);
        EXPECT_NEAR(rel.y / rel.magnitude(), 0.0, 1e-6);
    }
}




TEST_F(CatalogParsingTest, SourceCatalogWritesUnbinnedStarsExactlyOnce)
{
    struct RestoreDirectory
    {
        std::filesystem::path original, temporary;
        ~RestoreDirectory()
        {
            std::filesystem::current_path(original);
            std::filesystem::remove_all(temporary);
        }
    } restore{std::filesystem::current_path(), std::filesystem::temp_directory_path() /
        ("alienorum_catalog_writer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(restore.temporary / "catalogs");
    std::filesystem::current_path(restore.temporary);
    Star* primary = make_star("Writer Primary");
    primary->set_component('A', primary);
    Star* companion = make_star("Writer Companion");
    companion->make_companion_of(primary, 'B');
    make_star("Writer Unbinned");
    Star* fictional = make_star("Writer Fictional");
    fictional->fictional_origin = {{"test", true}};
    Constellation constellation;
    ConsBins bins;
    bins[&constellation][4]['y'] = {primary, primary};
    CatalogReader reader;
    EXPECT_EQ(reader.write_condensed_star_cat(bins), 4);
    std::ifstream input("catalogs/soles_alienorum.dat");
    std::map<std::string, int> names;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line[0] != '#') ++names[trim(line.substr(15, 39))];
    }
    EXPECT_EQ(names.size(), 4u);
    EXPECT_EQ(names["Sol"], 1);
    EXPECT_EQ(names["Writer Primary"], 1);
    EXPECT_EQ(names["Writer Companion"], 1);
    EXPECT_EQ(names["Writer Unbinned"], 1);
    EXPECT_EQ(names.count("Writer Fictional"), 0u);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, HipparcosOrbitPreservesMeasuredAndUnknownPhotometry)
{
    CatalogReader reader;
    Star* primary = make_star("Photometry A");
    primary->HIP = 12345;
    primary->distance = 20 * parsec;
    primary->distance_known = true;
    primary->absolute_magnitude = 4;
    Star* companion = make_star("Photometry B");
    companion->make_companion_of(primary, 'B');
    const double unknown = companion->apparent_magnitude;
    std::string row = hip_atlas_orbit;
    row.replace(0, 6, " 12345");
    Star** saved = hipcache;
    std::vector<Star*> cache(MAX_HIP + 1, nullptr);
    cache[12345] = primary;
    hipcache = cache.data();
    struct Restore
    {
        Star** value;
        ~Restore() { hipcache = value; }
    } restore{saved};
    int offset = ncelobjs;
    for (double magnitude : {8.0, 0.0, unknown})
    {
        companion->apparent_magnitude = magnitude;
        companion->absolute_magnitude = -3.5;
        EXPECT_TRUE(reader.load_Hipparcos_orbit_line(row.c_str(), cels, MAX_CELOBJS, offset));
        EXPECT_EQ(companion->apparent_magnitude, magnitude);
        EXPECT_DOUBLE_EQ(companion->absolute_magnitude, -3.5);
        EXPECT_EQ(primary->multisys->get_member('B'), companion);
    }
    delete_the_universe();
}

class CatalogPhotometryRegressionTest : public UniverseFixture {};

TEST_F(CatalogPhotometryRegressionTest, EarthViewNeverGreatlyOutshinesCatalogPhotometry)
{
    struct RestoreCatalogState
    {
        Star** hd = hdcache;
        Star** hip = hipcache;
        std::map<std::string, Star*> dm = std::move(dmcache);
        double time = simnow;
        bool offline = radio_silence;
        ~RestoreCatalogState()
        {
            for (int i = 0; cels[i]; ++i) delete cels[i];
            cels[0] = nullptr;
            ncelobjs = 0;
            hdcache = hd;
            hipcache = hip;
            dmcache = std::move(dm);
            simnow = time;
            radio_silence = offline;
        }
    };
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    RestoreCatalogState restore;
    hdcache = hd.data();
    hipcache = hip.data();
    dmcache.clear();
    radio_silence = true;
    Star::load_main_seq_dat();
    Star* sun = make_star("Sun");
    sun->distance = 0;
    CatalogReader reader;
    ASSERT_GT(reader.read_condensed_star_cat(), 100000);
    const int stars_end = ncelobjs;
    ASSERT_GT(reader.read_UNGC_catalog(cels, MAX_CELOBJS), 0);
    ASSERT_GT(reader.read_RC3_catalog(cels, MAX_CELOBJS), 1000);

    // Keep independent reference values before variable stars update their instantaneous V.
    std::vector<double> reference(ncelobjs, NAN), tolerance(ncelobjs, 0.5);
    int star_count = 0, galaxy_count = 0, variable_count = 0;
    for (int i = 1; i < ncelobjs; ++i)
    {
        const cel_obj_class cls = cels[i]->typeclass();
        const double catalog = cls == class_star ? static_cast<Star*>(cels[i])->apparent_magnitude
            : cls == class_galaxy ? static_cast<Galaxy*>(cels[i])->apparent_magnitude : NAN;
        if (!std::isfinite(catalog) || catalog <= -40 || catalog >= 60) continue;
        if (cels[i]->typeclass() == class_star)
        {
            Star* star = static_cast<Star*>(cels[i]);
            reference[i] = catalog;
            if (star->variability_period > 0 && std::isfinite(star->minmag)
                && star->minmag > -40 && star->minmag < 60)
            {
                reference[i] = std::min(reference[i], star->minmag);
                ++variable_count;
            }
            // Gaia-derived V estimates have a larger color-transformation uncertainty.
            if (star->apparent_magnitude_estimated) tolerance[i] = 1.5;
            ++star_count;
        }
        else if (cels[i]->typeclass() == class_galaxy && catalog != 0)
        {
            reference[i] = catalog;
            // UNGC and RC3 supply integrated B photometry, allowing B-V differences.
            tolerance[i] = 1.5;
            ++galaxy_count;
        }
    }
    EXPECT_GT(star_count, 100000);
    EXPECT_GT(galaxy_count, 1000);
    EXPECT_GT(variable_count, 100);
    int violations = 0;
    std::ostringstream examples;
    for (double time : {static_cast<double>(J2000_TIME_T), restore.time, restore.time + oneyear})
    {
        simnow = time;
        for (int i = 1; i < stars_end; ++i)
            if (cels[i]->typeclass() == class_star) static_cast<Star*>(cels[i])->update_location(simnow);
        CelestialLocation earth = sun->location;
        earth.local_position += Point(AU, 0, 0);
        for (int i = 1; i < ncelobjs; ++i)
        {
            if (!std::isfinite(reference[i])) continue;
            const double displayed = cels[i]->viewer_magnitude(earth);
            if (!std::isfinite(displayed) || displayed < reference[i] - tolerance[i])
            {
                if (violations < 12)
                    examples << " index=" << i << " catalog=" << reference[i] << " displayed=" << displayed;
                ++violations;
            }
        }
    }
    EXPECT_EQ(violations, 0) << examples.str();
    std::cout << "Earth photometry coverage: " << star_count << " stars, " << galaxy_count
        << " galaxies, " << variable_count << " variable stars, three epochs." << std::endl;
}

TEST_F(CatalogParsingTest, Authored82EridaniLettersSurviveCachePassesAndRetainSourceIdentity)
{
    for (bool cached : {false, true})
    {
        Star* host = make_star("82 Eridani");
        host->HD = 20794;
        strcpy(host->Gliese, "GJ 139");
        strcpy(host->Flamsteed, "82 Eri");
        host->catalog_aliases.push_back("GJ 139");
        host->catalog_aliases.push_back("82 Eri");
        host->catalog_aliases.push_back("HD 20794");
        host->has_disk = true;
        host->disk_heliocen_inclination = 50 * fiftyseventh;
        TestCatalogReader reader;
        std::map<int, std::vector<int>> planets;
        unsigned int count = 0;
        const char* source_letters[] = {"b", "d", "e", "f"};
        const char* display_letters[] = {"b", "c", "e", "d"};
        const double periods[] = {18.3, 89.7, 147, 648};
        for (int i = 0; i < 4; i++)
        {
            ExoRow row;
            row.hostname = "GJ 139";
            row.hd_name = "HD 20794";
            row.pl_name = std::string(cached ? "HD 20794 " : "GJ 139 ") + source_letters[i];
            row.pl_orbper = periods[i];
            row.pl_msinie = 3;
            if (cached)
            {
                FILE* cache = std::tmpfile();
                ASSERT_NE(cache, nullptr);
                reader.exorow_write_line(cache, row);
                rewind(cache);
                ExoRow restored;
                const bool valid = reader.exorow_read_line(cache, restored);
                fclose(cache);
                ASSERT_TRUE(valid);
                row = restored;
            }
            ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
        }
        ASSERT_EQ(count, 4u);
        for (int pass = 0; pass < 3; pass++)
        {
            reader.apply_exoplanet_names(planets);
            ASSERT_EQ(host->pl_indices.size(), 4u);
            for (int i = 0; i < 4; i++)
            {
                const int index = host->pl_indices[i];
                Planet* planet = static_cast<Planet*>(cels[index]);
                for (const char* alias : {"82 Eridani", "82 Eri", "HD 20794", "GJ 139"})
                {
                    const std::string query = std::string(alias) + " " + display_letters[i];
                    EXPECT_EQ(find_object(query.c_str(), false, 9e29, 0), index) << query;
                    EXPECT_EQ(find_object(query.c_str(), false), index) << query;
                }
                EXPECT_NEAR(planet->orbit->period / oneday, periods[i], 1e-6);
                EXPECT_EQ(planet->catalog_designation,
                    std::string(cached ? "HD 20794 " : "GJ 139 ") + source_letters[i]);
                planet->update_location(simnow);
                double inclination = 0, node = 0;
                incl_and_node_from_system_plane(planet->location.orbital_plane, inclination, node,
                    host->location.system_center);
                EXPECT_NEAR(inclination * fiftyseven, 50, 1e-5);
                Planet restored;
                ASSERT_TRUE(restored.from_json(planet->to_json()));
                EXPECT_EQ(restored.catalog_designation, planet->catalog_designation);
                EXPECT_EQ(restored.display_designation, planet->display_designation);
                EXPECT_STREQ(restored.name, planet->name);
            }
        }
        delete_the_universe();
        make_star("Sol");
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, MuAraeNamesMatchWikipediaPhysicalOrbitsAcrossSourceLetters)
{
    // Wikipedia's Mu Arae system table, checked 2026-10-23; periods here are the
    // local NASA snapshot's values, including its different source letters.
    const double periods[] = {9.6392, 307.9, 645.0, 3947.0};
    const char* names[] = {"Dulcinea", "Rocinante", "Quijote", "Sancho"};
    const char* canonical[] = {"c", "d", "b", "e"};
    const char* nasa[] = {"d", "e", "b", "c"};
    for (bool cached : {false, true})
    {
        Star* host = make_star("mu Ara");
        host->HD = 160691;
        strcpy(host->Gliese, "GJ 691");
        strcpy(host->Bayer, "mu Ara");
        for (const char* alias : {"Mu Arae", "HD 160691", "GJ 691"}) host->catalog_aliases.push_back(alias);
        TestCatalogReader reader;
        std::map<int, std::vector<int>> planets;
        unsigned int count = 0;
        for (int i = 0; i < 4; i++)
        {
            ExoRow row;
            row.hostname = cached ? "HD 160691" : "GJ 691";
            row.pl_name = row.hostname + " " + (cached ? canonical[i] : nasa[i]);
            row.pl_orbper = periods[i];
            row.pl_msinij = i == 0 ? 0.03 : 1.0;
            if (cached)
            {
                FILE* cache = std::tmpfile();
                ASSERT_NE(cache, nullptr);
                reader.exorow_write_line(cache, row);
                rewind(cache);
                ExoRow restored;
                const bool valid = reader.exorow_read_line(cache, restored);
                fclose(cache);
                ASSERT_TRUE(valid);
                row = restored;
            }
            ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
        }
        for (int pass = 0; pass < 3; pass++)
        {
            reader.apply_exoplanet_names(planets);
            ASSERT_EQ(host->pl_indices.size(), 4u);
            for (int i = 0; i < 4; i++)
            {
                const int index = host->pl_indices[i];
                Planet* planet = static_cast<Planet*>(cels[index]);
                EXPECT_STREQ(planet->name, names[i]);
                EXPECT_EQ(find_object(names[i], false, 9e29, 0), index);
                EXPECT_NEAR(planet->orbit->period / oneday, periods[i], 1e-6);
                for (const char* alias : {"mu Ara", "Mu Arae", "HD 160691", "GJ 691"})
                {
                    const std::string query = std::string(alias) + " " + canonical[i];
                    EXPECT_EQ(find_object(query.c_str(), false, 9e29, 0), index) << query;
                    EXPECT_EQ(find_object(query.c_str(), false), index) << query;
                }
                EXPECT_EQ(planet->catalog_designation,
                    std::string(cached ? "HD 160691 " : "GJ 691 ") + (cached ? canonical[i] : nasa[i]));
            }
        }
        delete_the_universe();
        make_star("Sol");
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, ReapplyingPlanetNamesPreservesOrbitFrame)
{
    Star* host = make_star("Frame regression host");
    host->has_disk = true;
    host->disk_heliocen_inclination = 50 * fiftyseventh;
    Planet* planet = make_planet(host, "Frame regression host b");
    planet->orbit->inclination = 70 * fiftyseventh;
    TestCatalogReader reader;
    const std::map<int, std::vector<int>> planets = {{host->seqno, {planet->seqno}}};
    reader.apply_exoplanet_names(planets);
    planet->update_location(simnow);
    const double inclination = planet->orbit->inclination;
    const double node = planet->orbit->ascending_node;
    const Point position = planet->location;
    for (int pass = 0; pass < 3; ++pass)
    {
        reader.apply_exoplanet_names(planets);
        planet->update_location(simnow);
        EXPECT_NEAR(planet->orbit->inclination, inclination, 1e-10);
        EXPECT_NEAR(planet->orbit->ascending_node, node, 1e-10);
        EXPECT_LT((Point(planet->location) - position).magnitude(), 0.001);
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, ExactEdgeOnCatalogPlaceholdersUseDiskForAnyHost)
{
    Star* host = make_star("Synthetic disk host");
    host->has_disk = true;
    host->disk_heliocen_inclination = 37 * fiftyseventh;
    TestCatalogReader reader;
    std::map<int, std::vector<int>> rows;
    unsigned int count = 0;
    for (int index = 0; index < 5; ++index)
    {
        json source = {{"hostname", host->name}, {"pl_name", "Synthetic planet " + std::to_string(index)},
            {"pl_orbper", 20 * pow(3, index)}, {"pl_msinie", 2},
            {"pl_orbincl", index == 1 ? 89.99 : 90.0}, {"tran_flag", index == 2 ? 1 : 0}};
        if (index == 2) source["tran_flag"] = true;
        if (index == 4)
        {
            source.erase("tran_flag");
            source["detection_type"] = "Radial Velocity, Primary Transit";
        }
        if (index == 3) source["pl_orbincl"] = nullptr;
        bool valid = false;
        ExoRow row = reader.exorow_from_json(source, &valid);
        ASSERT_TRUE(valid);
        FILE* cache = tmpfile();
        ASSERT_NE(cache, nullptr);
        reader.exorow_write_line(cache, row);
        rewind(cache);
        ExoRow restored;
        ASSERT_TRUE(reader.exorow_read_line(cache, restored));
        fclose(cache);
        EXPECT_EQ(restored.transiting, index == 2 || index == 4);
        ASSERT_TRUE(reader.add_exoplanet_from_row(restored, host, rows, count));
    }
    const double expected[] = {90, 89.99, 90, 90, 90};
    for (int pass = 0; pass < 3; ++pass)
    {
        reader.apply_exoplanet_names(rows);
        for (size_t i = 0; i < rows[host->seqno].size(); ++i)
        {
            Planet* planet = static_cast<Planet*>(cels[rows[host->seqno][i]]);
            planet->update_location(simnow);
            double inclination = 0, node = 0;
            incl_and_node_from_system_plane(planet->location.orbital_plane, inclination, node,
                host->location.system_center);
            EXPECT_NEAR(inclination * fiftyseven, expected[i], 1e-6);
            if (i == 0 || i == 3)
            {
                EXPECT_NEAR(planet->mass / earth_mass, 2, 1e-10);
            }
        }
    }
    delete_the_universe();
}

TEST_F(CatalogParsingTest, WideOrbitsWithoutPeriodsRemainValidForArbitraryNames)
{
    json wide = {{"pl_name", "Synthetic wide companion"}, {"hostname", "Synthetic primary"},
        {"pl_orbsmax", 450.0}, {"pl_orbper", nullptr}, {"ra", 12.0}, {"dec", 34.0}};
    json rows = json::array({wide});
    CatalogReader::dedup_planets(rows);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front()["pl_name"], wide["pl_name"]);
    EXPECT_DOUBLE_EQ(rows.front()["pl_orbsmax"], 450);
}

TEST_F(CatalogParsingTest, CurrentIauNamesRefreshCachedNamesAndPreserveComponentIdentity)
{
    Star *blaze = make_star("T CrB");
    blaze->HIP = 78322;
    blaze->has_custom_name = true;
    Star *recent = make_star("HIP 10324");
    recent->HIP = 10324;
    Star *authored = make_star("My authored label");
    authored->HIP = 10064;
    authored->has_custom_name = authored->user_edited = true;
    Star *primary = make_star("Old primary label");
    primary->HIP = 72105;
    primary->set_component('A', primary);
    Star *secondary = make_star("Old companion label", .4 * solar_mass);
    secondary->make_companion_of(primary, 'B');
    secondary->orbit->period = 123 * oneday;
    secondary->orbit->inclination = .7;
    Star *root = make_star("Betelgeuse");
    root->HIP = 27989;
    strcpy(root->Bayer, "Alp Ori");
    root->set_component('A', root);
    Star *child = make_star("Earlier B label");
    child->make_companion_of(root, 'B');
    child->has_custom_name = true;
    Star *campaign = make_star("51 Peg");
    campaign->HIP = 113357;
    class NamedHostReader : public CatalogReader
    {
        public:
        using CatalogReader::resolve_or_create_exostar;
    };
    NamedHostReader reader;
    EXPECT_GT(reader.read_starname_dat(cels), 0);
    EXPECT_STREQ(blaze->name, "Blaze Star");
    EXPECT_EQ(find_object("T CrB", true, 9e29, 0), blaze->seqno);
    EXPECT_STREQ(recent->name, "Bawy");
    EXPECT_STREQ(authored->name, "My authored label");
    EXPECT_EQ(find_object("Alaybasan", true, 9e29, 0), authored->seqno);
    EXPECT_STREQ(primary->name, "Izar");
    EXPECT_STREQ(secondary->name, "Pulcherrima");
    EXPECT_EQ(secondary->HIP, 0u);
    EXPECT_EQ(primary->HIP, 72105u);
    EXPECT_DOUBLE_EQ(secondary->mass, .4 * solar_mass);
    EXPECT_DOUBLE_EQ(secondary->orbit->period, 123 * oneday);
    EXPECT_DOUBLE_EQ(secondary->orbit->inclination, .7);
    EXPECT_STREQ(child->name, "Siwarha");
    EXPECT_STRNE(campaign->name, "Helvetios");
    ExoRow row;
    row.hostname = "Pulcherrima";
    bool was_new = true;
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &was_new), secondary);
    EXPECT_FALSE(was_new);
    EXPECT_DOUBLE_EQ(secondary->mass, .4 * solar_mass);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, RevisedSourceLettersAndPeriodsRepresentFourMuAraePlanets)
{
    json rows = json::array();
    const double nasa_periods[] = {645, 3947, 9.6392, 307.9};
    const double eu_periods[] = {643.25, 9.6386, 310.55, 4205.8};
    for (int i = 0; i < 4; ++i)
    {
        rows.push_back({{"pl_name", std::string("GJ 691 ") + char('b' + i)}, {"hostname", "GJ 691"},
            {"source_catalog", "NASA"}, {"ra", 266.0361585}, {"dec", -51.8348751}, {"pl_orbper", nasa_periods[i]}});
        rows.push_back({{"pl_name", std::string("mu Ara ") + char('b' + i)}, {"hostname", "mu Ara"},
            {"source_catalog", "EU"}, {"ra", 266.0333333}, {"dec", -51.8338889}, {"pl_orbper", eu_periods[i]}});
    }
    CatalogReader::dedup_planets(rows);
    ASSERT_EQ(rows.size(), 4u);
    for (const auto& row : rows) EXPECT_EQ(row["source_measurements"].size(), 2u);
    const json once = rows;
    CatalogReader::dedup_planets(rows);
    EXPECT_EQ(rows, once);
}

TEST_F(CatalogParsingTest, SourceIdentitySurvivesLargeOrbitRevisionsAndPrimaryComponentSpelling)
{
    json rows = json::array({
        {{"pl_name", "HD 12345 Ac"}, {"hostname", "HD 12345 A"}, {"source_catalog", "EU"}, {"pl_orbper", 100}},
        {{"pl_name", "HD 12345 c"}, {"hostname", "HD 12345"}, {"source_catalog", "NASA"}, {"pl_orbper", 400}}
    });
    CatalogReader::dedup_planets(rows);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_DOUBLE_EQ(rows[0]["pl_orbper"], 400);
    ASSERT_EQ(rows[0]["source_measurements"].size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0]["source_measurements"][0]["pl_orbper"], 100);
}

TEST_F(CatalogParsingTest, MatchingLettersCannotCollapseBodiesListedSeparatelyByEitherProvider)
{
    json rows = json::array({
        {{"pl_name", "TOI-406 c"}, {"hostname", "TOI-406"}, {"source_catalog", "NASA"}, {"pl_orbper", 3.307441},
            {"source_measurements", json::array({{{"source_catalog", "NASA"}, {"pl_name", "TOI-406 c"}},
                {{"source_catalog", "EU"}, {"pl_name", "TOI-406 b"}}})}},
        {{"pl_name", "TOI-406 c"}, {"hostname", "TOI-406"}, {"source_catalog", "EU"}, {"pl_orbper", 13.175682},
            {"source_measurements", json::array({{{"source_catalog", "NASA"}, {"pl_name", "TOI-406.01"}},
                {{"source_catalog", "EU"}, {"pl_name", "TOI-406 c"}}})}}
    });
    CatalogReader::dedup_planets(rows);
    EXPECT_EQ(rows.size(), 2u);
}

TEST_F(CatalogParsingTest, CrossSourceOrbitMatchingKeepsASeparateNearbyPeriod)
{
    json rows = json::array({
        {{"pl_name", "Host b"}, {"hostname", "Host"}, {"source_catalog", "NASA"}, {"pl_orbper", 100}},
        {{"pl_name", "Host c"}, {"hostname", "Host"}, {"source_catalog", "EU"}, {"pl_orbper", 101}},
        {{"pl_name", "Host d"}, {"hostname", "Host"}, {"source_catalog", "EU"}, {"pl_orbper", 110}}
    });
    CatalogReader::dedup_planets(rows);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[1]["pl_name"], "Host d");
    EXPECT_DOUBLE_EQ(rows[1]["pl_orbper"], 110);
}

TEST_F(StellarDownloadTest, EarlierDerivedCachesRebuildPlanetIdentityFromSavedSourceRows)
{
    struct RestoreNetwork
    {
        bool previous = radio_silence;
        ~RestoreNetwork() { radio_silence = previous; }
    } restore;
    radio_silence = true;
    Star* host = make_star("Revision host");
    const json rows = json::array({
        {{"pl_name", "Revision host b"}, {"hostname", "Revision host"}, {"source_catalog", "EU"},
            {"pl_orbper", 100}, {"ra", 10}, {"dec", 20}, {"sy_dist", 10}},
        {{"pl_name", "Revision host b"}, {"hostname", "Revision host"}, {"source_catalog", "NASA"},
            {"pl_orbper", 400}, {"ra", 10}, {"dec", 20}, {"sy_dist", 10}}
    });
    std::ofstream("catalogs/exoplanets.json") << rows;
    FILE* file = fopen("catalogs/exoplanets_planets.dat", "wb");
    ASSERT_NE(file, nullptr);
    fputs("# EXO_TRANSIT5\n", file);
    for (const auto& source : rows)
    {
        bool valid = false;
        const auto row = TestCatalogReader::exorow_from_json(source, &valid);
        ASSERT_TRUE(valid);
        TestCatalogReader::exorow_write_line(file, row);
    }
    fclose(file);
    CatalogReader reader;
    EXPECT_EQ(reader.load_exoplanets_from_tap(false), 1u);
    ASSERT_EQ(host->pl_indices.size(), 1u);
    EXPECT_NEAR(cels[host->pl_indices[0]]->orbit->period / oneday, 400, 1e-6);
}

TEST_F(CatalogParsingTest, SeparatePulsarsCannotMergeThroughAContradictorySemimajorAxis)
{
    json rows = json::array({
        {{"pl_name", "M62H b"}, {"hostname", "M62H"}, {"source_catalog", "EU"},
            {"ra", 255.3}, {"dec", -30.1}, {"pl_orbper", .132935028}, {"pl_orbsmax", .005}},
        {{"pl_name", "PSR J1701-3006F b"}, {"hostname", "PSR J1701-3006F"}, {"source_catalog", "NASA"},
            {"ra", 255.3}, {"dec", -30.1}, {"pl_orbper", .2}, {"pl_orbsmax", .005}}
    });
    CatalogReader::dedup_planets(rows);
    EXPECT_EQ(rows.size(), 2u);
}

TEST_F(CatalogParsingTest, AsteroidPhotometryDistinguishesZeroFromMissingAndMalformedFields)
{
    for (const std::string color : {"0.00", "0?  ", "    ", "oops"})
    {
        AstorbRow row;
        row.number = 1;
        row.name = "Photometry asteroid " + std::to_string(ncelobjs);
        row.diam = 848.4;
        row.incl = 10.588056;
        row.sma = 2.76577848;
        std::string buffer(astorb_ceres);
        buffer.replace(54, 4, color);
        ASSERT_TRUE(CatalogReader::load_asteroid(&row, buffer.data()));
        ASSERT_NE(row.cel, nullptr);
        const bool supplied = color.front() == '0';
        EXPECT_EQ(row.cel->is_bv_observed, supplied);
        EXPECT_DOUBLE_EQ(row.cel->BV_color, supplied ? 0 : .71);
    }
}

TEST_F(StellarRotationTest, LocalPhotometryMarksTheSunAndZeroValuesAndRecognizesOlderSaves)
{
    json records = json::array();
    records.push_back({{"BODYNAME", "Sol"}, {"BVmag", .653}});
    for (const char* name : {"Observed zero", "Unmeasured"})
    {
        json row = {{"BODYNAME", name}, {"CENTER_OF_ORBIT", "Sol"}, {"Mass", earth_mass / 1000},
            {"VolMeanRad", earth_radius}, {"SEMIMAJOR_AXIS", AU}, {"SurfacePressure", 0},
            {"SurfaceTemperature", 320}, {"HasWater", 0}};
        if (!strcmp(name, "Observed zero")) row["BVmag"] = "0?";
        records.push_back(row);
    }
    std::ofstream("catalogs/planets.json") << records.dump(2);
    CatalogReader reader;
    ASSERT_EQ(reader.read_local_planets(cels, MAX_CELOBJS), 2);
    EXPECT_TRUE(cels[0]->is_bv_observed);
    EXPECT_DOUBLE_EQ(cels[0]->BV_color, .653);
    auto* observed = static_cast<Planet*>(cels[1]);
    EXPECT_TRUE(observed->is_bv_observed);
    EXPECT_DOUBLE_EQ(observed->BV_color, 0);
    EXPECT_FALSE(cels[2]->is_bv_observed);
    json legacy = observed->to_json();
    legacy.erase("is_bv_observed");
    ASSERT_TRUE(observed->from_json(legacy));
    EXPECT_FALSE(observed->is_bv_observed);
    CatalogReader::apply_local_catalog_metadata(cels);
    EXPECT_TRUE(observed->is_bv_observed);
    EXPECT_DOUBLE_EQ(observed->BV_color, 0);
    observed->BV_color = .123;
    observed->is_bv_observed = false;
    CatalogReader::apply_local_catalog_metadata(cels);
    EXPECT_FALSE(observed->is_bv_observed);
    EXPECT_DOUBLE_EQ(observed->BV_color, .123);
}

class FictionalColorTestMap : public Map
{
public:
    FictionalColorTestMap()
    {
        image_width = 2;
        image_height = 1;
        allocated = 2;
        red_data = new unsigned char[2]{220, 220};
        green_data = new unsigned char[2]{180, 180};
        blue_data = new unsigned char[2]{120, 120};
        procedurally_generated = true;
        touch_gen();
    }
};

TEST_F(StellarRotationTest, SscAuthoredColorsRemainFictionalAndCanFollowNewTextures)
{
    std::ofstream file("fictional-color.ssc");
    file << "\"Fictional color\" \"Sol\"\n{\n"
        << " Radius 6000\n Mass 1\n Color [0.3 0.5 0.8]\n"
        << " EllipticalOrbit\n {\n SemiMajorAxis 1\n Period 2\n }\n}\n";
    file.close();
    SSCImport importer;
    ASSERT_TRUE(importer.read("fictional-color.ssc"));
    ASSERT_EQ(importer.report.bodies_added, 1);
    auto* body = static_cast<Planet*>(cels[1]);
    EXPECT_TRUE(body->lock_bv_color);
    EXPECT_FALSE(body->is_bv_observed);
    Planet restored;
    ASSERT_TRUE(restored.from_json(Serialization::expand_output_record(Serialization::output_record(body->to_json()))));
    EXPECT_FALSE(restored.is_bv_observed);
    const double authored = restored.BV_color;
    restored.surf_map = new FictionalColorTestMap;
    restored.update_texture_color();
    EXPECT_DOUBLE_EQ(restored.BV_color, restored.surf_map->texture_bv(&restored));
    EXPECT_NE(restored.BV_color, authored);
}

TEST_F(CatalogParsingTest, CancriTextureColorsReachNamedPlanets)
{
    bv_correction = -.6244;
    TestCatalogReader reader;
    Star* host = make_star("55 Cnc A");
    host->temperature = 5250;
    host->BV_color = -bv_correction;
    const char* names[] = {"55 Cnc Ae", "55 Cnc Ab", "55 Cnc Ac", "55 Cnc Af", "55 Cnc Ad"};
    for (const char* name : names)
    {
        Planet* body = make_planet(host, name);
        body->mass = name[8] == 'e' ? 8 * earth_mass : jupiter_mass;
        body->volumetric_mean_radius = name[8] == 'e' ? 12000000 : 70000000;
        body->rnd_seed = 42;
        body->fictitious_map_height = 32;
        body->has_water = 0;
        body->has_water_supplied = true;
        body->has_sim_life = false;
        body->ensure_atmosphere()->surface_pressure = 0;
        body->atm->pressure_supplied = true;
        reader.apply_exoplanet_names({{host->seqno, {body->seqno}}});
        SCOPED_TRACE(body->name);
        ASSERT_FALSE(body->is_bv_observed);
        body->ignore_map_files = true;
        load_textures(body);
        Map* map = uses_gaseous_map(body->type) ? body->cloud_map : body->surf_map;
        ASSERT_NE(map, nullptr);
        ASSERT_TRUE(map->is_complete());
        ASSERT_TRUE(map->procedurally_generated);
        EXPECT_DOUBLE_EQ(body->BV_color, map->texture_bv(body));
        if (uses_gaseous_map(body->type))
        {
            if (body->type == gas_giant) EXPECT_TRUE(body->has_spectral_giant_color());
            const Color actual = Color::color_from_planet(0, body);
            const Color expected = map->mean_texture_color(body);
            EXPECT_NEAR(actual.blue / actual.green, expected.blue / expected.green, 1e-12);
            EXPECT_NEAR(actual.red / actual.green, expected.red / expected.green, 1e-12);
        }
    }
}

TEST_F(CatalogParsingTest, BarnardAndGj3323CatalogPlanetsKeepTexturePhotometryThroughRegeneration)
{
    bv_correction = -.6244;
    Star::load_main_seq_dat();
    TestCatalogReader reader;
    FILE* file = fopen("catalogs/exoplanets_planets.dat", "r");
    ASSERT_NE(file, nullptr);
    std::vector<ExoRow> targets;
    ExoRow row;
    while (reader.exorow_read_line(file, row))
    {
        if (row.hostname.find("Barnard") != std::string::npos || row.pl_name == "GJ 3323 c") targets.push_back(row);
    }
    fclose(file);
    ASSERT_EQ(targets.size(), 5u);
    for (const auto& target : targets)
    {
        SCOPED_TRACE(target.pl_name);
        Star* host = make_star(target.hostname.c_str(), target.st_mass * solar_mass);
        host->temperature = target.st_teff;
        host->volumetric_mean_radius = target.st_rad * solar_radius;
        host->absolute_magnitude = target.sy_vmag - 5 * log10(target.sy_dist) + 5;
        host->distance = target.sy_dist * parsec;
        host->BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_sptyp(target.st_spectype.c_str()));
        snprintf(host->spectral_type, sizeof(host->spectral_type), "%s", target.st_spectype.c_str());
        reader.apply_exoplanet_metallicity(target, host);
        std::map<int, std::vector<int>> planets;
        unsigned int count = 0;
        ASSERT_TRUE(reader.add_exoplanet_from_row(target, host, planets, count));
        ASSERT_EQ(count, 1u);
        Planet* body = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
        ASSERT_FALSE(body->is_bv_observed);
        ASSERT_TRUE(uses_rocky_map(body->type));
        body->rnd_seed = 42;
        body->fictitious_map_height = 32;
        body->preserve_loaded_parameters = true;
        double first = NAN;
        for (int pass = 0; pass < 3; ++pass)
        {
            SCOPED_TRACE(pass);
            delete body->surf_map;
            delete body->cloud_map;
            body->surf_map = body->cloud_map = nullptr;
            body->ignore_map_files = true;
            load_textures(body);
            Map* map = body->get_day_map();
            ASSERT_NE(map, nullptr);
            ASSERT_TRUE(map->is_complete());
            EXPECT_DOUBLE_EQ(body->BV_color, map->texture_bv(body));
            const Color glare = Color::color_from_planet(0, body, 0, false);
            const Color mean = map->mean_texture_color(body);
            const Color daylight = Color::reflected_daylight(host);
            EXPECT_NEAR(glare.red / glare.green, mean.red / mean.green * daylight.red / daylight.green, 1e-12);
            EXPECT_NEAR(glare.blue / glare.green, mean.blue / mean.green * daylight.blue / daylight.green, 1e-12);
            if (!pass) first = body->BV_color;
            else EXPECT_NEAR(body->BV_color, first, .03);
            ASSERT_TRUE(body->from_json(body->to_json()));
            // Universe loading reconnects orbit parents after parsing each record.
            body->orbit->center = host;
        }
    }
}

TEST_F(CatalogParsingTest, CatalogMoonRetainsObservedColorAndGlareWithItsActualTexture)
{
    bv_correction = -.6244;
    snprintf(cels[0]->name, sizeof(cels[0]->name), "Sun");
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS), 0);
    Planet* moon = nullptr;
    for (int i = 0; cels[i]; ++i)
    {
        if (!strcmp(cels[i]->name, "Moon")) moon = static_cast<Planet*>(cels[i]);
    }
    ASSERT_NE(moon, nullptr);
    ASSERT_TRUE(moon->is_bv_observed);
    const double original_bv = moon->BV_color;
    const Color original = Color::color_from_planet(0, moon, planet_bv_correction);
    moon->surf_map = new Map(moon);
    ASSERT_TRUE(moon->surf_map->load_from_png("maps/Moon_surf.png"));
    ASSERT_TRUE(moon->surf_map->is_complete());
    moon->update_texture_color();
    const Color actual = Color::color_from_planet(0, moon, planet_bv_correction);
    EXPECT_DOUBLE_EQ(moon->BV_color, original_bv);
    EXPECT_DOUBLE_EQ(actual.red, original.red);
    EXPECT_DOUBLE_EQ(actual.green, original.green);
    EXPECT_DOUBLE_EQ(actual.blue, original.blue);
}

TEST_F(CatalogParsingTest, JanssenAndInnerTauCetiPlanetsIncludeAtmosphericScatteringInAutomaticGlare)
{
    const bool previous_clouds = show_clouds;
    struct RestoreClouds
    {
        bool value;
        ~RestoreClouds() { show_clouds = value; }
    } restore{previous_clouds};
    show_clouds = true;
    bv_correction = -.6244;
    Star::load_main_seq_dat();
    TestCatalogReader reader;
    FILE* file = fopen("catalogs/exoplanets_planets.dat", "r");
    ASSERT_NE(file, nullptr);
    std::vector<ExoRow> targets;
    ExoRow row;
    while (reader.exorow_read_line(file, row))
    {
        if (row.pl_name == "55 Cnc Ae" || row.pl_name == "tau Cet g" || row.pl_name == "tau Cet h") targets.push_back(row);
    }
    fclose(file);
    ASSERT_EQ(targets.size(), 3u);
    for (const auto& target : targets)
    {
        SCOPED_TRACE(target.pl_name);
        Star* host = make_star(target.hostname.c_str(), target.st_mass * solar_mass);
        host->temperature = target.st_teff;
        host->volumetric_mean_radius = target.st_rad * solar_radius;
        host->absolute_magnitude = target.sy_vmag - 5 * log10(target.sy_dist) + 5;
        host->BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_sptyp(target.st_spectype.c_str()));
        snprintf(host->spectral_type, sizeof(host->spectral_type), "%s", target.st_spectype.c_str());
        reader.apply_exoplanet_metallicity(target, host);
        std::map<int, std::vector<int>> planets;
        unsigned int count = 0;
        ASSERT_TRUE(reader.add_exoplanet_from_row(target, host, planets, count));
        reader.apply_exoplanet_names(planets);
        ASSERT_EQ(count, 1u);
        Planet* body = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
        ASSERT_TRUE(uses_rocky_map(body->type));
        body->rnd_seed = 42;
        body->fictitious_map_height = 32;
        body->ignore_map_files = true;
        load_textures(body);
        ASSERT_GT(body->get_surface_pressure(), 2e5);
        const double bv = body->BV_color;
        const Color surface = Color::color_from_planet(0, body, 0, false);
        const Color atmosphere = Color::color_from_planet(0, body);
        EXPECT_GT(atmosphere.blue / atmosphere.green, surface.blue / surface.green);
        EXPECT_DOUBLE_EQ(body->BV_color, bv);
        const Color repeat = Color::color_from_planet(0, body);
        EXPECT_DOUBLE_EQ(repeat.red, atmosphere.red);
        EXPECT_DOUBLE_EQ(repeat.green, atmosphere.green);
        EXPECT_DOUBLE_EQ(repeat.blue, atmosphere.blue);
    }
}

TEST_F(CatalogParsingTest, JanssenThermalRasterHasADayNightContrast)
{
    Star::load_main_seq_dat();
    TestCatalogReader reader;
    FILE* file = fopen("catalogs/exoplanets_planets.dat", "r");
    ASSERT_NE(file, nullptr);
    ExoRow row;
    while (reader.exorow_read_line(file, row) && row.pl_name != "55 Cnc Ae") {}
    fclose(file);
    ASSERT_EQ(row.pl_name, "55 Cnc Ae");
    Star* host = make_star(row.hostname.c_str(), row.st_mass * solar_mass);
    host->temperature = row.st_teff;
    host->volumetric_mean_radius = row.st_rad * solar_radius;
    host->absolute_magnitude = row.sy_vmag - 5 * std::log10(row.sy_dist) + 5;
    snprintf(host->spectral_type, sizeof(host->spectral_type), "%s", row.st_spectype.c_str());
    host->BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_sptyp(host->spectral_type));
    reader.apply_exoplanet_metallicity(row, host);
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
    ASSERT_EQ(count, 1u);
    auto* planet = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
    host->update_location(simnow);
    planet->update_location(simnow);
    ASSERT_TRUE(planet->is_tidal_locked());
    const double noon = _pi - planet->local_solar_time(0);
    const double latitude = host->Decl_as_radians(planet->location);
    const double day = planet->est_current_local_temp(latitude, noon, true);
    const double night = planet->est_current_local_temp(-latitude, noon + _pi, true);
    EXPECT_GT(day - night, 100) << "surface pressure " << planet->get_surface_pressure()
        << " Pa, atmospheric optical depth " << planet->get_atmospheric_tau();
    constexpr int width = 64, height = 32;
    std::array<float, width * height> raster{};
    planet->sample_current_local_temperatures(raster.data(), width, height);
    const auto [coldest, hottest] = std::minmax_element(raster.begin(), raster.end());
    EXPECT_GT(*hottest - *coldest, 100);
}

TEST_F(CatalogParsingTest, BrownCompanionAliasDoesNotCreateAPlanetAcrossCache)
{
    Star* host = make_star("eps Ind B");
    Star* companion = make_star("eps Ind C");
    companion->mass = .05*solar_mass;
    companion->temperature = 900;
    TestCatalogReader reader;
    bool valid = false;
    ExoRow row = reader.exorow_from_json({{"pl_name", "eps Ind Bb"},
        {"hostname", "eps Ind B"}, {"alt_target_name", "eps Ind C#GJ 845 Bb"},
        {"pl_orbper", 4022.19}, {"pl_bmassj", 53.25}}, &valid);
    ASSERT_TRUE(valid);
    for (bool cached : {false, true})
    {
        if (cached)
        {
            FILE* file = std::tmpfile();
            ASSERT_NE(file, nullptr);
            reader.exorow_write_line(file, row);
            rewind(file);
            ExoRow restored;
            ASSERT_TRUE(reader.exorow_read_line(file, restored));
            fclose(file);
            EXPECT_EQ(restored.companion_aliases, row.companion_aliases);
            row = restored;
        }
        std::map<int, std::vector<int>> planets;
        unsigned int count = 0;
        const int before = ncelobjs;
        EXPECT_FALSE(reader.add_exoplanet_from_row(row, host, planets, count));
        EXPECT_EQ(ncelobjs, before);
        EXPECT_EQ(count, 0u);
        EXPECT_TRUE(planets.empty());
        EXPECT_EQ(find_object("eps Ind Bb", true, 9e29, 0), companion->seqno);
    }
}

TEST_F(CatalogParsingTest, PlanetAliasesDoNotConsumeProximaBAndImagedPlanetReplacesDetachedDuplicate)
{
    make_star("Alpha Centauri");
    auto* proxima = make_star("Proxima Centauri", .1221 * solar_mass);
    auto* coconuts = make_star("COCONUTS-2 A", .4 * solar_mass);
    auto* detached = make_star("COCONUTS-2b", .04 * solar_mass);
    detached->temperature = 492;
    strcpy(detached->spectral_type, "T9");
    ExactStarSearchIndex lookup;
    TestCatalogReader reader;
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    bool valid = false;
    const auto b = reader.exorow_from_json({{"pl_name", "Proxima Centauri b"}, {"hostname", proxima->name},
        {"alt_target_name", "alpha Cen C b#GL 551 C b#HIP 70890 b"}, {"pl_orbper", 11.18465},
        {"pl_msinie", 1.055}}, &valid);
    ASSERT_TRUE(valid);
    EXPECT_TRUE(reader.add_exoplanet_from_row(b, proxima, planets, count, &lookup));
    const auto cold = reader.exorow_from_json({{"pl_name", "COCONUTS-2 b"}, {"hostname", coconuts->name},
        {"pl_bmassj", 7.5}, {"pl_radj", 1.122}, {"pl_orbsmax", 7506}}, &valid);
    ASSERT_TRUE(valid);
    ASSERT_TRUE(reader.add_exoplanet_from_row(cold, coconuts, planets, count, &lookup));
    EXPECT_TRUE(detached->deleted);
    const int planet = planets.at(coconuts->seqno).front();
    EXPECT_EQ(cels[planet]->orbit->center, coconuts);
    reader.apply_exoplanet_names(planets, &lookup);
    EXPECT_STREQ(cels[planet]->name, "COCONUTS-2 A b");
    EXPECT_EQ(find_object("COCONUTS-2 b"), planet);
}

TEST_F(CatalogParsingTest, CandidateStatusSurvivesPlanetCacheAndUniverseSave)
{
    auto* host = make_star("Proxima Centauri", .1221 * solar_mass);
    TestCatalogReader reader;
    bool valid = false;
    const auto input = reader.exorow_from_json({{"pl_name", "Proxima Centauri c"}, {"hostname", host->name},
        {"planet_status", "Candidate"}, {"pl_orbper", 1900}, {"pl_msinie", 5.8}}, &valid);
    ASSERT_TRUE(valid);
    FILE* file = std::tmpfile();
    ASSERT_NE(file, nullptr);
    reader.exorow_write_line(file, input);
    rewind(file);
    ExoRow row;
    const bool read = reader.exorow_read_line(file, row);
    fclose(file);
    ASSERT_TRUE(read);
    EXPECT_TRUE(row.catalog_candidate);
    std::map<int, std::vector<int>> planets;
    unsigned int count = 0;
    ASSERT_TRUE(reader.add_exoplanet_from_row(row, host, planets, count));
    auto* planet = static_cast<Planet*>(cels[planets.at(host->seqno).front()]);
    EXPECT_TRUE(planet->catalog_candidate);
    Planet restored;
    ASSERT_TRUE(restored.from_json(planet->to_json()));
    EXPECT_TRUE(restored.catalog_candidate);
}

TEST_F(CatalogParsingTest, MissingMDwarfColorUsesSubtypeWhileMeasuredZeroSurvivesBaking)
{
    struct Writer : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    } writer;
    Star* star = make_star("M subtype fallback", .3*solar_mass);
    strcpy(star->spectral_type, "M3.5");
    star->absolute_magnitude = 11.2;
    star->volumetric_mean_radius = .017621*solar_radius;
    star->BV_color = 0;
    star->temperature = 0;
    FILE* file = std::tmpfile();
    ASSERT_NE(file, nullptr);
    writer.write_condensed_star_cat_line(file, star);
    EXPECT_NEAR(star->estimate_temperature(), 3320, 1e-12);
    EXPECT_GT(star->BV_color, .5);
    star->BV_color = 0;
    star->is_bv_observed = true;
    writer.write_condensed_star_cat_line(file, star);
    EXPECT_DOUBLE_EQ(star->BV_color, 0);
    fclose(file);
}

TEST_F(CatalogParsingTest, BrownDwarfSupplementIsRepeatableAndPreservesExistingCompanions)
{
    Star* companion = make_star("Eps Ind C", .05*solar_mass);
    strcpy(companion->Gliese, "GJ 845 C");
    companion->temperature = 972;
    companion->BV_color = 1.83;
    companion->orbit = new Orbit();
    companion->orbit->semimajor_axis = 2.4*AU;
    Orbit* original_orbit = companion->orbit;
    const unsigned int added = load_brown_dwarf_supplement();
    EXPECT_GT(added, 300u);
    EXPECT_DOUBLE_EQ(companion->BV_color, 1.83);
    EXPECT_DOUBLE_EQ(companion->temperature, 972);
    EXPECT_EQ(companion->orbit, original_orbit);
    const int count = ncelobjs;
    EXPECT_EQ(load_brown_dwarf_supplement(), 0u);
    EXPECT_EQ(ncelobjs, count);
    for (int i = 1; i < ncelobjs; ++i)
    {
        Star* star = static_cast<Star*>(cels[i]);
        EXPECT_GT(star->distance, 0);
        EXPECT_TRUE(std::isfinite(star->temperature));
        if (star != companion && stellar_regime(star) == regime_substellar)
            EXPECT_GT(star->sidereal_rotational_period, 0);
    }
}

TEST_F(CatalogParsingTest, EpsilonIndCensusNamesReuseLegacyBAndCWithoutGlieseIdentifiers)
{
    Star* b = make_star("Eps Ind B", .06386*solar_mass);
    Star* c = make_star("Eps Ind C", .05081*solar_mass);
    b->temperature = 1312;
    c->temperature = 972;
    b->BV_color = 4.83;
    c->BV_color = 6.72;
    b->orbit = new Orbit();
    c->orbit = new Orbit();
    c->orbit->center = b;
    Orbit* b_orbit = b->orbit;
    Orbit* c_orbit = c->orbit;
    ASSERT_STREQ(b->Gliese, "");
    ASSERT_STREQ(c->Gliese, "");
    const int count = ncelobjs;
    load_brown_dwarf_supplement();
    EXPECT_EQ(find_object("epsilon Ind Ba", true, 9e29, 0), b->seqno);
    EXPECT_EQ(find_object("epsilon Ind Bb", true, 9e29, 0), c->seqno);
    EXPECT_EQ(find_object("Eps Ind B", true, 9e29, 0), b->seqno);
    EXPECT_EQ(find_object("Eps Ind C", true, 9e29, 0), c->seqno);
    int ba = 0, bb = 0;
    for (int i = count; i < ncelobjs; ++i)
    {
        if (std::string(cels[i]->name) == "epsilon Ind Ba") ++ba;
        if (std::string(cels[i]->name) == "epsilon Ind Bb") ++bb;
    }
    EXPECT_EQ(ba, 0);
    EXPECT_EQ(bb, 0);
    EXPECT_EQ(b->orbit, b_orbit);
    EXPECT_EQ(c->orbit, c_orbit);
    EXPECT_DOUBLE_EQ(b->temperature, 1312);
    EXPECT_DOUBLE_EQ(c->temperature, 972);
    EXPECT_DOUBLE_EQ(b->BV_color, 4.83);
    EXPECT_DOUBLE_EQ(c->BV_color, 6.72);
    EXPECT_EQ(load_brown_dwarf_supplement(), 0u);
}

TEST_F(StellarDownloadTest, BrownDwarfsAreFoldedIntoTheWriterAndReloadWithoutASupplementImport)
{
    std::filesystem::copy_file(original / "catalogs/brown_dwarfs_20pc.json", "catalogs/brown_dwarfs_20pc.json");
    std::filesystem::copy_file(original / "catalogs/ultracool_dwarfs.json", "catalogs/ultracool_dwarfs.json");
    CatalogReader reader;
    const int written = reader.write_condensed_star_cat({});
    ASSERT_GT(written, 4000);
    std::ifstream saved("catalogs/soles_alienorum.dat");
    const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("# BROWN20PC1 "), std::string::npos);
    EXPECT_NE(text.find(" AST1 "), std::string::npos);
    Star* reference = nullptr;
    for (int i = 0; cels[i]; ++i)
        if (i > 0 && static_cast<Star*>(cels[i])->alienorumid.empty()) { reference = static_cast<Star*>(cels[i]); break; }
    ASSERT_NE(reference, nullptr);
    const std::string name = reference->name;
    const double ra = reference->right_ascension, dec = reference->declination, epoch = reference->epoch;
    delete_the_universe();
    EXPECT_EQ(reader.read_condensed_star_cat(), written);
    EXPECT_TRUE(condensed_brown_dwarfs_loaded);
    EXPECT_EQ(load_brown_dwarf_supplement(), 0u);
    const int found = find_object(name.c_str(), true, 9e29, 0);
    ASSERT_GE(found, 0);
    EXPECT_DOUBLE_EQ(cels[found]->right_ascension, ra);
    EXPECT_DOUBLE_EQ(cels[found]->declination, dec);
    EXPECT_DOUBLE_EQ(cels[found]->epoch, epoch);
    const int luhman = find_object("Luhman 16 A", true, 9e29, 0);
    ASSERT_GE(luhman, 0);
    EXPECT_EQ(static_cast<Star*>(cels[luhman])->stellar_parameters["catalog"], "UltracoolSheet 2.1.0");
}

TEST_F(CatalogParsingTest, UltracoolSheetAddsDistantObjectsWithShortSearchableComponentNames)
{
    ASSERT_GT(load_brown_dwarf_supplement(), 4000u);
    const int a = find_object("Luhman 16 A", true, 9e29, 0);
    const int b = find_object("Luhman 16 B", true, 9e29, 0);
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    EXPECT_NE(a, b);
    EXPECT_STREQ(cels[a]->name, "Luhman 16 A");
    EXPECT_STREQ(cels[b]->name, "Luhman 16 B");
    EXPECT_EQ(find_object("WISE J104915.57-531906.1 A", true, 9e29, 0), a);
    EXPECT_EQ(find_object("WISE J104915.57-531906.1 B", true, 9e29, 0), b);
    int distant = 0, documented = 0;
    for (int index = 0; cels[index]; ++index)
    {
        auto* star = static_cast<Star*>(cels[index]);
        distant += star->distance > 20 * parsec;
        documented += star->stellar_parameters.is_object();
    }
    EXPECT_GT(distant, 2000);
    EXPECT_GT(documented, 3900);
    const int count = ncelobjs;
    EXPECT_EQ(load_brown_dwarf_supplement(), 0u);
    EXPECT_EQ(ncelobjs, count);
}

TEST_F(CatalogParsingTest, ImportedBrownDwarfsDoNotReceiveInventedAlienorumIds)
{
    load_brown_dwarf_supplement();
    for (int i = 1; i < ncelobjs; ++i)
    {
        Star* object = static_cast<Star*>(cels[i]);
        EXPECT_TRUE(object->alienorumid.empty()) << object->name;
    }
}

TEST_F(CatalogParsingTest, BrownDwarfPhotometryPreservesTheSourceBand)
{
    Star* star = make_star("Test ultracool companion", .06 * solar_mass);
    star->distance = 15 * parsec;
    star->volumetric_mean_radius = jupiter_radius;
    star->temperature = 0;
    star->spectral_type[0] = 0;
    update_brown_catalog_photometry(*star, json::object());
    EXPECT_DOUBLE_EQ(star->temperature, 0);
    EXPECT_DOUBLE_EQ(star->apparent_magnitude, 1e29);
    update_brown_catalog_photometry(*star, {{"gmag", 17.0}, {"temperature", 1700.0}});
    EXPECT_DOUBLE_EQ(star->temperature, 1700.0);
    EXPECT_DOUBLE_EQ(star->gaia_g_magnitude, 17.0);
    EXPECT_EQ(star->stellar_parameters["magnitude_band"], "Gaia G");
    EXPECT_TRUE(star->apparent_magnitude_estimated);

    update_brown_catalog_photometry(*star, {{"vmag", 20.0}});
    EXPECT_DOUBLE_EQ(star->apparent_magnitude, 20.0);
    EXPECT_DOUBLE_EQ(star->gaia_g_magnitude, 17.0);
    EXPECT_EQ(star->stellar_parameters["magnitude_band"], "Johnson V");
    EXPECT_FALSE(star->apparent_magnitude_estimated);
}
