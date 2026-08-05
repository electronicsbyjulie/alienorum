
#include "globals.h"
#include "visuals.h"
#include "loaders.h"
#include "sphere_impostor.h"
#include "gputex.h"

using namespace alienorum;

double jay, appmag, bloomrad, flare, theta, lmasslim, hz_y;
ImVec2 xycoord;
ImFont *global_font = nullptr, *Greek_font = nullptr;
const char *Greek_symbol_mapping = "abgdezhuiklmnqoprstyfxjv";

void draw_ra_dec_lines()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!cels[1]) return;
    int i, j;
    Cartesian2D prev, zdes;
    ImU32 gc = rgba_apply_redlight(global_style.grid_color);
    ImU32 gcb = rgba_apply_redlight(global_style.grid_color_brighter);
    ImU32 ec = rgba_apply_redlight(global_style.ecliptic_color);
    double node = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;
    double myeq = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;
    npaz = (view_mode == vm_horizon) ? fmod(npdummy.RA_as_radians(here, myeq), _pi*2) : 0;
    bool prev_valid = false;
    int jstart = -80; // (view_mode == vm_horizon) ? 0 : -80;
    bool is_sat = (whereami>0) && (cels[whereami]->typeclass() == class_satellite);
    Rotation ra_dec_plane = (whereami>0) ? (is_sat
            ? cels[whereami]->location.orbital_plane
            : cels[whereami]->location.equatorial_plane)
        : here.equatorial_plane;

    // RA and Dec lines.
    for (i=0; i<24; i++)
    {
        prev_valid = false;
        for (j=jstart; j<=80; j+=10)
        {
            Point jadolzhnaperejexatdoma = Point::from_ra_dec(fiftyseventh * i * 15, fiftyseventh * j, 5, node);
            if (view_mode == vm_horizon || is_sat)
            {
                jadolzhnaperejexatdoma = rotate3D(jadolzhnaperejexatdoma, center, ra_dec_plane.v, -ra_dec_plane.a);
                jadolzhnaperejexatdoma = to_viewer_plane(jadolzhnaperejexatdoma, 1);
                jadolzhnaperejexatdoma = rotate3D(jadolzhnaperejexatdoma, center, yaxis, -azimuth_correction);
            }
            if (view_mode == vm_horizon) jadolzhnaperejexatdoma = refract_true_point(jadolzhnaperejexatdoma);
            zdes = Cartesian2D(jadolzhnaperejexatdoma, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (j > jstart)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (dx1 < -33554432 || dy1 < -33554432 || dx2 < -33554432 || dy2 < -33554432) continue;

                ImVec2 destart(dx1, dy1), deend(dx2, dy2);

                // if (distance(destart, deend) > 500) std::cout << "Dec gridline from " << dx1 << "," << dy1 << " to " << dx2 << "," << dy2 << std::endl;

                if (prev_valid)
                {
                    wrapped_line(destart, deend, gc, 1.1, io);
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    for (j=jstart; j <= 80; j+=10)
    {
        prev_valid = false;
        for (i=0; i<=360; i++)
        {
            Point umenjanetdeneg = Point::from_ra_dec(fiftyseventh * i, fiftyseventh * j, 5, node);
            if (view_mode == vm_horizon || is_sat)
            {
                umenjanetdeneg = rotate3D(umenjanetdeneg, center, ra_dec_plane.v, -ra_dec_plane.a);
                umenjanetdeneg = to_viewer_plane(umenjanetdeneg, 1);
                umenjanetdeneg = rotate3D(umenjanetdeneg, center, yaxis, -azimuth_correction);
            }
            if (view_mode == vm_horizon) umenjanetdeneg = refract_true_point(umenjanetdeneg);
            zdes = Cartesian2D(umenjanetdeneg, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (i)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (dx1 < -33554432 || dy1 < -33554432 || dx2 < -33554432 || dy2 < -33554432) continue;

                ImVec2 rastart(dx1, dy1), raend(dx2, dy2);

                // if (distance(rastart, raend) > 500) std::cout << "RA gridline from " << dx1 << "," << dy1 << " to " << dx2 << "," << dy2 << std::endl;

                if (prev_valid)
                {
                    wrapped_line(rastart, raend, j ? gc : gcb, 1.1, io);
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    // Ecliptic
    if (whereami >= 0 && (cels[whereami]->typeclass() == class_planet))
    {
        prev_valid = false;
        for (i=0; i<=360; i++)
        {
            Point pt = Point::from_ra_dec(fiftyseventh * i, 0, AU);
            pt = rotate3D(pt, center, here.orbital_plane.v, -here.orbital_plane.a);
            pt = to_viewer_plane(pt);
            if (view_mode == vm_horizon) pt = refract_true_point(pt);

            zdes = Cartesian2D(pt, azimuth+azimuth_correction, altitude, zoom);

            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (view_mode == vm_horizon && pt.y<0)
            {
                prev = zdes;
                prev_valid = true;
                continue;
            }

            if (i & 1)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (prev_valid)
                wrapped_line(ImVec2(dx1, dy1), ImVec2(dx2, dy2), ec, 1.1, io);
            }

            prev = zdes;
            prev_valid = true;
        }
    }
}

double sphresolution = 0.1;
bool bugged = false;

// GPU sphere impostor path (see GPU_SPHERE_RENDERING_PLAN.md). Only reached when
// ALIENORUM_GPU_SPHERES is 1, and only for non-wireframe, non-skymap draws (draw_sphere()
// keeps handling wireframe mode itself in both configurations, and vm_skymap is excluded at
// the dispatch point below) -- see the dispatch point in draw_sphere().
//
// The screen placement is derived from the object's exact camera-space position and radius,
// not from a screen-space "projected center + scalar radius" circle: that circle
// approximation only holds when the object is far enough away (or small enough on screen)
// that perspective distortion across its own silhouette is negligible, and breaks down badly
// at close range / large angular size -- e.g. a low-orbit satellite looking at a planet, where
// the true projected shape is neither centered on the projected 3D center nor circular. See
// sphere_impostor.cpp for the tangent-line bounding geometry and per-pixel ray-sphere
// intersection that replace it; this function's job is just to hand that code the object's
// exact position and radius in the same "camera space" Cartesian2D itself works in (see
// point.cpp) -- after to_viewer_plane() and the azimuth/altitude rotation, before the
// perspective divide.
int draw_sphere_gpu(CelestialObject* cel, double arad)
{
    // camera_space is the object's true physical position, used for lighting below. display_space
    // is where it actually appears once atmospheric refraction bends the light on its way to the
    // observer -- the same bending refract_true_point()/atmospheric_refraction() (planet.cpp)
    // already applies to point-rendered stars in housekeeping.cpp and to grid lines in
    // draw_ra_dec_lines(). Inserted here between the azimuth and altitude rotations, same as both
    // of those call sites, so yaxis still means "zenith" at the moment refract_true_point()
    // measures the object's true altitude off it -- Cartesian2D's own altitude rotation is the
    // step that stops yaxis meaning zenith, so refraction has to land before it. Only the position
    // is bent: basisX/basisY (orientation) and bounding_r (shape) are physical properties of the
    // object itself, not of the light path to the observer, so they stay derived from the true
    // position.
    Point cel_azrot = rotate3D(to_viewer_plane(cel->tmprel), center, yaxis, -(azimuth + azimuth_correction));
    Point camera_space = rotate3D(cel_azrot, center, xaxis, altitude);
    Point display_space = (view_mode == vm_horizon)
        ? rotate3D(refract_true_point(cel_azrot), center, xaxis, altitude)
        : camera_space;
    double R = cel->get_equatorial_radius();

    // Local-frame semi-axes (X, Y, Z -- Y is polar; Z is lon=0, the axis pointing at the host
    // planet for a tidally-locked moon; see SphereImpostorInput's own comment on axis_x/y/z).
    // Matches the CPU path's own two shaping cases exactly (visuals.cpp's CPU polygon loop,
    // the "dwh"/"obl" locals): a moon with known depth/width/height (tidally locked, generally
    // triaxial and often stretched along the planet-pointing axis) uses those directly; every
    // other object (including planets) is a plain oblate spheroid, flattened only at the poles.
    cel_obj_class cls = cel->typeclass();
    bool dwh = (cls == class_moon)
        && ((Moon*)cel)->depth > zero_isnt_really_zero
        && ((Moon*)cel)->width > zero_isnt_really_zero
        && ((Moon*)cel)->height > zero_isnt_really_zero;
    double axis_x, axis_y, axis_z;
    if (dwh)
    {
        axis_x = ((Moon*)cel)->width * 0.5;
        axis_y = ((Moon*)cel)->height * 0.5;
        axis_z = ((Moon*)cel)->depth * 0.5;
    }
    else
    {
        axis_x = axis_z = R;
        axis_y = R * (1.0 - cel->oblateness);
    }
    double bounding_r = fmax(axis_x, fmax(axis_y, axis_z));

    if (!cel->looked_for_maps)
    {
        cel->looked_for_maps = true;
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    // The object's local +X/+Y axes (Point::from_ra_dec's convention: x=-sin(lon)cos(lat),
    // y=sin(lat)), expressed in camera space -- i.e. run through the exact inverse of the
    // chain that places a point on the object's surface (spin, axial tilt, viewer-plane,
    // camera rotation -- see the CPU polygon loop further down in this file for the forward
    // version), applied here to the standard basis vectors rather than a surface point.
    // sphere_impostor.cpp's shader uses these (plus their cross product for local +Z) to
    // rotate a camera-space hit normal back into the object's own frame and recover lat/lon.
    auto undo_to_local = [&](Point p) -> Point
    {
        p = rotate3D(p, center, xaxis, -altitude);
        p = rotate3D(p, center, yaxis, azimuth + azimuth_correction);
        p = to_viewer_plane(p, -1);
        p = rotate3D(p, center, cel->location.equatorial_plane.v, cel->location.equatorial_plane.a);
        p = rotate3D(p, center, yaxis, cel->timeofday());
        return p;
    };
    Point basisX = undo_to_local(Point(1, 0, 0));
    Point basisY = undo_to_local(Point(0, 1, 0));

    Color col = Color::color_from_magnitude_indices(4.2, cel->BV_color);
    RGB3Byte rgb = Color::rgb_from_color(col, -1);
    // Redlight (night-vision) mode is applied once, in the shader, after lighting/texturing --
    // applying it here too would double it up for the untextured fallback case.
    ImU32 solid_color = IM_COL32(rgb.r, rgb.g, rgb.b, 255);

    // Gas giants (Jupiter etc.) get their texture into cloud_map, never surf_map -- rocky
    // bodies (Earth, Moon, Io) use surf_map. Matches the CPU path's own priority.
    Map *day_map = cel->cloud_map ? cel->cloud_map : cel->surf_map;

    // Bump mapping (see sphere_impostor.cpp's fragment shader for the actual perturbation) --
    // matches the CPU path's own gate for whether bump data is worth reading at all
    // (visuals.cpp's CPU polygon loop: "bs = (cls==class_planet||cls==class_moon) ?
    // estimate_bump_scale() : 0", then only calls elevation_at() if map && bs).
    //
    // bump_strength is divided by the object's own estimate_bump_scale() -- the same value
    // that was multiplied in when bump_data was first loaded (see Map::load_from_jpeg/_png's
    // "as_bump" branch), i.e. the actual amplitude convention this specific object's elevation
    // data was baked with -- rather than by its physical radius. A version of this that divided
    // by radius alone left estimate_bump_scale()'s *other* factor -- surface_pressure, via
    // "0.001*radius*(surface_pressure?log(surface_pressure):1)/log(20)" -- completely
    // uncancelled: an atmosphere-bearing world like Earth gets a characteristic elevation range
    // roughly 11x its radius-only share compared to an airless one like the Moon, by that
    // formula alone, so identical strength read as tastefully craggy on the Moon (tuned against
    // it) but overdone on Earth/Mars.
    //
    // Switching straight to "divide by bump_scale" fixed *that* but broke the Moon instead, for
    // a units reason: bump_scale itself (~580m for the Moon) is roughly 3000x smaller than
    // radius (~1.74e6m), so the same kBumpStrength constant divided by the much smaller number
    // came out ~3000x stronger overall -- bug: a fuzzy, cauliflower-like noise blanketing the
    // *entire* disc, not just a rough terminator. kBumpStrength itself has to be rescaled to
    // compensate, calibrated so an airless body lands at the exact same absolute strength the
    // radius-normalized version did (since for an airless body, surface_pressure is 0 and
    // estimate_bump_scale() reduces to exactly 0.001*radius/log(20) -- i.e.
    // bump_scale/radius==0.001/log(20) for *any* airless body, independent of its actual size,
    // which is what makes a single fixed rescale factor work here at all). Atmosphere-bearing
    // bodies then land proportionally below that fixed point, by exactly how much bigger their
    // own bump_scale/radius ratio is -- which was the actual goal.
    bool bump_eligible = (cls == class_planet || cls == class_moon) && day_map && day_map->has_bump_data();
    double bump_scale = bump_eligible ? ((Planet*)cel)->estimate_bump_scale() : 0.0;
    const double kBumpStrength = 4.0 * 0.001 / log(20.0);

    // Lighting: matches the CPU path's own Lambertian day/night blend (see the "self_luminous"/
    // "daylight" logic further down in this file, in the CPU polygon-shading loop).
    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);

    Point light_dir(0, 0, 1);
    if (!self_luminous)
    {
        Point light_camera_space = rotate3D(
            rotate3D(to_viewer_plane(lightcen->tmprel), center, yaxis, -(azimuth + azimuth_correction)),
            center, xaxis, altitude);
        light_dir = light_camera_space - camera_space;
        double mag = light_dir.magnitude();
        if (mag > 0) light_dir = light_dir * (1.0 / mag);
    }

    Color daylight = Color::color_from_magnitude_indices(0, lightcen->BV_color);
    double dmax = fmax(fmax(daylight.red, daylight.green), daylight.blue);
    if (dmax > 0)
    {
        daylight.red /= dmax; daylight.green /= dmax; daylight.blue /= dmax;
    }
    // Compensate for the eye's white balance adjustment (matches the CPU path exactly).
    daylight.red = pow(daylight.red, 0.333);
    daylight.green = pow(daylight.green, 0.333);
    daylight.blue = pow(daylight.blue, 0.333);

    SphereImpostorInput in;
    in.cx = display_space.x; in.cy = display_space.y; in.cz = display_space.z; in.r = bounding_r;
    in.axis_x = axis_x; in.axis_y = axis_y; in.axis_z = axis_z;
    in.basisX[0] = basisX.x; in.basisX[1] = basisX.y; in.basisX[2] = basisX.z;
    in.basisY[0] = basisY.x; in.basisY[1] = basisY.y; in.basisY[2] = basisY.z;
    in.day_map_texture = gputex_for(day_map);
    in.night_map_texture = gputex_for(cel->night_map);
    in.bump_map_texture = bump_eligible ? gputex_bump_for(day_map) : 0;
    in.bump_strength = (bump_eligible && in.bump_map_texture && bump_scale > 0) ? (kBumpStrength / bump_scale) : 0.0;
    in.fallback_color = solid_color;
    in.light_dir[0] = light_dir.x; in.light_dir[1] = light_dir.y; in.light_dir[2] = light_dir.z;
    in.daylight_tint[0] = daylight.red; in.daylight_tint[1] = daylight.green; in.daylight_tint[2] = daylight.blue;
    in.self_luminous = self_luminous;
    in.night_illum = cel->night_map ? 0.0 : starlight;
    in.redlight_mode = redlight_mode;

    // Matches the CPU path's sky_grad blend (see the "if (view_mode == vm_horizon)" block
    // further down in this file): in horizon mode, standing on a body with an atmosphere, the
    // sky glows near the horizon and fades with altitude above it -- read the reference
    // ("at horizon", undecayed) entry straight out of the same sky_grad map draw_sky_gradient()
    // already populates once per frame (rbegin() is the highest key, i.e. the first-computed,
    // least-decayed row -- see that function), and let the shader reproduce the same per-row
    // exponential falloff (its fixed 0.999/0.9995/0.9999 factors) analytically from there,
    // rather than re-deriving the underlying atmosphere-color computation here.
    in.apply_sky_blend = false;
    if (view_mode == vm_horizon && !sky_grad.empty())
    {
        auto it = sky_grad.rbegin();
        in.sky_horizon_y = (double)it->first;
        in.sky_color[0] = it->second.r / 255.0;
        in.sky_color[1] = it->second.g / 255.0;
        in.sky_color[2] = it->second.b / 255.0;
        in.apply_sky_blend = true;
    }

    double xmin, ymin, xmax, ymax;
    bool ok = queue_sphere_impostor(in, zoom, dispcx, dispcy, &xmin, &ymin, &xmax, &ymax);
    if (!ok) return 0;

    cel->drawnxmin = xmin;
    cel->drawnxmax = xmax;
    cel->drawnymin = ymin;
    cel->drawnymax = ymax;

    ImGuiIO& io = ImGui::GetIO();
    if (xmax > 0 && xmin < io.DisplaySize.x && ymax > 0 && ymin < io.DisplaySize.y)
        cel->onscreen = true;

    return fmax(xmax - xmin, ymax - ymin) / 2;
}

// GPU ring impostor path -- companion to draw_sphere_gpu() above, called from the "// Rings"
// block further down in draw_sphere() whenever that same call is using the GPU disc path (see
// sphere_impostor.cpp's "Ring impostor" section for why this exists and how it replicates the
// CPU ring code's occlusion/shadow logic analytically instead of via a polygon mesh). Mirrors
// draw_sphere_gpu()'s own structure: recomputes the object's camera-space position and basis
// independently rather than receiving them from the caller, since it's meant to be a
// self-contained drop-in the same way draw_sphere_gpu() is.
void draw_ring_gpu(CelestialObject* cel)
{
    Planet *pl = (Planet*)cel;
    // display_space vs camera_space: see draw_sphere_gpu()'s own comment just above this
    // function -- same refraction treatment, same reason light_dir below stays on camera_space.
    Point cel_azrot = rotate3D(to_viewer_plane(cel->tmprel), center, yaxis, -(azimuth + azimuth_correction));
    Point camera_space = rotate3D(cel_azrot, center, xaxis, altitude);
    Point display_space = (view_mode == vm_horizon)
        ? rotate3D(refract_true_point(cel_azrot), center, xaxis, altitude)
        : camera_space;
    double R = cel->get_equatorial_radius();

    // Ring plane normal = the object's local +Y (polar) axis, rotated forward into camera
    // space -- the *same* forward chain camera_space itself uses just above (tilt, then
    // to_viewer_plane, then camera azimuth/altitude), applied to a direction instead of a
    // position (so the translation step, "+= cel->tmprel", is correctly skipped -- directions
    // aren't translated). No spin term: the CPU ring code never rotates ring geometry by
    // timeofday() at all (rings don't spin with the planet -- see the CPU ring loop's `dust`
    // computation further down, which only tilts by equatorial_plane).
    //
    // An earlier version of this function used draw_sphere_gpu()'s "undo_to_local" helper
    // instead (minus its spin step) -- wrong, and not just because of the spin term. That
    // helper computes something genuinely different: applying the *inverse*-ordered chain to
    // a standard basis vector e_i returns R^-1*e_i, i.e. row i of the forward rotation matrix
    // R -- correct for its actual purpose (the sphere fragment shader reconstructs R^-1*n via
    // n.x*basisX + n.y*basisY + n.z*basisZ, which only works out to R^-1*n because each basis
    // vector is a *row* of R used as a *column* of that reconstruction -- a row/column
    // transpose identity, not a literal "axis expressed in camera space"). What this function
    // actually requires is a genuine forward transform, R*(0,1,0) -- a different vector from
    // R^-1*(0,1,0) whenever R isn't symmetric, which is generally the case. Using the inverse
    // version here produced a ring plane that visibly wobbled with camera azimuth/altitude
    // (bug: rings misaligned with the visible disc, plane appearing to flip depending on
    // viewing angle) since R^-1*(0,1,0) has no reason to track the camera's own orientation
    // the way R*(0,1,0) correctly does.
    Point normal = rotate3D(
        rotate3D(
            to_viewer_plane(rotate3D(Point(0, 1, 0), center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a)),
            center, yaxis, -(azimuth + azimuth_correction)),
        center, xaxis, altitude);

    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);
    Point light_dir(0, 0, 1);
    if (!self_luminous)
    {
        Point light_camera_space = rotate3D(
            rotate3D(to_viewer_plane(lightcen->tmprel), center, yaxis, -(azimuth + azimuth_correction)),
            center, xaxis, altitude);
        light_dir = light_camera_space - camera_space;
        double mag = light_dir.magnitude();
        if (mag > 0) light_dir = light_dir * (1.0 / mag);
    }

    RingImpostorInput in;
    in.cx = display_space.x; in.cy = display_space.y; in.cz = display_space.z;
    in.inner_r = R; in.outer_r = pl->ring_radius;
    in.normal[0] = normal.x; in.normal[1] = normal.y; in.normal[2] = normal.z;
    in.ring_map_texture = gputex_for(cel->ring_map);
    in.ringx_map_texture = gputex_for(cel->ringx_map);
    in.fallback_color = IM_COL32(225, 208, 192, 255);   // matches the CPU path's default rgb
    in.light_dir[0] = light_dir.x; in.light_dir[1] = light_dir.y; in.light_dir[2] = light_dir.z;
    in.self_luminous = self_luminous;
    in.amt_lit = pl->amt_lit;
    in.redlight_mode = redlight_mode;

    queue_ring_impostor(in, zoom, dispcx, dispcy);
}

int draw_sphere(CelestialObject* cel, double arad)
{
    if (cel->seqno == whereami) return 0;
    double d = cel->tmprel.magnitude(), horizon_angle, elevation = 0;
    cel_obj_class cls = cel->typeclass();

    if (d > light_year*zoom) return 0;

    double bs = 0;
    if (cls == class_planet || cls == class_moon)
    {
        bs = (((Planet*)cel)->surf_map && ((Planet*)cel)->surf_map->has_bump_data())
            ? ((Planet*)cel)->estimate_bump_scale() : 0;
    }

    ImGuiIO& io = ImGui::GetIO();

    if ((d < cel->volumetric_mean_radius || (cls == class_satellite && d < 100)))
    {
        if (velocity.magnitude() && took_off_from != cel->seqno)
        {
            double d1 = (cel->tmprel + velocity).magnitude();
            if (d1 > d)
            {
                if (cls == class_star)
                {
                    whereami = selected = trackidx = -1;
                    here = cels[0]->location;
                    here.local_position.y -= AU;
                    velocity = Point(0,0,0);
                    memset( &cels[1], 0, MAX_CELOBJS-2);
                    return 0;
                }
                else if (cls == class_satellite)
                {
                    whereami = cel->seqno;
                    velocity = Point(0,0,0);
                    return 0;
                }
                else
                {
                    here.system_center = cel->location.system_center;
                    here.equatorial_plane = cel->location.equatorial_plane;
                    viewer_lon = cel->RA_as_radians(here, cel->timeofday()) - _pi;
                    viewer_lat = -cel->Decl_as_radians(here);
                    whereami = cel->seqno;
                    velocity = Point(0,0,0);
                    view_mode = vm_horizon;
                    altitude = 0;
                    trackidx = -1;
                }
            }
        }
    }
    else if (tookoff_countdown)
    {
        tookoff_countdown--;
        if (!tookoff_countdown) took_off_from = -1;
    }

    cel->drawnxmin = cel->drawnxmax = cel->drawnx;
    cel->drawnymin = cel->drawnymax = cel->drawny;
    if (sphresolution < 0.001/sphere_quality) sphresolution = 0.001/sphere_quality;
    bool wireframe = dragging || !cel->onscreen || d < cel->volumetric_mean_radius;
    if (whereami<0 || cels[whereami]->type != artificial) cel->onscreen = false;

    bool use_gpu_disc = false, use_gpu_ring = false;
#if ALIENORUM_GPU_SPHERES
    // vm_skymap isn't a pinhole camera (see Cartesian2D in point.cpp), so the camera-space
    // math draw_sphere_gpu() relies on doesn't apply there; fall through to the CPU path.
    use_gpu_disc = (!wireframe && view_mode != vm_skymap);

    // Deliberately its own condition, not just "use_gpu_disc" -- independent of
    // cel->onscreen and the close-range "d < volumetric_mean_radius" check baked into
    // `wireframe`. Both of those describe the *disc's* own state (is the disc's own small
    // bounding box on screen; is the camera essentially at the planet's surface), neither of
    // which says anything about whether the ring -- routinely 2-2.5x larger than the planet
    // itself -- is visible. Coupling ring rendering to the disc's wireframe/onscreen state
    // produced a feedback flicker: with the planet off-screen but the ring still (correctly)
    // extending into view, onscreen reads false -> GPU ring path off -> the CPU wireframe
    // ring-line code runs instead, whose own onscreen check is far more generous (any ring
    // vertex landing in the visible screen region, not just the disc's own bbox) -> flips
    // onscreen back true next frame -> GPU path back on -> the disc's own onscreen check (now
    // looking at the disc's narrow bbox again) fails again -> flips back off -> repeat. The
    // ring has its own independent visibility test in queue_ring_impostor(); it doesn't have
    // to borrow the disc's.
    use_gpu_ring = (!dragging && view_mode != vm_skymap);
#endif
    int i, j, l, m, lastm, n, result=0;
    Cartesian2D prev, zdes;
    std::vector<ImVec2> todraw;
    std::vector<Point> tdland;
    std::vector<double> tdlat, tdlon;
    std::vector<bool> tdvalid;
    ImU32 gc = rgba_apply_redlight(IM_COL32(176, 170, 164, 255));
    ImU32 gm = rgba_apply_redlight(IM_COL32(  0, 255,   0, 255));
    Color daylight = Color::color_from_magnitude_indices(0, cel->get_light_center()->BV_color);
    double f = fmax(fmax(daylight.red, daylight.green), daylight.blue);
    daylight.red /= f;
    daylight.green /= f;
    daylight.blue /= f;

    // Compensate for the eye's white balance adjustment
    daylight.red = pow(daylight.red, 0.333);
    daylight.green = pow(daylight.green, 0.333);
    daylight.blue = pow(daylight.blue, 0.333);

    if (wireframe)
    {
        Color wcol = Color::color_from_magnitude_indices(0, cel->BV_color);
        RGB3Byte wrgb = Color::rgb_from_color(wcol, -1);
        gc = rgba_apply_redlight(IM_COL32(wrgb.r, wrgb.g, wrgb.b, 255));
    }

    bool prev_valid = false;
    bool dwh = false;

    if (cls == class_moon)
        dwh = (((Moon*)cel)->depth > zero_isnt_really_zero
            && ((Moon*)cel)->width > zero_isnt_really_zero
            && ((Moon*)cel)->height > zero_isnt_really_zero);

    double equatorial_radius, theta, vtheta, cos_theta, cos_vtheta, is_day, is_night;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * .5;
    else
        equatorial_radius = cel->get_equatorial_radius();

    double lat, lon, z_cutoff = d + equatorial_radius * 0.2, obl = 1.0 - cel->oblateness;

    if (!wireframe && !cel->looked_for_maps)
    {
        cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    horizon_angle = cel->get_horizon_angle();
    bool worth_using_map = (bloomrad_cache[cel->seqno] > 5);                // Only if the disc will be big enouh to see any details.

    int i360, latmin = 1e9, latmax = -1e9, lonmin = 1e9, lonmax = -1e9, nstep = wireframe ? 10 : 5;
    for (i=0; i<=360; i+=nstep)
    {
        i360 = (i>=180 && lonmin<=0 && lonmax<180) ? (i - 360) : i;           // Catch if visible longitudes wrap around zero.
        prev_valid = false;
        for (j=-90; j<=90; j+=nstep)
        {
            Point cursor = Point::from_ra_dec(fiftyseventh * i, fiftyseventh * j, dwh ? 1 : equatorial_radius, 0);

            if (dwh)
            {
                cursor.x *= ((Moon*)cel)->width * .5;
                cursor.y *= ((Moon*)cel)->height * .5;
                cursor.z *= ((Moon*)cel)->depth * .5;
            }
            else cursor.y *= obl;
            cursor = rotate3D(cursor, center, yaxis, -cel->timeofday());

            cursor = rotate3D(cursor, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor += cel->tmprel;
            vtheta = fabs(fmod(find_3D_angle(cursor, center, cel->tmprel), _pi*2));
            cursor = to_viewer_plane(cursor);
            if (cursor.magnitude() > z_cutoff)
            {
                prev_valid = false;
                continue;
            }
            if (view_mode == vm_horizon) cursor = refract_true_point(cursor);
            zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (vtheta < horizon_angle)
            {
                if (i >= 180) i360 = i;

                if (i360 < lonmin) lonmin = i360;
                if (i360 > lonmax) lonmax = i360;

                if (j < latmin) latmin = j;
                if (j > latmax) latmax = j;
            }

            if (wireframe && (j > -80))
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (prev_valid)
                {
                    wrapped_line(ImVec2(dx1, dy1), ImVec2(dx2, dy2), i?gc:gm, 1, io);
                    if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1) cel->onscreen = true;
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    Map *map = nullptr, *nmap = nullptr;
    if (cel->cloud_map) map = cel->cloud_map;
    else if (cel->surf_map) map = cel->surf_map;
    if (cel->night_map) nmap = cel->night_map;
    double night_illum = nmap ? 0 : starlight;
    RGB3Byte rgb = Color::rgb_from_color(Color::color_from_magnitude_indices(4.2, cel->BV_color), -1), nrgb = {0,0,0};
    Point cursor, land;
    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);
    ImU32 imcol;

    auto sphere_began = std::chrono::high_resolution_clock::now();
    double step = wireframe
            ? (fiftyseventh*15)
            : ( (worth_using_map && (cel->surf_map || cel->cloud_map))
                ? fmax(fmin(_pi*sphresolution/arad*fiftyseventh, fiftyseventh*15), fiftyseventh*0.2)
                : fiftyseventh * 3
              ),
        stepcoslat, invlaststepcoslat = 1.0 / step;
    int perline=0, dx1, dy1, dx2, dy2;
    double polyr, polyg, polyb, lum, lum1;
    l = 0;

    bool lonmin_crosses_zero = (lonmin <= 0 && lonmax < 180), filter_longitudes = ((lonmax - lonmin) <= 180);
    double latmin_rad = fiftyseventh * (latmin-5) - step, latmax_rad = fiftyseventh * (latmax+5) + step,
        lonmin_rad = fiftyseventh * lonmin, lonmax_rad = fiftyseventh * lonmax;
    double lon360;
    if (use_gpu_disc)
    {
        int r = draw_sphere_gpu(cel, arad);
        if (!r) return 0;
        result = r;
    }
    else
    for (lat=-half_pi; lat <= half_pi; lat+=step)
    {
        if (lat < latmin_rad || lat > latmax_rad) continue;
        prev_valid = false;
        n = 0;
        stepcoslat = step / (cos(lat) + 0.1);
        for (lon=0; lon<=_pi*2; lon+=stepcoslat)
        {
            lon360 = lonmin_crosses_zero ? (lonmin_rad - _pi*2) : lonmin_rad;
            if (filter_longitudes && (lon360 < (lonmin_rad - stepcoslat) || lon360 > (lonmax_rad + stepcoslat))) continue;
            n++;
            elevation = (map && bs) ? (map->elevation_at(lat, lon)) : 0;
            land = Point::from_ra_dec(lon+_pi, lat, dwh ? 1 : (equatorial_radius + elevation), 0);

            if (dwh)
            {
                land.x *= ((Moon*)cel)->width  * .5;
                land.y *= ((Moon*)cel)->height * .5;
                land.z *= ((Moon*)cel)->depth  * .5;
                if (elevation) land.scale(land.magnitude()+elevation);          // TODO: This is a costly calculation - possible to streamline it?
            }
            else land.y *= obl;
            land = rotate3D(land, center, yaxis, -cel->timeofday());

            land = rotate3D(land, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor = land + cel->tmprel;
            cursor = to_viewer_plane(cursor);
            if (cursor.magnitude() > z_cutoff)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                tdland.push_back(center);
                tdlat.push_back(lat);
                tdlon.push_back(lon);
                l++;
                prev_valid = false;
                continue;
            }

            if (view_mode == vm_horizon) cursor = refract_true_point(cursor);
            zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                tdland.push_back(center);
                tdlat.push_back(lat);
                tdlon.push_back(lon);
                l++;
                prev_valid = false;
                continue;
            }

            if (lon)
            {
                land += cel->location.local_position;
                vtheta = fabs(fmod(find_3D_angle(land, here.local_position, cel->location.local_position), _pi*2));
                if (!wireframe && vtheta > horizon_angle)
                {
                    todraw.push_back(ImVec2(-1e13, -2e13));
                    tdvalid.push_back(false);
                    tdland.push_back(center);
                    tdlat.push_back(lat);
                    tdlon.push_back(lon);
                }
                else
                {
                    dx1 = dispcx + zdes.x * dispcx;
                    dy1 = dispcy + zdes.y * dispcx;
                    dx2 = dispcx + prev.x * dispcx;
                    dy2 = dispcy + prev.y * dispcx;

                    if (view_mode == vm_skymap)
                    {
                        if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                        if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                        if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                        if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                    }

                    double yd = (dy1 - cel->drawny);
                    if (yd > result) result = yd;

                    ImVec2 v = ImVec2(dx1, dy1);
                    if (prev_valid)
                    {
                        if (wireframe) wrapped_line(v, ImVec2(dx2, dy2), gc, 1, io);
                        if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1)
                        {
                            cel->onscreen = true;
                            if (dx1 < cel->drawnxmin) cel->drawnxmin = dx1;
                            if (dx1 > cel->drawnxmax) cel->drawnxmax = dx1;
                            if (dy1 < cel->drawnymin) cel->drawnymin = dy1;
                            if (dy1 > cel->drawnymax) cel->drawnymax = dy1;
                        }
                    }

                    todraw.push_back(v);
                    tdvalid.push_back(true);
                    // Also store 3D coordinates of each vertex.
                    tdland.push_back(land);
                    tdlat.push_back(lat);
                    tdlon.push_back(lon);

                    if (!wireframe && (lat>-half_pi) && !dragging && perline)
                    {
                        m = l - n - perline + round(lon*invlaststepcoslat) + 2;
                        if (m > 1 && tdvalid[l-1] && m < l && tdvalid[m] && tdvalid[m-1])
                        {
                            if (self_luminous)
                            {
                                cos_vtheta = cos(vtheta);
                                is_day = fmin(1, pow(cos_vtheta, 0.333));
                            }
                            else
                            {
                                // theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), _pi);

                                // Shade based on the normal of the 3D coordinates of the polygon vertices instead of angle to sun and cel center.
                                Point normal = compute_normal(land, tdland[l-1], tdland[m]) + compute_normal(tdland[l-1], tdland[m-1], tdland[m]);
                                theta = fmod(find_3D_angle(cel->location.local_position - normal, lightcen->location.local_position, cel->location.local_position), _pi);
                                if (fabs(theta) < half_pi)
                                {
                                    cos_theta = cos(theta);
                                    is_day = fmin(1, pow(cos_theta, 0.333) + night_illum);
                                }
                                else is_day = night_illum;
                            }

                            ImVec2 points[4];
                            points[0] = v;
                            points[1] = todraw[l-1];
                            points[2] = todraw[m-1];
                            points[3] = todraw[m];
                            double maplat = 0.25 * (lat + tdlat[l-1] + tdlat[m-1] + tdlat[m]);
                            double maplon = interpolate_angles(
                                interpolate_angles(lon, tdlon[l-1]),
                                interpolate_angles(tdlon[m-1], tdlon[m]));
                            if (map && is_day && worth_using_map) rgb = map->color_at(maplat, maplon-_pi);

                            if (view_mode == vm_skymap)
                            {
                                if (points[1].x > points[0].x + 1.9 * dispcx) points[0].x += dispcx*2;
                                if (points[2].x > points[0].x + 1.9 * dispcx) points[0].x += dispcx*2;
                                if (points[3].x > points[0].x + 1.9 * dispcx) points[0].x += dispcx*2;
                                if (points[0].x > points[1].x + 1.9 * dispcx) points[1].x += dispcx*2;
                                if (points[0].x > points[2].x + 1.9 * dispcx) points[2].x += dispcx*2;
                                if (points[0].x > points[3].x + 1.9 * dispcx) points[3].x += dispcx*2;

                                if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                                if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                            }

                            RGB3Byte rgblit = rgb;
                            rgblit.r *= daylight.red;
                            rgblit.g *= daylight.green;
                            rgblit.b *= daylight.blue;

                            if (nmap && worth_using_map)
                            {
                                is_night = 1.0 - is_day;
                                if (is_night)
                                {
                                    nrgb = nmap->color_at(maplat, maplon-_pi);

                                    polyr = is_day*rgblit.r + is_night*nrgb.r;
                                    polyg = is_day*rgblit.g + is_night*nrgb.g;
                                    polyb = is_day*rgblit.b + is_night*nrgb.b;
                                }
                            }
                            else
                            {
                                polyr = is_day*rgblit.r;
                                polyg = is_day*rgblit.g;
                                polyb = is_day*rgblit.b;
                            }

                            if (view_mode == vm_horizon)
                            {
                                if (sky_grad.find(dy1) != sky_grad.end())
                                {
                                    lum = sky_grad[dy1].luminance() * 0.003921569;
                                    lum1 = 1.0 - lum;
                                    polyr = fmin(255, lum1 * polyr + sky_grad[dy1].r);
                                    polyg = fmin(255, lum1 * polyg + sky_grad[dy1].g);
                                    polyb = fmin(255, lum1 * polyb + sky_grad[dy1].b);
                                }
                            }

                            imcol = rgba_apply_redlight(IM_COL32(polyr, polyg, polyb, 255));

                            ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, imcol);
                            if (m > lastm+1 && tdvalid[m-2])
                            {
                                points[2] = todraw[m-2];
                                points[3] = todraw[m-1];
                                ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, imcol);
                            }
                            cel->onscreen = true;
                        }

                        lastm = m;
                    } // if not wireframe
                } // if within horizon angle
                l++;
            } // if lon

            prev = zdes;
            prev_valid = true;
        } // for lon

        perline = max(0, n);
        invlaststepcoslat = 1.0/stepcoslat;
    } // for lat

    if (!wireframe && !dragging && (l > perline*2))
    {
        auto points = std::make_unique<ImVec2[]>(perline);
        n = 0;
        for (i=0; i<perline; i++)
        {
            j = l-perline-i-1;
            if (!tdvalid[j]) continue;
            points[n++] = todraw[j];
        }

        // Certain vars are left over from the last iteration; assume values are still good.
        ImU32 imcol = rgba_apply_redlight(IM_COL32(is_day*rgb.r, is_day*rgb.g, is_day*rgb.b, 255));
        ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points.get(), n, imcol);
    }

    // Rings
    if (cls == class_planet && ((Planet*)cel)->ring_radius)
    {
#if ALIENORUM_GPU_SPHERES
        // Analytic ray/plane impostor, matching the disc's own GPU treatment -- see
        // draw_ring_gpu() and sphere_impostor.cpp's "Ring impostor" section. Gated on
        // use_gpu_ring, not use_gpu_disc -- see that variable's own comment for why the two
        // have to be independent (skymap draws still keep the CPU polygon-mesh ring below
        // unconditionally, same as the disc does, since use_gpu_ring is false there too).
        if (use_gpu_ring)
        {
            draw_ring_gpu(cel);
        }
        else
#endif
        {
        std::vector<ImVec2> todrawr;
        std::vector<bool> tdvalidr;
        l = 0;
        Point dust;
        Planet *pl = (Planet*)cel;
        double ringsize = pl->ring_radius - equatorial_radius, ringd;
        if (ringsize <= 0)
        {
            std::cerr << "ERROR: Ring size less than equatorial radius for " << cel->name << std::endl << std::flush;
            throw 0xbadda7a;
        }

        // n (angular subdivisions) used to be implicitly bounded by `step`, which the
        // end-of-function adaptive throttle kept sane by measuring the CPU disc-mesh loop's
        // own render cost. With the disc now GPU-rendered (near-zero CPU cost) whenever
        // use_gpu_disc is true elsewhere in the app, that feedback loop no longer has anything
        // to react to on those frames, so `step` can drift far finer than the ring actually
        // requires on screen -- round(_pi*2/step)*13 was observed reaching ~9800, producing
        // 150,000+ AddConvexPolyFilled calls in a single frame. This CPU path is now only
        // reached while dragging or in skymap mode (see use_gpu_ring), but the cap is cheap
        // and correct there too, so it stays rather than special-casing it back out. Cap n by
        // the ring's actual apparent
        // size (arad, independent of the runaway step) instead: target roughly one quad per
        // 2px along the outer circumference. arad is a slope (~tan(angular_radius)*zoom), not
        // a pixel count -- dispcx converts it to one, same as the disc placement math further
        // up (e.g. "dx1 = dispcx + zdes.x * dispcx").
        double ring_outer_px = arad * dispcx * pl->ring_radius / equatorial_radius;
        int n_cap = (int)fmax(24, fmin(3000, _pi * ring_outer_px));
        n = fmin((double)n_cap, round(_pi*2/step) * 13);
        m = fmax(4, fmin(result, round(_pi*2/step)/2));
        double step1 = (double)ringsize / m, step2 = _pi*2/n;

        Map *rmap = cel->ring_map, *rxmap = cel->ringx_map;
        rgb = {225, 208, 192};
        for (ringd = equatorial_radius; ringd <= pl->ring_radius; ringd += step1)
        {
            double xmapd = (double)(ringd - equatorial_radius) * _pi*2 / ringsize;
            double ring_opacity = rxmap ? (255.0 * (1.0-pow((double)rxmap->color_at(0, xmapd).g/255, gossamer_rings))) : 0.5;
            if (rmap) rgb = rmap->color_at(0, xmapd);
            double lonlim = _pi*2+0.5*step2;

            for (lon=0; lon<lonlim; lon+=step2)
            {
                dust = Point::from_ra_dec(lon+_pi, 0, ringd, 0);

                dust = rotate3D(dust, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
                dust += cel->tmprel;
                Point yardstick = center - dust;
                yardstick.scale(equatorial_radius*2);
                yardstick += dust;
                cursor = to_viewer_plane(dust);

                if (cursor.magnitude() > z_cutoff && cel->tmprel.get_distance_to_line(dust, yardstick) < equatorial_radius )
                {
                    todrawr.push_back(ImVec2(-1e29,-1e53));
                    tdvalidr.push_back(false);
                    l++;
                    prev_valid = false;
                    prev = zdes;
                    continue;
                }

                if (view_mode == vm_horizon) cursor = refract_true_point(cursor);
                zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
                if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
                {
                    todrawr.push_back(ImVec2(-1e29,-1e9));
                    tdvalidr.push_back(false);
                    l++;
                    prev_valid = false;
                    prev = zdes;
                    continue;
                }

                dx1 = dispcx + zdes.x * dispcx;
                dy1 = dispcy + zdes.y * dispcx;
                dx2 = dispcx + prev.x * dispcx;
                dy2 = dispcy + prev.y * dispcx;

                if (view_mode == vm_skymap)
                {
                    if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                    if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                    if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                    if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                }

                ImVec2 v = ImVec2(dx1, dy1);
                if (lon)
                {
                    if (prev_valid && wireframe)
                    {
                        wrapped_line(v, ImVec2(dx2, dy2), gc, 1, io);
                        if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1) cel->onscreen = true;
                    }

                    if (prev_valid && !wireframe && !dragging)
                    {
                        m = l - n - 1;
                        if (m>=1 && tdvalidr[l-1] && tdvalidr[m] && tdvalidr[m-1])
                        {
                            is_day = (cel->tmprel.get_distance_to_line(dust, lightcen->tmprel) < equatorial_radius)
                                ? 0 : (0.15 + 0.44 * pl->amt_lit);

                            ImVec2 points[4];
                            points[0] = v;
                            points[1] = todrawr[l-1];
                            points[2] = todrawr[m-1];
                            points[3] = todrawr[m];
                            double polycx = 0.25 * (points[0].x + points[1].x + points[2].x + points[3].x),
                                   polycy = 0.25 * (points[0].y + points[1].y + points[2].y + points[3].y);
                            for (i=0; i<4; i++)
                            {
                                points[i].x += sgn(points[i].x-polycx);
                                points[i].y += sgn(points[i].y-polycy);
                            }

                            ImU32 imcol = rgba_apply_redlight(IM_COL32(rgb.r*is_day, rgb.g*is_day, rgb.b*is_day, ring_opacity));
                            ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, imcol);

                            cel->onscreen = true;
                        } // if all vertices valid
                    } // if ready draw filled poly
                } // if lon

                todrawr.push_back(v);
                tdvalidr.push_back(true);
                prev_valid = true;
                prev = zdes;
                l++;
            }
        }
        } // else (CPU ring path)
    }

    auto sphere_finished = std::chrono::high_resolution_clock::now();
    auto sphere_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(sphere_finished - sphere_began);

    if (!wireframe && cel->onscreen)
    {
        if (sphere_elapsed.count() >= (1.3e5*sphere_quality))
        {
            if (sphresolution < 0.2/sphere_quality)
            {
                sphresolution *= 1.3;
                if (sphere_elapsed.count() >= (3e5*sphere_quality)) sphresolution *= 2;
            }
            else if (!bugged)
            {
                std::cout << "System too slow! Texture rendering may be terrible." << std::endl;
                bugged = true;
            }
        }
        else if (sphere_elapsed.count() < (8e4*sphere_quality) && cel->type != star) sphresolution *= 0.9;
    }

    return result;
}

// Deterministic per-streak jitter. Keyed off the streak index rather than the clock so the
// corona keeps the same shape from frame to frame instead of shimmering.
static double flare_hash(int k)
{
    double s = sin(k * 12.9898) * 43758.5453;
    return s - floor(s);
}

// ImGui has no radial gradient, and stacking translucent discs leaves a hard edge at every
// disc, which is what made the halo read as a set of concentric rings. Drawing vertex-
// coloured annuli hands the falloff to the hardware interpolator, so it comes out smooth.
static void draw_radial_glow(ImVec2 c, double r_in, double r_out, RGB3Byte rgb,
    double peak_alpha, double falloff)
{
    if (r_out <= r_in || peak_alpha < 1.0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const int nring = 16;
    int nseg = (int)fmin(64.0, fmax(24.0, r_out * 0.5));

    dl->AddCircleFilled(c, r_in, rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, (int)peak_alpha)), 0);

    dl->PrimReserve(nring*nseg*6, nring*nseg*4);
    for (int i=0; i<nring; i++)
    {
        double f0 = (double)i/nring, f1 = (double)(i+1)/nring;
        double ra = r_in + (r_out-r_in)*f0, rb = r_in + (r_out-r_in)*f1;
        ImU32 ca = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, (int)(peak_alpha*pow(1.0-f0, falloff))));
        ImU32 cb = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, (int)(peak_alpha*pow(1.0-f1, falloff))));
        for (int s=0; s<nseg; s++)
        {
            double t0 = s*(_pi*2.0/nseg), t1 = (s+1)*(_pi*2.0/nseg);
            double x0 = cos(t0), y0 = sin(t0), x1 = cos(t1), y1 = sin(t1);
            unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(ImVec2(c.x + x0*ra, c.y + y0*ra), uv, ca);
            dl->PrimWriteVtx(ImVec2(c.x + x1*ra, c.y + y1*ra), uv, ca);
            dl->PrimWriteVtx(ImVec2(c.x + x1*rb, c.y + y1*rb), uv, cb);
            dl->PrimWriteVtx(ImVec2(c.x + x0*rb, c.y + y0*rb), uv, cb);
            dl->PrimWriteIdx((ImDrawIdx)(base+0));
            dl->PrimWriteIdx((ImDrawIdx)(base+1));
            dl->PrimWriteIdx((ImDrawIdx)(base+2));
            dl->PrimWriteIdx((ImDrawIdx)(base+0));
            dl->PrimWriteIdx((ImDrawIdx)(base+2));
            dl->PrimWriteIdx((ImDrawIdx)(base+3));
        }
    }
}

