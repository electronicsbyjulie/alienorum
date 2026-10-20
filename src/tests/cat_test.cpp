#include <cstring>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <vector>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "../loaders.h"
#include "../globals.h"
#include "../housekeeping.h"
#include "universe_fixture.h"

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
    file.close();
    CatalogReader reader;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 3);
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
    const double luminosity = Star::interpolate_mseq_lum(Star::get_mseqidx_from_BV(hot_star->BV_color));
    const double expected_magnitude = 4.85 - log(luminosity) * invlogmagnbase;
    EXPECT_NEAR(hot_star->absolute_magnitude, expected_magnitude, 1e-9);
    strcpy(secondary->name, "Preferred Companion");
    secondary->has_custom_name = false;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 3);
    EXPECT_STREQ(secondary->name, "Preferred Companion");
    EXPECT_FALSE(secondary->has_custom_name);
    secondary->has_custom_name = true;
    ASSERT_EQ(reader.read_star_orbits_dat(cels), 3);
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
    ASSERT_EQ(reader.read_WD_catalog(cels, MAX_CELOBJS, false), 83);
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
    EXPECT_EQ(verified, 83);
    EXPECT_EQ(reader.read_WD_catalog(cels, MAX_CELOBJS, false), 83);
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
    ASSERT_EQ(reader.read_condensed_star_cat(), 83);
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
    fclose(file);
    std::vector<std::string> rows;
    std::ifstream input("catalogs/soles_alienorum.dat");
    std::string row;
    while (std::getline(input, row))
    {
        rows.push_back(row);
    }
    input.close();
    ASSERT_EQ(rows.size(), 4u);
    ASSERT_EQ(rows[1].substr(76, 6), "+     ");
    ASSERT_EQ(rows[3].substr(76, 6), "+00.00");
    rows[2].replace(76, 6, "      ");
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
        ASSERT_EQ(reader.read_condensed_star_cat(), 4);
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
        if (pass == 0)
        {
            FILE *roundtrip = fopen("catalogs/soles_alienorum.dat", "w");
            ASSERT_NE(roundtrip, nullptr);
            for (unsigned int hd = 99990; hd <= 99993; hd++)
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
    using CatalogReader::resolve_or_create_exostar;
    using CatalogReader::exorow_has_position;
    using CatalogReader::read_exoplanet_host_positions;
    using CatalogReader::apply_exoplanet_host_position;
    using CatalogReader::exorow_read_line;
    using CatalogReader::exorow_write_line;
    using CatalogReader::accept_exoplanet_source;
};

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
    EXPECT_GT(host->volumetric_mean_radius, 1e6);
    EXPECT_LT(host->volumetric_mean_radius, 2e7);
    EXPECT_GT(host->absolute_magnitude, 10);
    host->volumetric_mean_radius = 0;
    EXPECT_EQ(reader.resolve_or_create_exostar(row, true, &fresh), host);
    EXPECT_GT(host->volumetric_mean_radius, 0);
    delete_the_universe();
}

TEST_F(CatalogParsingTest, Koi5UsesNasaWhileOtherHostsRetainEuData)
{
    json eu = json::object();
    eu["hostname"] = "KOI-5";
    eu["pl_name"] = "KOI-5 b";
    eu["pl_orbper"] = 7.05186;
    eu["sy_vmag"] = 10.5;
    EXPECT_FALSE(TestCatalogReader::accept_exoplanet_source(eu, false));

    json nasa = eu;
    nasa["hostname"] = "KOI-5 A";
    nasa["pl_orbper"] = 4.780327581;
    nasa["sy_vmag"] = 11.713;
    EXPECT_TRUE(TestCatalogReader::accept_exoplanet_source(nasa, true));
    EXPECT_EQ(nasa["hostname"], "KOI-5");
    EXPECT_EQ(nasa["source_catalog"], "NASA");
    EXPECT_EQ(nasa["pl_orbper"], 4.780327581);
    nasa["koi_disposition"] = "FALSE POSITIVE";
    EXPECT_FALSE(TestCatalogReader::accept_exoplanet_source(nasa, true));

    eu["hostname"] = "TOI-1241";
    EXPECT_FALSE(TestCatalogReader::accept_exoplanet_source(eu, false));
    eu["hostname"] = "KOI-55";
    EXPECT_TRUE(TestCatalogReader::accept_exoplanet_source(eu, false));
    EXPECT_EQ(eu["sy_vmag"], 10.5);
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

TEST_F(CatalogParsingTest, TestLoadExoplanetsFromTap)
{
    CatalogReader cr;
    radio_silence = true;

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

    int idx_e = find_object("PDS 70 e", false);
    EXPECT_GE(idx_e, 0);
    if (idx_e >= 0)
    {
        Planet* pe = (Planet*)cels[idx_e];
        ASSERT_NE(pe->orbit, nullptr);
        double pe_incl_deg = pe->orbit->inclination * fiftyseven;
        EXPECT_NEAR(pe_incl_deg, 180.0, 1.0);
    }

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
        EXPECT_NEAR(p->mass / earth_mass, 3.29, 0.2);
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

    EXPECT_EQ(find_object("82 Eri f", false, 9e29, 0), -1);
    EXPECT_EQ(find_object("HD 20794 d", false, 9e29, 0), -1);
    EXPECT_EQ(find_object("HD 20794 f", false, 9e29, 0), -1);
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

TEST_F(CatalogParsingTest, ApplyGaiaAstrometryUpdatesDistancesAndLuminosityClass)
{
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

    int updated = cr.apply_gaia_astrometry(test_cels);
    EXPECT_EQ(updated, 2);

    double expected_dist_pc = 1000.0 / 5.4151;
    EXPECT_NEAR(s1->distance / parsec, expected_dist_pc, 0.5);
    EXPECT_TRUE(s1->distance_known);
    EXPECT_GT(s1->absolute_magnitude, 0.0);

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



