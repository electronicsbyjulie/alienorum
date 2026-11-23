#include "../performance_options.h"
#include "../catalog_lock_trace.h"
#include <thread>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "../classes/star.h"
#include "../housekeeping.h"
#include "../inputs.h"
#include "universe_fixture.h"

using namespace alienorum;

extern double celmasslim;

// set_center_objects() is the pass that runs once loading has settled: it rebuilds the name and
// constellation indices, gives every object a system center, and repairs the handful of things a
// catalog can state that cannot be true. Nothing tested it, and the tests elsewhere that say "the
// indices are this function's doing, not append_cel()'s" were taking that on trust.

class HousekeepingTest : public UniverseFixture {};

namespace
{
    struct CandidateFrameRestore
    {
        CelestialObject* root = mycenobj;
        CelestialLocation position = here;
        int place = whereami, selection = selected, tracking = trackidx, editing = editidx;
        double magnification = zoom, width = dispcx;
        ViewMode mode = view_mode;
        ~CandidateFrameRestore()
        {
            mycenobj = root;
            here = position;
            whereami = place;
            selected = selection;
            trackidx = tracking;
            editidx = editing;
            zoom = magnification;
            dispcx = width;
            view_mode = mode;
            invalidate_frame_objects();
        }
    };
}

TEST_F(HousekeepingTest, FrameCandidatesPreserveOrderAndSystemActivationRules)
{
    CandidateFrameRestore restore;
    Star* local = make_star("Candidate local");
    Star* remote = make_star("Candidate remote");
    local->cenobj = local;
    remote->cenobj = remote;
    remote->location.system_center = Point(0, 0, 100 * light_year);
    Planet* near = make_planet(local, "Local world");
    Planet* far = make_planet(remote, "Remote world");
    Planet* sibling = make_planet(remote, "Remote sibling", 2 * AU);
    mycenobj = local;
    here = CelestialLocation();
    whereami = selected = trackidx = editidx = -1;
    zoom = 1;
    dispcx = 640;
    view_mode = vm_spaceship;
    const auto contains = [](const std::vector<int>& values, int index)
    {
        return std::find(values.begin(), values.end(), index) != values.end();
    };
    EXPECT_TRUE(contains(frame_object_candidates(), near->seqno));
    EXPECT_FALSE(contains(frame_object_candidates(), far->seqno));
    selected = remote->seqno;
    EXPECT_FALSE(contains(frame_object_candidates(), far->seqno));
    zoom = 1e8;
    EXPECT_TRUE(contains(frame_object_candidates(), far->seqno));
    zoom = 2;
    here = remote->location;
    here.local_position = Point(0, 0, AU);
    EXPECT_TRUE(contains(frame_object_candidates(), far->seqno));
    here = CelestialLocation();
    EXPECT_FALSE(contains(frame_object_candidates(), far->seqno));
    zoom = 1;
    selected = far->seqno;
    EXPECT_TRUE(contains(frame_object_candidates(), sibling->seqno));
    EXPECT_TRUE(std::is_sorted(frame_object_candidates().begin(), frame_object_candidates().end()));
    Planet* added = make_planet(remote, "Added after caching", 3 * AU);
    EXPECT_TRUE(contains(frame_object_candidates(), added->seqno));
    selected = -1;
    added->cenobj = local;
    added->orbit->center = local;
    added->sync_host_statistics();
    EXPECT_TRUE(contains(frame_object_candidates(), added->seqno));
    EXPECT_EQ(added->get_light_center(), local);
    selected = far->seqno;
    added->deleted = true;
    invalidate_frame_objects();
    EXPECT_FALSE(contains(frame_object_candidates(), added->seqno));
    selected = -1;
    editidx = far->seqno;
    EXPECT_TRUE(contains(frame_object_candidates(), sibling->seqno));
    editidx = -1;
    trackidx = far->seqno;
    EXPECT_TRUE(contains(frame_object_candidates(), sibling->seqno));
    trackidx = -1;
    EXPECT_FALSE(contains(frame_object_candidates(), sibling->seqno));
    delete_the_universe();
}

TEST_F(HousekeepingTest, QueuedInactiveOrbitAndHostEditsReconcileWithoutMovingThePlanet)
{
    CandidateFrameRestore restore;
    Star* local = make_star("Queue observer");
    Star* host = make_star("Queue host");
    Planet* planet = make_planet(host, "Queued world");
    local->cenobj = local;
    host->cenobj = host;
    mycenobj = local;
    whereami = selected = trackidx = editidx = -1;
    zoom = 1;
    const CelestialLocation position = planet->location;
    ASSERT_EQ(host->has_hz_planets, 1);
    planet->orbit->semimajor_axis = 4 * AU;
    planet->orbit->compute_period(planet->mass);
    Planet::reconcile_host_statistics();
    EXPECT_EQ(host->has_hz_planets, 0);
    planet->orbit->period = oneyear;
    planet->orbit->compute_semimajor_axis(planet->mass);
    Planet::reconcile_host_statistics();
    EXPECT_EQ(host->has_hz_planets, 1);
    host->absolute_magnitude = 0;
    host->update_location(simnow);
    Planet::reconcile_host_statistics();
    EXPECT_EQ(host->has_hz_planets, 0);
    EXPECT_EQ(planet->location.distance_to(position), 0);
    planet->set_explicit_mass(planet_count_mass_cutoff / 2);
    Planet::reconcile_host_statistics();
    EXPECT_EQ(host->has_planets, 0);
    delete_the_universe();
}

TEST_F(HousekeepingTest, FlightDisplacementTracksElapsedTimeAcrossFrameCadences)
{
    for (double speed : {0.0, 0.3*speed_of_light, 0.999*speed_of_light,
        1.00001*speed_of_light, 100*speed_of_light})
    {
        Point motion(1, -2, 3);
        motion.scale(speed/target_frame_rate);
        Point nominal = motion;
        if (speed < speed_of_light) nominal.scale(nominal.magnitude()/compute_time_dilation(speed));
        const Point expected = nominal*target_frame_rate;
        for (int cadence : {10, 30, 60})
        {
            Point integrated;
            for (int frame = 0; frame < cadence; ++frame)
                integrated += viewer_displacement(motion, 1.0/cadence);
            EXPECT_NEAR(integrated.distance_to(expected), 0, std::max(1e-9, speed*1e-12));
        }
        EXPECT_EQ(viewer_displacement(motion, 0).magnitude(), 0);
        EXPECT_NEAR(viewer_displacement(motion, 1.0/target_frame_rate).distance_to(nominal),
            0, std::max(1e-9, speed*1e-12));
    }
}