// Diffraction-style flare. A faint point source gets a four-point cross that fills out into
// a full circle of rays as it brightens. A very bright or well-resolved source (the Sun, the
// Moon) scatters its light into a hazy corona instead: sharp spikes wash out, and what is
// left is a smooth core glow frayed by fine radiating streaks.
void draw_flare(double flare, Color col, double vmag, double disc_px)
{
    if (whtbkgd) return;

    // An object viewed from zero distance (e.g. the Sun as seen from the Sun) makes
    // viewer_magnitude() divide by r*r = 0 and return -Infinity, which turns every
    // channel of col Infinite. 255/Infinity is a well-defined 0, but Infinite*0 is NaN,
    // and casting NaN to int is undefined behavior -- it does not clamp, it corrupts the
    // packed color's bits (verified: INT_MIN on this build), so a shape that size no
    // longer paints garbage over a small area, it paints garbage over most of the screen.
    if (!std::isfinite(flare) || !std::isfinite(vmag) || !std::isfinite(disc_px)
        || !std::isfinite(col.red) || !std::isfinite(col.green) || !std::isfinite(col.blue))
        return;

    double divisor = 255.0 / fmax(fmax(col.blue, col.red), col.green);
    RGB3Byte rgb;
    rgb.r = (int)(col.red * divisor);
    rgb.g = (int)(col.green* divisor);
    rgb.b = (int)(col.blue * divisor);

    // Four rays around magnitude -10 and dimmer, filling in to a full circle by the Sun.
    double fill = (vmag > -10.0) ? 0.0 : fmin(1.0, (-10.0 - vmag) / 16.0);

    // Glare scatters into a haze either because the source is overwhelmingly bright or
    // because its disc is resolved enough that it stops behaving like a point. Brightness
    // is what dominates: the Sun hazes over even when its disc is only a few pixels wide.
    // Ramps in smoothly from magnitude -1 so Venus and Jupiter pick up a slight haze while
    // the Moon and Sun saturate it.
    double glare = fmax(0.0, -1.0 - vmag);
    double haze = fmin(1.0, fmax(disc_px / 12.0, 1.0 - exp(-glare / 7.0)));

    double base_len = max_bloomrad * 1.5 + flare * 1.1;
    auto draw_streak = [&](double ang, double from_r, double to_r, double halfwidth, ImU32 c)
    {
        double dx = cos(ang), dy = sin(ang), px = -dy, py = dx;
        ImVec2 base_a(xycoord.x + dx*from_r + px*halfwidth, xycoord.y + dy*from_r + py*halfwidth);
        ImVec2 base_b(xycoord.x + dx*from_r - px*halfwidth, xycoord.y + dy*from_r - py*halfwidth);
        ImVec2 tip(xycoord.x + dx*to_r, xycoord.y + dy*to_r);
        ImGui::GetBackgroundDrawList()->AddTriangleFilled(base_a, base_b, tip, c);
    };

    if (haze > 0.01)
    {
        double halo_span = disc_px * 0.8 + base_len * 0.7;
        draw_radial_glow(xycoord, fmax(1.0, disc_px * 0.85), disc_px + halo_span, rgb,
            230.0 * haze, 2.2);

        // Fine radiating streaks. These are what keep the corona from reading as circles:
        // each one starts at the limb and runs out to its own length, so the glow frays.
        // The angle jitter is wider than one slot so streaks clump and leave gaps instead
        // of landing on an even spoke pattern.
        const int nfine = 128;
        double corona_len = base_len * (0.55 + 0.85 * haze);
        for (int k=0; k<nfine; k++)
        {
            double h1 = flare_hash(k), h2 = flare_hash(k + 977), h3 = flare_hash(k + 3121);
            int a = (int)(30.0 * haze * (0.25 + 0.75 * h2));
            if (a < 1) continue;
            ImU32 scol = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, a));
            double ang = (k + (h3 - 0.5) * 1.7) * (_pi * 2.0 / nfine);
            draw_streak(ang, disc_px * 0.7, disc_px + corona_len * (0.18 + 0.82 * pow(h1, 1.8)),
                0.6 + 1.2 * h2, scol);
        }
    }

    // Diffraction spikes. Each is a stack of triangles of growing length, so the overlap
    // piles up into a bright base that tapers toward the tip. Haze both dims these and
    // fattens them, which is what turns a hard cross into a soft blur.
    // Spikes belong to small point sources examined closely. A wide field washes them out,
    // and so does a resolved disc, so they fade in with zoom and out with haze.
    double zf = 0.1 + 0.9 * fmin(1.0, log(fmax(1.0, zoom)) / log(24.0));
    double spike_str = pow(1.0 - haze * 0.9, 1.6) * zf;
    if (spike_str > 0.02)
    {
        const int nslots = 24, nlayers = 5;
        // Off cardinal/diagonal so the four points don't look like a cross.
        const double spike_rotation = azimuth - 0.3 * altitude; // 25.0 * fiftyseventh;
        double ray_len = base_len * (1.0 - 0.5 * haze);
        double halfwidth_base = (1.7 + flare * 0.006) * (1.0 + 2.5 * haze);
        for (int k=0; k<nslots; k++)
        {
            bool primary = !(k % 6);
            double weight;
            if (primary) weight = 1.0;
            else if (!(k % 3)) weight = fill;                    // diagonals fill in first
            else weight = fmax(0.0, fill * 2.0 - 1.0);           // the rest arrive last
            if (weight < 0.01) continue;

            int a = (int)(105.0 * weight * spike_str);
            if (a < 1) continue;
            ImU32 fcol = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, a));

            // A little length variation so the corona does not look mechanically even.
            double vary = 0.78 + 0.22 * (double)((k * 7) % 5) / 4.0;
            double len = ray_len * vary * (primary ? 1.0 : 0.55);
            double halfwidth = halfwidth_base * (primary ? 1.0 : 0.7);
            double ang = k * (_pi * 2.0 / nslots) + spike_rotation;

            for (int j=1; j<=nlayers; j++)
            {
                double frac = (double)j / nlayers;
                draw_streak(ang, 0, len*frac, halfwidth * (1.0 - 0.55*frac), fcol);
            }
        }
    }
}

