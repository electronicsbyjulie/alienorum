#ifndef ALIENORUM_HORIZON_MESH_GPU_H
#define ALIENORUM_HORIZON_MESH_GPU_H

#include <array>
#include <vector>
#include "classes/noise.h"

namespace alienorum
{
    struct HorizonMeshGpuStats
    {
        bool accelerated = false;
        bool cold = false;
        unsigned int samples = 0;
        unsigned int builds = 0;
        double initialization_ms = 0;
        double generation_ms = 0;
    };

    // Returns the twelve-octave weighted noise totals. Geographic positions,
    // water, bump maps, and coastal constraints retain their CPU precision.
    bool generate_horizon_mesh_gpu(const std::vector<std::array<double, 3>> &positions,
        const PerlinNoise &noise, std::vector<double> &totals);
    void clear_horizon_mesh_gpu();
    const HorizonMeshGpuStats &horizon_mesh_gpu_stats();
    extern bool horizon_mesh_gpu_reference;
}

#endif
