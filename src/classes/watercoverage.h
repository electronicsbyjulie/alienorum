#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "surfacenavigation.h"

namespace alienorum
{
    // Dialog-only conversion; terrain generation and serialized water levels stay unchanged.
    class WaterCoverage
    {
        Planet *body = nullptr;
        Map *map = nullptr;
        unsigned int generation = 0;
        std::array<double, 17> state{};
        std::vector<double> thresholds;
        double measured = 0;
        bool available = false, adjustable = false, generated_surface = false;
        static constexpr int rows = 128, columns = 256;
        static constexpr double samples = rows * columns;

        public:
        void refresh(Planet *planet, double displacement)
        {
            if (generating_fic_texture || !planet || !planet->surf_map || !planet->surf_map->is_complete())
            {
                available = false;
                return;
            }
            Map *surface = planet->surf_map;
            LocalClimate climate = planet->local_climate();
            const double radius = planet->get_equatorial_radius();
            const double bump_scale = planet->m_bump_scale > 0 ? planet->m_bump_scale : planet->estimate_bump_scale();
            const bool generated = surface->has_bump_data()
                && surface->surface_pixel(0, 0).second != SurfaceMaterial::unknown;
            const bool ocean = planet->type == waterworld || planet->type == hycean;
            const bool editable = surface->has_bump_data() && !ocean && bump_scale > 0;
            const std::array<double, 17> current =
            {
                climate.mean_k, climate.contrast_k, climate.redistribution, climate.regional_k,
                climate.sun_x, climate.sun_y, climate.sun_z, double(climate.stellar_locked),
                bump_scale, radius, displacement, planet->get_surface_pressure(),
                double(planet->type), double(planet->vegetation_r), double(planet->vegetation_g),
                double(planet->vegetation_b), generated ? 0.0 : double(planet->has_water)
            };
            if (available && body == planet && map == surface && generation == surface->gen && state == current) return;
            body = planet;
            map = surface;
            generation = surface->gen;
            state = current;
            adjustable = editable;
            generated_surface = generated && !ocean;
            thresholds.clear();
            measured = 0;
            SurfaceBiomes field(static_cast<uint32_t>(planet->seqno));
            SurfaceSampler sampler(planet, surface, field, radius, displacement);
            for (int row = 0; row < rows; ++row)
            {
                // Uniform sin(latitude) gives equal surface area, including the polar caps.
                const double latitude = asin(-1.0 + 2.0 * (row + .5) / rows);
                for (int column = 0; column < columns; ++column)
                {
                    const double longitude = -_pi + 2 * _pi * (column + .5) / columns;
                    if (!generated_surface) measured += sampler.water_at(latitude, longitude) ? 100.0 / samples : 0;
                    if (!adjustable) continue;
                    const auto coordinate = field.coordinates(radius, latitude, longitude, displacement);
                    const double height = surface->elevation_at(coordinate.first, coordinate.second) / bump_scale + .5;
                    if (std::isfinite(height)) thresholds.push_back(std::clamp(height, 0.0, 1.0));
                }
            }
            std::sort(thresholds.begin(), thresholds.end());
            available = true;
        }

        bool ready() const { return available; }
        bool editable() const { return available && adjustable; }
        double percent(double level) const
        {
            if (!available) return 0;
            if (!generated_surface || !adjustable) return measured;
            if (!std::isfinite(level)) return 0;
            const auto end = std::lower_bound(thresholds.begin(), thresholds.end(), std::clamp(level, 0.0, 1.0));
            return 100 * (end - thresholds.begin()) / samples;
        }
        float level(double percentage) const
        {
            if (!editable() || !std::isfinite(percentage) || percentage <= 0) return 0;
            if (percentage >= 100 || thresholds.empty()) return 1;
            const size_t count = static_cast<size_t>(ceil(std::clamp(percentage, 0.0, 100.0) * .01 * samples));
            if (count > thresholds.size()) return 1;
            const double threshold = thresholds[count - 1];
            float result = static_cast<float>(threshold);
            if (result <= threshold) result = std::nextafter(result, 1.0f);
            return std::clamp(result, 0.0f, 1.0f);
        }
    };

    // A percentage entered before elevations exist survives the first generation pass.
    class WaterCoverageEditor
    {
        WaterCoverage coverage;
        Planet *body = nullptr;
        int body_index = -1;
        double requested = 0;
        bool pending = false, awaiting_elevations = false;
        unsigned int starting_generation = 0;

        unsigned int map_generation() const { return body && body->surf_map ? body->surf_map->gen : 0; }

        public:
        void refresh(Planet *planet, double displacement)
        {
            if (planet != body)
            {
                body = planet;
                body_index = planet ? planet->seqno : -1;
                pending = awaiting_elevations = false;
            }
            coverage.refresh(planet, displacement);
            if (!coverage.ready() || !pending || map_generation() == starting_generation) return;
            if (awaiting_elevations)
            {
                if (!coverage.editable()) return;
                body->has_water = coverage.level(requested);
                awaiting_elevations = false;
                starting_generation = map_generation();
                body->mark_all_maps_for_regen();
            }
            else pending = false;
        }
        void poll(double displacement)
        {
            if (!pending || generating_fic_texture) return;
            if (body_index < 0 || body_index >= ncelobjs || !cels || cels[body_index] != body || body->deleted)
            {
                pending = awaiting_elevations = false;
                body = nullptr;
                return;
            }
            if (map_generation() != starting_generation) refresh(body, displacement);
        }
        bool ready() const { return coverage.ready(); }
        bool has_target() const { return pending; }
        double percent() const { return pending ? requested : coverage.percent(body ? body->has_water : 0); }
        bool editable() const { return body && body->type != waterworld && body->type != hycean && !generating_fic_texture; }
        void edit(double percentage)
        {
            if (!editable() || !std::isfinite(percentage)) return;
            requested = std::clamp(percentage, 0.0, 100.0);
            pending = true;
            awaiting_elevations = !coverage.editable();
            starting_generation = map_generation();
            if (coverage.editable()) body->has_water = coverage.level(requested);
            body->user_edited = true;
        }
        void prepare_generation(CelestialObject *object, bool new_terrain = false)
        {
            if (object != body || !editable()) return;
            if (!pending) return;
            requested = percent();
            pending = true;
            awaiting_elevations = new_terrain || !coverage.editable();
            starting_generation = map_generation();
            if (!awaiting_elevations) body->has_water = coverage.level(requested);
        }
    };

}
