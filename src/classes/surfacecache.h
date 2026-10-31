#ifndef ALIENORUM_SURFACE_CACHE_H
#define ALIENORUM_SURFACE_CACHE_H

#include "surfaceoptions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <list>
#include <unordered_map>

namespace alienorum
{
    // Lazy cube-face tiles sample a continuous, planet-fixed scalar field.
    // Shared edge nodes keep interpolation continuous at face boundaries and poles.
    class SurfaceFieldCache
    {
        struct Key
        {
            std::int64_t x, y;
            int face;
            bool operator==(const Key &other) const { return x == other.x && y == other.y && face == other.face; }
        };
        struct Hash
        {
            size_t operator()(const Key &key) const
            {
                std::uint64_t hash = std::uint64_t(key.x) * 0x9e3779b97f4a7c15ULL
                    ^ std::uint64_t(key.y) * 0xbf58476d1ce4e5b9ULL ^ std::uint64_t(key.face) * 0x94d049bb133111ebULL;
                hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
                hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
                return hash ^ (hash >> 31);
            }
        };
        struct Tile
        {
            std::array<double, 64> values = {};
            std::uint64_t valid = 0;
            std::list<Key>::iterator age;
        };
        std::unordered_map<Key, Tile, Hash> tiles;
        std::list<Key> ages;
        size_t capacity;
        std::int64_t cells = 0;
        std::uint64_t world = 0;
        double radius = 0, spacing = 0;
        size_t evaluations = 0;
        std::uint64_t generation = 0;
        size_t patch_hits = 0;
#if PERF_P034
        Key last_key = {};
        Tile* last_tile = nullptr;
        size_t tile_fast_hits = 0;
#endif

        template<class Sample>
        double node(int face, std::int64_t x, std::int64_t y, const Sample &sample)
        {
            const Key key = {x / 8, y / 8, face};
            Tile* selected = nullptr;
#if PERF_P034
            if (last_tile && last_key == key)
            {
                selected = last_tile;
                ++tile_fast_hits;
            }
#endif
            if (!selected)
            {
                auto found = tiles.find(key);
                if (found == tiles.end())
                {
#if PERF_P034
                    last_tile = nullptr;
#endif
                    if (tiles.size() >= capacity)
                    {
                        tiles.erase(ages.back());
                        ages.pop_back();
                    }
                    ages.push_front(key);
                    Tile tile;
                    tile.age = ages.begin();
                    found = tiles.emplace(key, tile).first;
                }
                selected = &found->second;
#if PERF_P034
                if (selected->age != ages.begin()) ages.splice(ages.begin(), ages, selected->age);
                last_key = key;
                last_tile = selected;
#else
                ages.splice(ages.begin(), ages, selected->age);
#endif
            }
            Tile &tile = *selected;
            const unsigned index = (y % 8) * 8 + x % 8;
            const std::uint64_t mask = std::uint64_t(1) << index;
            if (!(tile.valid & mask))
            {
                const int axis = face / 2;
                double position[3];
                position[axis] = face % 2 == 0 ? 1 : -1;
                position[(axis + 1) % 3] = -1 + 2.0 * x / cells;
                position[(axis + 2) % 3] = -1 + 2.0 * y / cells;
                const double length = std::hypot(std::hypot(position[0], position[1]), position[2]);
                tile.values[index] = sample(position[0] / length, position[1] / length, position[2] / length);
                tile.valid |= mask;
                ++evaluations;
            }
            return tile.values[index];
        }

        public:
        struct Patch
        {
            std::uint64_t generation = 0;
            int face = -1;
            std::int64_t x = 0, y = 0;
            double a = 0, b = 0, c = 0, d = 0;
        };
        explicit SurfaceFieldCache(size_t maximum_tiles = 8192) : capacity(std::max<size_t>(1, maximum_tiles)) {}
        SurfaceFieldCache(const SurfaceFieldCache &) = delete;
        SurfaceFieldCache &operator=(const SurfaceFieldCache &) = delete;

        void configure(std::uint64_t identity, double world_radius, double sample_spacing)
        {
            if (cells && world == identity && radius == world_radius && spacing == sample_spacing) return;
#if PERF_P034
            last_tile = nullptr;
            tile_fast_hits = 0;
#endif
            tiles.clear();
            ages.clear();
            world = identity;
            radius = world_radius;
            spacing = sample_spacing;
            cells = std::isfinite(radius) && std::isfinite(spacing) && radius > 0 && spacing > 0
                ? std::max<std::int64_t>(1, static_cast<std::int64_t>(ceil(2 * radius / spacing))) : 0;
            evaluations = 0;
            ++generation;
            patch_hits = 0;
        }

        template<class Sample>
        double value(double x, double y, double z, const Sample &sample, Patch* patch = nullptr)
        {
            if (!cells || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return sample(x, y, z);
            const double position[] = {x, y, z};
            int axis = 0;
            if (fabs(y) > fabs(position[axis])) axis = 1;
            if (fabs(z) > fabs(position[axis])) axis = 2;
            const double dominant = fabs(position[axis]);
            if (!(dominant > 0)) return sample(x, y, z);
            const int face = axis * 2 + (position[axis] < 0 ? 1 : 0);
            const double u = std::clamp((position[(axis + 1) % 3] / dominant + 1) * .5 * cells, 0.0, static_cast<double>(cells));
            const double v = std::clamp((position[(axis + 2) % 3] / dominant + 1) * .5 * cells, 0.0, static_cast<double>(cells));
            const auto ix = std::min(cells - 1, static_cast<std::int64_t>(floor(u)));
            const auto iy = std::min(cells - 1, static_cast<std::int64_t>(floor(v)));
            const double fx = u - ix, fy = v - iy;
            double a, b, c, d;
            if (patch && patch->generation == generation && patch->face == face && patch->x == ix && patch->y == iy)
            {
                a = patch->a;
                b = patch->b;
                c = patch->c;
                d = patch->d;
                ++patch_hits;
            }
            else
            {
                a = node(face, ix, iy, sample);
                b = node(face, ix + 1, iy, sample);
                c = node(face, ix, iy + 1, sample);
                d = node(face, ix + 1, iy + 1, sample);
                if (patch) *patch = {generation, face, ix, iy, a, b, c, d};
            }
            return (a + fx * (b - a)) * (1 - fy) + (c + fx * (d - c)) * fy;
        }

        size_t sample_count() const { return evaluations; }
        size_t tile_count() const { return tiles.size(); }
        size_t patch_hit_count() const { return patch_hits; }
        size_t tile_fast_hit_count() const
        {
#if PERF_P034
            return tile_fast_hits;
#else
            return 0;
#endif
        }
    };
}

#endif
