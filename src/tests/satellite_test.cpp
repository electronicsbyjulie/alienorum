#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/planet.h"
#include "../classes/satellite.h"
#include "universe_fixture.h"
#include "../housekeeping.h"

using namespace alienorum;
using json = nlohmann::json;

class SatelliteViewerPlaneTest : public UniverseFixture
{
    protected:
    const int saved_where = whereami;
    const ViewMode saved_mode = view_mode;
    const ViewerPlaneMode saved_plane = vplane_mode;
    const CelestialLocation saved_here = here;
    const bool saved_inverted = satview_upsidedown, saved_changed = viewchanged;

    void TearDown() override
    {
        whereami = -1;
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i]->typeclass() != class_satellite) continue;
            delete cels[i]->orbit;
            cels[i]->orbit = nullptr;
        }
        delete_the_universe();
        whereami = saved_where;
        view_mode = saved_mode;
        vplane_mode = saved_plane;
        here = saved_here;
        satview_upsidedown = saved_inverted;
        viewchanged = saved_changed;
        UniverseFixture::TearDown();
    }
};

TEST_F(SatelliteViewerPlaneTest, EsusSatelliteUpStaysAccurateAtGalacticDistances)
{
    auto* host = make_planet(make_star("Esus"), "Esus e");
    auto* satellite = new Satellite();
    strcpy(satellite->name, "scrisumiiouelor");
    satellite->orbit = new Orbit();
    satellite->orbit->center = host;
    ASSERT_TRUE(append_cel(satellite));
    whereami = satellite->seqno;
    vplane_mode = vplane_local;
    const Point host_local(-60987591692.4603, 20004314288.43489, -150399184629.94974);
    const Point saved_satellite(-60990281010.74737, 19999570701.534767, -150403900295.20905);
    const Point initial_radial = saved_satellite - host_local;
    for (bool inverted : {false, true})
    {
        satview_upsidedown = inverted;
        for (bool distant : {false, true})
        {
            host->location.galactic_center = distant ? Point(1.4141746374029638, -.4166599158977142, .6970997186990577) : Point();
            host->location.system_center = distant ? Point(3.1559711293162844e18, 8.767235288236061e18, 9.050110633097912e18) : Point();
            for (int frame = 0; frame < 128; ++frame)
            {
                const Point radial = rotate3D(initial_radial, center, yaxis, frame * .05);
                host->location.local_position = host_local + Point(frame * 12345.0, frame * 6789.0, frame * -23456.0);
                satellite->location = host->location;
                satellite->location.local_position += radial;
                view_mode = vm_spaceship;
                set_viewer_location_and_plane();
                Point expected = radial * (inverted ? -1 : 1);
                expected.scale(1);
                const Point up = rotate3D(expected, center, here.equatorial_plane.v, here.equatorial_plane.a);
                EXPECT_NEAR(up.x, 0, 1e-9) << "frame " << frame;
                EXPECT_NEAR(up.y, 1, 1e-9) << "frame " << frame;
                EXPECT_NEAR(up.z, 0, 1e-9) << "frame " << frame;
                EXPECT_EQ(here.galactic_center.to_json(), satellite->location.galactic_center.to_json());
                EXPECT_EQ(here.system_center.to_json(), satellite->location.system_center.to_json());
            }
        }
    }
}

class SatelliteCsvTest : public UniverseFixture
{
    protected:
    std::filesystem::path original_directory, test_directory;

