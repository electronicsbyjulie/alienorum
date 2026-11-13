#pragma once

#include "numeric.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace alienorum
{
    struct PlantCrownTexture
    {
        double cluster_size_m = .65;
        double leaf_size_m = .065;
        double density = .78;
        double contrast = .55;
        double roughness = .24;
        double vertical_stretch = 1.15;
        int pattern = 0;

        static PlantCrownTexture generated(uint32_t seed)
        {
            std::mt19937 random(seed ^ 0x6c656166u);
            const auto draw = [&](double low, double high)
            {
                return low + (high - low) * (random() / 4294967296.0);
            };
            PlantCrownTexture result;
            result.pattern = random() % 4;
            result.cluster_size_m = draw(.25, 1.4);
            result.leaf_size_m = draw(.025, .18);
            result.density = draw(.35, .95);
            result.contrast = draw(.3, .75);
            result.roughness = draw(.08, .4);
            result.vertical_stretch = draw(.45, 2.8);
            return result;
        }

        void apply(const nlohmann::json& values)
        {
            if (!values.is_object()) return;
            const auto field = [&](const char* key, double& target, double low, double high)
            {
                auto found = values.find(key);
                if (found == values.end()) return;
                double value = NAN;
                if (found->is_number()) value = found->get<double>();
                else if (found->is_string()) parse_numeric_value(found->get<std::string>(), value);
                if (std::isfinite(value) && value >= low && value <= high) target = value;
            };
            field("cluster_size_m", cluster_size_m, .001, 100);
            field("leaf_size_m", leaf_size_m, .001, 100);
            field("density", density, 0, 1);
            field("contrast", contrast, 0, 1);
            field("roughness", roughness, 0, 1);
            field("vertical_stretch", vertical_stretch, .01, 100);
            const auto found = values.find("pattern");
            if (found != values.end() && found->is_string())
            {
                const std::array<const char*, 4> names = {"broadleaf", "tufted", "ribbed", "veined"};
                for (int index = 0; index < 4; ++index)
                    if (*found == names[index]) pattern = index;
            }
        }
    };

    struct CrownNoiseSample
    {
        double value = 0;
        std::array<double, 3> gradient{};
    };

    inline CrownNoiseSample crown_periodic_noise(double x, double y, double z, int period)
    {
        const std::array<double, 3> position = {x, y, z};
        std::array<int, 3> cell{};
        std::array<double, 3> weight{}, derivative{};
        for (int axis = 0; axis < 3; ++axis)
        {
            cell[axis] = static_cast<int>(std::floor(position[axis]));
            const double t = position[axis] - cell[axis];
            weight[axis] = t * t * (3 - 2 * t);
            derivative[axis] = 6 * t * (1 - t);
        }
        CrownNoiseSample result;
        for (int corner = 0; corner < 8; ++corner)
        {
            uint32_t hash = 0x7f4a7c15u;
            std::array<double, 3> blend{}, slope{};
            for (int axis = 0; axis < 3; ++axis)
            {
                const int side = (corner >> axis) & 1;
                const int coordinate = ((cell[axis] + side) % period + period) % period;
                hash ^= uint32_t(coordinate) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
                blend[axis] = side ? weight[axis] : 1 - weight[axis];
                slope[axis] = side ? derivative[axis] : -derivative[axis];
            }
            hash ^= hash >> 16; hash *= 0x7feb352du;
            hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
            const double value = hash / 2147483648.0 - 1;
            result.value += value * blend[0] * blend[1] * blend[2];
            for (int axis = 0; axis < 3; ++axis)
                result.gradient[axis] += value * slope[axis] * blend[(axis + 1) % 3] * blend[(axis + 2) % 3];
        }
        return result;
    }

    // RGB stores a surface gradient; alpha stores its matching density field.
    // This generated volume repeats in all axes and carries no runtime asset file.
    inline std::vector<unsigned char> build_crown_texture_volume(int size = 64)
    {
        size = std::clamp(size, 8, 128);
        std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * size * 4);
        for (int z = 0; z < size; ++z)
        {
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const double nx = (x + .5) * 8 / size, ny = (y + .5) * 8 / size, nz = (z + .5) * 8 / size;
                    const auto first = crown_periodic_noise(nx, ny, nz, 8);
                    const auto second = crown_periodic_noise(nx * 2, ny * 2, nz * 2, 16);
                    const size_t pixel = (static_cast<size_t>(z) * size * size + y * size + x) * 4;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const double gradient = first.gradient[axis] * .7 + second.gradient[axis] * .6;
                        pixels[pixel + axis] = static_cast<unsigned char>(std::lround(std::clamp(.5 + gradient * .125, 0.0, 1.0) * 255));
                    }
                    const double value = .7 * first.value + .3 * second.value;
                    pixels[pixel + 3] = static_cast<unsigned char>(std::lround(std::clamp(.5 + value * .5, 0.0, 1.0) * 255));
                }
            }
        }
        return pixels;
    }
}