int draw_satellite_icon(ImVec2 xycoord, ImU32 satcol)
{
    // Satellite icons.
    ImVec2 antenna_top              = ImVec2(xycoord.x,                                             xycoord.y - antenna_height  );
    ImVec2 panel_left_stem          = ImVec2(xycoord.x - antenna_height,                            xycoord.y                   );
    ImVec2 panel_right_stem         = ImVec2(xycoord.x + antenna_height,                            xycoord.y                   );
    ImVec2 panel_left_topprox       = ImVec2(xycoord.x - antenna_height + panel_tilt,               xycoord.y - antenna_height  );
    ImVec2 panel_left_topdist       = ImVec2(xycoord.x - antenna_height + panel_tilt - panel_width, xycoord.y - antenna_height  );
    ImVec2 panel_left_botprox       = ImVec2(xycoord.x - antenna_height - panel_tilt,               xycoord.y + antenna_height  );
    ImVec2 panel_left_botdist       = ImVec2(xycoord.x - antenna_height - panel_tilt - panel_width, xycoord.y + antenna_height  );
    ImVec2 panel_right_topprox      = ImVec2(xycoord.x + antenna_height + panel_tilt,               xycoord.y - antenna_height  );
    ImVec2 panel_right_topdist      = ImVec2(xycoord.x + antenna_height + panel_tilt + panel_width, xycoord.y - antenna_height  );
    ImVec2 panel_right_botprox      = ImVec2(xycoord.x + antenna_height - panel_tilt,               xycoord.y + antenna_height  );
    ImVec2 panel_right_botdist      = ImVec2(xycoord.x + antenna_height - panel_tilt + panel_width, xycoord.y + antenna_height  );

    ImGui::GetBackgroundDrawList()->AddLine(xycoord, antenna_top, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_stem, panel_right_stem, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_topprox, panel_left_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_botdist, panel_left_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_botdist, panel_left_botprox, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_topprox, panel_left_botprox, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_topprox, panel_right_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_botdist, panel_right_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_botdist, panel_right_botprox, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_topprox, panel_right_botprox, satcol, 1);

    return antenna_height + panel_tilt + panel_width;
}

