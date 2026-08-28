#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include "universe_fixture.h"

using namespace alienorum;
using json = nlohmann::json;

static std::string temp_universe(const char* stem)
{
    return (std::filesystem::temp_directory_path() / (std::string("alienorum_") + stem + ".json")).string();
}

class SerializationTest : public UniverseFixture
{
    protected:
    bool save_and_reload(const char* stem, bool only_edited = false)
    {
        std::string path = temp_universe(stem);
        std::fstream out(path, std::ios::out | std::ios::trunc);
        if (!Serialization::save_all(out, cels, only_edited)) return false;
        out.close();

        delete_the_universe();

        std::fstream in(path, std::ios::in);
        bool ok = Serialization::load_all(in, cels, MAX_CELOBJS);
        in.close();
        return ok;
    }

    json read_file(const char* stem)
    {
        std::fstream in(temp_universe(stem), std::ios::in);
        json j;
        in >> j;
        return j;
    }

    void write_file(const char* stem, json j)
    {
        std::fstream out(temp_universe(stem), std::ios::out | std::ios::trunc);
        out << j.dump(4);
    }

    bool load_file(const char* stem)
    {
        std::fstream in(temp_universe(stem), std::ios::in);
        bool ok = Serialization::load_all(in, cels, MAX_CELOBJS);
        in.close();
        return ok;
    }

    CelestialObject* find_loaded(const char* name)
    {
        for (int i=0; cels[i]; i++) if (!strcmp(cels[i]->name, name)) return cels[i];
        return nullptr;
    }
};

// =====================================================================
// Whole-universe round trip
// =====================================================================

TEST_F(SerializationTest, RoundTripPreservesHierarchyAndFields)
{
    Star* sun = make_star("Test Primary");
    sun->HD = 12345;
    sun->has_planets = 2;
    sun->has_hz_planets = 1;

    Planet* world = make_planet(sun, "Test World", AU);
    world->orbit->eccentricity = 0.0167;
    world->orbit->inclination = 0.5;
    world->orbit->ascending_node = 1.25;
    world->orbit->arg_periapsis = 2.5;
    world->temperature = 288;
    world->albedo = 0.306;
    world->asteroid_no = 4001;
    world->lock_type = true;
    world->ensure_atmosphere()->surface_pressure = 101325;

    Moon* luna = new Moon();
    strcpy(luna->name, "Test Moon");
    luna->namelen = 0;
    luna->mass = 7.342e25;
    luna->volumetric_mean_radius = 1.737e6;
    luna->height = 3.4e6;
    luna->type = rocky;
    luna->orbit = new Orbit();
    luna->orbit->center = world;
    luna->orbit->center_name = world->name;
    luna->orbit->semimajor_axis = 3.844e8;
    luna->cenobj = sun;
    append_cel(luna);

    ASSERT_TRUE(save_and_reload("roundtrip"));
    EXPECT_EQ(ncelobjs, 3);

    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->typeclass(), class_star);
    EXPECT_EQ(rsun->HD, 12345);
    EXPECT_DOUBLE_EQ(rsun->mass, solar_mass);
    EXPECT_DOUBLE_EQ(rsun->temperature, sun_temp);
    EXPECT_EQ(rsun->has_planets, 2);
    EXPECT_EQ(rsun->has_hz_planets, 1);

    Planet* rworld = (Planet*)find_loaded("Test World");
    ASSERT_NE(rworld, nullptr);
    EXPECT_EQ(rworld->typeclass(), class_planet);
    ASSERT_NE(rworld->orbit, nullptr);
    EXPECT_EQ(rworld->orbit->center, rsun);                 // relinked by name, not by pointer
    EXPECT_DOUBLE_EQ(rworld->orbit->semimajor_axis, AU);
    EXPECT_NEAR(rworld->orbit->eccentricity, 0.0167, 1e-12);
    EXPECT_NEAR(rworld->orbit->inclination, 0.5, 1e-12);    // radians out, degrees in the file
    EXPECT_NEAR(rworld->orbit->ascending_node, 1.25, 1e-12);
    EXPECT_NEAR(rworld->orbit->arg_periapsis, 2.5, 1e-12);
    EXPECT_DOUBLE_EQ(rworld->temperature, 288);
    EXPECT_DOUBLE_EQ(rworld->albedo, 0.306);
    EXPECT_EQ(rworld->asteroid_no, 4001);
    EXPECT_TRUE(rworld->lock_type);
    ASSERT_NE(rworld->atm, nullptr);
    EXPECT_DOUBLE_EQ(rworld->get_surface_pressure(), 101325);

    Moon* rluna = (Moon*)find_loaded("Test Moon");
    ASSERT_NE(rluna, nullptr);
    EXPECT_EQ(rluna->typeclass(), class_moon);
    ASSERT_NE(rluna->orbit, nullptr);
    EXPECT_EQ(rluna->orbit->center, rworld);                // two levels down, still linked
    EXPECT_DOUBLE_EQ(rluna->height, 3.4e6);

    delete_the_universe();
}

