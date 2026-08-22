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
