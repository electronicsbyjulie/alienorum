
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
    if (view_mode == vm_system) return;

    int i, j;
    Cartesian2D prev, zdes;
    ImU32 gc = rgba_apply_redlight(Color::adjust_alpha(global_style.grid_color, 0.1));
    ImU32 gcb = rgba_apply_redlight(Color::adjust_alpha(global_style.grid_color_brighter, 0.1));
    ImU32 ec = rgba_apply_redlight(Color::adjust_alpha(global_style.ecliptic_color, 0.25));
    double node = (whereami >= 0) ? cels[whereami]->equinox_RA : 0;
    myeq = (whereami >= 0) ? cels[whereami]->equinox_RA : 0;
    npaz = (view_mode == vm_horizon) ? fmod(npdummy.RA_as_radians(here, myeq), _pi*2) : 0;
    bool prev_valid = false;
    int jstart = (view_mode == vm_skymap) ? -90 : -80;
    int jend = -jstart;
    bool is_sat = (whereami>0) && (cels[whereami]->typeclass() == class_satellite);
    Rotation ra_dec_plane = (whereami>0) ? (is_sat
            ? cels[whereami]->location.orbital_plane
            : cels[whereami]->location.equatorial_plane)
        : here.equatorial_plane;

    // RA and Dec lines.
    for (i=0; i<24; i++)
    {
        prev_valid = false;
        for (j=jstart; j<=jend; j+=10)
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

    for (j=jstart; j <= jend; j+=10)
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

// ---- Eclipses ------------------------------------------------------------------------------
//
// Any solid body between another body and their shared star throws a shadow onto it: a moon's
// shadow crossing its planet, a planet's shadow swallowing its moon, one moon's shadow on
// another. Which of the two is the "eclipse" and which the "transit" is only a matter of where
// the viewer stands, so nothing here distinguishes them -- both are the same geometry, and the
// shader (see sphere_impostor.cpp) resolves it per pixel by asking how much of the star's disc
// the caster hides as seen from that exact point of the surface.
//
// The candidate list exists because the search would otherwise have to run over `cels`, which
// holds up to MAX_CELOBJS entries (star catalogs, and with astorb loaded, a great many minor
// planets) -- for every disc, every frame. Rebuilt once per frame by refresh_eclipse_casters()
// at the top of draw_objects(); draw_sphere_gpu() then only walks this much shorter list, and
// rejects everything outside the object's own system on a single pointer comparison.
// Defined further down, with the rest of the atmosphere code: both an eclipse's copper light
// and a planet's own glowing limb are colored from a world's air.
static void atmosphere_colors(Planet *pl, double out_high[3], double out_low[3], double out_umbra[3]);

// Brightness of the light an Earth-thick atmosphere refracts into its own shadow, as a fraction
// of direct sunlight -- see the shader's UMBRA_REFRACTION comment for why this is a legibility
// figure rather than the photometric one (a totally eclipsed Moon really is some ten thousand
// times fainter than a full one, which on an unexposed screen is no color at all).
static const double kUmbraLight = 0.15;

// How far out the limb glow is drawn, in pressure scale heights. Air thins exponentially and
// never actually stops, so this is a choice about where it stops being worth drawing: six scale
// heights leaves about a quarter of a percent of the surface density, which is roughly where a
// real limb fades out of a photograph. It also sets how far the impostor's bounding quad has to
// grow (see queue_sphere_impostor), so raising it is not free.
static const double kAtmosphereScaleHeights = 6.0;

// How much air light is reckoned to have crossed when it comes to us *through* a world's
// atmosphere rather than off the top of it -- and so how much of its blue was taken out on the
// way, which is to say how red the copper gets. Raise it for a deeper, more saturated red on a
// backlit limb and in an eclipse's umbra, lower it toward a pale amber. Scaled per world by its
// own surface pressure below, so this is the figure for an Earth-thick atmosphere.
//
// Its companion knob is ATM_REDDEN_PHASE in sphere_impostor.cpp, which decides *when* through
// the phases the reddening arrives rather than how far it goes.
static const double kAtmosphereOpticalDepth = 3.0;

struct EclipseCandidate
{
    CelestialObject *obj;
    CelestialObject *lightcen;      // resolved once per frame here rather than per receiver
    double radius;
};
static std::vector<EclipseCandidate> eclipse_candidates;

CelestialObject *eclipsed_light = nullptr;
double eclipsed_fraction = 0;

// Fraction of one disc hidden behind another, both as angles seen from the same point: R is the
// light source's angular radius, r the occulter's, d the angle between their centers. This is
// the same circle-circle intersection the impostor's fragment shader runs per pixel (see
// disc_overlap() in sphere_impostor.cpp) -- deliberately, since the two have to agree: the
// shader decides how dark a patch of a distant planet goes, this decides how dark the sky goes
// for someone standing under it, and a difference between them would show up as a shadow on the
// ground that does not match the sky above it. The formula lives twice because one copy has to
// be GLSL and the other C++; if either is ever corrected, correct both.
static double cpu_disc_overlap(double R, double r, double d)
{
    if (R <= 0) return 0;
    if (d >= R + r) return 0;                       // no contact
    if (d <= r - R) return 1;                       // totality
    if (d <= R - r) return (r*r)/(R*R);             // annular: the occulter cannot cover it all
    // atan2 rather than acos for both angles, matching the shader's copy line for line -- and for
    // the same reason, which is spelled out there: with a caster far larger than the light source
    // (a planet's shadow on its own moon, where the two differ by a factor of a few hundred) the
    // cosine acos would be handed sits a few parts in a million from 1.0 across the entire
    // penumbra, and acos is vertical there. Double precision hides that here where single does
    // not, but the two have to agree pixel for pixel, so they are written the same way.
    double d2 = d*d, R2 = R*R, r2 = r*r;
    double lens = 0.5*sqrt(fmax(0.0, (R + r - d)*(d + r - R)*(d - r + R)*(d + r + R)));
    double a1 = atan2(2*lens, d2 + r2 - R2);
    double a2 = atan2(2*lens, d2 + R2 - r2);
    return fmin(1.0, fmax(0.0, (r2*a1 + R2*a2 - lens)/(_pi*R2)));
}

double eclipse_obscuration(CelestialObject *light)
{
    if (!light || eclipse_candidates.empty()) return 0;

    double d_light = light->tmprel.magnitude();
    double r_light = light->get_equatorial_radius();
    if (d_light <= 0 || r_light <= 0) return 0;

    double light_ang = asin(fmin(1.0, r_light / d_light));
    Point lhat = light->tmprel * (1.0 / d_light);

    double worst = 0;
    for (const EclipseCandidate &cand : eclipse_candidates)
    {
        if (cand.obj == light) continue;
        // The world under the observer's feet is skipped: it hides the sun for half of every
        // day and that is called night, not an eclipse -- the sin(altitude) term in the
        // daylight computation already says so, and counting it here would say it twice.
        if (whereami >= 0 && cand.obj == cels[whereami]) continue;

        double dist = cand.obj->tmprel.magnitude();
        if (dist <= 0 || dist >= d_light) continue;         // has to be between us and the light

        double ang = asin(fmin(1.0, cand.radius / dist));
        double cosine = (cand.obj->tmprel.x*lhat.x + cand.obj->tmprel.y*lhat.y + cand.obj->tmprel.z*lhat.z) / dist;
        double sep = acos(fmin(1.0, fmax(-1.0, cosine)));
        // Deepest occulter rather than the sum, for the same reason the shader takes the deepest
        // of its casters: two bodies in front of the same sun would be hiding overlapping parts
        // of one disc, and adding them would count the overlap twice.
        worst = fmax(worst, cpu_disc_overlap(light_ang, ang, sep));
    }
    return worst;
}

// The stretch of eclipse_candidates belonging to one light source, as a half-open [begin, end).
// The list is kept sorted by lightcen (see refresh_eclipse_casters) so that this is two binary
// searches rather than a walk: with an exoplanet catalog loaded the candidate list runs to
// thousands of bodies, all but a handful of them orbiting stars that have nothing to do with the
// one being asked about, and both the per-disc caster search and the per-body magnitude test ask
// this question for every object they touch.
static void candidates_for_light(CelestialObject *lightcen, int &begin, int &end)
{
    auto lo = std::lower_bound(eclipse_candidates.begin(), eclipse_candidates.end(), lightcen,
        [](const EclipseCandidate &a, CelestialObject *key) { return a.lightcen < key; });
    auto hi = std::upper_bound(lo, eclipse_candidates.end(), lightcen,
        [](CelestialObject *key, const EclipseCandidate &a) { return key < a.lightcen; });
    begin = (int)(lo - eclipse_candidates.begin());
    end   = (int)(hi - eclipse_candidates.begin());
}

// ---- Shadows in the system frame ---------------------------------------------------------
//
// Two things here have to know where a shadow falls without reference to where anyone is
// watching from: the sun clock, which shades a whole world's map by where its light is landing,
// and the apparent magnitude of a body too far off to be more than a dot (eclipse_illumination()
// below). Everything else in this file works from tmprel, positions relative to the observer --
// but a shadow is thrown between two bodies and cares nothing for who is looking, so these take
// local_position throughout, the frame the sun clock's surface points are already built in (see
// draw_sunclock: `land += cel->location.local_position`).
//
// On the sun clock's scale an eclipse gets drawn as what it actually is: a small dark spot
// crossing the daylit side at a thousand-odd kilometres an hour, ringed by the much wider, much
// softer penumbra. That shape comes out of the same disc-overlap arithmetic as everywhere else,
// asked separately for each point of the map rather than once for the planet.
struct ShadowCaster
{
    CelestialObject *obj;           // kept so callers can ask what the shadow is being thrown by
    Point center;
    double radius;
};

// Bodies whose shadow could touch this world at all, found once per frame so the per-pixel loop
// below can skip the whole question the overwhelming majority of the time -- and, when there is
// an eclipse, usually has exactly one body to consider.
static int gather_shadow_casters(CelestialObject *cel, CelestialObject *lightcen,
    ShadowCaster *out, int maxn)
{
    if (!cel || !lightcen || lightcen == cel) return 0;

    Point cpos = cel->location.local_position;
    Point to_light = lightcen->location.local_position - cpos;
    double d_light = to_light.magnitude();
    double r_light = lightcen->get_equatorial_radius();
    if (d_light <= 0 || r_light <= 0) return 0;

    double light_ang = asin(fmin(1.0, r_light / d_light));
    Point lhat = to_light * (1.0 / d_light);
    double bounding_r = cel->get_equatorial_radius();

    int lo, hi;
    candidates_for_light(lightcen, lo, hi);

    int n = 0;
    for (int c = lo; c < hi; c++)
    {
        const EclipseCandidate &cand = eclipse_candidates[c];
        if (cand.obj == cel || cand.obj == lightcen) continue;

        Point rel = cand.obj->location.local_position - cpos;
        double along = rel.x*lhat.x + rel.y*lhat.y + rel.z*lhat.z;
        if (along <= 0) continue;                       // casts its shadow away from us

        Point perp = rel - lhat*along;
        double h = perp.magnitude();
        // Same reach test as the disc path: the caster's own width, plus how far the penumbra has
        // spread over the distance travelled, plus the target's own radius since the shadow only
        // has to reach some part of it.
        if (h >= cand.radius + along*tan(light_ang) + bounding_r) continue;

        if (n < maxn) out[n++] = { cand.obj, cand.obj->location.local_position, cand.radius };
    }
    return n;
}

// Fraction of the light source hidden as seen from one point in space -- one point of a map for
// the sun clock, one sample of a body's cross-section for the magnitude below.
// Taken by value, not by const reference: Point's own arithmetic operators are not const-
// qualified, so a const Point cannot be subtracted from anything. Same convention as
// find_3D_angle() and the rest of point.cpp's interface.
static double point_obscuration(Point surface, Point lightpos, double light_r,
    ShadowCaster *casters, int n)
{
    Point to_light = lightpos - surface;
    double d_light = to_light.magnitude();
    if (d_light <= 0 || light_r <= 0) return 0;

    double light_ang = asin(fmin(1.0, light_r / d_light));
    double inv = 1.0 / d_light;
    double lx = to_light.x*inv, ly = to_light.y*inv, lz = to_light.z*inv;

    double worst = 0;
    for (int i = 0; i < n; i++)
    {
        Point rel = casters[i].center - surface;
        double dist = rel.magnitude();
        if (dist <= 0) continue;
        double ang = asin(fmin(1.0, casters[i].radius / dist));
        double cosine = (rel.x*lx + rel.y*ly + rel.z*lz) / dist;
        double sep = acos(fmin(1.0, fmax(-1.0, cosine)));
        worst = fmax(worst, cpu_disc_overlap(light_ang, ang, sep));
    }
    return worst;
}

// ---- Eclipses seen from far off ------------------------------------------------------------
//
// A body small enough on screen to be a dot has no surface to shade, but it still goes dark when
// it walks into somebody's shadow, and the only number a dot has to say so with is its apparent
// magnitude. This is what makes a moon of Jupiter blink out as it crosses behind its planet, and
// what takes the glare off the full Moon halfway through a lunar eclipse.
//
// What separates this from eclipse_obscuration(), which asks the same question on the observer's
// behalf, is that a magnitude measures *total* light. So the answer cannot be the obscuration at
// one point; it has to be averaged over the whole cross-section the body turns toward its star,
// and that average is the entire difference between the two eclipses of the Earth-Moon pair.
// Earth's shadow is far wider than the Moon and takes essentially all of its light: the Moon
// falls from magnitude -12.7 to somewhere around zero, which is a dull copper dot with no flare
// left on it at all. The Moon's shadow on Earth is a spot a hundred kilometres wide on a disc
// twelve thousand across; it can never cost Earth more than the few percent of the sunbeam the
// Moon's own disc intercepts, which is a couple hundredths of a magnitude -- nothing. Sampling
// the cross-section rather than the center is what gets both of those right from one rule.

// Sunlight that a world's own air refracts into its shadow, as a fraction of what falls outside
// it. Unlike kUmbraLight further up -- which is a legibility figure, chosen so that an eclipsed
// surface stays visible on a screen -- this one is meant to be the photometry, because a
// magnitude is a measurement and reads as wrong if it is not. A totally eclipsed Moon lands
// between about magnitude -1 and +1 against the -12.7 of a full one, so the light reaching it is
// down by some twelve or thirteen magnitudes. This figure is scaled by how much air the caster
// actually has (see umbra_flux below), and 1e-5 is what puts Earth's own atmosphere, at its one
// bar, in the middle of that range. Turn it down for darker eclipses -- they genuinely vary, and
// a stratosphere full of volcanic dust has taken the Moon to +4 -- and up for brighter ones.
static const double kUmbraFluxFraction = 1e-5;

// How the cross-section gets sampled: rings of points spread so that each stands for an equal
// share of the area, since each equal patch of that disc intercepts an equal share of the
// starlight and the plain average over them is therefore the fraction of the light that survives.
// Enough points to put a smooth curve under a partial eclipse, few enough that a per-body,
// per-frame loop costs nothing on the overwhelming majority of frames, which have no eclipse in
// them at all and never reach the loop.
static const int kShadowRings = 4, kShadowSpokes = 8;

// What one caster's own atmosphere lets into the middle of its shadow, as a fraction of direct
// starlight. Airless bodies return 0 and throw the hard black shadow they really do -- which is
// why a moon vanishing behind an airless world vanishes completely, while the Moon in Earth's
// shadow only reddens.
static double umbra_flux(CelestialObject *caster)
{
    cel_obj_class cls = caster->typeclass();
    if (cls != class_planet && cls != class_moon) return 0;

    double pressure = ((Planet*)caster)->get_surface_pressure();
    if (pressure <= 0) return 0;

    // Saturating in pressure for the same reason as the shader's copy of this thought: past about
    // an Earth atmosphere, more air does not put more light into the shadow, it puts less, a
    // thick enough atmosphere being simply opaque.
    return kUmbraFluxFraction * (1.0 - exp(-pressure / oneatm));
}

double eclipse_illumination(CelestialObject *cel)
{
    if (!cel || eclipse_candidates.empty()) return 1;

    cel_obj_class cls = cel->typeclass();
    if (cls != class_planet && cls != class_moon) return 1;     // a star is not lit by anything

    CelestialObject *lightcen = cel->get_light_center();
    if (!lightcen || lightcen == cel) return 1;

    ShadowCaster casters[max_eclipse_casters];
    int n = gather_shadow_casters(cel, lightcen, casters, max_eclipse_casters);
    if (!n) return 1;                                           // nearly always, and nearly free

    Point pos = cel->location.local_position;
    Point light_pos = lightcen->location.local_position;
    double light_r = lightcen->get_equatorial_radius();
    double body_r = cel->get_equatorial_radius();
    Point to_light = light_pos - pos;
    double d_light = to_light.magnitude();
    if (d_light <= 0 || light_r <= 0 || body_r <= 0) return 1;

    // The most generous shadow on offer sets the floor: if two bodies are somehow both covering
    // the star, the light refracted around the one with air still arrives.
    double floor_flux = 0;
    for (int i = 0; i < n; i++) floor_flux = fmax(floor_flux, umbra_flux(casters[i].obj));

    // Two unit vectors across the line to the star, spanning the disc the starlight falls on.
    // Any pair will do -- the samples are averaged, so where the pattern is clocked does not
    // matter -- so this just crosses the light direction with whichever axis it is least parallel
    // to, which cannot degenerate.
    Point lhat = to_light * (1.0 / d_light);
    Point ref = (fabs(lhat.x) < 0.9) ? Point(1, 0, 0) : Point(0, 1, 0);
    Point u(lhat.y*ref.z - lhat.z*ref.y, lhat.z*ref.x - lhat.x*ref.z, lhat.x*ref.y - lhat.y*ref.x);
    u.scale(1.0);
    Point v(lhat.y*u.z - lhat.z*u.y, lhat.z*u.x - lhat.x*u.z, lhat.x*u.y - lhat.y*u.x);

    double lit = 0;
    for (int ring = 0; ring < kShadowRings; ring++)
    {
        // Equal-area radii: ring k of n sits at sqrt((k+0.5)/n) of the way out, which puts the
        // same amount of cross-section behind every sample and lets them be averaged unweighted.
        double rr = body_r * sqrt((ring + 0.5) / kShadowRings);
        for (int spoke = 0; spoke < kShadowSpokes; spoke++)
        {
            // Alternate rings are clocked half a step round, so the samples do not line up into
            // spokes that a shadow edge could cross all at once.
            double th = 2 * _pi * (spoke + 0.5*(ring & 1)) / kShadowSpokes;
            Point sample = pos + u*(rr*cos(th)) + v*(rr*sin(th));
            lit += fmax(1.0 - point_obscuration(sample, light_pos, light_r, casters, n), floor_flux);
        }
    }
    lit /= (kShadowRings * kShadowSpokes);

    // Floored well below anything that can still be seen, purely so that an airless world's
    // shadow -- which lets through nothing whatsoever -- turns into a large number of magnitudes
    // rather than an infinite one.
    return fmax(lit, 1e-12);
}

void refresh_eclipse_casters()
{
    eclipse_candidates.clear();
#if !ALIENORUM_GPU_SPHERES
    return;                     // only the GPU impostor path shades eclipses
#else
    for (int i = 0; cels[i] && i < MAX_CELOBJS; i++)
    {
        CelestialObject *c = cels[i];
        if (c->deleted) continue;

        cel_obj_class cls = c->typeclass();
        // Planets and moons only. Stars are excluded because a system's star is the light
        // source itself, not an occluder (a companion star crossing in front of the primary is
        // real but is its own rendering problem, not this one). Satellites are excluded on
        // size: an object a few meters across casts a shadow a few meters across, which is
        // below one pixel from any distance the body it orbits is still on screen.
        if (cls != class_planet && cls != class_moon) continue;

        // Minor planets are skipped for both reasons at once -- an asteroid's shadow is never
        // resolvable, and astorb can load hundreds of thousands of them, which is exactly the
        // cost this list is meant to avoid paying per disc per frame.
        if (cls == class_planet && ((Planet*)c)->asteroid_no) continue;

        double radius = c->get_equatorial_radius();
        if (radius <= 0) continue;

        CelestialObject *lc = c->get_light_center();
        if (!lc || lc == c) continue;

        eclipse_candidates.push_back({c, lc, radius});
    }

    // Grouped by light source, so that everything downstream can take the slice belonging to one
    // star instead of walking the whole list -- see candidates_for_light(). The order within a
    // group is the pointer order of an unstable sort and so is not meaningful, which is fine:
    // every consumer either takes the deepest shadow of the lot or ranks them itself.
    std::sort(eclipse_candidates.begin(), eclipse_candidates.end(),
        [](const EclipseCandidate &a, const EclipseCandidate &b) { return a.lightcen < b.lightcen; });
#endif
}

// Fills in->casters/num_casters/light_angular_radius for one object about to be drawn: the
// bodies whose shadow actually reaches it, at most max_eclipse_casters of them, expressed
// relative to its own center in camera space. camera_space is the object's true (unrefracted)
// camera-space position, since a shadow is cast between two physical bodies and knows nothing
// about the light path to the observer -- see draw_sphere_gpu()'s own display_space comment.
static void collect_eclipse_casters(SphereImpostorInput &in, CelestialObject *cel,
    CelestialObject *lightcen, const Point &camera_space, double bounding_r)
{
    in.num_casters = 0;
    in.light_angular_radius = 0;
    if (!lightcen || lightcen == cel || eclipse_candidates.empty()) return;

    Point to_light = lightcen->tmprel - cel->tmprel;
    double d_light = to_light.magnitude();
    double r_light = lightcen->get_equatorial_radius();
    if (d_light <= 0 || r_light <= 0) return;

    double light_ang = asin(fmin(1.0, r_light / d_light));
    Point lhat = to_light * (1.0 / d_light);

    // Selected casters, kept sorted by `score` (see below) so that when more than
    // max_eclipse_casters qualify, the ones dropped are the ones grazing the object's edge
    // rather than the one sitting squarely on it.
    double scores[max_eclipse_casters];
    CelestialObject *chosen[max_eclipse_casters];
    int n = 0;

    // Only the bodies lit by this same star: a different system, or a different star of this one,
    // cannot be throwing this shadow. That filter is the slice, not a test in the loop.
    int lo, hi;
    candidates_for_light(lightcen, lo, hi);

    for (int c = lo; c < hi; c++)
    {
        const EclipseCandidate &cand = eclipse_candidates[c];
        if (cand.obj == cel || cand.obj == lightcen) continue;

        Point rel = cand.obj->tmprel - cel->tmprel;
        double along = rel.x*lhat.x + rel.y*lhat.y + rel.z*lhat.z;
        if (along <= 0) continue;                       // behind us with respect to the light: its shadow points away

        double dist = rel.magnitude();
        // A caster far smaller (angularly) than the star it crosses can only ever hide a sliver
        // of the disc -- 1% of it at this cutoff -- which is a dimming no one can see, and the
        // slot it would occupy is better kept for a caster that matters. This is what stops a
        // distant outer moon from crowding out the inner one actually casting the shadow.
        if (asin(fmin(1.0, cand.radius / dist)) < 0.1 * light_ang) continue;

        // Perpendicular distance from this object's center to the caster's shadow axis, against
        // how wide the shadow has spread by the time it arrives. The penumbra widens away from
        // the caster at the light's own angular radius (that spreading *is* the penumbra), and
        // bounding_r is added because the shadow only has to reach some part of the object, not
        // its center.
        Point perp = rel - lhat*along;
        double h = perp.magnitude();
        double reach = cand.radius + along*tan(light_ang) + bounding_r;
        if (h >= reach) continue;

        double score = h / reach;                       // 0 = dead centre, 1 = just grazing
        int at = n;
        while (at > 0 && scores[at-1] > score) at--;
        if (at >= max_eclipse_casters) continue;         // full, and this one is the least central
        // Shift the tail down one slot to open `at`, dropping the last entry if the list is
        // already full. n is the first free slot when it isn't.
        for (int k = (n < max_eclipse_casters) ? n : (max_eclipse_casters-1); k > at; k--)
        {
            scores[k] = scores[k-1];
            chosen[k] = chosen[k-1];
        }
        scores[at] = score;
        chosen[at] = cand.obj;
        if (n < max_eclipse_casters) n++;
    }

    for (int i = 0; i < n; i++)
    {
        // Same transform chain the object's own center goes through, so the difference below is
        // a genuine camera-space offset between the two bodies.
        Point cam = rotate3D(
            rotate3D(to_viewer_plane(chosen[i]->tmprel), center, yaxis, -(azimuth + azimuth_correction)),
            center, xaxis, altitude);
        in.casters[i].dx = cam.x - camera_space.x;
        in.casters[i].dy = cam.y - camera_space.y;
        in.casters[i].dz = cam.z - camera_space.z;
        in.casters[i].r = chosen[i]->get_equatorial_radius();

        // What this caster's own air lets into its shadow. An airless caster leaves this at 0
        // and casts the hard black umbra it really does -- the difference between a moon's
        // shadow crossing a planet, which stays sharp and dark, and the planet's shadow
        // swallowing that moon, which glows.
        in.casters[i].umbra_tint[0] = in.casters[i].umbra_tint[1] = in.casters[i].umbra_tint[2] = 0;
        in.casters[i].umbra_light = 0;
        cel_obj_class ccls = chosen[i]->typeclass();
        if (ccls == class_planet || ccls == class_moon)
        {
            Planet *cpl = (Planet*)chosen[i];
            double pressure = cpl->get_surface_pressure();
            if (pressure > 0)
            {
                double high[3], low[3], umbra[3];
                atmosphere_colors(cpl, high, low, umbra);
                // Saturating in pressure, not proportional: past about an Earth atmosphere, more
                // air makes the light redder (which the colors above already say) but not
                // brighter -- past a point it makes it dimmer, since a thick enough atmosphere
                // is simply opaque. Venus casts a darker shadow than Earth, not a brighter one.
                double thickness = 1.0 - exp(-pressure / oneatm);
                for (int k = 0; k < 3; k++) in.casters[i].umbra_tint[k] = umbra[k];
                in.casters[i].umbra_light = kUmbraLight * thickness;
            }
        }
    }
    in.num_casters = n;
    if (n) in.light_angular_radius = light_ang;
}

// ---- Atmospheric color -----------------------------------------------------------------
//
// What a world's air looks like, both from outside it (the lit band on its limb) and from
// inside its own shadow (the copper light an eclipse falls into). Both come from one place: the
// Rayleigh/particulate mix draw_sky_gradient() already paints that world's sky with when you
// stand on it, so a planet whose skies are butterscotch has a butterscotch limb and throws a
// butterscotch shadow, with nothing tuned twice to say so.
//
// The two directions differ only in path length. Looking *at* the air high on the limb, light
// has been scattered towards us over a short path, and we see the scattered color directly --
// blue on Earth, for exactly the reason the sky is. Looking *through* it, along a path grazing
// the whole limb, the same scattering has removed that blue on the way, and what survives is
// the complement: the transmission exp(-depth * scattering), which is why sunsets are red and
// why the shadow behind a world is red rather than black. So both colors below come from one
// pair of numbers -- how strongly this air scatters, and how much of it the light crossed.
//
// out_high  = scattered color, for the top of the limb band.
// out_low   = transmitted color at grazing incidence, for the bottom of it (sunset colors).
// out_umbra = transmitted color over a path twice as long again -- light that had to graze the
//             limb and bend inwards to reach the shadow at all.
// Each is normalized to a brightest channel of 1 and then scaled by how much air there is, so
// a thin atmosphere is not merely paler but dimmer.
static void atmosphere_colors(Planet *pl, double out_high[3], double out_low[3], double out_umbra[3])
{
    for (int i = 0; i < 3; i++) out_high[i] = out_low[i] = out_umbra[i] = 0;

    double pressure = pl->get_surface_pressure();
    if (pressure <= 0) return;

    // For gas giants, ice giants, and overcast worlds, most of that atmosphere is below the
    // opaque part, below the cloud tops.
    if (pl->type == gas_giant || pl->type == ice_giant || pl->type == hot_jupiter
        || (pressure >= 5*oneatm && pl->type != clearskies)
        )
        pressure = fmin(5*oneatm, pressure/100);

    // Matches draw_sky_gradient() exactly: Rayleigh scattering in the fixed 0.37/0.58/0.81 blue-
    // weighted ratio, crossfaded against the world's own color where particulates (dust, haze,
    // smog) scatter greyly instead and hand the sky the ground's color back.
    double particulates = pl->get_particulates();
    double Rayleigh = 1.0 - particulates;
    Color pcol = Color::color_from_magnitude_indices(0, pl->BV_color);
    pcol.normalize(1);

    const double scatter[3] = {0.37, 0.58, 0.81};
    double haze[3] = {pcol.red, pcol.green, pcol.blue};
    double high[3], low[3], umbra[3];

    // How much air a grazing ray crosses, in units where 1 is roughly Earth's. Compressed hard
    // (a fourth root) because pressure ranges over orders of magnitude between the worlds this
    // app carries -- Mars at 0.006 atm and Venus at 92 -- while the color it produces does not.
    double depth = kAtmosphereOpticalDepth * fmin(3.0, fmax(0.2, pow(pressure / oneatm, 0.25)));

    for (int i = 0; i < 3; i++)
    {
        high[i] = Rayleigh*scatter[i] + particulates*haze[i];
        low[i] = exp(-depth * scatter[i]);
        umbra[i] = exp(-2.0 * depth * scatter[i]);
    }

    // Particulates redden a long path too, but they do it greyly -- they scatter all colors
    // much alike -- so they get folded into the transmitted colors as the world's own hue
    // rather than through the wavelength-dependent exponential above.
    for (int i = 0; i < 3; i++)
    {
        low[i] = Rayleigh*low[i] + particulates*haze[i]*low[i];
        umbra[i] = Rayleigh*umbra[i] + particulates*haze[i]*umbra[i];
    }

    // Amount of air, as an overall brightness: 1 for an Earth-like atmosphere and up, falling
    // away for a thin one so Mars's limb is a faint line where Earth's is a bright thread.
    double amount = fmin(1.0, pow(pressure / oneatm, 0.25));

    double *outs[3] = {out_high, out_low, out_umbra};
    double *ins[3] = {high, low, umbra};
    for (int k = 0; k < 3; k++)
    {
        double mx = fmax(ins[k][0], fmax(ins[k][1], ins[k][2]));
        if (mx <= 0) continue;
        for (int i = 0; i < 3; i++) outs[k][i] = (ins[k][i] / mx) * amount;
    }
}

// The ring plane's normal in camera space -- the planet's local +Y (polar) axis, rotated
// forward into camera space by the *same* forward chain a position goes through (tilt, then
// to_viewer_plane, then camera azimuth/altitude), applied to a direction instead of a position
// (so the translation step, "+= cel->tmprel", is correctly skipped -- directions aren't
// translated). No spin term: the CPU ring code never rotates ring geometry by timeofday() at
// all, rings not spinning with the planet.
//
// Do NOT reach for draw_sphere_gpu()'s "undo_to_local" helper here, with or without its spin
// step. That helper computes something genuinely different: applying the *inverse*-ordered
// chain to a standard basis vector e_i returns R^-1*e_i, i.e. row i of the forward rotation
// matrix R -- correct for its actual purpose (the sphere fragment shader reconstructs R^-1*n as
// n.x*basisX + n.y*basisY + n.z*basisZ, which only works out because each basis vector is a
// *row* of R used as a *column* of that reconstruction -- a transpose identity, not a literal
// "axis expressed in camera space"). What a ring plane requires is a genuine forward transform,
// R*(0,1,0) -- a different vector from R^-1*(0,1,0) whenever R isn't symmetric, which is
// generally the case. An earlier version of draw_ring_gpu() used the inverse version and
// produced a ring plane that visibly wobbled with camera azimuth/altitude (bug: rings
// misaligned with the visible disc, plane appearing to flip depending on viewing angle), since
// R^-1*(0,1,0) has no reason to track the camera's own orientation the way R*(0,1,0) does.
//
// Shared by draw_ring_gpu() (which draws the rings) and draw_sphere_gpu() (which shadows the
// planet with them) precisely so the two can never disagree about where the ring plane is: a
// ring shadow that does not line up with the ring casting it is worse than no shadow at all.
static Point ring_plane_normal(CelestialObject *cel)
{
    return rotate3D(
        rotate3D(
            to_viewer_plane(rotate3D(Point(0, 1, 0), center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a)),
            center, yaxis, -(azimuth + azimuth_correction)),
        center, xaxis, altitude);
}

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

    if (view_mode == vm_system) camera_space = display_space = Point(0, 0, R * 10);

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
    // std::cout << cel->name << " bounding_r=" << bounding_r << std::endl;

    spawn_texture_load(cel);

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

    double limb_a = 0, limb_b = 0;
    if (self_luminous)
    {
        if (cel->typeclass() == class_star) ((Star*)cel)->limb_darkening_coefficients(limb_a, limb_b);
        else { limb_a = 0.49; limb_b = 0.21; }          // repli de type solaire
    }

    Point light_dir(0, 0, 1);
    if (!self_luminous)
    {
        if (view_mode == vm_system)
        {
            light_dir = Point(0,0,-AU);
        }
        else
        {
            Point light_camera_space = rotate3D(
                rotate3D(to_viewer_plane(lightcen->tmprel), center, yaxis, -(azimuth + azimuth_correction)),
                center, xaxis, altitude);
            light_dir = light_camera_space - camera_space;
            double mag = light_dir.magnitude();
            if (mag > 0) light_dir = light_dir * (1.0 / mag);
        }
    }

    Color daylight = Color::color_from_magnitude_indices(0, lightcen->BV_color);
    double dmax = fmax(fmax(daylight.red, daylight.green), daylight.blue);
    if (dmax > 0)
    {
        daylight.red /= dmax; daylight.green /= dmax; daylight.blue /= dmax;
    }
    // Compensate for the eye's white balance adjustment (matches the CPU path exactly).
    daylight.red = pow(daylight.red, 1.0/3.0);
    daylight.green = pow(daylight.green, 1.0/3.0);
    daylight.blue = pow(daylight.blue, 1.0/3.0);

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
    in.limb_a = limb_a;
    in.limb_b = limb_b;
    in.night_illum = cel->night_map ? 0.0 : starlight;
    in.redlight_mode = redlight_mode;
    if (self_luminous) { in.num_casters = 0; in.light_angular_radius = 0; }
    else collect_eclipse_casters(in, cel, lightcen, camera_space, bounding_r);

    // The band of lit air on this world's own limb. Its height is the world's own pressure
    // scale height (so a hydrogen giant's is puffy and Mars's is thin), and its colors come from
    // the same place its skies do -- see atmosphere_colors() above.
    in.atmosphere_height = 0;
    for (int k = 0; k < 3; k++) in.atmosphere_color[k] = in.atmosphere_low_color[k] = 0;
    if (!self_luminous && (cls == class_planet || cls == class_moon))
    {
        Planet *pl = (Planet*)cel;
        double scale_height = pl->estimate_scale_height();
        if (scale_height > 0)
        {
            in.atmosphere_height = kAtmosphereScaleHeights * scale_height;
            double umbra_unused[3];
            atmosphere_colors(pl, in.atmosphere_color, in.atmosphere_low_color, umbra_unused);
        }
    }

    // A ringed planet shadowed by its own rings -- Saturn's dark band across the winter
    // hemisphere. Deliberately not gated on use_gpu_ring (see its comment in draw_sphere()):
    // that flag says whether the *rings* are drawn analytically this frame, while the shadow
    // they throw is a property of the planet's own surface and belongs with the disc either
    // way. Handed the same plane normal and the same opacity map the ring impostor draws with.
    in.ring_normal[0] = in.ring_normal[1] = in.ring_normal[2] = 0;
    in.ring_inner_r = in.ring_outer_r = 0;
    in.ringx_map_texture = 0;
    if (!self_luminous && cls == class_planet && ((Planet*)cel)->ring_radius > R)
    {
        Point ring_normal = ring_plane_normal(cel);
        in.ring_normal[0] = ring_normal.x;
        in.ring_normal[1] = ring_normal.y;
        in.ring_normal[2] = ring_normal.z;
        in.ring_inner_r = R;
        in.ring_outer_r = ((Planet*)cel)->ring_radius;
        in.ringx_map_texture = gputex_for(cel->ringx_map);
    }

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

    double xmin=1e29, ymin=1e29, xmax=0, ymax=0;
    bool ok = queue_sphere_impostor(in,
        (view_mode == vm_system) ? (arad*10.0/dispcx) : zoom,
        (view_mode == vm_system) ? cel->drawnx : dispcx,
        (view_mode == vm_system) ? cel->drawny : dispcy,
        dispcx,
        &xmin, &ymin, &xmax, &ymax);
    // std::cout << cel->name << ": " << xmin << "," << ymin << " ~ " << xmax << "," << ymax << std::endl << std::endl;
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

    // Standing on the ringed world itself -- draw_horizon()'s call, and the usual way these rings
    // are ever seen -- cel_azrot points from the observer straight down at the planet's own centre.
    // That is not a body in the sky whose light bends on its way in; it is the geometric anchor of
    // the ring plane, so refracting it models nothing. It is also exactly the degenerate input
    // refract_true_point() now guards against (see its comment in planet.cpp): the rotation axis is
    // a cross product that has vanished into rounding noise. Either change alone stops the ring
    // from jumping around the screen; this one records that the question should never have been
    // asked. Rings on a planet viewed from anywhere else still refract as before.
    bool standing_on_it = (whereami >= 0 && cel->seqno == whereami);
    Point display_space = (view_mode == vm_horizon && !standing_on_it)
        ? rotate3D(refract_true_point(cel_azrot), center, xaxis, altitude)
        : camera_space;
    double R = cel->get_equatorial_radius();

    // See ring_plane_normal() above for what this is and why it is emphatically not the same
    // vector as the sphere impostor's own basisY. The planet's disc shader is handed the very
    // same normal, to shadow the planet with these rings.
    Point normal = ring_plane_normal(cel);

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
    in.oblateness = cel->oblateness;
    in.ring_map_texture = gputex_for(cel->ring_map);
    in.ringx_map_texture = gputex_for(cel->ringx_map);
    in.fallback_color = IM_COL32(225, 208, 192, 255);   // matches the CPU path's default rgb
    in.light_dir[0] = light_dir.x; in.light_dir[1] = light_dir.y; in.light_dir[2] = light_dir.z;
    in.self_luminous = self_luminous;
    in.amt_lit = pl->amt_lit;
    in.redlight_mode = redlight_mode;

    queue_ring_impostor(in, zoom, dispcx, dispcy, dispcx);
}