TEST_F(SerializationTest, RoundTripKeepsComets)
{
    Star* sun = make_star("Test Primary");

    Comet* c = new Comet();
    strcpy(c->name, "Test Comet");
    c->namelen = 0;
    c->designation = "C/2024 T1";
    c->H1 = 5.5; c->R1 = 10.0; c->D1 = 5.0;
    c->orbit = new Orbit();
    c->orbit->center = sun;
    c->orbit->center_name = sun->name;
    c->orbit->eccentricity = 1.2;                           // hyperbolic: a visitor
    c->orbit->periapsis_distance = 0.5 * AU;
    c->cenobj = sun;
    append_cel(c);

    ASSERT_TRUE(save_and_reload("comet"));

    Comet* rc = (Comet*)find_loaded("Test Comet");
    ASSERT_NE(rc, nullptr);
    EXPECT_EQ(rc->typeclass(), class_comet);
    EXPECT_EQ(rc->designation, "C/2024 T1");
    EXPECT_DOUBLE_EQ(rc->H1, 5.5);
    EXPECT_DOUBLE_EQ(rc->R1, 10.0);
    ASSERT_NE(rc->orbit, nullptr);
    EXPECT_TRUE(rc->orbit->is_open());

    delete_the_universe();
}

// =====================================================================
// The load loop's bookkeeping
// =====================================================================

TEST_F(SerializationTest, LoadKeepsAnObjectThatHasNoOrbit)
{
    Star* sun = make_star("Lone Star");
    make_planet(sun, "First Planet", AU);
    make_planet(sun, "Second Planet", 2*AU);
    make_planet(sun, "Third Planet", 3*AU);
    EXPECT_EQ(ncelobjs, 4);

    ASSERT_TRUE(save_and_reload("orbitless"));

    EXPECT_EQ(ncelobjs, 4);
    EXPECT_NE(find_loaded("Lone Star"), nullptr);
    EXPECT_NE(find_loaded("First Planet"), nullptr);
    EXPECT_NE(find_loaded("Second Planet"), nullptr);
    EXPECT_NE(find_loaded("Third Planet"), nullptr);

    delete_the_universe();
}

TEST_F(SerializationTest, LoadUpdatesAMatchingObjectInPlace)
{
    Star* sun = make_star("Test Primary");
    sun->absolute_magnitude = 4.83;
    ASSERT_TRUE(save_and_reload("inplace"));
    ASSERT_EQ(ncelobjs, 1);

    // Load the same file a second time on top of what it just produced.
    ASSERT_TRUE(load_file("inplace"));
    EXPECT_EQ(ncelobjs, 1) << "the same object loaded twice should not appear twice";

    delete_the_universe();
}

TEST_F(SerializationTest, LoadDoesNotWriteStarFieldsThroughSomethingThatIsNotAStar)
{
    Star* sun = make_star("Test Primary");
    Planet* world = make_planet(sun, "Test World", AU);

    Moon* stray = new Moon();
    strcpy(stray->name, "Stray Moon");
    stray->namelen = 0;
    stray->mass = 7.342e25;
    stray->volumetric_mean_radius = 1.737e6;
    // type deliberately left at the default, so it is its own light center.
    stray->orbit = new Orbit();
    stray->orbit->center = world;
    stray->orbit->center_name = world->name;
    stray->orbit->semimajor_axis = 3.844e8;
    stray->cenobj = sun;
    append_cel(stray);

    ASSERT_TRUE(save_and_reload("nonstar"));
    EXPECT_EQ(ncelobjs, 3);

    // Nothing was credited to the moon, and the star kept the count it was counted for.
    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->has_planets, 1) << "the planet, and not the stray moon as well";

    delete_the_universe();
}

// =====================================================================
// The planet tallies
// =====================================================================

