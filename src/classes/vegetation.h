#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
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
