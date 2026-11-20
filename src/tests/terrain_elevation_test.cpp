#include "../classes/elevationgrid.h"
#include "../classes/distantskyline.h"
#include <chrono>
#include <filesystem>
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
    const auto radians = [](double value) { return value * pi / 180; };
    ElevationGrid grid;
    const auto start = std::chrono::steady_clock::now();
    check(grid.load("maps/Mars_bump.png", 65535, -.5), "Calibrated Mars PNG loads");
    const auto loaded = std::chrono::steady_clock::now();
    auto sample = [&](double latitude, double longitude) { return grid.sample(latitude, longitude); };
    const double summit = sample(radians(18.65), radians(-133.8));
    check(summit > 19000 && summit < 22000, "Summit remains above 19 km without clipping");
    const double cliff = sample(radians(21.510), radians(-137.646));
    check(cliff > 5000 && cliff < 8000, "Mapped cliff has physical elevation");
    for (const auto& view : {std::array<double, 3>{22, -138.1, 140}, {21.8, -137.95, 136}})
    {
        const double latitude = radians(view[0]), longitude = radians(view[1]);
        const double ground = sample(latitude, longitude);
        check(ground > -2500 && ground < -500, "Viewpoint lies on the low plains");
        check(cliff - ground > 7000, "Basal scarp retains over 7 km of relief");
        DistantSkyline skyline;
        int calls = 0;
        auto counted = [&](double lat, double lon) { ++calls; return sample(lat, lon); };
        skyline.update(&grid, 1, 4, latitude, longitude, radius, 1.7526, .5 / radius, counted);
        const double angle = skyline.angle(radians(view[2])) * 180 / pi;
        check(angle > 8 && angle < 25, "Distant cliff forms a dramatic skyline");
        const int cold_calls = calls;
        skyline.update(&grid, 1, 4, latitude, longitude, radius, 1.7526, .5 / radius, counted);
        check(calls == cold_calls, "Static views reuse elevation samples");
        std::cout << view[0] << ", " << view[1] << ": ground " << ground
            << " m; skyline " << angle << " degrees\n";
    }
    check(std::abs(sample(0, -pi) - sample(0, pi)) < 1e-8, "Longitude seam wraps");
    check(std::isfinite(sample(pi, 0)) && std::isfinite(sample(-pi, 0)), "Polar samples clamp");
    const auto temporary = std::filesystem::temp_directory_path() / "alienorum-elevation-roundtrip.png";
    check(grid.save(temporary.string()), "Save preserves numeric PNG");
    ElevationGrid restored;
    check(restored.load(temporary.string(), 65535, -.5), "Saved numeric PNG reloads");
    check(restored.sample(radians(18.65), radians(-133.8)) == summit, "Round trip retains full precision");
    ElevationGrid flat;
    check(flat.load(temporary.string(), 0, 123), "Explicit zero height is valid");
    check(flat.sample(0, 0) == 123, "Zero height retains its supplied offset");
    check(!flat.load(temporary.string(), -1, 0), "Negative height is rejected");
    check(flat.sample(0, 0) == 123, "Failed import preserves available data");
    std::filesystem::remove(temporary);
    std::cout << "Terrain elevation checks passed; numeric map load "
        << std::chrono::duration<double, std::milli>(loaded - start).count() << " ms\n";
}