TEST_F(HousekeepingTest, CulledPlanetsRefreshHzCountsAfterDirectOrbitChanges)
{
    struct RestoreFrame
    {
        CelestialObject* center = mycenobj;
        int place = whereami, selection = selected, tracking = trackidx, editing = editidx;
        ViewMode mode = view_mode;
        ~RestoreFrame()
        {
            mycenobj = center;
            whereami = place;
            selected = selection;
            trackidx = tracking;
            editidx = editing;
            view_mode = mode;
        }
    } restore;
    Star* observer = make_star("Observer");
    Star* host = make_star("Distant planet host");
    Planet* planet = make_planet(host, "Distant planet", AU);
    planet->cenobj = host;
    mycenobj = observer;
    whereami = selected = trackidx = editidx = -1;
    view_mode = vm_spaceship;
    EXPECT_EQ(host->has_hz_planets, 1);
    planet->orbit->semimajor_axis = 4 * AU;
    EXPECT_FALSE(compute_object_location(planet));
    EXPECT_EQ(host->has_hz_planets, 0);
    planet->orbit->semimajor_axis = AU;
    EXPECT_FALSE(compute_object_location(planet));
    EXPECT_EQ(host->has_hz_planets, 1);
    EXPECT_EQ(host->has_planets, 1);
    const CelestialLocation original_location = planet->location;
    for (int frame = 0; frame < 100; ++frame) EXPECT_FALSE(compute_object_location(planet));
    EXPECT_EQ(planet->location.distance_to(original_location), 0);
    EXPECT_EQ(host->pl_indices.size(), 1u);
    planet->mass = planet_count_mass_cutoff / 2;
    EXPECT_FALSE(compute_object_location(planet));
    EXPECT_EQ(host->has_planets, 0);
    planet->mass = earth_mass;
    EXPECT_FALSE(compute_object_location(planet));
    EXPECT_EQ(host->has_planets, 1);
    planet->deleted = true;
    EXPECT_FALSE(compute_object_location(planet));
    EXPECT_EQ(host->has_planets, 0);
    delete_the_universe();
}

TEST_F(HousekeepingTest, SystemViewUpdatesUnselectedMoonsBeforeSkyVisibilityChecks)
{
    struct RestoreFrame
    {
        CelestialObject* center = mycenobj;
        CelestialLocation position = here;
        int place = whereami, selection = selected, tracking = trackidx, editing = editidx;
        ViewMode mode = view_mode;
        double magnification = zoom, mass_limit = celmasslim;
        ~RestoreFrame()
        {
            mycenobj = center;
            here = position;
            whereami = place;
            selected = selection;
            trackidx = tracking;
            editidx = editing;
            view_mode = mode;
            zoom = magnification;
            celmasslim = mass_limit;
        }
    } restore;
    Star* sun = make_star("Sol");
    sun->distance = 0;
    sun->cenobj = sun;
    sun->location = CelestialLocation();
    Planet* parent = make_planet(sun, "Tilted parent");
    parent->cenobj = sun;
    parent->orbit->inclination = 0.4;
    parent->obliquity = 0.3;
    parent->equinox = 0.7;
    parent->J2 = 0.01;
    parent->update_location(simnow);
    Moon* moon = new Moon();
    moon->mass = lunar_mass;
    moon->volumetric_mean_radius = 1737400;
    moon->absolute_magnitude = 99;
    moon->cenobj = sun;
    moon->user_added = true;
    moon->orbit = new Orbit();
    moon->orbit->center = parent;
    moon->orbit->semimajor_axis = 384400000;
    moon->orbit->inclination = 0.1;
    moon->orbit->compute_period(moon->mass);
    moon->obliquity = 0.05;
    moon->location.local_position = Point(0, 0, 20 * AU);
    append_cel(moon);
    mycenobj = sun;
    here = CelestialLocation();
    whereami = trackidx = editidx = -1;
    selected = parent->seqno;
    zoom = 1;
    celmasslim = 0;
    view_mode = vm_spaceship;
    EXPECT_FALSE(compute_object_location(moon));
    view_mode = vm_system;
    ASSERT_TRUE(compute_object_location(moon));
    EXPECT_LT(moon->location.distance_to(parent->location), 2 * moon->orbit->semimajor_axis);
    Point pole = rotate3D(yaxis, center, moon->location.equatorial_plane.v, -moon->location.equatorial_plane.a);
    pole.scale(1);
    EXPECT_GT((pole - Point(0, 1, 0)).magnitude(), 0.01);
    const Point position(moon->location);
    selected = moon->seqno;
    ASSERT_TRUE(compute_object_location(moon));
    Point selected_pole = rotate3D(yaxis, center, moon->location.equatorial_plane.v, -moon->location.equatorial_plane.a);
    selected_pole.scale(1);
    EXPECT_LT((pole - selected_pole).magnitude(), 1e-10);
    EXPECT_LT((Point(moon->location) - position).magnitude(), 1e-4);
    delete_the_universe();
}

TEST_F(HousekeepingTest, DeleteHandlesAbsentSelectionAndClearsDescendantNavigation)
{
    const int previous_selected = selected, previous_tracking = trackidx, previous_editing = editidx;
    const int previous_place = whereami;
    const auto previous_mode = view_mode;
    CelestialObject* previous_center = mycenobj;
    Star* sun = make_star("Sol");
    selected = trackidx = -1;
    process_key_delete();
    EXPECT_FALSE(sun->deleted);
    Planet* parent = make_planet(sun, "Authored parent");
    Planet* child = make_planet(sun, "Authored child");
    parent->user_added = child->user_added = true;
    sun->pl_indices = {parent->seqno, child->seqno};
    sun->has_planets = 2;
    child->orbit->center = parent;
    selected = -1;
    trackidx = parent->seqno;
    editidx = whereami = child->seqno;
    mycenobj = parent;
    view_mode = vm_horizon;
    process_key_delete();
    EXPECT_TRUE(parent->deleted);
    EXPECT_TRUE(child->deleted);
    EXPECT_EQ(selected, -1);
    EXPECT_EQ(trackidx, -1);
    EXPECT_EQ(editidx, -1);
    EXPECT_EQ(whereami, sun->seqno);
    EXPECT_EQ(view_mode, vm_spaceship);
    EXPECT_TRUE(sun->pl_indices.empty());
    EXPECT_EQ(sun->has_planets, 0);
    selected = previous_selected;
    trackidx = previous_tracking;
    editidx = previous_editing;
    whereami = previous_place;
    view_mode = previous_mode;
    mycenobj = previous_center;
    delete_the_universe();
}

