#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <vector>

namespace alienorum
{
    // Observer-space elevation angles; independent of camera heading and zoom.
    class DistantSkyline
    {
        static constexpr int columns = 1024;
        static constexpr double pi = 3.14159265358979323846;
    public:
        struct Layer
        {
            double bearing_left, bearing_right;
            double bottom_left, bottom_right, top_left, top_right;
            double distance;
            std::array<double, 3> normal;
        };

    private:
        using Key = std::tuple<const void*, unsigned int, int, double, double, double, double, double, bool>;
        Key saved{};
        bool valid = false;
        std::array<double, columns> angles{};
        std::vector<Layer> terrain_layers;

    public:
        template<class Sample>
        void update(const void* map, unsigned int generation, int world, double latitude,
            double longitude, double radius, double eye, double curvature, const Sample& elevation, bool layered = false)
        {
            const Key key{map, generation, world, latitude, longitude, radius, eye, curvature, layered};
            if (valid && key == saved) return;
            const double base = elevation(latitude, longitude);
            const double sin_lat = sin(latitude), cos_lat = cos(latitude);
            const double bending = .5 / radius - curvature;
            // Two thousand kilometres captures tall distant relief on large rocky worlds.
            // Limit the arc on small bodies; sample every 5 km or finer there.
            const double reach = std::min(2000000.0, radius * .5);
            constexpr int rows = 400;
            std::vector<std::array<double, 3>> positions(layered ? rows * columns : 0);
            std::vector<double> elevations(layered ? rows * columns : 0);
            terrain_layers.clear();
            std::array<double, rows> distances{}, sines{}, cosines{};
            for (int row = 0; row < rows; ++row)
            {
                distances[row] = reach * (row + 1.0) / rows;
                sines[row] = sin(distances[row] / radius);
                cosines[row] = cos(distances[row] / radius);
            }
            for (int column = 0; column < columns; ++column)
            {
                const double bearing = 2 * pi * column / columns;
                const double north = cos(bearing), east = sin(bearing);
                double highest = -pi / 2;
                for (int row = 0; row < rows; ++row)
                {
                    const double sine = sines[row], cosine = cosines[row];
                    const double sample_lat = asin(std::clamp(sin_lat * cosine + cos_lat * sine * north, -1.0, 1.0));
                    const double sample_lon = longitude + atan2(east * sine * cos_lat,
                        cosine - sin_lat * sin(sample_lat));
                    const double sample_radius = radius + elevation(sample_lat, sample_lon);
                    const double horizontal = sample_radius * sine;
                    const double vertical = (sample_radius - radius - base) * cosine
                        + (radius + base) * (cosine - 1) - eye
                        + bending * distances[row] * distances[row];
                    const double angle = atan2(vertical, horizontal);
                    highest = std::max(highest, angle);
                    if (layered)
                    {
                        const size_t index = row * columns + column;
                        positions[index] = {horizontal * east, sample_radius * cosine - radius - base, horizontal * north};
                        elevations[index] = angle;
                    }
                }
                angles[column] = highest;
            }
            if (layered)
            {
                // Keep only faces that raise the horizon above all nearer rings.
                // Hidden terrain has no draw cost; normals remain physical, without refraction.
                std::array<double, columns> prior, next;
                prior.fill(-atan(2 * sqrt(std::max(0.0, curvature * eye))));
                for (int row = 0; row < rows; ++row)
                {
                    for (int column = 0; column < columns; ++column)
                    {
                        next[column] = std::max(prior[column], elevations[row * columns + column]);
                    }
                    for (int column = 0; column < columns; ++column)
                    {
                        const int adjacent = (column + 1) % columns;
                        if (next[column] <= prior[column] && next[adjacent] <= prior[adjacent]) continue;
                        const auto& left = positions[row * columns + column];
                        const auto& right = positions[row * columns + adjacent];
                        const auto inner_left = row ? positions[(row - 1) * columns + column] : std::array<double, 3>{0, 0, 0};
                        const auto inner_right = row ? positions[(row - 1) * columns + adjacent] : std::array<double, 3>{0, 0, 0};
                        std::array<double, 3> radial, tangent;
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            radial[axis] = left[axis] + right[axis] - inner_left[axis] - inner_right[axis];
                            tangent[axis] = right[axis] - left[axis];
                        }
                        std::array<double, 3> normal
                        {
                            radial[1] * tangent[2] - radial[2] * tangent[1],
                            radial[2] * tangent[0] - radial[0] * tangent[2],
                            radial[0] * tangent[1] - radial[1] * tangent[0]
                        };
                        const double length = hypot(hypot(normal[0], normal[1]), normal[2]);
                        if (!(length > 0)) continue;
                        for (double& value : normal) value /= length;
                        terrain_layers.push_back({2 * pi * column / columns, 2 * pi * (column + 1) / columns,
                            std::min(prior[column], angles[column]), std::min(prior[adjacent], angles[adjacent]),
                            std::min(next[column], angles[column]), std::min(next[adjacent], angles[adjacent]), distances[row], normal});
                    }
                    prior = next;
                }
            }
            saved = key;
            valid = true;
        }

        const std::vector<Layer>& layers() const { return terrain_layers; }

        static double illumination(const Layer& layer, const std::array<double, 3>& light, double diffuse = 0)
        {
            const double ambient = .22 + .6 * std::clamp(diffuse, 0.0, 1.0);
            const double direct = std::max(0.0, layer.normal[0] * light[0]
                + layer.normal[1] * light[1] + layer.normal[2] * light[2]);
            return (ambient + (1 - ambient) * direct)
                / (ambient + (1 - ambient) * std::max(0.0, light[1]));
        }

        double angle(double bearing) const
        {
            double position = fmod(bearing / (2 * pi), 1.0);
            if (position < 0) position += 1;
            position *= columns;
            const int first = static_cast<int>(position);
            const double fraction = position - first;
            return angles[first] * (1 - fraction) + angles[(first + 1) % columns] * fraction;
        }
    };
}