double global_magshift;
bool draw_one_object(int i)
{
    bool obj_is_localsys = (cels[i]->cenobj == mycenobj);
    if (!show_localsys && obj_is_localsys) return false;

    int j;
    cel_obj_class cls = cels[i]->typeclass();
    xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
    appmag = vmag_cache[i] - sky_mag_shift;
    double brght = pow(magnbase, -appmag);
    bloomrad = fabs(pow(brght, 0.5)*global_brightness);
    // The bloom disc saturates at max_bloomrad long before the bloom-based flare threshold
    // is met, which left the brightest planets and stars as flat blobs with nothing around
    // them. Give anything brighter than magnitude -1 a glare of its own. Keying that off
    // magnitude rather than bloomrad keeps the count bounded: bloomrad scales with
    // global_brightness, so a threshold low enough to catch Venus at default brightness
    // would flare six figures' worth of stars once brightness is turned up.
    double f_bloom = (bloomrad>1.5*max_bloomrad) ? 1.0+sqrt(bloomrad-1.5*max_bloomrad)*8 : 0;
    double f_mag = fmax(0.0, -1.0 - vmag_cache[i]) * 12.0;
    flare = fmin(max_flare, fmax(f_bloom, f_mag));
    bloomrad = fmin(max_bloomrad, bloomrad*10);
    if (cls == class_satellite)
    {
        if (!show_sats) return false;
        if (cels[i]->orbit && (cels[i]->tmprel.magnitude() > cels[i]->orbit->semimajor_axis*zoom*6))
        {
            cels[i]->drawnx = cels[i]->drawny = -1e9;
            return false;
        }

        double line_of_sight = cels[i]->orbit->center->location.local_position.get_distance_to_line(
            cels[i]->location.local_position, cels[i]->get_light_center()->location.local_position);

        ImU32 satcol = (i == selected)
            ? rgba_apply_redlight(global_style.selected_color)
            : ((line_of_sight < cels[i]->volumetric_mean_radius)
                ? rgba_apply_redlight(IM_COL32(128,  96,  64, 255))
                : (whtbkgd
                    ? rgba_apply_redlight(IM_COL32(  0,   0,   0, 255))
                    : rgba_apply_redlight(IM_COL32(255, 255, 255, 255))));

        if (show_labels || lbl_localsys || show_consln || show_grid)
        {
            bloomrad_cache[i] = bloomrad = draw_satellite_icon(xycoord, satcol);
        }
        else
        {
            ImGui::GetBackgroundDrawList()->AddCircleFilled(xycoord, 1, satcol);
            bloomrad_cache[i] = bloomrad = 1;
        }
    }
    else if (angular_radius[i]*zoom > sphere_rad_threshold)
    {
        if (flare)
        {
            Color col = Color::color_from_magnitude_indices(appmag, cels[i]->BV_color);
            draw_flare(flare, col, vmag_cache[i], angular_radius[i]*zoom*dispcx);
        }

        CelestialObject *cel = cels[i];
        bloomrad_cache[i] = bloomrad = draw_sphere(cel, angular_radius[i]*zoom);
        discinstead[i] = false;
        if (!cels[1]) return false;

        if (selected == i)
        {
            ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(global_style.selected_color), 0, 2);
        }
    }
    else
    {
        discinstead[i] = false;

        Color col = Color::color_from_magnitude_indices(appmag, cels[i]->BV_color);

        // Adjust for mesopic and scotopic color perception, e.g. dim red stars tend to look grayish.
        float effmag = vmag_cache[i] + global_magshift;
        if (effmag > 2)
        {
            double effect = fmin(1, (effmag - 2) / 8), effect1 = 1.0 - effect;
            col.red = effect*0.5*col.green + effect1*col.red;
            col.blue = effect*col.green + effect1*col.blue;
        }

        if (flare) draw_flare(flare, col, vmag_cache[i], 0);

        brght = pow(magnbase, -appmag) * global_brightness * 50;
        double circ, lbrght, lpxval, tosub, softmod = 1.0 - bloom_softness;
        // if (i == 1075) std::cout << i << ":" << cels[i]->name << ": " << brght << std::endl;
        bool first = true;
        std::vector<double> circradii, circpixvals;
        for (bloomrad = 0.5; brght > 0; bloomrad += 0.5)
        {
            if (first)
            {
                double area = _pi * bloomrad * bloomrad;
                lbrght = brght*softmod;
                lpxval = lbrght/area;
                tosub = fmin(area, lbrght);
                circradii.push_back(bloomrad);
                circpixvals.push_back(fmin(1, lpxval));
                // if (i == 1075) std::cout << " area " << area << " so " << brght << " - " << tosub << " = ";
                brght -= tosub;
                // if (i == 1075) std::cout << brght << std::endl;
                first = false;
            }
            else
            {
                circ = 2.0 * _pi * bloomrad;
                lbrght = brght*softmod;
                lpxval = lbrght/circ;
                tosub = fmin(circ, lbrght);
                circradii.push_back(bloomrad);
                circpixvals.push_back(fmin(1, lpxval));
                // if (i == 1075) std::cout << " circ " << circ << " so " << brght << " - " << tosub << " = ";
                brght -= tosub;
                // if (i == 1075) std::cout << brght << std::endl;
            }
            if (bloomrad >= max_bloomrad) break;
            if (lpxval < 0.03) break;
        }
        bloomrad_cache[i] = fmin(1.414, bloomrad);

        double divisor = 1.0 / fmin(col.red, col.blue);
        col.red *= divisor; col.green *= divisor; col.blue *= divisor;
        int n = circradii.size();
        // if (i == 1075) std::cout << n << " radii:" << std::endl;
        for (j=n-1; j>=0; j--)
        {
            jay = circradii[j];
            RGB3Byte rgb = Color::rgb_from_color(col, circpixvals[j]);
            // if (i == 1075) std::cout << " draw radius " << jay << " pixel value * " << circpixvals[j] << std::endl;
            if (rgb.r >= 8 || rgb.b >= 8)
            {
                ImGui::GetBackgroundDrawList()->AddCircleFilled(xycoord, fmax(0.9, jay),
                    Color::black_to_transparent(IM_COL32(rgb.r, rgb.g, rgb.b, 255)), 0);
                cels[i]->onscreen = true;
            }
            if (rgb.r == 255 && rgb.b == 255) break;
        }
    }
    if (selected == i && cels[1])
    {
        ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(global_style.selected_color), 0, 2);
    }

    if ( (show_labels && cels[i]->type == star && !cels[i]->orbit &&
            ((!cbolbls_selected_idx && appmag <= appmagn_lblcut)
            || (cbolbls_selected_idx == lbltype_intrinsic && cels[i]->absolute_magnitude <= absmagn_lblcut)
            || (cbolbls_selected_idx == lbltype_nearby && here.distance_to(cels[i]->location) <= distance_lblcut)
            || (cbolbls_selected_idx == lbltype_Bayer && strlen(((Star*)cels[i])->Bayer))
            || (cbolbls_selected_idx == lbltype_Flamsteed && strlen(((Star*)cels[i])->Flamsteed))
            || (cbolbls_selected_idx == lbltype_Gould && (((Star*)cels[i])->GouldNo > 0))
            || (cbolbls_selected_idx == lbltype_sunlike && ((Star*)cels[i])->is_sunlike())
            || (cbolbls_selected_idx == lbltype_planets && (((Star*)cels[i])->has_planets >= planets_lblcut) )
            || (cbolbls_selected_idx == lbltype_planethz && (((Star*)cels[i])->has_hz_planets) )
            || (cbolbls_selected_idx == lbltype_binary && (((Star*)cels[i])->multisys))
            || (cbolbls_selected_idx == lbltype_knpole && cels[i]->known_poles)
            ))
        || (obj_is_localsys && lbl_localsys
            && ((cels[i]->mass >= lmasslim)
                || (vmag_cache[i] < (mag_limit_adjusted-4))
                || (cels[i]->tmprel.magnitude() < AU)
                )
            )
        || i == selected)
    {
        const char *dispname = cels[i]->name;
        ImFont *font = global_font;
        std::string str;
        cel_obj_class cls = cels[i]->typeclass();
        double lfontsz = global_font_size;
        if (cbolbls_selected_idx == lbltype_Bayer && cls == class_star && (((Star*)cels[i])->BayerGrkno >= 0))
        {
            // str = trim(std::string(((Star*)cels[i])->Bayer).substr(0, strlen(((Star*)cels[i])->Bayer)-3));
            // dispname = str.c_str();
            char c = Greek_symbol_mapping[((Star*)cels[i])->BayerGrkno];
            str = std::string(1, c);
            if (((Star*)cels[i])->Bayer[3] >= '1') str += std::string(1, ((Star*)cels[i])->Bayer[3]);
            dispname = str.c_str();
            font = Greek_font;
            lfontsz *= 1.312;           // for better visibility
        }
        else if (cbolbls_selected_idx == lbltype_Flamsteed && cls == class_star && strlen(((Star*)cels[i])->Flamsteed))
        {
            str = trim(std::string(((Star*)cels[i])->Flamsteed).substr(0, strlen(((Star*)cels[i])->Flamsteed)-3));
            dispname = str.c_str();
        }
        else if (cbolbls_selected_idx == lbltype_Gould && cls == class_star && (((Star*)cels[i])->GouldNo > 0))
        {
            str = std::to_string(((Star*)cels[i])->GouldNo);
            dispname = str.c_str();
        }
        ImVec2 sz = ImGui::CalcTextSize(dispname);
        ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(cels[i]->drawnx - sz.x/2, cels[i]->drawny+bloomrad+1),
            rgba_apply_redlight((i == selected) ? global_style.selected_color : global_style.objlbl_color),
            dispname);
    }
    return true;
}

