#ifndef _AlienorumSunclockGpu
#define _AlienorumSunclockGpu

#include "imgui.h"

namespace alienorum
{
    const int max_sunclock_casters = 4;

    struct SunClockCaster
    {
        double dx, dy, dz;
        double radius;
    };

    struct SunClockGpuInput
    {
        double sclk_scale;
        double azimuth;
        double altitude;
        double zoom;

        unsigned int day_map_texture;
        unsigned int night_map_texture;
        unsigned int bump_map_texture;

        double fallback_color[3];
        double daylight_tint[3];

        bool self_luminous;
        bool redlight_mode;

        double body_axes[3];
        double rot_matrix[16];

        double light_dir[3];
        double light_radius;
        double light_pos_rel[3];

        int num_casters;
        SunClockCaster casters[max_sunclock_casters];
    };

    bool queue_sunclock_gpu(const SunClockGpuInput &in, double dispcx, double dispcy);
}

#endif
