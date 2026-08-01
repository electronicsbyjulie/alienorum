
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
    // All inputs describing the sphere itself, kept as plain scalars (no Point/Rotation types)
    // so this header stays free of a globals.h dependency -- see the comment above.
    struct SphereImpostorInput
    {
        // Center and radius in the app's existing "camera space" -- i.e. after
        // to_viewer_plane() and the azimuth/altitude rotation Cartesian2D itself applies, but
        // before its perspective divide (see point.cpp) -- in the same distance units as
        // everything else in the app (meters).
        double cx, cy, cz, r;

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
        // (a limb-darkening-like falloff) instead of by angle to a separate light source, and
        // never blends a night side.
        bool self_luminous;

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
}

#endif
