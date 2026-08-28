#ifndef _AlienorumTestUniverseFixture
#define _AlienorumTestUniverseFixture

#include <cstring>
#include <gtest/gtest.h>
#include "../classes/serial.h"

class UniverseFixture : public ::testing::Test
{
    protected:
    void SetUp() override
    {
        empty_the_universe();
    }

    void TearDown() override
    {
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

        sat_data.clear();
        sat_sources.clear();
        best_source.clear();
    }

    void delete_the_universe()
    {
        for (int i=0; cels[i]; i++) delete cels[i];
        empty_the_universe();
    }

    Star* make_star(const char* name = "Test Primary", double mass = solar_mass)
    {
        Star* s = new Star();
        strcpy(s->name, name);
        s->namelen = 0;
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
        p->namelen = 0;
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
