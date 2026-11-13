#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include "noise.h"

namespace alienorum
{
    enum class GiantCloudStyle { banded, hazy, clear, synchronous };

    // A static cloud albedo field. Vortices advect the same bands and fine tracers,
    // rather than painting isolated swirls over an unrelated background.
    class GiantClouds
    {
        static constexpr double pi = 3.14159265358979323846;
        struct Vortex
        {
            double longitude, latitude, width, height, twist;
            bool bright;
        };
        PerlinNoise noise;
        std::array<double, 3> base;
        std::vector<Vortex> vortices;
        double frequency, phase, strength;
        double palette_phase = 0;
        GiantCloudStyle style;

        static double smooth(double low, double high, double value)
        {
            const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
            return t * t * (3 - 2 * t);
        }

        double fractal(double x, double y, double z, int octaves, double footprint)
        {
            double result = 0, weight = .5, total = 0;
            for (int octave = 0; octave < octaves; ++octave)
            {
                const double filter = 1 - smooth(.18, .65, footprint);
                result += filter * weight * noise.noise(x, y, z);
                total += weight;
                x = x * 2.03 + 7.1;
                y = y * 2.03 + 13.7;
                z = z * 2.03 - 9.2;
                weight *= .52;
                footprint *= 2.03;
            }
            return result / total;
        }

    public:
        GiantClouds(uint32_t seed, std::array<double, 3> color, int bands, double contrast,
            GiantCloudStyle mode, bool major, double storm_latitude = -.3, double storm_longitude = pi)
            : noise(seed ^ 0x434c4f55u), base(color), frequency(std::max(3, bands) * 1.3),
              phase(0), strength(std::clamp(contrast * 2.4, 0.0, 1.0)), style(mode)
        {
            std::mt19937 random(seed ^ 0x766f7274u);
            const auto draw = [&](double low, double high)
            {
                return low + (high - low) * (random() / 4294967296.0);
            };
            phase = draw(-.3, .3);
            if (mode == GiantCloudStyle::clear) return;
            // The supplied B-V conversion owns the palette; the seed only varies
            // cloud structure and brightness within that color.
            palette_phase = draw(-pi, pi);
            if (mode == GiantCloudStyle::synchronous) frequency *= .45;
            if (major && mode != GiantCloudStyle::synchronous)
            {
                const double height = draw(.075, .12);
                vortices.push_back({storm_longitude, storm_latitude, height * draw(2.7, 3.8),
                    height, std::copysign(draw(3.5, 5), storm_latitude), false});
            }
            const int count = mode == GiantCloudStyle::hazy ? 7 : 38;
            for (int index = 0; index < count; ++index)
            {
                const double latitude = draw(-1.14, 1.14);
                const double height = draw(.014, .047);
                vortices.push_back({draw(-pi, pi), latitude, height * draw(1.4, 3.1), height,
                    std::copysign(draw(1.7, 3.5), latitude), draw(0, 1) < .4});
            }
        }