TEST_F(HousekeepingTest, LocalCompanionBypassesTheBackgroundVisibilityCache)
{
    struct RestoreFrame
    {
        CelestialObject *center = mycenobj;
        CelestialLocation position = here;
        int place = whereami, selection = selected, tracking = trackidx, editing = editidx;
        ~RestoreFrame()
        {
            mycenobj = center;
            here = position;
            whereami = place;
            selected = selection;
            trackidx = tracking;
            editidx = editing;
        }
    } restore;
    make_star("Sol")->distance = 0;
    Star *primary = make_star("Gliese 86");
    primary->cenobj = primary;
    primary->location.system_center = Point(0, 0, 10 * parsec);
    Star *companion = make_star("Gliese 86 B");
    companion->make_companion_of(primary, 'B');
    companion->absolute_magnitude = 12;
    companion->apparent_magnitude = 99;
    companion->orbit->semimajor_axis = 20 * AU;
    companion->orbit->period = 90 * oneyear;
    Planet *world = make_planet(primary, "GJ 86 Ab", 0.1 * AU);
    whereami = world->seqno;
    mycenobj = primary;
    here = primary->location;
    here.local_position = Point(AU, 0, 0);
    selected = trackidx = editidx = -1;
    ASSERT_FALSE(companion->is_really_truly_in_visible_box(Point(here)));
    EXPECT_TRUE(compute_object_location(companion));
    EXPECT_TRUE(companion->tmp_vis_flag);
    EXPECT_LT(companion->viewer_magnitude(here), normal_best_mag_limit);
    Star *background = make_star("Distant unrelated WD");
    background->cenobj = background;
    background->apparent_magnitude = 99;
    background->location.system_center = Point(parsec, 0, 0);
    EXPECT_FALSE(compute_object_location(background));
    delete_the_universe();
}

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
    EXPECT_NE(report.find("Review System ( G2V 5)"), std::string::npos);
    EXPECT_NE(report.find("Review System C ( G2V 2)"), std::string::npos);
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

TEST_F(HousekeepingTest, ActiveWorldListTracksSelectionDeletionAndHostChanges)
{
    CandidateFrameRestore restore;
    Star* local = make_star("Local source");
    Star* remote = make_star("Remote source");
    local->cenobj = local;
    remote->cenobj = remote;
    Planet* first = make_planet(local);
    Planet* second = make_planet(remote);
    mycenobj = local;
    whereami = selected = trackidx = editidx = -1;
    zoom = 1;
    EXPECT_EQ(frame_active_world_candidates(), std::vector<int>({first->seqno}));
    selected = second->seqno;
    EXPECT_EQ(frame_active_world_candidates(), std::vector<int>({first->seqno, second->seqno}));
    first->deleted = true;
    invalidate_frame_objects();
    EXPECT_EQ(frame_active_world_candidates(), std::vector<int>({second->seqno}));
    selected = -1;
    second->cenobj = local;
    second->orbit->center = local;
    second->sync_host_statistics();
    EXPECT_EQ(frame_active_world_candidates(), std::vector<int>({second->seqno}));
}

TEST_F(HousekeepingTest, SurfaceObserverRefreshesWalkingRotationShapeAndBodyFrame)
{
    CandidateFrameRestore frame_restore;
    struct SurfaceRestore
    {
        double latitude = viewer_lat, longitude = viewer_lon, epoch = simnow;
        double north = npaz, correction = azimuth_correction;
        bool changed = viewchanged;
        CelestialLocation pole = npdummy.location;
        ~SurfaceRestore()
        {
            viewer_lat = latitude;
            viewer_lon = longitude;
            simnow = epoch;
            npaz = north;
            azimuth_correction = correction;
            viewchanged = changed;
            npdummy.location = pole;
        }
    } surface_restore;
    Star* host = make_star();
    Planet* world = make_planet(host);
    world->sidereal_rotational_period = oneday;
    world->location.local_position = Point(AU, 0, 0);
    whereami = world->seqno;
    view_mode = vm_horizon;
    viewer_lat = .2;
    viewer_lon = .3;
    simnow = J2000_TIME_T;
    set_viewer_surface_location(true);
    CelestialLocation first = here;
    double north = npaz;
    here.local_position = Point(0, 0, 0);
    npaz = 123;
    set_viewer_surface_location(true);
    EXPECT_TRUE(here.local_position == first.local_position);
    EXPECT_DOUBLE_EQ(npaz, north);
    viewer_lat += .01;
    set_viewer_surface_location(true);
    EXPECT_GT(here.distance_to(first), 1000);
    first = here;
    simnow += 100;
    set_viewer_surface_location(true);
    EXPECT_GT(here.distance_to(first), 1000);
    first = here;
    world->volumetric_mean_radius *= 1.1;
    set_viewer_surface_location(true);
    EXPECT_GT(here.distance_to(first), 1000);
    first = here;
    world->location.system_center.x += 1e10;
    world->location.equatorial_plane.a = .4;
    world->location.equatorial_plane.v = Point(1, 0, 0);
    set_viewer_surface_location(true);
    EXPECT_TRUE(here.system_center == world->location.system_center);
    EXPECT_GT(here.distance_to(first), 1e9);
    set_viewer_surface_location(false);
    EXPECT_DOUBLE_EQ(here.equatorial_plane.a, world->location.equatorial_plane.a);
    Moon* moon = new Moon();
    moon->cenobj = host;
    moon->sidereal_rotational_period = oneday;
    moon->width = 20000;
    moon->height = 10000;
    moon->depth = 30000;
    append_cel(moon);
    whereami = moon->seqno;
    set_viewer_surface_location(true);
    first = here;
    set_viewer_surface_location(true);
    EXPECT_TRUE(here.local_position == first.local_position);
    moon->height *= 2;
    set_viewer_surface_location(true);
    EXPECT_GT(here.distance_to(first), 100);
}

