#ifndef ALIENORUM_CONSTELLATION_CACHE_H
#define ALIENORUM_CONSTELLATION_CACHE_H
#include "cons.h"
#include <unordered_map>
#ifndef PERF_P108
#define PERF_P108 1
#endif
namespace alienorum
{
    inline bool constellation_cache_reference = false;
    inline bool same_label_point(const Point& a, const Point& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    class ConstellationLabelRecord
    {
        std::vector<ConsBoundary> boundaries;
        std::vector<Point> directions;
        Point reference_sum, projected_sum, endpoint_sum, observer;
        Rotation plane;
        bool prepared = false, projected = false, endpoints_valid = false;
        struct Endpoint
        {
            Star* star = nullptr;
            bool active = false;
            Point position;
        };
        std::vector<Endpoint> endpoints;
        bool have_endpoints = false;
        public:
        uint64_t boundary_hits = 0, projection_hits = 0, endpoint_hits = 0;
        const std::vector<Point>& prepare(const std::vector<ConsBoundary>& source)
        {
            bool equal = prepared && source.size() == boundaries.size();
            const auto same = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
            if (equal) for (size_t i = 0; i < source.size(); ++i)
            {
                if (!same(source[i].RA, boundaries[i].RA) || !same(source[i].decl, boundaries[i].decl))
                {
                    equal = false;
                    break;
                }
            }
            if (equal) { ++boundary_hits; return directions; }
            boundaries = source;
            directions.clear();
            reference_sum = Point();
            for (const auto& boundary : source)
            {
                if (!std::isfinite(boundary.RA) || !std::isfinite(boundary.decl)) continue;
                directions.push_back(Point::from_ra_dec(boundary.RA, boundary.decl, light_year));
                reference_sum += directions.back();
            }
            prepared = true;
            projected = false;
            return directions;
        }
        Point boundary_direction()
        {
            const auto& current = here.equatorial_plane;
            if (projected && current.a == plane.a && same_label_point(current.v, plane.v))
            {
                ++projection_hits;
                return projected_sum;
            }
            projected_sum = Point();
            if (!current.v.squared_magnitude()) projected_sum = reference_sum;
            else for (const auto& point : directions) projected_sum += to_viewer_plane(point);
            plane = current;
            projected = true;
            return projected_sum;
        }
        Point endpoint_direction(const std::vector<ConsLine>& lines, const Point& current, bool& have)
        {
            bool changed = !endpoints_valid || !same_label_point(observer, current)
                || endpoints.size() != lines.size() * 2;
            endpoints.resize(lines.size() * 2);
            size_t index = 0;
            for (const auto& line : lines)
            {
                for (Star* star : {line.a, line.b})
                {
                    const bool active = star && !star->deleted;
                    const Point position = active ? Point(star->location) : Point();
                    auto& previous = endpoints[index++];
                    changed |= previous.star != star || previous.active != active
                        || !same_label_point(previous.position, position);
                    previous = {star, active, position};
                }
            }
            if (changed)
            {
                endpoint_sum = Point();
                have_endpoints = false;
                for (const auto& endpoint : endpoints)
                {
                    if (!endpoint.active) continue;
                    endpoint_sum += endpoint.position - current;
                    have_endpoints = true;
                }
                observer = current;
                endpoints_valid = true;
            }
            else ++endpoint_hits;
            have = have_endpoints;
            return endpoint_sum;
        }
    };
    class ConstellationLabelCache
    {
        std::unordered_map<size_t, ConstellationLabelRecord> records;
        public:
        ConstellationLabelRecord& get(size_t index)
        {
            auto found = records.find(index);
            if (found != records.end()) return found->second;
            if (records.size() >= 256) records.clear();
            return records[index];
        }
        size_t size() const { return records.size(); }
    };
    inline ConstellationLabelCache& constellation_label_cache()
    {
        static ConstellationLabelCache cache;
        return cache;
    }
}
#endif
