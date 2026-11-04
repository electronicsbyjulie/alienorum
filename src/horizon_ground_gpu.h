#ifndef ALIENORUM_HORIZON_GROUND_GPU_H
#define ALIENORUM_HORIZON_GROUND_GPU_H

#include <vector>
#include <array>
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
        unsigned int haze_tiles = 0;
        unsigned int depth_uploads = 0, horizon_uploads = 0;
    };

    struct HorizonGroundHaze
    {
        std::array<float, 3> extinction{}, light{};
        // Screen center, focal length, sine of viewing altitude.
        std::array<float, 4> camera{};
        // Cosine of viewing altitude, curvature, eye height, horizon distance.
        std::array<float, 4> surface{};
    };

    // The CPU certifies each tile's color; the GPU applies the per-pixel terrain
    // coverage and horizon masks. Boundary tiles retain the reference painter.
    bool prepare_horizon_ground_gpu(int width, int height, const std::vector<float> &distances,
        const std::vector<double> &horizon, const std::vector<int> &base_top, bool terrain,
        const HorizonGroundHaze& haze = {});
    void queue_horizon_ground_gpu(std::vector<HorizonGroundRect> rectangles);
    void clear_horizon_ground_gpu();
    void reset_horizon_ground_gpu_stats();
    const HorizonGroundGpuStats &horizon_ground_gpu_stats();

    // Runtime reference selection is for image comparisons; the compile-time
    // switch in visuals.h always takes precedence.
    extern bool horizon_ground_gpu_reference;
}

#endif