// ---- Releasing the universe --------------------------------------------------------------
//
// Flying into a star unlinks every object but the Sun from `cels` (see draw_sphere() below) and
// leaves the app in its "no universe" state. That unlinking used to be the whole of it, which
// orphaned the objects rather than freeing them: half a million CelestialObjects, their Orbits,
// their Locale arrays, and their Maps -- and the Maps are the part that matters, since a single
// 10000x5000 surface map is 150 MB of channel data against a few hundred bytes for the object
// that owns it.
//
// It cannot simply delete them on the spot, for two reasons, and the pair of functions below is
// what each of them costs:
//
//   * Other things hold raw pointers into those objects -- hdcache, hipcache, the
//     constellation and first-letter indices, the constellation line endpoints, the astorb and
//     comet catalog rows, mycenobj. A dangling entry there is strictly worse than the leak being
//     fixed: find_object() would go on handing back seqno values whose cels[] slot is now null,
//     and whereami/selected get set straight from those. So the forgetting is not separable from
//     the freeing, and release_universe_objects() does both halves at once.
//
//   * A detached load_textures() thread writes into the object it was handed -- its maps, its
//     has_real_maps flag -- for as long as it runs, and generating a procedural map can run for
//     minutes (hence the "this may take a few minutes" notice in the map editor). Freeing an
//     object while one of those is in flight turns a leak into a use-after-free. So the objects
//     are handed to reap_released_objects(), which the main loop calls once a frame and which
//     does nothing until texture_loads_pending has come back down. Waiting by returning rather
//     than by sleeping is what keeps the render thread out of it.
static std::vector<CelestialObject*> released_objects;

