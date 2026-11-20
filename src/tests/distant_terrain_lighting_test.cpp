#include "../classes/distantskyline.h"
#include "../classes/elevationgrid.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace alienorum;

static void check(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

int main()
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double radius = 3396180.9694929486;
    ElevationGrid map;
    check(map.load("maps/Mars_bump.png", 65535, -.5), "Mars elevation loads");
    int calls = 0;
    const auto sample = [&](double latitude, double longitude)
    {
        ++calls;
        return map.sample(latitude, longitude);
    };
    DistantSkyline skyline;
    const auto start = std::chrono::steady_clock::now();
    skyline.update(&map, 1, 4, 22 * pi / 180, -138.1 * pi / 180, radius, 1.7526, .5 / radius, sample, true);
    const auto finish = std::chrono::steady_clock::now();
    check(!skyline.layers().empty(), "Visible terrain produces shaded faces");
    double greatest_change = 0;
    for (const auto& layer : skyline.layers())
    {
        const double length = hypot(hypot(layer.normal[0], layer.normal[1]), layer.normal[2]);
        check(std::abs(length - 1) < 1e-9, "Face normals have unit length");
        check(layer.top_left >= layer.bottom_left && layer.top_right >= layer.bottom_right,
            "Layers clip against closer terrain without inverted faces");
        check(layer.top_left > layer.bottom_left || layer.top_right > layer.bottom_right,
            "Fully hidden bands are excluded");
        check(layer.top_left <= skyline.angle(layer.bearing_left) + 1e-9,
            "Shaded terrain stays within the sampled silhouette");
        const double east = DistantSkyline::illumination(layer, {.9, .2, .1});
        const double west = DistantSkyline::illumination(layer, {-.9, .2, -.1});
        check(std::isfinite(east) && east > 0 && std::isfinite(west) && west > 0,
            "Shaded faces retain ambient illumination");
        greatest_change = std::max(greatest_change, std::abs(east - west));
    }
    check(greatest_change > .5, "Reversing the Sun changes slope brightness");
    const int cold_calls = calls;
    skyline.update(&map, 1, 4, 22 * pi / 180, -138.1 * pi / 180, radius, 1.7526, .5 / radius, sample, true);
    check(calls == cold_calls, "Static geometry and normals reuse the cache");
    const double angle = skyline.angle(140 * pi / 180);
    skyline.update(&map, 1, 4, 22 * pi / 180, -138.1 * pi / 180, radius, 1.7526, .5 / radius, sample, false);
    check(skyline.layers().empty(), "Comparison mode omits layer geometry");
    check(skyline.angle(140 * pi / 180) == angle, "Layering preserves the elevation skyline");
    DistantSkyline::Layer flat{};
    flat.normal = {0, 1, 0};
    check(DistantSkyline::illumination(flat, {.8, .6, 0}) == 1, "Flat terrain matches ground lighting");
    std::cout << "Distant terrain lighting checks passed; cold geometry "
        << std::chrono::duration<double, std::milli>(finish - start).count() << " ms\n";
}