TEST_F(SerializationTest, LoadDoesNotDoubleAStatedPlanetTally)
{
    Star* sun = make_star("Test Primary");
    make_planet(sun, "Planet One", AU);
    make_planet(sun, "Planet Two", 2*AU);
    make_planet(sun, "Planet Three", 3*AU);
    sun->has_planets = 3;
    sun->has_hz_planets = 1;

    ASSERT_TRUE(save_and_reload("tally"));

    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->has_planets, 3) << "the file's tally plus a recount would be six";
    EXPECT_EQ(rsun->has_hz_planets, 1);

    delete_the_universe();
}

TEST_F(SerializationTest, LoadCountsTheTallyWhenTheFileStatesNone)
{
    Star* sun = make_star("Test Primary");
    make_planet(sun, "Planet One", AU);
    make_planet(sun, "Planet Two", 2*AU);
    sun->has_planets = 2;

    std::string path = temp_universe("oldtally");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();
    delete_the_universe();

    json j = read_file("oldtally");
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        it.value().erase("has_planets");
        it.value().erase("has_hz_planets");
    }
    write_file("oldtally", j);

    ASSERT_TRUE(load_file("oldtally"));
    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->has_planets, 2) << "with nothing stated, the planets in the file are counted";

    delete_the_universe();
}

// =====================================================================
// Fields that used to be lost or mangled in the trip
// =====================================================================

TEST_F(SerializationTest, ZeroPrecessionSurvivesAsZeroAndNotInfinity)
{
    Star* sun = make_star("Test Primary");
    EXPECT_DOUBLE_EQ(sun->precession, 0);

    std::string path = temp_universe("precession");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();

    json j = read_file("precession");
    ASSERT_TRUE(j.begin().value().contains("precession"));
    EXPECT_FALSE(j.begin().value()["precession"].is_null()) << "a null here is a divide by zero";
    EXPECT_DOUBLE_EQ(j.begin().value()["precession"].get<double>(), 0);

    delete_the_universe();
    ASSERT_TRUE(load_file("precession"));
    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_FALSE(std::isinf(rsun->precession));
    EXPECT_FALSE(std::isnan(rsun->precession));
    EXPECT_DOUBLE_EQ(rsun->precession, 0);

    delete_the_universe();
}

TEST_F(SerializationTest, AStatedPrecessionPeriodRoundTrips)
{
    Star* sun = make_star("Test Primary");
    sun->precession = _pi * 2 / (25772 * oneyear);          // Earth's, in radians per second

    std::string path = temp_universe("precession2");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();

    json j = read_file("precession2");
    EXPECT_NEAR(j.begin().value()["precession"].get<double>(), 25772, 1e-6) << "written in years";

    delete_the_universe();
    ASSERT_TRUE(load_file("precession2"));
    EXPECT_NEAR(find_loaded("Test Primary")->precession, _pi * 2 / (25772 * oneyear), 1e-20);

    delete_the_universe();
}

TEST_F(SerializationTest, OldFileStatingZeroPrecessionLoadsAsZero)
{
    // 37 of the 38 objects in Koora.json state exactly this, from before the guard existed.
    make_star("Test Primary");
    std::string path = temp_universe("oldprecession");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();
    delete_the_universe();

    json j = read_file("oldprecession");
    for (auto it = j.begin(); it != j.end(); ++it) it.value()["precession"] = 0.0;
    write_file("oldprecession", j);

    ASSERT_TRUE(load_file("oldprecession"));
    CelestialObject* rsun = find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_FALSE(std::isinf(rsun->precession));
    EXPECT_DOUBLE_EQ(rsun->precession, 0);

    delete_the_universe();
}

// =====================================================================
// Saving only what was edited
// =====================================================================

TEST_F(SerializationTest, OnlyEditedWritesOnlyTheEditedObjects)
{
    // This is the mode the program actually saves in (loaders.cpp), so that a universe file holds
    // the user's own work and not a copy of every catalog it happened to have open.
    Star* kept = make_star("Edited Star");
    kept->user_edited = true;
    Star* skipped = make_star("Catalog Star");
    skipped->user_edited = false;

    std::string path = temp_universe("onlyedited");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, true));
    out.close();

    json j = read_file("onlyedited");
    EXPECT_EQ(j.size(), 1);
    EXPECT_TRUE(j.contains("Edited Star"));
    EXPECT_FALSE(j.contains("Catalog Star"));

    delete_the_universe();
}

// =====================================================================
// find_object()
// =====================================================================

class FindObjectTest : public UniverseFixture {};