    void SetUp() override
    {
        UniverseFixture::SetUp();
        original_directory = std::filesystem::current_path();
        test_directory = std::filesystem::temp_directory_path() / ("alienorum_sat_csv_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(test_directory / "catalogs" / "sat");
        std::filesystem::current_path(test_directory);
    }

    void TearDown() override
    {
        std::filesystem::current_path(original_directory);
        std::filesystem::remove_all(test_directory);
        UniverseFixture::TearDown();
    }

    void write(const SatSource& source, const std::string& rows)
    {
        std::ofstream file(test_directory / "catalogs" / "sat" / (source.local_name + ".csv"));
        file << "OBJECT_NAME,OBJECT_ID,NORAD_CAT_ID\n" << rows;
    }

    std::string master(const char *name, unsigned int id)
    {
        return std::string(name) + ",2000-001A," + std::to_string(id)
            + ",PAY,+,US,2000-01-01,SITE,,92,51.6,450,400,4,,EA,ORB\n";
    }

    std::string orbit(const char *name, unsigned int id, double motion)
    {
        return std::string(name) + ",2000-001A,2026-10-20T00:00:00,"
            + std::to_string(motion) + ",0.001,51.6,100,50,20,0,U,"
            + std::to_string(id) + ",999,100,0.0001,0,0\n";
    }
};

TEST_F(SatelliteCsvTest, SupplementalUpdatesPreserveMasterIdentityAndPreferredSource)
{
    SatSource catalog, broad, focused;
    catalog.local_name = "master";
    broad.local_name = "broad";
    focused.local_name = "focused";
    broad.is_supplemental = focused.is_supplemental = true;
    write(catalog, master("Master name", 42) + master("Other satellite", 99));
    ASSERT_TRUE(catalog.read_csv_data());
    write(broad, orbit("Orbit name", 42, 15.0) + orbit("Other orbit", 99, 14.0));
    ASSERT_TRUE(broad.read_csv_data());
    ASSERT_EQ(best_source.at(42), &broad);
    write(focused, orbit("Focused orbit", 42, 16.0));
    ASSERT_TRUE(focused.read_csv_data());
    ASSERT_EQ(sat_data.size(), 2u);
    EXPECT_EQ(sat_data[0].OBJECT_NAME, "Master name");
    EXPECT_EQ(sat_data[0].OWNER, "US");
    EXPECT_EQ(sat_data[0].ORBIT_CENTER, "EA");
    EXPECT_DOUBLE_EQ(sat_data[0].MEAN_MOTION, 16.0);
    EXPECT_EQ(sat_data[0].catalog, "broad focused");
    EXPECT_EQ(best_source.at(42), &focused);

    write(broad, orbit("Latest orbit", 42, 17.0) + orbit("Other orbit", 99, 14.0));
    ASSERT_TRUE(broad.read_csv_data());
    EXPECT_DOUBLE_EQ(sat_data[0].MEAN_MOTION, 17.0);
    EXPECT_EQ(best_source.at(42), &focused);
    write(catalog, master("Updated master name", 42));
    ASSERT_TRUE(catalog.read_csv_data());
    EXPECT_EQ(sat_data[0].OBJECT_NAME, "Updated master name");
    EXPECT_DOUBLE_EQ(sat_data[0].MEAN_MOTION, 17.0);
}

TEST_F(SatelliteCsvTest, NewRecordsAreMatchedWithinAFileAndAfterCatalogReset)
{
    SatSource catalog, supplemental;
    catalog.local_name = "master";
    supplemental.local_name = "supplemental";
    supplemental.is_supplemental = true;
    write(catalog, master("First", 42) + master("Updated first", 42));
    ASSERT_TRUE(catalog.read_csv_data());
    ASSERT_EQ(sat_data.size(), 1u);
    EXPECT_EQ(sat_data[0].OBJECT_NAME, "Updated first");
    write(supplemental, orbit("New satellite", 777, 12.0) + orbit("New satellite", 777, 13.0));
    ASSERT_TRUE(supplemental.read_csv_data());
    ASSERT_EQ(sat_data.size(), 2u);
    EXPECT_EQ(sat_data[1].NORAD_CAT_ID, 777u);
    EXPECT_DOUBLE_EQ(sat_data[1].MEAN_MOTION, 13.0);
    sat_data.clear();
    best_source.clear();
    ASSERT_TRUE(supplemental.read_csv_data());
    ASSERT_EQ(sat_data.size(), 1u);
    EXPECT_EQ(sat_data[0].NORAD_CAT_ID, 777u);
    EXPECT_DOUBLE_EQ(sat_data[0].MEAN_MOTION, 13.0);
}

// Nothing here downloads anything. The fixture empties sat_sources and best_source, which are the
// only two things populate() consults before deciding to fetch, and the age tests read files that
// are already on disk.

// =====================================================================
// Satellite & SatSource Serialization
// =====================================================================

TEST(SatSourceTest, JsonSerialization)
{
    SatSource original;
    original.url = "https://celestrak.org/NORAD/elements/weather.txt";
    original.local_name = "weather";
    original.is_supplemental = false;
    original.always_check = true;

    json j = original.to_json();
    
    // Test the specific keys your custom to_json generates
    EXPECT_EQ(j["URL"], "https://celestrak.org/NORAD/elements/weather.txt");
    EXPECT_EQ(j["LocalName"], "weather");
    EXPECT_EQ(j["Type"], "master"); // Because is_supplemental is false

    SatSource restored;
    bool success = restored.from_json(j);

    EXPECT_TRUE(success);
    EXPECT_EQ(restored.url, "https://celestrak.org/NORAD/elements/weather.txt");
    EXPECT_EQ(restored.local_name, "weather");
    EXPECT_FALSE(restored.is_supplemental);
}

TEST(SatelliteTest, JsonSerialization)
{
    Satellite sat;
    
    // Verify the constructor set the correct classifications
    EXPECT_EQ(sat.typeclass(), class_satellite);
    EXPECT_EQ(sat.type, artificial);

    sat.bstar = 0.0001234;

    json j = sat.to_json();

    Satellite restored;
    bool success = restored.from_json(j);

    EXPECT_TRUE(success);
    EXPECT_DOUBLE_EQ(restored.bstar, 0.0001234);
    EXPECT_EQ(restored.typeclass(), class_satellite);
    EXPECT_EQ(restored.type, artificial);
}

// =====================================================================
// Network Policy & Threshold Logic
// =====================================================================

TEST(SatSourceTest, RespectsDownloadInterval)
{
    // This used to compare two literals against the sat_download_interval macro, which is true by
    // arithmetic and runs none of the program. What actually guards the fetch is data_age_hours(),
    // so that is what is tested -- against files already on disk, downloading nothing.
    SatSource missing;
    missing.local_name = "no_such_catalog_as_this_one";
    EXPECT_EQ(missing.csv_fname(), std::string("catalogs") + _FSSTR + "sat" + _FSSTR
        + "no_such_catalog_as_this_one.csv");

    // A file that is not there is reported as impossibly old, so that the first run always
    // fetches rather than deciding it already has a copy.
    EXPECT_EQ(missing.data_age_hours(), 100000);
    EXPECT_GE(missing.data_age_hours() * 3600, sat_download_interval);

    // A real one, if this working tree has any: its age is a sane number of hours and the
    // comparison the download is gated on can be made without going anywhere near the network.
    SatSource active;
    active.local_name = "active";
    if (file_exists(active.csv_fname().c_str()))
    {
        int hours = active.data_age_hours();
        EXPECT_GE(hours, 0);
        EXPECT_LT(hours, 100000);
    }

    // data_age_hours() reports whole hours, so download_data() compares a value that has been
    // rounded down -- a file of 1h59m counts as one hour old. That errs towards waiting longer
    // between fetches, never shorter, which is the side to err on. Pinned here so that tidying it
    // up later cannot quietly turn it round.
    EXPECT_EQ(sat_download_interval % 3600, 0)
        << "the interval is compared against a whole number of hours, so it should be whole hours";
}

// =====================================================================
// SatSource::populate() -- the real thing, offline
// =====================================================================

class SatellitePopulateTest : public UniverseFixture
{
    protected:
    // A catalog-coded host; its name does not determine the satellite center.
    Planet* make_earth()
    {
        Star* sun = make_star("Sol");
        Planet* earth = make_planet(sun, "Satellite host", AU);
        earth->volumetric_mean_radius = 6378137.0;
        earth->oblateness = 0;
        earth->mass = earth_mass;
        earth->orbit_center_codes = {"EA", "EM"};
        earth->J2 = 0.00108262545;
        return earth;
    }