TEST_F(HousekeepingTest, GreenwichSunIsNearDailyAltitudeExtremes)
{
    CandidateFrameRestore frame_restore;
    struct ObserverRestore
    {
        std::function<void()> clear_universe;
        double latitude = viewer_lat, longitude = viewer_lon, eye_height = viewer_eye_height;
        double epoch = simnow, julian_date = JDnow, north = npaz, correction = azimuth_correction;
        bool changed = viewchanged, offline = radio_silence;
        CelestialLocation pole = npdummy.location;
        ~ObserverRestore()
        {
            clear_universe();
            viewer_lat = latitude;
            viewer_lon = longitude;
            viewer_eye_height = eye_height;
            simnow = epoch;
            JDnow = julian_date;
            npaz = north;
            azimuth_correction = correction;
            viewchanged = changed;
            radio_silence = offline;
            npdummy.location = pole;
        }
    } observer_restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    const int earth_index = find_object("Earth");
    ASSERT_GT(earth_index, 0);
    ASSERT_EQ(cels[earth_index]->typeclass(), class_planet);
    auto* earth = static_cast<Planet*>(cels[earth_index]);
    ASSERT_NE(earth->orbit, nullptr);
    ASSERT_EQ(earth->orbit->center, sun);

    whereami = earth_index;
    view_mode = vm_horizon;
    viewer_lat = 51.477 * fiftyseventh;
    viewer_lon = 0;
    viewer_eye_height = 1.75;
    const auto solar_altitude = [&](double jd)
    {
        JDnow = jd;
        simnow = J2000_TIME_T + (jd - J2000) * oneday;
        earth->update_location(simnow);
        set_viewer_location_and_plane();
        return sun->Decl_as_radians_refracted(here) * fiftyseven;
    };

    constexpr int samples_per_day = 144;
    constexpr double tolerance_degrees = 1.0;
    for (const double jd : {2451544.5, 2461242.0})
    {
        const double midnight = std::floor(jd - 0.5) + 0.5;
        const bool at_midnight = jd == midnight;
        SCOPED_TRACE(at_midnight ? "JD 2451544.5 (midnight)" : "JD 2461242.0 (noon)");
        const double target = solar_altitude(jd);
        double daily_min = target, daily_max = target;
        for (int sample = 0; sample < samples_per_day; ++sample)
        {
            const double angle = solar_altitude(midnight + double(sample) / samples_per_day);
            ASSERT_TRUE(std::isfinite(angle));
            daily_min = std::min(daily_min, angle);
            daily_max = std::max(daily_max, angle);
        }
        EXPECT_LT(daily_min, 0.0);
        EXPECT_GT(daily_max, 0.0);
        if (at_midnight) EXPECT_LT(target, 0.0);
        else EXPECT_GT(target, 0.0);
        EXPECT_NEAR(target, at_midnight ? daily_min : daily_max, tolerance_degrees);
    }
}

TEST_F(HousekeepingTest, MarsSunMatchesMissionSitesAndPrimeMeridian)
{
    CandidateFrameRestore frame_restore;
    struct ObserverRestore
    {
        std::function<void()> clear_universe;
        double latitude = viewer_lat, longitude = viewer_lon, eye_height = viewer_eye_height;
        double epoch = simnow, julian_date = JDnow, north = npaz, correction = azimuth_correction;
        bool changed = viewchanged, offline = radio_silence;
        CelestialLocation pole = npdummy.location;
        ~ObserverRestore()
        {
            clear_universe();
            viewer_lat = latitude;
            viewer_lon = longitude;
            viewer_eye_height = eye_height;
            simnow = epoch;
            JDnow = julian_date;
            npaz = north;
            azimuth_correction = correction;
            viewchanged = changed;
            radio_silence = offline;
            npdummy.location = pole;
        }
    } observer_restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    const int mars_index = find_object("Mars");
    ASSERT_GT(mars_index, 0);
    ASSERT_EQ(cels[mars_index]->typeclass(), class_planet);
    auto* mars = static_cast<Planet*>(cels[mars_index]);
    ASSERT_NE(mars->orbit, nullptr);
    ASSERT_EQ(mars->orbit->center, sun);

    // Freeze both snapshots in UTC so repeated runs exercise the same Mars clock readings.
    // The orbital period is from https://nssdc.gsfc.nasa.gov/planetary/factsheet/marsfact.html.
    constexpr double snapshot_jd = 2461367.0420833333;
    constexpr double sol_in_earth_days = 1.0274912517;
    constexpr double sidereal_year_in_earth_days = 686.980;
    const struct
    {
        const char* utc;
        double jd;
    } snapshots[] =
    {
        {"2026-11-22T13:00:36Z", snapshot_jd},
        {"2027-11-01T13:05:59.622Z", snapshot_jd + (sol_in_earth_days + sidereal_year_in_earth_days) / 2}
    };

    // Mission coordinates are from https://marsti.me/ and Airy-0 is at 5.07 degrees south
    // on the prime meridian (https://planetarynames.wr.usgs.gov/Feature/115).
    // The geometric altitudes and north-through-east azimuths use NASA Mars24 steps B through D
    // with TT minus UTC of 69.184 seconds (https://www.giss.nasa.gov/tools/mars24/help/algorithm.html).
    const struct
    {
        const char* name;
        double latitude, longitude_west;
        struct { double altitude, azimuth; } reference[2];
    } sites[] =
    {
        {"Zhurong",        25.048,  250.076,  {{-24.760,  63.833}, {-68.630, 359.497}}},
        {"Ingenuity",      18.4372, 282.5493, {{-52.733,  40.445}, {-54.820, 290.828}}},
        {"Perseverance",   18.4447, 282.5492, {{-52.728,  40.439}, {-54.817, 290.838}}},
        {"InSight",         4.5024, 224.3766, {{ -7.253,  78.605}, {-64.537,  89.102}}},
        {"Curiosity",      -4.5985, 222.5583, {{ -7.171,  79.780}, {-61.464, 106.419}}},
        {"MTC (Airy-0)",   -5.07,     0,     {{-36.389, 279.551}, { 20.329, 267.947}}}
    };

    whereami = mars_index;
    view_mode = vm_horizon;
    viewer_eye_height = 1.75;
    constexpr double tolerance_degrees = 1.0;
    for (size_t index = 0; index < std::size(snapshots); ++index)
    {
        const auto& snapshot = snapshots[index];
        JDnow = snapshot.jd;
        simnow = J2000_TIME_T + (JDnow - J2000) * oneday;
        mars->update_location(simnow);
        for (const auto& site : sites)
        {
            viewer_lat = site.latitude * fiftyseventh;
            viewer_lon = std::remainder(-site.longitude_west, 360.0) * fiftyseventh;
            set_viewer_location_and_plane();
            const double altitude_degrees = sun->Decl_as_radians(here) * fiftyseven;
            const double north_offset = npdummy.RA_as_radians(here, 0) - sun->RA_as_radians(here, 0);
            const double azimuth_degrees = std::fmod(north_offset * fiftyseven + 360.0, 360.0);
            const auto& reference = site.reference[index];
            SCOPED_TRACE(::testing::Message() << site.name << " at " << snapshot.utc
                << ", altitude " << altitude_degrees << " versus " << reference.altitude
                << ", azimuth " << azimuth_degrees << " versus " << reference.azimuth);
            EXPECT_NEAR(altitude_degrees, reference.altitude, tolerance_degrees);
            EXPECT_NEAR(std::remainder(azimuth_degrees - reference.azimuth, 360.0), 0.0, tolerance_degrees);
        }
    }
}