void release_universe_objects()
{
    // Collected before the caller unlinks them -- afterwards there is no way left to find them.
    for (int i = 1; cels[i] && i < MAX_CELOBJS; i++)
        if (cels[i] != cels[0]) released_objects.push_back(cels[i]);

    if (hdcache)  memset(hdcache,  0, sizeof(Star*) * (MAX_HD+1));
    if (hipcache) memset(hipcache, 0, sizeof(Star*) * (MAX_HIP+1));
    constellation_index.clear();
    first_letter_index.clear();
    for (Constellation &c : constellations)
        for (ConsLine &ln : c.lines) ln.a = ln.b = nullptr;
    for (AstorbRow &row : astorb) row.cel = nullptr;
    for (CometRow &row : comets) row.cel = nullptr;

    // Rebuilt from scratch by refresh_eclipse_casters() at the top of every
    // compute_object_draw_coordinates(), but it is holding pointers right now -- this runs
    // mid-frame, from inside draw_objects().
    eclipse_candidates.clear();

    last_xplored_cen = last_neighb_cen = nullptr;

    // compute_object_draw_coordinates() only reassigns mycenobj when whereami >= 0, and the
    // caller has just set whereami to -1, so this would otherwise keep pointing at a released
    // object -- and it is dereferenced unguarded on the next frame.
    mycenobj = cels[0];
}

void reap_released_objects()
{
    if (released_objects.empty()) return;
    if (texture_loads_pending > 0) return;      // try again next frame

    // Keyed on Map*, and every Map below is about to stop existing. Left alone, a later Map
    // allocated at the same address would collide with the stale entry and draw its texture.
    gputex_clear_cache();

    // StarMulti objects are shared between the members of a multiple system, so unlink() -- which
    // nulls the pointer in every member -- is what stops the second member of a pair from
    // deleting one the first already did. Same order, and the same reasoning, as main()'s own
    // teardown on the way out.
    for (CelestialObject *c : released_objects)
    {
        if (c->typeclass() == class_star && ((Star*)c)->multisys)
        {
            StarMulti *sm = ((Star*)c)->multisys;
            sm->unlink();
            delete sm;
        }
    }

    for (CelestialObject *c : released_objects)
    {
        // Nulled after deleting rather than left to the destructors: ~Star, ~Planet and ~Comet
        // each free the orbit, but ~Galaxy and ~Satellite do not exist and ~CelestialObject is
        // defaulted, so doing it here is the only way to cover all five without knowing which is
        // which. The destructors that do free it then see nullptr and leave it alone.
        delete c->orbit;
        c->orbit = nullptr;

        // Nothing frees the maps or the locales at all -- not any destructor, not main(). The
        // aliasing pass is defensive: nothing currently points two of these at one Map, and a
        // double delete would be a far worse regression than the leak being fixed here.
        Map *maps[5] = { c->surf_map, c->cloud_map, c->night_map, c->ring_map, c->ringx_map };
        for (int m = 0; m < 5; m++)
        {
            if (!maps[m]) continue;
            for (int n = m+1; n < 5; n++) if (maps[n] == maps[m]) maps[n] = nullptr;
            delete maps[m];
        }
        c->surf_map = c->cloud_map = c->night_map = c->ring_map = c->ringx_map = nullptr;

        delete[] c->locales;
        c->locales = nullptr;
        c->nlocales = 0;

        delete c;
    }

    released_objects.clear();
    released_objects.shrink_to_fit();
    std::cout << "Released destroyed universe." << std::endl;
}

