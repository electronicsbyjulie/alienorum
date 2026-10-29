#include "surfacenavigation.h"
#include <queue>
#include <limits>

using namespace alienorum;

SurfaceSampler::SurfaceSampler(Planet *body, Map *surface, SurfaceBiomes &noise, double world_radius, double displacement)
    : planet(body), map(surface), field(noise), radius(world_radius), strength(displacement),
      bound(SurfaceBiomes::angular_bound(world_radius, displacement))
{
    favorable = planet && planet->get_surface_pressure() >= 150 && planet->estimate_surface_temperature() < 400;
    biomes = planet && map && uses_rocky_map(planet->type) && favorable
        && (planet->has_water || planet->vegetation_r || planet->vegetation_g || planet->vegetation_b);
    ocean = planet && (planet->type == waterworld || planet->type == hycean);
    water = ocean || (biomes && planet->has_water);
}

std::pair<RGB3, SurfaceMaterial> SurfaceSampler::info(double latitude, double longitude)
{
    if (!map) return {RGB3(0, 8, 24), SurfaceMaterial::unknown};
    if (biomes)
    {
        RGB3 uniform;
        if (map->color_within_cap(latitude, longitude, bound, uniform, true))
            return {uniform, map->material_at(latitude, longitude)};
        const auto coord = field.coordinates(radius, latitude, longitude, strength);
        latitude = coord.first;
        longitude = coord.second;
    }
    return {map->color_at(latitude, longitude), map->material_at(latitude, longitude)};
}

bool SurfaceSampler::vegetation_color(const RGB3 &sample) const
{
    if (!planet || !map || !uses_rocky_map(planet->type) || !favorable || !planet->has_water
        || !(planet->vegetation_r || planet->vegetation_g || planet->vegetation_b)) return false;
    const double delta = fabs(Color::hue_from_rgb(sample.r, sample.g, sample.b)
        - Color::hue_from_rgb(planet->vegetation_r, planet->vegetation_g, planet->vegetation_b));
    return std::min(delta, 2 * _pi - delta) < .35
        && fabs(Color::sat_from_rgb(sample.r, sample.g, sample.b)
            - Color::sat_from_rgb(planet->vegetation_r, planet->vegetation_g, planet->vegetation_b)) < .35;
}

bool SurfaceSampler::vegetation_at(double latitude, double longitude)
{
    const auto sample = info(latitude, longitude);
    return sample.second == SurfaceMaterial::unknown ? vegetation_color(sample.first)
        : sample.second == SurfaceMaterial::vegetation;
}

SurfaceSampler::Sample SurfaceSampler::sample(double latitude, double longitude)
{
    const auto value = info(latitude, longitude);
    const bool vegetation = value.second == SurfaceMaterial::unknown
        ? vegetation_color(value.first) : value.second == SurfaceMaterial::vegetation;
    return {value.first, value.second, water_sample(value), vegetation};
}

bool SurfaceSampler::water_sample(const std::pair<RGB3, SurfaceMaterial> &sample) const
{
    if (!water) return false;
    if (sample.second != SurfaceMaterial::unknown) return sample.second == SurfaceMaterial::water;
    if (ocean) return true;
    return sample.first.b > .8 * sample.first.r && std::max(sample.first.b, sample.first.g) > 1.333 * sample.first.r
        && !vegetation_color(sample.first);
}

bool SurfaceSampler::water_at(double latitude, double longitude)
{
    return water && water_sample(info(latitude, longitude));
}

bool SurfaceSampler::water_at(double latitude, double longitude, WaterPatch& patch)
{
    if (!water || !map || !planet || !(radius > 0) || !std::isfinite(radius)
        || !std::isfinite(latitude) || !std::isfinite(longitude) || fabs(latitude) > half_pi)
        return water_at(latitude, longitude);
    const WaterPatch::Key key = {map, map->gen, planet, radius, strength, biomes, ocean, favorable,
        planet->has_water != 0, planet->type, planet->vegetation_r, planet->vegetation_g, planet->vegetation_b};
    const double padding = std::min(32.0, radius * .001);
    const double latitude_delta = sin((latitude - patch.latitude) * .5);
    const double longitude_delta = sin((longitude - patch.longitude) * .5);
    const double arc = padding / radius * .5;
    const double limit = sin(arc) * sin(arc) * (1 - 1e-8);
    const double separation = latitude_delta * latitude_delta
        + cos(latitude) * cos(patch.latitude) * longitude_delta * longitude_delta;
    if (patch.attempted && patch.key == key && fabs(longitude - patch.longitude) <= _pi && separation <= limit)
    {
        if (patch.uniform && !generating_fic_texture) return patch.value;
        return water_at(latitude, longitude);
    }
    patch.key = key;
    patch.latitude = latitude;
    patch.longitude = longitude;
    patch.attempted = true;
    RGB3 color;
    patch.uniform = !generating_fic_texture && map->color_within_cap(latitude, longitude,
        (biomes ? bound : 0) + padding / radius, color, true);
    if (patch.uniform)
    {
        patch.value = water_sample({color, map->material_at(latitude, longitude)});
        return patch.value;
    }
    return water_at(latitude, longitude);
}

