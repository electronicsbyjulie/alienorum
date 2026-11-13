#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace alienorum
{
    struct PlantMeshVertex
    {
        std::array<float, 4> position;
        std::array<float, 3> normal;
    };

    // Unit spheroid plus a capped frustum. Instance dimensions set their scale.
    inline std::vector<PlantMeshVertex> build_plant_mesh(int segments, int rings)
    {
        segments = std::max(6, segments);
        rings = std::max(3, rings);
        constexpr double pi = 3.14159265358979323846;
        std::vector<PlantMeshVertex> vertices;
        const auto sphere = [&](int ring, int segment)
        {
            const double latitude = -pi * .5 + pi * ring / rings;
            const double longitude = 2 * pi * segment / segments;
            const float x = std::cos(latitude) * std::cos(longitude);
            const float y = std::sin(latitude);
            const float z = std::cos(latitude) * std::sin(longitude);
            return PlantMeshVertex{{x, y, z, 1}, {x, y, z}};
        };
        const auto triangle = [&](PlantMeshVertex a, PlantMeshVertex b, PlantMeshVertex c)
        {
            vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
        };
        for (int ring = 0; ring < rings; ++ring)
        {
            for (int segment = 0; segment < segments; ++segment)
            {
                const auto a = sphere(ring, segment), b = sphere(ring + 1, segment);
                const auto c = sphere(ring, segment + 1), d = sphere(ring + 1, segment + 1);
                if (ring > 0) triangle(a, b, c);
                if (ring + 1 < rings) triangle(c, b, d);
            }
        }
        for (int segment = 0; segment < segments; ++segment)
        {
            const double first = 2 * pi * segment / segments, second = 2 * pi * (segment + 1) / segments;
            const float x0 = std::cos(first), z0 = std::sin(first), x1 = std::cos(second), z1 = std::sin(second);
            PlantMeshVertex a{{x0, 0, z0, 0}, {x0, 0, z0}}, b{{x0, 1, z0, 0}, {x0, 0, z0}};
            PlantMeshVertex c{{x1, 0, z1, 0}, {x1, 0, z1}}, d{{x1, 1, z1, 0}, {x1, 0, z1}};
            triangle(a, b, c); triangle(c, b, d);
            a.normal = c.normal = {0, -1, 0};
            b.normal = d.normal = {0, 1, 0};
            triangle({{0, 0, 0, 0}, {0, -1, 0}}, a, c);
            triangle({{0, 1, 0, 0}, {0, 1, 0}}, d, b);
        }
        return vertices;
    }
}