int draw_sphere(CelestialObject* cel, double arad)
{
    if ((cel->seqno == whereami) && (view_mode != vm_system)) return 0;
    if (view_mode == vm_system) cel->tmprel = Point(AU, 0, 0);
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
                    // Take ownership of everything about to be unlinked, and drop every cached
                    // pointer into it, before the wipe below makes it unreachable. The wipe
                    // itself, and everything else this branch does, is unchanged -- see
                    // release_universe_objects() for why the actual freeing happens later.
                    release_universe_objects();
                    memset( &cels[1], 0, (MAX_CELOBJS-2) * sizeof(CelestialObject*) );
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
                    here.galactic_center = cel->location.galactic_center;
                    here.system_center = cel->location.system_center;
                    here.equatorial_plane = cel->location.equatorial_plane;
                    viewer_lon = cel->RA_as_radians(here, cel->timeofday()) - _pi;
                    viewer_lat = -cel->Decl_as_radians(here);
                    viewer_tz = 0;
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
    bool wireframe = (view_mode != vm_system) && (dragging || !cel->onscreen || d < cel->volumetric_mean_radius);
    if (whereami<0 || cels[whereami]->type != artificial) cel->onscreen = false;

    bool use_gpu_disc = false, use_gpu_ring = false;
#if ALIENORUM_GPU_SPHERES
    // vm_skymap isn't a pinhole camera (see Cartesian2D in point.cpp), so the camera-space
    // math draw_sphere_gpu() relies on doesn't apply there; fall through to the CPU path.
    use_gpu_disc = (view_mode == vm_system) || (!wireframe && view_mode != vm_skymap);

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
    // lastm carries the previous iteration's m across the mesh loop, and the gap-filling quad
    // below reads it before anything has written it: the block that assigns it is gated on
    // perline, which is zero for the whole of the first latitude band, so the first iteration of
    // the second band read an indeterminate value. Zero is the conservative start -- the gap fill
    // it guards is bounds-checked independently ("m > 1 && m < l"), so the worst a stale compare
    // can now do on that one iteration is paint a redundant quad over vertices that are really
    // there, instead of branching on stack garbage.
    int i, j, l, m, lastm = 0, n, result=0;
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
    daylight.red = pow(daylight.red, 1.0/3.0);
    daylight.green = pow(daylight.green, 1.0/3.0);
    daylight.blue = pow(daylight.blue, 1.0/3.0);

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

    // is_day and is_night are read after the mesh loop, by the polar-cap fill, which the comment
    // there describes as relying on "values left over from the last iteration" -- but the loop
    // can finish without ever reaching the branch that assigns them, and both are read again by
    // the sky blend inside it. Starting from full daylight is the neutral choice: it is what a
    // body with no light center already gets, and it keeps the cap the same colour as the disc it
    // sits on rather than whatever was on the stack.
    double equatorial_radius, theta, vtheta, cos_theta, cos_vtheta, is_day = 1, is_night = 0;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * .5;
    else
        equatorial_radius = cel->get_equatorial_radius();

    double lat, lon, z_cutoff = d + equatorial_radius * 0.2, obl = 1.0 - cel->oblateness;

    if (!wireframe) spawn_texture_load(cel);

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

    double cpu_limb_a = 0, cpu_limb_b = 0;
    if (self_luminous)
    {
        if (cel->typeclass() == class_star) ((Star*)cel)->limb_darkening_coefficients(cpu_limb_a, cpu_limb_b);
        else { cpu_limb_a = 0.49; cpu_limb_b = 0.21; }
    }

    auto sphere_began = std::chrono::high_resolution_clock::now();
    double step = wireframe
            ? (fiftyseventh*15)
            : ( (worth_using_map && (cel->surf_map || cel->cloud_map))
                ? fmax(fmin(_pi*sphresolution/arad*fiftyseventh, fiftyseventh*15), fiftyseventh*0.2)
                : fiftyseventh * 3
              ),
        stepcoslat, invlaststepcoslat = 1.0 / step;
    int perline=0, dx1, dy1, dx2, dy2;
    double polyr = 0, polyg = 0, polyb = 0, lum, lum1;
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
            lon360 = lonmin_crosses_zero && lon > _pi ? (lon - _pi*2) : lon;
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
                                // Quadratic law, identical to the GPU shader: pow(mu, 1/3) that
                                // was here tumbled to zero at the star's limb and that's not what we want.
                                cos_vtheta = fmax(0.0, cos(vtheta));
                                double om = 1.0 - cos_vtheta;
                                is_day = fmax(0.0, fmin(1.0, 1.0 - cpu_limb_a*om - cpu_limb_b*om*om));
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
                                    is_day = fmin(1, pow(cos_theta, 1.0/3.0) + night_illum);
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

                            // The daylit contribution unconditionally, then the night map on top
                            // of it where there is any night to show. Written as an if/else
                            // before, which left polyr/g/b unassigned in one reachable case --
                            // a body that has a night map on a patch that is fully lit, where
                            // "is_night = 1.0 - is_day" comes out zero and the inner branch is
                            // skipped -- and the sky blend below then read them.
                            is_night = nmap ? (1.0 - is_day) : 0;
                            polyr = is_day*rgblit.r;
                            polyg = is_day*rgblit.g;
                            polyb = is_day*rgblit.b;
                            if (nmap && worth_using_map && is_night > 0)
                            {
                                nrgb = nmap->color_at(maplat, maplon-_pi);
                                polyr += is_night*nrgb.r;
                                polyg += is_night*nrgb.g;
                                polyb += is_night*nrgb.b;
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
    if (whtbkgd)
    {
        if (flare > 53.81) flare = 53.81;               // ogranichit' blik v meste oshibki
    }

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
    if (whtbkgd) rgb.invert_luminance();

    // Four rays around magnitude -10 and dimmer, filling in to a full circle by the Sun.
    double fill = (vmag > -10.0) ? 0.0 : fmin(1.0, (-10.0 - vmag) / 16.0);

    // Glare scatters into a haze either because the source is overwhelmingly bright or
    // because its disc is resolved enough that it stops behaving like a point. Brightness
    // is what dominates: the Sun hazes over even when its disc is only a few pixels wide.
    // Ramps in smoothly from magnitude -1 so Venus and Jupiter pick up a slight haze while
    // the Moon and Sun saturate it.
    double glare = fmax(0.0, -1.0 - vmag);
    double haze = fmin(1.0, fmax(disc_px / 12.0, 1.0 - exp(-glare / 7.0)));

    double base_len = (max_bloomrad * 1.5 + flare * 1.1);
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
        draw_radial_glow(xycoord, fmax(1.0, disc_px * 0.85), disc_px/3 + halo_span, rgb,
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

// The corona, and the only circumstance under which anyone has ever seen it: a star's own
// photosphere covered by something. It is roughly a millionth as bright as the disc it
// surrounds, so it is not that the corona appears during totality -- it is there always, and
// totality is merely the one time the glare stops drowning it.
//
// Drawn before the eclipsing body's own disc, which is nearer and therefore drawn later, and so
// paints over the inner part of this and leaves the ring. `obsc` decides everything: the ramp
// below keeps the corona invisible until the covering is nearly complete, since even one percent
// of the photosphere still showing is thousands of times brighter than the whole corona. That
// same cutoff is why an annular eclipse -- which never exceeds it, the moon being too far away
// to cover the disc at all -- correctly shows nothing.
static void draw_corona(ImVec2 at, double sun_px, double obsc, double BV)
{
    if (whtbkgd) return;
    if (!std::isfinite(sun_px) || !std::isfinite(obsc) || sun_px <= 0) return;

    double strength = pow(fmax(0.0, fmin(1.0, (obsc - 0.97) / 0.03)), 1.5);
    if (strength < 0.02) return;

    // Pearl white, barely carrying the star's own hue: the corona is hot enough that its light
    // is essentially the star's, scattered by free electrons, which is a grey process.
    Color col = Color::color_from_magnitude_indices(0, BV);
    col.normalize(1);
    RGB3Byte rgb;
    rgb.r = (int)(255 * (0.82 + 0.18*col.red));
    rgb.g = (int)(255 * (0.82 + 0.18*col.green));
    rgb.b = (int)(255 * (0.82 + 0.18*col.blue));

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    double r_in = fmax(2.0, sun_px);

    // Two overlapping falloffs rather than one: the inner corona is bright and tight against the
    // limb, the outer faint and reaching several radii out, and a single exponent cannot be both.
    draw_radial_glow(at, r_in*1.01, r_in*2.2, rgb, 150.0*strength, 2.6);
    draw_radial_glow(at, r_in*1.01, r_in*5.0, rgb, 55.0*strength, 1.7);

    // Streamers. The corona is shaped by the star's magnetic field, not by gravity, which is why
    // it is never a smooth halo: it reaches furthest along the field's open lines and leaves
    // gaps where the field is closed. Hashed, not random, so the shape holds still from frame to
    // frame instead of boiling.
    const int nstream = 40;
    for (int k = 0; k < nstream; k++)
    {
        double h1 = flare_hash(k*13 + 5), h2 = flare_hash(k*29 + 71), h3 = flare_hash(k*7 + 311);
        int a = (int)(46.0 * strength * (0.3 + 0.7*h2));
        if (a < 1) continue;
        double ang = (k + (h3 - 0.5)*1.6) * (_pi*2.0/nstream);
        double len = r_in * (0.8 + 4.0*pow(h1, 2.0));
        double halfwidth = r_in * (0.06 + 0.10*h2);
        double dx = cos(ang), dy = sin(ang), px = -dy, py = dx;
        ImVec2 base_a(at.x + dx*r_in*0.98 + px*halfwidth, at.y + dy*r_in*0.98 + py*halfwidth);
        ImVec2 base_b(at.x + dx*r_in*0.98 - px*halfwidth, at.y + dy*r_in*0.98 - py*halfwidth);
        ImVec2 tip(at.x + dx*(r_in + len), at.y + dy*(r_in + len));
        dl->AddTriangleFilled(base_a, base_b, tip, rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, a)));
    }

    // The chromosphere: a thin, reddish rim with a few bright prominences (Baily's beads). The color comes from
    // lines of hydrogen and helium, not thermal blackbody, so it does not change with temperature or star color.
    int bailyr = 255, bailyg = 62, bailyb = 40;
    int lbr = bailyr, lbg = bailyg, lbb = bailyb;
    float ringsz = r_in*1.02, steepness = 1.13;           // steepness must be more than 1 no matter what
    // ImU32 fire = rgba_apply_redlight(IM_COL32(bailyr, bailyg, bailyb, (int)(210*strength)));
    // dl->AddCircle(at, r_in*1.02, fire, 0, fmax(1.0, r_in*0.035));
    for (; ringsz > r_in; ringsz -= 0.5)
    {
        ImU32 fire = rgba_apply_redlight(IM_COL32(lbr, lbg, lbb, (int)(210*strength)));
        dl->AddCircle(at, ringsz, fire, 0);
        // lbr starts out at 255 so don't try to increase it.
        lbg = std::min(255, (int)(lbg*steepness));
        lbb = std::min(255, (int)(lbb*steepness));
    }
    for (int k = 0; k < 10; k++)
    {
        double h1 = flare_hash(k*97 + 17), h2 = flare_hash(k*41 + 233);
        if (h2 < 0.35) continue;                        // not every eclipse gets five of them
        double ang = h1 * _pi * 2.0;
        double reach = r_in * (0.019 + 0.038*h2);

        lbr = bailyr; lbg = bailyg; lbb = bailyb;
        float beadsz = fmax(1.0, reach);
        ImVec2 bailyxy = ImVec2(at.x + cos(ang)*(r_in + reach*0.5), at.y + sin(ang)*(r_in + reach*0.5));
        for (; beadsz > 0.5; beadsz -= 0.5)
        {
            dl->AddCircleFilled(bailyxy, beadsz, rgba_apply_redlight(IM_COL32(lbr, lbg, lbb, (int)(190*strength))), 0);
            // lbr starts out at 255 so don't try to increase it.
            lbg = std::min(255, (int)(lbg*steepness));
            lbb = std::min(255, (int)(lbb*steepness));
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
// A galaxy as a soft, oriented ellipse.
//
// The ellipse is not drawn as a 2D shape rotated by the catalogued position angle: instead the
// galaxy's actual disc -- a circle of radius R lying in the plane that read_UNGC/RC3_catalog built
// from its inclination and position angle -- is projected through the same chain everything else
// uses. The foreshortening then produces the ellipse on its own, at the right angle, and keeps
// producing the right one as the viewer flies around it. A rotated 2D ellipse would be correct only
// from Earth.
//
// Brightness is per unit area rather than total: a galaxy's flux is spread over its whole disc, so
// M31 covering three degrees has to come out far fainter per pixel than a compact one of the same
// magnitude. Without that, every large nearby galaxy renders as a flat white blob.
// Surface brightness at fractional radius f (0 at the nucleus, 1 at the rim) and disc-plane angle
// t, for a galaxy of Hubble stage T. This is what turns the soft ellipse into something that reads
// as a galaxy: a concentrated bulge, an exponential disc, and a pair of logarithmic arms wound at
// a pitch that follows the type -- tight for an Sa, open for an Sc, which is most of what the
// Hubble sequence actually describes.
//
// Everything here is in the disc's OWN polar coordinates, which is why it lands correctly on the
// projected ellipse: the mesh's segment index is the disc angle by construction, and its ring
// index the fractional radius, so the pattern foreshortens along with the disc instead of being
// painted flat onto the screen.
static double galaxy_surface_intensity(double f, double t, double T, bool barred)
{
    // An elliptical has no disc to put arms on -- see the inclination discussion: it is a triaxial
    // spheroid, and its light falls off far more steeply than a disc's.
    if (T < 0) return pow(fmax(0.0, 1.0 - f), 3.4);

    // Pitch angle: about 8 degrees at S0a, opening to roughly 29 by Sd. Arms are logarithmic
    // spirals, so a constant pitch means theta advances with the logarithm of the radius.
    double pitch = (8.0 + 2.6 * fmin(fmax(T, 0.0), 8.0)) * fiftyseventh;

    // The bulge shrinks along the sequence: prominent in an Sa, barely there in an Sd.
    double bulge = fmax(0.05, 0.60 - 0.07 * T) * exp(-f / 0.09);
    double disc = pow(fmax(0.0, 1.0 - f), 2.2);

    double fc = fmax(f, 0.14);                      // the winding runs away as f approaches zero
    double phi = t - log(fc) / tan(pitch);
    double arm = pow(0.5 + 0.5 * cos(2.0 * phi), 2.0);

    // Suppressed inside the bulge, and faded out towards the late types, whose arms break up into
    // patches rather than continuing as a two-armed pattern.
    double inner = fmin(1.0, fmax(0.0, (f - 0.10) / 0.16));
    double late = fmin(1.0, fmax(0.0, 1.0 - (T - 6.0) / 4.0));
    double val = bulge + disc * (0.30 + 0.95 * inner * late * arm);

    if (barred)
    {
        // A quartic in f gives the bar a definite end rather than a fade, which is what makes it
        // read as a bar; the width term is measured across it, hence sin(t)*f.
        double along = exp(-pow(f / 0.34, 4.0));
        double across = exp(-pow(sin(t) * f / 0.055, 2.0));
        val += 0.55 * along * across;
    }

    return val;
}

static double draw_galaxy(CelestialObject* cel, double appmag)
{
    Galaxy *g = (Galaxy*)cel;
    if (g->angular_diameter <= 0) return 0;
    if (inside_galaxy_idx == cel->seqno) return 0;

    g->volumetric_mean_radius = cel->distance * g->angular_diameter * 0.5;
    if (!(g->volumetric_mean_radius > 0)) return 0;

    // In-plane basis: the disc plane's own axes, rotated out into world space. local_system_plane
    // maps world to plane, so the inverse rotation takes the plane's x and z back out.
    Rotation pl = cel->location.local_system_plane;
    Point e1 = rotate3D(xaxis, center, pl.v, -pl.a);
    Point e2 = rotate3D(zaxis, center, pl.v, -pl.a);
    e1.scale(1);
    e2.scale(1);

    // Mesh resolution follows the on-screen size: a galaxy five pixels across gains nothing from
    // 64 segments, and at high zoom the magnitude limit lets hundreds of them through at once.
    // Estimated analytically because the rim below cannot be built until the count is chosen.
    const int kMaxSeg = 72;
    double est_px = g->angular_diameter * zoom * dispcx;
    int nseg = (int)fmin((double)kMaxSeg, fmax(12.0, est_px * 1.1));
    int nring = (int)fmin(18.0, fmax(4.0, est_px * 0.25));

    // Screen extent of the rim, both to size the falloff and to reject the offscreen cheaply.
    double xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30;
    ImVec2 rim[kMaxSeg];
    for (int s = 0; s < nseg; s++)
    {
        double t = s * (_pi * 2.0 / nseg);
        Point p = cel->tmprel + (e1 * (g->volumetric_mean_radius * cos(t))) + (e2 * (g->volumetric_mean_radius * sin(t)));
        p = to_viewer_plane(p);
        if (view_mode == vm_horizon) p = refract_true_point(p);
        Cartesian2D z = Cartesian2D(p, azimuth + azimuth_correction, altitude, zoom);
        rim[s] = ImVec2(dispcx + z.x * dispcx, dispcy + z.y * dispcx);
        if (z.x < -1e4 || z.y < -1e4)
        {
            continue;                 // behind the camera
        }
        if (rim[s].x < xmin) xmin = rim[s].x;
        if (rim[s].x > xmax) xmax = rim[s].x;
        if (rim[s].y < ymin) ymin = rim[s].y;
        if (rim[s].y > ymax) ymax = rim[s].y;
    }

    double wide = xmax - xmin, tall = ymax - ymin;
    // std::cout << cel->name << " wide=" << wide << " tall=" << tall << std::endl;
    if (wide < 1.5 && tall < 1.5) return 0;                     // smaller than a pixel: the point path has it
    if (xmax < 0 || ymax < 0 || xmin > dispcx*2 || ymin > dispcy*2) return 0;

    // Total flux spread over the projected area, then a cap so a big nearby galaxy stays readable.
    double area = fmax(4.0, _pi * wide * tall * 0.25);
    double total = pow(magnbase, -appmag) * global_brightness * zoom * zoom * 1e+4;
    double peak = fmin(210.0, total / area * 255.0);
    if (peak < 2.0) return 0;

    Color col = Color::color_from_magnitude_indices(0, cel->BV_color);
    col.normalize(255);
    RGB3Byte rgb((unsigned char)col.red, (unsigned char)col.green, (unsigned char)col.blue);

    // Type drives the whole pattern; an unknown one is treated as a middling spiral rather than
    // as an elliptical, since that is the commoner shape and the safer-looking mistake.
    double T = g->T_known ? g->morphological_T : 3.0;

    // The RC3 spells the family in the third character of its type string: A unbarred, B barred,
    // X intermediate. The UNGC's own short forms ("Im", "Sph") have nothing there, hence the
    // length check.
    bool barred = (strlen(g->morph_type) > 2 && g->morph_type[2] == 'B');

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    ImVec2 mid(dispcx + 0, dispcy + 0);
    {
        Point p = to_viewer_plane(cel->tmprel);
        if (view_mode == vm_horizon) p = refract_true_point(p);
        Cartesian2D z = Cartesian2D(p, azimuth + azimuth_correction, altitude, zoom);
        mid = ImVec2(dispcx + z.x * dispcx, dispcy + z.y * dispcx);
    }

    // Alpha now varies with the segment as well as the ring, so it is worked out per vertex of the
    // (nring+1) x nseg lattice once and reused by the four quads that meet at each.
    std::vector<unsigned char> lattice((nring+1) * nseg);
    for (int r = 0; r <= nring; r++)
    {
        double f = (double)r / nring;
        for (int s = 0; s < nseg; s++)
        {
            double v = galaxy_surface_intensity(f, s * (_pi * 2.0 / nseg), T, barred) * peak;
            lattice[r*nseg + s] = (unsigned char)fmin(255.0, fmax(0.0, v));
        }
    }
    #define galaxy_vtx_col(rho, sigma) rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, lattice[(rho)*nseg + (sigma)]))

    // PrimReserve commits the vertex and index counts up front, so any quad the loop below skips
    // would leave four vertices and six indices of uninitialised buffer behind it -- stale geometry
    // from an earlier frame, drawn with whatever colours happened to still be sitting in it. That
    // showed up as black-and-white wedges and stray slivers over whatever else was on screen, most
    // visibly over the bright objects, which is a long way from the galaxy that actually caused it.
    // The test depends only on the segment, not the ring, so count the survivors once and reserve
    // exactly those.
    int nvalid = 0;
    for (int s = 0; s < nseg; s++)
    {
        int s1 = (s+1) % nseg;
        if (rim[s].x < -1e4 || rim[s].y < -1e4 || rim[s1].x < -1e4 || rim[s1].y < -1e4) continue;
        nvalid++;
    }
    if (!nvalid) return 0;

    dl->PrimReserve(nring*nvalid*6, nring*nvalid*4);
    for (int r = 0; r < nring; r++)
    {
        double f0 = (double)r / nring, f1 = (double)(r+1) / nring;
        int r1 = r+1;
        for (int s = 0; s < nseg; s++)
        {
            int s1 = (s+1) % nseg;
            if (rim[s].x < -1e4 || rim[s].y < -1e4 || rim[s1].x < -1e4 || rim[s1].y < -1e4) continue;
            // Each rim point scaled towards the centre gives the inner rings for free, and keeps
            // them concentric in SCREEN space, which is what the projected disc actually is.
            ImVec2 a0(mid.x + (rim[s ].x - mid.x)*f0, mid.y + (rim[s ].y - mid.y)*f0);
            ImVec2 a1(mid.x + (rim[s1].x - mid.x)*f0, mid.y + (rim[s1].y - mid.y)*f0);
            ImVec2 b1(mid.x + (rim[s1].x - mid.x)*f1, mid.y + (rim[s1].y - mid.y)*f1);
            ImVec2 b0(mid.x + (rim[s ].x - mid.x)*f1, mid.y + (rim[s ].y - mid.y)*f1);
            unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(a0, uv, galaxy_vtx_col(r,   s ));
            dl->PrimWriteVtx(a1, uv, galaxy_vtx_col(r,   s1));
            dl->PrimWriteVtx(b1, uv, galaxy_vtx_col(r1, s1));
            dl->PrimWriteVtx(b0, uv, galaxy_vtx_col(r1, s ));
            dl->PrimWriteIdx((ImDrawIdx)(base+0));
            dl->PrimWriteIdx((ImDrawIdx)(base+1));
            dl->PrimWriteIdx((ImDrawIdx)(base+2));
            dl->PrimWriteIdx((ImDrawIdx)(base+0));
            dl->PrimWriteIdx((ImDrawIdx)(base+2));
            dl->PrimWriteIdx((ImDrawIdx)(base+3));
        }
    }

    cel->drawnxmin = xmin; cel->drawnxmax = xmax;
    cel->drawnymin = ymin; cel->drawnymax = ymax;
    cel->onscreen = true;
    return fmax(wide, tall) * 0.5;
}

// ---------------------------------------------------------------------------------------------
// Comets.
//
// There is no body here to draw. The nucleus is a few kilometers of dirty ice and is below a pixel
// from any distance a comet has ever been looked at from, and none of what is actually seen
// belongs to it: a coma a hundred thousand kilometers wide boiled off it by the sun, and a tail
// blown out of that coma by the solar wind and by sunlight. So a comet is drawn as what it looks
// like -- a fuzzy ball with a streamer off it -- and never goes near draw_sphere().
//
// Two things follow from the tail being blown rather than dragged, and both of them show. It
// points away from the SUN and not backwards along the orbit, so an outbound comet flies
// tail-first. And its length is a distance in space rather than an angle in the sky, so it
// foreshortens: a comet seen from nearly along its own tail shows a stub, or a round halo with no
// tail at all. That is why everything below is built in space and projected point by point,
// instead of being drawn as a screen-space wedge off the head.

// How a comet's light is divided between the three things drawn from it. The condensation is the
// point path's business further down draw_one_object(), so what it gets is not passed to anything
// here -- it is simply what these two do not take.
static const double kComaShare = 0.45, kTailShare = 0.30;

static Point comet_cross(Point a, Point b)
{
    return Point(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

// The same camera chain that fixes drawnx/drawny in compute_object_draw_coordinates(), for one
// point given relative to the viewer the way tmprel is -- so feeding it tmprel itself lands
// exactly on the head's drawn position, and the coma, the label, and the selection ring cannot
// disagree about where the comet is.
static bool comet_project(Point rel, const Rotation &viewer_plane, ImVec2 &out)
{
    Point p = rotate3D(rel, center, viewer_plane.v, -viewer_plane.a);
    if (view_mode == vm_horizon) p = refract_true_point(p);
    Cartesian2D c(p, azimuth + azimuth_correction, altitude, zoom);
    if (c.x < -1e4 || c.y < -1e4) return false;                 // behind the camera
    out = ImVec2(dispcx + c.x * dispcx, dispcy + c.y * dispcx);
    return true;
}

// One vertex color of the coma or the tail: an emission tint carried at some surface brightness.
// A comet is not a blackbody and has no business being colored from one -- the green of a coma is
// C2 fluorescing in sunlight and the blue of an ion tail is CO+ doing the same -- so the tint is
// fixed and only its brightness varies. Put through black_to_transparent() so a dim value comes
// out as a faint wash of the full color rather than as a muddy dark one, and so it composites over
// a white background as well as over a black one; that is the road the point path's bloom circles
// take too.
static ImU32 comet_vtx(int r, int g, int b, double value)
{
    if (!(value > (1.0/255.0))) return IM_COL32(r, g, b, 0);
    if (value > 1.0) value = 1.0;

    // Whatever gets near the top of the scale comes out white, in an eye as on a plate: the tint
    // is what colors the faint parts, and the blazing middle of a coma is white in every
    // photograph ever taken of one. Left out, the core is a flat lime disc with a hard edge, which
    // is the one thing a coma never looks like.
    const double kWhiten = 0.5;
    if (value > kWhiten)
    {
        double w = (value - kWhiten) / (1.0 - kWhiten);
        r += (int)((255 - r) * w);
        g += (int)((255 - g) * w);
        b += (int)((255 - b) * w);
    }

    return rgba_apply_redlight(Color::black_to_transparent(
        IM_COL32((int)(r*value), (int)(g*value), (int)(b*value), 255)));
}

// Surface brightness down the axis of the tail, t running from the head to the tip. Steep, and it
// has to be: what a tail actually looks like is a blaze coming out of the coma that is half gone
// within a tenth of its length and a faint streamer for the rest, and a profile gentle enough to
// still be worth something at the far end reads as a searchlight beam instead. The divisor does
// that near work and the power closes the tail off at its end.
static double comet_tail_axial(double t)
{
    return pow(1.0 - t, 1.2) / (1.0 + t/0.12);
}

// Along the tail the color walks from the near-white green it leaves the coma with into the blue
// of the ion tail proper, t running from the head to the tip.
static ImU32 comet_tail_vtx(double t, double value)
{
    return comet_vtx((int)(152 - 47*t), (int)(212 - 72*t), (int)(246 + 9*t), value);
}

// Draws the coma and the tail, and returns the coma's radius on screen in pixels -- or zero if
// this comet has no coma worth drawing, in which case the caller falls back to the plain point
// path. The head itself is left to that path in either case: the condensation at the middle of a
// coma is a point source, and the code that draws point sources already draws it correctly.
static double draw_comet(CelestialObject *cel, double appmag)
{
    Comet *cm = (Comet*)cel;
    CelestialObject *light = cel->get_light_center();
    if (!light) return 0;

    double dist_m = cel->tmprel.magnitude();
    if (!(dist_m > 0)) return 0;

    double r_AU = cel->location.distance_to(light->location) * invAU;
    if (r_AU < 0.01) r_AU = 0.01;

    // Nothing is boiling off out at Jupiter. Water ice starts to sublimate in earnest around three
    // AU and is the whole show inside it; further out only the more volatile ices do anything at
    // all, and by five the comet is a bare rock with a magnitude and no picture. Faded rather than
    // switched, so that a comet does not sprout a tail between one frame and the next.
    const double kActivityFull = 3.0, kActivityNone = 5.0;
    double activity = (kActivityNone - r_AU) / (kActivityNone - kActivityFull);
    if (activity <= 0) return 0;
    if (activity > 1) activity = 1;

    double h, slope_r, slope_d;
    cm->light_curve_parameters(h, slope_r, slope_d);

    // How big a comet this is. No catalog states a coma diameter or a tail length, so both are
    // inferred from the one number that does say how much gas the thing is throwing off: the
    // absolute magnitude of its total light curve, referred here to H1 = 8, an unremarkable comet.
    // The tenth-power exponent is gentle on purpose. H1 runs across some fourteen magnitudes in
    // the catalog, and the straight 10^(0.2 dH) that the magnitudes themselves are built on would
    // hand Hale-Bopp a tail several AU long.
    double scale = pow(10.0, 0.1 * (8.0 - h));
    if (scale < 0.2) scale = 0.2;
    if (scale > 10.0) scale = 10.0;

    // A hundred thousand kilometers of coma and a tenth of an AU of tail for that unremarkable
    // comet at one AU, both growing as it comes in. Those are the middle of the observed ranges --
    // comas run from a few thousand kilometers to something over a million, tails from nothing to
    // about an AU, which is where the clamps sit. Against the three comets in the catalog anyone
    // has seen: Hyakutake's coma comes out a degree and a half across for the week it hung over
    // the Plough, Halley gets three tenths of an AU of tail and Hale-Bopp a quarter, all of which
    // is the right size for what was photographed.
    //
    // How fast they grow on the way in is the other thing the light curve knows. R1 is the slope
    // that separates a comet from a rock -- ten of it means the brightness going as the fourth
    // power of the solar distance -- and what is steepening is the outgassing, so the tail follows
    // it rather than a made-up exponent of its own. Clamped either side because a few hundred
    // rows of the catalog carry slopes that no comet has ever obeyed.
    double ramp = slope_r / 10.0;
    if (ramp < 0.4) ramp = 0.4;
    if (ramp > 2.0) ramp = 2.0;

    double coma_m = 1.0e8 * scale * pow(r_AU, -0.5);
    if (coma_m > 1.5e9) coma_m = 1.5e9;
    double tail_m = 1.5e10 * scale * pow(r_AU, -ramp);
    if (tail_m > 1.5 * AU) tail_m = 1.5 * AU;
    if (tail_m < coma_m * 4) tail_m = coma_m * 4;
    coma_m *= activity;
    tail_m *= activity;

    double coma_px = fabs(std::atan2(coma_m, dist_m)) * zoom * dispcx;
    if (!(coma_px >= 1.2)) return 0;                    // smaller than the bloom that would surround it: leave this one to the point path

    // Flux, in the units draw_galaxy() works in and for the same reason it does: the zoom squared
    // is what keeps an extended object's surface brightness the same as the view is magnified,
    // where a point source's bloom rightly stays the size it was. Divided by an area in pixels it
    // gives a pixel value, 255 being full.
    double total = pow(magnbase, -appmag) * global_brightness * zoom * zoom * 1e+4;
    if (!(total > 0)) return 0;

    Rotation viewer_plane = align_points_3d(to_viewer_plane(yaxis), yaxis, center);
    ImVec2 head;
    if (!comet_project(cel->tmprel, viewer_plane, head)) return 0;

    // The tail points away from the sun, and that is the whole of it: the gas is pushed out by the
    // solar wind and the dust by the pressure of sunlight, and neither has anything to do with
    // which way the comet happens to be travelling.
    Point antisun = cel->tmprel - light->tmprel;
    if (!(antisun.squared_magnitude() > 0)) return 0;
    antisun.scale(1);

    // The dust half of it does lag, though. A grain leaves the nucleus carrying the comet's own
    // orbital velocity and keeps it, so the further back down the tail it is, the further behind
    // the comet's present line of flight it has fallen -- and that is what curves a comet's tail,
    // growing as the square of the distance back. Only the part of the lag lying across the tail
    // is worth anything here; the part along it would just make the tail longer. Nothing stores a
    // velocity, so it is read off the orbit six hours either side of now and the comet put back,
    // the same trick the orbit tracer in draw_objects() uses.
    const double kBend = 0.22;
    Point lag;
    bool bent = false;
    if (cel->orbit && cel->orbit->center)
    {
        CelestialLocation was = cel->location;
        cm->update_location(simnow - 6 * 3600.0);
        CelestialLocation before = cel->location - here;
        cm->update_location(simnow + 6 * 3600.0);
        CelestialLocation after = cel->location - here;
        cel->location = was;

        Point v = Point(after) - Point(before);
        double along = v.x*antisun.x + v.y*antisun.y + v.z*antisun.z;
        Point across(antisun.x*along - v.x, antisun.y*along - v.y, antisun.z*along - v.z);
        if (across.squared_magnitude() > 0)
        {
            across.scale(1);
            lag = across;
            bent = true;
        }
    }

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    double dispw = dispcx*2, disph = dispcy*2;

    // Surface brightness of the coma goes as one over the distance from the nucleus, which is not
    // a fudge: gas leaving the nucleus at a steady rate in every direction thins as the square of
    // the radius, and the column an observer looks down is longer towards the middle in exactly
    // the same proportion, so the two conspire and leave 1/rho. It is why a coma has a blazing
    // middle and no edge to speak of. Softened at both ends -- kCore keeps the singularity at the
    // origin from running away, where the condensation the point path draws is sitting anyway, and
    // the exponential is the molecules being broken up by sunlight faster than they can get any
    // further out, which is what really ends a coma. The 0.824 is what x/(x + kCore) e^-x
    // integrates to over the disc, and is there so the profile adds up to the coma's share.
    const double kCore = 0.08;
    double k_bright = total * kComaShare / (2.0 * _pi * 0.824 * coma_px);

    // How much of that is worth drawing, and the answer is not "the coma": a coma has no edge, and
    // what an observer sees the size of is however much of it stands above the faintest thing the
    // eye or the plate can pick out. So a bright comet shows a big coma and a faint one a small
    // coma out of the very same cloud, which is exactly what the records of any one comet across
    // an apparition show. Solving (x + kCore) e^x = k/(R*floor) for where the profile crosses that
    // floor, by Newton from a guess of one coma radius; it converges in a few passes across the
    // whole range of brightnesses in play here.
    const double kFloor = 1.5 / 255.0;
    double spread = 1.0;
    {
        double A = k_bright / (coma_px * kFloor);
        for (int it = 0; it < 12; it++)
        {
            double e = exp(spread);
            double f = (spread + kCore) * e - A;
            double step = f / ((spread + kCore + 1.0) * e);
            spread -= step;
            if (spread < 0.15) { spread = 0.15; break; }
            if (fabs(step) < 1e-4) break;
        }
        if (!(spread > 1.0)) spread = 1.0;              // never smaller than the coma itself, NaN included
        if (spread > 5.0) spread = 5.0;
    }
    double draw_px = coma_px * spread;

    // The tail comes out of the coma at the width of the coma it is coming out of, so it is sized
    // from what is actually visible of that rather than from the nominal radius underneath it.
    double coma_seen_m = coma_m * spread;

    // ---------------------------------------------------------------------------------- the tail
    const int kTailSeg = 40;                            // steps down the spine
    const int kTailLane = 7;                            // lanes across it, edge to edge through the axis
    ImVec2 spine[kTailSeg+1], ofs_u[kTailSeg+1], ofs_v[kTailSeg+1], nrm[kTailSeg+1];
    float half[kTailSeg+1];
    int last = -1;

    {
        // Two directions across the tail's axis, to measure its thickness with. Which two does not
        // matter; what they are for is projecting a real width in space, so that a tail pointed at
        // the viewer opens out into a fan instead of collapsing onto a line.
        Point u = comet_cross(antisun, xaxis);
        if (!(u.squared_magnitude() > 1e-6)) u = comet_cross(antisun, yaxis);
        u.scale(1);
        Point v = comet_cross(antisun, u);
        v.scale(1);

        for (int k = 0; k <= kTailSeg; k++)
        {
            double t = (double)k / kTailSeg;
            Point sp = cel->tmprel + antisun * (tail_m * t);
            if (bent) sp += lag * (tail_m * kBend * t * t);

            // sp is in viewer-relative coordinates, so its own magnitude is its distance from the
            // camera -- and a tail up to 1.5 AU long can run straight through that distance when
            // the comet itself is much closer than that, which happens: Earth passed within 0.1 AU
            // of Hyakutake days either side of the date this code is tested against, well inside
            // the length of the tail this loop was drawing it. Nothing at that range projects
            // sanely -- the perspective divide the Cartesian2D constructor does is blowing up
            // toward the camera plane, and comet_project()'s "behind camera" guard only catches
            // pt.z actually negative, not the near-zero positive values just before it, so a run of
            // samples on the approach can land at wildly wrong but still finite screen coordinates,
            // each too close to its neighbor to trip the jump check below, folding the ribbon back
            // on itself into a solid wedge instead of a break in the line. There is nothing
            // meaningful to draw that close in anyway -- the true sky there is full of tail in
            // every direction, not a thin ribbon a screen quad can stand in for -- so stop short of
            // the region where the projection degenerates rather than pushing a sample into it.
            if (sp.squared_magnitude() < dist_m*dist_m*0.01) break;

            ImVec2 sc, pu, pv;
            if (!comet_project(sp, viewer_plane, sc)) break;
            if (sc.x < -8*dispw || sc.x > 9*dispw || sc.y < -8*disph || sc.y > 9*disph) break;
            // vm_skymap is a whole sky flattened onto a rectangle, and a tail running off one edge
            // of it reappears at the other. Stop at the seam rather than drawing a band across the
            // entire map to get there.
            if (k && distance(sc, spine[k-1]) > dispw) break;

            double w_m = coma_seen_m * (0.55 + 2.0 * t);
            if (!comet_project(sp + u * w_m, viewer_plane, pu)) break;
            if (!comet_project(sp + v * w_m, viewer_plane, pv)) break;

            spine[k] = sc;
            ofs_u[k] = ImVec2(pu.x - sc.x, pu.y - sc.y);
            ofs_v[k] = ImVec2(pv.x - sc.x, pv.y - sc.y);
            last = k;
        }
    }

    if (last >= 2)
    {
        // Half-width of the tail at each sample, measured across whichever way the spine is
        // running on screen there. The tail's cross-section is a circle in space, so on screen it
        // is an ellipse whose axes are the two projected offsets worked out above, and an
        // ellipse's reach in a given direction is the root of the sum of the squares of its axes'
        // components along it.
        for (int k = 0; k <= last; k++)
        {
            ImVec2 a = spine[k > 0 ? k-1 : 0], b = spine[k < last ? k+1 : last];
            double dx = b.x - a.x, dy = b.y - a.y, dl2 = sqrt(dx*dx + dy*dy);
            nrm[k] = (dl2 > 1e-6) ? ImVec2(-dy/dl2, dx/dl2) : ImVec2(0, 1);
            double cu = ofs_u[k].x*nrm[k].x + ofs_u[k].y*nrm[k].y;
            double cv = ofs_v[k].x*nrm[k].x + ofs_v[k].y*nrm[k].y;
            half[k] = (float)fmax(0.8, sqrt(cu*cu + cv*cv));
        }

        // Across the tail: a rounded profile rather than a slab, since what is being looked
        // through is a cone of thin gas and there is far more of it to look through down the
        // middle than at the edge. Seven lanes, because five left the edges looking cut.
        static const double lane_at[kTailLane]  = { -1.0, -0.62, -0.3, 0.0, 0.3, 0.62, 1.0 };
        static const double lane_val[kTailLane] = {  0.0,  0.28,  0.72, 1.0, 0.72, 0.28, 0.0 };

        // Spread the tail's share of the comet's light over exactly what is about to be drawn,
        // summed rather than integrated because the projection can do anything it likes to the
        // spacing of the samples. That lane profile comes to about 0.94 half-widths integrated
        // across the tail; the rest of each term is the length of that step of spine.
        double sum = 0;
        for (int k = 0; k < last; k++)
            sum += comet_tail_axial((double)k/kTailSeg) * half[k] * 0.94 * distance(spine[k], spine[k+1]);
        double amp = (sum > 0) ? (total * kTailShare / sum) : 0;

        if (amp > 0)
        {
            dl->PrimReserve(last*(kTailLane-1)*6, last*(kTailLane-1)*4);
            for (int k = 0; k < last; k++)
            {
                double t0 = (double)k / kTailSeg, t1 = (double)(k+1) / kTailSeg;
                double a0 = amp * comet_tail_axial(t0), a1 = amp * comet_tail_axial(t1);
                for (int l = 0; l < kTailLane-1; l++)
                {
                    float o0 = half[k]   * (float)lane_at[l],   o1 = half[k+1] * (float)lane_at[l];
                    float p0 = half[k]   * (float)lane_at[l+1], p1 = half[k+1] * (float)lane_at[l+1];
                    unsigned int base = dl->_VtxCurrentIdx;
                    dl->PrimWriteVtx(ImVec2(spine[k  ].x + nrm[k  ].x*o0, spine[k  ].y + nrm[k  ].y*o0), uv, comet_tail_vtx(t0, a0*lane_val[l]));
                    dl->PrimWriteVtx(ImVec2(spine[k+1].x + nrm[k+1].x*o1, spine[k+1].y + nrm[k+1].y*o1), uv, comet_tail_vtx(t1, a1*lane_val[l]));
                    dl->PrimWriteVtx(ImVec2(spine[k+1].x + nrm[k+1].x*p1, spine[k+1].y + nrm[k+1].y*p1), uv, comet_tail_vtx(t1, a1*lane_val[l+1]));
                    dl->PrimWriteVtx(ImVec2(spine[k  ].x + nrm[k  ].x*p0, spine[k  ].y + nrm[k  ].y*p0), uv, comet_tail_vtx(t0, a0*lane_val[l+1]));
                    dl->PrimWriteIdx((ImDrawIdx)(base+0));
                    dl->PrimWriteIdx((ImDrawIdx)(base+1));
                    dl->PrimWriteIdx((ImDrawIdx)(base+2));
                    dl->PrimWriteIdx((ImDrawIdx)(base+0));
                    dl->PrimWriteIdx((ImDrawIdx)(base+2));
                    dl->PrimWriteIdx((ImDrawIdx)(base+3));
                }
            }
        }
    }

    // ---------------------------------------------------------------------------------- the coma
    const int kMaxComaSeg = 56;
    int nseg = (int)fmin((double)kMaxComaSeg, fmax(14.0, draw_px * 1.2));
    int nring = (int)fmin(18.0, fmax(5.0, draw_px * 0.45));

    // A coma is not quite round. The sunward side is pressed in against the outflow and the far
    // side is drawn out into the tail, so the whole thing is stretched a little the way the tail
    // goes -- the "hood" that makes a bright comet look like a paintbrush rather than a ball.
    const double kHood = 0.28;
    double hood_x = 0, hood_y = 0;
    if (last >= 1)
    {
        double dx = spine[1].x - spine[0].x, dy = spine[1].y - spine[0].y, dl2 = sqrt(dx*dx + dy*dy);
        if (dl2 > 1e-6) { hood_x = dx/dl2; hood_y = dy/dl2; }
    }

    ImVec2 rim[kMaxComaSeg];
    for (int s = 0; s < nseg; s++)
    {
        double t = s * (_pi * 2.0 / nseg), ct = cos(t), st = sin(t);
        double stretch = draw_px * (1.0 + kHood * (ct*hood_x + st*hood_y));
        rim[s] = ImVec2(head.x + ct*stretch, head.y + st*stretch);
    }

    #define coma_vtx_col(rho) comet_vtx(150, 255, 205, \
        k_bright * exp(-(double)(rho)/nring*spread) / (coma_px * ((double)(rho)/nring*spread + kCore)))

    dl->PrimReserve(nring*nseg*6, nring*nseg*4);
    for (int r = 0; r < nring; r++)
    {
        double f0 = (double)r / nring, f1 = (double)(r+1) / nring;
        for (int s = 0; s < nseg; s++)
        {
            int s1 = (s+1) % nseg;
            unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(ImVec2(head.x + (rim[s ].x - head.x)*f0, head.y + (rim[s ].y - head.y)*f0), uv, coma_vtx_col(r));
            dl->PrimWriteVtx(ImVec2(head.x + (rim[s1].x - head.x)*f0, head.y + (rim[s1].y - head.y)*f0), uv, coma_vtx_col(r));
            dl->PrimWriteVtx(ImVec2(head.x + (rim[s1].x - head.x)*f1, head.y + (rim[s1].y - head.y)*f1), uv, coma_vtx_col(r+1));
            dl->PrimWriteVtx(ImVec2(head.x + (rim[s ].x - head.x)*f1, head.y + (rim[s ].y - head.y)*f1), uv, coma_vtx_col(r+1));
            dl->PrimWriteIdx((ImDrawIdx)(base+0));
            dl->PrimWriteIdx((ImDrawIdx)(base+1));
            dl->PrimWriteIdx((ImDrawIdx)(base+2));
            dl->PrimWriteIdx((ImDrawIdx)(base+0));
            dl->PrimWriteIdx((ImDrawIdx)(base+2));
            dl->PrimWriteIdx((ImDrawIdx)(base+3));
        }
    }
    #undef coma_vtx_col

    // The coma is the part of a comet big enough to aim a mouse at, so it is what the picking in
    // inputs.cpp gets told about. The tail is deliberately left out of it: a box around a tail
    // forty degrees long would swallow half the sky and everything in it.
    cel->drawnxmin = head.x - draw_px; cel->drawnxmax = head.x + draw_px;
    cel->drawnymin = head.y - draw_px; cel->drawnymax = head.y + draw_px;
    cel->onscreen = true;
    return draw_px;
}

bool draw_one_object(int i)
{
        bool obj_is_localsys = (cels[i]->cenobj == mycenobj);
    if (!show_localsys && obj_is_localsys) return false;
    if ((i == whereami) && (view_mode != vm_system)) return false;
    
    int j;
    double coma_px = 0;
    cel_obj_class cls = cels[i]->typeclass();
    xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
    appmag = vmag_cache[i] - sky_mag_shift;
    double brght = pow(magnbase, -appmag);
    bloomrad = fabs(pow(brght, 0.5)*global_brightness);

    double f_bloom = (bloomrad>1.5*max_bloomrad) ? 1.0+sqrt(bloomrad-1.5*max_bloomrad)*13 : 0;
    double f_mag = fmax(0.0, -1.0 - vmag_cache[i]) * 20.0;
    double f_ang = angular_radius[i]*zoom*dispcx;
    flare = fmin(max_flare, fmax(f_bloom, f_mag) / fmax(1, f_ang));
    // if (flare >= 5) std::cout << cels[i]->name << " f_bloom=" << f_bloom << " f_mag=" << f_mag << " f_ang=" << f_ang << " flare=" << flare << std::endl;
    if (view_mode == vm_horizon && cels[i]->Decl_as_radians(here) < -angular_radius[i]) flare = 0;
    bloomrad = fmin(max_bloomrad, bloomrad*10);
    if (cls == class_galaxy)
    {
        double r = draw_galaxy(cels[i], appmag);
        if (r > 0)
        {
            bloomrad_cache[i] = bloomrad = r;
            discinstead[i] = false;
            if (selected == i)
                ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2,
                    rgba_apply_redlight(global_style.selected_color), 0, 2);
            goto labels_step;
        }
        // Too small, too faint, or off screen: fall through to the point path below.
        if (i != inside_galaxy_idx) goto dot_instead;
    }
    else if (cls == class_satellite)
    {
        if (!show_sats || !cels[i]->orbit || !cels[i]->orbit->center) return false;
        if ((cels[i]->tmprel.magnitude() > cels[i]->orbit->semimajor_axis*zoom*6)
            && (angular_radius[i] < 0.01*fiftyseventh)
            )
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
    else if (cls == class_comet)
    {
        coma_px = draw_comet(cels[i], appmag);
        if (coma_px > 0)
        {
            // What the coma and the tail did not take is the condensation at the head, and the
            // point path below draws it: it is a point source, and that code already knows how to
            // draw one. Handed over as a magnitude, since that is the currency it works in.
            appmag -= log(1.0 - kComaShare - kTailShare) * invlogmagnbase;
        }
        goto dot_instead;
    }
    else if (angular_radius[i]*zoom > sphere_rad_threshold)
    {
        // An eclipsed star loses its glare along with its light: the flare drawn here is the
        // scatter of an overwhelming photosphere in the eye and in the air, and as the moon
        // covers that photosphere it goes with it. What is left behind, and only then, is the
        // corona -- a million times fainter, and invisible any other day of the century.
        double obsc = (cels[i] == eclipsed_light) ? eclipsed_fraction : 0.0;
        if (flare)
        {
            Color col = Color::color_from_magnitude_indices(appmag, cels[i]->BV_color);
            draw_flare(flare * (1.0 - obsc), col, vmag_cache[i], angular_radius[i]*zoom*dispcx);
        }
        if (obsc > 0) draw_corona(xycoord, angular_radius[i]*zoom*dispcx, obsc, cels[i]->BV_color);

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
        dot_instead:
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
        
        double divisor = 1.0 / fmax(.00392, fmin(col.red, col.blue));
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
    
    // A comet drawn as a comet is far larger than the condensation the point path just finished
    // with, and the label and the selection ring belong outside the coma rather than buried in it.
    // The cache goes with it so the layering in draw_objects() puts a comet in front of the field
    // stars it is drifting over, and not behind them.
    if (coma_px > bloomrad)
    {
        bloomrad = coma_px;
        bloomrad_cache[i] = coma_px;
    }
    if (selected == i && cels[1])
    {
        ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(global_style.selected_color), 0, 2);
    }
    

    labels_step:
    Star *s = (cels[i]->type == star) ? (Star*)cels[i] : nullptr;
    if ( (show_labels && s && !cels[i]->orbit &&
            ((!cbolbls_selected_idx && appmag <= appmagn_lblcut)
            || (cbolbls_selected_idx == lbltype_intrinsic && cels[i]->absolute_magnitude <= absmagn_lblcut)
            || (cbolbls_selected_idx == lbltype_nearby && here.distance_to(cels[i]->location) <= distance_lblcut)
            || (cbolbls_selected_idx == lbltype_Bayer && strlen(s->Bayer) && (!cons4lbl || s->matches_constellation(cons4lbl->abbrev.c_str())))
            || (cbolbls_selected_idx == lbltype_Flamsteed && strlen(s->Flamsteed) && (!cons4lbl || s->matches_constellation(cons4lbl->abbrev.c_str())))
            || ((cbolbls_selected_idx == lbltype_Gould && s->GouldNo > 0) && (!cons4lbl || s->matches_constellation(cons4lbl->abbrev.c_str())))
            || (cbolbls_selected_idx == lbltype_sunlike && s->is_sunlike())
            || (cbolbls_selected_idx == lbltype_planets && (s->has_planets >= planets_lblcut) )
            || (cbolbls_selected_idx == lbltype_planethz && (s->has_hz_planets) )
            || (cbolbls_selected_idx == lbltype_binary && (s->multisys))
            || (cbolbls_selected_idx == lbltype_knpole && cels[i]->known_poles)
            ))
        || (obj_is_localsys && lbl_localsys
            && ((cels[i]->mass >= lmasslim)
                || (vmag_cache[i] < (mag_limit_adjusted-4))
                || (cels[i]->tmprel.magnitude() < AU)
                )
            )
        || (cels[i]->type == galaxy && label_galaxies && vmag_cache[i] < (mag_limit_adjusted))
        || i == selected)
    {
        const char *dispname = cels[i]->name;
        int l = strlen(dispname);
        if (!l) return true;
        int disph = dispcy * 2;
        std::string lopped;
        if (dispname[l-1] == 'A' && cels[i] != mycenobj)
        {
            lopped = lop_component(dispname);
            dispname = lopped.c_str();
        }
        ImFont *font = global_font;
        std::string str;
        cel_obj_class cls = cels[i]->typeclass();
        double lfontsz = global_font_size;
        if (cbolbls_selected_idx == lbltype_Bayer && cls == class_star && (((Star*)cels[i])->BayerGrkno >= 0))
        {
            // str = trim(std::string(((Star*)cels[i])->Bayer).substr(0, strlen(((Star*)cels[i])->Bayer)-3));
            // dispname = str.c_str();
            int lBayerGrkNo = ((Star*)cels[i])->BayerGrkno;
            if (lBayerGrkNo >= 100) lBayerGrkNo = (lBayerGrkNo-100)/10;
            char c = Greek_symbol_mapping[lBayerGrkNo];
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
        else if (cls == class_star && cels[i]->cenobj == mycenobj && ((Star*)cels[i])->local_name.size())
            dispname = ((Star*)cels[i])->local_name.c_str();

        ImVec2 sz = ImGui::CalcTextSize(dispname);
        int dy = cels[i]->drawny+bloomrad+1;
        if (cels[i]->drawny < disph && dy > disph-sz.y) dy = disph-sz.y;
        ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(cels[i]->drawnx - sz.x/2, dy),
            rgba_apply_redlight(Color::ensure_wcag_contrast(
                (i == selected) ? global_style.selected_color : global_style.objlbl_color, whtbkgd, 4.5, -1, true)),
            dispname);
    }
    return true;
}

// One horizontal crossing of the band's outline: the scanline it lands on, and where along it.
struct BandCrossing
{
    int y;
    float x;
    bool dir;
};

void draw_galaxy_band()
{
    if (!show_galaxy_band || inside_galaxy_idx < 0) return;
    if (view_mode == vm_system) return;

    CelestialObject *cel = cels[inside_galaxy_idx];
    Galaxy *g = (Galaxy*)cel;
    if (g->tmprel.magnitude() > g->volumetric_mean_radius) return;

    int h, i, n;

    // The .dat file's longitude runs in galactic coordinates with 0 at the galactic center, so
    // the seam at the +-pi wraparound naturally falls 180 degrees from it -- but only once the
    // pattern is spun so that longitude 0 points where the CURRENT viewer actually sees the
    // center, not where Sol does. local_system_plane only encodes that fixed Sol-relative
    // orientation of the disc, so the extra spin has to be measured in the disc's own local
    // frame (canonical zaxis = longitude 0), the same way incl_and_node_from_system_plane
    // recovers an ascending node.
    Rotation pl = g->location.local_system_plane;
    Point viewer_dir = rotate3D(g->tmprel, center, pl.v, pl.a);
    double gyaw = find_angle_along_vector(zaxis, viewer_dir, center, yaxis);
    double gbrt = (view_mode == vm_horizon) ? (8 * pow(magnbase, sky_mag_shift)) : 6;
    if (gbrt < 2) return;

    // gcol, the band's outline colour, went with the outline pass that used to sit at the bottom
    // of this function behind an "if (1)" that made it unreachable.
    ImU32 fillcol = rgba_apply_redlight(
        whtbkgd
        ? IM_COL32(0, 0, 0, 20)
        : IM_COL32(192, 224, 255, (int)gbrt));      // subtle glow filling the band. TODO: Galaxy color.

    ImGuiIO& io = ImGui::GetIO();

    // Project both boundary roads (road1 = north edge, road2 = south edge) to screen space once,
    // up front, so the fill pass below and the outline pass further down share the same points
    // instead of re-deriving them twice.
    std::vector<ImVec2> screen[2];
    std::vector<bool> good[2];

    std::vector<Point> viewspace[2];
    bool camera_is_directional = (view_mode != vm_skymap);

    for (h=0; h<2; h++)
    {
        n = h ? g->band.road2_gra.size() : g->band.road1_gra.size();
        screen[h].assign(n, ImVec2());
        good[h].assign(n, false);
        if (camera_is_directional) viewspace[h].assign(n, Point());

        for (i=0; i<n; i++)
        {
            double road_dist = h ? g->band.road2_dist[i] : g->band.road1_dist[i];
            Point pt = Point::from_ra_dec(
                h ? g->band.road2_gra[i] : g->band.road1_gra[i],
                h ? g->band.road2_gdecl[i] : g->band.road1_gdecl[i],
                g->volumetric_mean_radius, 0);
            if (road_dist) pt.y *= road_dist / g->volumetric_mean_radius;
            pt = rotate3D(pt, center, yaxis, gyaw);
            pt = rotate3D(pt, center, pl.v, -pl.a);
            pt += g->tmprel;
            if (!road_dist)
            {
                road_dist = pt.magnitude();
                if (h) g->band.road2_dist[i] = road_dist;
                else g->band.road1_dist[i] = road_dist;
            }
            pt = to_viewer_plane(pt, 1);
            pt = refract_true_point(pt);

            // azimuth_correction, not just azimuth: in horizon mode set_viewer_surface_location()
            // sets it to -npaz, the azimuth of the planet's own north pole, which is what ties the
            // horizon frame's zero of azimuth to true north.
            Cartesian2D cart(pt, azimuth + azimuth_correction, altitude, zoom);
            if (cart.x > -1e21 && cart.y > -1e21)
            {
                screen[h][i].x = dispcx + dispcx * cart.x;
                screen[h][i].y = dispcy + dispcx * cart.y;
                good[h][i] = true;
            }

            if (camera_is_directional)
            {
                // Mirrors the rotation Cartesian2D just did internally (its "else" branch, taken
                // whenever view_mode != vm_skymap) so viewspace[] lands in the same camera-facing
                // frame its own pt.z < 0 test used -- without this exact match, clipping the fill
                // outline against z=0 would clip against the wrong plane.
                Point vp = pt;
                if (azimuth + azimuth_correction) vp = rotate3D(vp, center, yaxis, -(azimuth + azimuth_correction));
                if (altitude) vp = rotate3D(vp, center, xaxis, altitude);
                viewspace[h][i] = vp;
            }
        }
    }

    // Fill, by scanline; we cannot simply stitch a ribbon of triangles between the two roads,
    // because the band's edges are not a smooth corridor. The two roads run the full sweep of
    // longitude from -pi to +pi as open curves whose endpoints meet on the sky, and each has
    // deep fjord-like notches. The roads also have different point counts.
    //
    // Scanline conversion sidesteps the pairing question entirely by finding  where the outline
    // crosses each row of pixels. Sort those crossings along the row and fill between alternate
    // pairs -- the even-odd rule -- and the interior falls out correctly no matter how sinuous
    // or notched the outline is.
    // Also, the spans are one pixel tall and never overlap, so a translucent fill stays at
    // exactly its own alpha.
    int disph = (int)(dispcy*2), dispw = (int)(dispcx*2);
    int dcx = (int)io.DisplaySize.x / 2;

    std::vector<BandCrossing> crossings;
    // Walk the closed outline: road1 forward, then road2 backward. That traversal is what makes
    // the two roads bound one region rather than two open curves -- and because road1's ends and
    // road2's ends coincide on the sky, the joins between them are zero-length, so the ring
    // closes without any artificial seam edge being invented.
    int n1 = screen[0].size(), n2 = screen[1].size();
    int total = n1 + n2;
    if (n1 >= 2 && n2 >= 2)
    {
        // Accumulate every scanline this edge crosses. Sampling at pixel centres (y+0.5) with a
        // half-open rule on the endpoints is what keeps parity exact: a vertex landing precisely
        // on a scanline is counted by one of its two edges, never both and never neither.
        auto emit_edge = [&](ImVec2 p, ImVec2 q)
        {
            if (p.y == q.y) return;
            if (fabs(p.x) > 1e6 || fabs(q.x) > 1e6) return;

            bool py_qy_dir = (p.y > q.y);
            if (py_qy_dir) { ImVec2 t = p; p = q; q = t; }

            int y0 = (int)ceil(p.y - 0.5), y1 = (int)ceil(q.y - 0.5) - 1;
            if (y0 < 0) y0 = 0;
            if (y1 > disph-1) y1 = disph-1;

            double slope = (q.x - p.x) / (q.y - p.y);
            for (int y = y0; y <= y1; y++)
            {
                BandCrossing c;
                c.y = y;
                c.x = (float)(p.x + slope * ((y + 0.5) - p.y));
                c.dir = py_qy_dir;
                crossings.push_back(c);
            }
        };

        // Same seam rule as wrapped_line(): an edge that leaps the width of the sky is really the
        // band wrapping round behind the viewer, so hand the scanline both halves of it. Their
        // crossings sit outside the screen on one side each, which is harmless -- parity is
        // counted over every crossing, and only the drawing is clipped to the display.
        auto emit_wrapped = [&](ImVec2 p, ImVec2 q)
        {
            if ((view_mode == vm_skymap || view_mode == vm_sunclock)
                && fabs(p.x - q.x) > zoom*dcx
                && ((p.x < dcx && q.x > dcx) || (p.x > dcx && q.x < dcx)))
            {
                ImVec2 q2 = q, p2 = p;
                q2.x += (q2.x > dcx) ? -dcx*2 : dcx*2;
                p2.x += (p2.x > dcx) ? -dcx*2 : dcx*2;
                emit_edge(p, q2);
                emit_edge(p2, q);
            }
            else emit_edge(p, q);
        };

        // Index into the concatenated outline: road1 forward, then road2 in reverse. This is what
        // makes the two roads bound one region rather than two open curves -- and because road1's
        // ends and road2's ends coincide on the sky, the join between them is zero-length, so the
        // ring closes without an artificial seam edge being invented.
        auto outline_point = [&](int i) -> const Point&
        {
            int hh = (i < n1) ? 0 : 1, ii = (i < n1) ? i : (n2-1 - (i - n1));
            return viewspace[hh][ii];
        };

        if (!camera_is_directional)
        {
            // vm_skymap never culls by depth (its projection is the flat equirectangular one, no
            // camera plane to be behind), so every road point is already valid and the previous
            // per-edge walk is exact as it stands.
            for (i=0; i<total; i++)
            {
                int ha = (i < n1) ? 0 : 1, ia = (i < n1) ? i : (n2-1 - (i - n1));
                int k = (i+1) % total;
                int hb = (k < n1) ? 0 : 1, ib = (k < n1) ? k : (n2-1 - (k - n1));

                if (good[ha][ia] && good[hb][ib])
                    emit_wrapped(screen[ha][ia], screen[hb][ib]);
            }
        }
        else
        {
            // Everywhere else, Cartesian2D refuses points behind the camera plane (pt.z < 0), and
            // we cannot simply leave those vertices out of the walk with a closed outline: dropping
            // a vertex does not remove its two edges, it reconnects its neighbours across whatever
            // the vertex used to separate, so a stretch of missing vertices silently rewires the
            // polygon's boundary and desyncs the even-odd parity for every scanline downstream of
            // the gap, causing the band fill to vanish in some frames and fill everywhere BUT the
            // band in others. And the band circles the whole sky, so very close to half its vertices
            // are behind the camera at any moment, regardless of zoom.
            //
            // The fix is to clip the loop against the camera plane (pt.z == 0) properly, inserting
            // a new vertex exactly where each edge crosses it rather than dropping either endpoint.
            // This is the standard Sutherland-Hodgman clip of a closed polygon against a single
            // plane, and it always yields a new, still-closed polygon -- so the scanline pass below
            // never has to special-case a gap.
            const double eps = g->volumetric_mean_radius * 1e-6;
            std::vector<Point> clipped;
            clipped.reserve(total + 8);

            for (i=0; i<total; i++)
            {
                const Point& curr = outline_point(i);
                const Point& prev = outline_point((i-1+total) % total);
                bool curr_in = curr.z >= eps, prev_in = prev.z >= eps;

                if (curr_in != prev_in)
                {
                    double t = (eps - prev.z) / (curr.z - prev.z);
                    clipped.push_back(Point(
                        prev.x + t*(curr.x-prev.x),
                        prev.y + t*(curr.y-prev.y),
                        eps));
                }
                if (curr_in) clipped.push_back(curr);
            }

            int cn = clipped.size();
            for (i=0; i<cn; i++)
            {
                const Point& p = clipped[i];
                const Point& q = clipped[(i+1) % cn];
                ImVec2 sp(dispcx + dispcx * (p.x/p.z*zoom), dispcy + dispcx * (-p.y/p.z*zoom));
                ImVec2 sq(dispcx + dispcx * (q.x/q.z*zoom), dispcy + dispcx * (-q.y/q.z*zoom));
                emit_wrapped(sp, sq);
            }
        }
    }

    // The scanline fill, unconditionally. This used to be "if (1) // camera_is_directional)" with
    // an else that drew the band as a pair of outlines instead -- unreachable as written, and
    // superseded: the fill covers both projections now that the vm_skymap case is handled by
    // emit_wrapped() and the directional case by the polygon clip above. The outline pass is in
    // the history if it is ever wanted back.
    {
        if (crossings.size() >= 2)
        {
            std::sort(crossings.begin(), crossings.end(),
                [](const BandCrossing& a, const BandCrossing& b)
                { return (a.y != b.y) ? (a.y < b.y) : (a.x < b.x); });

            ImDrawList *list = ImGui::GetBackgroundDrawList();
            size_t s = 0;
            while (s < crossings.size())
            {
                size_t e = s;
                while (e < crossings.size() && crossings[e].y == crossings[s].y) e++;

                // An odd number of crossings means the outline was left open on this row -- points
                // dropped by the projection behind the viewer, most often. That used to be
                // described here as a reason to skip the row, and a count was taken for it, but
                // the skip itself was never written and the count went unread. What actually
                // happens is below: drawable is padded out to an even length with the right-hand
                // edge of the screen, so an open row fills to the edge rather than being dropped.
                std::vector<double> drawable;
                bool first = true;
                for (int k = s; k < e; k++)
                {
                    if (first && crossings[k].dir) drawable.push_back(0); // k++;
                    if (crossings[k].x > -1e6 && crossings[k].x < 1e6)
                        drawable.push_back(crossings[k].x);
                    first = false;
                }
                std::sort(drawable.begin(), drawable.end()); // , std::greater<double>());
                int drawable_sz = drawable.size()-1;     // since we're counting by twos, ensure we don't overflow if the number is odd.
                if (!(drawable_sz & 0x1))
                {
                    drawable.push_back(dispw);
                    drawable_sz++;
                }

                float y = (float)crossings[s].y;
                for (int k = 0; k < drawable_sz; k+=2)
                {
                    float x0 = drawable[k], x1 = drawable[k+1];
                    if (x0 < 0) x0 = 0;
                    if (x1 > dispw) x1 = (float)dispw;
                    list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y+1.0f), fillcol);
                }

                s = e;
            }
        }
    }
}

void draw_objects()
{
    if (!ncelobjs) return;

    if (view_mode == vm_system)
    {
        draw_system_view();
        return;
    }

    int i, j, n, pass;
    double step, dispw = dispcx*2, disph = dispcy*2;
    double orbseg = 81;
    lmasslim = lbllsys_mass_lim*1000;
    std::vector<CelestialObject*> to_draw_layered;
    global_magshift = -log(global_brightness) * invlogmagnbase;
    myeq = (whereami >= 0) ? cels[whereami]->equinox_RA : 0;

    if (whereami >= 0 && cels[whereami]->typeclass() == class_star)
    {
        if (view_mode == vm_horizon) view_mode = vm_spaceship;
    }
    if (whereami >= 0 && trackidx == whereami) trackidx = -1;

    double mycensq = mycenobj->tmprel.squared_magnitude();
    double layer_cutoff = mycensq * 1.1 * zoom * zoom;
    mag_limit_adjusted = log(pow(magnbase, normal_best_mag_limit)*zoom) * invlogmagnbase;

    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    ImGuiIO& io = ImGui::GetIO();

    // Once per frame, ahead of any disc: every disc drawn below asks this list who might be
    // The eclipse caster list every disc below consults is rebuilt earlier than this, at the top
    // of compute_object_draw_coordinates(), because the sky's own brightness depends on it too
    // and is settled before any of this runs. See refresh_eclipse_casters() near draw_sphere_gpu().

    // Orbits
    if (show_orbits && show_localsys) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (cels[i]->deleted) continue;
        if (!cels[i]->orbit) continue;
        if (cels[i]->cenobj != mycenobj && (whereami<0 || cels[i]->orbit->center != cels[whereami])) continue;
        if (cels[i]->orbit->center == mycenobj && cels[i]->mass < lmasslim) continue;

        Color col = Color::color_from_magnitude_indices(vmag_cache[i] + 5, cels[i]->BV_color);
        RGB3Byte rgb = Color::rgb_from_color(col, 1);
        if (whtbkgd) rgb.invert_luminance();
        ImU32 imcol = (i==selected) ? rgba_apply_redlight(global_style.selected_orbit_color) : rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 64));
        CelestialLocation was = cels[i]->location;
        bool is_star = (cels[i]->typeclass() == class_star),
            is_moon = (cels[i]->typeclass() == class_moon),
            is_sat = (cels[i]->typeclass() == class_satellite),
            is_comet = (cels[i]->typeclass() == class_comet);

        // One period covers a closed orbit exactly once. An open one has no period to cover, so
        // trace a human span of it instead: the loop below runs forward from now, so give it a
        // hundred and twenty years of the comet's path rather than pretend to draw a curve that
        // in truth runs to infinity at both ends.
        step = cels[i]->orbit->period
            ? (cels[i]->orbit->period / orbseg)
            : (120 * oneyear / orbseg);

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
            else if (is_comet)
                ((Comet*)cels[i])->update_location(simnow + step*j - light_travel_time);
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
        if (cels[i]->deleted) continue;
        cels[i]->drawnxmin = cels[i]->drawnxmax = cels[i]->drawnymin = cels[i]->drawnymax = -1e9;
        if ((i == whereami) && (view_mode != vm_system)) continue;

        if (!pass && fabs(bloomrad_cache[i]) > 3) continue;
        else if (pass && fabs(bloomrad_cache[i]) <= 3) continue;

        // A comet is drawn far larger than the head this test is looking at: the tail can lie
        // across the whole screen with the nucleus itself well off the edge of it, and those are
        // precisely the passes worth watching. angular_radius[] knows only about the nucleus and
        // would throw the comet away here, so give it a wide berth of screens instead and let
        // draw_comet() clip its own geometry. A comet behind the camera still fails this: the
        // sentinel for that is -1e29, several orders past the window below.
        if (cels[i]->typeclass() == class_comet)
        {
            if (cels[i]->drawnx < -4*dispw || cels[i]->drawnx > 5*dispw) continue;
            if (cels[i]->drawny < -4*disph || cels[i]->drawny > 5*disph) continue;
        }
        else if (angular_radius[i]*zoom < sphere_rad_threshold)
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

        // Only a Star has has_planets/has_hz_planets, and this loop walks every object there is.
        // The two clauses below used to cast cels[i] to Star* whatever it actually was: the
        // short-circuit reads as a guard but is not one, because when cbolbls_selected_idx IS
        // lbltype_planets or lbltype_planethz -- exactly when the user has asked to label stars
        // by their planets -- the member read runs against every planet, satellite, comet and
        // galaxy in the array, at whatever offset Star::has_planets happens to fall. A non-star
        // gets no exemption from the magnitude cut, which is what !istar says here.
        Star *istar = (cels[i]->typeclass() == class_star) ? (Star*)cels[i] : nullptr;
        if (appmag > mag_limit_adjusted && i
            && (angular_radius[i] < 0.01*fiftyseventh)
            && (cbolbls_selected_idx != lbltype_planets  || !istar || istar->has_planets < planets_lblcut)
            && (cbolbls_selected_idx != lbltype_planethz || !istar || !istar->has_hz_planets)) continue;

        bloomrad = fabs(bloomrad_cache[i]);
        bloomrad = fmin(max_bloomrad, bloomrad);

        // if (cls != class_satellite && angular_radius[i]*zoom > sphere_rad_threshold)
        if (mycensq < light_year_sq
            && cels[i]->tmprel.squared_magnitude() < layer_cutoff)
        {
            // Insertion sort by horizon distance, nearest last. discinstead[i] was being used as
            // the "did it go in" scratch flag and then overwritten with true on every path
            // regardless, which made the branch that read it unable to change anything. A local
            // says what it means and leaves the array to its own purpose.
            n = to_draw_layered.size();
            bool inserted = false;
            if (n)
            {
                double trm = cels[i]->get_horizon_distance();
                for (j=0; j<n; j++)
                {
                    if (to_draw_layered[j]->get_horizon_distance() < trm)
                    {
                        to_draw_layered.insert(to_draw_layered.begin()+j, cels[i]);
                        inserted = true;
                        break;
                    }
                }
            }
            if (!inserted) to_draw_layered.push_back(cels[i]);
            discinstead[i] = true;
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

void draw_system_view()
{
    int i, j, l, n = lsyscache.size();

    double padding = dispcy/20;
    int num_stars = 0;
    for (i=0; i<n; i++) if (lsyscache[i]->typeclass() == class_star) num_stars++;

    for (i=0; i<num_stars; i++)
    {
        Star *s = (Star*)lsyscache[i];          // Since stars get listed first, we don't have to check the type class.
        s->drawnx = -dispcx/29;
        s->drawny = dispcy + (2.0 * i - num_stars + 1) * (dispcy / num_stars);

        // dry run - find planetary system scaling
        double sdrad = dispcy/num_stars + log(s->volumetric_mean_radius / solar_radius) * 20;
        double cursor = s->drawnx + sdrad + padding;

        for (j=num_stars; j<n; j++)
        {
            cel_obj_class cls = lsyscache[j]->typeclass();
            if (cls != class_planet) continue;

            Planet *p = (Planet*)lsyscache[j];
            if (p->mass < 0.01 * earth_mass) continue;
            if (!p->orbit || p->orbit->center != s) continue;
            p->drawny = s->drawny;
            double pdrad = dispcx/10 + log(p->volumetric_mean_radius / earth_radius)*20;
            p->drawnx = cursor + pdrad;
            // std::cout << p->name << " cursor=" << cursor << " + pdrad=" << pdrad << " + pdrad=" << pdrad;           // deliberately twice
            cursor = p->drawnx + pdrad + padding;
            // std::cout << " + padding=" << padding << " = " << cursor << std::endl;
        }

        // compute scaling
        // std::cout << "cursor=" << cursor << ", width=" << (dispcx*2) << std::endl << std::endl;
        double curscale = fmin(1, dispcx*2.0 / cursor);         // do not expand system if already fits

        // actual draw with scaling applied
        sdrad = (dispcy/num_stars + log(s->volumetric_mean_radius / solar_radius) * 20) * curscale;
        draw_sphere(s, sdrad);
        cursor = s->drawnx + sdrad + padding*curscale;
        if (lbl_localsys)
        {
            const char *dispname = s->name;
            int disph = dispcy * 2;
            ImFont *font = global_font;
            double lfontsz = global_font_size;

            ImVec2 lsz = ImGui::CalcTextSize(dispname);
            int dy = s->drawny+sdrad+1;
            if (s->drawny < disph && dy > disph-lsz.y) dy = disph-lsz.y;
            ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(fmax(0, s->drawnx - lsz.x/2), dy),
                rgba_apply_redlight(Color::ensure_wcag_contrast(
                    (i == selected) ? global_style.selected_color : global_style.objlbl_color, whtbkgd, 4.5, -1, true)),
                dispname);
        }

        for (j=num_stars; j<n; j++)             // Stars always get listed first, so we can easily skip ahead to the planets.
        {
            cel_obj_class cls = lsyscache[j]->typeclass();
            if (cls != class_planet) continue;

            Planet *p = (Planet*)lsyscache[j];
            if (!p->orbit || p->orbit->center != s) continue;
            if (p->mass < 0.01 * earth_mass) continue;
            p->drawny = s->drawny;
            double pdrad = (dispcx/10 + log(p->volumetric_mean_radius / earth_radius)*10) * curscale;
            p->drawnx = cursor + pdrad;
            draw_sphere(p, pdrad);
            if (p->ring_radius) draw_ring_gpu(p);
            cursor = p->drawnx + pdrad + padding * curscale;
            // std::cout << "Draw " << p->name << " at " << p->drawnx << "," << p->drawny << std::endl;

            if (lbl_localsys)
            {
                const char *dispname = p->name;
                int disph = dispcy * 2;
                ImFont *font = global_font;
                double lfontsz = global_font_size;

                ImVec2 lsz = ImGui::CalcTextSize(dispname);
                int dy = p->drawny+pdrad+1;
                if (p->drawny < disph && dy > disph-lsz.y) dy = disph-lsz.y;
                ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(p->drawnx - lsz.x/2, dy),
                    rgba_apply_redlight(Color::ensure_wcag_contrast(
                        (i == selected) ? global_style.selected_color : global_style.objlbl_color, whtbkgd, 4.5, -1, true)),
                    dispname);
            }
        }
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
        spawn_texture_load(obj);

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
                rgba_apply_redlight(Color::ensure_wcag_contrast(
                    (i == selected) ? global_style.selected_color : global_style.objlbl_color, whtbkgd, 4.5, -1, true)),
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

ViewMode last_vmode = vm_spaceship;
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

    spawn_texture_load(cel);

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

    // An eclipse crossing the map. Gathered once here, so a frame with nothing eclipsing anything
    // -- which is nearly every frame -- pays one short list walk rather than anything per pixel.
    ShadowCaster sc_casters[max_eclipse_casters];
    int n_sc_casters = self_luminous ? 0
        : gather_shadow_casters(cel, lightcen, sc_casters, max_eclipse_casters);
    double sc_light_r = lightcen ? lightcen->get_equatorial_radius() : 0;
    Point sc_light_pos = lightcen ? lightcen->location.local_position : Point();

    // How dark the middle of an umbra is allowed to get on the map. Totality really does cut the
    // direct light to essentially nothing, but a map is something you read: a pure black hole
    // punched through the coastline says less than a very dark patch you can still see the
    // geography through.
    const double sclk_umbra_floor = 0.12;

    double equatorial_radius, theta, cos_theta, is_day = 1, is_night = 0;
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
            // is_night was left alone on this branch, so a star's own sun clock read whatever the
            // previous pixel -- or, on the first pixel, the stack -- had put there, and then
            // blended the night map by it.
            if (self_luminous) { is_day = 1; is_night = 0; }
            else
            {
                theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), _pi);
                if (fabs(theta) < half_pi)
                {
                    cos_theta = cos(theta);
                    is_day = fmin(1, pow(cos_theta, 1.0/3.0));
                    is_night = 0;
                }
                // TODO: Twilight
                else
                {
                    is_day = 0;
                    is_night = 1;
                }

                // The eclipse itself, asked per point of the map rather than per world, which is
                // the whole reason it comes out as a moving spot with a soft rim instead of a
                // uniform dimming. Only the daylit side can lose anything: a shadow crossing the
                // night side has nothing to take away.
                if (n_sc_casters && is_day > 0)
                {
                    double obsc = point_obscuration(land, sc_light_pos, sc_light_r,
                        sc_casters, n_sc_casters);
                    if (obsc > 0) is_day *= fmax(1.0 - obsc, sclk_umbra_floor);
                }
            }

            if (map) rgb = map->color_at(lat, lon);
            else rgb = prgb;

            if (nmap)
            {
                nrgb = nmap->color_at(lat, lon);
            }
            else
            {
                nrgb.r = rgb.r * 0.20;
                nrgb.g = rgb.g * 0.25;
                nrgb.b = rgb.b * 0.29;
            }

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
    // whereami >= 0, not just view_mode: nothing keeps the two in step. process_key_cmd_char()
    // sets whereami to -1 on 'w' (warp) and on '+' from a standing start, neither of which
    // touches view_mode -- so warping away while standing on a surface leaves horizon mode with
    // no world under it, and cels[-1] is read before any null check can help.
    if (view_mode == vm_horizon && whereami >= 0)
    {
        int j;
        CelestialObject *cel = cels[whereami];
        spawn_texture_load(cel);

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
            if (hz_dx[j] < -1e4) draw_marker[j] = false;
            // else if (hz_dy[j] < 0) hz_dy[j] = 0;
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
    // TODO: Generate a fictitious skyline.
    // See find_horizon() for why the whereami test has to be here as well as the view_mode one.
    if (view_mode == vm_horizon && whereami >= 0)
    {
        int i, j, j1;
        CelestialObject *cel = cels[whereami];
        if (!cel) return;
        cel_obj_class cls = cel->typeclass();
        Planet *p = (cls == class_planet || cls == class_moon) ? (Planet*)cel : nullptr;
    
        if (p->ring_radius) draw_ring_gpu(cel);                     // TODO: Rings appear in front of atmosphere - bad - but if we move this to draw_sky_gradient() it cuts them off.

        spawn_texture_load(cel);

        double is_day = fmin(1, luminous_flux*2.5e-11 + starlight);

        Map *map = cel->surf_map;
        RGB3Byte rgb = map ? map->color_at(viewer_lat, viewer_lon) : RGB3Byte(0, 8, 24);
        rgb.r *= is_day;
        rgb.g *= is_day;
        rgb.b *= is_day;

        bool is_water = (p->type == rocky)
            && (rgb.b > 0.8 * rgb.r)
            && (fmax(rgb.b, rgb.g) > 1.333 * rgb.r);                // this is admittedly a hare-brained kludge but it should work 99.9% of the time.

        if (p && p->type == lavaworld && p->night_map)
        {
            RGB3Byte nrgb = p->night_map->color_at(viewer_lat, viewer_lon);
            double xlavabrt = 3.0 * pow(magnbase,  sky_mag_shift);
            rgb.r = std::fmin(255, (double)rgb.r + xlavabrt * nrgb.r);
            rgb.g = std::fmin(255, (double)rgb.g + xlavabrt * nrgb.g);
            rgb.b = std::fmin(255, (double)rgb.b + xlavabrt * nrgb.b);
        }

        double hzheight[hznodes];
        if (show_terrain && (cels[whereami]->type >= (rocky & 0xfffffff0)) && !is_water)
        {
            // The raw noise shape only depends on where the viewer is standing, not on which way
            // they're looking or how far they've zoomed -- so it's cached and only regenerated
            // when viewer_lat/viewer_lon/whereami actually change, instead of every frame.
            static double cached_raw_noise[hznodes];
            static double cached_raw_min = 0;
            static double cached_lat = 1e293, cached_lon = 1e293;
            static int cached_whereami = -2;

            if (cached_whereami != whereami || cached_lat != viewer_lat || cached_lon != viewer_lon)
            {
                // not ideal, since the horizon will change unpredictably when walking, but for standing still this should work.
                pn.reseed(0xb0ad * 15*viewer_lat + 0x1cea * 60*viewer_lon);
                cached_raw_min = 9999;
                for (j = 0; j < hznodes; j++)
                {
                    double theta = _pi * 2 * j / hznodes;
                    double scale = 0.19;
                    double nx = scale * sin(theta), ny = scale * cos(theta);
                    cached_raw_noise[j] = fBm(nx, ny, 0, 29, 1.29, 0.8559);             // The octaves must stay a high number like this or the terrain won't look realistic.
                    if (cached_raw_noise[j] < cached_raw_min) cached_raw_min = cached_raw_noise[j];
                }
                cached_lat = viewer_lat;
                cached_lon = viewer_lon;
                cached_whereami = whereami;
            }

            double terrain_height = dispcx * 0.1 * zoom;
            for (j = 0; j < hznodes; j++)
                hzheight[j] = hz_dy[j] - terrain_height * (cached_raw_noise[j] - cached_raw_min) * 3;
        }
        else for (j = 0; j < hznodes; j++) hzheight[j] = hz_dy[j];

        double hz_fx = -1e9, hz_fy = 1e9;
        ImVec2 points[4];
        for (j = 0; j <= hznodes; j++) if (hz_dx[j%hznodes] > -1e5 && hzheight[j%hznodes] > -1e5)
        {
            j1 = j%hznodes;
            if (hz_fx > -1e8 && hz_fy < 1e8 && hz_fy > -1e4 && hzheight[j1] > -1e4 && fabs(hz_fx-hz_dx[j1]) < dispcx * zoom)
            {
                if (altitude > (fiftyseventh * 40) && (hzheight[j1] <= 0 || hz_fy <= 0)) goto _skip_hz_element;

                points[0] = ImVec2(hz_fx, hz_fy);
                points[1] = ImVec2(hz_dx[j1]+1, hzheight[j1]);
                points[2] = ImVec2(hz_dx[j1]+1, dispcy*2);
                points[3] = ImVec2(hz_fx, dispcy*2);

                ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4,
                    rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, dragging ? (192-128*is_day) : 255)));
            }

            _skip_hz_element:
            hz_fx = hz_dx[j1];
            hz_fy = hzheight[j1];
        }

        double hzbrt = _lum_r_comp*rgb.r + _lum_g_comp*rgb.g + _lum_b_comp*rgb.b;
        ImU32 mkrcol = rgba_apply_redlight((hzbrt >= 144) ? IM_COL32(0,0,0,255) : global_style.conslbl_color);
        if (show_grid) for (i = 0; i < 16; i++) if (draw_marker[j = i*64])
        {
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(hz_dx[j], hz_dy[j]), mkrcol, compass[i]);
            if (hzbrt >= 144) ImGui::GetBackgroundDrawList()->AddText(ImVec2(hz_dx[j]-1, hz_dy[j]), mkrcol, compass[i]);
        }
    }
}

