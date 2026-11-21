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

    inline double terrain_night_light_ratio(double surface_cosine, double radial_cosine)
    {
        return std::clamp(1.0 + .65 * (surface_cosine - radial_cosine), .55, 1.25);
    }

    inline double terrain_solar_exposure(double horizon_cosine, double relative_height, double solar_radius_sine)
    {
        const double h = std::max(0.0, relative_height);
        const double elevation = horizon_cosine + std::sqrt(h * (2 + h)) / (1 + h);
        if (!(solar_radius_sine > 0)) return elevation > 0 ? 1.0 : 0.0;
        const double visible = std::clamp(.5 + .5 * elevation / solar_radius_sine, 0.0, 1.0);
        return visible * visible * (3 - 2 * visible);
    }
}

#define ALIENORUM_TERRAIN_SOLAR_GLSL \
    "float terrain_solar_exposure(float horizon_cosine, float relative_height, float solar_radius_sine)\n" \
    "{\n" \
    "    float h = max(0.0, relative_height);\n" \
    "    float elevation = horizon_cosine + sqrt(h * (2.0 + h)) / (1.0 + h);\n" \
    "    return solar_radius_sine > 0.0 ? smoothstep(-solar_radius_sine, solar_radius_sine, elevation) : step(1e-9, elevation);\n" \
    "}\n"

#define ALIENORUM_TERRAIN_NORMAL_GLSL \
    "vec3 terrain_position(float lon, float lat, vec3 axes, vec2 dx, vec2 dy)\n" \
    "{\n" \
    "    vec3 direction = vec3(-sin(lon) * cos(lat), sin(lat), cos(lon) * cos(lat));\n" \
    "    vec3 position = direction * axes;\n" \
    "    vec2 uv = vec2(fract(lon / (2.0 * PI) + 0.5), 0.5 - lat / PI);\n" \
    "    float height = textureGrad(uBumpMap, uv, dx, dy).r;\n" \
    "    float radius = length(position);\n" \
    "    return radius > 0.0 ? position * ((radius + height) / radius) : position;\n" \
    "}\n" \
    "\n" \
    "vec3 terrain_normal(float lon, float lat, vec3 axes, vec2 dx, vec2 dy)\n" \
    "{\n" \
    "    vec3 fallback = normalize(vec3(-sin(lon) * cos(lat), sin(lat), cos(lon) * cos(lat)) / axes);\n" \
    "    if (abs(cos(lat)) < 0.0001) return fallback;\n" \
    "    vec2 tex_size = vec2(textureSize(uBumpMap, 0));\n" \
    "    float dl = (2.0 * PI) / tex_size.x;\n" \
    "    float dp = PI / tex_size.y;\n" \
    "    vec3 east = terrain_position(lon + dl, lat, axes, dx, dy);\n" \
    "    vec3 west = terrain_position(lon - dl, lat, axes, dx, dy);\n" \
    "    vec3 north = terrain_position(lon, min((0.5 * PI), lat + dp), axes, dx, dy);\n" \
    "    vec3 south = terrain_position(lon, max(-(0.5 * PI), lat - dp), axes, dx, dy);\n" \
    "    vec3 normal = normalize(cross(north - south, east - west));\n" \
    "    return dot(normal, fallback) < 0.0 ? -normal : normal;\n" \
    "}\n" \
    "\n"
