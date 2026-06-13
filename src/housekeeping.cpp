
#include "housekeeping.h"
#include "inputs.h"

void refresh_star_visibilities()
{
    int i;
    for (i=0; cels[i]; i++) if (cels[i]->typeclass() == class_star) ((Star*)cels[i])->is_really_truly_in_visible_box(here);
}


void set_viewer_location_and_plane()
{
    if (whereami < 0)
    {
        view_mode = vm_skyatlas;
        return;
    }

    if (whereami >= 0 && cels[whereami]->typeclass() == class_satellite)
    {
        view_mode = vm_skyatlas;

        if (cels[whereami]->orbit->center)
        {
            here = cels[whereami]->location;
            Point me = cels[whereami]->location;
            Point zenith = me - cels[whereami]->orbit->center->location;
            here.equatorial_plane = align_points_3d(zenith, yaxis, center);
            viewchanged = true;
            return;
        }
    }

    if (view_mode == vm_skyatlas)
    {
        here = cels[whereami]->location;
        azimuth_correction = 0;
        npaz = 0;
    }
    else if (view_mode == vm_horizon)
    {
        CelestialObject *cel = cels[whereami];
        here = cel->location;
        assert(cel->sidereal_rotational_period != 0);

        double rads_sec = cel->sidereal_rotational_period ? ((M_PI * 2) / cel->sidereal_rotational_period) : 0;
        double seconds_since_epoch = (simnow - J2000_TIME_T) + ((J2000 - cel->epoch)*oneday);
        double timeofday = fmod(rads_sec * seconds_since_epoch - cel->lon_J2000_offset, M_PI*2);
        if (cel->orbit && fabs(cel->orbit->period - cel->sidereal_rotational_period) < 0.01 * cel->orbit->period)
        {
            timeofday += cel->orbit->ascending_node;
            timeofday += cel->orbit->arg_periapsis;
            timeofday += cel->orbit->mean_anomaly;
            timeofday += M_PI_2;
        }

        bool dwh = false;
        if (cel->typeclass() == class_moon)
            dwh = (((Moon*)cel)->depth > zero_isnt_really_zero
                && ((Moon*)cel)->width > zero_isnt_really_zero
                && ((Moon*)cel)->height > zero_isnt_really_zero);

        double obl = 1.0 - cel->oblateness, equatorial_radius;
        if (dwh)
            equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * 500;
        else
            equatorial_radius = cel->volumetric_mean_radius * pow(1.0 - cel->oblateness, 0.333);

        Point cursor = Point::from_ra_dec(viewer_lon, viewer_lat, dwh ? 1 : equatorial_radius, 0);

        if (dwh)
        {
            cursor.x *= ((Moon*)cel)->width * 500;
            cursor.y *= ((Moon*)cel)->height * 500;
            cursor.z *= ((Moon*)cel)->depth * 500;
        }
        else cursor.y *= obl;
        cursor = rotate3D(cursor, center, yaxis, -timeofday);
        cursor = rotate3D(cursor, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);

        here.local_position = cel->location.local_position + cursor;
        here.equatorial_plane = align_points_3d(cursor, yaxis, center);

        Point north_pole = rotate3D(yaxis, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
        npdummy.location = cel->location;
        npdummy.location.local_position = north_pole;
        azimuth_correction = 0;
        npaz = fmod(npdummy.RA_as_radians(here, 0), M_PI*2);
        azimuth_correction = -npaz;
        viewchanged = true;
    }
    else if (view_mode == vm_sunclock)
    {
        here = cels[whereami]->location;
        azimuth_correction = 0;
        npaz = 0;
    }
}

double dispw, disph, lmasslim;
bool compute_object_location(CelestialObject* cel, int i)
{
    num_stars_in_box = 0;
    bool star_in_box;

    CelestialLocation tmp = cel->location - here;
    cel->tmprel = Point(tmp);
    switch (cel->typeclass())
    {
        case class_star:
        if (i > 0)
        {
            if ((star_in_box = (i ? ((Star*)cel)->is_in_visible_box(Point(here)) : true))) num_stars_in_box++;              // ANC
            ((Star*)cel)->tmp_vis_flag = star_in_box;
            if (i!=selected && i!=trackidx && i!=editidx && i!=whereami && cel->cenobj!=mycenobj)
            {
                if (!star_in_box)
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
                if (!redo_proper_motions && !cel->orbit) return false;
                if (cel->orbit && cel->orbit->center && (whereami < 0 || cel->orbit->center != cels[whereami])
                    && (cel->orbit->center->drawnx < 0 || cel->orbit->center->drawny < 0
                        || cel->orbit->center->drawnx > dispw || cel->orbit->center->drawny > disph
                        || cel->orbit->semimajor_axis < cel->location.distance_to(here)*1e-4*zoom
                        )
                    )
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
            }
        }

        ((Star*)cel)->update_location(simnow);
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
                        ((cel->mass < lmasslim)
                        && (cel->tmprel.magnitude() > AU)
                    ))
                    && (((Planet*)cel)->viewer_reflectance_magnitude(here, 1, mycenobj->absolute_magnitude, cel->orbit->semimajor_axis) > 6.5))
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
            }
        }
        ((Planet*)cel)->update_location(simnow);
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
                            ((cel->mass >= lmasslim)
                        && (cel->tmprel.magnitude() > AU)
                    ))
                    && (((Planet*)cel)->viewer_reflectance_magnitude(here, 1, mycenobj->absolute_magnitude, cel->orbit->semimajor_axis) > 6.5))
                {
                    cel->drawnx = cel->drawny = -1e9;
                    return false;
                }
            }
        }
        ((Moon*)cel)->update_location(simnow);
        break;

        case class_satellite:
        ((Satellite*)cel)->update_location(simnow);

        default:
        ;
    }

    return true;
}

