
#ifndef _AlienorumSphereImpostor
#define _AlienorumSphereImpostor

// Deliberately does not include globals.h, which pulls SDL_opengl.h: sphere_impostor.cpp uses
// imgui's bundled GL loader, whose PFNGL...PROC typedefs clash with SDL_opengl.h's in one
// translation unit. Depending on imgui.h alone (for ImVec2/ImU32) keeps that loader out of every
// file that includes this one.
#include "imgui.h"

namespace alienorum
{
    // Eclipse casters per impostor draw. Four, since the shader takes them as one mat4 uniform,
    // a column each, and mat4 is the widest uniform this project's stripped GL loader can set.
    // Well past anything real: two shadows on one body at once is already an exotic sight.
    const int max_eclipse_casters = 4;

    // One body whose shadow can fall on the object being drawn. The shader works out per pixel
    // how much of the light source's *disc* it hides as seen from that point of the surface,
    // which is what gives a real eclipse its umbra, penumbra, and annular case.
    struct EclipseCaster
    {
        // Caster center relative to the center of the object being drawn: camera space, same
        // units as cx/cy/cz, but a difference of two positions. Relative on purpose -- the drawn
        // position carries a refraction offset in horizon mode that the shadow geometry must not
        // see, and a difference cancels it exactly.
        double dx, dy, dz;

        // Caster radius, in metres. The shadow is treated as cast by a sphere: a caster's own
        // oblateness sits far below the penumbra's softness at any visible distance.
        double r;

        // What this caster's umbra looks like from inside. A body with air casts no black shadow:
        // sunlight grazing its limb is refracted inwards and reddened by the long slant path,
        // which is why a totally eclipsed Moon turns copper. umbra_tint is that light's color
        // (brightest channel normalized to 1), umbra_light its brightness against direct
        // sunlight. 0 gives the hard black umbra an airless caster really does throw.
        double umbra_tint[3];
        double umbra_light;
    };

    // All inputs describing the sphere itself, kept as plain scalars (no Point/Rotation types)
    // so this header stays free of a globals.h dependency -- see the comment above.
    struct SphereImpostorInput
    {
        // Center in the app's "camera space": after to_viewer_plane() and Cartesian2D's
        // azimuth/altitude rotation, before its perspective divide. Metres, like everything else.
        double cx, cy, cz;

        // r is the bounding radius, max of the three axes, used for the screen-space bounding
        // quad (tangent_bounds()) and lent to the ring impostor's occlusion test. For a
        // non-spherical body it over-estimates the true silhouette, which only costs some cheap
        // discarded fragments.
        //
        // axis_x/axis_y/axis_z are the semi-axes along local +X/+Y/+Z (+Z being the vector
        // product of the other two), all equal to r for a plain sphere. Two real shapes use them:
        //   - Oblate planet: axis_x=axis_z=equatorial_radius, axis_y scaled by (1-oblateness).
        //   - Triaxial tidally-locked moon: axis_x=width/2 (orbit-direction), axis_y=height/2
        //     (polar), axis_z=depth/2 (pointing at the host planet, lon=0).
        double r, axis_x, axis_y, axis_z;

        // The object's local +X and +Y axes (Point::from_ra_dec's convention: x=-sin(lon)cos(lat),
        // y=sin(lat)) in the same camera space as cx,cy,cz -- the local basis run through the same
        // placement chain as the center. With their vector product for +Z, the shader uses them to
        // rotate a surface normal back into the object's frame and recover lat/lon for sampling.
        double basisX[3], basisY[3];

        // GL texture names from gputex_for(), or 0 when unavailable (still loading, or no night
        // map at all). Without a day map the disc is fallback_color, unlit; without a night map
        // the unlit side gets a flat night_illum ambient level instead of city lights.
        unsigned int day_map_texture;
        unsigned int night_map_texture;
        ImU32 fallback_color;