void draw_objects()
{
    if (!ncelobjs) return;
    int i, j, n, pass;
    double step, dispw = dispcx*2, disph = dispcy*2;
    double orbseg = 81;
    lmasslim = lbllsys_mass_lim*1000;
    std::vector<CelestialObject*> to_draw_layered;
    global_magshift = -log(global_brightness) * invlogmagnbase;

    double mycensq = mycenobj->tmprel.squared_magnitude();
    double layer_cutoff = mycensq * 1.1 * zoom * zoom;
    mag_limit_adjusted = log(pow(magnbase, normal_best_mag_limit)*zoom) * invlogmagnbase;

    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    ImGuiIO& io = ImGui::GetIO();

    // Orbits
    if (show_orbits && show_localsys) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (!cels[i]->orbit) continue;
        if (cels[i]->cenobj != mycenobj && (whereami<0 || cels[i]->orbit->center != cels[whereami])) continue;
        if (cels[i]->orbit->center == mycenobj && cels[i]->mass < lmasslim) continue;

        Color col = Color::color_from_magnitude_indices(vmag_cache[i] + 5, cels[i]->BV_color);
        RGB3Byte rgb = Color::rgb_from_color(col, 1);
        ImU32 imcol = (i==selected) ? rgba_apply_redlight(global_style.selected_orbit_color) : rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 64));
        step = cels[i]->orbit->period / orbseg;
        CelestialLocation was = cels[i]->location;
        bool is_star = (cels[i]->typeclass() == class_star),
            is_moon = (cels[i]->typeclass() == class_moon),
            is_sat = (cels[i]->typeclass() == class_satellite);

        double viewer_distance = cels[i]->tmprel.magnitude();
        double light_travel_time = viewer_distance / speed_of_light;

        Cartesian2D lastcart;
        try
        {
            lastcart = Cartesian2D(cels[i]->drawnx, cels[i]->drawny);
        }
        catch(...)
        {
            lastcart.x = lastcart.y = -1e9;
        }
        for (j=-4; j<=orbseg; j++)
        {
            if (is_star)
                ((Star*)cels[i])->update_location(simnow + step*j - light_travel_time);
            else if (is_moon)
                ((Moon*)cels[i])->update_location(simnow + step*j - light_travel_time);
            else if (is_sat)
                ((Satellite*)cels[i])->update_location(simnow + step*j - light_travel_time);
            else
                ((Planet*)cels[i])->update_location(simnow + step*j - light_travel_time);

            CelestialLocation orbrel = cels[i]->location - here;

            Point rel = rotate3D(Point(orbrel), center, viewer_plane.v, -viewer_plane.a);

            Cartesian2D cart;
            try
            {
                cart = Cartesian2D(rel, azimuth+azimuth_correction, altitude, zoom);
                cart.x = dispcx + cart.x * dispcx; cart.y = dispcy + cart.y * dispcx;

                double dx1 = cart.x, dy1 = cart.y, dx2 = lastcart.x, dy2 = lastcart.y;

                if (lastcart.x >= -200 && lastcart.y >= -200 && cart.x >= -200 && cart.y >= -200)
                    wrapped_line(ImVec2(dx1, dy1), ImVec2(dx2, dy2), imcol, io);
            }
            catch (...)
            {
                cart.x = cart.y = -1e9;
            }

            lastcart = cart;
        }
        cels[i]->location = was;
    }

    // Faraway objects
    for (pass=0; pass<=1; pass++) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        cels[i]->drawnxmin = cels[i]->drawnxmax = cels[i]->drawnymin = cels[i]->drawnymax = -1e9;
        if (i == whereami) continue;

        if (!pass && fabs(bloomrad_cache[i]) > 3) continue;
        else if (pass && fabs(bloomrad_cache[i]) <= 3) continue;

        if (angular_radius[i]*zoom < sphere_rad_threshold)
        {
            if (cels[i]->drawnx < 0 || cels[i]->drawnx >= dispw) continue;
            if (cels[i]->drawny < 0 || cels[i]->drawny >= disph) continue;
        }

        // Counterintuitive that we would process *more* objects during dragging and not *less*,
        // but since discs become transparent wireframes during drag, it only makes sense that the
        // ground should become transparent as well.
        if (view_mode == vm_horizon && !dragging && cels[i]->viewrel.y < 0 && angular_radius[i] < sphere_rad_threshold)
        {
            continue;
        }

        xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
        appmag = vmag_cache[i] - sky_mag_shift;
        if (appmag > mag_limit_adjusted && i
            && (cbolbls_selected_idx != 6 || (((Star*)cels[i])->has_planets < planets_lblcut) )
            && (cbolbls_selected_idx != 7 || !(((Star*)cels[i])->has_hz_planets) )) continue;

        bloomrad = fabs(bloomrad_cache[i]);
        bloomrad = fmin(max_bloomrad, bloomrad);

        // if (cls != class_satellite && angular_radius[i]*zoom > sphere_rad_threshold)
        if (mycensq < light_year_sq
            && cels[i]->tmprel.squared_magnitude() < layer_cutoff)
        {
            n = to_draw_layered.size();
            if (!n)
            {
                to_draw_layered.push_back(cels[i]);
                discinstead[i] = true;
            }
            else
            {
                discinstead[i] = false;
                double trm = cels[i]->get_horizon_distance();
                for (j=0; j<n; j++)
                {
                    if (to_draw_layered[j]->get_horizon_distance() < trm)
                    {
                        to_draw_layered.insert(to_draw_layered.begin()+j, cels[i]);
                        discinstead[i] = true;
                        break;
                    }
                }
                if (!discinstead[i])
                {
                    to_draw_layered.push_back(cels[i]);
                }
                discinstead[i] = true;
            }
        }
        else draw_one_object(i);
        if (!cels[1]) return;
    }

    if (!cels[1]) return;

    // Near objects
    n = to_draw_layered.size();
    for (j=0; j<n; j++)
    {
        draw_one_object(to_draw_layered[j]->seqno);
        if (!cels[1]) return;
    }

}

