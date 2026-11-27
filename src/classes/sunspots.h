#ifndef ALIENORUM_SUNSPOTS_H
#define ALIENORUM_SUNSPOTS_H

#include <algorithm>
#include <cmath>
#include "misc.h"

namespace alienorum
{
    struct SunspotProfile
    {
        double phase, core_fraction, twist, bridge_offset;
        double pixel_size;
        int filaments;
        bool pore, light_bridge;

        static double transition(double low, double high, double value)
        {
            const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
            return t*t*(3 - 2*t);
        }

        // Coordinates lie in the oriented tangent plane, in units of the spot radius.
        // The result is the local fraction of the full umbral temperature deficit.
        double deficit(double x, double y, double radius) const
        {
            if (radius >= 1.55) return 0;
            const double angle = std::atan2(y, x);
            const double edge_width = std::clamp(pixel_size * .8, .025, .28);
            const double edge_pattern = fBm(2.1*std::cos(angle) + phase, 2.1*std::sin(angle), phase, 2, 2, .5);
            const double lobes = .065 * std::cos(3*angle + phase) + .22 * (edge_pattern - .5);
            if (pore)
                return .91 * (1 - transition(.88 - edge_width, 1.06 + edge_width, radius - lobes));

            const double core_edge = core_fraction + lobes + .03 * std::sin(9*angle + 2*phase);
            const double penumbra = 1 - transition(.88 - edge_width, 1.08 + edge_width, radius - lobes);
            const double radial = std::clamp((radius - core_edge) / (1 - core_edge), 0.0, 1.0);
            const double flow = angle + twist * radial * std::sin(2*angle + phase) + .13 * radial * (edge_pattern - .5);
            const double frequency = filaments / (2*_pi);
            const double thread = fBm(frequency*std::cos(flow), frequency*std::sin(flow), 13 + phase + .55*radial,
                pixel_size < .035 ? 2 : 1, 2, .4);
            const double crest = transition(.36, .67, thread);
            const double dark_lane = transition(.64, .79, thread) * transition(.01, .05, .07 - pixel_size);
            const double strength = .51 + .49 * edge_pattern;
            const double inner_tip = .055 * crest * strength;
            const double core = 1 - transition(core_edge - edge_width - inner_tip,
                core_edge + edge_width - .4*inner_tip, radius);
            double result = core + (1 - core) * penumbra * (.56 - .24*radial - strength*(.27*crest - .17*dark_lane));

            if (core > .05 && pixel_size < .13)
            {
                const double dot_frequency = std::min(23.0, .48 / pixel_size);
                const double cells = fBm(x*dot_frequency + phase*3, y*dot_frequency - phase, 37 + phase, 2, 2, .5);
                const double dots = transition(.62, .84, cells);
                result -= .58 * core * dots;
                if (light_bridge)
                {
                    const double path = bridge_offset + .12 * std::sin(5*x + phase) + .10*x;
                    const double distance = std::abs(y - path);
                    const double bridge = 1 - transition(.013, .041 + .42*pixel_size, distance);
                    const double lane = (1 - transition(.002, .012, distance)) * transition(0, .025, .04 - pixel_size);
                    result -= core * bridge * (.52 + .28*cells - .31*lane);
                }
            }
            return std::clamp(result, 0.0, 1.0);
        }
    };
}

#endif
