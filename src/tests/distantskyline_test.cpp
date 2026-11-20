// Standalone regression executable for the cached distant elevation sampler.
#include "../classes/distantskyline.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main()
{
    using alienorum::DistantSkyline;
    constexpr double radius = 3390000;
    constexpr double eye = 2;
    constexpr double pi = 3.14159265358979323846;
    DistantSkyline skyline;
    int calls = 0;
    // Align the summit with a radial sample for the analytic angle comparison.
    const auto mountain = [&](double latitude, double longitude)
    {
        ++calls;
        const double distance = radius * hypot(latitude - 322050 / radius, longitude);
        return 22000 * std::max(0.0, 1 - distance / 80000);
    };
    const auto start = std::chrono::steady_clock::now();
    skyline.update(&calls, 1, 4, 0, 0, radius, eye, .5 / radius, mountain);
    const auto finish = std::chrono::steady_clock::now();
    // The summit lies beyond both previous terrain range limits.
    const double expected = atan2((radius + 22000) * cos(322050 / radius) - radius - eye,
        (radius + 22000) * sin(322050 / radius));
    assert(skyline.angle(0) > .01);
    assert(std::abs(skyline.angle(0) - expected) < .001);
    assert(skyline.angle(pi) < 0);
    assert(std::abs(skyline.angle(0) - skyline.angle(2 * pi)) < 1e-12);
    const int sampled = calls;
    skyline.update(&calls, 1, 4, 0, 0, radius, eye, .5 / radius, mountain);
    assert(calls == sampled);
    skyline.update(&calls, 2, 4, 0, 0, radius, eye, .5 / radius, mountain);
    assert(calls > sampled);
    const int revised = calls;
    skyline.update(&calls, 2, 4, .01, 0, radius, eye, .5 / radius, mountain);
    assert(calls > revised);
    auto verify_refresh = [&](const void* map, int world, double latitude, double longitude,
        double world_radius, double height, double curvature)
    {
        const int before = calls;
        skyline.update(map, 2, world, latitude, longitude, world_radius, height, curvature, mountain);
        assert(calls > before);
    };
    verify_refresh(&calls, 4, .01, .01, radius, eye, .5 / radius);
    verify_refresh(&calls, 4, .01, .01, radius, 10, .5 / radius);
    verify_refresh(&calls, 4, .01, .01, radius, 10, .4 / radius);
    verify_refresh(&calls, 5, .01, .01, radius, 10, .4 / radius);
    verify_refresh(nullptr, 5, .01, .01, radius, 10, .4 / radius);
    verify_refresh(nullptr, 5, .01, .01, radius + 1000, 10, .4 / radius);
    const auto hidden_mountain = [](double latitude, double longitude)
    {
        const double distance = radius * hypot(latitude - 400000 / radius, longitude);
        return 22000 * std::max(0.0, 1 - distance / 80000);
    };
    skyline.update(nullptr, 3, 4, 0, 0, radius, eye, .5 / radius, hidden_mountain);
    assert(skyline.angle(0) < 0); // Curvature hides this summit at 400 km.
    const auto flat = [](double, double) { return 0.0; };
    skyline.update(nullptr, 1, 4, 0, 0, radius, eye, .5 / radius, flat);
    assert(skyline.angle(0) < 0);
    assert(skyline.angle(0) == skyline.angle(pi));
    std::cout << "Distant skyline checks passed; cold sampling: "
        << std::chrono::duration<double, std::milli>(finish - start).count() << " ms\n";
}
