#include "../performance_options.h"
#include "../horizon_performance.h"
#include "localsurfacetile.h"
#include "surfacenavigation.h"
#include "surfaceoptions.h"
#include "surfaceclimate.h"
#include "surfacephase.h"
#include <queue>
#include <limits>

using namespace alienorum;

namespace
{
    bool certified_measured_side(const ElevationGrid& grid, double latitude, double longitude, double angle, bool above)
    {
        if (grid.on_one_side_of_sea_level(latitude, longitude, angle, above)) return true;
        if (!horizon_fast_path(209) || grid.empty() || !std::isfinite(latitude) || !std::isfinite(longitude)
            || !std::isfinite(angle) || angle < 0 || fabs(latitude) + angle >= half_pi) return false;
        const double longitude_radius = angle / cos(fabs(latitude) + angle);
        double wrapped = fmod(longitude + _pi, 2 * _pi);
        if (wrapped < 0) wrapped += 2 * _pi;
        if (wrapped <= longitude_radius || wrapped + longitude_radius >= 2 * _pi) return false;
        const long width = grid.get_width(), height = grid.get_height();
        const double west = (wrapped - longitude_radius) * width / (2 * _pi) - .5;
        const double east = (wrapped + longitude_radius) * width / (2 * _pi) - .5;
        const auto row = [&](double lat)
        {
            return std::clamp((half_pi - lat) * height / _pi - .5, 0.0, double(height - 1));
        };
        const double north = row(latitude + angle), south = row(latitude - angle);
        const long left = floor(west), right = ceil(east), top = floor(north), bottom = ceil(south);
        if (right - left > 16 || bottom - top > 16) return false;
        // Clip the cells to the footprint before bounding their bilinear heights.
        // The extrema occur at the clipped edges or the intervening grid vertices.
        for (long row_index = top; row_index <= bottom; ++row_index)
        {
            const double y = std::clamp(double(row_index), north, south);
            const long y0 = floor(y), y1 = std::min(y0 + 1, height - 1);
            const double ty = y - y0;
            for (long column = left; column <= right; ++column)
            {
                const double x = std::clamp(double(column), west, east);
                const long first = floor(x);
                const long x0 = (first + width) % width, x1 = (x0 + 1) % width;
                const double tx = x - first;
                const auto lerp_row = [&](long y_index)
                {
                    const auto index = static_cast<size_t>(y_index) * width;
                    return grid.elevation_at_index(index + x0) * (1 - tx) + grid.elevation_at_index(index + x1) * tx;
                };
                const double height_m = lerp_row(y0) * (1 - ty) + lerp_row(y1) * ty;
                if ((above ? height_m : -height_m) <= 1e-8) return false;
            }
        }
        return true;
    }