        std::array<unsigned char, 3> sample(double longitude, double latitude, double angular_pixel)
        {
            if (style == GiantCloudStyle::clear)
                return {static_cast<unsigned char>(base[0]), static_cast<unsigned char>(base[1]), static_cast<unsigned char>(base[2])};
            longitude = std::remainder(longitude, 2 * pi);
            if (std::abs(latitude) > pi * .5 - 1e-10) longitude = 0;
            const double original_latitude = latitude;
            double storm_mask = 0, storm_radius = 0;
            double storm_x = 0, storm_y = 0, storm_height = 1;
            bool bright_storm = false;
            double stretch = 1;
            for (const auto& vortex : vortices)
            {
                const double dy = (latitude - vortex.latitude) / vortex.height;
                if (std::abs(dy) >= 2.4) continue;
                const double cosine = std::cos(vortex.latitude);
                const double dx = std::remainder(longitude - vortex.longitude, 2 * pi) * cosine / vortex.width;
                const double radius2 = dx * dx + dy * dy;
                if (radius2 >= 2.4 * 2.4) continue;
                const double envelope = std::pow(1 - radius2 / (2.4 * 2.4), 3);
                const double bend = noise.noise(dx * 1.7 + 31, dy * 1.7 + 4, vortex.latitude * 3 + phase);
                const double angle = vortex.twist * envelope * (1 + .23 * bend);
                const double strain = 1 + 6 * std::abs(vortex.twist) * radius2 / (2.4 * 2.4)
                    * std::pow(1 - radius2 / (2.4 * 2.4), 2);
                stretch = std::max(stretch, strain);
                const double c = std::cos(angle), s = std::sin(angle);
                longitude += ((dx * c - dy * s) - dx) * vortex.width / cosine;
                latitude += ((dx * s + dy * c) - dy) * vortex.height;
                const double radius = std::sqrt(radius2);
                const double mask = 1 - smooth(.7, 2.1, radius + .18 * bend);
                if (mask > storm_mask)
                {
                    storm_mask = mask;
                    storm_radius = radius;
                    storm_x = dx * c - dy * s;
                    storm_y = dx * s + dy * c;
                    storm_height = vortex.height;
                    bright_storm = vortex.bright;
                }
            }
            const double ring = std::cos(latitude);
            double x = ring * std::cos(longitude), y = ring * std::sin(longitude), z = std::sin(latitude);
            // Stellar locking adds broad day/night structure without rotating the
            // zonal jets away from the equator or separating storms from their belts.
            const double irradiation = style == GiantCloudStyle::synchronous ? x * .65 : 0;
            const double broad = noise.noise(x * 3.2 + 17, y * 3.2 - 9, z * 3.2 + 4);
            const double cross = noise.noise(x * 4.7 - 3, y * 4.7 + 21, z * 4.7 + 11);
            // Latitude-dependent shear elongates tracers along the zonal jets.
            const double shear = .12 * std::sin(z * frequency + phase);
            const double c = std::cos(shear), s = std::sin(shear);
            const double qx = (x * c - y * s) * 12 + broad * 1.8;
            const double qy = (x * s + y * c) * 12 + cross * 1.8;
            const double qz = z * 12 + broad * 1.2;
            const double footprint = angular_pixel * 12 * (1 + .12 * frequency) * stretch;
            const double folds = fractal(qx, qy, qz, 3, footprint);
            const double threads = fractal(qx * 2.2 + folds * 2.8,
                qy * 2.2 + broad * 2.8, qz * 2.2 + cross * 2.8, 3, footprint * 2.2);
            const double fine_weight = 1 - smooth(.18, .65, footprint * 4);
            const double grain = noise.noise(qx * 7, qy * 7, qz * 7) * fine_weight;
            const double band_phase = z * frequency + phase + broad * 1.4 + folds * 1.2 + irradiation;
            const double belt = smooth(.18, .8, .5 - .5 * std::cos(band_phase));
            const double boundary = 4 * belt * (1 - belt);
            const double filament = std::sin(folds * 26 + threads * 11 + z * frequency * 3);
            const double billow = smooth(-.18, .35, folds + threads * .7);
            const double condensate = std::clamp((1 - belt) * .85 + billow * .3
                + threads * .3, 0.0, 1.0);
            const double detail = folds * .23 + threads * .11 + grain * .018
                + filament * (.006 + .018 * boundary) * fine_weight;
            const double polar = smooth(.85, 1.5, std::abs(original_latitude));
            const double weather = strength * (style == GiantCloudStyle::hazy ? .45 : 1);
            const double storm_cloud = storm_mask > 0
                ? fractal(storm_x * 2.3 + 9, storm_y * 2.3 - 2, phase + 17, 3,
                    angular_pixel / storm_height) : 0;
            const double storm_tracer = std::sin(storm_x * 2.1 + storm_y * 1.5 + storm_cloud * 3);
            const double clearing = smooth(.28, .55,
                fractal(qx * .65 - 11, qy * .65 + 31, qz * .65 + 5, 2, footprint * .65)) * belt;
            const double tint = std::sin(band_phase * .43 + palette_phase);
            const double light = 1.12, dark = .78;
            double brightness = dark + (light - dark) * condensate + .7 * detail + .025 * tint;
            const double interior = bright_storm ? .75 + .7 * storm_cloud + .06 * storm_tracer
                : .4 + 1.2 * storm_cloud + .06 * storm_tracer;
            const double rim = std::exp(-std::pow((storm_radius - 1.4) / .2, 2));
            const double storm = dark + (light - dark)
                * std::clamp(interior + .65 * rim * std::max(0.0, .4 + storm_cloud), 0.0, 1.0);
            brightness += (storm - brightness) * storm_mask * .75;
            brightness += (.82 - brightness) * clearing * .65;
            brightness = brightness * (1 - .55 * polar) + .94 * .55 * polar;
            brightness = 1 + (brightness - 1) * weather;
            // One common scale preserves channel ratios, including in highlights.
            const double peak = std::max({base[0], base[1], base[2], 1.0});
            brightness = std::clamp(brightness, 0.0, 255.0 / peak);
            std::array<unsigned char, 3> result{};
            for (int channel = 0; channel < 3; ++channel)
            {
                result[channel] = static_cast<unsigned char>(std::clamp(std::lround(base[channel] * brightness), 0l, 255l));
            }
            return result;
        }
    };
}
