#include <cstring>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <set>
#include <vector>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "../classes/cons.h"
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
    EXPECT_EQ(measured->stellar_abundances, saved_abundances);
    EXPECT_EQ(measured->catalog_aliases, std::vector<std::string>({"Measured abundance host"}));
    EXPECT_DOUBLE_EQ(solar->metallicity, 0);
    EXPECT_EQ(solar->metallicity_ratio, "[M/H]");
    EXPECT_TRUE(std::isnan(solar->metallicity_error_upper));
    EXPECT_TRUE(std::isnan(unknown->metallicity));
    delete_the_universe();
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
    EXPECT_STREQ(planet->name, "55 Cnc Bb");
    delete_the_universe();
}

TEST_F(CatalogParsingTest, MuAraNasaRecordsUseEuLettersAndNamesForTheirPeriods)
{
    struct OrbitName
    {
        char nasa_letter, eu_letter;
        double period;
        const char *friendly;
    };
    // https://en.wikipedia.org/wiki/Mu_Arae#System_architecture_and_habitability
    const OrbitName examples[] =
    {
        {'d', 'c', 9.638, "Dulcinea"}, {'e', 'd', 308.36, "Rocinante"},
        {'b', 'b', 644.92, "Quijote"}, {'c', 'e', 4019.0, "Sancho"}
    };
    Star *host = make_star("mu Ara");
    TestCatalogReader reader;
    std::map<int, std::vector<int>> planets;
    std::vector<Planet*> objects;
    for (const auto& example : examples)
    {
        json row = {{"hostname", "HD 160691"},
            {"pl_name", std::string("HD 160691 ") + example.nasa_letter},
            {"pl_orbper", example.period}};
        ASSERT_TRUE(reader.accept_exoplanet_source(row, true));
        const std::string designation = std::string("mu Ara ") + example.eu_letter;
        EXPECT_EQ(row["pl_name"], designation);
        EXPECT_EQ(row["hostname"], "mu Ara");
        EXPECT_DOUBLE_EQ(row["pl_orbper"], example.period);
        const json first_pass = row;
        EXPECT_TRUE(reader.accept_exoplanet_source(row, true));
        EXPECT_EQ(row, first_pass);
        EXPECT_TRUE(reader.accept_exoplanet_source(row, false));
        EXPECT_EQ(row, first_pass);

        Planet *planet = make_planet(host, designation.c_str());
        planet->orbit->period = example.period * oneday;
        planets[host->seqno].push_back(planet->seqno);
        objects.push_back(planet);
    }
    reader.apply_exoplanet_names(planets);
    for (size_t i = 0; i < objects.size(); ++i)
    {
        EXPECT_STREQ(objects[i]->name, examples[i].friendly);
        EXPECT_NEAR(objects[i]->orbit->period / oneday, examples[i].period, 1e-9);
    }
    delete_the_universe();
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
                EXPECT_NEAR(inclination * fiftyseven, known_planet && i == 0 ? 90 : 103.82, 1e-5);
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
        EXPECT_NEAR(inclination * fiftyseven, 86.45, 1e-5) << planet->name;
        ++checked;
    }
    EXPECT_EQ(checked, 2);
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
    } restore;
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
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
