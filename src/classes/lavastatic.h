#ifndef ALIENORUM_LAVA_STATIC_H
#define ALIENORUM_LAVA_STATIC_H
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#ifndef PERF_P105
#define PERF_P105 0
#endif
namespace alienorum
{
    inline bool lava_static_reference = false;
    inline uint32_t lava_hash(uint32_t value)
    {
        value = ((value >> 16) ^ value) * 0x45d9f3b;
        value = ((value >> 16) ^ value) * 0x45d9f3b;
        return (value >> 16) ^ value;
    }
    struct LavaPoint
    {
        uint32_t seed = 0;
        double x = 0, z = 0, distance = 0;
    };
    struct LavaStep : LavaPoint
    {
        std::array<LavaPoint, 7> branch;
    };
    struct LavaRoot : LavaPoint
    {
        double angle = 0, heading = 0, segment = 0;
        int count = 0;
        std::array<LavaStep, 20> steps;
    };
    inline LavaRoot build_lava_root(int owner, int index, double range)
    {
        LavaRoot root;
        root.seed = lava_hash(uint32_t(owner * 1013904223u + index * 2246822519u + 0x8a35c));
        const double u_ang = (double)(root.seed & 0xFFFF) / 65535.0;
        const double u_dist = (double)((root.seed >> 16) & 0x7FFF) / 32767.0;
        root.angle = u_ang * 2.0 * _pi;
        const double radius = 2.2 + (range * 0.18) * pow(u_dist, 1.8);
        root.heading = root.angle + 1.25 + ((double)((root.seed >> 8) & 0xFF) / 255.0 - 0.5) * 1.6;
        root.segment = (1.8 + 3.2 * ((double)(root.seed & 0xFF) / 255.0)) * sqrt(radius / 6.0);
        root.count = 14 + int((root.seed >> 4) % 6);
        root.x = radius * sin(root.angle);
        root.z = radius * cos(root.angle);
        root.distance = sqrt(root.x * root.x + root.z * root.z);
        double x = root.x, z = root.z, heading = root.heading;
        uint32_t seed = root.seed;
        for (int i = 0; i < root.count; ++i)
        {
            auto& step = root.steps[i];
            seed = lava_hash(seed);
            step.seed = seed;
            const double turn = (double)(seed & 0x7FFF) / 32767.0;
            const double length = (double)((seed >> 15) & 0x7FFF) / 32767.0;
            heading += (turn - 0.5) * (96.0 * fiftyseventh);
            const double segment = root.segment * (0.65 + 0.70 * length);
            step.x = x + segment * sin(heading);
            step.z = z + segment * cos(heading);
            step.distance = sqrt(step.x * step.x + step.z * step.z);
            if (i == 3 || i == 7 || i == 11)
            {
                double bx = x, bz = z;
                double branch_heading = heading + ((i % 2 == 0) ? 1.0 : -1.0) * (42.0 * fiftyseventh);
                uint32_t branch_seed = seed ^ 0x9e3779b9;
                for (int j = 0; j < 5 + i % 3; ++j)
                {
                    auto& point = step.branch[j];
                    branch_seed = lava_hash(branch_seed);
                    point.seed = branch_seed;
                    const double turn = (double)(branch_seed & 0x7FFF) / 32767.0;
                    const double length = (double)((branch_seed >> 15) & 0x7FFF) / 32767.0;
                    branch_heading += (turn - 0.5) * (70.0 * fiftyseventh);
                    const double segment = root.segment * 0.65 * (0.6 + 0.6 * length);
                    point.x = bx + segment * sin(branch_heading);
                    point.z = bz + segment * cos(branch_heading);
                    point.distance = sqrt(point.x * point.x + point.z * point.z);
                    bx = point.x;
                    bz = point.z;
                }
            }
            x = step.x;
            z = step.z;
        }
        return root;
    }
    class LavaTopologyCache
    {
        int owner = -1;
        double range = std::numeric_limits<double>::quiet_NaN();
        std::array<LavaRoot, 28> roots;
        public:
        uint64_t hits = 0, builds = 0;
        const std::array<LavaRoot, 28>& get(int world, double radius)
        {
            if (world == owner && radius == range) { ++hits; return roots; }
            for (int i = 0; i < 28; ++i) roots[i] = build_lava_root(world, i, radius);
            owner = world;
            range = radius;
            ++builds;
            return roots;
        }
    };
    inline LavaTopologyCache& lava_topology_cache()
    {
        static LavaTopologyCache cache;
        return cache;
    }
    struct LavaParticle
    {
        double u0, u1, u2, u3, distance, speed, drift, x, z;
    };
    class LavaParticleCache
    {
        int owner = -1;
        std::array<LavaParticle, 320> particles;
        public:
        uint64_t hits = 0, builds = 0;
        const std::array<LavaParticle, 320>& get(int world)
        {
            if (world == owner) { ++hits; return particles; }
            const uint32_t seed = uint32_t(world * 1013904223u + 0x5a17e);
            for (int i = 0; i < 320; ++i)
            {
                const uint32_t hash = lava_hash(seed + uint32_t(i) * 2246822519u);
                auto& p = particles[i];
                p.u0 = (double)(hash & 0x3FFF) / 16383.0;
                p.u1 = (double)((hash >> 14) & 0x3FFF) / 16383.0;
                p.u2 = (double)((hash >> 20) & 0xFF) / 255.0;
                p.u3 = (double)((hash >> 8) & 0xFF) / 255.0;
                const double angle = p.u0 * 2.0 * _pi;
                p.distance = 1.2 + 38.0 * sqrt(p.u1);
                p.speed = 7.0 + 15.0 * p.u2;
                p.drift = 0.25 + 0.45 * p.u2;
                p.x = p.distance * sin(angle);
                p.z = p.distance * cos(angle);
            }
            owner = world;
            ++builds;
            return particles;
        }
    };
    inline LavaParticleCache& lava_particle_cache()
    {
        static LavaParticleCache cache;
        return cache;
    }
}
#endif
