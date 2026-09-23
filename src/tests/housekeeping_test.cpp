#include <cstring>
#include <gtest/gtest.h>
#include "../classes/star.h"
#include "../housekeeping.h"
#include "universe_fixture.h"

using namespace alienorum;

// set_center_objects() is the pass that runs once loading has settled: it rebuilds the name and
// constellation indices, gives every object a system center, and repairs the handful of things a
// catalog can state that cannot be true. Nothing tested it, and the tests elsewhere that say "the
// indices are this function's doing, not append_cel()'s" were taking that on trust.

class HousekeepingTest : public UniverseFixture {};

TEST_F(HousekeepingTest, BuildsTheFirstLetterIndex)
{
    Star* sun = make_star("Sol");
    Star* rigel = make_star("Rigel");
    Star* betelgeuse = make_star("Betelgeuse");
    Star* numbered = make_star("61 Cygni");

    set_center_objects();

    // Thirty-six buckets: ten digits and twenty-six letters, folded to one case.
    ASSERT_EQ(first_letter_index.size(), 36u);

    auto bucket_of = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        return c - 'a' + 10;
    };

    ASSERT_FALSE(first_letter_index[bucket_of('R')].empty());
    EXPECT_EQ(first_letter_index[bucket_of('R')].back(), rigel);
    ASSERT_FALSE(first_letter_index[bucket_of('B')].empty());
    EXPECT_EQ(first_letter_index[bucket_of('B')].back(), betelgeuse);
    ASSERT_FALSE(first_letter_index[bucket_of('S')].empty());
    EXPECT_EQ(first_letter_index[bucket_of('S')].back(), sun);

    // A name that begins with a digit goes in the digit's bucket, not a letter's.
    ASSERT_FALSE(first_letter_index[bucket_of('6')].empty());
    EXPECT_EQ(first_letter_index[bucket_of('6')].back(), numbered);

    delete_the_universe();
}

TEST_F(HousekeepingTest, IndexesStarsByConstellation)
{
    make_star("Sol");
    Star* rigel = make_star("Rigel");
    strcpy(rigel->constellation, "Ori");
    Star* saiph = make_star("Saiph");
    strcpy(saiph->constellation, "Ori");
    Star* deneb = make_star("Deneb");
    strcpy(deneb->constellation, "Cyg");

    set_center_objects();

    ASSERT_EQ(constellation_index["Ori"].size(), 2u);
    EXPECT_EQ(constellation_index["Cyg"].size(), 1u);
    EXPECT_EQ(constellation_index["Cyg"].back(), deneb);
    EXPECT_TRUE(constellation_index["Tau"].empty());

    // And the same star is not counted twice when the pass runs again, which it does after every
    // catalog and every universe file.
    set_center_objects();
    EXPECT_EQ(constellation_index["Ori"].size(), 2u)
        << "the index is rebuilt rather than appended to";

    delete_the_universe();
}

TEST_F(HousekeepingTest, GivesEveryObjectASystemCenter)
{
    Star* sun = make_star("Sol");
    Planet* world = make_planet(sun, "Test World", AU);
    world->cenobj = nullptr;                            // as an object straight out of a file has

    set_center_objects();

    // A star is the center of its own system; anything else inherits one.
    EXPECT_EQ(sun->cenobj, sun);
    EXPECT_NE(world->cenobj, nullptr);

    delete_the_universe();
}

TEST_F(HousekeepingTest, BreaksAnOrbitAroundItself)
{
    // A catalog cross-reference that resolves to the object it came from would otherwise be an
    // orbit with no center of mass but its own, which the position code cannot solve at all.
    Star* sun = make_star("Sol");
    Planet* world = make_planet(sun, "Test World", AU);
    world->orbit->center = world;

    set_center_objects();

    EXPECT_EQ(world->orbit, nullptr) << "an orbit around itself is discarded, not solved";

    delete_the_universe();
}

TEST_F(HousekeepingTest, SkipsDeletedObjects)
{
    make_star("Sol");
    Star* gone = make_star("Deleted Star");
    gone->deleted = true;

    set_center_objects();

    // Nothing that has been deleted is put back into the indices for the search to find.
    for (const auto& bucket : first_letter_index)
        for (CelestialObject* c : bucket)
            EXPECT_NE(c, gone) << "a deleted object was indexed";

    delete_the_universe();
}

