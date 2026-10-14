#ifndef ALIENORUM_LAVAFISSURES_H
#define ALIENORUM_LAVAFISSURES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "noise.h"

namespace alienorum
{
    // Shared by the reflective surface and emissive night map. Sphere coordinates
    // avoid longitude seams, and each world's seed fixes its fracture network.
    class LavaFissures
    {
        PerlinNoise noise;

        static double smooth(double value)
        {
            const double t = std::clamp(value, 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }

        public:
        explicit LavaFissures(std::uint32_t seed) : noise(seed ^ 0x4c415641u)
        {
        }

        double intensity(double x, double y, double z)
        {
            // Unequal spacing comes from warped noise contours, with higher
            // frequencies introducing sharp kinks rather than smooth stripes.
            const double wx = x * 18.0 + 2.1 * noise.noise(x * 4.3 + 17.0, y * 4.3, z * 4.3);
            const double wy = y * 18.0 + 2.1 * noise.noise(x * 4.3, y * 4.3 + 31.0, z * 4.3);
            const double wz = z * 18.0 + 2.1 * noise.noise(x * 4.3, y * 4.3, z * 4.3 + 47.0);
            const double province = std::clamp(0.5 + noise.noise(x * 3.1 + 71.0, y * 3.1 + 19.0, z * 3.1), 0.0, 1.0);
            const double width = 0.035 + 0.055 * province;
            const double primary = noise.noise(wx, wy, wz)
                + 0.32 * noise.noise(wx * 2.7 + 11.0, wy * 2.7, wz * 2.7)
                + 0.13 * noise.noise(wx * 7.1, wy * 7.1 + 23.0, wz * 7.1);
            const double branch = noise.noise(wx + 83.0, wy + 41.0, wz + 29.0)
                + 0.32 * noise.noise(wx * 2.7, wy * 2.7 + 37.0, wz * 2.7)
                + 0.13 * noise.noise(wx * 7.1 + 53.0, wy * 7.1, wz * 7.1);
            const double major = std::max(smooth(1.0 - std::fabs(primary) / width),
                0.78 * smooth(1.0 - std::fabs(branch) / (width * 0.7)));
            const double minor_field = noise.noise(wx * 2.9 + 67.0, wy * 2.9 + 13.0, wz * 2.9);
            const double minor = smooth(1.0 - std::fabs(minor_field) / 0.04)
                * std::min(1.0, major * 3.0 + 0.1);
            // Quiet provinces interrupt the network; thinner branches remain
            // dimmer than exposed magma along the main fractures.
            const double activity = smooth((province - 0.30) / 0.35);
            return std::clamp((major + 0.22 * minor) * activity, 0.0, 1.0);
        }
    };
}

#endif
