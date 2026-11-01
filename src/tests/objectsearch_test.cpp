#include <chrono>
#include <cstring>
#include <iostream>
#include "universe_fixture.h"
#include "../classes/objectsearch.h"
#include "../classes/moon.h"
#include "../classes/galaxy.h"
#include "../inputs.h"
#include "../dialogs.h"

using namespace alienorum;

class ObjectSearchTest : public UniverseFixture
{
    protected:
    void SetUp() override
    {
        UniverseFixture::SetUp();
        close_object_search_results();
        selected = trackidx = whereami = editidx = -1;
        searched = lookfor_notfound = objedtwnd = false;
        view_mode = vm_spaceship;
        here.local_position = Point(0, 0, 0);
    }

    void TearDown() override
    {
        close_object_search_results();
        selected = trackidx = whereami = editidx = -1;
        objedtwnd = false;
        delete_the_universe();
        UniverseFixture::TearDown();
    }

    Moon* make_moon(Planet* parent, const char* name)
    {
        auto* moon = new Moon;
        strcpy(moon->name, name);
        moon->orbit = new Orbit;
        moon->orbit->center = parent;
        append_cel(moon);
        return moon;
    }

    Galaxy* make_galaxy(const char* name)
    {
        auto* galaxy = new Galaxy;
        strcpy(galaxy->name, name);
        append_cel(galaxy);
        return galaxy;
    }

    void submit(const char* query)
    {
        snprintf(lookfor, sizeof(lookfor), "%s", query);
        do_find();
    }
};

TEST_F(ObjectSearchTest, ExactNamesAndCatalogAliasesIgnoreCaseAndWhitespace)
{
    Star* star = make_star("Tau Ceti");
    star->HD = 10700;
    star->add_catalog_alias("HD 10700");
    auto* galaxy = make_galaxy("IC 1613");
    for (const char* query : {"tauCETI", "  hD  10700 ", "HD10700", "HD 010700"})
    {
        const auto result = search_objects(query);
        ASSERT_EQ(result.state, ObjectSearchState::single);
        ASSERT_EQ(result.matches.size(), 1u);
        EXPECT_EQ(result.matches[0].index, star->seqno);
        EXPECT_TRUE(result.exact);
    }
    const auto result = search_objects("ic1613");
    ASSERT_EQ(result.state, ObjectSearchState::single);
    EXPECT_EQ(result.matches[0].index, galaxy->seqno);
    for (const char* query : {"Tau Ctei", "Tau Cetiq", "Tau Cti", "Tau Cetx"})
    {
        const auto suggestion = search_objects(query);
        ASSERT_EQ(suggestion.state, ObjectSearchState::single) << query;
        EXPECT_EQ(suggestion.matches.front().index, star->seqno);
        EXPECT_EQ(suggestion.matches.front().distance, 1) << query;
    }
}

TEST_F(ObjectSearchTest, ExactAmbiguityCountsObjectsBeforeTheDisplayLimit)
{
    Star* star = make_star("Atlas");
    star->add_catalog_alias("AT LAS");
    Planet* parent = make_planet(star, "Saturn");
    Moon* moon = make_moon(parent, "Atlas");
    const auto result = search_objects("atlas", 1);
    EXPECT_EQ(result.state, ObjectSearchState::multiple);
    EXPECT_TRUE(result.exact);
    EXPECT_EQ(result.total, 2u);
    ASSERT_EQ(result.matches.size(), 2u);
    EXPECT_NE(result.matches[0].type, result.matches[1].type);
    moon->deleted = true;
    const auto remaining = search_objects("atlas");
    EXPECT_EQ(remaining.state, ObjectSearchState::single);
    EXPECT_EQ(remaining.total, 1u);
}

TEST_F(ObjectSearchTest, ApproximateSuggestionsUseSpellingDistanceWithoutSpecialCases)
{
    Star* star = make_star("Alp Ori");
    star->local_name = "Betelgeuse";
    const auto result = search_objects("beetlejuice");
    ASSERT_FALSE(result.matches.empty());
    EXPECT_EQ(result.matches.front().index, star->seqno);
    EXPECT_FALSE(result.exact);
    EXPECT_EQ(result.state, ObjectSearchState::single);
    EXPECT_EQ(search_objects("UnrelatedWombat").state, ObjectSearchState::absent);
}

TEST_F(ObjectSearchTest, ApproximateCountsAreDistinctAndIndependentOfTruncation)
{
    Star* first = make_star("Arcturus");
    first->add_catalog_alias("Arcturos");
    EXPECT_EQ(search_objects("Arcturss").total, 1u);
    make_star("Arcturis");
    const auto result = search_objects("Arcturss", 1);
    EXPECT_EQ(result.state, ObjectSearchState::multiple);
    EXPECT_EQ(result.total, 2u);
    EXPECT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches.front().primary_name, "Arcturis");
    EXPECT_EQ(search_objects("Arcturss", 1).matches.front().index, result.matches.front().index);
    EXPECT_TRUE(search_objects("Arcturus").exact);
    EXPECT_EQ(search_objects("Arcturus").total, 1u);
}

TEST_F(ObjectSearchTest, CatalogPrefixesMayChangeButNumbersAndComponentsStayProtected)
{
    make_galaxy("IC 1613");
    Star* star = make_star("HD 1613");
    star->HD = 1613;
    make_galaxy("IC 1614");
    const auto result = search_objects("NGC 1613");
    EXPECT_EQ(result.total, 2u);
    EXPECT_FALSE(result.exact);
    EXPECT_EQ(search_objects("NGC 1615").state, ObjectSearchState::absent);
    make_star("HD 10700 A");
    EXPECT_EQ(search_objects("HD 10700 B").state, ObjectSearchState::absent);
    make_star("Alp Cen A");
    EXPECT_EQ(search_objects("Alph Cen B").state, ObjectSearchState::absent);
    make_star("BD+10 123");
    EXPECT_EQ(search_objects("BD-10 123").state, ObjectSearchState::absent);
}