TEST_F(HousekeepingTest, ExcludesFaintStarsByDefaultAndIncludesWhenZoomed)
{
    Star* sol = make_star("Sol");
    sol->distance = 0;
    here = sol->location;
    mycenobj = sol;
    whereami = 0;

    Star* bright = make_star("Bright Star");
    bright->apparent_magnitude = 5.5;
    bright->distance = 15 * parsec;
    bright->location.system_center = Point(0, 0, 15 * parsec);

    Star* faint = make_star("Faint Star");
    faint->apparent_magnitude = 8.5;
    faint->distance = 50 * parsec;
    faint->location.system_center = Point(0, 0, 50 * parsec);

    zoom = 1.0;
    global_brightness = 1.0;
    view_mode = vm_skymap;

    visible_cels.clear();
    update_visible_cels();

    bool found_bright = false;
    bool found_faint = false;
    for (CelestialObject* cel : visible_cels)
    {
        if (cel == bright)
        {
            found_bright = true;
        }
        if (cel == faint)
        {
            found_faint = true;
        }
    }
    EXPECT_TRUE(found_bright);
    EXPECT_FALSE(found_faint);

    // Zooming in raises magnitude cutoff, allowing faint star to appear
    zoom = 5.0;
    update_visible_cels();

    found_faint = false;
    for (CelestialObject* cel : visible_cels)
    {
        if (cel == faint)
        {
            found_faint = true;
        }
    }
    EXPECT_TRUE(found_faint);

    delete_the_universe();
}

TEST_F(HousekeepingTest, ExcludesMoonsUntilOrbitSubtendsSufficientPixels)
{
    Star* sol = make_star("Sol");
    sol->distance = 0;
    here = sol->location;
    mycenobj = sol;
    whereami = 0;

    Planet* jupiter = make_planet(sol, "Jupiter", 5.2 * AU);
    jupiter->volumetric_mean_radius = 7.15e7;
    jupiter->location.system_center = Point(0, 0, 5.2 * AU);

    // Callisto orbit sma ~ 1.88e9 m
    Moon* callisto = make_moon(jupiter, "Callisto", 1.88e9);
    callisto->location.system_center = jupiter->location.system_center + Point(1.88e9, 0, 0);

    dispcx = 960;
    dispcy = 540;
    zoom = 1.0;
    view_mode = vm_skymap;

    visible_cels.clear();
    update_visible_cels();

    bool found_callisto = false;
    for (CelestialObject* cel : visible_cels)
    {
        if (cel == callisto)
        {
            found_callisto = true;
        }
    }
    EXPECT_FALSE(found_callisto);

    // Zooming in magnifies the subtended pixels of Callisto's orbit
    zoom = 5.0;
    update_visible_cels();

    found_callisto = false;
    for (CelestialObject* cel : visible_cels)
    {
        if (cel == callisto)
        {
            found_callisto = true;
        }
    }
    EXPECT_TRUE(found_callisto);

    delete_the_universe();
}

TEST_F(HousekeepingTest, GatesStellarMotionOnDistanceAndProperMotion)
{
    Star* sol = make_star("Sol");
    sol->distance = 0;
    here = sol->location;
    mycenobj = sol;
    whereami = 0;

    Star* distant = make_star("Distant Slow Star");
    distant->distance = 500 * light_year;
    distant->location.system_center = Point(0, 0, 500 * light_year);
    distant->proper_motion_RA = 0;
    distant->proper_motion_decl = 0;
    distant->make_universally_visible();

    Star* nearby = make_star("Nearby Fast Star");
    nearby->distance = 4.0 * light_year;
    nearby->location.system_center = Point(0, 0, 4.0 * light_year);
    nearby->proper_motion_RA = 1e-12;
    nearby->proper_motion_decl = 0;
    nearby->make_universally_visible();

    redo_proper_motions = false;

    // Distant star with negligible motion skips location calculation
    EXPECT_FALSE(compute_object_location(distant));

    // Nearby star within 50 ly is always computed
    EXPECT_TRUE(compute_object_location(nearby));

    // Setting redo_proper_motions (such as century step Z/Shift+Z) forces computation
    redo_proper_motions = true;
    EXPECT_TRUE(compute_object_location(distant));

    delete_the_universe();
}