bool SurfaceSampler::base_water(unsigned long column, unsigned long row) const
{
    return map && water_sample(map->surface_pixel(column, row));
}

namespace
{
    using Coord = std::pair<double, double>;

    double angle(Coord a, Coord b)
    {
        const double lat = sin((b.first - a.first) * .5), lon = sin((b.second - a.second) * .5);
        const double value = std::clamp(lat * lat + cos(a.first) * cos(b.first) * lon * lon, 0.0, 1.0);
        return 2 * atan2(sqrt(value), sqrt(1 - value));
    }

    Coord offset(Coord origin, double east, double north, double radius)
    {
        const double distance = hypot(east, north);
        if (!distance) return origin;
        const double arc = distance / radius;
        const double clat = cos(origin.first), slat = sin(origin.first);
        const double clon = cos(origin.second), slon = sin(origin.second);
        const double e = east / distance * sin(arc), n = north / distance * sin(arc);
        const double x = clat * clon * cos(arc) - e * slon - n * slat * clon;
        const double y = clat * slon * cos(arc) + e * clon - n * slat * slon;
        const double z = slat * cos(arc) + n * clat;
        return {atan2(z, hypot(x, y)), atan2(y, x)};
    }

    Coord midpoint(Coord a, Coord b)
    {
        const double x = cos(a.first) * cos(a.second) + cos(b.first) * cos(b.second);
        const double y = cos(a.first) * sin(a.second) + cos(b.first) * sin(b.second);
        return {atan2(sin(a.first) + sin(b.first), hypot(x, y)), atan2(y, x)};
    }

    struct Segment
    {
        bool horizontal;
        double fixed, first, last;
        Coord point(double fraction) const
        {
            const double moving = first + fraction * (last - first);
            return horizontal ? Coord(fixed, moving) : Coord(moving, fixed);
        }
        double length(double radius) const { return radius * (last - first) * (horizontal ? fabs(cos(fixed)) : 1); }
        Coord closest(Coord origin) const
        {
            Coord best = point(0);
            const auto consider = [&](Coord candidate)
            {
                if (angle(origin, candidate) < angle(origin, best)) best = candidate;
            };
            consider(point(1));
            if (horizontal)
            {
                for (double shift : {-2 * _pi, 0.0, 2 * _pi})
                {
                    consider({fixed, std::clamp(origin.second + shift, first, last)});
                }
            }
            else
            {
                const double lat = atan2(sin(origin.first), cos(origin.first) * cos(fixed - origin.second));
                consider({std::clamp(lat, first, last), fixed});
            }
            return best;
        }
    };

    struct Node
    {
        Segment segment;
        double lower;
        bool operator<(const Node &other) const { return lower > other.lower; }
    };

    struct FarthestFirst
    {
        bool operator()(const Node &a, const Node &b) const { return a.lower < b.lower; }
    };
}