TEST_F(HousekeepingTest, LunarPrimeMeridianFacesEarthThroughTwoLunations)
{
    CandidateFrameRestore frame_restore;
    struct ObserverRestore
    {
        std::function<void()> clear_universe;
        double latitude = viewer_lat, longitude = viewer_lon, eye_height = viewer_eye_height;
        double epoch = simnow, julian_date = JDnow, north = npaz, correction = azimuth_correction;
        bool changed = viewchanged, offline = radio_silence;
        CelestialLocation pole = npdummy.location;
        ~ObserverRestore()
        {
            clear_universe();
            viewer_lat = latitude;
            viewer_lon = longitude;
            viewer_eye_height = eye_height;
            simnow = epoch;
            JDnow = julian_date;
            npaz = north;
            azimuth_correction = correction;
            viewchanged = changed;
            radio_silence = offline;
            npdummy.location = pole;
        }
    } observer_restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    const int earth_index = find_object("Earth");
    ASSERT_GT(earth_index, 0);
    ASSERT_EQ(cels[earth_index]->typeclass(), class_planet);
    auto* earth = static_cast<Planet*>(cels[earth_index]);
    ASSERT_NE(earth->orbit, nullptr);
    ASSERT_EQ(earth->orbit->center, sun);
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, earth), 0);
    const int moon_index = find_object("Moon");
    ASSERT_GT(moon_index, 0);
    ASSERT_EQ(cels[moon_index]->typeclass(), class_moon);
    auto* moon = static_cast<Moon*>(cels[moon_index]);
    ASSERT_NE(moon->orbit, nullptr);
    ASSERT_EQ(moon->orbit->center, earth);
    ASSERT_EQ(moon->get_light_center(), sun);

    // The final samples use the next New Moons from NASA's phase tables.
    // https://eclipse.gsfc.nasa.gov/phase/phases1901.html
    // https://eclipse.gsfc.nasa.gov/phase/phase2001gmt.html
    const struct
    {
        const char* label;
        double start_jd, end_jd;
    } lunations[] =
    {
        {"lunation_1979", 2443931.20493, 2443960.624305556},
        {"lunation_2017", 2457987.26852, 2458016.729166667}
    };

    // Rounded geocentric libration limits from JPL D-32296, figure 2-8.
    // https://ssd.jpl.nasa.gov/doc/lunar_cmd_2005_jpl_d32296.pdf
    constexpr double longitude_limit_degrees = 8.2;
    constexpr double latitude_limit_degrees = 7.0;
    constexpr double mean_offset_limit_degrees = 2.0;
    constexpr double new_moon_illuminated_limit = 0.001;
    constexpr int intervals = 10;
    whereami = moon_index;
    view_mode = vm_horizon;
    viewer_lat = viewer_lon = 0;
    viewer_eye_height = 1.75;

    for (const auto& lunation : lunations)
    {
        SCOPED_TRACE(lunation.label);
        Point mean_direction;
        double max_longitude = 0, max_latitude = 0, starting_illumination = 0;
        for (int sample = 0; sample <= intervals; ++sample)
        {
            JDnow = lunation.start_jd + (lunation.end_jd - lunation.start_jd) * sample / intervals;
            simnow = J2000_TIME_T + (JDnow - J2000) * oneday;
            SCOPED_TRACE(std::string("JD ") + std::to_string(JDnow));
            earth->update_location(simnow);
            moon->update_location(simnow);
            set_viewer_location_and_plane();

            // At latitude and longitude zero, the horizon axes are east, up, and north.
            // Use the center-to-center ray to measure geocentric libration.
            const Point moon_to_earth = earth->location.local_position - moon->location.local_position;
            const Point direction = rotate3D(to_viewer_plane(moon_to_earth), center, yaxis, -azimuth_correction);
            const double distance = direction.magnitude();
            ASSERT_TRUE(std::isfinite(distance) && distance > 0);
            const double longitude = std::atan2(direction.x, direction.y) * fiftyseven;
            const double latitude = std::atan2(direction.z, std::hypot(direction.x, direction.y)) * fiftyseven;
            max_longitude = std::max(max_longitude, std::abs(longitude));
            max_latitude = std::max(max_latitude, std::abs(latitude));
            EXPECT_LE(std::abs(longitude), longitude_limit_degrees) << "Sub-Earth longitude " << longitude;
            EXPECT_LE(std::abs(latitude), latitude_limit_degrees) << "Sub-Earth latitude " << latitude;

            // Half-weight the endpoints when averaging the ten equal intervals.
            const double weight = (sample == 0 || sample == intervals) ? 0.5 : 1.0;
            mean_direction += direction * (weight / distance);
            if (sample == 0)
            {
                moon->viewer_reflectance_magnitude(earth->location);
                starting_illumination = moon->amt_lit;
                EXPECT_GE(starting_illumination, 0.0);
                EXPECT_LE(starting_illumination, new_moon_illuminated_limit);
            }
        }
        const double mean_offset = std::atan2(std::hypot(mean_direction.x, mean_direction.z), mean_direction.y) * fiftyseven;
        EXPECT_LE(mean_offset, mean_offset_limit_degrees);
        const std::string label = lunation.label;
        RecordProperty(label + "_mean_offset_degrees", std::to_string(mean_offset));
        RecordProperty(label + "_max_longitude_degrees", std::to_string(max_longitude));
        RecordProperty(label + "_max_latitude_degrees", std::to_string(max_latitude));
        RecordProperty(label + "_starting_illuminated_percent", std::to_string(starting_illumination * 100));
    }
}

