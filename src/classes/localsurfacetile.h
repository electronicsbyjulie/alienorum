#ifndef ALIENORUM_LOCAL_SURFACE_TILE_H
#define ALIENORUM_LOCAL_SURFACE_TILE_H

#include "surfacebiomes.h"
#include <limits>

namespace alienorum
{
    // A bounded lazy tile over a planet-fixed tangent chart. Every node uses the exact field.
    class LocalSurfaceTile
    {
        static constexpr int cells = 64, width = cells + 1;
        static constexpr double maximum_spacing = .125;
        double spacing = maximum_spacing, extent = cells * spacing * .5;
        std::vector<std::array<double, 3>> values;
        std::vector<unsigned char> populated;
        std::array<double, 3> normal = {}, east = {}, north = {};
        uint32_t world = 0, epoch = 0;
        const void* source = nullptr;
        double radius = 0, strength = 0, error = 0, maximum_offset = 0, pole_limit = 0, angle_error = 0;
        bool ready = false;
        size_t evaluations = 0;

        const std::array<double, 3>& node(int x, int y, SurfaceBiomes& field)
        {
            const size_t index = y * width + x;
            if (!populated[index])
            {
                const double u = x * spacing - extent, v = y * spacing - extent;
                std::array<double, 3> position;
                for (int axis = 0; axis < 3; ++axis)
                    position[axis] = radius * normal[axis] + u * east[axis] + v * north[axis];
                const double length = hypot(hypot(position[0], position[1]), position[2]);
                for (double& component : position) component = radius * component / length;
                const auto coordinate = field.coordinates(radius,
                    asin(std::clamp(position[2] / radius, -1.0, 1.0)), atan2(position[1], position[0]), strength);
                values[index] = {radius * cos(coordinate.first) * cos(coordinate.second),
                    radius * cos(coordinate.first) * sin(coordinate.second), radius * sin(coordinate.first)};
                populated[index] = 1;
                ++evaluations;
            }
            return values[index];
        }
        public:
        bool matches(uint32_t identity, const void* map, uint32_t generation, double r, double displacement) const
        {
            return ready && world == identity && source == map && epoch == generation && radius == r && strength == displacement;
        }
        bool configure(uint32_t identity, const void* map, uint32_t generation, double r, double latitude,
            double longitude, double displacement, double maximum_error = .01)
        {
            const double maximum = SurfaceBiomes::maximum_displacement(displacement);
            const double integer_limit = (std::numeric_limits<int>::max() - 1024.0) * SurfaceBiomes::scales[1];
            if (!std::isfinite(r) || !std::isfinite(latitude) || !std::isfinite(longitude) || !std::isfinite(displacement)
                || !(r > 2 * maximum) || r > integer_limit
                || fabs(latitude) + SurfaceBiomes::angular_bound(r, displacement) >= acos(.125))
            {
                ready = false;
                return false;
            }
            const double gradient = SurfaceBiomes::gradient_bound(displacement);
            const double hessian = SurfaceBiomes::hessian_bound(displacement);
            const double minimum_length = r - maximum;
            const double first = 1 + 3 * gradient;
            const double second = 3 / r + 3 * sqrt(3.0) * hessian + 9 * gradient / r;
            const double mapped_second = r * (second / minimum_length + 3 * first * first / (minimum_length * minimum_length));
            const double roundoff = 4096 * std::numeric_limits<double>::epsilon() * (1 + r) * first;
            if (!std::isfinite(maximum_error) || !(maximum_error > roundoff) || !(mapped_second > 0))
            {
                ready = false;
                return false;
            }
            const double next_spacing = std::min(maximum_spacing, sqrt(4 * .95 * (maximum_error - roundoff) / mapped_second));
            if (!(next_spacing > 32 * std::numeric_limits<double>::epsilon() * r))
            {
                ready = false;
                return false;
            }
            const double chart_angle = atan(sqrt(2.0) * cells * next_spacing * .5 / r);
            if (fabs(latitude) + chart_angle + SurfaceBiomes::angular_bound(r, displacement) >= acos(.125))
            {
                ready = false;
                return false;
            }
            const std::array<double, 3> direction = {cos(latitude) * cos(longitude), cos(latitude) * sin(longitude), sin(latitude)};
            double offset = 0;
            for (int axis = 0; axis < 3; ++axis) offset += pow(r * (direction[axis] - normal[axis]), 2);
            if (matches(identity, map, generation, r, displacement) && spacing == next_spacing
                && offset <= extent * extent * .25) return true;
            spacing = next_spacing;
            extent = cells * spacing * .5;
            world = identity;
            source = map;
            epoch = generation;
            radius = r;
            strength = displacement;
            maximum_offset = maximum;
            pole_limit = acos(.125) - SurfaceBiomes::angular_bound(radius, strength);
            normal = direction;
            east = {-sin(longitude), cos(longitude), 0};
            north = {-sin(latitude) * cos(longitude), -sin(latitude) * sin(longitude), cos(latitude)};
            values.resize(width * width);
            populated.assign(values.size(), 0);
            evaluations = 0;
            // Quintic fade has |f'| <= 2 and |f''| <= 6; gradients bound
            // component derivatives by 9 and 40. Propagate these through the
            // spherical chart and normalization before the bilinear error bound.
            error = spacing * spacing * mapped_second / 4 + roundoff;
            ready = std::isfinite(error) && error <= maximum_error && error < radius * .5;
            angle_error = ready ? asin(error / (radius - error)) : 0;
            return ready;
        }
        bool coordinates(SurfaceBiomes& field, double latitude, double longitude,
            std::pair<double, double>& result, double& angle)
        {
            if (!ready || field.world_id() != world || !std::isfinite(latitude) || !std::isfinite(longitude)
                || fabs(latitude) >= pole_limit) return false;
            const std::array<double, 3> position = {radius * cos(latitude) * cos(longitude),
                radius * cos(latitude) * sin(longitude), radius * sin(latitude)};
            double along = 0, u = 0, v = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                along += position[axis] * normal[axis];
                u += position[axis] * east[axis];
                v += position[axis] * north[axis];
            }
            if (!(along > 0)) return false;
            u *= radius / along;
            v *= radius / along;
            if (fabs(u) > extent || fabs(v) > extent) return false;
            const double sx = std::clamp((u + extent) / spacing, 0.0, double(cells));
            const double sy = std::clamp((v + extent) / spacing, 0.0, double(cells));
            const int x = std::min(cells - 1, static_cast<int>(floor(sx)));
            const int y = std::min(cells - 1, static_cast<int>(floor(sy)));
            const double fx = sx - x, fy = sy - y;
            const auto& a = node(x, y, field);
            const auto& b = node(x + 1, y, field);
            const auto& c = node(x, y + 1, field);
            const auto& d = node(x + 1, y + 1, field);
            std::array<double, 3> displaced;
            for (int axis = 0; axis < 3; ++axis)
                displaced[axis] = (a[axis] + fx * (b[axis] - a[axis])) * (1 - fy)
                    + (c[axis] + fx * (d[axis] - c[axis])) * fy;
            const double length = hypot(hypot(displaced[0], displaced[1]), displaced[2]);
            result = {asin(std::clamp(displaced[2] / length, -1.0, 1.0)), atan2(displaced[1], displaced[0])};
            angle = angle_error;
            return std::isfinite(result.first) && std::isfinite(result.second);
        }
        size_t sample_count() const { return evaluations; }
        double error_bound() const { return error; }
    };
}
#endif
