#ifndef ALIENORUM_SCENERY_DRAW_H
#define ALIENORUM_SCENERY_DRAW_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "imgui.h"

namespace alienorum
{
    inline double scenery_crown_aspect(double horizontal_distance, double relative_height, double oblateness)
    {
        const double range = std::hypot(horizontal_distance, relative_height);
        return range > 0 ? std::hypot(oblateness * horizontal_distance, relative_height) / range
            : std::abs(oblateness);
    }

    inline void scenery_crown_contact(double center_x, double center_y, double trunk_width,
        double& radius_x, double& radius_y, double& tip_x, double& tip_y)
    {
        // Enlarge both axes together to preserve the sightline's crown aspect.
        const double scale = std::max(1.0, trunk_width / radius_x);
        radius_x *= scale;
        radius_y *= scale;
        // The screen ellipse approximates perspective; close any gap at its rim.
        const double dx = tip_x - center_x, dy = tip_y - center_y;
        const double radius = std::hypot(dx / radius_x, dy / radius_y);
        if (radius > 1.0)
        {
            tip_x = center_x + dx / radius;
            tip_y = center_y + dy / radius;
        }
    }

    class SceneryCrownOcclusion
    {
        static constexpr int cell_size = 4;
        int width = 0, height = 0, words = 0, rows = 0;
        std::vector<std::uint64_t> opaque;

        static std::uint64_t mask(int first, int last, int word)
        {
            const int left = std::max(0, first - word * 64);
            const int right = std::min(64, last - word * 64);
            return (UINT64_MAX << left) & (right == 64 ? UINT64_MAX : (std::uint64_t(1) << right) - 1);
        }

        public:
        void reset(int w, int h)
        {
            width = std::max(0, w);
            height = std::max(0, h);
            const int columns = (width + cell_size - 1) / cell_size;
            words = (columns + 63) / 64;
            rows = (height + cell_size - 1) / cell_size;
            opaque.assign(static_cast<size_t>(words) * rows, 0);
        }

        bool covered(double left, double top, double right, double bottom) const
        {
            if (opaque.empty() || !std::isfinite(left) || !std::isfinite(top)
                || !std::isfinite(right) || !std::isfinite(bottom)) return false;
            left = std::max(0.0, left);
            top = std::max(0.0, top);
            right = std::min(double(width), right);
            bottom = std::min(double(height), bottom);
            if (left >= right || top >= bottom) return true;
            const int first_x = static_cast<int>(floor(left / cell_size));
            const int first_y = static_cast<int>(floor(top / cell_size));
            const int last_x = static_cast<int>(ceil(right / cell_size));
            const int last_y = static_cast<int>(ceil(bottom / cell_size));
            for (int y = first_y; y < last_y; ++y)
            {
                for (int word = first_x / 64; word < (last_x + 63) / 64; ++word)
                {
                    const auto bits = mask(first_x, last_x, word);
                    if ((opaque[static_cast<size_t>(y) * words + word] & bits) != bits) return false;
                }
            }
            return true;
        }

        void cover(double x, double y, double radius_x, double radius_y)
        {
            // This rectangle lies inside even a four-segment crown. Leave an
            // additional pixel margin for the antialiased polygon boundary.
            const double half_x = radius_x * .5 - 2, half_y = radius_y * .5 - 2;
            if (!(half_x > 0) || !(half_y > 0) || !std::isfinite(x) || !std::isfinite(y)
                || !std::isfinite(half_x) || !std::isfinite(half_y)) return;
            const int first_x = static_cast<int>(ceil(std::clamp(x - half_x, 0.0, double(width)) / cell_size));
            const int first_y = static_cast<int>(ceil(std::clamp(y - half_y, 0.0, double(height)) / cell_size));
            const int last_x = static_cast<int>(floor(std::clamp(x + half_x, 0.0, double(width)) / cell_size));
            const int last_y = static_cast<int>(floor(std::clamp(y + half_y, 0.0, double(height)) / cell_size));
            if (first_x >= last_x || first_y >= last_y) return;
            for (int row = first_y; row < last_y; ++row)
            {
                for (int word = first_x / 64; word < (last_x + 63) / 64; ++word)
                {
                    opaque[static_cast<size_t>(row) * words + word] |= mask(first_x, last_x, word);
                }
            }
        }
    };

    // Reuse the native ellipse's unit contour, retaining its adaptive tessellation.
    inline void draw_scenery_crown(ImDrawList *draw, const ImVec2 &center, const ImVec2 &radius, ImU32 color)
    {
        if (!(color & IM_COL32_A_MASK)) return;
        const int segments = draw->_CalcCircleAutoSegmentCount(std::max(radius.x, radius.y));
        static std::unordered_map<int, std::vector<ImVec2>> contours;
        auto found = contours.find(segments);
        if (found == contours.end())
        {
            constexpr float pi = 3.14159265358979323846f;
            const float maximum = pi * 2.0f * (float(segments) - 1.0f) / float(segments);
            std::vector<ImVec2> contour;
            contour.reserve(segments);
            for (int i = 0; i < segments; ++i)
            {
                const float angle = (float(i) / float(segments - 1)) * maximum;
                contour.emplace_back(cosf(angle), sinf(angle));
            }
            found = contours.emplace(segments, std::move(contour)).first;
        }
        draw->_Path.reserve(draw->_Path.Size + segments);
        for (const auto &point : found->second)
        {
            draw->PathLineTo(ImVec2(point.x * radius.x + center.x, point.y * radius.y + center.y));
        }
        draw->PathFillConvex(color);
    }
}

#endif
