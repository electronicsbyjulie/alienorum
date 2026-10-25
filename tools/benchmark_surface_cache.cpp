// Standalone CPU benchmark: compile with src/classes/noise.cpp and -O2.
#include "../src/classes/surfacecache.h"
#include "../src/classes/surfacescatter.h"
#include "../src/classes/noise.h"
#include <chrono>
#include <iostream>
#include <vector>

using namespace alienorum;
using Clock = std::chrono::steady_clock;
static volatile double checksum = 0;
static constexpr double radius = 6371000, pi = 3.141592653589793;
static constexpr int frames = 12;

static double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static std::vector<std::array<double, 3>> positions(int rows, int columns, double walk)
{
    std::vector<std::array<double, 3>> result;
    const double latitude = .45 + walk / radius, longitude = .2;
    for (int row = 0; row < rows; ++row)
    {
        const double distance = rows == 1 ? 9500 : 150 * pow(60000.0 / 150, double(row) / (rows - 1));
        const double arc = distance / radius;
        for (int column = 0; column < columns; ++column)
        {
            const double bearing = 2 * pi * column / columns;
            const double lat = asin(sin(latitude) * cos(arc) + cos(latitude) * sin(arc) * cos(bearing));
            const double lon = longitude + atan2(sin(bearing) * sin(arc) * cos(latitude),
                cos(arc) - sin(latitude) * sin(lat));
            result.push_back({cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat)});
        }
    }
    return result;
}

static void noise_benchmark(int octaves, int rows, int columns, double spacing)
{
    PerlinNoise noise(42);
    SurfaceFieldCache cache;
    cache.configure(42, radius, spacing);
    const auto sample = [&](double x, double y, double z)
    {
        double total = 0, weight = 0, amplitude = 1, frequency = 1;
        x *= radius / 50000;
        y *= radius / 50000;
        z *= radius / 50000;
        for (int octave = 0; octave < octaves; ++octave)
        {
            total += noise.noise(x * frequency, y * frequency, z * frequency) * amplitude;
            weight += amplitude;
            amplitude *= .8559;
            frequency *= 1.29;
        }
        return total / weight;
    };
    auto start = Clock::now();
    for (int frame = 1; frame <= frames; ++frame)
    {
        for (const auto &point : positions(rows, columns, frame * .25))
        {
            checksum += sample(point[0], point[1], point[2]);
        }
    }
    const double direct_ms = elapsed(start) / frames;
    start = Clock::now();
    for (const auto &point : positions(rows, columns, 0))
    {
        checksum += cache.value(point[0], point[1], point[2], sample);
    }
    const double cold_ms = elapsed(start);
    start = Clock::now();
    for (int frame = 1; frame <= frames; ++frame)
    {
        for (const auto &point : positions(rows, columns, frame * .25))
        {
            checksum += cache.value(point[0], point[1], point[2], sample);
        }
    }
    const double cached_ms = elapsed(start) / frames;
    std::cout << (rows == 1 ? "skyline" : "terrain") << ',' << direct_ms << ',' << cached_ms
        << ',' << cold_ms << ',' << direct_ms / cached_ms << '\n';
}

static void scenery_benchmark()
{
    const double spacing = sqrt(pi * 10000 / 256);
    const auto clearance = [&](const std::vector<SurfaceScenery> &objects)
    {
        std::vector<double> radii;
        for (const auto &object : objects) radii.push_back(scenery_size(object.seed, 1, 4));
        return scenery_clearance(objects, radii, radius);
    };
    auto start = Clock::now();
    for (int frame = 1; frame <= frames; ++frame)
    {
        const auto objects = surface_scenery(radius, .45 + frame * .25 / radius, .2, 1010, spacing, 42);
        const auto retained = clearance(objects);
        checksum += retained.size();
    }
    const double direct_ms = elapsed(start) / frames;
    SurfaceSceneryCache cache;
    start = Clock::now();
    const auto retained = clearance(cache.update(radius, .45, .2, 1010, spacing, 42));
    const double cold_ms = elapsed(start);
    start = Clock::now();
    for (int frame = 1; frame <= frames; ++frame)
    {
        const auto &objects = cache.update(radius, .45 + frame * .25 / radius, .2, 1010, spacing, 42);
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (retained[i] && hypot(objects[i].east, objects[i].north) < 1000) checksum += 1;
        }
    }
    const double cached_ms = elapsed(start) / frames;
    std::cout << "scenery," << direct_ms << ',' << cached_ms << ',' << cold_ms << ',' << direct_ms / cached_ms << '\n';
}

int main()
{
    std::cout << "stage,direct_ms_per_step,cached_ms_per_step,cold_ms,speedup\n";
    noise_benchmark(12, 40, 512, 128);
    noise_benchmark(29, 1, 8192, 8);
    scenery_benchmark();
    return !std::isfinite(checksum);
}