    // Memoize a small Cartesian cell only when every displaced point in it maps
    // to the same atlas pixel. Bounds preserve exact colors and material tags.
    bool certified_surface_cell(Map* map, SurfaceBiomes& field, double radius, double strength,
        double latitude, double longitude, std::pair<RGB3, SurfaceMaterial>& result)
    {
        if (surface_uniform_reference || generating_fic_texture || !map->is_complete()
            || !std::isfinite(radius) || radius <= 0 || radius > 1e12 || !std::isfinite(strength)
            || !std::isfinite(latitude) || !std::isfinite(longitude) || fabs(latitude) > half_pi - .001) return false;
        constexpr double spacing = .5;
        const double minimum_radius = radius - SurfaceBiomes::maximum_displacement(strength);
        if (!(minimum_radius > 2)) return false;
        struct Entry
        {
            std::array<int64_t, 3> cell{};
            bool attempted = false, uniform = false;
            unsigned int revision = 0;
            std::pair<RGB3, SurfaceMaterial> value;
        };
        static thread_local std::array<Entry, 16384> entries;
        using Key = std::tuple<Map*, unsigned int, uint32_t, double, double>;
        static thread_local Key previous;
        const Key key = {map, map->gen, field.world_id(), radius, strength};
        if (key != previous)
        {
            for (auto& entry : entries) entry.attempted = false;
            previous = key;
        }
        const double horizontal = radius * cos(latitude);
        const std::array<double, 3> point = {horizontal * cos(longitude), horizontal * sin(longitude), radius * sin(latitude)};
        std::array<int64_t, 3> cell{};
        uint64_t hash = 1469598103934665603ULL;
        for (int axis = 0; axis < 3; ++axis)
        {
            cell[axis] = static_cast<int64_t>(floor(point[axis] / spacing));
            hash = (hash ^ static_cast<uint64_t>(cell[axis])) * 1099511628211ULL;
        }
        auto& entry = entries[hash & (entries.size() - 1)];
        if (!entry.attempted || entry.cell != cell || entry.revision != map->surface_revision())
        {
            entry.cell = cell;
            entry.revision = map->surface_revision();
            entry.attempted = true;
            entry.uniform = false;
            const double x = (cell[0] + .5) * spacing, y = (cell[1] + .5) * spacing, z = (cell[2] + .5) * spacing;
            const double length = hypot(hypot(x, y), z);
            if (!(length > 0)) return false;
            const auto center = field.coordinates(radius, asin(std::clamp(z / length, -1.0, 1.0)), atan2(y, x), strength);
            // Projecting a cube midpoint onto the sphere moves it at most another
            // half diagonal. The displacement Jacobian is bounded by 1 + 3G.
            const double chord = sqrt(3.0) * spacing * (1 + 3 * SurfaceBiomes::gradient_bound(strength));
            const double angle = 2 * asin(std::min(1.0, chord / (2 * minimum_radius))) + 1e-10;
            RGB3 color;
            entry.uniform = map->color_within_cap(center.first, center.second, angle, color, false);
            if (entry.uniform) entry.value = {color, map->material_at(center.first, center.second)};
        }
        if (entry.uniform) result = entry.value;
        return entry.uniform;
    }
}

SurfaceSampler::SurfaceSampler(Planet *body, Map *surface, SurfaceBiomes &noise, double world_radius, double displacement)
    : planet(body), map(surface), field(noise), radius(world_radius), strength(displacement),
      bound(SurfaceBiomes::angular_bound(world_radius, displacement))
{
    const double pressure = planet ? planet->get_surface_pressure() : 0;
    const double inverse_boil = 1.0 / (water_freezing + 100.0)
        - (8.314 / 40660.0) * std::log(std::max(1.0, pressure) / 1.0e5);
    const double boiling = inverse_boil > 0 ? std::min(647.096, 1.0 / inverse_boil) : 647.096;
    favorable = planet && pressure >= 150 && planet->estimate_surface_temperature() < boiling;
    biomes = planet && map && uses_rocky_map(planet->type) && favorable
        && (planet->has_water || planet->vegetation_r || planet->vegetation_g || planet->vegetation_b);
    ocean = planet && (planet->type == waterworld || planet->type == hycean);
    water = ocean || (biomes && planet->has_water);
}

void SurfaceSampler::capture_life_rules()
{
    frame_surface_life = !planet || planet->simulated_surface_life_allowed();
    frame_land_plants = planet && planet->simulated_land_plants_allowed();
}

void SurfaceSampler::use_measured_shore(double latitude, double longitude)
{
    measured_shore = water && !ocean && planet && planet->bump_has_bathymetry && map->has_calibrated_bump();
    shore_latitude = latitude;
    shore_longitude = longitude;
}

bool SurfaceSampler::shore_dry_within_cap(double latitude, double longitude, double angle) const
{
    return !measured_shore || certified_measured_side(map->calibrated_elevations(), latitude, longitude, angle, true);
}

bool SurfaceSampler::shore_basin_within_cap(double latitude, double longitude, double angle) const
{
    if (!measured_shore || !certified_measured_side(map->calibrated_elevations(), latitude, longitude, angle, false)) return false;
    const auto phase = map->phase_state();
    if (!phase || phase->generation != map->gen || phase->tiles.empty()) return true;

    // A phase patch can expose dry ground inside the measured basin.
    const double center = std::remainder(longitude, 2 * _pi);
    const double longitude_radius = angle / cos(fabs(latitude) + angle);
    for (const auto& entry : phase->tiles)
    {
        const auto& patch = entry.second.rect;
        if (!patch.width || !patch.height) return false;
        const auto northwest = map->surface_pixel_bounds(patch.x, patch.y);
        const auto southeast = map->surface_pixel_bounds(patch.x + patch.width - 1, patch.y + patch.height - 1);
        if (latitude + angle >= southeast[0] && latitude - angle <= northwest[1]
            && center + longitude_radius >= northwest[2] && center - longitude_radius <= southeast[3]) return false;
    }
    return true;
}