        // GL texture name (gputex_bump_for() in gputex.h) for the day map's bump/elevation
        // data, or 0 if unavailable (most objects never load one). Perturbs the shading normal
        // per-pixel (bump mapping -- the true ray/ellipsoid intersection and silhouette are
        // never affected, only the lighting) to fake the rough, cratered look a real rocky
        // body's terminator has, which the perfectly smooth analytic disc has no other way to
        // show -- matches the CPU path's own use of the same bump data (Map::elevation_at()),
        // just as a lighting perturbation instead of an actual per-vertex displacement (the
        // impostor has no vertices to displace).
        //
        // bump_strength is pre-divided, on the CPU side, by the object's own
        // estimate_bump_scale() -- the same value bump_data's elevations were originally scaled
        // by at load time (see Map::load_from_jpeg/_png's "as_bump" branch) -- rather than by
        // the object's radius. That value already folds in both the object's size and (for
        // worlds with an atmosphere) a surface-pressure factor, so normalizing by it keeps the
        // resulting effect visually consistent across bodies whose elevation data was baked at
        // very different absolute scales (an earlier version divided by radius alone, which
        // left the pressure factor uncancelled: Earth's atmosphere-driven elevation range is
        // roughly 11x its radius-only share versus the airless Moon's, so the same tuning
        // constant read as tasteful on the Moon and overdone on Earth). 0 disables the effect
        // (same as bump_map_texture==0, kept separate so a caller could in principle dial it
        // down without dropping the texture).
        unsigned int bump_map_texture;
        double bump_strength;

        // Direction from the object's center towards its light source (e.g. planet -> star),
        // in the same camera space as cx,cy,cz -- unit length. Unused (may be left zeroed)
        // when self_luminous is true.
        double light_dir[3];

        // White-balance-adjusted tint derived from the light source's own color (matches the
        // CPU path's `daylight` computation in visuals.cpp: normalize the light's color so its
        // brightest channel is 1, then take a cube root to compensate for the eye's white
        // balance adaptation), multiplied into the lit side of the surface.
        double daylight_tint[3];

        // True for the object that *is* its own light source (a star) -- shades by view angle
        // (limb darkening) instead of by angle to a separate light source, and never blends a
        // night side.
        bool self_luminous;

        // Coefficients of the quadratic limb-darkening law used when self_luminous is set:
        //     I(mu)/I(0) = 1 - a*(1 - mu) - b*(1 - mu)^2,     mu = cos(angle from disc center)
        // Star::limb_darkening_coefficients() derives them from the star's own T_eff and log g.
        // Ignored when self_luminous is false. Left at 0 they give a flat, unshaded disc.
        double limb_a, limb_b;

        // Eclipses: bodies that sit between this object and its light source, close enough to
        // the line between them to throw part of their shadow onto it. num_casters entries of
        // casters[] are read (0 disables the whole test, which is the overwhelmingly common
        // case); the caller is expected to have already discarded casters whose shadow misses
        // this object entirely, since only max_eclipse_casters of them fit.
        //
        // light_angular_radius is the angular radius (radians) of the light source's own disc
        // as seen from this object, and is what makes the shadow soft: a shadow cast by a
        // point-like light source would have a hard edge, but a real star has an angular size,
        // so around the fully-shadowed umbra there is a penumbra where the caster hides only
        // part of the star's disc. 0 (with num_casters 0) means "no eclipse this draw" and skips
        // the per-pixel test outright. Unused when self_luminous is true -- a star's own disc
        // isn't lit by anything, and a body transiting in front of one is simply drawn over it.
        int num_casters;
        EclipseCaster casters[max_eclipse_casters];
        double light_angular_radius;

