#include "performance_options.h"

#include "housekeeping.h"
#include "inputs.h"
#include "visuals.h"        // eclipse_obscuration(): an eclipse dims the daylight computed here
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <array>
#include <atomic>
#include <iterator>

using namespace alienorum;

void refresh_star_visibilities()
{
    int i;
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->deleted) continue;
        if (cels[i]->typeclass() == class_star) ((Star*)cels[i])->is_really_truly_in_visible_box(here);
    }
}

Point viewer_displacement(const Point& motion, double elapsed_seconds)
{
    const double speed = motion.magnitude()*target_frame_rate;
    Point displacement = motion;
    if (speed < speed_of_light) displacement.scale(displacement.magnitude()/compute_time_dilation(speed));
    return displacement*(elapsed_seconds*target_frame_rate);
}

void set_viewer_surface_location(bool also_set_plane)
{
    CelestialObject *cel = cels[whereami];
    if (cel->typeclass() == class_star)
    {
        view_mode = vm_spaceship;
        here = cels[whereami]->location;
        azimuth_correction = 0;
        npaz = 0;
        return;
    }
    here = cel->location;
    // A zero rotation period comes from catalog data, not from a mistake in this file, so it is
    // not something an assert can be responsible for: NDEBUG removes it and the division inside
    // timeofday() runs anyway. A body that does not rotate simply keeps longitude zero facing
    // where it started, which is the sensible reading of "no rotation" and what the rest of this
    // function then works from.
    if (!cel->sidereal_rotational_period)
        std::cerr << "Warning: " << cel->name << " has no rotation period; treating it as fixed." << std::endl;

    bool dwh = false;
    if (cel->typeclass() == class_moon)
        dwh = (((Moon*)cel)->depth > zero_isnt_really_zero
            && ((Moon*)cel)->width > zero_isnt_really_zero
            && ((Moon*)cel)->height > zero_isnt_really_zero);

    double obl = 1.0 - cel->oblateness, equatorial_radius;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * .5;
    else
        equatorial_radius = cel->get_equatorial_radius();

    const double rotation_phase = cel->timeofday();
    struct SurfaceObserverContext
    {
        const CelestialObject* body = nullptr;
        std::array<double, 30> key = {};
        CelestialLocation observer, pole;
        double north = 0, correction = 0;
    };
    static SurfaceObserverContext context;
    std::array<double, 30> key = {};
    if (performance_enabled(12))
    {
        const auto& location = cel->location;
        size_t index = 0;
        for (const Point& point : {location.galactic_center, location.system_center, location.local_position,
            location.local_system_plane.v, location.orbital_plane.v, location.equatorial_plane.v})
        {
            key[index++] = point.x;
            key[index++] = point.y;
            key[index++] = point.z;
        }
        key[index++] = location.local_system_plane.a;
        key[index++] = location.orbital_plane.a;
        key[index++] = location.equatorial_plane.a;
        key[index++] = viewer_lat;
        key[index++] = viewer_lon;
        key[index++] = rotation_phase;
        key[index++] = obl;
        key[index++] = equatorial_radius;
        key[index++] = also_set_plane;
        if (dwh)
        {
            key[index++] = static_cast<Moon*>(cel)->width;
            key[index++] = static_cast<Moon*>(cel)->height;
            key[index++] = static_cast<Moon*>(cel)->depth;
        }
        if (context.body == cel && context.key == key)
        {
            here = context.observer;
            if (also_set_plane)
            {
                npdummy.location = context.pole;
                npaz = context.north;
                azimuth_correction = context.correction;
            }
            viewchanged = true;
            return;
        }
    }

    Point cursor = Point::from_ra_dec(viewer_lon, viewer_lat, dwh ? 1 : equatorial_radius, 0);

    if (dwh)
    {
        cursor.x *= ((Moon*)cel)->width * .5;
        cursor.y *= ((Moon*)cel)->height * .5;
        cursor.z *= ((Moon*)cel)->depth * .5;
    }
    else cursor.y *= obl;
    cursor = rotate3D(cursor, center, yaxis, -rotation_phase);
    cursor = rotate3D(cursor, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);

    here.local_position = cel->location.local_position + cursor;
    if (also_set_plane)
    {
        here.equatorial_plane = align_points_3d(cursor, yaxis, center);

        Point north_pole = rotate3D(yaxis, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
        npdummy.location = cel->location;
        npdummy.location.local_position = north_pole;
        azimuth_correction = 0;
        npaz = fmod(npdummy.RA_as_radians(here, 0), _pi*2);
        azimuth_correction = -npaz;
    }

    if (performance_enabled(12))
    {
        context.body = cel;
        context.key = key;
        context.observer = here;
        if (also_set_plane)
        {
            context.pole = npdummy.location;
            context.north = npaz;
            context.correction = azimuth_correction;
        }
    }
    else context.body = nullptr;
    viewchanged = true;
}

void set_viewer_location_and_plane()
{
    if (whereami < 0)
    {
        view_mode = vm_spaceship;
        save_viewer_latlon = true;
        return;
    }

    if (whereami >= 0 && cels[whereami]->typeclass() == class_satellite)
    {
        view_mode = vm_spaceship;

        if (vplane_mode == vplane_local && cels[whereami]->orbit->center)
        {
            here = cels[whereami]->location;
            Point me = cels[whereami]->location;
            Point zenith = satview_upsidedown ? (Point(cels[whereami]->orbit->center->location) - me) : (me - cels[whereami]->orbit->center->location);
            here.equatorial_plane = align_points_3d(zenith, yaxis, center);
            viewchanged = true;
            return;
        }
    }

    if (view_mode == vm_system)
    {
        if (mycenobj) here = mycenobj->location;
    }
    else if (vplane_mode == vplane_local && (view_mode == vm_spaceship || view_mode == vm_skymap))
    {
        // Issue #98 debug code - preserve and come back to it when more time and less sleep debt:
        // if (cels[whereami]->orbit) std::cout << cels[whereami]->orbit->center << "%" << cels[whereami]->orbit->period << std::endl;

        // Issue #98 quick fix
        if (!cels[whereami]->location.orbital_plane.a && !cels[whereami]->location.orbital_plane.v.magnitude()
            && cels[whereami]->orbit && cels[whereami]->orbit->center && !cels[whereami]->orbit->period)
        {
            cels[whereami]->location.equatorial_plane = cels[whereami]->location.orbital_plane = cels[whereami]->location.local_system_plane
                = cels[whereami]->orbit->center->location.local_system_plane;
            cels[whereami]->location.local_position = (Point)cels[whereami]->location - (Point)cels[whereami]->orbit->center->location;
            cels[whereami]->location.system_center = cels[whereami]->orbit->center->location.system_center;
            cels[whereami]->location.galactic_center = cels[whereami]->orbit->center->location.galactic_center;
        }

        here = cels[whereami]->location;

        // Issue #98 debug code - preserve and come back to it when more time and less sleep debt:
        #if 0
        std::cout << "Viewer location: " << here.system_center << ":" << here.local_position
            << "\n\tsystem plane=" << here.local_system_plane
            << "\n\torbital plane=" << here.orbital_plane
            << "\n\tequatorial plane=" << here.equatorial_plane
            << std::endl << std::endl;
        #endif

        azimuth_correction = 0;
        npaz = 0;
    }
    else if (view_mode == vm_horizon)
    {
        set_viewer_surface_location(true);
    }
    else if (view_mode == vm_sunclock)
    {
        here = cels[whereami]->location;
        azimuth_correction = 0;
        npaz = 0;
    }
    else if (vplane_mode == vplane_ICRF)
    {
        here = cels[whereami]->location;
        here.equatorial_plane.a = 0;
    }
    else if (vplane_mode == vplane_ecliptic)
    {
        here = cels[whereami]->location;
        here.equatorial_plane = here.orbital_plane;
    }
    else if (vplane_mode == vplane_galactic && inside_galaxy_idx >= 0)
    {
        here = cels[whereami]->location;
        here.equatorial_plane = cels[inside_galaxy_idx]->location.equatorial_plane;
    }
}

double dispw, disph, celmasslim;
namespace
{
    struct StellarUpdateStamp
    {
        Star* star = nullptr;
        CelestialObject* center = nullptr;
        double epoch = 0;
        uint64_t generation = 0;
    };
    struct StellarUpdateFrame;
    thread_local std::vector<StellarUpdateStamp> stellar_update_stamps;
    thread_local uint64_t stellar_update_generation = 0;
    thread_local StellarUpdateFrame* stellar_update_frame = nullptr;

    struct StellarUpdateFrame
    {
        uint64_t generation;
        StellarUpdateFrame* previous;

        StellarUpdateFrame() : generation(++stellar_update_generation), previous(stellar_update_frame)
        {
            if (!generation)
            {
                for (auto& stamp : stellar_update_stamps) stamp.generation = 0;
                generation = ++stellar_update_generation;
            }
            stellar_update_stamps.resize(ncelobjs);
            stellar_update_frame = this;
        }

        ~StellarUpdateFrame()
        {
            stellar_update_frame = previous;
        }
    };
}

static bool update_stellar_lineage(Star *member, double epoch, double orbital_epoch)
{
    Star *lineage[26];
    int count = 0;
    while (member)
    {
        if (count == 26) return false;
        for (int ancestor = 0; ancestor < count; ++ancestor)
        {
            if (lineage[ancestor] == member) return false;
        }
        lineage[count++] = member;
        if (!member->orbit || !member->orbit->center || member->orbit->center->typeclass() != class_star) break;
        member = static_cast<Star*>(member->orbit->center);
    }
    while (count)
    {
        Star *ancestor = lineage[--count];
        // Catalog astrometry already represents received light at its reference epoch.
        const double update_epoch = ancestor->orbit ? orbital_epoch : epoch;
        StellarUpdateStamp* stamp = nullptr;
        // Independent astrometry is reusable; orbital members retain parent-state updates.
        // Variable brightness also depends on viewer-relative state, so always update it.
        if (stellar_update_frame && !ancestor->orbit && !ancestor->variability_period && !ancestor->deleted
            && ancestor->volumetric_mean_radius > 0 && std::isfinite(ancestor->volumetric_mean_radius)
            && ancestor->seqno >= 0 && ancestor->seqno < static_cast<int>(stellar_update_stamps.size())
            && cels[ancestor->seqno] == ancestor)
            stamp = &stellar_update_stamps[ancestor->seqno];
        const bool reused = stamp && stamp->generation == stellar_update_frame->generation
            && stamp->star == ancestor && stamp->epoch == update_epoch && stamp->center == mycenobj;
        if (!reused)
        {
            ancestor->update_location(update_epoch);
            if (stamp)
            {
                stamp->star = ancestor;
                stamp->center = mycenobj;
                stamp->epoch = update_epoch;
                stamp->generation = stellar_update_frame->generation;
            }
        }
        CelestialLocation relative = ancestor->location - here;
        ancestor->tmprel = Point(relative);
    }
    return true;
}

namespace
{
    using FrameContext = std::tuple<CelestialObject*, int, int, int, int, double, double, ViewMode,
        std::array<double, 9>>;
    struct FrameObjects
    {
        std::atomic<bool> dirty{true};
        bool prepared = false, published = false, initialize_projection = true;
        CelestialObject** array = nullptr;
        int count = -1;
        FrameContext context;
        std::vector<int> other, all, worlds, candidates, projected, bright;
        std::vector<const CelestialObject*> hosts;
        std::vector<cel_obj_class> classes;
        std::unordered_map<const CelestialObject*, std::vector<int>> systems;
        std::unordered_set<int> active;
        std::vector<std::pair<int, CelestialObject*>> previous;
    };
    FrameObjects& frame_objects()
    {
        // The registry also serves catalog teardown during process shutdown.
        static FrameObjects* state = new FrameObjects;
        return *state;
    }
    bool frame_location_pass = false;
    FrameContext frame_context()
    {
        return {mycenobj, whereami, selected, trackidx, editidx, zoom, dispcx, view_mode,
            {here.galactic_center.x, here.galactic_center.y, here.galactic_center.z,
                here.system_center.x, here.system_center.y, here.system_center.z,
                here.local_position.x, here.local_position.y, here.local_position.z}};
    }
    bool frame_matches()
    {
        const auto& state = frame_objects();
        return state.prepared && !state.dirty && state.array == cels && state.count == ncelobjs
            && state.context == frame_context();
    }
    void prepare_frame_objects()
    {
        auto& state = frame_objects();
        if (state.dirty || state.array != cels || state.count != ncelobjs)
        {
            state.other.clear();
            state.all.clear();
            state.systems.clear();
            state.hosts.assign(ncelobjs, nullptr);
            if (performance_enabled(8)) state.classes.assign(ncelobjs, class_unknown);
            else state.classes.clear();
            for (int index = 0; index < ncelobjs; ++index)
            {
                auto* object = cels[index];
                if (!object) continue;
                state.all.push_back(index);
                state.hosts[index] = object->cenobj;
                const auto cls = object->typeclass();
                if (performance_enabled(8)) state.classes[index] = cls;
                if (cls == class_planet || cls == class_moon) state.systems[object->cenobj].push_back(index);
                else state.other.push_back(index);
            }
            state.array = cels;
            state.count = ncelobjs;
            state.dirty = false;
            state.initialize_projection = true;
        }
        state.active.clear();
        const auto include_system = [&](const CelestialObject* root)
        {
            const auto found = state.systems.find(root);
            if (found != state.systems.end())
                for (int index : found->second) if (!cels[index]->deleted) state.active.insert(index);
        };
        include_system(mycenobj);
        for (int index : {whereami, selected, trackidx, editidx})
        {
            if (index < 0 || index >= ncelobjs || !cels[index] || cels[index]->deleted) continue;
            const auto* target = cels[index];
            const auto cls = target->typeclass();
            if (cls == class_planet || cls == class_moon) state.active.insert(index);
            const auto* root = target->cenobj ? target->cenobj : target;
            if (cls == class_planet || cls == class_moon) include_system(root);
            else if (cls == class_star && zoom > 1)
            {
                const auto found = state.systems.find(root);
                if (found == state.systems.end()) continue;
                const double distance = CelestialLocation(root->location).distance_to(here);
                for (int child : found->second)
                {
                    if (cels[child]->deleted || !cels[child]->cenobj || state.active.count(child)) continue;
                    double extent = 0;
                    const CelestialObject* cursor = cels[child];
                    for (int depth = 0; cursor && cursor->orbit && depth < 32; ++depth)
                    {
                        extent += cursor->orbit->semimajor_axis * (1 + cursor->orbit->eccentricity);
                        cursor = cursor->orbit->center;
                        if (cursor && cursor->typeclass() == class_star) break;
                    }
                    if (extent * zoom * dispcx >= std::max(1.0, distance)) state.active.insert(child);
                }
            }
        }
        auto& worlds = state.worlds;
        worlds.assign(state.active.begin(), state.active.end());
        std::sort(worlds.begin(), worlds.end());
        state.candidates.clear();
        state.candidates.reserve(state.other.size() + worlds.size());
        std::merge(state.other.begin(), state.other.end(), worlds.begin(), worlds.end(),
            std::back_inserter(state.candidates));
        if (!performance_enabled(2)) state.candidates = state.all;
        state.context = frame_context();
        state.prepared = true;
        state.published = false;
    }
    void reset_projection(int index, CelestialObject* object)
    {
        if (vmag_cache) vmag_cache[index] = 999;
        object->drawnx = object->drawnxmin = object->drawnxmax
            = object->drawny = object->drawnymin = object->drawnymax = -1e9;
        object->onscreen = object->label_shown = false;
    }
}

void invalidate_frame_objects()
{
    frame_objects().dirty = true;
}

const std::vector<int>& frame_object_candidates()
{
    if (!frame_matches()) prepare_frame_objects();
    return frame_objects().candidates;
}

const std::vector<int>& frame_active_world_candidates()
{
    if (!frame_matches()) prepare_frame_objects();
    return frame_objects().worlds;
}

const std::vector<int>& frame_draw_candidates()
{
    const auto& fallback = frame_object_candidates();
    return performance_enabled(5) && frame_objects().published ? frame_objects().projected : fallback;
}

const std::vector<int>& frame_bright_candidates()
{
    const auto& fallback = frame_object_candidates();
    return performance_enabled(5) && frame_objects().published ? frame_objects().bright : fallback;
}

const std::vector<int>& frame_pick_candidates()
{
    const auto& candidates = frame_draw_candidates();
    return view_mode == vm_system || view_mode == vm_sunclock ? frame_objects().all : candidates;
}

bool planetary_system_is_active(const CelestialObject* cel)
{
    const cel_obj_class cls = cel->typeclass();
    if (cls != class_planet && cls != class_moon) return true;
    if (performance_enabled(1) && frame_matches() && cel->seqno >= 0 && cel->seqno < ncelobjs
        && cels[cel->seqno] == cel
        && frame_objects().hosts[cel->seqno] == cel->cenobj)
        return frame_objects().active.count(cel->seqno) != 0;
    if (cel->cenobj == mycenobj) return true;
    for (int index : {whereami, selected, trackidx, editidx})
    {
        if (index < 0 || index >= ncelobjs || !cels[index] || cels[index]->deleted) continue;
        const CelestialObject* target = cels[index];
        if (target == cel) return true;
        const CelestialObject* target_system = target->cenobj ? target->cenobj : target;
        if (!cel->cenobj || target_system != cel->cenobj) continue;
        if (target->typeclass() == class_planet || target->typeclass() == class_moon) return true;
        if (target->typeclass() != class_star || !(zoom > 1)) continue;
        // A selected/ tracked stellar system becomes relevant when its orbits resolve on screen.
        double extent = 0;
        const CelestialObject* cursor = cel;
        for (int depth = 0; cursor && cursor->orbit && depth < 32; ++depth)
        {
            extent += cursor->orbit->semimajor_axis*(1+cursor->orbit->eccentricity);
            cursor = cursor->orbit->center;
            if (cursor && cursor->typeclass() == class_star) break;
        }
        const double distance = cel->cenobj->location.distance_to(here);
        if (extent*zoom*dispcx >= std::max(1.0, distance)) return true;
    }
    return false;
}

bool compute_object_location(CelestialObject* cel, bool* relative_valid)
{
    const auto mark_relative = [&](bool valid)
    {
        if (performance_enabled(13) && relative_valid) *relative_valid = valid;
    };
    mark_relative(false);
    if (!frame_location_pass && cel->typeclass() == class_planet)
        static_cast<Planet*>(cel)->refresh_host_statistics_if_changed();
    if (!planetary_system_is_active(cel))
    {
        cel->drawnx = cel->drawny = -1e9;
        return false;
    }
    bool star_in_box;
    int i = cel->seqno;
    const bool system_object = view_mode == vm_system && mycenobj &&
        (cel == mycenobj || cel->cenobj == mycenobj);

    CelestialLocation tmp = cel->location - here;
    cel->tmprel = Point(tmp);
    mark_relative(true);
    double viewer_distance = cel->tmprel.magnitude();
    double light_travel_time = system_object ? 0 : viewer_distance / speed_of_light;
    double AU_zoomed_squared = AU*AU*zoom*zoom;

    // Nothing about this depends on cel, but the dialogs call this function on a single object
    // outside any frame loop and would notice it going missing -- so memoize rather than hoist it
    // into the caller. A pow() and a log() apiece, over every object in the catalog, every frame.
    {
        static double mla_zoom = -1e300, mla_limit = -1e300, mla_value = 0;
        if (zoom != mla_zoom || normal_best_mag_limit != mla_limit)
        {
            mla_value = log(pow(magnbase, normal_best_mag_limit)*zoom) * invlogmagnbase;
            mla_zoom = zoom;
            mla_limit = normal_best_mag_limit;
        }
        mag_limit_adjusted = mla_value;
    }
    switch (cel->typeclass())
    {
        case class_star:
        // ((Star*)cel), not ((Star*)cels[i]): the two are the same object -- i is cel->seqno --
        // but only the first is guaranteed a Star by the case label we are standing in, and the
        // second form is what made the identical test in draw_objects() a genuine bad cast.
        if ((star_in_box = (i
            ? ((cel->cenobj == mycenobj && mycenobj)
                || ((Star*)cel)->is_in_visible_box(Point(here))
                || ((Star*)cel)->is_faved
                || (cbolbls_selected_idx == lbltype_planets && (((Star*)cel)->has_planets >= planets_lblcut) )
                || (cbolbls_selected_idx == lbltype_planethz && (((Star*)cel)->has_hz_planets) )
                )
            : true))) num_stars_in_box++;              // ANC
        if (i > 0)
        {
            ((Star*)cel)->tmp_vis_flag = star_in_box;
            if (!system_object && i!=selected && i!=trackidx && i!=editidx && i!=whereami && cel->cenobj!=mycenobj && !((Star*)cel)->is_faved)
            {
                if (!star_in_box && !((Star*)cel)->is_universally_visible()
                    && (cbolbls_selected_idx != lbltype_planets  || (((Star*)cel)->has_planets < planets_lblcut) )
                    && (cbolbls_selected_idx != lbltype_planethz || !(((Star*)cel)->has_hz_planets) )
                    )
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
                if (!redo_proper_motions && !cel->orbit && !((Star*)cel)->variability_period) return false;
            }
        }

        mark_relative(false);
        if (!update_stellar_lineage(static_cast<Star*>(cel), simnow, simnow - light_travel_time)) return false;
        tmp = cel->location - here;
        cel->tmprel = Point(tmp);
        mark_relative(true);
        if (!system_object && i > 0 && whereami >= 0 && cel->tmprel.magnitude() < cels[whereami]->volumetric_mean_radius)
        {
            cel->drawnx = cel->drawny = -1e9;
            return false;
        }
        break;

        case class_planet:
        if (i > 0)
        {
            if (!system_object && i!=selected && i!=trackidx && i!=editidx && i!=whereami)
            {
                if (cel->cenobj == mycenobj && cel->orbit &&
                    (
                        ((cel->mass < celmasslim)
                        && (cel->tmprel.squared_magnitude() > AU_zoomed_squared)
                    ))
                    // Second argument is a phase angle in radians, so zero is opposition, the
                    // body at its very brightest -- which is the right question to ask here,
                    // where all we want to know is whether it could ever be worth drawing. The
                    // old 1 was a radian, 57 degrees of gibbous, and phase_brightness() puts a
                    // world there at 0.23 of full where the old linear ramp claimed 0.68: left
                    // alone it would have quietly culled bodies that used to make the cut.
                    && (((Planet*)cel)->viewer_reflectance_magnitude(here, 0, mycenobj->absolute_magnitude, cel->orbit->semimajor_axis) > mag_limit_adjusted))
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
            }
        }
        mark_relative(false);
        ((Planet*)cel)->update_location(simnow - light_travel_time);
        break;

        case class_moon:
        if (i > 0)
        {
            if (!system_object && i!=selected && i!=trackidx && i!=editidx && i!=whereami)
            {
                if (cel->cenobj == mycenobj && cel->orbit &&
                    (
                            ((cel->mass >= celmasslim)
                        && (cel->tmprel.squared_magnitude() > AU_zoomed_squared)
                    ))
                    // Zero phase angle, i.e. the moon at opposition and fully lit -- see the
                    // matching test in the class_planet case above.
                    && (((Planet*)cel)->viewer_reflectance_magnitude(here, 0, mycenobj->absolute_magnitude, cel->orbit->semimajor_axis) > mag_limit_adjusted))
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
            }
        }
        mark_relative(false);
        ((Moon*)cel)->update_location(simnow - light_travel_time);
        break;

        case class_comet:
        mark_relative(false);
        ((Comet*)cel)->update_location(simnow - light_travel_time);
        break;

        case class_satellite:
        mark_relative(false);
        ((Satellite*)cel)->update_location(simnow - light_travel_time);

        default:
        ;
    }

    return true;
}

void compute_object_draw_coordinates()
{
    num_stars_in_box = 0;
    if (!ncelobjs) return;
    StellarUpdateFrame update_frame;
    struct LocationPass
    {
        bool saved = frame_location_pass;
        LocationPass() { frame_location_pass = true; }
        ~LocationPass() { frame_location_pass = saved; }
    } location_pass;
    Planet::reconcile_host_statistics();
    if (!performance_enabled(4))
    {
        for (int index = 0; index < ncelobjs; ++index)
        {
            if (cels[index] && !cels[index]->deleted && cels[index]->typeclass() == class_planet)
                static_cast<Planet*>(cels[index])->refresh_host_statistics_if_changed();
        }
    }
    auto& objects = frame_objects();
    objects.prepared = objects.published = false;

    if (redo_proper_motions)
    {
        // A time jump must move stars before old visibility boxes can reject them.
        for (int index = 0; index < ncelobjs; ++index)
        {
            if (!cels[index] || cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
            Star *star = static_cast<Star*>(cels[index]);
            update_stellar_lineage(star, simnow, simnow);
        }
    }

    int i, j, n;
    dispw = dispcx*2;
    disph = dispcy*2;
    celmasslim = lbllsys_mass_lim * 1000;

    eclipsed_light = nullptr;
    eclipsed_fraction = 0;
    if (whereami >= 0) mycenobj = cels[whereami]->cenobj;
    double mycenobj_distsq = mycenobj ? mycenobj->location.squared_distance_to(here) : 0;
    // The bounding-cube pre-filter in the loop below compares against a length, not a squared
    // one, so take the root once here rather than per object.
    double mycenobj_dist = sqrt(mycenobj_distsq);
    if (whereami >= 0)
    {
        std::vector<CelestialObject*> have_to_know;
        CelestialObject *cursor = cels[whereami];
        have_to_know.push_back(cursor);
        while (cursor->orbit && cursor->orbit->center)
        {
            cursor = cursor->orbit->center;
            have_to_know.insert(have_to_know.begin(), cursor);
        }

        n = have_to_know.size();
        for (i=0; i<2; i++)
        {
            for (j=0; j<n; j++)
                compute_object_location(have_to_know[j]);

            set_viewer_location_and_plane();
        }
    }

    if (redo_proper_motions) refresh_star_visibilities();

    prepare_frame_objects();
    if (objects.initialize_projection)
    {
        for (int index : objects.all) reset_projection(index, cels[index]);
        objects.initialize_projection = false;
    }
    CelestialObject* initial_center = mycenobj;
    for (int index : objects.candidates)
    {
        i = index;
        if (cels[i]->deleted) continue;
        bool relative_valid = false;
        const bool updated = compute_object_location(cels[i], performance_enabled(13) ? &relative_valid : nullptr);
        const bool cached_visible_star = (performance_enabled(8) ? objects.classes[i] : cels[i]->typeclass()) == class_star &&
            static_cast<Star*>(cels[i])->tmp_vis_flag;
        if (!updated && !cached_visible_star) continue;

        if (!relative_valid)
        {
            CelestialLocation tmp = cels[i]->location - here;
            cels[i]->tmprel = Point(tmp);
        }

        // If entering a new star system, change allegiance to new center object.
        if (whereami < 0 && cels[i]->type == star
            && fabs(cels[i]->tmprel.x) < mycenobj_dist
            && fabs(cels[i]->tmprel.y) < mycenobj_dist
            && fabs(cels[i]->tmprel.z) < mycenobj_dist
            && cels[i]->tmprel.squared_magnitude() < mycenobj_distsq)
        {
            mycenobj = cels[i]->cenobj;
            mycenobj_distsq = cels[i]->tmprel.squared_magnitude();
            mycenobj_dist = sqrt(mycenobj_distsq);
            here.rebase_system(mycenobj->location);
        }
    }

    if (initial_center != mycenobj)
    {
        prepare_frame_objects();
        for (int index : objects.candidates)
        {
            const auto cls = (performance_enabled(8) ? objects.classes[index] : cels[index]->typeclass());
            if (!cels[index]->deleted && (cls == class_planet || cls == class_moon))
                compute_object_location(cels[index]);
        }
    }
    Planet::reconcile_host_statistics();
    if (mycenobj)
    {
        if (whereami < 0) here.rebase_system(mycenobj->location);
        else
        {
            here.system_center = mycenobj->location.system_center;
            here.galactic_center = mycenobj->location.galactic_center;
        }

        static CelestialObject *llmycen = nullptr;
        if (llmycen != mycenobj)
        {
            lsyscache.clear();
            llmycen = mycenobj;
        }
        if ((explorer || view_mode == vm_system) && !lsyscache.size())
        {
            for (i=0; cels[i]; i++)
            {
                if (cels[i]->cenobj != mycenobj) continue;
                lsyscache.push_back(cels[i]);
            }

            std::sort(lsyscache.begin(), lsyscache.end(), [](const CelestialObject* a, const CelestialObject* b)
            {
                if (!a) return b != nullptr; 
                if (!b) return false;
                
                return *a < *b;
            });
        }
    }

    if (!frame_matches()) prepare_frame_objects();
    refresh_eclipse_casters();
    set_viewer_location_and_plane();
    if (trackidx >= 0) center_tracked();

    bool airy_rock = view_mode == vm_horizon && whereami > 0 && uses_rocky_map(cels[whereami]->type) && ((Planet*)cels[whereami])->get_surface_pressure();

    luminous_flux = cels[1] ? 0 : 1e10;
    inside_galaxy_idx = -1;
    if (!performance_enabled(3)) for (int index : objects.all) reset_projection(index, cels[index]);
    if (performance_enabled(3)) for (const auto& entry : objects.previous)
    {
        if (entry.first >= 0 && entry.first < ncelobjs && cels[entry.first] == entry.second)
            reset_projection(entry.first, entry.second);
    }
    objects.previous.clear();
    objects.projected.clear();
    objects.bright.clear();
    for (int index : objects.candidates)
    {
        i = index;
        if (cels[i]->deleted) continue;
        if (!performance_enabled(2) && !planetary_system_is_active(cels[i])) continue;
        if (isnan(cels[i]->tmprel.x)) continue;
        
        // From inside a galaxy's disc, it won't do to merely project an ellipse. We wrap a band all the way round with draw_galaxy_band() instead,
        // and this is where it decides. The margin lets the band take over a little sooner, when the galaxy fills most of the view.
        if ((performance_enabled(8) ? objects.classes[i] : cels[i]->typeclass()) == class_galaxy)
        {
            double gr = cels[i]->distance * ((Galaxy*)cels[i])->angular_diameter * 0.5;
            if (gr > 0 && cels[i]->tmprel.squared_magnitude() < gr * gr * 1.44)
                inside_galaxy_idx = i;
        }

        if ((i == whereami) && (view_mode != vm_system)) continue;
        
        // A galaxy's cenobj is itself. Galaxies get their own visibility test further down, on apparent magnitude, the same way
        // a star out of its visible box would.
        if ((performance_enabled(8) ? objects.classes[i] : cels[i]->typeclass()) == class_star)
        {
            Star* cels_i_star = ((Star*)cels[i]);

            if (cels_i_star
                && cels_i_star->seqno
                && i!=selected && i!=trackidx && i!=whereami && cels_i_star!=mycenobj
                && !cels_i_star->tmp_vis_flag
                && !cels_i_star->is_universally_visible())
            {
                cels[i]->drawnx = cels[i]->drawny = -1e9;
                continue;
            }
        }

        if (performance_enabled(3)) reset_projection(i, cels[i]);
        if (performance_enabled(3)) objects.previous.emplace_back(i, cels[i]);
        objects.projected.push_back(i);
        Point rel = cels[i]->tmprel;
        double relm = rel.magnitude();

        if (cels[i]->orbit
            && relm > 1e4 * cels[i]->orbit->semimajor_axis * cels[i]->orbit->semimajor_axis * zoom * zoom)
        {
            angular_radius[i] = std::asin(std::clamp(cels[i]->volumetric_mean_radius / fmax(relm, 1e-29), 0.0, 1.0));
            if (angular_radius[i] < 0.01*fiftyseventh)
            {
                cels[i]->drawnx = cels[i]->drawny = -1e9;
                continue;
            }
        }

        rel = to_viewer_plane(rel);

        cel_obj_class icls = (performance_enabled(8) ? objects.classes[i] : cels[i]->typeclass());
        if (icls == class_planet || icls == class_moon)
        {
            vmag_cache[i] = ((Planet*)cels[i])->viewer_reflectance_magnitude(here);

            double lit = eclipse_illumination(cels[i]);
            if (lit < 1.0) vmag_cache[i] -= log(lit) * invlogmagnbase;
        }
        else if (icls == class_comet) vmag_cache[i] = ((Comet*)cels[i])->viewer_comet_magnitude(here);
        else vmag_cache[i] = cels[i]->viewer_magnitude(here);

        if (!(vmag_cache[i] > -1.5)) objects.bright.push_back(i);
        double brght;

        if (view_mode == vm_horizon)
        {
            if (airy_rock) rel = refract_true_point(rel);

            if (vmag_cache[i] < -10 /* && rel.y >= 0 */)
            {
                brght = global_brightness * pow(magnbase, -vmag_cache[i]);
                float theta = cels[i]->Decl_as_radians(here);

                if (cels[whereami]->typeclass() == class_planet || cels[whereami]->typeclass() == class_moon)
                {
                    // Interpolated twilight values.
                    float theta_deg = theta * fiftyseven, twilight;
                    if (theta_deg >= 6) twilight = 6.0;
                    else if (theta_deg >= 0) twilight = (.24 + 0.96 * theta_deg);
                    else if (theta_deg >= -6) twilight = (.0305 + 0.03491666 * (theta_deg+6));
                    else if (theta_deg >= -12) twilight = (.0029 + 0.0046 * (theta_deg+12));
                    else if (theta_deg >= -18) twilight = (0.00048333333333 * (theta_deg+18));
                    else twilight = 0;

                    double add_flux = brght * (fmax(0, sin(theta)) + 0.01*twilight);

                    // Under a solar eclipse.
                    const double kTotalityFloor = 1.2e-3;
                    double obsc = eclipse_obscuration(cels[i]);
                    if (obsc > 0)
                    {
                        add_flux *= fmax(1.0 - obsc, kTotalityFloor);
                        if (obsc > eclipsed_fraction)
                        {
                            eclipsed_fraction = obsc;
                            eclipsed_light = cels[i];
                        }
                    }

                    if (!isnan(add_flux) && !isinf(add_flux) && cels[i]->type != artificial) luminous_flux += add_flux;
                }
            }
        }

        cels[i]->viewrel = rel;

        angular_radius[i] = std::asin(std::clamp(cels[i]->volumetric_mean_radius / fmax(relm, 1e-29), 0.0, 1.0));

        Cartesian2D cart(rel, azimuth+azimuth_correction, altitude, zoom);
        float dx = cart.x * dispcx + dispcx, dy = cart.y * dispcx + dispcy;
        cels[i]->drawnx = cels[i]->drawnxmin = cels[i]->drawnxmax = dx;
        cels[i]->drawny = cels[i]->drawnymin = cels[i]->drawnymax = dy;

        if (dx < 0 || dx >= dispw) continue;
        if (dy < 0 || dy >= disph) continue;
    }

    objects.context = frame_context();
    objects.published = true;
    Planet::reconcile_host_statistics();
    redo_proper_motions = false;
}

void set_center_objects()
{
    mtx.lock();
    loading_msg = std::string("Checking data integrity...");
    mtx.unlock();

    int i;
    first_letter_index.clear();
    for (i=0; i<36; i++) first_letter_index.push_back(std::vector<CelestialObject*>());

    // Emptied for the same reason as the letter buckets above: this pass rebuilds both indices
    // from the whole array, and it runs more than once -- loading a universe file calls it, and
    // then the end of the startup sequence calls it again. Left uncleared, every star in every
    // constellation was listed twice over after a universe file was loaded on the command line.
    constellation_index.clear();
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->deleted) continue;

        // Center of each star system
        if (!cels[i]->cenobj) cels[i]->cenobj = cels[i];

        // Orbit integrity check
        if (cels[i]->orbit && cels[i]->orbit->center == cels[i])
        {
            delete cels[i]->orbit;
            cels[i]->orbit = nullptr;
        }

        cel_obj_class cls = cels[i]->typeclass();

        if (cls == class_moon && cels[i]->volumetric_mean_radius < 0.1*((Moon*)cels[i])->height)
        {
            Moon *m = (Moon*)cels[i];
            m->volumetric_mean_radius = 0.5 * pow(m->depth * m->width * m->height, 1.0/3.0);
        }

        // Multiple star integrity
        if (cls == class_star)
        {
            Star *s = (Star*)cels[i];
            if (s->multisys)
            {
                char comp = s->multisys->is_member(s);
                if (comp > 'A' || s->multisys->num_members() > 1)
                {
                    if (comp)
                    {
                        // Only force "<primary base> <letter>" when this star's current name is
                        // empty or already just the primary's base name -- never when it already
                        // reads differently (e.g. Zeta 1/2 Reticuli, each with its own Bayer name).
                        Star *primary = s->multisys->get_member('A');
                        if (!s->has_custom_name && primary && primary != s)
                        {
                            std::string base = lop_component(primary->name);
                            std::string current = trim(lop_component(s->name));
                            if (!current.size() || current == base)
                            {
                                strcpy(s->name, (base + std::string(" ") + std::string(1, comp)).c_str());
                                s->namelen = strlen(s->name);
                            }
                        }
                    }
                    else
                    {
                        s->set_component(s->multisys->next_available(), s);
                    }
                }
            }
        }

        // System center integrity
        int hops = 0;
        while (cels[i]->cenobj->orbit && cels[i]->cenobj->orbit->center && cels[i]->cenobj->orbit->center->typeclass() != class_galaxy)
        {
            // Detect circular references.
            if (cels[i]->cenobj->orbit->center == cels[i])
            {
                hops++;
                if (hops > MAX_CELOBJS)
                {
                    cels[i]->cenobj = cels[i];
                    std::cerr << "Circular cenobj refrence for " << cels[i]->name << std::endl;
                    break;
                }
                bool cels_i_is_true_center = (cels[i]->seqno < cels[i]->cenobj->seqno);
                bool ct = compare_types(cels[i]->cenobj->type, cels[i]->type);
                if (ct && cels[i]->type != cels[i]->cenobj->type) cels_i_is_true_center = true;
                else if (!ct) cels_i_is_true_center = false;
                else if (cels[i]->absolute_magnitude < cels[i]->cenobj->absolute_magnitude) cels_i_is_true_center = true;
                else if (cels[i]->absolute_magnitude > cels[i]->cenobj->absolute_magnitude) cels_i_is_true_center = false;

                if (cels_i_is_true_center)
                {
                    cels[i]->cenobj = cels[i];
                    delete cels[i]->orbit;
                    cels[i]->orbit = nullptr;
                }
                else
                {
                    cels[i]->cenobj->cenobj = cels[i]->cenobj;
                    delete cels[i]->cenobj->orbit;
                    cels[i]->cenobj->orbit = nullptr;
                }
                break;
            }
            cels[i]->cenobj = cels[i]->cenobj->orbit->center;
            if (cels[i]->cenobj->orbit && cels[i]->cenobj->orbit->center == cels[i]->cenobj) cels[i]->cenobj->orbit = nullptr;
        }

        if (cels[i]->type == star)
        {
            Star *s = (Star*)cels[i];
            s->update_location(simnow);
            if (!s->fictional_origin.is_object() && s->orbit && s->orbit->center && s->orbit->center->typeclass() == class_star &&
                std::isfinite(s->apparent_magnitude) && s->apparent_magnitude >= -40 && s->apparent_magnitude < 99)
            {
                // Bound companions use the adopted system distance, rather than an independent
                // catalog parallax. Keep the reference distance fixed when simulation time moves.
                const double observed_distance = s->cenobj->distance;
                if (std::isfinite(observed_distance) && observed_distance > 0)
                {
                    s->absolute_magnitude = s->apparent_magnitude - 5.0 * log10(observed_distance / (10.0 * parsec));
                }
            }
        }
        else if (cels[i]->orbit)
        {
            if (cels[i]->typeclass() == class_planet) ((Planet*)cels[i])->update_location(simnow);
            else if (cels[i]->typeclass() == class_moon) ((Moon*)cels[i])->update_location(simnow);
            else if (cels[i]->typeclass() == class_satellite) ((Satellite*)cels[i])->update_location(simnow);
        }

        // Name and constellation indices
        char c = cels[i]->name[0];
        bool is_HD;
        if (c == 'H' && cels[i]->name[1] == 'D')
        {
            int hdno = atoi(&cels[i]->name[2]);
            is_HD = (hdno && hdcache && hdcache[hdno]);
        }
        else is_HD = false;
        if (!is_HD)
        {
            if (c >= '0' && c <= '9') c -= '0';
            else if (c >= 'A' && c <= 'Z') c = c - 'A' + 10;
            else if (c >= 'a' && c <= 'z') c = c - 'a' + 10;
            if (c >= 0 && c < 36) first_letter_index[c].push_back(cels[i]);
        }

        if (cels[i]->typeclass() == class_star)
        {
            Star *s = (Star*)cels[i];
            if (strlen(s->constellation))
            {
                constellation_index[std::string(s->constellation)].push_back(cels[i]);
            }

            if (s->orbit && s->orbit->center && !compare_types(s->orbit->center->type, s->type))
            {
                delete s->orbit;
                s->orbit = nullptr;
                s->cenobj = s;
            }

            if (!s->BV_color && !s->UB_color)
            {
                s->estimate_BV();
                s->estimate_UB();
            }
        }

        if (!cels[i]->user_edited || cels[i]->user_added) cels[i]->origname = cels[i]->name;
        if (cels[i]->orbit && cels[i]->orbit->center) cels[i]->origcenname = cels[i]->orbit->center->name;
    }

    invalidate_frame_objects();
    Planet::rebuild_host_statistics();
    ((Star*)cels[0])->make_universally_visible();

    if (showfails)
    {
        // Compare settled intrinsic magnitudes after all components adopt their system distance.
        for (int index = 0; cels[index]; index++)
        {
            if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
            Star *primary = static_cast<Star*>(cels[index]);
            if (!primary->multisys || primary->multisys->get_member('A') != primary ||
                !std::isfinite(primary->absolute_magnitude) || primary->absolute_magnitude >= 1e28) continue;
            Star *brightest = primary;
            for (char component = 'B'; component <= 'Z'; component++)
            {
                Star *member = primary->multisys->get_member(component);
                if (member && !member->deleted && std::isfinite(member->absolute_magnitude) &&
                    member->absolute_magnitude < brightest->absolute_magnitude) brightest = member;
            }
            if (brightest != primary)
            {
                std::cerr << "Primary fainter than companion: "
                    << primary->name << " ( " << primary->spectral_type << " " << primary->absolute_magnitude << ") vs. "
                    << brightest->name << " ( " << brightest->spectral_type << " " << brightest->absolute_magnitude << ")." << std::endl;
            }
        }
    }

    #if 0
    #ifdef DEBUG
    // Test a sample constellation.
    const char* test_cons = "UMa";
    int n = constellation_index[test_cons].size();
    for (i=0; i<n; i++)
    {
        Star *s = (Star*)constellation_index[test_cons][i];
        std::cout << s->Bayer << " " << s->name << std::endl << std::flush;
    }
    #endif
    #endif
}
