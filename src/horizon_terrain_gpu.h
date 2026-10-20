#ifndef ALIENORUM_HORIZON_TERRAIN_GPU_H
#define ALIENORUM_HORIZON_TERRAIN_GPU_H

#include "classes/horizonterrain.h"

namespace alienorum
{
    struct HorizonTerrainGpuStats
    {
        bool accelerated = false;
        unsigned int triangles = 0;
        unsigned int pixels = 0;
        unsigned int repaired_pixels = 0;
        unsigned int max_candidates = 0;
        double preparation_ms = 0;
        double selection_ms = 0;
        double reconstruction_ms = 0;
    };

    // Input order matches the reference rasterizer. The target is reset by the
    // caller; a false result leaves it empty for the CPU fallback.
    bool rasterize_horizon_terrain_gpu(HorizonTerrainRaster &target,
        const std::vector<ProjectedTerrainVertex> &triangles, int width, int height);
    const HorizonTerrainGpuStats &horizon_terrain_gpu_stats();
    void clear_horizon_terrain_gpu();
    extern bool horizon_terrain_gpu_reference;
}

#endif