TEST_F(FindObjectTest, ExactNameWins)
{
    Star* a = make_star("Alpha Centauri");
    Star* b = make_star("Beta Centauri");
    a->apparent_magnitude = 0;
    b->apparent_magnitude = 0.6;

    EXPECT_EQ(find_object("Alpha Centauri"), a->seqno);
    EXPECT_EQ(find_object("Beta Centauri"), b->seqno);

    delete_the_universe();
}

TEST_F(FindObjectTest, ToleratesAMisspelling)
{
    Star* a = make_star("Betelgeuse");
    a->apparent_magnitude = 0.5;

    // Damerau-Levenshtein: one transposition and one substitution, inside the default requirement.
    EXPECT_EQ(find_object("Betelgeuze"), a->seqno);

    delete_the_universe();
}

TEST_F(FindObjectTest, RefusesSomethingNothingLikeAnyOfThem)
{
    make_star("Betelgeuse");
    EXPECT_EQ(find_object("Nonexistent Wombat"), -1);

    delete_the_universe();
}

TEST_F(FindObjectTest, OnlyStarsSkipsThePlanets)
{
    Star* sun = make_star("Test Primary");
    Planet* p = make_planet(sun, "Wobbler", AU);

    EXPECT_EQ(find_object("Wobbler"), p->seqno);
    EXPECT_NE(find_object("Wobbler", true), p->seqno);      // only_stars: the planet is not eligible

    delete_the_universe();
}

TEST_F(FindObjectTest, ExactNameIgnoresTheMagnitudeLimit)
{
    Star* faint = make_star("Faint Thing");
    faint->apparent_magnitude = 14;

    EXPECT_EQ(find_object("Faint Thing", false, 6.0), faint->seqno);

    delete_the_universe();
}

// =====================================================================
// Idempotence: everything written is read back
// =====================================================================

template <class T> static void expect_json_idempotent(T& original, const char* what)
{
    json first = original.to_json();

    T restored;
    ASSERT_TRUE(restored.from_json(first)) << what;
    json second = restored.to_json();

    for (auto it = first.begin(); it != first.end(); ++it)
    {
        ASSERT_TRUE(second.contains(it.key())) << what << ": " << it.key() << " was not written back";
        const json& a = it.value();
        const json& b = second[it.key()];
        if (a.is_number() && b.is_number())
        {
            double x = a.get<double>(), y = b.get<double>();
            EXPECT_NEAR(x, y, std::fabs(x) * 1e-12 + 1e-12)
                << what << ": " << it.key() << " did not survive the round trip";
        }
        else EXPECT_EQ(a.dump(), b.dump())
            << what << ": " << it.key() << " did not survive the round trip";
    }
    for (auto it = second.begin(); it != second.end(); ++it)
        EXPECT_TRUE(first.contains(it.key())) << what << ": " << it.key() << " appeared out of nowhere";
}

class JsonIdempotenceTest : public UniverseFixture
{
    protected:
    // The fields every celestial object has, filled with values that are distinguishable from
    // each other and from any default.
    void fill_common(CelestialObject& cel, const char* name)
    {
        strcpy(cel.name, name);
        cel.origname = name;
        cel.origcenname = "Something Else";
        cel.mass = 1.234e27;
        cel.volumetric_mean_radius = 5.678e6;
        cel.oblateness = 0.00335;
        cel.temperature = 288;
        cel.sidereal_rotational_period = 86164.1;
        cel.right_ascension = 1.1;
        cel.declination = -0.4;
        cel.obliquity = 0.409;
        cel.equinox = 0.7;
        cel.lon_J2000_offset = 0.25;
        cel.precession = _pi * 2 / (25772 * oneyear);
        cel.distance = 12 * light_year;
        cel.distance_known = true;
        cel.epoch = 2451545.0;
        cel.absolute_magnitude = 4.83;
        cel.UB_color = 0.17;
        cel.BV_color = 0.65;
        cel.VR_color = 0.36;
        cel.RI_color = 0.32;
        cel.user_added = true;
        cel.location.galactic_center = Point(1, 2, 3);
        cel.location.system_center = Point(4e11, 5e11, 6e11);
        cel.location.local_position = Point(7, 8, 9);
    }
};

TEST_F(JsonIdempotenceTest, CelestialObject)
{
    CelestialObject cel;
    fill_common(cel, "Test Object");
    cel.type = rocky;
    expect_json_idempotent(cel, "CelestialObject");
}

