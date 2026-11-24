#pragma once

#include <algorithm>
#include <cmath>

namespace alienorum
{
    class HorizonQuality
    {
        int level = 0;
        double slow_seconds = 0;
        double fast_seconds = 0;
        double frame_seconds = 0;
        double motion_recovery = 0;
        bool motion_budget = false;

    public:
        void observe(double seconds)
        {
            if (!(seconds > 0) || !std::isfinite(seconds))
            {
                *this = {};
                return;
            }
            frame_seconds = seconds;
            // With hysteresis, a faster preview does not immediately trigger a full repaint.
            motion_recovery = seconds < .027 ? motion_recovery + seconds : 0;
            if (seconds > .040) motion_budget = true;
            else if (motion_recovery >= .75) motion_budget = false;
            slow_seconds = seconds > .100 ? slow_seconds + std::min(seconds, .2) : 0;
            fast_seconds = seconds < .080 ? fast_seconds + seconds : 0;
            if (slow_seconds >= .5 && level < 2)
            {
                ++level;
                slow_seconds = 0;
            }
            else if (fast_seconds >= 2.0 && level > 0)
            {
                --level;
                fast_seconds = 0;
            }
        }

        bool can_refine_terrain() const { return frame_seconds > 0 && frame_seconds <= .100; }
        bool should_undo_terrain_refinement() const { return frame_seconds > 1.0 / 7; }
        int ground_stride() const { return 1 << level; }
        float cloud_scale() const { return 1.0f - .2f * level; }
        bool limit_motion_detail() const { return motion_budget; }
        double wave_scale() const { return 1.0 - .25 * level; }
        double silhouette_error() const { return .025 + .35 * level; }
        int shore_stride() const { return 1 << level; }
    };
}
