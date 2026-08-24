
#include "housekeeping.h"
#include "inputs.h"
#include "visuals.h"        // eclipse_obscuration(): an eclipse dims the daylight computed here

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

    Point cursor = Point::from_ra_dec(viewer_lon, viewer_lat, dwh ? 1 : equatorial_radius, 0);

    if (dwh)
    {
        cursor.x *= ((Moon*)cel)->width * .5;
        cursor.y *= ((Moon*)cel)->height * .5;
        cursor.z *= ((Moon*)cel)->depth * .5;
    }
    else cursor.y *= obl;
    cursor = rotate3D(cursor, center, yaxis, -cel->timeofday());
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

    if (vplane_mode == vplane_local && (view_mode == vm_spaceship || view_mode == vm_skymap))
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
bool compute_object_location(CelestialObject* cel)
{
    bool star_in_box;
    int i = cel->seqno;

    CelestialLocation tmp = cel->location - here;
    cel->tmprel = Point(tmp);
    double viewer_distance = cel->tmprel.magnitude();
    double light_travel_time = viewer_distance / speed_of_light;
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
            ? (((Star*)cel)->is_in_visible_box(Point(here))
                || (cbolbls_selected_idx == lbltype_planets && (((Star*)cel)->has_planets >= planets_lblcut) )
                || (cbolbls_selected_idx == lbltype_planethz && (((Star*)cel)->has_hz_planets) )
                )
            : true))) num_stars_in_box++;              // ANC
        if (i > 0)
        {
            ((Star*)cel)->tmp_vis_flag = star_in_box;
            if (i!=selected && i!=trackidx && i!=editidx && i!=whereami && cel->cenobj!=mycenobj)
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
                if (cel->orbit && cel->orbit->center && (whereami < 0 || cel->orbit->center != cels[whereami])
                    && (cel->orbit->center->drawnx < 0 || cel->orbit->center->drawny < 0
                        || cel->orbit->center->drawnx > dispw || cel->orbit->center->drawny > disph
                        || cel->orbit->semimajor_axis < cel->location.distance_to(here)*1e-4*zoom
                        )
                     && !((Star*)cel)->is_universally_visible()
                    )
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
            }
        }

        ((Star*)cel)->update_location(simnow - light_travel_time);
        tmp = cel->location - here;
        cel->tmprel = Point(tmp);
        if (i > 0 && whereami >= 0 && cel->tmprel.magnitude() < cels[whereami]->volumetric_mean_radius)
        {
            cel->drawnx = cel->drawny = -1e9;
            return false;
        }
        break;

        case class_planet:
        if (i > 0)
        {
            if (i!=selected && i!=trackidx && i!=editidx && i!=whereami)
            {
                if (cel->cenobj!=mycenobj)
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
                else if (cel->orbit &&
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
        ((Planet*)cel)->update_location(simnow - light_travel_time);
        break;

        case class_moon:
        if (i > 0)
        {
            if (i!=selected && i!=trackidx && i!=editidx && i!=whereami)
            {
                if (cel->cenobj!=mycenobj)
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
                else if (cel->orbit &&
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
        ((Moon*)cel)->update_location(simnow - light_travel_time);
        break;

        case class_comet:
        ((Comet*)cel)->update_location(simnow - light_travel_time);
        break;

        case class_satellite:
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
    if (!bx_cache) bx_cache = new int[MAX_CELOBJS];
    if (!by_cache) by_cache = new int[MAX_CELOBJS];

    int i, j, n, bx, by;
    dispw = dispcx*2;
    disph = dispcy*2;
    celmasslim = lbllsys_mass_lim * 1000;

    refresh_eclipse_casters();
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

    for (i=0; i<drawn_cache_split; i++) for (j=0; j<drawn_cache_split; j++) drawnblocks[i][j].clear();
    for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (cels[i]->deleted) continue;
        if (!compute_object_location(cels[i])) continue;

        CelestialLocation tmp = cels[i]->location - here;
        cels[i]->tmprel = Point(tmp);                           // fix race condition: tmprel must come AFTER tmp, not before.

        // If entering a new star system, change allegiance to new center object.
        if (whereami < 0 && cels[i]->type == star
            && fabs(cels[i]->tmprel.x) < mycenobj_dist
            && fabs(cels[i]->tmprel.y) < mycenobj_dist
            && fabs(cels[i]->tmprel.z) < mycenobj_dist
            && cels[i]->tmprel.squared_magnitude() < mycenobj_distsq)
        {
            mycenobj = cels[i]->cenobj;
            CelestialLocation was_here = here;
            here.galactic_center = cels[i]->location.galactic_center;           // TODO:
            here.system_center = cels[i]->location.system_center;
            here.local_position = Point(was_here) - here.system_center;
        }
    }

    if (mycenobj)
    {
        here.system_center = mycenobj->location.system_center;
        here.galactic_center = mycenobj->location.galactic_center;
    }

    set_viewer_location_and_plane();
    if (trackidx >= 0) center_tracked();

    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    if (1) // viewchanged || redo_proper_motions)
    {
        luminous_flux = cels[1] ? 0 : 1e10;
        inside_galaxy_idx = -1;
        for (i=0; cels[i] && i<MAX_CELOBJS; i++)
        {

            vmag_cache[i] = 999;                    // in case we bail early, so coming back into view later doesn't flare way too bright
            if (cels[i]->deleted) continue;
            cels[i]->drawnx = cels[i]->drawnxmin = cels[i]->drawnxmax
                = cels[i]->drawny = cels[i]->drawnymin = cels[i]->drawnymax = -1e9;
            if (isnan(cels[i]->tmprel.x)) continue;
            
            // From inside a galaxy's disc, it won't do to merely project an ellipse. We wrap a band all the way round with draw_galaxy_band() instead,
            // and this is where it decides. The margin lets the band take over a little sooner, when the galaxy fills most of the view.
            if (cels[i]->typeclass() == class_galaxy)
            {
                double gr = cels[i]->distance * ((Galaxy*)cels[i])->angular_diameter * 0.5;
                if (gr > 0 && cels[i]->tmprel.squared_magnitude() < gr * gr * 1.44)
                    inside_galaxy_idx = i;
            }

            if (i == whereami) continue;
            
            // A galaxy's cenobj is itself. Galaxies get their own visibility test further down, on apparent magnitude, the same way
            // a star out of its visible box would.
            if (cels[i]->typeclass() == class_star)
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

            Point rel = cels[i]->tmprel;
            double relm = rel.magnitude();

            if (cels[i]->orbit
                && relm > 1e4 * cels[i]->orbit->semimajor_axis * cels[i]->orbit->semimajor_axis * zoom * zoom)
            {
                angular_radius[i] = fabs(std::atan2(cels[i]->volumetric_mean_radius, rel.magnitude()));
                if (angular_radius[i] < 0.01*fiftyseventh)
                {
                    cels[i]->drawnx = cels[i]->drawny = -1e9;
                    continue;
                }
            }

            rel = rotate3D(rel, center, viewer_plane.v, -viewer_plane.a);

            cel_obj_class icls = cels[i]->typeclass();
            if (icls == class_planet || icls == class_moon)
            {
                vmag_cache[i] = ((Planet*)cels[i])->viewer_reflectance_magnitude(here);

                double lit = eclipse_illumination(cels[i]);
                if (lit < 1.0) vmag_cache[i] -= log(lit) * invlogmagnbase;
            }
            else if (icls == class_comet) vmag_cache[i] = ((Comet*)cels[i])->viewer_comet_magnitude(here);
            else vmag_cache[i] = cels[i]->viewer_magnitude(here);

            double brght;

            if (view_mode == vm_horizon)
            {
                /* Point axis = compute_normal(rel, yaxis, center);
                rel = rotate3D(rel, center, axis, ((Planet*)cels[whereami])->atmospheric_refraction(cels[i]->Decl_as_radians(here))); */

                rel = refract_true_point(rel);

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

                        // A solar eclipse, from underneath it. Nothing here is specific to
                        // eclipses: the daylight this loop is accumulating is simply the light
                        // actually arriving, and during an eclipse most of it is not arriving.
                        // Everything the sky does about that follows on its own from
                        // luminous_flux -- draw_sky_gradient() dims, sky_mag_shift falls with
                        // it, and stars that were being drowned out clear the magnitude limit
                        // and come out, which is exactly what happens outdoors.
                        //
                        // The floor is what still lights the sky once the disc is gone -- the
                        // corona, and air outside the shadow scattering light back in under it.
                        // Its value is set by how totality should *look*, not by the photometry:
                        // the corona really is about a millionth of the photosphere, and a
                        // millionth put through the compression curve draw_sky_gradient() runs
                        // luminous_flux through comes out as an essentially black sky with the
                        // Milky Way across it, which is not what anyone standing under an
                        // eclipse has ever seen. Totality is a deep blue twilight with the
                        // brightest few stars and planets out, and 1e-3 is what lands there on
                        // this particular curve.
                        //
                        // Turn it down for a darker totality, up for a milder one. It is a
                        // fraction of the light the sun would be delivering uneclipsed, so it
                        // follows the sun up and down the sky on its own.
                        const double kTotalityFloor = 1.2e-3;
                        double obsc = eclipse_obscuration(cels[i]);
                        if (obsc > 0)
                        {
                            // fmax, not a blend: at true totality the geometry really does reach
                            // an obscuration of exactly 1 (measured), so a floor written as a
                            // share of the covered fraction would have nothing left to scale and
                            // the sky would go out entirely. This way the floor is a hard limit
                            // on how dark an eclipse can drive the daylight, whatever the
                            // geometry does.
                            add_flux *= fmax(1.0 - obsc, kTotalityFloor);
                            if (obsc > eclipsed_fraction)
                            {
                                eclipsed_fraction = obsc;
                                eclipsed_light = cels[i];
                            }
                        }

                        if (!isnan(add_flux) && !isinf(add_flux)) luminous_flux += add_flux;
                    }
                }
            }

            cels[i]->viewrel = rel;

            // Ahead of the screen-bounds bails below, not after them. Every rotation rel has been
            // through preserves its magnitude, so the value is the same either way -- what changes
            // is that an object off the edge of the screen now gets a current one instead of
            // keeping whatever it had on the last frame its centre was inside the window.
            //
            // Nothing culls on being offscreen alone: draw_objects() deliberately exempts anything
            // with a big enough disc ("angular_radius[i]*zoom < sphere_rad_threshold" guarding the
            // drawnx/drawny test there), because a disc whose centre is past the edge can still
            // throw its glare across the visible frame. So a body that drifts out of frame while
            // large goes on being drawn from a frozen angular_radius, and draw_one_object() divides
            // its flare by exactly that ("f_ang"). vmag_cache[i] is still updated every frame, a
            // few lines up -- so on a close flyby the brightness kept climbing while the disc it
            // was being spread over stayed pinned at its stale value. Bug: the flare of an object
            // that had left the frame staying far too bright until it came back into view and
            // refreshed this.
            angular_radius[i] = fabs(std::atan2(cels[i]->volumetric_mean_radius, rel.magnitude()));

            Cartesian2D cart(rel, azimuth+azimuth_correction, altitude, zoom);
            float dx = cart.x * dispcx + dispcx, dy = cart.y * dispcx + dispcy;
            cels[i]->drawnx = cels[i]->drawnxmin = cels[i]->drawnxmax = dx;
            cels[i]->drawny = cels[i]->drawnymin = cels[i]->drawnymax = dy;

            if (dx < 0 || dx >= dispw) continue;
            if (dy < 0 || dy >= disph) continue;

            bx = dx*drawblxscalex;
            by = dy*drawblxscaley;
            if (bx<0 || bx>=drawn_cache_split || by<0 || by>=drawn_cache_split) continue;
            drawnblocks[bx][by].push_back(i);
            bx_cache[i] = bx;
            by_cache[i] = by;
                    }
    }

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
                                strcpy(s->name, (base + std::string(" ") + std::string(1, comp)).c_str());
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
                if (cels[i]->type < cels[i]->cenobj->type) cels_i_is_true_center = true;
                else if (cels[i]->type > cels[i]->cenobj->type) cels_i_is_true_center = false;
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
            if (cels[i]->orbit && cels[i]->absolute_magnitude < cels[i]->cenobj->absolute_magnitude)
                cels[i]->absolute_magnitude = cels[i]->cenobj->absolute_magnitude + 1;
            ((Star*)cels[i])->update_location(simnow);
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

            if (s->orbit && s->orbit->center && s->orbit->center->type > s->type)
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

    ((Star*)cels[0])->make_universally_visible();

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