ImVec2 sc_drawcoords(CelestialObject *obj, CelestialObject *cel, bool update_drawnxy = true)
{
    Point relloc = obj->location.local_position - cel->location.local_position;
    relloc = rotate3D(relloc, center, cel->location.equatorial_plane.v, cel->location.equatorial_plane.a);
    relloc = rotate3D(relloc, center, yaxis, cel->timeofday());

    double lon = fmod(find_angle(relloc.z, -relloc.x) - azimuth, _pi*2);
    if (lon >  _pi) lon -= _pi*2;
    if (lon < -_pi) lon += _pi*2;
    double lat = fmod(find_angle(sqrt(relloc.x*relloc.x+relloc.z*relloc.z), relloc.y) - altitude, _pi*2);
    if (lat < -half_pi) lat += _pi*2;
    if (lat >  half_pi) lat -= _pi*2;
 
    int dx = dispcx + lon/sclk_scale;
    int dy = dispcy - lat/sclk_scale;

    if (update_drawnxy)
    {
        obj->drawnx = dx;
        obj->drawny = dy;
    }

    return ImVec2(dx,dy);
}

void sc_draw_object(CelestialObject *obj, CelestialObject *cel)
{
    ImVec2 objdxy = sc_drawcoords(obj, cel);
    cel_obj_class cls = obj->typeclass();
    ImGuiIO& io = ImGui::GetIO();

    int dx, dy;
    if (cls == class_star)
    {
        Color objcol = Color::color_from_magnitude_indices(0, obj->BV_color);
        objcol.normalize(255);
        ImU32 obj32 = rgba_apply_redlight(IM_COL32((int)objcol.red, (int)objcol.green, (int)objcol.blue, 255));
        ImGui::GetBackgroundDrawList()->AddCircleFilled(objdxy, 10, obj32);
        double lstep = _pi / 8;
        double r = 20;
        int dx1 = objdxy.x, dx2, dy1 = objdxy.y - r, dy2;
        for (theta = lstep; theta <= _pi*2+0.001; theta += lstep)
        {
            dx2 = dx1;
            dy2 = dy1;
            r = 33 - r;
            dx1 = objdxy.x + r * sin(theta);
            dy1 = objdxy.y - r * cos(theta);
            ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1,dy1), ImVec2(dx2,dy2), obj32);
        }
    }
    else if (cls == class_planet || cls == class_moon)
    {
        if (!obj->looked_for_maps)
        {
            obj->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
            std::thread ttex(load_textures, obj);
            ttex.detach();
        }

        Color objcol = Color::color_from_magnitude_indices(0, obj->BV_color);
        objcol.normalize(255);
        int x, y;
        RGB3Byte rgb;
        double theta, phi;
        for (y = -ico_sz; y <= ico_sz; y++)
        {
            theta = half_pi * pow(fabs(y) / (ico_sz+1), 1) * sgn(y);
            int xsz = sqrt(ico_sz*ico_sz - y*y);

            for (x = -xsz; x <= xsz; x++)
            {
                phi = half_pi / xsz * x;
                if (obj->cloud_map) rgb = obj->cloud_map->color_at(theta, phi);
                else if (obj->surf_map) rgb = obj->surf_map->color_at(theta, phi);
                else rgb = RGB3Byte(objcol.red, objcol.green, objcol.blue);

                dx = objdxy.x + x;
                dy = objdxy.y - y;

                ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(dx,dy), ImVec2(dx+1,dy+1),
                    rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 255))
                    );
            }
        }
    }
    else if (cls == class_satellite)
    {
        double line_of_sight = cel->location.local_position.get_distance_to_line(
            obj->location.local_position, obj->get_light_center()->location.local_position);
        int i = obj->seqno;

        ImU32 satcol = (i == selected)
            ? rgba_apply_redlight(global_style.selected_color)
            : ((line_of_sight < cel->volumetric_mean_radius)
                ? rgba_apply_redlight(IM_COL32(128,  96,  64, 255))
                : rgba_apply_redlight(IM_COL32(255, 255, 255, 255)));

        bloomrad = draw_satellite_icon(objdxy, satcol);

        if (i == selected || i == trackidx)
        {
            dx = objdxy.x;
            dy = objdxy.y;
            ImU32 col = rgba_apply_redlight((i == trackidx) ? IM_COL32(255, 255, 255, 64) : global_style.selected_color);
            ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx,0), ImVec2(dx,dy-ln_spc), col);
            ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0,dy), ImVec2(dx-ln_spc-circ_sz,dy), col);
            ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx,dy+ln_spc), ImVec2(dx,dispcy*2), col);
            ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx+ln_spc+circ_sz,dy), ImVec2(dispcx*2,dy), col);
        }

        if (show_labels)
        {
            const char *dispname = obj->name;
            ImFont *font = global_font;
            std::string str;
            double lfontsz = global_font_size;
            ImVec2 sz = ImGui::CalcTextSize(dispname);
            ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(obj->drawnx - sz.x/2, obj->drawny+bloomrad+1),
                rgba_apply_redlight((i == selected) ? global_style.selected_color : global_style.objlbl_color),
                dispname);
        }

        if (show_orbits && obj->orbit)
        {
            int dx1 = -1e9, dy1 = -1e9, dx2, dx2a, dy2;
            double sincewhen, hasta_la_pasta = simnow + 0.5*obj->orbit->period, stepf = obj->orbit->period / 30;
            bool satsunlit;

            for (sincewhen = simnow - 0.5*obj->orbit->period; sincewhen <= hasta_la_pasta; sincewhen += stepf)
            {
                ((Satellite*)obj)->update_location(sincewhen);
                objdxy = sc_drawcoords(obj, cel, false);
                dx2 = objdxy.x;
                dy2 = objdxy.y;

                line_of_sight = cel->location.local_position.get_distance_to_line(
                    obj->location.local_position, obj->get_light_center()->location.local_position);

                satsunlit = (line_of_sight < cel->volumetric_mean_radius);

                if (dx1 >= -1000 && dy1 >= 0)
                {
                    satcol = (i == selected)
                        ? rgba_apply_redlight(global_style.selected_color)
                        : (satsunlit
                            ? rgba_apply_redlight(IM_COL32(128,  96,  64, 128))
                            : rgba_apply_redlight(IM_COL32(255, 255, 255, 128)));

                    dx2a = dx2;
                    if (dx2a < (dx1-dispcx)) dx2a += dispcx*2;
                    else if (dx2a > (dx1+dispcx)) dx2a -= dispcx*2;

                    wrapped_line(ImVec2(dx1,dy1), ImVec2(dx2,dy2), satcol, io);
                }

                dx1 = dx2;
                dy1 = dy2;
            }
            ((Satellite*)obj)->update_location(simnow);
        }
    }
}

