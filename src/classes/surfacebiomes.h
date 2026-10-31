#ifndef _AlienorumSurfaceBiomes
#define _AlienorumSurfaceBiomes

#include <cmath>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>
#include "noise.h"

namespace alienorum
{
    class SurfaceBiomes
    {
        PerlinNoise noise;
        uint32_t world = 0;

        public:
        explicit SurfaceBiomes(uint32_t world_id = 0) : noise(world_id), world(world_id)
        {
        }

        void set_world(uint32_t world_id)
        {
            if (world != world_id)
            {
                noise.reseed(world_id);
                world = world_id;
            }
        }

        uint32_t world_id() const { return world; }
        inline static constexpr std::array<double, 2> scales = {160.0, 36.0};
        inline static constexpr std::array<double, 2> amplitudes = {32.0, 6.0};
        static double maximum_displacement(double strength)
        {
            return sqrt(3.0) * 2 * (amplitudes[0] + amplitudes[1]) * fabs(strength);
        }
        static double gradient_bound(double strength)
        {
            double sum = 0;
            for (size_t octave = 0; octave < scales.size(); ++octave) sum += amplitudes[octave] / scales[octave];
            return 9 * sum * fabs(strength);
        }
        static double hessian_bound(double strength)
        {
            double sum = 0;
            for (size_t octave = 0; octave < scales.size(); ++octave)
                sum += amplitudes[octave] / (scales[octave] * scales[octave]);
            return 40 * sum * fabs(strength);
        }
        static double angular_bound(double radius, double strength = 1.0)
        {
            // Each gradient is bounded by two; interpolation is a convex combination.
            // Both octaves together displace each Cartesian component by at most 76 m.
            const double displacement = maximum_displacement(strength);
            if (!(radius > displacement)) return 4.0;
            return asin(displacement / radius) + 1.0e-12;
        }

        std::pair<double, double> coordinates(double radius, double latitude, double longitude, double strength = 1.0)
        {
            if (radius <= 0 || strength == 0)
            {
                return std::make_pair(latitude, longitude);
            }
            const double x = radius * cos(latitude) * cos(longitude);
            const double y = radius * cos(latitude) * sin(longitude);
            const double z = radius * sin(latitude);
            double dx = 0, dy = 0, dz = 0;
            // Cartesian displacement stays continuous at the date line and poles.
            for (int octave = 0; octave < 2; ++octave)
            {
                const double scale = scales[octave];
                const double amplitude = amplitudes[octave] * strength;
                const double nx = x / scale, ny = y / scale, nz = z / scale;
                dx += amplitude * noise.noise(nx + 19.3, ny + 7.1, nz + 31.7);
                dy += amplitude * noise.noise(nx + 43.1, ny + 53.7, nz + 11.9);
                dz += amplitude * noise.noise(nx + 13.7, ny + 29.3, nz + 67.1);
            }
            const double length = hypot(hypot(x + dx, y + dy), z + dz);
            return std::make_pair(asin(std::clamp((z + dz) / length, -1.0, 1.0)), atan2(y + dy, x + dx));
        }
    };

    // Relief is in meters along a circular horizon ring, with sea level at zero.
    inline void taper_coastal_relief(std::vector<double> &relief, const std::vector<bool> &water,
        double spacing, double incline)
    {
        const int count = static_cast<int>(relief.size());
        if (!count || water.size() != relief.size())
        {
            return;
        }
        std::vector<int> distance(count, count);
        bool coast = false;
        for (int i = 0; i < count; ++i)
        {
            if (water[i])
            {
                distance[i] = 0;
                coast = true;
            }
        }
        if (!coast)
        {
            return;
        }
        for (int i = 0; i < 2 * count; ++i)
        {
            const int j = i % count;
            distance[j] = std::min(distance[j], distance[(j + count - 1) % count] + 1);
        }
        for (int i = 2 * count - 1; i >= 0; --i)
        {
            const int j = i % count;
            distance[j] = std::min(distance[j], distance[(j + 1) % count] + 1);
        }
        for (int i = 0; i < count; ++i)
        {
            const double limit = distance[i] * spacing * tan(incline);
            relief[i] = relief[i] > 0 ? -relief[i] * expm1(-limit / relief[i]) : 0;
        }
    }
}

#endif
