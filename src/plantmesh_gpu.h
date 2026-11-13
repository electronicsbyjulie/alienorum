#pragma once

#include "imgui.h"
#include "classes/planttexture.h"

#ifndef PLANT_CROWN_TEXTURES
#define PLANT_CROWN_TEXTURES 1
#endif
#include <vector>

namespace alienorum
{
    struct PlantMeshInstance
    {
        ImVec4 base;
        ImVec4 dimensions;
        ImU32 crown_color = 0, trunk_color = 0;
        ImVec4 haze = ImVec4(1, 1, 1, 1);
        ImVec4 texture_frame = ImVec4(1, 0, 0, 0);
    };
    struct PlantMeshView
    {
        float focal = 1, center_x = 1, center_y = 1;
        float sin_pitch = 0, cos_pitch = 1;
        ImVec4 light = ImVec4(0, 1, 0, 1);
        float ambient = .3f;
        ImVec4 airlight;
        bool redlight = false;
        bool crown_textures = false;
        PlantCrownTexture crown_texture;
    };
    struct PlantMeshStats
    {
        unsigned int instances = 0, triangles = 0, draws = 0;
    };
    bool prepare_plant_mesh_gpu(const PlantMeshView& view, const std::vector<float>& terrain_depth = {});
    void add_plant_mesh_gpu(const PlantMeshInstance& plant, float screen_radius);
    void flush_plant_mesh_gpu();
    void clear_plant_mesh_gpu();
    const PlantMeshStats& plant_mesh_stats();
}