ViewMode last_vmode = vm_skyatlas;
void draw_sunclock()
{
    if (whereami < 0) return;

    int i;
    if (last_vmode != view_mode) for (i=0; cels[i]; i++)
    {
        cels[i]->drawnx = cels[i]->drawny = -1e9;
    }

    CelestialObject *cel = cels[whereami];
    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);
    cel_obj_class cls = cel->typeclass();

    if (!cel->nlocales) cel->read_locales("locales.json");

    if (!cel->looked_for_maps)
    {
        cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    Color c = Color::color_from_magnitude_indices(0, cel->BV_color);
    Color daylight = Color::color_from_magnitude_indices(0, cel->get_light_center()->BV_color);
    RGB3Byte prgb = Color::rgb_from_color(c, -1), rgb = prgb, nrgb(0,0,0);
    daylight.normalize(1);

    int x, y, dx, dy, step=2, size = dispcx/2, halfwid = size*2;
    sclk_scale = half_pi / size / zoom;
    double lat, lon, obl = 1.0 - cel->oblateness, elevation;
    Map *map = cel->surf_map ? cel->surf_map : (cel->cloud_map ? cel->cloud_map : nullptr);
    Map *nmap = cel->night_map ? cel->night_map : nullptr;
    Point land;
    bool dwh = false;

    if (cls == class_moon)
        dwh = (((Moon*)cel)->depth > zero_isnt_really_zero
            && ((Moon*)cel)->width > zero_isnt_really_zero
            && ((Moon*)cel)->height > zero_isnt_really_zero);

    double equatorial_radius, theta, cos_theta, is_day, is_night;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * .5;
    else
        equatorial_radius = cel->get_equatorial_radius();


    for (y=dispcy; y>=-dispcy; y-=step)
    {
        dy = dispcy + y;
        lat = lat_from_y(y);
        if (fabs(lat) > half_pi) continue;

        for (x=-halfwid; x<halfwid; x+=step)
        {
            dx = dispcx + x;
            lon = lon_from_x(x);
            elevation = (map) ? (map->elevation_at(lat, lon)) : 0;
            land = Point::from_ra_dec(lon, lat, dwh ? 1 : (equatorial_radius + elevation), 0);

            if (dwh)
            {
                land.x *= ((Moon*)cel)->width  * .5;
                land.y *= ((Moon*)cel)->height * .5;
                land.z *= ((Moon*)cel)->depth  * .5;
                if (elevation) land.scale(land.magnitude()+elevation);          // TODO: This is a costly calculation - possible to streamline it?
            }
            else land.y *= obl;
            land = rotate3D(land, center, yaxis, -cel->timeofday());
            land = rotate3D(land, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);

            land += cel->location.local_position;
            if (self_luminous) is_day = 1;
            else
            {
                theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), _pi);
                if (fabs(theta) < half_pi)
                {
                    cos_theta = cos(theta);
                    is_day = fmin(1, pow(cos_theta, 0.333));
                    is_night = 0;
                }
                // TODO: Twilight
                else
                {
                    is_day = 0;
                    is_night = 1;
                }
            }

            if (map) rgb = map->color_at(lat, lon);
            else rgb = prgb;

            if (nmap) nrgb = nmap->color_at(lat, lon);

            if (self_luminous)
            {
                rgb.r *= is_day;
                rgb.g *= is_day;
                rgb.b *= is_day;
            }
            else
            {
                rgb.r *= is_day * daylight.red;
                rgb.g *= is_day * daylight.green;
                rgb.b *= is_day * daylight.blue;
            }

            if (is_night)
            {
                rgb.r += nrgb.r * is_night;
                rgb.g += nrgb.g * is_night;
                rgb.b += nrgb.b * is_night;
            }

            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(dx, dy), ImVec2(dx+step, dy+step),
                rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 255)));
        }
    }

    if (selected_locale)
    {
        dx = dispcx + fmod(selected_locale->lon * fiftyseventh - azimuth , _pi*2) / sclk_scale;
        dy = dispcy - fmod(selected_locale->lat * fiftyseventh - altitude, _pi*2) / sclk_scale;
        while (dx < 0) dx += dispcx*2;
        while (dx >= dispcx*2) dx -= dispcx*2;
        while (dy < 0) dy += dispcy*2;
        while (dy >= dispcy*2) dy -= dispcy*2;

        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx,0), ImVec2(dx,dy-ln_spc), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0,dy), ImVec2(dx-ln_spc,dy), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx,dy+ln_spc), ImVec2(dx,dispcy*2), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx+ln_spc,dy), ImVec2(dispcx*2,dy), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(dx,dy), circ_sz, rgba_apply_redlight(global_style.selected_color), 0, 2);
    }

    if (show_sats && first_sat >= 0) for (i=first_sat; i<ncelobjs; i++)
    {
        if (cels[i] && cels[i]->typeclass() == class_satellite && cels[i]->orbit && cels[i]->orbit->center == cel)
        {
            sc_draw_object(cels[i], cel);
        }
    }

    // Subsolar point
    CelestialObject *sun = cel->get_light_center();
    if (sun && sun != cel)
    {
        sc_draw_object(sun, cel);
    }

    // Substellar point of host star, if different from light center
    CelestialObject *host = cel->cenobj;
    if (host && host != sun)
    {
        sc_draw_object(host, cel);
    }

    if (cel->type != star) for (i=cel->seqno+1; cels[i]; i++)
    {
        if (!cels[i]->orbit) continue;
        if (cels[i]->type == star) continue;
        if (cels[i]->type == artificial) continue;
        if (cels[i]->orbit->center != cel) continue;
        if (cels[i]->typeclass() == class_moon && !((Moon*)cels[i])->major_moon) continue;
        sc_draw_object(cels[i], cel);
    }

    // Subplanetary point if on a moon
    CelestialObject *planet = cel->orbit ? cel->orbit->center : nullptr;
    if (planet && planet != sun)
    {
        sc_draw_object(planet, cel);
    }
}

#define hznodes 1024
bool draw_marker[hznodes];
double hz_dx[hznodes], hz_dy[hznodes];
void find_horizon()
{
    hz_y = dispcy*29;
    if (view_mode == vm_horizon)
    {
        int j;
        CelestialObject *cel = cels[whereami];
        if (!cel->looked_for_maps)
        {
            cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
            std::thread ttex(load_textures, cel);
            ttex.detach();
        }

        Planet *p;
        double horizon_lift_rad = 0;
        if (cel->typeclass() == class_planet || cel->typeclass() == class_moon)
        {
            p = (Planet*)cel;

            // Shared with atmospheric_refraction() (planet.cpp) -- see its own comment: star
            // refraction near the horizon is calibrated against this same lift, so a star at the
            // true horizon doesn't render as if it were behind the visually-raised ground.
            horizon_lift_rad = p->atmospheric_horizon_lift();
        }

        Point pthz = rotate3D(zaxis, center, xaxis, -horizon_lift_rad);
        // std::cout << "pthz=" << pthz << std::endl;

        double theta = 0, step = _pi*2/hznodes;
        for (j = 0; j < hznodes; j++)
        {
            draw_marker[j] = false;
            Point pt = rotate3D(pthz, center, yaxis, theta);
            Point pt0 = rotate3D(zaxis, center, yaxis, theta);

            Cartesian2D horizon = Cartesian2D(pt, azimuth, altitude, zoom);
            Cartesian2D horizon0 = Cartesian2D(pt0, azimuth, altitude, zoom);
            hz_dx[j] = horizon.x * dispcx + dispcx;
            hz_dy[j] = horizon.y * dispcx + dispcy;
            if (hz_dy[j] < 0) hz_dy[j] = 0;
            else draw_marker[j] = (hz_dx[j] >= 0 && hz_dx[j] < dispcx*2);
            // if (draw_marker[j]) std::cout << "pt=" << pt << std::endl;
            if (draw_marker[j] && hz_y > dispcy*2) hz_y = horizon0.y * dispcx + dispcy;
            theta += step;
        }
    }
}