TEST_F(JsonIdempotenceTest, Orbit)
{
    Orbit orbit;
    orbit.semimajor_axis = 2.5 * AU;
    orbit.eccentricity = 0.42;
    orbit.inclination = 0.3;
    orbit.ascending_node = 1.1;
    orbit.arg_periapsis = 2.2;
    orbit.mean_anomaly = 3.3;
    orbit.period = 4 * oneyear;
    orbit.epoch = 2451545.0;
    orbit.prec_node = 1e-9;
    orbit.proc_argperi = 2e-9;
    expect_json_idempotent(orbit, "Orbit");
}

TEST_F(JsonIdempotenceTest, Star)
{
    Star s;
    fill_common(s, "Test Star");
    s.type = star;
    strcpy(s.spectral_type, "G2V");
    strcpy(s.constellation, "Ori");
    strcpy(s.Bayer, "Alp Ori");
    strcpy(s.Flamsteed, "58 Ori");
    strcpy(s.Gliese, "Gl 999");
    strcpy(s.Bonn_survey, "BD");
    s.Bonn_survey_sign = '-';
    s.Bonn_survey_declination = 12;
    s.Bonn_survey_sequential = 3456;
    s.proper_motion_RA = 1e-12;
    s.proper_motion_decl = -2e-12;
    s.radial_velocity = 12345;
    s.apparent_magnitude = 4.5;
    s.parallax = 1e-7;
    s.HR = 1; s.HD = 2; s.HIP = 3; s.SAO = 4; s.SB9 = 5;
    s.CCDM = "12345+6789";
    s.BayerGrkno = 0;
    s.FlamsteedNo = 58;
    s.has_planets = 3;
    s.has_hz_planets = 1;
    s.is_orbit_multiple = true;
    expect_json_idempotent(s, "Star");
}

TEST_F(JsonIdempotenceTest, Planet)
{
    Planet p;
    fill_common(p, "Test Planet");
    p.type = rocky;
    p.albedo = 0.306;
    p.opposition_surge = 0.35;
    p.J2 = 0.0010826;
    p.asteroid_no = 4001;
    p.lock_type = true;
    Atmosphere* atm = p.ensure_atmosphere();
    atm->surface_pressure = 101325;
    atm->tau = 0.83;
    atm->particulates = 0.2;
    AtmosphereComposition* comp = atm->ensure_composition();
    comp->N2_portion = 0.78;
    comp->O2_portion = 0.21;
    comp->Ar_portion = 0.009;
    comp->CO2_portion = 0.001;
    expect_json_idempotent(p, "Planet");
}

TEST_F(JsonIdempotenceTest, Moon)
{
    Moon m;
    fill_common(m, "Test Moon");
    m.type = rocky;
    m.albedo = 0.12;
    m.height = 3.4e6;
    m.width = 3.5e6;
    m.depth = 3.3e6;
    expect_json_idempotent(m, "Moon");
}

TEST_F(JsonIdempotenceTest, Comet)
{
    Comet c;
    fill_common(c, "Test Comet");
    c.type = icy_tailed;
    c.designation = "C/2024 T1";
    c.H1 = 5.5; c.R1 = 10.0; c.D1 = 5.0;
    c.H2 = 14.0; c.R2 = 5.0; c.D2 = 5.0;
    expect_json_idempotent(c, "Comet");
}

TEST_F(JsonIdempotenceTest, Satellite)
{
    Satellite sat;
    fill_common(sat, "Test Satellite");
    sat.type = artificial;
    sat.bstar = 0.0001234;
    sat.mean_motion = 15.5 * 2 * _pi / oneday;
    expect_json_idempotent(sat, "Satellite");
}

TEST_F(JsonIdempotenceTest, Galaxy)
{
    Galaxy g;
    fill_common(g, "Test Galaxy");
    g.type = galaxy;
    expect_json_idempotent(g, "Galaxy");
}

TEST_F(JsonIdempotenceTest, Atmosphere)
{
    CelestialObject owner;
    Atmosphere atm(&owner);
    atm.surface_pressure = 9200000;             // Venus
    atm.tau = 4.2;
    atm.particulates = 0.9;
    AtmosphereComposition* comp = atm.ensure_composition();
    comp->CO2_portion = 0.965;
    comp->N2_portion = 0.035;

    // Atmosphere::from_json() has no default constructor to restore into, so this one is done by
    // hand rather than through the template.
    json first = atm.to_json();
    Atmosphere restored(&owner);
    ASSERT_TRUE(restored.from_json(first));
    EXPECT_EQ(first.dump(), restored.to_json().dump());
}
