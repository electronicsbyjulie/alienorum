#ifndef ALIENORUM_FICTIONAL_STARS_H
#define ALIENORUM_FICTIONAL_STARS_H

#include "galaxy.h"
#include "star.h"
#include "planet.h"
#include <atomic>
#include <memory>
#include <random>
#include <vector>

namespace alienorum
{
    struct FictionalImage
    {
        int width = 0, height = 0;
        std::string path, hash;
        bool panoramic = false;
        std::vector<float> rgb;
        bool load(const std::string& filename, bool panorama);
        Point sample(double u, double v) const;
        bool available() const { return !rgb.empty(); }
    };

    struct FictionalGalaxy
    {
        std::string key, code, name;
        Point origin_mly, east, height_axis, south;
        double radius_ly = 50000;
        double morphology = 3;
        bool milky_way = false, spheroid = false;
        FictionalImage image;
        Rotation band_plane;
        double band_yaw = 0;
        std::vector<double> ray_columns;
        Point image_ray(double u, double v) const;
        static FictionalGalaxy from_galaxy(const Galaxy& galaxy);
        Point world_offset(Point disk_ly) const;
        Point disk_offset(Point world_meters) const;
        Point from_milky_ray(double longitude, double latitude, double distance_ly) const;
        Point population_color(Point disk_ly) const;
        double density(Point disk_ly) const;
        bool inside(Point disk_ly) const;
        double advisory_inner() const;
        double advisory_outer() const;
    };

    struct FictionalRegion
    {
        Point center_ly;
        double radius_ly = 1000;
        int star_count = 10000;
        std::string catalog_prefix = "F";
        double minimum_spacing_ly = 1;
        int center_subtype = 6;
        uint64_t seed = 1;
        std::string region_id;
        std::string identity;
        std::vector<std::string> reserved_names;
        bool clusters = true;
        bool planetary_systems = true;
        bool add_moons = false;
        bool historical_annulus = false;
    };

    struct FictionalBatch
    {
        std::vector<std::unique_ptr<StarMulti>> staged_systems;
        std::vector<std::unique_ptr<Star>> stars;
        std::vector<std::unique_ptr<Planet>> worlds;
        int planet_hosts = 0, planets = 0, moons = 0;
        std::string error;
        json provenance;
        double elapsed_seconds = 0;
        int systems = 0, clusters = 0, nearby = 0, naked_eye = 0;
        size_t estimated_save_bytes = 0;
        bool install(std::string& message);
    };

    void generate_fictional_white_dwarf(Star& star, double age_gyr, std::mt19937_64& random);
    void restore_fictional_motion(Star& star);
    std::vector<std::string> fictional_reserved_names();
    std::string fictional_region_id();
    std::string fictional_population_id();
    const char* fictional_center_type(int index);
    FictionalBatch generate_fictional_stars(const FictionalGalaxy& galaxy, const FictionalRegion& region,
        const std::atomic<bool>* cancel = nullptr, std::atomic<int>* progress = nullptr);
}

#endif
