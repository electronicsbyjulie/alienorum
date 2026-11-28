#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
        using Key = std::tuple<const void*, unsigned int, int, double, double, double, double, double, bool, bool>;
        Key saved{};
        bool valid = false;
        std::array<double, columns> angles{};
        std::vector<Layer> terrain_layers;
        std::vector<double> profile_distances;
        std::vector<double> profile_slopes;
        std::vector<double> far_heights;
        using FarKey = std::tuple<const void*, unsigned int, int, double>;
        FarKey far_key{};
        double far_latitude = 0, far_longitude = 0;

    public:
        template<class Sample>
        void update(const void* map, unsigned int generation, int world, double latitude,
            double longitude, double radius, double eye, double curvature, const Sample& elevation,
            bool layered = false, bool sample_foreground = false)
        {
            const Key key{map, generation, world, latitude, longitude, radius, eye, curvature, layered, sample_foreground};
            if (valid && key == saved) return;
            const double base = elevation(latitude, longitude);
            const double sin_lat = sin(latitude), cos_lat = cos(latitude);
            const double bending = .5 / radius - curvature;
            // Two thousand kilometres captures tall distant relief on large rocky worlds.
            // Limit the arc on small bodies; sample every 5 km or finer there.
            const double reach = std::min(2000000.0, radius * .5);
            constexpr int far_rows = 400;
            const int near_rows = sample_foreground ? 32 : 0;
            const int rows = far_rows + near_rows;
            std::vector<std::array<double, 3>> positions(layered ? rows * columns : 0);
            std::vector<double> slopes(layered ? rows * columns : 0);
            profile_slopes.resize(sample_foreground ? rows * columns : 0);
            terrain_layers.clear();
            std::vector<double> distances(rows), sines(rows), cosines(rows);
            const double first_far = reach / far_rows;
            const double first_near = std::min(1.0, first_far * .01);
            const FarKey far_sample_key{map, generation, world, radius};
            // Walking can move the camera by metres while the distant samples are
            // kilometres apart. Keep nearby geometry and the observer height exact.
            const double reuse_limit = std::min(8.0, first_far * .002);
            const double travel = radius * hypot(latitude - far_latitude,
                cos_lat * remainder(longitude - far_longitude, 2 * pi));
            const bool reuse_far = far_sample_key == far_key && far_heights.size() == far_rows * columns
                && travel <= reuse_limit && fabs(latitude - far_latitude) <= reuse_limit / radius
                && fabs(cos_lat) > .001;
            if (!reuse_far) far_heights.resize(far_rows * columns);
            for (int row = 0; row < rows; ++row)
            {
                distances[row] = row < near_rows
                    ? first_near * pow(first_far / first_near, double(row) / near_rows)
                    : first_far * (row - near_rows + 1);
                sines[row] = sin(distances[row] / radius);
                cosines[row] = cos(distances[row] / radius);
            }
            if (sample_foreground) profile_distances = distances;
            else profile_distances.clear();
            for (int column = 0; column < columns; ++column)
            {
                const double bearing = 2 * pi * column / columns;
                const double north = cos(bearing), east = sin(bearing);
                double highest = -std::numeric_limits<double>::infinity();
                for (int row = 0; row < rows; ++row)
                {
                    const double sine = sines[row], cosine = cosines[row];
                    double sample_height;
                    if (row >= near_rows && reuse_far)
                    {
                        sample_height = far_heights[column * far_rows + row - near_rows];
                    }
                    else
                    {
                        const double sine_lat = std::clamp(sin_lat * cosine + cos_lat * sine * north, -1.0, 1.0);
                        const double sample_lat = asin(sine_lat);
                        const double sample_lon = longitude + atan2(east * sine * cos_lat, cosine - sin_lat * sine_lat);
                        sample_height = elevation(sample_lat, sample_lon);
                        if (row >= near_rows) far_heights[column * far_rows + row - near_rows] = sample_height;
                    }
                    const double sample_radius = radius + sample_height;
                    const double horizontal = sample_radius * sine;
                    const double vertical = (sample_radius - radius - base) * cosine
                        + (radius + base) * (cosine - 1) - eye
                        + bending * distances[row] * distances[row];
                    const double slope = vertical / std::max(1e-12, horizontal);
                    highest = std::max(highest, slope);
                    if (sample_foreground) profile_slopes[column * rows + row] = highest;
                    if (layered)
                    {
                        const size_t index = row * columns + column;
                        positions[index] = {horizontal * east, sample_radius * cosine - radius - base, horizontal * north};
                        slopes[index] = slope;
                    }
                }
                angles[column] = atan(highest);
            }
            if (!reuse_far)
            {
                far_key = far_sample_key;
                far_latitude = latitude;
                far_longitude = longitude;
            }
            if (layered)
            {
                // Keep only faces that raise the horizon above all nearer rings.
                // Hidden terrain has no draw cost; normals remain physical, without refraction.
                std::array<double, columns> prior, next;
                const double reference_eye = eye + (sample_foreground ? std::max(0.0, base) : 0.0);
                prior.fill(-2 * sqrt(std::max(0.0, curvature * reference_eye)));
                for (int row = 0; row < rows; ++row)
                {
                    for (int column = 0; column < columns; ++column)
                    {
                        next[column] = std::max(prior[column], slopes[row * columns + column]);
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
                            std::min(atan(prior[column]), angles[column]), std::min(atan(prior[adjacent]), angles[adjacent]),
                            std::min(atan(next[column]), angles[column]), std::min(atan(next[adjacent]), angles[adjacent]), distances[row], normal});
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

        static double column_position(double bearing)
        {
            double position = fmod(bearing / (2 * pi), 1.0);
            if (position < 0) position += 1;
            return position < 1 ? position * columns : 0;
        }

        double angle(double bearing) const
        {
            const double position = column_position(bearing);
            const int first = static_cast<int>(position);
            const double fraction = position - first;
            return angles[first] * (1 - fraction) + angles[(first + 1) % columns] * fraction;
        }

        double distance(double bearing, double elevation) const
        {
            return distance_at_slope(bearing, tan(elevation));
        }

        double minimum_profile_slope(double distance) const
        {
            const auto end = std::upper_bound(profile_distances.begin(), profile_distances.end(), distance);
            if (end == profile_distances.begin()) return -std::numeric_limits<double>::infinity();
            const size_t row = end - profile_distances.begin() - 1;
            const size_t rows = profile_distances.size();
            double slope = profile_slopes[row];
            for (int column = 1; column < columns; ++column)
                slope = std::min(slope, profile_slopes[column * rows + row]);
            return slope;
        }

        bool covers_slope(double bearing, double slope) const
        {
            const size_t rows = profile_distances.size();
            if (!rows) return false;
            const double position = column_position(bearing);
            const int first = static_cast<int>(position);
            const double fraction = position - first;
            return slope <= profile_slopes[(first + 1) * rows - 1] * (1 - fraction)
                + profile_slopes[(((first + 1) % columns) + 1) * rows - 1] * fraction;
        }

        double distance_at_slope(double bearing, double slope) const
        {
            const size_t rows = profile_distances.size();
            if (!rows) return std::numeric_limits<double>::infinity();
            const double position = column_position(bearing);
            const int first = static_cast<int>(position);
            const double fraction = position - first;
            const double* left = profile_slopes.data() + first * rows;
            const double* right = profile_slopes.data() + ((first + 1) % columns) * rows;
            const auto slope_at = [&](size_t row) { return left[row] * (1 - fraction) + right[row] * fraction; };
            if (slope > slope_at(rows - 1)) return std::numeric_limits<double>::infinity();
            size_t low = 0, high = rows - 1;
            while (low < high)
            {
                const size_t middle = low + (high - low) / 2;
                if (slope_at(middle) < slope) low = middle + 1;
                else high = middle;
            }
            if (!low) return profile_distances[0];
            const double before = slope_at(low - 1), after = slope_at(low);
            const double share = after > before ? (slope - before) / (after - before) : 1;
            return profile_distances[low - 1] + share * (profile_distances[low] - profile_distances[low - 1]);
        }
    };
}
