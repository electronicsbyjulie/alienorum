#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace alienorum
{
    using TerrainVector = std::array<double, 3>;

    inline TerrainVector terrain_unit(TerrainVector value)
    {
        const double length = hypot(hypot(value[0], value[1]), value[2]);
        if (!(length > 0)) return {0, 1, 0};
        for (double& component : value) component /= length;
        return value;
    }

    inline TerrainVector terrain_position(double longitude, double latitude,
        const TerrainVector& axes, double elevation)
    {
        TerrainVector position = {-sin(longitude) * cos(latitude) * axes[0],
            sin(latitude) * axes[1], cos(longitude) * cos(latitude) * axes[2]};
        const double length = hypot(hypot(position[0], position[1]), position[2]);
        if (length > 0)
        {
            for (double& component : position) component *= (length + elevation) / length;
        }
        return position;
    }

    template<class Elevation>
    TerrainVector terrain_normal(double longitude, double latitude, const TerrainVector& axes,
        double longitude_step, double latitude_step, const Elevation& elevation)
    {
        const TerrainVector fallback = terrain_unit({-sin(longitude) * cos(latitude) / axes[0],
            sin(latitude) / axes[1], cos(longitude) * cos(latitude) / axes[2]});
        if (fabs(cos(latitude)) < 1e-4) return fallback;
        constexpr double terrain_half_pi = 1.57079632679489661923;
        const double north = std::min(terrain_half_pi, latitude + latitude_step);
        const double south = std::max(-terrain_half_pi, latitude - latitude_step);
        const auto east_point = terrain_position(longitude + longitude_step, latitude, axes,
            elevation(latitude, longitude + longitude_step));
        const auto west_point = terrain_position(longitude - longitude_step, latitude, axes,
            elevation(latitude, longitude - longitude_step));
        const auto north_point = terrain_position(longitude, north, axes, elevation(north, longitude));
        const auto south_point = terrain_position(longitude, south, axes, elevation(south, longitude));
        TerrainVector dx, dy;
        for (int axis = 0; axis < 3; ++axis)
        {
            dx[axis] = east_point[axis] - west_point[axis];
            dy[axis] = north_point[axis] - south_point[axis];
        }
        TerrainVector normal = terrain_unit({dy[1] * dx[2] - dy[2] * dx[1],
            dy[2] * dx[0] - dy[0] * dx[2], dy[0] * dx[1] - dy[1] * dx[0]});
        if (normal[0] * fallback[0] + normal[1] * fallback[1] + normal[2] * fallback[2] < 0)
        {
            for (double& value : normal) value = -value;
        }
        return normal;
    }

    inline double terrain_light_ratio(double surface_cosine, double radial_cosine)
    {
        constexpr double ambient = .22;
        return (ambient + (1 - ambient) * std::max(0.0, surface_cosine))
            / (ambient + (1 - ambient) * std::max(0.0, radial_cosine));
    }
}