bool SurfaceSampler::surface_life_allowed() const
{
    return frame_surface_life ? *frame_surface_life : !planet || planet->simulated_surface_life_allowed();
}

bool SurfaceSampler::land_plants_allowed() const
{
    return frame_land_plants ? *frame_land_plants : planet && planet->simulated_land_plants_allowed();
}

std::pair<RGB3, SurfaceMaterial> SurfaceSampler::info(double latitude, double longitude)
{
    if (measured_shore)
        return measured_info(latitude, longitude, map->elevation_at_bilinear(latitude, longitude) <= 0);
    return texture_info(latitude, longitude);
}

RGB3 SurfaceSampler::shore_swatch(bool in_basin)
{
    auto& swatch = in_basin ? wet_swatch : dry_swatch;
    if (swatch) return *swatch;
    const double lon_step = 2 * _pi / map->get_width();
    const double lat_step = _pi / map->get_height();
    for (int ring = 0; ring <= 2; ++ring)
    {
        for (int y = -ring; y <= ring; ++y)
        {
            for (int x = -ring; x <= ring; ++x)
            {
                if (std::max(abs(x), abs(y)) != ring) continue;
                const auto value = map->surface_at(shore_latitude + y * lat_step, shore_longitude + x * lon_step);
                if (value.second != SurfaceMaterial::ice && water_sample(value) == in_basin)
                {
                    swatch = value.first;
                    return *swatch;
                }
            }
        }
    }
    swatch = in_basin ? RGB3(23, 53, 103) : RGB3(168, 155, 127);
    return *swatch;
}

std::pair<RGB3, SurfaceMaterial> SurfaceSampler::measured_info(double latitude, double longitude, bool in_basin)
{
    const auto value = texture_info(latitude, longitude);
    if (in_basin && value.second == SurfaceMaterial::ice) return value;
    if (in_basin && value.second == SurfaceMaterial::ground && map->phase_ocean_at(latitude, longitude)) return value;
    const bool texture_water = water_sample(value);
    if (texture_water == in_basin && value.second != SurfaceMaterial::ice) return value;
    // The color atlas can put the coast in a different place from measured relief.
    return {shore_swatch(in_basin), in_basin ? SurfaceMaterial::water : SurfaceMaterial::ground};
}

std::pair<RGB3, SurfaceMaterial> SurfaceSampler::texture_info(double latitude, double longitude)
{
    if (!map) return {RGB3(0, 8, 24), SurfaceMaterial::unknown};
    if (biomes)
    {
        RGB3 uniform;
        if (map->color_within_cap(latitude, longitude, bound, uniform, true))
            return {uniform, map->material_at(latitude, longitude)};
        if (performance_enabled(37) && local_tile && local_tile->matches(field.world_id(), map, map->gen, radius, strength))
        {
            std::pair<double, double> approximate;
            double error_angle = 0;
            RGB3 certified;
            if (local_tile->coordinates(field, latitude, longitude, approximate, error_angle)
                && map->color_within_cap(approximate.first, approximate.second, error_angle, certified, true))
                return {certified, map->material_at(approximate.first, approximate.second)};
        }
        std::pair<RGB3, SurfaceMaterial> certified;
        if (certified_surface_cell(map, field, radius, strength, latitude, longitude, certified)) return certified;
        const auto coord = field.coordinates(radius, latitude, longitude, strength);
        latitude = coord.first;
        longitude = coord.second;
    }
    if (PERF_P032) return map->surface_at(latitude, longitude);
    return {map->color_at(latitude, longitude), map->material_at(latitude, longitude)};
}

bool SurfaceSampler::vegetation_color(const RGB3 &sample) const
{
    if (!planet || !map || !uses_rocky_map(planet->type) || !favorable || !planet->has_water
        || !surface_life_allowed()
        || !(planet->vegetation_r || planet->vegetation_g || planet->vegetation_b)) return false;
    const double delta = fabs(Color::hue_from_rgb(sample.r, sample.g, sample.b)
        - Color::hue_from_rgb(planet->vegetation_r, planet->vegetation_g, planet->vegetation_b));
    return std::min(delta, 2 * _pi - delta) < .35
        && fabs(Color::sat_from_rgb(sample.r, sample.g, sample.b)
            - Color::sat_from_rgb(planet->vegetation_r, planet->vegetation_g, planet->vegetation_b)) < .35;
}

