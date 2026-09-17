
#ifndef _AlienorumSphereImpostor
#define _AlienorumSphereImpostor

// Deliberately does not include globals.h, which pulls SDL_opengl.h: sphere_impostor.cpp uses
// imgui's bundled GL loader, whose PFNGL...PROC typedefs clash with SDL_opengl.h's in one
// translation unit. Depending on imgui.h alone (for ImVec2/ImU32) keeps that loader out of every
// file that includes this one.
#include "imgui.h"

namespace alienorum
{
    const int max_eclipse_casters = 4;

    struct EclipseCaster
    {
        double dx, dy, dz;
        double r;

        double umbra_tint[3];
        double umbra_light;
    };

    struct SphereImpostorInput
    {
        double cx, cy, cz;
        double r, axis_x, axis_y, axis_z;

        double basisX[3], basisY[3];

        unsigned int day_map_texture;
        unsigned int night_map_texture;
        ImU32 fallback_color;

        unsigned int bump_map_texture;
        double bump_strength;

        double light_dir[3];

        double daylight_tint[3];

        bool self_luminous;

        double limb_a, limb_b;

        int num_casters;
        EclipseCaster casters[max_eclipse_casters];
        double light_angular_radius;

        double ring_normal[3];
        double ring_inner_r, ring_outer_r;
        unsigned int ringx_map_texture;

        double atmosphere_height;
        double atmosphere_color[3];
        double atmosphere_low_color[3];

        double night_illum;
        bool redlight_mode;

        bool apply_sky_blend;
        double sky_color[3];
        double sky_horizon_y;
    };

    bool queue_sphere_impostor(const SphereImpostorInput &in, double zoom,
        double dispcx, double dispcy, double scalex,
        double *out_xmin, double *out_ymin, double *out_xmax, double *out_ymax);

    void impostor_begin_frame();

    struct RingImpostorInput
    {
        double cx, cy, cz;
        double inner_r, outer_r;
        double normal[3];
        double oblateness;
        unsigned int ring_map_texture;
        unsigned int ringx_map_texture;
        ImU32 fallback_color;

        double light_dir[3];
        bool self_luminous;

        double amt_lit;

        bool redlight_mode;
    };

    bool queue_ring_impostor(const RingImpostorInput &in, double zoom,
        double dispcx, double dispcy, double scalex);
}

#endif
