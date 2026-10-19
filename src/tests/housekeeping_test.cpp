#include <cstring>
#include <iostream>
#include <sstream>
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

TEST_F(HousekeepingTest, BrightCompanionReportIsGatedAndListsEachSystemOnce)
{
    make_star("Sol")->distance = 0;
    Star *primary = make_star("Review System");
    primary->absolute_magnitude = primary->apparent_magnitude = 5;
    primary->set_component('A', primary);
    Star *secondary = make_star("Review System B");
    secondary->make_companion_of(primary, 'B');
    secondary->absolute_magnitude = secondary->apparent_magnitude = 4;
    Star *brightest = make_star("Review System C");
    brightest->make_companion_of(primary, 'C');
    brightest->absolute_magnitude = brightest->apparent_magnitude = 2;
    Star *normal = make_star("Normal System");
    normal->absolute_magnitude = normal->apparent_magnitude = 1;
    normal->set_component('A', normal);
    Star *faint = make_star("Normal System B");
    faint->make_companion_of(normal, 'B');
    faint->absolute_magnitude = faint->apparent_magnitude = 3;
    std::ostringstream output;
    std::streambuf *original = std::cerr.rdbuf(output.rdbuf());
    const bool saved_showfails = showfails;
    showfails = false;
    set_center_objects();
    EXPECT_TRUE(output.str().empty());
    showfails = true;
    set_center_objects();
    showfails = saved_showfails;
    std::cerr.rdbuf(original);
    const std::string report = output.str();
    EXPECT_NE(report.find("Review System (primary Mv 5)"), std::string::npos);
    EXPECT_NE(report.find("Review System C (companion Mv 2)"), std::string::npos);
    EXPECT_EQ(report.find("Normal System"), std::string::npos);
    EXPECT_EQ(std::count(report.begin(), report.end(), '\n'), 1);
    EXPECT_EQ(primary->multisys->get_member('A'), primary);
    EXPECT_EQ(brightest->orbit->center, primary);
    delete_the_universe();
}

TEST_F(HousekeepingTest, CompanionBrightnessUsesSystemDistance)
{
    Star *sun = make_star("Sol");
    sun->distance = 0;
    Star *primary = make_star("Tau Ophiuchi");
    primary->distance = 56.234 * light_year;
    primary->apparent_magnitude = 5.24;
    primary->absolute_magnitude = 4.06;
    Star *companion = make_star("Tau Ophiuchi B");
    strcpy(companion->spectral_type, "F5III+...");
    companion->distance = 179.32 * light_year;
    companion->apparent_magnitude = 5.94;
    companion->absolute_magnitude = 1.07;
    companion->orbit = new Orbit();
    companion->orbit->center = primary;
    companion->orbit->period = 8191.47 * oneday;
    companion->orbit->semimajor_axis = 10 * AU;

    set_center_objects();
    EXPECT_NEAR(companion->viewer_magnitude(sun->location), 5.94, 1e-5);
    EXPECT_EQ(companion->cenobj, primary);
    EXPECT_DOUBLE_EQ(primary->absolute_magnitude, 4.06);
    set_center_objects();
    EXPECT_NEAR(companion->viewer_magnitude(sun->location), 5.94, 1e-5);

    const double reference_magnitude = companion->absolute_magnitude;
    const double original_time = simnow;
    primary->radial_velocity = 40000;
    simnow += 1000 * oneyear;
    set_center_objects();
    EXPECT_DOUBLE_EQ(companion->absolute_magnitude, reference_magnitude);
    simnow = original_time;
    primary->radial_velocity = 0;

    // A measured companion can be brighter than its primary; zero is also a valid magnitude.
    companion->apparent_magnitude = 0;
    set_center_objects();
    EXPECT_NEAR(companion->viewer_magnitude(sun->location), 0, 1e-5);

    // A companion with only a published intrinsic luminosity keeps that luminosity.
    companion->apparent_magnitude = 1e29;
    companion->absolute_magnitude = 2;
    set_center_objects();
    EXPECT_DOUBLE_EQ(companion->absolute_magnitude, 2);
    delete_the_universe();
}

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
