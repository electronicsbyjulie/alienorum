
#ifndef _AlienorumSphereImpostor
#define _AlienorumSphereImpostor

// Deliberately does not include globals.h (which pulls SDL_opengl.h): sphere_impostor.cpp
// includes imgui's own bundled GL loader for its raw shader/VAO/VBO calls, and that loader
// conflicts (duplicate/incompatible PFNGL...PROC typedefs) with SDL_opengl.h in the same
// translation unit -- see imgui_impl_opengl3_loader.h's own comment about this. Keeping this
// header's dependency down to just imgui.h (for ImVec2/ImU32) means any file including it
// (e.g. visuals.cpp, which does include globals.h/SDL_opengl.h) never pulls the loader in.
#include "imgui.h"

namespace alienorum
{
    // How many eclipse-casting bodies one impostor draw can carry (see
    // SphereImpostorInput::casters). Four, because the shader receives them as a single mat4
    // uniform -- one column per caster -- and mat4 is the widest uniform type this project's
    // stripped-down GL loader can actually set (see sphere_impostor.cpp's top-of-file comment:
    // glUniformMatrix4fv survives the stripping, glUniform3fv/4fv do not). Four is well past
    // what any real configuration calls for anyway; a body with two other bodies' shadows on it
    // at once is already an exotic sight.
    const int max_eclipse_casters = 4;

    // One body whose shadow can fall on the object being drawn -- i.e. one eclipse. The shader
    // computes, per pixel, how much of the light source's *disc* this body hides as seen from
    // that particular point on the surface, which is what gives a real eclipse its umbra and
    // penumbra (and its annular case) instead of a hard-edged binary shadow.
    struct EclipseCaster
    {
        // Caster center relative to the *center of the object being drawn* -- camera space,
        // same units and orientation as SphereImpostorInput::cx/cy/cz, but a difference of two
        // positions rather than a position. Deliberately relative: the object's own drawn
        // position carries an atmospheric-refraction offset in horizon mode (see
        // draw_sphere_gpu()'s display_space) that the shadow geometry should not see, and a
        // difference cancels it exactly.
        double dx, dy, dz;

        // Caster radius, same units (meters). Its shadow is treated as cast by a sphere of this
        // radius; a caster's own oblateness/triaxiality is ignored, being far below the
        // penumbra's own softness at any distance where the shadow is visible at all.
        double r;
    };

    // All inputs describing the sphere itself, kept as plain scalars (no Point/Rotation types)
    // so this header stays free of a globals.h dependency -- see the comment above.
    struct SphereImpostorInput
    {
        // Center in the app's existing "camera space" -- i.e. after to_viewer_plane() and the
        // azimuth/altitude rotation Cartesian2D itself applies, but before its perspective
        // divide (see point.cpp) -- in the same distance units as everything else in the app
        // (meters).
        double cx, cy, cz;

        // r is the overall bounding radius (max(axis_x,axis_y,axis_z)) used for the impostor's
        // screen-space bounding quad (see tangent_bounds()) and lent to the ring impostor's own
        // occlusion test -- for a non-spherical body this is a conservative over-estimate of
        // the true (smaller, direction-dependent) silhouette, same "slack costs some cheap
        // discards" tradeoff already used for the ring's own bounding quad.
        //
        // axis_x/axis_y/axis_z are the true semi-axes along the local +X/+Y/+Z directions (see
        // basisX/basisY below; +Z is their cross product) -- equal to r on all three for a
        // plain sphere, the common case. Two real shapes reuse this:
        //   - An oblate planet: axis_x=axis_z=equatorial_radius, axis_y=equatorial_radius*
        //     (1-oblateness) (flattened at the poles, matching the CPU path's own "obl"
        //     factor).
        //   - A moon with known depth/width/height (tidally locked, generally triaxial):
        //     axis_x=width/2 (orbit-direction), axis_y=height/2 (polar),
        //     axis_z=depth/2 (the axis pointing at the host planet -- lon=0 in
        //     Point::from_ra_dec's convention, matching the CPU path's own dwh scaling).
        double r, axis_x, axis_y, axis_z;

        // The object's local +X and +Y axes (as used by Point::from_ra_dec: x=-sin(lon)cos(lat),
        // y=sin(lat)), expressed in the same camera space as cx,cy,cz above -- i.e. these are
        // the local frame's basis vectors run through the same to_viewer_plane + spin + tilt +
        // camera rotation chain used to place the center. The shader uses them (plus their
        // cross product for local +Z) to rotate each pixel's camera-space surface normal back
        // into the object's own frame and recover lat/lon for texture sampling. Only used when
        // day_map_texture is nonzero.
        double basisX[3], basisY[3];

        // GL texture names (as produced by gputex_for() in gputex.h) for the day/surface and
        // night maps, or 0 if not available (e.g. still loading asynchronously, or the object
        // simply has no night map). day_map_texture==0 falls back to fallback_color, unlit;
        // night_map_texture==0 falls back to a flat night_illum ambient level instead of a
        // "city lights" texture on the unlit side.
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