void draw_horizon()
{
    // Horizon
    // TODO: Render according to bump map and generate a fictitious skyline.
    if (view_mode == vm_horizon)
    {
        int i, j;
        CelestialObject *cel = cels[whereami];

        if (!cel->looked_for_maps)
        {
            cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
            std::thread ttex(load_textures, cel);
            ttex.detach();
        }

        double is_day = fmin(1, luminous_flux*2.5e-11 + starlight);

        Map *map = cel->surf_map;
        RGB3Byte rgb = map ? map->color_at(viewer_lat, viewer_lon) : RGB3Byte(0, 8, 24);
        rgb.r *= is_day;
        rgb.g *= is_day;
        rgb.b *= is_day;

        double hz_fx = -1e9, hz_fy = 1e9;
        ImVec2 points[4];
        for (j = 0; j <= hznodes; j++) if (hz_dx[j%hznodes] > -1e5 && hz_dy[j%hznodes] > -1e5)              // draw_marker[j])
        {
            if (hz_fx > -1e8 && hz_fy < 1e8 && fabs(hz_fx-hz_dx[j%hznodes]) < dispcx * zoom)
            {
                points[0] = ImVec2(hz_fx, hz_fy);
                points[1] = ImVec2(hz_dx[j%hznodes]+1, hz_dy[j%hznodes]);
                points[2] = ImVec2(hz_dx[j%hznodes]+1, dispcy*2);
                points[3] = ImVec2(hz_fx, dispcy*2);

                ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4,
                    rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, dragging ? (192-128*is_day) : 255)));
            }

            hz_fx = hz_dx[j%hznodes];
            hz_fy = hz_dy[j%hznodes];
        }

        /*double hz_draw_y = (hz_y > dispcy*28 && altitude < 0) ? 0 : hz_y;
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, hz_draw_y), ImVec2(dispcx*2, dispcy*2),
            rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, dragging ? (192-128*is_day) : 255)));*/

        if (1) // hz_y < dispcy*2)
        {
            double hzbrt = _lum_r_comp*rgb.r + _lum_g_comp*rgb.g * _lum_b_comp*rgb.b;
            ImU32 mkrcol = rgba_apply_redlight((hzbrt >= 176) ? IM_COL32(0,0,0,255) : global_style.conslbl_color);
            if (show_grid) for (i = 0; i < 16; i++) if (draw_marker[j = i*64])
            {
                ImGui::GetBackgroundDrawList()->AddText(ImVec2(hz_dx[j], hz_dy[j]), mkrcol, compass[i]);
                if (hzbrt >= 176) ImGui::GetBackgroundDrawList()->AddText(ImVec2(hz_dx[j]-1, hz_dy[j]), mkrcol, compass[i]);
            }
        }
    }
}

void draw_sky_gradient()
{
    sky_grad.clear();
    if (whtbkgd) return;
    if (!dragging && (cels[whereami]->typeclass() == class_planet || cels[whereami]->typeclass() == class_moon))
    {
        Planet *p = (Planet*)cels[whereami];
        if (p->surface_pressure)
        {
            double Rayleigh = 1.0 - p->atmospheric_particulates;
            Color pcol = Color::color_from_magnitude_indices(0, p->BV_color);
            pcol.normalize(1);

            float city_lights = 0;
            if (cels[whereami]->night_map)
            {
                RGB3Byte rgb = cels[whereami]->night_map->color_at(viewer_lat, viewer_lon);
                if (rgb.r > 0.7*rgb.b) city_lights = rgb.r;
            }

            int x_extent = dispcx*2-1;
            double skylight = fmin(1, pow(luminous_flux*2.5e-11, 1.0/5.5) + starlight + 0.001 * city_lights);
            sky_mag_shift = skylight * -10;
            double  r = fmin(1, (Rayleigh * 0.37 + p->atmospheric_particulates * pcol.red  ) * skylight),
                    g = fmin(1, (Rayleigh * 0.58 + p->atmospheric_particulates * pcol.green) * skylight),
                    b = fmin(1, (Rayleigh * 0.81 + p->atmospheric_particulates * pcol.blue ) * skylight),
                    a = fmin(1, pow(p->surface_pressure, 0.1) * skylight);
            unsigned char r255, g255, b255;
            for (int y = fmin(hz_y, dispcy*2-1); y>=0; y--)
            {
                r255 = r*255; g255 = g*255; b255 = b*255;
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, y), ImVec2(x_extent, y),
                    rgba_apply_redlight(IM_COL32( (int)(r255), (int)(g255), (int)(b255), (int)(a*255) ) ));

                r *= 0.999;
                g *= 0.9995;
                b *= 0.9999;

                sky_grad[y] = RGB3Byte(r255*a, g255*a, b255*a);
            }
        }
    }
}

void draw_cons_lines()
{
    if (!cels[1]) return;
    int i, l, m, n;
    double dispw = dispcx*2, disph = dispcy*2;
    ImGuiIO& io = ImGui::GetIO();

    // Hide lines if more than 10 l.y. from Sun.
    draw_actual_conslines = here.distance_to(cels[0]->location) < light_year*10;

    n = constellations.size();
    for (i=0; i<n; i++)
    {
        m = constellations[i].lines.size();
        for (l=0; l<m; l++)
        {
            if (!constellations[i].lines[l].a || !constellations[i].lines[l].b) continue;
            if (constellations[i].lines[l].a == mycenobj) continue;
            if (constellations[i].lines[l].b == mycenobj) continue;

            int dx1, dx2, dy1, dy2;

            dx1 = constellations[i].lines[l].a->drawnx;
            dy1 = constellations[i].lines[l].a->drawny;
            if (dx1 < -1e3) continue;
            if (dy1 < -1e3) continue;

            dx2 = constellations[i].lines[l].b->drawnx;
            dy2 = constellations[i].lines[l].b->drawny;
            if (dx2 < -1e3) continue;
            if (dy2 < -1e3) continue;

            if (draw_actual_conslines)
                wrapped_line(ImVec2(dx1, dy1), ImVec2(dx2, dy2), global_style.consline_color, 1, io);
        }
    }

    // Constellation labels
    n = constellations.size();
    ImU32 cbcol = rgba_apply_redlight(Color::adjust_alpha(global_style.consline_color, 0.2));
    if (show_labels || (show_consln && !draw_actual_conslines)) for (l=0; l<=n; l++)
    {
        Point lconsdir;

        // Constellation boundaries
        m = constellations[l].bounds.size();
        for (i=0; i<m; i++)
        {
            Point cbd = Point::from_ra_dec(constellations[l].bounds[i].RA, constellations[l].bounds[i].decl, light_year);
            cbd = to_viewer_plane(cbd);
            lconsdir += cbd;
            Cartesian2D cart(cbd, azimuth+azimuth_correction, altitude, zoom);
            float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);

            if (dx < 0 || dy < 0) continue;
            if (draw_actual_conslines) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(dx,dy), ImVec2(dx+1,dy+1), cbcol);
        }

        if (!constellations[l].lines.size()) continue;
        Cartesian2D cart(lconsdir, azimuth+azimuth_correction, altitude, zoom);
        float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);

        if (dx < 0 || dy < 0) continue;
        ImVec2 sz = ImGui::CalcTextSize(constellations[l].name.c_str());
        dx -= sz.x/2;
        dy -= sz.y/2;
        if (dx >= 0 && dx < dispw && dy >= 0 && dy < disph)
        {
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(dx, dy),
                rgba_apply_redlight(global_style.conslbl_color),
                constellations[l].name.c_str());
        }
    }

    if (show_axes)
    {
        Point axisdir[6] = {xaxis, yaxis, zaxis, center-xaxis, center-yaxis, center-zaxis};
        for (i=0; i<6; i++)
        {
            Point laxdir = to_viewer_plane(axisdir[i]);
            Cartesian2D cart(laxdir, azimuth+azimuth_correction, altitude, zoom);
            float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);

            if (dx < 0 || dy < 0) continue;
            ImVec2 sz(64,64);
            dx -= sz.x/2;
            dy -= sz.y/2;
            if (dx >= 0 && dx < dispw && dy >= 0 && dy < disph)
            {
                std::string axname = (i<3) ? "+" : "-";
                ImU32 axcolor;

                switch (i % 3)
                {
                    case 0: axname += std::string("X"); axcolor = IM_COL32(255, 0, 0, 255); break;
                    case 1: axname += std::string("Y"); axcolor = IM_COL32(0, 255, 0, 255); break;
                    case 2: axname += std::string("Z"); axcolor = IM_COL32(0, 0, 255, 255); break;
                }

                ImGui::GetBackgroundDrawList()->AddText(global_font, 64,
                    ImVec2(dx, dy),
                    rgba_apply_redlight(axcolor),
                    axname.c_str());
            }
        }
    }
}

void draw_mouse_cursor(ImGuiIO& io)
{
    if (!hide_mouse || (frames_without_mousemove > 203) || !cels[1]) return;

    cursor_size = (int)io.DisplaySize.x/99;
    circle_size = cursor_size / 2.5;

    ImU32 cc[3];
    cc[0] = rgba_apply_redlight(global_style.cursor_color1);
    cc[1] = rgba_apply_redlight(global_style.cursor_color2);
    cc[2] = rgba_apply_redlight(global_style.cursor_color3);

    int i;

    for (i=0; i<3; i++)
    {
        // top
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size, io.MousePos.y - circle_size*2 + (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y - cursor_size - circle_size + (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size, io.MousePos.y - circle_size*2 + (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y - cursor_size - circle_size + (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);

        // left
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size*2 + (i-1)*_cursor_fade, io.MousePos.y - circle_size),
            ImVec2(io.MousePos.x - cursor_size - circle_size + (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size*2 + (i-1)*_cursor_fade, io.MousePos.y + circle_size),
            ImVec2(io.MousePos.x - cursor_size - circle_size + (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);

        // bottom
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size, io.MousePos.y + circle_size*2 - (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y + cursor_size + circle_size - (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size, io.MousePos.y + circle_size*2 - (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y + cursor_size + circle_size - (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);

        // right
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size*2 - (i-1)*_cursor_fade, io.MousePos.y + circle_size),
            ImVec2(io.MousePos.x + cursor_size + circle_size - (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size*2 - (i-1)*_cursor_fade, io.MousePos.y - circle_size),
            ImVec2(io.MousePos.x + cursor_size + circle_size - (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);
    }
}

std::vector<Cloud> skyclouds;
void draw_cloudy_sky()
{
    if (view_mode != vm_horizon) return;

    unsigned int seed = 65536 * (viewer_lat + _pi);
    srand(seed);
    seed = (rand() % 65536) + (65536 * fabs(viewer_lon));

    CelestialObject *cel = cels[whereami];
    if (!cel->cloud_map) return;

    RGB3Byte rgb = cel->cloud_map->color_at(viewer_lat, viewer_lon);
    double cloudiness = sqrt(fmin(1,rgb.luminance()/192));
    double is_day = fmin(1, luminous_flux*2.5e-11 + starlight);

    rgb.r *= is_day;
    rgb.g *= is_day;
    rgb.b *= is_day;

    ImU32 imc = IM_COL32(rgb.r, rgb.g, rgb.b, (dragging ? 128 : 255)*cloudiness);
    if (hz_y > 0 && (hz_y < dispcy*28 || altitude > 1)) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(dispcx*2, hz_y), imc);

    #if 0
    if (!skyclouds.size())
    {
        // TODO:

        Cloud c;
        c.color = rgb;
        c.core_dist = cel->volumetric_mean_radius + 1500;
        c.height = 200;
        c.width = 500;
        c.latitude = viewer_lat;
        c.longitude = viewer_lon;

        skyclouds.push_back(c);
    }

    int i, n = skyclouds.size();
    for (i=0; i<n; i++) skyclouds[i].draw(cel->volumetric_mean_radius);
    #endif
}