void draw_sky_gradient()
{
    sky_grad.clear();
    if (whtbkgd) return;
    // See find_horizon(): this is only ever called in horizon mode, which does not by itself
    // guarantee there is a world underfoot.
    if (whereami < 0 || !cels[whereami]) return;
    if (!dragging && (cels[whereami]->typeclass() == class_planet || cels[whereami]->typeclass() == class_moon))
    {
        Planet *p = (Planet*)cels[whereami];
        if (p->get_surface_pressure())
        {
            double particulates = p->get_particulates();
            double Rayleigh = 1.0 - particulates;
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
            double  r = fmin(1, (Rayleigh * 0.37 + particulates * pcol.red  ) * skylight),
                    g = fmin(1, (Rayleigh * 0.58 + particulates * pcol.green) * skylight),
                    b = fmin(1, (Rayleigh * 0.81 + particulates * pcol.blue ) * skylight),
                    a = fmin(1, pow(p->get_surface_pressure(), 0.1) * skylight);

            double redden = 0, sunset_r = 0, sunset_g = 0, sunset_b = 0;
            if (mycenobj)
            {
                double atm_high_unused[3], atm_low[3], atm_umbra_unused[3];
                atmosphere_colors(p, atm_high_unused, atm_low, atm_umbra_unused);

                double sun_elev = sin(mycenobj->Decl_as_radians(here));
                double redden_t = fmin(1.0, fmax(0.0, (0.35 - sun_elev) / (0.35 - 0.02)));
                redden = redden_t * redden_t * (3.0 - 2.0 * redden_t);

                sunset_r = fmin(1, atm_low[0] * skylight);
                sunset_g = fmin(1, atm_low[1] * skylight);
                sunset_b = fmin(1, atm_low[2] * skylight);
            }
            const double kSkyReddenVerticalFalloff = 0.99;

            unsigned char r255, g255, b255;
            for (int y = fmin(hz_y, dispcy*2-1); y>=0; y--)
            {
                double rr = r * (1 - redden) + sunset_r * redden;
                double gg = g * (1 - redden) + sunset_g * redden;
                double bb = b * (1 - redden) + sunset_b * redden;
                r255 = rr*255; g255 = gg*255; b255 = bb*255;
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, y), ImVec2(x_extent, y),
                    rgba_apply_redlight(IM_COL32( (int)(r255), (int)(g255), (int)(b255), (int)(a*255) ) ));

                r *= 0.999;
                g *= 0.9995;
                b *= 0.9999;
                redden *= kSkyReddenVerticalFalloff;

                sky_grad[y] = RGB3Byte(r255*a, g255*a, b255*a);
            }
        }
    }
}

