#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace alienorum
{
    // Observer-space elevation angles; independent of camera heading and zoom.
    class DistantSkyline
    {
        static constexpr int columns = 1024;
        static constexpr double pi = 3.14159265358979323846;
        using Key = std::tuple<const void*, unsigned int, int, double, double, double, double, double>;
        Key saved{};
        bool valid = false;
        std::array<double, columns> angles{};

    public:
        template<class Sample>
        void update(const void* map, unsigned int generation, int world, double latitude,
            double longitude, double radius, double eye, double curvature, const Sample& elevation)
        {
            const Key key{map, generation, world, latitude, longitude, radius, eye, curvature};
            if (valid && key == saved) return;
            const double base = elevation(latitude, longitude);
            const double sin_lat = sin(latitude), cos_lat = cos(latitude);
            const double bending = .5 / radius - curvature;
            // Two thousand kilometres captures tall distant relief on large rocky worlds.
            // Limit the arc on small bodies; sample every 5 km or finer there.
            const double reach = std::min(2000000.0, radius * .5);
            constexpr int rows = 400;
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
                    highest = std::max(highest, atan2(vertical, horizontal));
                }
                angles[column] = highest;
            }
            saved = key;
            valid = true;
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