bool SurfaceSampler::vegetation_at(double latitude, double longitude)
{
    if (!surface_life_allowed()) return false;
    const auto sample = info(latitude, longitude);
    return sample.second == SurfaceMaterial::unknown ? vegetation_color(sample.first)
        : sample.second == SurfaceMaterial::vegetation;
}

double SurfaceSampler::vegetation_density_at(double latitude, double longitude)
{
    if (!planet || !map || !surface_life_allowed()) return 0;
    if (!vegetation_habitat)
    {
        const double pressure = planet->get_surface_pressure();
        const Orbit* orbit = planet->orbit;
        auto key = std::make_tuple(planet, map, map->gen, planet->temperature, pressure,
            planet->mass, planet->volumetric_mean_radius, planet->obliquity,
            planet->sidereal_rotational_period, planet->veg_min_temp, planet->veg_max_temp,
            orbit ? orbit->semimajor_axis : 0.0, orbit ? orbit->eccentricity : 0.0,
            orbit ? orbit->period : 0.0);
        static thread_local decltype(key) previous;
        static thread_local std::shared_ptr<const VegetationHabitat> cached;
        if (!cached || previous != key)
        {
            cached = std::make_shared<VegetationHabitat>(planet->vegetation_seasons());
            previous = key;
        }
        vegetation_habitat = cached;
        const double salinity = planet->ocean_chemistry().at("salinity_g_kg").get<double>();
        vegetation_freezing = seawater_freezing_k(salinity, pressure);
        vegetation_boiling = surface_boiling_k(pressure);
    }
    // Use geographic coordinates rather than the coarser biome pixel, so density
    // remains continuous as the viewer crosses texture samples.
    const double ring = std::cos(latitude);
    return vegetation_habitat->density(-ring * std::cos(longitude), -ring * std::sin(longitude),
        std::sin(latitude), map->elevation_at_bilinear(latitude, longitude), planet->veg_min_temp,
        planet->veg_max_temp, vegetation_freezing, vegetation_boiling);
}

double alienorum::vegetation_cover_fraction(const RGB3& ground, const RGB3& foliage)
{
    // Project chromaticity along a soil-to-foliage blend. Brightness variations
    // from map shading cannot turn a sparse grove into a forest.
    const double total = double(ground.r) + ground.g + ground.b;
    const double leaf_total = double(foliage.r) + foliage.g + foliage.b;
    if (!(total > 0) || !(leaf_total > 0)) return 0;
    const double soil[] = {.40, .35, .25};
    const double sample[] = {ground.r / total, ground.g / total, ground.b / total};
    const double leaf[] = {foliage.r / leaf_total, foliage.g / leaf_total, foliage.b / leaf_total};
    double projection = 0, length = 0;
    for (int channel = 0; channel < 3; ++channel)
    {
        const double direction = leaf[channel] - soil[channel];
        projection += (sample[channel] - soil[channel]) * direction;
        length += direction * direction;
    }
    return length > 1e-6 ? vegetation_transition(projection / length) : 0;
}

bool SurfaceSampler::tree_site_at(double latitude, double longitude)
{
    if (!planet || !map || !map->is_complete() || !uses_rocky_map(planet->type)
        || !land_plants_allowed() || !planet->has_water
        || !(planet->vegetation_r || planet->vegetation_g || planet->vegetation_b)) return false;
    // Tree identity follows permanent terrain, never the published frost/foliage color.
    const auto coordinate = biomes ? field.coordinates(radius, latitude, longitude, strength)
        : std::make_pair(latitude, longitude);
    const auto terrain = map->underlying_surface_at(coordinate.first, coordinate.second);
    if (terrain.second == SurfaceMaterial::water || ocean) return false;
    if (map->procedurally_generated && map->has_bump_data())
        return map->elevation_at(coordinate.first, coordinate.second) > planet->m_bump_scale * (planet->has_water - .5);
    return !water_sample(terrain);
}