        // A ringed planet's own rings, shadowing the planet itself -- the dark band Saturn
        // wears across its northern hemisphere for half its year. This is the exact mirror of
        // the shadow the ring impostor already casts the other way (the planet darkening the
        // far side of its rings, see RingImpostorInput::light_dir), and it is genuinely the
        // planet's own rings only: another body's rings are far too distant to shadow anything
        // here.
        //
        // ring_normal is the ring plane's normal in camera space, unit length, computed exactly
        // as RingImpostorInput::normal is (both call ring_plane_normal() in visuals.cpp, so the
        // shadow can never drift out of alignment with the rings actually drawn). ring_inner_r/
        // ring_outer_r are the same equatorial_radius/ring_radius pair the ring impostor uses,
        // in meters. ringx_map_texture supplies the opacity, read through the identical curve
        // the ring shader itself applies, so a gap that shows as transparent in the rings casts
        // no shadow and a dense band casts a solid one. ring_outer_r <= ring_inner_r (the
        // default for every ringless body) disables the whole test.
        double ring_normal[3];
        double ring_inner_r, ring_outer_r;
        unsigned int ringx_map_texture;

        // The band of glowing air on the limb of a world with an atmosphere -- the thin blue
        // arc Earth wears in every photograph taken from orbit or from the Moon, reddening to
        // orange where it meets the terminator. Drawn as a shell outside the solid body, so the
        // impostor's own bounding quad is grown by atmosphere_height to make room for it (the
        // silhouette test itself is untouched: the solid body ends where it always did).
        //
        // atmosphere_height is how far out the glow is drawn, in meters -- a few pressure scale
        // heights (Planet::estimate_scale_height()), since the air's density, and with it the
        // glow, falls off exponentially rather than stopping anywhere. 0 disables the whole
        // thing, which is every airless body and every star.
        //
        // atmosphere_color is the color of light scattered by the air high up, and
        // atmosphere_low_color the redder color it takes on near the surface, where the line of
        // sight is long enough to have lost its blue -- the glow crossfades between the two with
        // altitude, which is what makes the arc blue at the top and sunset-orange at the bottom.
        // Both come from the same Rayleigh/particulate mix draw_sky_gradient() colors the sky
        // with from the ground, so a world's limb and its skies always agree.
        double atmosphere_height;
        double atmosphere_color[3];
        double atmosphere_low_color[3];

        // Ambient illumination level for the unlit side when no night-map texture is available
        // (matches the CPU path's `starlight` constant, used only when there's no night map to
        // supply "city lights" detail instead).
        double night_illum;

        // Mirrors the app's global night-vision "redlight mode" (see rgba_apply_redlight() in
        // color.cpp) -- applied in-shader since the lit/textured color here is only known at
        // the pixel level, unlike every other on-screen primitive where it's applied to an
        // already-fixed color on the CPU.
        bool redlight_mode;

        // Matches the CPU path's sky_grad blend (visuals.cpp draw_sphere(), vm_horizon only):
        // in horizon mode, standing on a body with an atmosphere, the sky itself glows near
        // the horizon and fades with altitude above it -- this blends that same glow over the
        // dark/unlit side of any other disc near the horizon, exactly the way it fades into a
        // real planet's own atmosphere in that view. sky_color is the already alpha-
        // premultiplied glow color (0-1) at sky_horizon_y (screen pixels, same convention as
        // everything else here); the per-row falloff above that (the CPU path's fixed 0.999/
        // 0.9995/0.9999 per-row decay for r/g/b) is reproduced analytically in the shader.
        // false unless the caller is actually in vm_horizon with a live sky gradient this frame.
        bool apply_sky_blend;
        double sky_color[3];
        double sky_horizon_y;
    };

