#ifndef ALIENORUM_SCENERY_GPU_H
#define ALIENORUM_SCENERY_GPU_H

#include "imgui.h"

namespace alienorum
{
    struct SceneryPlantGpu
    {
        ImVec4 crown;
        ImVec2 trunk[4];
        ImU32 crown_color = 0, trunk_color = 0;
        float crown_visible = 1, trunk_visible = 1;
        float segments = 0, crown_first = 0, contour_row = 0;
    };
    struct SceneryGpuStats
    {
        unsigned int instances = 0, draws = 0;
    };
    bool prepare_scenery_gpu();
    void add_scenery_plant_gpu(const SceneryPlantGpu& plant, bool crown_first, bool circle = false);
    void flush_scenery_gpu();
    void clear_scenery_gpu();
    const SceneryGpuStats& scenery_gpu_stats();
    extern bool scenery_gpu_reference;
}
#endif