double SurfaceSampler::tree_density_at(double latitude, double longitude)
{
    if (!tree_site_at(latitude, longitude)) return 0;
    // Established woodland follows one full-season climate baseline per map.
    // Surface phase and live atmospheric changes affect crowns, not tree identity.
    struct ForestClimate
    {
        std::shared_ptr<const VegetationHabitat> habitat;
        double freezing, boiling;
    };
    const auto key = std::make_tuple(planet, map, map->instance_id(), planet->veg_min_temp, planet->veg_max_temp);
    static thread_local std::map<decltype(key), ForestClimate> forests;
    auto found = forests.find(key);
    if (found == forests.end())
    {
        vegetation_density_at(latitude, longitude);
        if (forests.size() >= 64) forests.erase(forests.begin());
        found = forests.emplace(key, ForestClimate{vegetation_habitat, vegetation_freezing, vegetation_boiling}).first;
    }
    const auto& forest = found->second;
    const double ring = std::cos(latitude);
    const double habitat = forest.habitat->density(-ring * std::cos(longitude), -ring * std::sin(longitude),
        std::sin(latitude), map->elevation_at_bilinear(latitude, longitude), planet->veg_min_temp,
        planet->veg_max_temp, forest.freezing, forest.boiling);
    if (map->procedurally_generated) return habitat;
    const auto coordinate = biomes ? field.coordinates(radius, latitude, longitude, strength)
        : std::make_pair(latitude, longitude);
    const auto terrain = map->underlying_surface_at(coordinate.first, coordinate.second);
    if (terrain.second == SurfaceMaterial::vegetation) return habitat;
    return habitat * vegetation_cover_fraction(terrain.first,
        RGB3(planet->vegetation_r, planet->vegetation_g, planet->vegetation_b));
}

TreeAppearance SurfaceSampler::tree_appearance_at(double latitude, double longitude,
    const std::vector<LocalClimate>& recent_climates)
{
    if (!planet || !map || recent_climates.empty()) return {};
    const auto coordinate = biomes ? field.coordinates(radius, latitude, longitude, strength)
        : std::make_pair(latitude, longitude);
    const double elevation = map->elevation_at_bilinear(latitude, longitude);
    // The phase solver already integrates the accumulation and melting of land frost.
    const auto state = map->surface_state_at(coordinate.first, coordinate.second, recent_climates.front(),
        0, planet->get_surface_pressure());
    const double ring = std::cos(latitude);
    return tree_appearance(recent_climates, -ring * std::cos(longitude), -ring * std::sin(longitude),
        std::sin(latitude), elevation, planet->veg_min_temp, planet->veg_max_temp, state.ice);
}

SurfaceSampler::Sample SurfaceSampler::sample(double latitude, double longitude)
{
    const auto value = info(latitude, longitude);
    const bool vegetation = surface_life_allowed()
        && (value.second == SurfaceMaterial::unknown
            ? vegetation_color(value.first) : value.second == SurfaceMaterial::vegetation);
    return {value.first, value.second, water_sample(value), vegetation};
}