    // Queues a GPU-rendered sphere impostor (see GPU_SPHERE_RENDERING_PLAN.md) into ImGui's
    // background draw list. `zoom` is the same perspective scale factor Cartesian2D uses;
    // dispcx/dispcy convert the resulting screen-space ("zdes") coordinates to pixels, matching
    // every other on-screen primitive in this app.
    //
    // The screen-space bounding box actually used is computed from the true tangent-line
    // geometry (exact at any distance/angle -- see sphere_impostor.cpp), not from projecting
    // the 3D center and assuming a symmetric circle around it, which breaks down badly at
    // close range / large angular size (e.g. a low-orbit satellite looking at a planet).
    // The bounds are written to *out_xmin/xmin/xmax/ymax (screen pixels) for the caller to
    // reuse (selection bounding box, etc). Returns false (and writes nothing) if the camera
    // is degenerate for this object (e.g. genuinely inside the sphere).
    bool queue_sphere_impostor(const SphereImpostorInput &in, double zoom,
        double dispcx, double dispcy,
        double *out_xmin, double *out_ymin, double *out_xmax, double *out_ymax);

    // All inputs describing a planet's ring system, analogous to SphereImpostorInput -- see
    // queue_ring_impostor() below and the comment there for why this exists as a real
    // ray/plane-intersection shader rather than the CPU path's polygon-mesh annulus.
    struct RingImpostorInput
    {
        // Planet center in camera space -- same convention (and, for a given object, the same
        // values) as SphereImpostorInput::cx/cy/cz.
        double cx, cy, cz;

        // Ring inner/outer radius (equatorial_radius and ring_radius respectively), same units
        // as cx/cy/cz (meters).
        double inner_r, outer_r;

        // Ring plane normal -- the object's local +Y (polar) axis in camera space. Computed by
        // the caller as SphereImpostorInput's basisY recovery chain with the *final* spin step
        // dropped entirely (rings don't spin with the planet -- see draw_ring_gpu()'s comment
        // for why simply reusing basisY as-is is wrong: spin there is the last of five chained
        // steps, applied to whatever the first four already turned (0,1,0) into, not to
        // (0,1,0) itself, so it is not a no-op in that position). Unit length.
        double normal[3];

        // GL texture names (gputex_for()) for the ring color/opacity maps, or 0 if unavailable.
        // Matches the CPU path's rmap/rxmap: ring_map supplies color (Map::color_at(0, xmapd)
        // scanned radially, lat fixed at 0), ringx_map's green channel supplies opacity via the
        // gossamer_rings falloff curve. fallback_color is used unlit when ring_map_texture==0
        // (a flat tan, matching the CPU path's `rgb = {225,208,192}` default).
        unsigned int ring_map_texture;
        unsigned int ringx_map_texture;
        ImU32 fallback_color;

        // Direction from the object's center towards its light source, camera space, unit
        // length -- used only for the eclipse/shadow test (is this ring point behind the
        // planet as seen from the light?), unused when self_luminous.
        double light_dir[3];
        bool self_luminous;

        // Matches the CPU path's pl->amt_lit -- the planet's own lit fraction, used as the
        // ring's baseline brightness outside of the planet's shadow (CPU path:
        // "0.15 + 0.44*amt_lit"). Rings don't get their own per-point Lambertian term in the
        // existing model; this is a straight port of that same simplification.
        double amt_lit;

        bool redlight_mode;
    };

    // Queues a GPU-rendered ring impostor into ImGui's background draw list, analogous to
    // queue_sphere_impostor() -- see the comment there for the zoom/dispcx/dispcy convention.
    // Unlike the sphere, a ring's screen-space bound is computed from the *outer* radius using
    // the same tangent-line geometry (a conservative over-estimate for a tilted ring's true
    // elliptical silhouette, never an under-estimate); the fragment shader discards every pixel
    // outside the true annulus, so the extra quad area just costs some cheap discards.
    // No output bounding box -- unlike the disc, nothing downstream requires the ring's
    // on-screen extent. Returns false (queues nothing) if the input is geometrically degenerate (e.g.
    // inner_r >= outer_r, or the camera is inside the outer radius).
    bool queue_ring_impostor(const RingImpostorInput &in, double zoom,
        double dispcx, double dispcy);
}

#endif