void draw_cons_lines()
{
    if (!cels[1]) return;
    if (view_mode == vm_system) return;

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
            if (constellations[i].lines[l].a->deleted) continue;
            if (constellations[i].lines[l].b->deleted) continue;

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
                wrapped_line(ImVec2(dx1, dy1), ImVec2(dx2, dy2),
                    rgba_apply_redlight(Color::adjust_alpha(global_style.consline_color, 0.21)),
                    1, io);
        }
    }

    // Constellation labels
    n = constellations.size();
    ImU32 cbcol = rgba_apply_redlight(Color::adjust_alpha(global_style.consline_color, 0.05));
    if (show_labels || (show_consln && !draw_actual_conslines)) for (l=0; l<n; l++)
    {
        Point lconsdir;

        // Constellation boundaries, drawn as a dashed line: connect every other
        // pair of adjacent perimeter points, leaving the pairs in between as gaps.
        m = constellations[l].bounds.size();
        float pdx = 0, pdy = 0;
        bool pvalid = false;
        for (i=0; i<m; i++)
        {
            Point cbd = Point::from_ra_dec(constellations[l].bounds[i].RA, constellations[l].bounds[i].decl, light_year);
            cbd = to_viewer_plane(cbd);
            cbd = refract_true_point(cbd);
            lconsdir += cbd;
            Cartesian2D cart(cbd, azimuth+azimuth_correction, altitude, zoom);
            float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);

            bool valid = !(dx < 0 || dy < 0);
            if (draw_actual_conslines && (i % 2) == 1 && pvalid && valid)
                wrapped_line(ImVec2(pdx, pdy), ImVec2(dx, dy), cbcol, io);

            pdx = dx; pdy = dy; pvalid = valid;
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
                rgba_apply_redlight(Color::ensure_wcag_contrast(global_style.conslbl_color, whtbkgd, 4.5, -1, true)),
                constellations[l].name.c_str());
        }
    }

    if (show_axes)
    {
        Point axisdir[6] = {xaxis, yaxis, zaxis, center-xaxis, center-yaxis, center-zaxis};
        for (i=0; i<6; i++)
        {
            Point laxdir = to_viewer_plane(axisdir[i]);
            laxdir = refract_true_point(laxdir);
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
    cc[0] = rgba_apply_redlight(Color::adjust_alpha(global_style.cursor_color1, 0.18));
    cc[1] = rgba_apply_redlight(Color::adjust_alpha(global_style.cursor_color2, 0.17));
    cc[2] = rgba_apply_redlight(Color::adjust_alpha(global_style.cursor_color3, 0.16));

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

    // A seed was derived here from the viewer's latitude and longitude, assigned twice and never
    // read, around a commented-out std::srand that the mutex pair existed to protect. Nothing in
    // this function is random any more, so all of it is gone. The seeding belongs back here if
    // the sky ever grows the individual clouds sketched out at the bottom of the function.

    // See find_horizon(): horizon mode does not imply a world underfoot.
    if (whereami < 0) return;
    CelestialObject *cel = cels[whereami];
    if (!cel || !cel->cloud_map) return;

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