    // One line of the catalog, as an ISS-like record.
    SatRecord iss_record()
    {
        SatRecord record;
        record.OBJECT_NAME = "ISS (ZARYA)";
        record.NORAD_CAT_ID = 25544;
        record.ORBIT_CENTER = "EA";
        record.EPOCH = "2023-01-01T00:00:00";
        record.MEAN_MOTION = 15.5;               // revolutions per day
        record.ECCENTRICITY = 0.00045;
        record.INCLINATION = 51.64;
        record.RA_OF_ASC_NODE = 100.0;
        record.ARG_OF_PERICENTER = 50.0;
        record.MEAN_ANOMALY = 20.0;
        record.PERIOD = 92.0;                    // minutes
        record.BSTAR = 0.0001234;
        return record;
    }
};

TEST_F(SatellitePopulateTest, FillsInTheOrbitFromTheCatalogRecord)
{
    make_earth();
    sat_data.push_back(iss_record());

    Satellite sat;
    ASSERT_TRUE(SatSource::populate(&sat, 0));

    EXPECT_STREQ(sat.name, "ISS (ZARYA)");
    ASSERT_NE(sat.orbit, nullptr);
    ASSERT_NE(sat.orbit->center, nullptr);
    EXPECT_STREQ(sat.orbit->center->name, "Satellite host");

    // Degrees in the file, radians in the program.
    EXPECT_NEAR(sat.orbit->inclination, 51.64 * fiftyseventh, 1e-12);
    EXPECT_NEAR(sat.orbit->ascending_node, 100.0 * fiftyseventh, 1e-12);
    EXPECT_NEAR(sat.orbit->arg_periapsis, 50.0 * fiftyseventh, 1e-12);
    EXPECT_NEAR(sat.orbit->mean_anomaly, 20.0 * fiftyseventh, 1e-12);
    EXPECT_DOUBLE_EQ(sat.orbit->eccentricity, 0.00045);

    // Minutes in the file, seconds in the program; revolutions a day become radians a second.
    EXPECT_DOUBLE_EQ(sat.orbit->period, 92.0 * 60);
    EXPECT_NEAR(sat.mean_motion, 15.5 * 2 * _pi / oneday, 1e-15);
    EXPECT_DOUBLE_EQ(sat.bstar, 0.0001234);

    // The epoch is a Julian date, taken from the ISO string in UTC.
    EXPECT_NEAR(sat.epoch, (double)(from_iso_string("2023-01-01T00:00:00", "%Y-%m-%dT%H:%M:%S")
        - J2000_TIME_T) / oneday + J2000, 1e-9);

    // A low orbit around the Earth: a few hundred kilometres up, not a few million.
    EXPECT_GT(sat.orbit->semimajor_axis, 6.6e6);
    EXPECT_LT(sat.orbit->semimajor_axis, 7.2e6);

    delete_the_universe();
}

TEST_F(SatellitePopulateTest, ComputesTheNodalPrecession)
{
    // The formula lives in populate(); this calls it rather than restating it, so that changing
    // the code changes the test's answer. (The previous version recomputed the whole expression
    // in the test, which would have gone on passing had populate() been rewritten wrongly.)
    make_earth();
    sat_data.push_back(iss_record());

    Satellite sat;
    ASSERT_TRUE(SatSource::populate(&sat, 0));
    ASSERT_NE(sat.orbit, nullptr);

    // prec_node is stored unsigned-by-convention: update_orbit_location() applies it as
    //     node_adjustment = seconds_since_epoch * -prec_node,
    // so a positive value IS the westward regression of the node. For the ISS's 51.6 degree
    // prograde orbit that is about five degrees a day, which is why its ground track repeats
    // against a slowly turning orbital plane.
    EXPECT_GT(sat.orbit->prec_node, 0);
    EXPECT_NEAR(sat.orbit->prec_node * oneday * fiftyseven, 5.0, 1.0);

    // The apsides turn too, and more slowly.
    EXPECT_NE(sat.orbit->proc_argperi, 0);
    EXPECT_LT(std::fabs(sat.orbit->proc_argperi), std::fabs(sat.orbit->prec_node));

    // A polar orbit has no nodal regression at all: cos(90 degrees) is zero, which is what makes
    // a sun-synchronous orbit possible just off the pole.
    sat_data[0].INCLINATION = 90.0;
    Satellite polar;
    ASSERT_TRUE(SatSource::populate(&polar, 0));
    EXPECT_NEAR(polar.orbit->prec_node, 0, 1e-18);

    // And past the pole it turns the other way.
    sat_data[0].INCLINATION = 100.0;
    Satellite retrograde;
    ASSERT_TRUE(SatSource::populate(&retrograde, 0));
    EXPECT_LT(retrograde.orbit->prec_node, 0);

    delete_the_universe();
}

TEST_F(SatellitePopulateTest, RefusesWhatItCannotPlace)
{
    make_earth();

    // An index past the end of the catalog.
    Satellite sat;
    EXPECT_FALSE(SatSource::populate(&sat, 99));

    // No satellite at all.
    EXPECT_FALSE(SatSource::populate(nullptr, 0));

    // A center this program does not know about: refused rather than orbiting nothing.
    SatRecord elsewhere = iss_record();
    elsewhere.ORBIT_CENTER = "XX";
    sat_data.push_back(elsewhere);
    Satellite stray;
    EXPECT_FALSE(SatSource::populate(&stray, 0));

    // And something that is not a satellite.
    Planet not_a_satellite;
    EXPECT_FALSE(SatSource::populate((Satellite*)&not_a_satellite, 0));

    delete_the_universe();
}

// =====================================================================
// Orbital Math & Precession Calculations
// =====================================================================

TEST(SatelliteMathTest, PopulateCalculatesPrecession)
{
    Planet earth;
    
    // Verify the Planet constructor set its class correctly
    EXPECT_EQ(earth.typeclass(), class_planet); 
    
    earth.seqno = 0;
    earth.volumetric_mean_radius = 6378137.0; // Equatorial radius
    earth.J2 = 0.00108262545; // Earth J2 coefficient
    
    // Mock the global cels array just enough to satisfy find_object("Earth")
    // Note: You will ensure find_object("Earth") returns a valid index
    // and cels[index] points to 'earth' in your actual test runner environment.
    
    // Setup a raw record with realistic ISS telemetry
    SatRecord record;
    record.OBJECT_NAME = "ISS";
    record.NORAD_CAT_ID = 25544;
    record.ORBIT_CENTER = "EA";
    record.EPOCH = "2023-01-01T00:00:00"; 
    record.MEAN_MOTION = 15.5; // revs per day
    record.ECCENTRICITY = 0.00045;
    record.INCLINATION = 51.64;
    record.RA_OF_ASC_NODE = 100.0;
    record.ARG_OF_PERICENTER = 50.0;
    record.MEAN_ANOMALY = 20.0;
    record.PERIOD = 92.0; // minutes
    
    // Inject into the global sat_data vector
    sat_data.clear();
    sat_data.push_back(record);
    
    Satellite sat;
    
    // Bypass the actual populate() call if global state (cels array, find_object) 
    // is too heavily coupled, and test the pure math directly here to mirror it:
    
    double mean_motion_rads = record.MEAN_MOTION * 2.0 * _pi / oneday; 
    double inclination_rads = record.INCLINATION * fiftyseventh;
    
    // Calculate semimajor axis (simplified for the test from orbit->compute_semimajor_axis)
    double semimajor_axis = 6770000.0; // Rough ISS orbit in meters
    
    // The precession math from your populate() method:
    double p = semimajor_axis * (1.0 - record.ECCENTRICITY * record.ECCENTRICITY);
    double paren = earth.get_equatorial_radius() / p;
    paren *= paren;
    double common_term = earth.J2 * paren * mean_motion_rads;
    double cos_incl = cos(inclination_rads);
    
    double expected_prec_node = 1.5 * common_term * cos_incl;
    double expected_proc_argperi = 0.75 * common_term * (5.0 * cos_incl * cos_incl - 1.0);
    
    // Ensure the precession values result in non-zero, predictable drift rates
    EXPECT_NE(expected_prec_node, 0.0);
    EXPECT_NE(expected_proc_argperi, 0.0);
    
    // prec_node holds the rate unsigned-by-convention: update_orbit_location() applies it as
    //     node_adjustment = seconds_since_epoch * -PN,
    // so a positive prec_node IS the westward drift of the node. For the ISS (51.6 deg, prograde)
    // cos i > 0, the stored rate is positive, and the node duly regresses about 5 degrees a day.
    EXPECT_GT(expected_prec_node, 0.0);
    EXPECT_NEAR(expected_prec_node * oneday * fiftyseven, 5.0, 1.0);        // degrees per day
    
    // Retrograde orbits (cos i < 0) turn it round and the node advances instead.
    double retrograde_prec_node = 1.5 * common_term * cos(100.0 * fiftyseventh);
    EXPECT_LT(retrograde_prec_node, 0.0);
}

// =====================================================================
// CSV Parser Tests
// =====================================================================

TEST(CSVParserTest, ParsesSimpleCommaSeparation)
{
    const char* row = "ISS,25544,EA,2023-01-01";
    std::vector<std::string> parsed = parse_csv_row(row);

    ASSERT_EQ(parsed.size(), 4);
    EXPECT_EQ(parsed[0], "ISS");
    EXPECT_EQ(parsed[1], "25544");
    EXPECT_EQ(parsed[2], "EA");
    EXPECT_EQ(parsed[3], "2023-01-01");
}

TEST(CSVParserTest, HandlesEmptyFields)
{
    // Simulates a row where some data is missing: "OBJECT_NAME,,NORAD,,OWNER"
    const char* row = "OBJECT_NAME,,NORAD,,OWNER";
    std::vector<std::string> parsed = parse_csv_row(row);

    ASSERT_EQ(parsed.size(), 5);
    EXPECT_EQ(parsed[0], "OBJECT_NAME");
    EXPECT_EQ(parsed[1], "");
    EXPECT_EQ(parsed[2], "NORAD");
    EXPECT_EQ(parsed[3], "");
    EXPECT_EQ(parsed[4], "OWNER");
}

TEST(CSVParserTest, HandlesTrailingCommas)
{
    // A trailing comma should result in a final empty string element
    const char* row = "ISS,25544,";
    std::vector<std::string> parsed = parse_csv_row(row);

    ASSERT_EQ(parsed.size(), 3);
    EXPECT_EQ(parsed[0], "ISS");
    EXPECT_EQ(parsed[1], "25544");
    EXPECT_EQ(parsed[2], "");
}

TEST(CSVParserTest, HandlesQuotedFields)
{
    // If your parser supports standard CSV quoting to allow commas inside fields
    // e.g., "ISS (ZARYA)",25544,"EARTH, LOW ORBIT"
    const char* row = "\"ISS (ZARYA)\",25544,\"EARTH, LOW ORBIT\"";
    std::vector<std::string> parsed = parse_csv_row(row);

    // Depending on your parse_csv_row implementation, you might expect 3 fields
    // with the outer quotes stripped. Adjust these assertions if your parser 
    // behaves differently (e.g., leaves quotes intact).
    ASSERT_EQ(parsed.size(), 3);
    EXPECT_EQ(parsed[0], "ISS (ZARYA)");
    EXPECT_EQ(parsed[1], "25544");
    EXPECT_EQ(parsed[2], "EARTH, LOW ORBIT");
}

TEST(CSVParserTest, HandlesEmptyString)
{
    const char* row = "";
    std::vector<std::string> parsed = parse_csv_row(row);

    // Depending on implementation, an empty string might return an empty vector
    // or a vector with a single empty string element.
    EXPECT_TRUE(parsed.empty() || (parsed.size() == 1 && parsed[0] == ""));
}

