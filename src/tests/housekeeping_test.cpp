#include "../performance_options.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <gtest/gtest.h>
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
