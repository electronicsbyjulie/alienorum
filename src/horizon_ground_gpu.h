#ifndef ALIENORUM_HORIZON_GROUND_GPU_H
#define ALIENORUM_HORIZON_GROUND_GPU_H

#include <vector>
#include "imgui.h"

namespace alienorum
{
    struct HorizonGroundRect
    {
        int left, top, right, bottom;
        ImU32 color;
    };

    struct HorizonGroundGpuStats
    {
        unsigned int tiles = 0;
        unsigned int pixels = 0;
    };

    // The CPU certifies each tile's color; the GPU applies the per-pixel terrain
    // coverage and horizon masks. Boundary tiles retain the reference painter.
    bool prepare_horizon_ground_gpu(int width, int height, const std::vector<float> &distances,
        const std::vector<double> &horizon, const std::vector<int> &base_top, bool terrain);
    void queue_horizon_ground_gpu(std::vector<HorizonGroundRect> rectangles);
    void clear_horizon_ground_gpu();
    const HorizonGroundGpuStats &horizon_ground_gpu_stats();

    // Runtime reference selection is for image comparisons; the compile-time
    // switch in visuals.h always takes precedence.
    extern bool horizon_ground_gpu_reference;
}

#endif
