#ifndef ALIENORUM_SURFACE_PHASE_FIXTURE_H
#define ALIENORUM_SURFACE_PHASE_FIXTURE_H

#include "../classes/celestial.h"
#include "../classes/surfacephase.h"
#include <chrono>
#include <thread>

class WaterPhaseTestMap : public alienorum::Map
{
public:
    explicit WaterPhaseTestMap(unsigned long width = 64, unsigned long height = 32, bool frozen_land = false)
    {
        image_width = width; image_height = height; allocated = width * height;
        lat_scale = height / _pi; lon_scale = width / (2 * _pi);
        red_data = new unsigned char[allocated]; green_data = new unsigned char[allocated]; blue_data = new unsigned char[allocated];
        bump_data = new double[allocated];
        surface_materials.resize(allocated);
        for (unsigned long i = 0; i < allocated; ++i)
        {
            const bool land = frozen_land && (i % width >= width / 2);
            red_data[i] = land ? 220 : 20; green_data[i] = land ? 235 : 70; blue_data[i] = land ? 245 : 140;
            bump_data[i] = land ? 500 : -100;
            surface_materials[i] = land ? alienorum::SurfaceMaterial::ground : alienorum::SurfaceMaterial::water;
        }
        procedurally_generated = frozen_land;
        touch_gen();
    }
    void copy_surface(const alienorum::Map& source)
    {
        std::vector<unsigned char> rgba(allocated * 4);
        source.export_rgba(rgba.data());
        for (unsigned long i = 0; i < allocated; ++i)
        {
            red_data[i] = rgba[i * 4]; green_data[i] = rgba[i * 4 + 1]; blue_data[i] = rgba[i * 4 + 2];
        }
        touch_gen();
    }
};

inline alienorum::LocalClimate uniform_phase_climate(double temperature)
{
    alienorum::LocalClimate climate;
    climate.mean_k = temperature; climate.contrast_k = climate.regional_k = climate.dry_belt_k = climate.lapse_k_m = 0;
    climate.redistribution = 1;
    return climate;
}

inline bool publish_water_phase(alienorum::Map& map, const alienorum::LocalClimate& climate, double epoch)
{
    const uint64_t before = map.water_phase_revision();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (auto state = map.phase_state()) state->scan = 0;
        if (auto state = map.phase_state()) state->batch_limit = 1;
        map.update_water_phase_tile(climate, epoch, .3, .3, 35, 101325, .5, 1000, false);
        if (map.water_phase_revision() != before) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
#endif
