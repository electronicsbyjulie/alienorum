#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "imgui.h"

namespace alienorum
{
    inline bool snow_cpu_reference = false;
    struct PrecipitationStats
    {
        double cpu_milliseconds = 0;
        size_t particles = 0, flakes = 0;
    };
    inline PrecipitationStats precipitation_stats;

    struct PrecipitationParticle
    {
        double u0, u1, u2, u3, phase, x, z, sin0, cos0, sin1, cos1;
    };

    inline PrecipitationParticle precipitation_particle(uint32_t seed, uint32_t index, bool precompute_drift = true)
    {
        const auto hash = [&](uint32_t salt)
        {
            uint32_t value = seed ^ (index * 0x9e3779b9u) ^ salt;
            value = ((value >> 16) ^ value) * 0x45d9f3bu;
            value = ((value >> 16) ^ value) * 0x45d9f3bu;
            return (value >> 16) ^ value;
        };
        const uint32_t h0 = hash(0x1a874b21u), h1 = hash(0x85ebca6bu);
        const uint32_t h2 = hash(0xc2b2ae35u), h3 = hash(0x27d4eb2fu);
        const double u0 = h0 / 4294967296.0, u1 = h1 / 4294967296.0;
        const double angle0 = u0 * 6.28318530717958647692, angle1 = u1 * 6.28318530717958647692;
        const double sin0 = sin(angle0), cos0 = cos(angle0), radius = 34 * sqrt(u1);
        return {u0, u1, h2 / 4294967296.0, h3 / 4294967296.0,
            double((h1 ^ (h2 >> 1)) & 0xFFFF) / 65536.0,
            radius * sin0, radius * cos0, sin0, cos0,
            precompute_drift ? sin(angle1) : 0, precompute_drift ? cos(angle1) : 0};
    }

    class PrecipitationCache
    {
        uint32_t seed = 0;
        std::vector<PrecipitationParticle> particles;

        public:
        const std::vector<PrecipitationParticle>& get(uint32_t world_seed, size_t count)
        {
            if (world_seed != seed)
            {
                seed = world_seed;
                particles.clear();
            }
            count = std::min(count, size_t(2400));
            if (particles.capacity() < count) particles.reserve(2400);
            while (particles.size() < count) particles.push_back(precipitation_particle(seed, particles.size()));
            return particles;
        }
    };

    class SnowflakeMesh
    {
        std::array<ImVec2, 6> unit, fringe;

        public:
        SnowflakeMesh()
        {
            // Capture the native six-sided contour and its antialiasing miter once.
            ImDrawList sample(ImGui::GetDrawListSharedData());
            sample._ResetForNewFrame();
            sample.Flags &= ~ImDrawListFlags_AntiAliasedFill;
            sample.AddCircleFilled(ImVec2(0, 0), 1, IM_COL32_WHITE, 6);
            for (int point = 0; point < 6; ++point) unit[point] = sample.VtxBuffer[point].pos;
            sample._ResetForNewFrame();
            sample.Flags |= ImDrawListFlags_AntiAliasedFill;
            sample._FringeScale = 1;
            sample.AddCircleFilled(ImVec2(0, 0), 1, IM_COL32_WHITE, 6);
            for (int point = 0; point < 6; ++point)
            {
                const auto inner = sample.VtxBuffer[point * 2].pos;
                const auto outer = sample.VtxBuffer[point * 2 + 1].pos;
                fringe[point] = ImVec2((outer.x - inner.x) * .5f, (outer.y - inner.y) * .5f);
            }
        }

        void draw(ImDrawList* list, ImVec2 center, float radius, ImU32 color) const
        {
            if (!(color & IM_COL32_A_MASK) || radius < .5f) return;
            const bool aa = list->Flags & ImDrawListFlags_AntiAliasedFill;
            const unsigned stride = aa ? 2 : 1;
            list->PrimReserve(aa ? 48 : 12, aa ? 12 : 6);
            const unsigned base = list->_VtxCurrentIdx;
            for (unsigned point = 2; point < 6; ++point)
            {
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base));
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base + (point - 1) * stride));
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base + point * stride));
            }
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            for (unsigned point = 0; point < 6; ++point)
            {
                const ImVec2 position(center.x + unit[point].x * radius, center.y + unit[point].y * radius);
                if (aa)
                {
                    const ImVec2 offset(fringe[point].x * list->_FringeScale, fringe[point].y * list->_FringeScale);
                    list->PrimWriteVtx(ImVec2(position.x - offset.x, position.y - offset.y), uv, color);
                    list->PrimWriteVtx(ImVec2(position.x + offset.x, position.y + offset.y), uv, color & ~IM_COL32_A_MASK);
                    const unsigned current = base + point * 2, previous = base + ((point + 5) % 6) * 2;
                    for (unsigned index : {current, previous, previous + 1, previous + 1, current + 1, current})
                        list->PrimWriteIdx(static_cast<ImDrawIdx>(index));
                }
                else list->PrimWriteVtx(position, uv, color);
            }
        }
    };
}