TEST_F(HousekeepingTest, LunarPositionMatchesHorizonsBetweenEphemerisSamples)
{
    struct OrbitRestore
    {
        std::function<void()> clear_universe;
        double epoch = simnow, julian_date = JDnow;
        bool offline = radio_silence;
        ~OrbitRestore()
        {
            clear_universe();
            simnow = epoch;
            JDnow = julian_date;
            radio_silence = offline;
        }
    } restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    auto& system_plane = sun->location.local_system_plane;
    system_plane.v = Point(0, 0, 1);
    system_plane.a = 0.4;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    const int earth_index = find_object("Earth");
    ASSERT_GT(earth_index, 0);
    ASSERT_EQ(cels[earth_index]->typeclass(), class_planet);
    auto* earth = static_cast<Planet*>(cels[earth_index]);
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, earth), 0);
    const int moon_index = find_object("Moon");
    ASSERT_GT(moon_index, 0);
    ASSERT_EQ(cels[moon_index]->typeclass(), class_moon);
    auto* moon = static_cast<Moon*>(cels[moon_index]);
    ASSERT_NE(moon->orbit, nullptr);
    ASSERT_EQ(moon->orbit->center, earth);
    ASSERT_GE(moon->orbit->num_osc, 2u);

    const auto position_at = [&](double jd)
    {
        JDnow = jd;
        simnow = J2000_TIME_T + (jd - J2000) * oneday;
        earth->update_location(simnow);
        moon->update_location(simnow);
        const auto relative = moon->location.local_position - earth->location.local_position;
        const auto ecliptic = rotate3D(relative, center, system_plane.v, system_plane.a);
        return Point(ecliptic.z, -ecliptic.x, ecliptic.y) * 0.001;
    };

    // JPL Horizons geometric vectors, Earth center (399) to Moon (301), J2000 ecliptic, km.
    // The exact queries are recorded in docs/astronomy-audit/orbit-interpolation.tsv.
    const struct
    {
        double jd;
        Point position;
    } references[] =
    {
        {2422325.0, Point(279652.6391174170, 237369.7315477148, 8455.671862865434)},
        {2451543.0, Point(-372106.2984946174, -127356.7268489019, 34820.25108696324)},
        {2480754.0, Point(-142391.6368020591, -377986.0246835253, -27459.03515759160)}
    };
    for (const auto& reference : references)
    {
        SCOPED_TRACE(reference.jd);
        const Point actual = position_at(reference.jd);
        const Point expected = reference.position;
        const double dot = actual.x * expected.x + actual.y * expected.y + actual.z * expected.z;
        const double cosine = dot / (actual.magnitude() * expected.magnitude());
        EXPECT_LT(std::acos(std::clamp(cosine, -1.0, 1.0)) * fiftyseven * 3600, 20);
        EXPECT_LT(actual.distance_to(expected), 50);
    }

    // Convert the table's true anomaly and orbital radius at the first, interior, and last knots.
    EXPECT_LT(position_at(2415020.5).distance_to(Point(24464.931732, -367508.959549, 7042.562946)), 0.001);
    const double knot = 2480750.5;
    const Point at_knot = position_at(knot);
    EXPECT_LT(at_knot.distance_to(Point(-352065.113703, -179415.296293, -4942.086265)), 0.001);
    EXPECT_LT(position_at(2488065.5).distance_to(Point(-71219.954147, 373521.754775, 30904.242976)), 0.001);

    const double seconds = 5;
    const double step = seconds / oneday;
    const Point left_velocity = (at_knot - position_at(knot - step)) * (1 / seconds);
    const Point right_velocity = (position_at(knot + step) - at_knot) * (1 / seconds);
    EXPECT_LT(left_velocity.distance_to(right_velocity), 0.0001);
}

TEST_F(HousekeepingTest, JupiterGreatRedSpotFacesEarthAtTransits)
{
    CandidateFrameRestore frame_restore;
    struct TransitRestore
    {
        std::function<void()> clear_universe;
        double epoch = simnow, julian_date = JDnow, correction = azimuth_correction;
        bool offline = radio_silence;
        ~TransitRestore()
        {
            clear_universe();
            simnow = epoch;
            JDnow = julian_date;
            azimuth_correction = correction;
            radio_silence = offline;
        }
    } transit_restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    const int earth_index = find_object("Earth"), jupiter_index = find_object("Jupiter");
    ASSERT_GT(earth_index, 0);
    ASSERT_GT(jupiter_index, 0);
    ASSERT_EQ(cels[earth_index]->typeclass(), class_planet);
    ASSERT_EQ(cels[jupiter_index]->typeclass(), class_planet);
    auto* earth = static_cast<Planet*>(cels[earth_index]);
    auto* jupiter = static_cast<Planet*>(cels[jupiter_index]);
    ASSERT_NE(earth->orbit, nullptr);
    ASSERT_NE(jupiter->orbit, nullptr);
    ASSERT_EQ(earth->orbit->center, sun);
    ASSERT_EQ(jupiter->orbit->center, sun);

    // The 2026 transit was first in the default list at 13:21:05 UTC that day.
    // https://tools.davidastro.com/en/tools/jupiter-grs-transit-calculator
    // The 1979 central-meridian separation and following transit are from the IMCCE JGRS ephemeris.
    // https://ssp.imcce.fr/webservices/vo-tools/jgrs/
    const struct
    {
        const char* label;
        const char* utc;
        double jd, offset;
    } transits[] =
    {
        {"spot_1979",    "1979-07-09T02:30:00Z",     2444063.6041666665, 117.21},
        {"transit_1979", "1979-07-09T09:11:50.153Z", 2444063.8832193636,   0.0},
        {"transit_2026", "2026-11-22T14:41:11.485Z", 2461367.111938484,    0.0}
    };

    constexpr double spot_longitude = 126.0;
    constexpr double longitude_tolerance = 30.0; // Approximately the calculator's 50-minute viewing window on either side.
    azimuth_correction = 0;
    for (const auto& transit : transits)
    {
        SCOPED_TRACE(transit.utc);
        JDnow = transit.jd;
        simnow = J2000_TIME_T + (JDnow - J2000) * oneday;
        earth->update_location(simnow);
        jupiter->update_location(simnow);

        // The equatorial longitude minus the spin phase is Jupiter's texture longitude.
        const double earth_longitude = earth->RA_as_radians(jupiter->location, jupiter->timeofday()) * fiftyseven;
        const double offset = std::remainder(spot_longitude - earth_longitude, 360.0);
        EXPECT_NEAR(std::remainder(offset - transit.offset, 360.0), 0.0, longitude_tolerance)
            << "Earth-facing texture longitude " << earth_longitude << ", spot offset " << offset;
        const std::string label = transit.label;
        RecordProperty(label + "_earth_longitude_degrees", std::to_string(earth_longitude));
        RecordProperty(label + "_spot_offset_degrees", std::to_string(offset));
    }
}