void compute_object_draw_coordinates()
{
    if (!ncelobjs) return;
    int i, j, n, bx, by;
    dispw = dispcx*2;
    disph = dispcy*2;
    lmasslim = lbllsys_mass_lim * 1000;
    if (whereami >= 0) mycenobj = cels[whereami]->cenobj;
    double mycenobj_dist = mycenobj->location.distance_to(here);
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
                compute_object_location(have_to_know[j], -1);

            here = cels[whereami]->location;
        }
    }

    for (i=0; i<drawn_cache_split; i++) for (j=0; j<drawn_cache_split; j++) drawnblocks[i][j].clear();
    for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        CelestialLocation tmp = cels[i]->location - here;
        cels[i]->tmprel = Point(tmp);

        if (!compute_object_location(cels[i], i)) continue;

        // If entering a new star system, change allegiance to new center object.
        if (whereami < 0 && cels[i]->type == star
            // .magnitude() is more expensive than simple xyz comparisons, and the distance sphere will always fit in the dimension cube.
            && cels[i]->tmprel.x < mycenobj_dist && cels[i]->tmprel.y < mycenobj_dist && cels[i]->tmprel.z < mycenobj_dist
            && cels[i]->tmprel.magnitude() < mycenobj_dist)
        {
            mycenobj = cels[i]->cenobj;
        }
    }

    set_viewer_location_and_plane();
    if (trackidx >= 0) center_tracked();

    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    if (viewchanged || redo_proper_motions)
    {
        luminous_flux = cels[1] ? 0 : 1e10;
        for (i=0; cels[i] && i<MAX_CELOBJS; i++)
        {
            if (isnan(cels[i]->tmprel.x)) continue;
            if (i == whereami) continue;

            if (cels[i]->typeclass() == class_star
                && i!=selected && i!=trackidx && i!=whereami && cels[i]->cenobj!=mycenobj
                && !((Star*)cels[i])->tmp_vis_flag)
                continue;

            Point rel = cels[i]->tmprel;

            rel = rotate3D(rel, center, viewer_plane.v, -viewer_plane.a);

            vmag_cache[i] = (cels[i]->typeclass() == class_planet || cels[i]->typeclass() == class_moon)
                ? ((Planet*)cels[i])->viewer_reflectance_magnitude(here)
                : cels[i]->viewer_magnitude(here);

            double brght;

            if ((view_mode == vm_horizon) && vmag_cache[i] < -10 && rel.y >= 0)
            {
                brght = global_brightness * pow(magnbase, -vmag_cache[i]);
                float theta = cels[i]->Decl_as_radians(here);
                double add_flux = brght * sin(theta);
                if (!isnan(add_flux) && !isinf(add_flux)) luminous_flux += add_flux;
            }

            brght = global_brightness * pow(magnbase, -vmag_cache[i] + sky_mag_shift);
            bloomrad_cache[i] = fmax(1.414, sqrt(brght)*global_brightness);

            cels[i]->viewrel = rel;

            Cartesian2D cart(rel, azimuth+azimuth_correction, altitude, zoom);
            float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);
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

            angular_radius[i] = fabs(std::atan2(cels[i]->volumetric_mean_radius, rel.magnitude()));
        }
    }

    redo_proper_motions = false;
}

void set_center_objects()
{
    int i;
    first_letter_index.clear();
    for (i=0; i<36; i++) first_letter_index.push_back(std::vector<CelestialObject*>());
    for (i=0; cels[i]; i++)
    {
        // Center of each star system
        if (!cels[i]->cenobj) cels[i]->cenobj = cels[i];

        // Orbit integrity check
        if (cels[i]->orbit && cels[i]->orbit->center == cels[i])
        {
            delete cels[i]->orbit;
            cels[i]->orbit = nullptr;
        }

        // System center integrity
        while (cels[i]->cenobj->orbit && cels[i]->cenobj->orbit->center && cels[i]->cenobj->orbit->center->typeclass() != class_galaxy)
            cels[i]->cenobj = cels[i]->cenobj->orbit->center;
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
        if (c != 'H' || cels[i]->name[1] != 'D')
        {
            if (c >= '0' && c <= '9') c -= '0';
            else if (c >= 'A' && c <= 'Z') c = c - 'A' + 10;
            else if (c >= 'a' && c <= 'z') c = c - 'a' + 10;
            if (c >= 0 && c < 36)
                first_letter_index[c]
                    .push_back(cels[i]);
        }

        if (cels[i]->typeclass() == class_star)
        {
            Star *s = (Star*)cels[i];
            if (strlen(s->constellation))
            {
                constellation_index[std::string(s->constellation)].push_back(cels[i]);
            }
        }
    }

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
}
