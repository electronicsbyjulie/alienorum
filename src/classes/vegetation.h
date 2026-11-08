#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>
#include "climate.h"

// Adjustable, speculative limits for active water-based organic ecosystems, in kelvin.
// These are simulation assumptions, not measured universal biochemical limits.
// 223.15 K (-50 C) is an optimistic allowance for cryoprotective aqueous mixtures,
// beyond observed growth near 253 K: doi:10.1371/journal.pone.0066207.
// 453.15 K (+180 C) uses the generous end of estimates for aqueous metabolite
// stability: doi:10.3390/life5021054. A new replicator alone
// does not protect membranes, catalysts, or metabolites against decomposition.
#define ORGANIC_LIFE_MIN_TEMP_K 223.15
#define ORGANIC_LIFE_MAX_TEMP_K 453.15

namespace alienorum
{
    inline bool vegetation_temperature_suitable(double temperature, double minimum, double maximum)
    {
        return minimum < maximum && temperature >= minimum && temperature <= maximum
            && temperature >= ORGANIC_LIFE_MIN_TEMP_K && temperature <= ORGANIC_LIFE_MAX_TEMP_K;
    }

    class VegetationHabitat
    {
        static constexpr size_t rows = 33;
        size_t columns = 1, count = 0;
        std::vector<double> temperatures;
        LocalClimate baseline;

    public:
        explicit VegetationHabitat(const std::vector<LocalClimate>& seasons)
        {
            if (seasons.empty()) return;
            baseline = seasons.front();
            columns = baseline.stellar_locked ? 16 : 1;
            const size_t hours = baseline.stellar_locked ? 1 : 8;
            count = seasons.size() * hours;
            temperatures.resize(count * rows * columns);
            for (size_t season = 0; season < seasons.size(); ++season)
                for (size_t hour = 0; hour < hours; ++hour)
                {
                    LocalClimate climate = seasons[season];
                    climate.regional_k = climate.lapse_k_m = 0;
                    climate.ice_season.reset();
                    const double angle = hour * 6.283185307179586 / hours;
                    const double sx = climate.sun_x, sy = climate.sun_y;
                    climate.sun_x = sx * std::cos(angle) - sy * std::sin(angle);
                    climate.sun_y = sx * std::sin(angle) + sy * std::cos(angle);
                    for (size_t row = 0; row < rows; ++row)
                        for (size_t column = 0; column < columns; ++column)
                        {
                            const double z = -1 + 2.0 * row / (rows - 1), ring = std::sqrt(std::max(0.0, 1 - z*z));
                            const double lon = column * 6.283185307179586 / columns;
                            temperatures[((season * hours + hour) * rows + row) * columns + column]
                                = climate.temperature(-ring*std::cos(lon), -ring*std::sin(lon), z, climate.sea_level_m, false);
                        }
                }
        }

        bool suitable(double x, double y, double z, double elevation, double minimum, double maximum,
            double freezing, double boiling) const
        {
            if (!count || !(minimum < maximum)) return false;
            const double low = std::max({minimum, freezing, double(ORGANIC_LIFE_MIN_TEMP_K)});
            const double high = std::min({maximum, boiling, double(ORGANIC_LIFE_MAX_TEMP_K)});
            if (!(low < high)) return false;
            const double latitude = std::clamp((z + 1) * .5 * (rows - 1), 0.0, double(rows - 1));
            const size_t south = static_cast<size_t>(latitude), north = std::min(south + 1, rows - 1);
            double longitude = std::atan2(-y, -x) / 6.283185307179586;
            longitude = (longitude - std::floor(longitude)) * columns;
            const size_t west = static_cast<size_t>(longitude), east = (west + 1) % columns;
            const double offset = baseline.regional_temperature(x, y, z)
                - baseline.lapse_k_m * std::max(0.0, elevation - baseline.sea_level_m);
            size_t growing = 0;
            // Scenario rule: at least one tenth of the sampled year/day cycle supports growth.
            for (size_t sample = 0; sample < count; ++sample)
            {
                const auto value = [&](size_t row)
                {
                    const size_t base = (sample * rows + row) * columns;
                    return temperatures[base + west] + (temperatures[base + east] - temperatures[base + west]) * (longitude - west);
                };
                const double temperature = value(south) + (value(north) - value(south)) * (latitude - south) + offset;
                if (temperature >= low && temperature < high && ++growing * 10 >= count) return true;
            }
            return false;
        }
    };

    inline std::pair<double, double> vegetation_temperature_range(std::mt19937& random,
        const LocalClimate& climate, double highland_m)
    {
        double cold = climate.mean_k, hot = climate.mean_k;
        // Sample the same regional/day-night climate used by surface generation.
        // Include representative uplands, rather than fitting to a single mean.
        for (int row = 0; row <= 12; ++row)
        {
            const double z = -1.0 + row / 6.0;
            const double radial = std::sqrt(std::max(0.0, 1.0 - z * z));
            for (int column = 0; column < 24; ++column)
            {
                const double angle = column * 6.283185307179586 / 24;
                for (double elevation : {climate.sea_level_m, std::max(climate.sea_level_m, highland_m)})
                {
                    const double temperature = climate.temperature(radial * std::cos(angle),
                        radial * std::sin(angle), z, elevation);
                    if (!std::isfinite(temperature)) continue;
                    cold = std::min(cold, temperature);
                    hot = std::max(hot, temperature);
                }
            }
        }
        if (!std::isfinite(cold) || !std::isfinite(hot)) cold = hot = 294;
        std::uniform_real_distribution<double> unit(0, 1);
        const double adaptation = .55 + .30 * unit(random);
        const double center = 294 + adaptation * ((cold + hot) * .5 - 294)
            + (unit(random) * 16 - 8);
        // Partial adaptation leaves cold and hot regions unsuitable; biospheres
        // vary in breadth and can entirely miss an extreme planet's climate.
        const double half_width = std::clamp((16 + adaptation * ((hot - cold) * .5 - 16))
            * (.65 + .45 * unit(random)), 12.0, 60.0);
        return
        {
            std::clamp(center - half_width, ORGANIC_LIFE_MIN_TEMP_K, ORGANIC_LIFE_MAX_TEMP_K),
            std::clamp(center + half_width, ORGANIC_LIFE_MIN_TEMP_K, ORGANIC_LIFE_MAX_TEMP_K)
        };
    }
}