TEST_F(HousekeepingTest, SolarSystemPrimeMeridiansMatchTheMidnightReference)
{
    CandidateFrameRestore frame_restore;
    struct RotationRestore
    {
        std::function<void()> clear_universe;
        double epoch = simnow, julian_date = JDnow;
        bool offline = radio_silence;
        ~RotationRestore()
        {
            clear_universe();
            simnow = epoch;
            JDnow = julian_date;
            radio_silence = offline;
        }
    } rotation_restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    JDnow = 2451544.5;
    simnow = J2000_TIME_T + (JDnow - J2000) * oneday;
    EXPECT_DOUBLE_EQ(J2000, JDnow);
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    make_planet(sun, "Ceres")->asteroid_no = 1;
    make_planet(sun, "Pluto")->asteroid_no = 134340;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, nullptr, sun), 0);

    // ICRF directions of the equatorial point at longitude zero, in degrees at midnight UTC.
    // W is measured eastward from the ICRF equator's ascending node on the body's equator.
    // Reference: https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc
    // Greenwich uses IERS Earth Rotation Angle with UT1 approximated by UTC.
    // Hyperion has a bump map and a chaotic spin with no published orientation model for this date.
    const struct
    {
        const char* body;
        double ra, dec;
    } reference[] =
    {
        {"Sun",         91.812684,  25.422142}, // W = 77.094337
        {"Mercury",    340.878418, -15.297323}, // W = 326.535701
        {"Venus",      165.097635,   7.282357}, // W = 160.939584
        {"Earth",       99.967821,  -0.000001}, // W = 9.967812
        {"Mars",        48.834633,   0.872896}, // W = 1.446735
        {"Jupiter",    205.891994, -12.557789}, // System III W = 210.328695
        {"Saturn",     124.731565,  -0.662354}, // W = 354.105364
        {"Uranus",     242.291076,  74.314223}, // W = 94.017749
        {"Neptune",     15.280573, -14.619687}, // W = 339.828122
        {"Pluto",      276.410975, -82.341279}, // W = 274.555609
        {"Ceres",       74.398155,  18.921727}, // W = 55.280726
        {"Moon",        29.021064,  13.549092}, // W = 34.616780
        {"Phobos",     237.436163,  -6.801766}, // W = 191.609070
        {"Deimos",     349.586033, -33.492194}, // W = 295.079410
        {"Io",          97.776871,  25.152582}, // W = 98.880841
        {"Europa",     344.803642,  -6.104832}, // W = 345.446960
        {"Ganymede",    15.455000,   7.938146}, // W = 18.665545
        {"Callisto",   245.417716, -23.562467}, // W = 249.192201
        {"Mimas",      304.339726,   0.589161}, // W = 173.157307
        {"Enceladus",    5.636829,  -5.314054}, // W = 235.149226
        {"Tethys",      42.891917,  -6.908125}, // W = 280.511630
        {"Dione",       62.717766,  -6.009307}, // W = 291.930248
        {"Rhea",       325.641635,  -1.885303}, // W = 196.160226
        {"Titan",      304.827142,   0.535764}, // W = 175.313783
        {"Iapetus",     41.331941,  -1.820822}, // W = 352.934393
        {"Phoebe",     159.991833,  11.687441}, // W = 73.452587
        {"Janus",      290.791698,   2.176795}, // W = 160.827484
        {"Epimetheus", 165.402090,   3.872174}, // W = 34.612108
        {"Charon",      96.410975,  82.341279}  // W = 94.555609
    };
    for (const auto& entry : reference)
    {
        SCOPED_TRACE(entry.body);
        const int index = find_object(entry.body);
        ASSERT_GE(index, 0);
        auto* body = cels[index];
        ASSERT_TRUE(body->has_rotation_model());
        if (body->typeclass() == class_star) static_cast<Star*>(body)->update_location(simnow);
        else if (body->typeclass() == class_moon) static_cast<Moon*>(body)->update_location(simnow);
        else static_cast<Planet*>(body)->update_location(simnow);
        const Point icrf = Point::from_ra_dec(entry.ra * fiftyseventh, entry.dec * fiftyseventh, 1);
        const Point ecliptic = rotate3D(icrf, center, ICRF_to_ecliptic.v, ICRF_to_ecliptic.a);
        const auto& plane = body->location.local_system_plane;
        const Point expected = rotate3D(ecliptic, center, plane.v, -plane.a);
        EXPECT_LE(find_3D_angle(body->prime_meridian_direction(), expected, center) * fiftyseven, 0.001);
        if (strcmp(entry.body, "Jupiter"))
        {
            const Point equatorial(Point::from_ra_dec(body->timeofday(), 0, 1));
            const auto& equator = body->location.equatorial_plane;
            const Point surface = rotate3D(equatorial, center, equator.v, -equator.a);
            EXPECT_LE(find_3D_angle(surface, expected, center) * fiftyseven, 0.001);
        }
    }
}

