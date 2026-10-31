#ifndef ALIENORUM_ROCK_MESH_CACHE_H
#define ALIENORUM_ROCK_MESH_CACHE_H
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <unordered_map>
#ifndef PERF_P101
#define PERF_P101 0
#endif
namespace alienorum
{
    inline bool rock_mesh_reference = false;
    struct RockShape
    {
        double cos_y, sin_y, scale_x, scale_z, scale_y, tilt_x, tilt_z;
        std::array<std::array<double, 3>, 26> vertices;
    };
    inline RockShape build_rock_shape(uint32_t seed)
    {
        uint32_t sh = seed;
        const auto next_rnd = [&]()
        {
            sh = ((sh >> 16) ^ sh) * 0x45d9f3b;
            sh = ((sh >> 16) ^ sh) * 0x45d9f3b;
            sh = (sh >> 16) ^ sh;
            return (double)(sh & 0xFFFF) / 65535.0;
        };
        static const float rock_dome_verts[26][3] =
        {
            { 0.000000f,  1.000000f,  0.000000f}, // 0: Apex
            { 0.894427f,  0.447214f,  0.000000f}, // 1
            { 0.276393f,  0.447214f,  0.850651f}, // 2
            {-0.723607f,  0.447214f,  0.525731f}, // 3
            {-0.723607f,  0.447214f, -0.525731f}, // 4
            { 0.276393f,  0.447214f, -0.850651f}, // 5
            { 0.525731f,  0.850651f,  0.000000f}, // 6
            { 0.688191f,  0.525731f,  0.500000f}, // 7
            { 0.162460f,  0.850651f,  0.500000f}, // 8
            {-0.262866f,  0.525731f,  0.809017f}, // 9
            {-0.425325f,  0.850651f,  0.309017f}, // 10
            {-0.850651f,  0.525731f,  0.000000f}, // 11
            {-0.425325f,  0.850651f, -0.309017f}, // 12
            {-0.262866f,  0.525731f, -0.809017f}, // 13
            { 0.162460f,  0.850651f, -0.500000f}, // 14
            { 0.688191f,  0.525731f, -0.500000f}, // 15
            { 0.587785f,  0.000000f,  0.809017f}, // 16: Equator base
            { 0.951057f,  0.000000f,  0.309017f}, // 17
            { 0.951057f,  0.000000f, -0.309017f}, // 18
            {-0.587785f,  0.000000f,  0.809017f}, // 19
            { 0.000000f,  0.000000f,  1.000000f}, // 20
            {-0.951057f,  0.000000f, -0.309017f}, // 21
            {-0.951057f,  0.000000f,  0.309017f}, // 22
            {-0.000000f,  0.000000f, -1.000000f}, // 23
            {-0.587785f,  0.000000f, -0.809017f}, // 24
            { 0.587785f,  0.000000f, -0.809017f}  // 25
        };

        RockShape result;
        const double yaw = next_rnd() * 2.0 * _pi;
        result.cos_y = cos(yaw);
        result.sin_y = sin(yaw);
        result.scale_x = 0.88 + 0.24 * next_rnd();
        result.scale_z = 0.78 + 0.30 * next_rnd();
        result.scale_y = 0.92 + 0.16 * next_rnd();
        const double apex_x = (next_rnd() - 0.5) * 0.35;
        const double apex_z = (next_rnd() - 0.5) * 0.35;
        result.tilt_x = (next_rnd() - 0.5) * 0.12;
        result.tilt_z = (next_rnd() - 0.5) * 0.12;
        const double jagg = ROCK_JAGGEDNESS * (0.85 + 0.30 * next_rnd());
        for (int vi = 0; vi < 26; ++vi)
        {
            uint32_t vsh = seed ^ ((uint32_t)vi * 0x9e3779b9u);
            vsh = ((vsh >> 16) ^ vsh) * 0x45d9f3b;
            vsh = ((vsh >> 16) ^ vsh) * 0x45d9f3b;
            vsh = (vsh >> 16) ^ vsh;
            const double vrnd = (double)(vsh & 0xFFFF) / 65535.0;
            const double disp = 1.0 + jagg * (vrnd - 0.5);
            auto& vertex = result.vertices[vi];
            vertex = {rock_dome_verts[vi][0] * disp, rock_dome_verts[vi][1] * disp, rock_dome_verts[vi][2] * disp};
            if (vi == 0) { vertex[0] += apex_x; vertex[2] += apex_z; }
            else if (rock_dome_verts[vi][1] < 0.05f) vertex[1] = 0.0;
        }
        return result;
    }
    class RockMeshCache
    {
        std::unordered_map<uint32_t, RockShape> shapes;
        std::deque<uint32_t> order;
        public:
        uint64_t hits = 0, misses = 0;
        const RockShape& get(uint32_t seed)
        {
            const auto found = shapes.find(seed);
            if (found != shapes.end()) { ++hits; return found->second; }
            ++misses;
            if (shapes.size() >= 1024) { shapes.erase(order.front()); order.pop_front(); }
            order.push_back(seed);
            return shapes.emplace(seed, build_rock_shape(seed)).first->second;
        }
        size_t size() const { return shapes.size(); }
    };
    inline RockMeshCache& rock_mesh_cache()
    {
        static RockMeshCache cache;
        return cache;
    }
}
#endif