TEST_F(ObjectSearchTest, ShortExemptionsAreExactOnlyAndReflectCatalogChanges)
{
    Star* host = make_star();
    Moon* io = make_moon(make_planet(host), "Io");
    make_galaxy("M1");
    host->add_catalog_alias("Ab");
    EXPECT_EQ(search_objects(" i O ").matches.front().index, io->seqno);
    EXPECT_EQ(search_objects(" m 1 ").state, ObjectSearchState::single);
    EXPECT_EQ(search_objects("b").state, ObjectSearchState::rejected);
    EXPECT_EQ(search_objects("Ab").state, ObjectSearchState::rejected);
    EXPECT_EQ(search_objects("Lo").state, ObjectSearchState::rejected);
    EXPECT_EQ(search_objects(" \t ").state, ObjectSearchState::blank);
    io->deleted = true;
    EXPECT_EQ(search_objects("Io").state, ObjectSearchState::rejected);
    auto* added = make_galaxy("M2");
    EXPECT_EQ(search_objects("M2").matches.front().index, added->seqno);
}

TEST_F(ObjectSearchTest, RenamesAliasesAndReplacedSlotsCannotLeaveAnActionableStaleTarget)
{
    Star* star = make_star("Old Name");
    const auto old = search_objects("Old Name").matches.front();
    strcpy(star->name, "New Name");
    EXPECT_EQ(resolve_object_search_match(old), nullptr);
    EXPECT_EQ(search_objects("New Name").matches.front().index, star->seqno);
    star->add_catalog_alias("Fresh Alias");
    EXPECT_EQ(search_objects("FreshAlias").state, ObjectSearchState::single);
    const auto before = search_objects("New Name").matches.front();
    delete star;
    ncelobjs = 0;
    cels[0] = nullptr;
    make_star("New Name");
    EXPECT_EQ(resolve_object_search_match(before), nullptr);
    EXPECT_FALSE(apply_object_search_action(before, ObjectSearchAction::rise));
    EXPECT_FALSE(cels[0]->alert_rise);
}

TEST_F(ObjectSearchTest, InteractiveRoutingDoesNotMoveTheViewForAmbiguousOrInvalidQueries)
{
    Star* host = make_star("Atlas");
    Planet* parent = make_planet(host, "Saturn");
    make_moon(parent, "Atlas");
    selected = trackidx = parent->seqno;
    azimuth = .3;
    altitude = .4;
    submit("atlas");
    EXPECT_TRUE(object_search_results_open());
    EXPECT_EQ(selected, parent->seqno);
    EXPECT_EQ(trackidx, parent->seqno);
    EXPECT_DOUBLE_EQ(azimuth, .3);
    EXPECT_DOUBLE_EQ(altitude, .4);
    submit("NoSuchWombatAnywhere");
    EXPECT_FALSE(object_search_results_open());
    EXPECT_TRUE(lookfor_notfound);
    EXPECT_EQ(selected, parent->seqno);
    submit("b");
    EXPECT_TRUE(lookfor_notfound);
    submit("   ");
    EXPECT_FALSE(lookfor_notfound);
    EXPECT_FALSE(object_search_results_open());
    EXPECT_DOUBLE_EQ(azimuth, .3);
    EXPECT_DOUBLE_EQ(altitude, .4);
}

TEST_F(ObjectSearchTest, EveryResultActionUsesTheChosenLiveObject)
{
    Star* host = make_star();
    host->location.local_position = Point(1e11, 1e11, 1e11);
    Planet* planet = make_planet(host, "Action target");
    planet->location.local_position = Point(1e10, 1e10, 1e10);
    const auto target = search_objects("Action target").matches.front();
    selected = host->seqno;
    azimuth = .3;
    altitude = .4;
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::rise));
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::set));
    EXPECT_TRUE(planet->alert_rise);
    EXPECT_TRUE(planet->alert_set);
    EXPECT_EQ(selected, host->seqno);
    EXPECT_DOUBLE_EQ(azimuth, .3);
    EXPECT_DOUBLE_EQ(altitude, .4);
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::edit));
    EXPECT_TRUE(objedtwnd);
    EXPECT_EQ(editidx, planet->seqno);
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::select));
    EXPECT_EQ(selected, planet->seqno);
    EXPECT_DOUBLE_EQ(azimuth, .3);
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::track));
    EXPECT_EQ(trackidx, planet->seqno);
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::focus));
    EXPECT_EQ(selected, planet->seqno);
    EXPECT_EQ(trackidx, -1);
    EXPECT_TRUE(searched);
    ASSERT_TRUE(apply_object_search_action(target, ObjectSearchAction::go));
    EXPECT_EQ(whereami, planet->seqno);
    EXPECT_EQ(selected, -1);
    EXPECT_EQ(trackidx, -1);
    EXPECT_EQ(zoom, 1);
}

TEST_F(ObjectSearchTest, SnapshotSearchHandlesALargeCatalog)
{
    for (int i = 0; i < 20000; ++i)
    {
        Star* star = make_star(("HD " + std::to_string(100000 + i)).c_str());
        star->HD = 100000 + i;
    }
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(search_objects("hd119999").matches.front().index, 19999);
    EXPECT_EQ(search_objects("UnrelatedWombat").state, ObjectSearchState::absent);
    std::cout << "Exact and approximate searches across 20000 objects: "
        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms\n";
}
