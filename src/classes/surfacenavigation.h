#ifndef ALIENORUM_SURFACE_NAVIGATION_H
#define ALIENORUM_SURFACE_NAVIGATION_H

#include "planet.h"
#include "surfacebiomes.h"

namespace alienorum
{
    // One material interpretation for horizon painting, scenery, and navigation.
    class SurfaceSampler
    {
        Planet *planet;
        Map *map;
        SurfaceBiomes &field;
        double radius, strength, bound;
        bool favorable, biomes, ocean, water;

        bool water_sample(const std::pair<RGB3, SurfaceMaterial> &sample) const;

        public:
        SurfaceSampler(Planet *body, Map *surface, SurfaceBiomes &noise, double world_radius, double displacement);
        std::pair<RGB3, SurfaceMaterial> info(double latitude, double longitude);
        RGB3 color_at(double latitude, double longitude) { return info(latitude, longitude).first; }
        bool vegetation_color(const RGB3 &sample) const;
        bool vegetation_at(double latitude, double longitude);
        bool water_at(double latitude, double longitude);
        bool base_water(unsigned long column, unsigned long row) const;
        bool water_favorable() const { return favorable; }
        bool has_biomes() const { return biomes; }
        bool has_surface_water() const { return water; }
        bool ocean_world() const { return ocean; }
        double world_radius() const { return radius; }
        double displacement_bound() const { return biomes ? bound * radius : 0; }
        Map *surface_map() const { return map; }
    };

    struct ShoreLocation
    {
        bool found = false;
        double latitude = 0, longitude = 0;
        double water_latitude = 0, water_longitude = 0;
        double distance = 0, score = 0;
    };

    ShoreLocation find_nearest_shore(SurfaceSampler &surface, double latitude, double longitude, double facing);
}

#endif
