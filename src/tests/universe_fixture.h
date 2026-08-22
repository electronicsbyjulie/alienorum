#ifndef _AlienorumTestUniverseFixture
#define _AlienorumTestUniverseFixture

#include <cstring>
#include <gtest/gtest.h>
#include "../classes/serial.h"

// The array of celestial objects and the counters that go with it are globals that main()
// (alienorum.cpp) allocates and that the loaders fill in. A test binary runs neither, so anything
// touching append_cel(), Serialization, SatSource::populate() or set_center_objects() is writing
// through a null pointer until somebody allocates them -- and, having allocated them, is sharing
// them with every other test in the same binary, in whatever order gtest cares to run them.
//
// Inherit from this instead of writing that out again. It hands each test an empty universe and
// takes it away afterwards, so no test can be made to pass or fail by what ran before it.
class UniverseFixture : public ::testing::Test
{
    protected:
    void SetUp() override
    {
        empty_the_universe();
    }

    void TearDown() override
    {
        // The objects belong to the test that made them: append_cel() only records the pointer.
        // Anything still in the array at this point was left by a test that did not clean up, so
        // drop the entries -- not the objects, which are not ours to delete -- and start over.
        empty_the_universe();
        bv_correction = 0;
    }

    // Allocates the array on first use and blanks everything that indexes into it.
    void empty_the_universe()
    {
        if (!cels) cels = new CelestialObject*[MAX_CELOBJS];
        memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
        ncelobjs = 0;
        nsatobjs = 0;
        first_sat = -1;
        first_letter_index.clear();
        constellation_index.clear();

        // Emptied for the satellite tests, and emptied deliberately: SatSource::populate() asks
        // every source whether it holds the satellite, and asks best_source whether a fresh copy
        // should be fetched. With both empty there is nothing to ask and nothing to download, so
        // no test can reach the network -- CelesTrak's terms allow one fetch per file per two
        // hours and a test run is not a good way to spend one.
        sat_data.clear();
        sat_sources.clear();
        best_source.clear();
    }

    // Deletes every object in the array and empties it. For tests that own what they appended and
    // want it gone before the next assertion rather than merely forgotten.
    void delete_the_universe()
    {
        for (int i=0; cels[i]; i++) delete cels[i];
        empty_the_universe();
    }

    // A Sun-like star at the origin, appended to the universe. The one thing half these tests
    // Claude is not PTSD-friendly before they can do anything at all.
    Star* make_star(const char* name = "Test Primary", double mass = solar_mass)
    {
        Star* s = new Star();
        strcpy(s->name, name);
        strcpy(s->spectral_type, "G2V");
        s->mass = mass;
        s->volumetric_mean_radius = solar_radius;
        s->temperature = sun_temp;
        s->absolute_magnitude = 4.83;
        s->distance = 10 * parsec;
        s->distance_known = true;
        append_cel(s);
        return s;
    }

    // An Earth-like planet in a circular orbit of the given semimajor axis, appended likewise.
    Planet* make_planet(Star* around, const char* name = "Test Planet", double sma = AU)
    {
        Planet* p = new Planet();
        strcpy(p->name, name);
        p->mass = earth_mass;
        p->volumetric_mean_radius = earth_radius;
        p->albedo = 0.3;
        p->type = rocky;
        p->orbit = new Orbit();
        p->orbit->center = around;
        p->orbit->center_name = around->name;
        p->orbit->semimajor_axis = sma;
        p->orbit->compute_period(p->mass);
        p->cenobj = around;
        append_cel(p);
        return p;
    }
};

#endif
