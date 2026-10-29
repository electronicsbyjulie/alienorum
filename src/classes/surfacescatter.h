#ifndef ALIENORUM_SURFACE_SCATTER_H
#define ALIENORUM_SURFACE_SCATTER_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace alienorum
{
    struct SurfaceScenery
    {
        double east;
        double north;
        double latitude;
        double longitude;
        std::uint64_t id;
        std::uint32_t seed;
        std::array<double, 3> position = {};
    };

    inline std::uint64_t scenery_hash(std::uint64_t value)
    {
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    inline double scenery_size(std::uint32_t identity, double minimum, double maximum)
    {
        const double fraction = ((identity ^ 0x9e3779b9u) & 0xffff) / 65535.0;
        return minimum + (maximum - minimum) * fraction * fraction * fraction;
    }

    // Lower identities win conflicts, even when the winning neighbor is itself
    // suppressed. This local rule keeps spacing stable during travel and panning.
    // Include candidates beyond the visible range by twice the largest radius.
    inline std::vector<bool> scenery_clearance(const std::vector<SurfaceScenery> &objects,
        const std::vector<double> &radii, double radius, double gap = 0.1)
    {
        std::vector<bool> retained(objects.size(), true);
        if (radii.size() != objects.size() || !(radius > 0)) return retained;
        double largest = 0;
        for (double crown : radii) largest = std::max(largest, crown);
        if (!(largest > 0)) return retained;
        const double cell_size = 2 * largest + std::max(0.0, gap);
        using Cell = std::pair<long long, long long>;
        const auto cell_at = [&](const SurfaceScenery &object)
        {
            return Cell(static_cast<long long>(floor(object.east / cell_size)),
                static_cast<long long>(floor(object.north / cell_size)));
        };
        std::map<Cell, std::vector<size_t>> cells;
        for (size_t i = 0; i < objects.size(); i++)
        {
            if (radii[i] > 0) cells[cell_at(objects[i])].push_back(i);
        }
        for (size_t i = 0; i < objects.size(); i++)
        {
            if (!(radii[i] > 0)) continue;
            const Cell cell = cell_at(objects[i]);
            for (int x = -1; x <= 1 && retained[i]; x++)
            {
                for (int y = -1; y <= 1 && retained[i]; y++)
                {
                    const auto found = cells.find(Cell(cell.first + x, cell.second + y));
                    if (found == cells.end()) continue;
                    for (size_t j : found->second)
                    {
                        if (objects[j].id >= objects[i].id) continue;
                        const double clearance = radii[i] + radii[j] + std::max(0.0, gap);
                        const double east = objects[i].east - objects[j].east;
                        const double north = objects[i].north - objects[j].north;
                        if (east * east + north * north >= clearance * clearance) continue;
                        const double latitude = sin((objects[i].latitude - objects[j].latitude) * 0.5);
                        const double longitude = sin((objects[i].longitude - objects[j].longitude) * 0.5);
                        const double chord_squared = 4 * radius * radius * (latitude * latitude
                            + cos(objects[i].latitude) * cos(objects[j].latitude) * longitude * longitude);
                        if (chord_squared < clearance * clearance)
                        {
                            retained[i] = false;
                            break;
                        }
                    }
                }
            }
        }
        return retained;
    }

    // Six fixed cube faces tile the sphere without longitude seams or polar
    // singularities. Generate only cells within the observer's local range.
    inline std::vector<SurfaceScenery> surface_scenery(double radius, double latitude,
        double longitude, double range, double spacing, std::uint64_t world)
    {
        std::vector<SurfaceScenery> result;
        if (!(radius > 0.0) || !(spacing > 0.0) || !(range > 0.0)
            || !std::isfinite(radius) || !std::isfinite(latitude) || !std::isfinite(longitude))
        {
            return result;
        }
        const double observer[3] =
        {
            cos(latitude) * cos(longitude), cos(latitude) * sin(longitude), sin(latitude)
        };
        const double east[3] =
        {
            -sin(longitude), cos(longitude), 0.0
        };
        const double north[3] =
        {
            -sin(latitude) * cos(longitude), -sin(latitude) * sin(longitude), cos(latitude)
        };
        const std::int64_t cells = static_cast<std::int64_t>(ceil(2.0 * radius / spacing));
        const double step = 2.0 / cells;
        const double angular_range = std::min(0.25, range / radius);
        for (int face = 0; face < 6; ++face)
        {
            const int axis = face / 2;
            const int u_axis = (axis + 1) % 3;
            const int v_axis = (axis + 2) % 3;
            const double sign = (face % 2 == 0) ? 1.0 : -1.0;
            const double facing = sign * observer[axis];
            if (facing < 0.5 - angular_range * 2.0)
            {
                continue;
            }
            const double u = observer[u_axis] / facing;
            const double v = observer[v_axis] / facing;
            const double margin = angular_range * (1.0 + std::max(fabs(u), fabs(v)))
                / std::max(0.1, facing - angular_range) + step;
            const auto index = [&](double coordinate) -> std::int64_t
            {
                return static_cast<std::int64_t>(floor((coordinate + 1.0) / step));
            };
            const std::int64_t first_u = std::max<std::int64_t>(0, index(u - margin));
            const std::int64_t last_u = std::min(cells - 1, index(u + margin));
            const std::int64_t first_v = std::max<std::int64_t>(0, index(v - margin));
            const std::int64_t last_v = std::min(cells - 1, index(v + margin));
            for (std::int64_t row = first_v; row <= last_v; ++row)
            {
                for (std::int64_t col = first_u; col <= last_u; ++col)
                {
                    const std::uint64_t id = scenery_hash(world ^ scenery_hash(face + 1)
                        ^ scenery_hash(static_cast<std::uint64_t>(row) + 0x12345678ULL)
                        ^ scenery_hash(static_cast<std::uint64_t>(col) + 0x87654321ULL));
                    const double jitter_u = (scenery_hash(id) & 0xffffff) / 16777216.0;
                    const double jitter_v = (scenery_hash(id + 1) & 0xffffff) / 16777216.0;
                    double position[3];
                    position[axis] = sign;
                    position[u_axis] = -1.0 + (col + 0.1 + 0.8 * jitter_u) * step;
                    position[v_axis] = -1.0 + (row + 0.1 + 0.8 * jitter_v) * step;
                    const double length = sqrt(1.0 + position[u_axis] * position[u_axis]
                        + position[v_axis] * position[v_axis]);
                    // Correct the cube projection's changing area to keep density uniform.
                    const double acceptance = (scenery_hash(id + 2) & 0xffffff) / 16777216.0;
                    if (acceptance > 1.0 / (length * length * length))
                    {
                        continue;
                    }
                    double offset_east = 0.0;
                    double offset_north = 0.0;
                    double chord_squared = 0.0;
                    for (int component = 0; component < 3; ++component)
                    {
                        const double offset = radius * (position[component] / length - observer[component]);
                        offset_east += offset * east[component];
                        offset_north += offset * north[component];
                        chord_squared += offset * offset;
                    }
                    if (chord_squared > range * range)
                    {
                        continue;
                    }
                    result.push_back(
                    {
                        offset_east, offset_north, asin(position[2] / length), atan2(position[1], position[0]), id,
                        static_cast<std::uint32_t>(scenery_hash(id + 3)),
                        {position[0] / length, position[1] / length, position[2] / length}
                    });
                }
            }
        }
        return result;
    }
    // Keep a padded geographic patch across small steps; only its local projection changes.
    class SurfaceSceneryCache
    {
        struct Chunk
        {
            size_t first = 0, last = 0;
            std::array<double, 3> low, high;
        };
        std::vector<SurfaceScenery> scenery;
        std::vector<Chunk> chunks;
        std::vector<size_t> candidates;
        std::vector<size_t> projected_revision;
        size_t pose_revision = 0;
        bool fully_projected = false;
        std::array<double, 3> current_observer, current_east, current_north;
        std::array<double, 3> anchor = {};
        double saved_radius = 0, saved_range = 0, saved_spacing = 0;
        double saved_latitude = NAN, saved_longitude = NAN;
        std::uint64_t saved_world = 0;
        size_t revision = 0;

        public:
        const std::vector<SurfaceScenery> &update(double radius, double latitude, double longitude,
            double range, double spacing, std::uint64_t world, bool project_all = true)
        {
            if (!(radius > 0) || !(range > 0) || !(spacing > 0) || !std::isfinite(radius)
                || !std::isfinite(range) || !std::isfinite(spacing)
                || !std::isfinite(latitude) || !std::isfinite(longitude))
            {
                scenery.clear();
                chunks.clear();
                projected_revision.clear();
                saved_radius = 0;
                ++revision;
                return scenery;
            }
            const double observer[] = {cos(latitude) * cos(longitude), cos(latitude) * sin(longitude), sin(latitude)};
            const double padding = std::clamp(range * .125, 16.0, 128.0);
            double offset_squared = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double offset = radius * (observer[axis] - anchor[axis]);
                offset_squared += offset * offset;
            }
            if (!revision || radius != saved_radius || range != saved_range || spacing != saved_spacing
                || world != saved_world || offset_squared > padding * padding * .5625)
            {
                scenery = surface_scenery(radius, latitude, longitude, range + padding, spacing, world);
                projected_revision.assign(scenery.size(), 0);
                chunks.clear();
                for (size_t first = 0; first < scenery.size(); first += 64)
                {
                    Chunk chunk;
                    chunk.first = first;
                    chunk.last = std::min(first + 64, scenery.size());
                    chunk.low = chunk.high = scenery[first].position;
                    for (size_t i = first + 1; i < chunk.last; ++i)
                    {
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            chunk.low[axis] = std::min(chunk.low[axis], scenery[i].position[axis]);
                            chunk.high[axis] = std::max(chunk.high[axis], scenery[i].position[axis]);
                        }
                    }
                    chunks.push_back(chunk);
                }
                std::copy(observer, observer + 3, anchor.begin());
                saved_radius = radius;
                saved_range = range;
                saved_spacing = spacing;
                saved_world = world;
                saved_latitude = saved_longitude = NAN;
                ++revision;
            }
            if (latitude != saved_latitude || longitude != saved_longitude)
            {
                ++pose_revision;
                fully_projected = false;
                const double east[] = {-sin(longitude), cos(longitude), 0};
                const double north[] = {-sin(latitude) * cos(longitude), -sin(latitude) * sin(longitude), cos(latitude)};
                std::copy(observer, observer + 3, current_observer.begin());
                std::copy(east, east + 3, current_east.begin());
                std::copy(north, north + 3, current_north.begin());
                saved_latitude = latitude;
                saved_longitude = longitude;
            }
            if (project_all && !fully_projected)
            {
                for (size_t i = 0; i < scenery.size(); ++i) project(i);
                fully_projected = true;
            }
            return scenery;
        }

        size_t generation() const { return revision; }

        void project(size_t i)
        {
            if (projected_revision[i] == pose_revision) return;
            projected_revision[i] = pose_revision;
            scenery[i].east = scenery[i].north = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double offset = saved_radius * (scenery[i].position[axis] - current_observer[axis]);
                scenery[i].east += offset * current_east[axis];
                scenery[i].north += offset * current_north[axis];
            }
        }

        // Reject only a certified rear volume. Wide/pitched views and uncertain bounds retain it.
        const std::vector<size_t>& visible(double azimuth, double pitch, double low_height,
            double high_height, bool reference = false, bool project_candidates = true)
        {
            candidates.clear();
            const double sa = sin(azimuth), ca = cos(azimuth), sp = sin(pitch), cp = cos(pitch);
            std::array<double, 3> forward;
            for (int axis = 0; axis < 3; ++axis)
                forward[axis] = current_east[axis] * sa + current_north[axis] * ca;
            for (const auto& chunk : chunks)
            {
                double low = 0, high = 0;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double a = saved_radius * (chunk.low[axis] - current_observer[axis]) * forward[axis];
                    const double b = saved_radius * (chunk.high[axis] - current_observer[axis]) * forward[axis];
                    low += std::min(a, b);
                    high += std::max(a, b);
                }
                const double z = std::max(low * cp, high * cp)
                    + std::max(low_height * sp, high_height * sp);
                if (!reference && std::isfinite(high) && std::isfinite(z) && high < -1e-5 && z < -1e-5) continue;
                for (size_t i = chunk.first; i < chunk.last; ++i)
                {
                    if (project_candidates) project(i);
                    candidates.push_back(i);
                }
            }
            return candidates;
        }
    };
}

#endif