TEST_F(HousekeepingTest, JupiterGreatRedSpotMatchesIMCCETransitWithLightTime)
{
    CandidateFrameRestore frame_restore;
    struct TransitRestore
    {
        std::function<void()> clear_universe;
        double epoch = simnow, julian_date = JDnow, correction = azimuth_correction;
        bool offline = radio_silence;
        ~TransitRestore()
        {
            clear_universe();
            simnow = epoch;
            JDnow = julian_date;
            azimuth_correction = correction;
            radio_silence = offline;
        }
    } transit_restore{[this]() { delete_the_universe(); }};

    radio_silence = true;
    azimuth_correction = 0;
    Star* sun = make_star("Sun");
    sun->distance = 0;
    sun->cenobj = sun;
    CatalogReader reader;
    ASSERT_GT(reader.read_local_planets(cels, MAX_CELOBJS, sun), 0);
    const int earth_index = find_object("Earth"), jupiter_index = find_object("Jupiter");
    ASSERT_GT(earth_index, 0);
    ASSERT_GT(jupiter_index, 0);
    ASSERT_EQ(cels[earth_index]->typeclass(), class_planet);
    ASSERT_EQ(cels[jupiter_index]->typeclass(), class_planet);
    auto* earth = static_cast<Planet*>(cels[earth_index]);
    auto* jupiter = static_cast<Planet*>(cels[jupiter_index]);

    // IMCCE JGRS geocentric transit on 2026-11-22 at 14:48:19.461 UTC.
    // https://ssp.imcce.fr/webservices/vo-tools/jgrs/transit/2026-11-22
    JDnow = 2461367.116891910;
    simnow = J2000_TIME_T + (JDnow - J2000) * oneday;
    earth->update_location(simnow);
    jupiter->update_location(simnow);
    const double light_time = earth->location.distance_to(jupiter->location) / speed_of_light;
    jupiter->set_texture_light_time(light_time);
    jupiter->update_location(simnow - light_time);
    const double longitude = earth->RA_as_radians(jupiter->location, jupiter->timeofday()) * fiftyseven;
    EXPECT_NEAR(std::remainder(longitude - 126.0, 360.0), 0.0, 3.0);
}

TEST_F(HousekeepingTest, RelativeVectorValidityFollowsTheFinalLocation)
{
    CandidateFrameRestore restore;
    Star* host = make_star("Relative source");
    host->cenobj = host;
    Planet* world = make_planet(host);
    mycenobj = host;
    here = CelestialLocation();
    here.local_position = Point(2 * AU, AU, 0);
    whereami = selected = trackidx = editidx = -1;
    view_mode = vm_system;
    bool valid = false;
    ASSERT_TRUE(compute_object_location(host, &valid));
    EXPECT_EQ(valid, performance_enabled(13));
    CelestialLocation relative = host->location - here;
    EXPECT_TRUE(host->tmprel == Point(relative));
    valid = false;
    here.local_position.y += AU;
    ASSERT_TRUE(compute_object_location(host, &valid));
    EXPECT_EQ(valid, performance_enabled(13));
    relative = host->location - here;
    EXPECT_TRUE(host->tmprel == Point(relative));
    valid = false;
    ASSERT_TRUE(compute_object_location(world, &valid));
    EXPECT_FALSE(valid);
}

TEST_F(HousekeepingTest, CatalogTracePreservesRecursiveReleaseAndReacquire)
{
    std::recursive_mutex mutex;
    const auto before = catalog_lock_sample(CatalogLockSite::frame);
    {
        CatalogLock outer(mutex, CatalogLockSite::frame);
        EXPECT_TRUE(outer.owns_lock());
        {
            CatalogLock inner(mutex, CatalogLockSite::frame);
            EXPECT_TRUE(inner.owns_lock());
        }
        outer.unlock();
        EXPECT_FALSE(outer.owns_lock());
        outer.lock();
        EXPECT_TRUE(outer.owns_lock());
    }
    {
        CatalogLock owner(mutex, CatalogLockSite::publication);
        std::atomic<bool> started{false}, acquired{false};
        std::thread worker([&]()
        {
            started = true;
            CatalogLock publisher(mutex, CatalogLockSite::publication);
            acquired = true;
        });
        while (!started.load()) std::this_thread::yield();
        EXPECT_FALSE(acquired.load());
        owner.unlock();
        worker.join();
        EXPECT_TRUE(acquired.load());
    }
    const auto after = catalog_lock_sample(CatalogLockSite::frame);
    EXPECT_EQ(after.acquisitions - before.acquisitions, performance_enabled(18) ? 3u : 0u);
    if (performance_enabled(18))
    {
        EXPECT_GT(after.hold_ns, before.hold_ns);
        EXPECT_GT(after.maximum_hold_ns, 0u);
    }
}

TEST_F(HousekeepingTest, MissingRotationDiagnosticKeepsTheFirstWarningAndRearmsAfterRecovery)
{
    CandidateFrameRestore frame_restore;
    struct ObserverRestore
    {
        double latitude = viewer_lat, longitude = viewer_lon, north = npaz, correction = azimuth_correction;
        bool changed = viewchanged;
        CelestialLocation pole = npdummy.location;
        ~ObserverRestore()
        {
            viewer_lat = latitude;
            viewer_lon = longitude;
            npaz = north;
            azimuth_correction = correction;
            viewchanged = changed;
            npdummy.location = pole;
        }
    } observer_restore;
    std::ostringstream captured;
    struct StreamRestore
    {
        std::streambuf* previous;
        ~StreamRestore() { std::cerr.rdbuf(previous); }
    } stream_restore{std::cerr.rdbuf(captured.rdbuf())};
    Star* host = make_star("Diagnostic host");
    Planet* first = make_planet(host, "Diagnostic first world");
    Planet* second = make_planet(host, "Diagnostic second world");
    first->sidereal_rotational_period = second->sidereal_rotational_period = 0;
    whereami = first->seqno;
    view_mode = vm_horizon;
    viewer_lat = .2;
    viewer_lon = .3;
    set_viewer_surface_location(true);
    const CelestialLocation position = here;
    set_viewer_surface_location(true);
    EXPECT_TRUE(here.local_position == position.local_position);
    first->sidereal_rotational_period = oneday;
    set_viewer_surface_location(true);
    first->sidereal_rotational_period = 0;
    set_viewer_surface_location(true);
    set_viewer_surface_location(true);
    EXPECT_TRUE(here.local_position == position.local_position);
    whereami = second->seqno;
    set_viewer_surface_location(true);
    set_viewer_surface_location(true);
    whereami = first->seqno;
    set_viewer_surface_location(true);
    const std::string output = captured.str(), message = "has no rotation period";
    size_t count = 0, offset = 0;
    while ((offset = output.find(message, offset)) != std::string::npos) { ++count; offset += message.size(); }
    EXPECT_EQ(count, performance_enabled(78) ? 3u : 7u);
    EXPECT_NE(output.find(first->name), std::string::npos);
    EXPECT_NE(output.find(second->name), std::string::npos);
    EXPECT_FALSE(first->to_json().contains("missing_rotation_reported"));
}