ShoreLocation alienorum::find_nearest_shore(SurfaceSampler &surface, double latitude, double longitude, double facing)
{
    ShoreLocation best;
    best.score = std::numeric_limits<double>::infinity();
    Map *map = surface.surface_map();
    const double radius = surface.world_radius();
    if (!map || !map->is_complete() || !surface.has_surface_water() || !(radius > 0) || !std::isfinite(radius)
        || !std::isfinite(latitude) || !std::isfinite(longitude) || !std::isfinite(facing)) return best;
    const Coord origin(latitude, std::remainder(longitude, 2 * _pi));
    const auto bearing_from_origin = [&](Coord point)
    {
        const double delta = point.second - origin.second;
        return atan2(sin(delta) * cos(point.first),
            cos(origin.first) * sin(point.first) - sin(origin.first) * cos(point.first) * cos(delta));
    };
    const auto score = [&](Coord point)
    {
        return radius * angle(origin, point) * (1.5 - .5 * cos(bearing_from_origin(point) - facing));
    };
    const auto crossing = [&](Coord a, bool wet_a, Coord b)
    {
        for (int iteration = 0; iteration < 40 && radius * angle(a, b) > .05; ++iteration)
        {
            const Coord middle = midpoint(a, b);
            if (surface.water_at(middle.first, middle.second) == wet_a) a = middle;
            else b = middle;
        }
        const Coord land = wet_a ? b : a, water = wet_a ? a : b;
        const double cost = score(land);
        if (cost < best.score)
        {
            best = {true, land.first, std::remainder(land.second, 2 * _pi),
                water.first, std::remainder(water.second, 2 * _pi), radius * angle(origin, land), cost};
        }
    };
    const bool origin_wet = surface.water_at(origin.first, origin.second);
    for (int i = 0; i < 32; ++i)
    {
        const double bearing = 2 * _pi * i / 32;
        const Coord point = offset(origin, sin(bearing), cos(bearing), radius);
        if (surface.water_at(point.first, point.second) != origin_wet) crossing(origin, origin_wet, point);
    }
    if (best.found && best.distance <= .05) return best;

    const unsigned long width = map->get_width(), height = map->get_height();
    if (!width || !height) return best;
    std::vector<bool> wet(width * height);
    bool any_water = false, any_land = false;
    for (unsigned long row = 0; row < height; ++row)
    {
        for (unsigned long column = 0; column < width; ++column)
        {
            const bool value = surface.base_water(column, row);
            wet[row * width + column] = value;
            any_water |= value;
            any_land |= !value;
        }
    }
    if (!any_water || !any_land) return best;
    const auto each_coast = [&](const auto &visit)
    {
        for (unsigned long row = 0; row + 1 < height; ++row)
        {
            for (unsigned long column = 0; column < width;)
            {
                if (wet[row * width + column] == wet[(row + 1) * width + column]) { ++column; continue; }
                const unsigned long first = column++;
                while (column < width && wet[row * width + column] != wet[(row + 1) * width + column]) ++column;
                const auto a = map->surface_pixel_bounds(first, row), b = map->surface_pixel_bounds(column - 1, row);
                visit(Segment{true, a[0], a[2], b[3]});
            }
        }
        for (unsigned long column = 0; column < width; ++column)
        {
            const unsigned long previous = (column + width - 1) % width;
            for (unsigned long row = 0; row < height;)
            {
                if (wet[row * width + column] == wet[row * width + previous]) { ++row; continue; }
                const unsigned long first = row++;
                while (row < height && wet[row * width + column] != wet[row * width + previous]) ++row;
                const auto a = map->surface_pixel_bounds(column, first), b = map->surface_pixel_bounds(column, row - 1);
                if (a[1] > b[0]) visit(Segment{false, a[2], b[0], a[1]});
            }
        }
    };
    const double displacement = std::min(_pi * radius, surface.displacement_bound());
    const auto bound = [&](const Segment &segment)
    {
        const double distance = std::max(0.0, radius * angle(origin, segment.closest(origin)) - displacement);
        const Coord center = segment.point(.5);
        const double separation = angle(origin, center);
        const double cap = std::min(_pi, (segment.length(radius) * .5 + displacement) / radius);
        double weight = 1;
        if (cap < separation && cap < _pi - separation)
        {
            const double spread = asin(std::clamp(sin(cap) / sin(separation), 0.0, 1.0));
            const double difference = fabs(std::remainder(bearing_from_origin(center) - facing, 2 * _pi));
            weight = 1.5 - .5 * cos(std::max(0.0, difference - spread));
        }
        return distance * weight;
    };
    const auto scan = [&](Coord center, double extent, double spacing)
    {
        const int count = std::clamp(static_cast<int>(ceil(2 * extent / spacing)), 2, 384);
        const double step = 2 * extent / count;
        std::vector<Coord> previous(count + 1), current(count + 1);
        std::vector<bool> previous_water(count + 1), current_water(count + 1);
        for (int row = 0; row <= count; ++row)
        {
            for (int column = 0; column <= count; ++column)
            {
                const Coord point = offset(center, -extent + column * step, -extent + row * step, radius);
                const bool water = surface.water_at(point.first, point.second);
                current[column] = point;
                current_water[column] = water;
                if (column && water != current_water[column - 1]) crossing(current[column - 1], current_water[column - 1], point);
                if (row && water != previous_water[column]) crossing(previous[column], previous_water[column], point);
            }
            previous.swap(current);
            previous_water.swap(current_water);
        }
    };
    const double spacing = std::clamp(radius * std::min(_pi / height, 2 * _pi / width) * .25, .5, 16.0);
    std::priority_queue<Node, std::vector<Node>, FarthestFirst> nearest;
    each_coast([&](const Segment &segment)
    {
        nearest.push({segment, bound(segment)});
        if (nearest.size() > 32) nearest.pop();
    });
    std::vector<Node> seeds;
    while (!nearest.empty()) { seeds.push_back(nearest.top()); nearest.pop(); }
    for (auto it = seeds.rbegin(); it != seeds.rend() && !best.found; ++it)
    {
        scan(it->segment.closest(origin), std::min(_pi * radius, displacement + 256), spacing);
    }

    std::priority_queue<Node> pending;
    each_coast([&](const Segment &segment)
    {
        const double lower = bound(segment);
        if (lower <= best.score) pending.push({segment, lower});
    });
    while (!pending.empty())
    {
        const Node node = pending.top();
        pending.pop();
        if (node.lower > best.score) break;
        if (node.segment.length(radius) > std::max(512.0, 2 * displacement))
        {
            Segment a = node.segment, b = node.segment;
            a.last = b.first = (a.first + a.last) * .5;
            for (const auto &part : {a, b})
            {
                const double lower = bound(part);
                if (lower <= best.score) pending.push({part, lower});
            }
        }
        else scan(node.segment.point(.5), std::min(_pi * radius, displacement + node.segment.length(radius) * .5 + spacing), spacing);
    }
    if (best.found) scan({best.latitude, best.longitude}, 32, .5);
    return best;
}
