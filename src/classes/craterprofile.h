#pragma once

#include <algorithm>
#include <cmath>

namespace alienorum
{
    struct CraterProfile
    {
        static double transition_diameter(double gravity, bool ice)
        {
            const double reference = ice ? 6000 : 15000;
            return std::clamp(reference * 1.62 / std::max(.1, gravity), 1200.0, 60000.0);
        }

        static double depth(double diameter_m)
        {
            // Fresh-crater reference envelope, metres. Pike's simple/complex
            // relations are tabulated by the Lunar and Planetary Institute:
            // https://www.lpi.usra.edu/lunar/tools/lunarcratercalc/theory.pdf
            if (!(diameter_m > 0) || !std::isfinite(diameter_m)) return 0;
            const double km = diameter_m * .001;
            return 1000 * std::min(.196 * std::pow(km, 1.010), 1.044 * std::pow(km, .301));
        }

        static double smooth(double value)
        {
            const double t = std::clamp(value, 0.0, 1.0);
            return t * t * (3 - 2 * t);
        }

        static double elevation(double radius, double cavity_depth, double rim_height,
            double rim_width, double central_peak, bool peak_ring = false)
        {
            const bool complex = central_peak > 0;
            const double floor = complex ? .35 : 0;
            const double wall = smooth((radius - floor) / (1 - floor));
            const double bowl = radius < 1 ? -cavity_depth * (1 - wall) : 0;
            const double rim = rim_height * std::exp(-std::pow((radius - 1) / std::max(.01, rim_width), 2));
            const double peak_radius = peak_ring ? .30 : 0;
            const double peak = central_peak * std::exp(-std::pow((radius - peak_radius) / (peak_ring ? .09 : .15), 2));
            return bowl + rim + peak;
        }

        static double rim_albedo(double radius, double rim_width, double freshness, double patch)
        {
            // Fresh ejecta can differ in albedo; it is not a baked lighting ring.
            const double profile = std::exp(-std::pow((radius - 1) / std::max(.01, rim_width), 2));
            return 1 + .025 * std::clamp(freshness, 0.0, 1.0) * profile * std::clamp(patch, 0.0, 1.0);
        }
    };
}