bool SurfaceSampler::uniform_sample(double latitude, double longitude, double angle, Sample& result)
{
    RGB3 color;
    const bool wet_cap = measured_shore && horizon_fast_path(206)
        && map->elevation_at_bilinear(latitude, longitude) < -1e-8;
    if (!map || !map->is_complete() || generating_fic_texture || !std::isfinite(angle) || angle < 0
        || (wet_cap ? !certified_measured_side(map->calibrated_elevations(), latitude, longitude, angle, false)
            : !shore_dry_within_cap(latitude, longitude, angle))
        || !map->color_within_cap(latitude, longitude, angle + (biomes ? bound : 0), color, true)) return false;
    const auto source = std::make_pair(color, map->material_at(latitude, longitude));
    // Published ground beneath sea level can represent local evaporation.
    if (wet_cap && !water_sample(source) && source.second != SurfaceMaterial::ice) return false;
    const auto value = measured_shore && !wet_cap ? measured_info(latitude, longitude, false) : source;
    result = {color, value.second, water_sample(value), surface_life_allowed()
        && (value.second == SurfaceMaterial::unknown
            ? vegetation_color(color) : value.second == SurfaceMaterial::vegetation)};
    return true;
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

double SurfaceSampler::basin_elevation() const
{
    return planet && map && map->procedurally_generated && !planet->bump_has_bathymetry
        ? planet->m_bump_scale * (planet->has_water - .5) : 0;
}

double SurfaceSampler::skyline_elevation(double latitude, double longitude)
{
    if (measured_shore) return std::max(0.0, map->elevation_at_bilinear(latitude, longitude));
    // Basin surfaces share one elevation even when the relief map disagrees at the coast.
    return basin_at(latitude, longitude) ? basin_elevation() : map->elevation_at_bilinear(latitude, longitude);
}

bool SurfaceSampler::basin_at(double latitude, double longitude, std::pair<RGB3, SurfaceMaterial>* sampled)
{
    if (!water || !map) return false;
    if (measured_shore)
    {
        const bool in_basin = map->elevation_at_bilinear(latitude, longitude) <= 0;
        if (sampled) *sampled = measured_info(latitude, longitude, in_basin);
        return in_basin;
    }
    const auto value = info(latitude, longitude);
    if (sampled) *sampled = value;
    if (water_sample(value) || value.second == SurfaceMaterial::ice) return true;
    if (value.second == SurfaceMaterial::frost) return false;
    const bool procedural_basin = map->procedurally_generated && map->has_bump_data() && planet && planet->has_water > 0;
    const bool has_phase = map->water_phase_revision() != 0;
    if (!procedural_basin && !has_phase) return false;
    const auto coordinate = biomes ? field.coordinates(radius, latitude, longitude, strength)
        : std::make_pair(latitude, longitude);
    return (has_phase && map->phase_ocean_at(coordinate.first, coordinate.second))
        || (procedural_basin && map->elevation_at(coordinate.first, coordinate.second) <= basin_elevation());
}

bool SurfaceSampler::water_at(double latitude, double longitude, WaterPatch& patch, bool include_frozen_basins)
{
    const auto classify = [&]() { return include_frozen_basins ? basin_at(latitude, longitude) : water_at(latitude, longitude); };
    if (measured_shore || !water || !map || !planet || !(radius > 0) || !std::isfinite(radius)
        || !std::isfinite(latitude) || !std::isfinite(longitude) || fabs(latitude) > half_pi)
        return classify();
    const WaterPatch::Key key = {map, include_frozen_basins ? map->gen : map->surface_revision(), planet, radius, strength, biomes, ocean, favorable,
        planet->has_water != 0, planet->type, planet->vegetation_r, planet->vegetation_g, planet->vegetation_b,
        include_frozen_basins, surface_life_allowed()};
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
        return classify();
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
        patch.value = include_frozen_basins ? basin_at(latitude, longitude) : water_sample({color, map->material_at(latitude, longitude)});
        return patch.value;
    }
    return classify();
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

    struct ElevationCell
    {
        double south, north, west, east;
        double sw, se, nw, ne;

        bool crosses_sea_level() const
        {
            return std::min({sw, se, nw, ne}) <= 0 && std::max({sw, se, nw, ne}) > 0;
        }
    };

    const std::vector<ElevationCell>& measured_coastal_cells(const Map& map)
    {
        static thread_local const Map* source = nullptr;
        static thread_local unsigned int generation = 0;
        static thread_local std::vector<ElevationCell> cells;
        if (source == &map && generation == map.gen) return cells;

        const auto& grid = map.calibrated_elevations();
        const auto width = grid.get_width(), height = grid.get_height();
        const double lat_step = _pi / height, lon_step = 2 * _pi / width;
        cells.clear();
        // Between pixel centers the elevation is bilinear. Only cells with
        // both signs can contain a shoreline. Include the clamped polar strips.
        for (unsigned int row = 0; row <= height; ++row)
        {
            const auto top = row ? row - 1 : 0;
            const auto bottom = std::min(row, height - 1);
            const double north = row ? half_pi - (row - .5) * lat_step : half_pi;
            const double south = row < height ? half_pi - (row + .5) * lat_step : -half_pi;
            for (unsigned int column = 0; column < width; ++column)
            {
                const auto next = (column + 1) % width;
                const auto index = [&](unsigned int y, unsigned int x) { return size_t(y) * width + x; };
                const double west = (column + .5) * lon_step - _pi;
                const ElevationCell cell{south, north, west, west + lon_step,
                    grid.elevation_at_index(index(bottom, column)), grid.elevation_at_index(index(bottom, next)),
                    grid.elevation_at_index(index(top, column)), grid.elevation_at_index(index(top, next))};
                if (cell.crosses_sea_level()) cells.push_back(cell);
            }
        }
        source = &map;
        generation = map.gen;
        return cells;
    }
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
    const bool measured = surface.has_measured_shore();
    const auto is_water = [&](Coord point)
    {
        return measured ? surface.basin_at(point.first, point.second) : surface.water_at(point.first, point.second);
    };
    const auto crossing = [&](Coord a, bool wet_a, Coord b)
    {
        for (int iteration = 0; iteration < 40 && radius * angle(a, b) > .05; ++iteration)
        {
            const Coord middle = midpoint(a, b);
            if (is_water(middle) == wet_a) a = middle;
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
    const bool origin_wet = is_water(origin);
    for (int i = 0; i < 32; ++i)
    {
        const double bearing = 2 * _pi * i / 32;
        const Coord point = offset(origin, sin(bearing), cos(bearing), radius);
        if (is_water(point) != origin_wet) crossing(origin, origin_wet, point);
    }
    if (best.found && best.distance <= .05) return best;

    if (measured)
    {
        struct Candidate
        {
            ElevationCell cell;
            double lower;
            bool operator<(const Candidate& other) const { return lower > other.lower; }
        };
        std::priority_queue<Candidate> candidates;
        const auto enqueue = [&](const ElevationCell& cell)
        {
            if (!cell.crosses_sea_level()) return;
            const Coord center{(cell.south + cell.north) * .5, (cell.west + cell.east) * .5};
            const double cap = ((cell.north - cell.south) + (cell.east - cell.west)) * .5;
            const double separation = angle(origin, center);
            double weight = 1;
            if (cap < separation && cap < _pi - separation)
            {
                const double spread = asin(std::clamp(sin(cap) / sin(separation), 0.0, 1.0));
                const double difference = fabs(std::remainder(bearing_from_origin(center) - facing, 2 * _pi));
                weight = 1.5 - .5 * cos(std::max(0.0, difference - spread));
            }
            const double lower = radius * std::max(0.0, separation - cap) * weight;
            if (lower <= best.score) candidates.push({cell, lower});
        };
        for (const auto& cell : measured_coastal_cells(*map)) enqueue(cell);
        while (!candidates.empty() && candidates.top().lower <= best.score)
        {
            const auto cell = candidates.top().cell;
            candidates.pop();
            const auto bracket = [&](Coord first, Coord second, double first_height, double second_height)
            {
                if ((first_height <= 0) == (second_height <= 0)) return;
                const bool first_water = is_water(first);
                if (first_water != is_water(second)) crossing(first, first_water, second);
            };
            if ((cell.north - cell.south + cell.east - cell.west) * radius <= 2)
            {
                bracket({cell.south, cell.west}, {cell.south, cell.east}, cell.sw, cell.se);
                bracket({cell.north, cell.west}, {cell.north, cell.east}, cell.nw, cell.ne);
                bracket({cell.south, cell.west}, {cell.north, cell.west}, cell.sw, cell.nw);
                bracket({cell.south, cell.east}, {cell.north, cell.east}, cell.se, cell.ne);
                continue;
            }
            const double mid_lat = (cell.south + cell.north) * .5, mid_lon = (cell.west + cell.east) * .5;
            const double s = (cell.sw + cell.se) * .5, n = (cell.nw + cell.ne) * .5;
            const double w = (cell.sw + cell.nw) * .5, e = (cell.se + cell.ne) * .5, center = (s + n) * .5;
            enqueue({cell.south, mid_lat, cell.west, mid_lon, cell.sw, s, w, center});
            enqueue({cell.south, mid_lat, mid_lon, cell.east, s, cell.se, center, e});
            enqueue({mid_lat, cell.north, cell.west, mid_lon, w, center, cell.nw, n});
            enqueue({mid_lat, cell.north, mid_lon, cell.east, center, e, n, cell.ne});
        }
        return best;
    }

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
