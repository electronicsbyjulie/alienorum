#include "../classes/elevationgrid.h"
#include "../classes/terrainlighting.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace alienorum;

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    nlohmann::json catalog;
    std::ifstream("catalogs/planets.json") >> catalog;
    const std::vector<std::string> names = {"Earth", "Moon", "Mercury", "Venus", "Mimas", "Tethys", "Dione",
        "Phoebe", "Phobos", "Deimos", "Hyperion", "Janus", "Epimetheus", "Enceladus", "Titan"};
    constexpr double pi = 3.14159265358979323846;
    for (const auto& name : names)
    {
        const auto record = std::find_if(catalog.begin(), catalog.end(), [&](const auto& value)
        {
            return value.value("BODYNAME", std::string()) == name;
        });
        check(record != catalog.end(), "Calibrated body is in the catalog");
        check(record->contains("BumpHeight") && record->contains("BumpOffset"), "Numeric map has explicit calibration");
        ElevationGrid grid;
        check(grid.load(record->at("BumpMap").template get<std::string>(),
            record->at("BumpHeight"), record->at("BumpOffset")), "Calibrated runtime PNG loads");
        double lowest = 1e30, highest = -1e30;
        for (int lat = -85; lat <= 85; lat += 5)
        {
            for (int lon = -180; lon < 180; lon += 5)
            {
                const double value = grid.sample(lat * pi / 180, lon * pi / 180);
                check(std::isfinite(value), "Terrain samples remain finite");
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
        }
        check(highest - lowest > 500, "Known worlds retain kilometre-scale terrain");
        if (name == "Earth") check(lowest < -6000 && highest > 3000, "Earth includes bathymetry and mountains");
        if (name == "Moon") check(lowest < -6000 && highest > 5000, "LOLA retains physical lunar relief");
        if (name == "Mercury") check(lowest < -2000 && highest > 2000, "MESSENGER scale factor is applied");
        if (name == "Venus") check(highest > 6000, "Magellan retains highland elevations");
        std::cout << name << ": sampled range " << lowest << " to " << highest << " m\n";
    }
    const TerrainVector axes = {1000000, 1000000, 1000000};
    const auto flat = [](double, double) { return 0.0; };
    for (double longitude : {-pi, 0.0, pi})
    {
        const auto normal = terrain_normal(longitude, 0, axes, .001, .001, flat);
        check(std::abs(normal[0] + sin(longitude)) < 1e-10 && std::abs(normal[2] - cos(longitude)) < 1e-10,
            "Flat spheres and longitude seams keep their radial normal");
    }
    const auto slope = [](double, double longitude) { return longitude * 100000; };
    const auto normal = terrain_normal(0, 0, axes, .001, .001, slope);
    check(normal[0] > .09 && normal[2] > .99, "Physical elevation gradient tilts the normal");
    check(terrain_light_ratio(normal[0] * .98 + normal[2] * .2, .2) > 1,
        "Slope toward the Sun brightens");
    check(terrain_light_ratio(-normal[0] * .98 + normal[2] * .2, .2) < 1,
        "Slope away from the Sun darkens");
    check(terrain_light_ratio(0, .5) > 0, "Unlit slopes keep ambient illumination");
    const auto pole = terrain_normal(0, pi / 2, axes, .001, .001, slope);
    check(std::isfinite(pole[1]) && pole[1] > .99, "Polar normals stay finite");
    std::cout << "Solar elevation and terrain lighting checks passed\n";
}
