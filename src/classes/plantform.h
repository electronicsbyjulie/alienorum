#pragma once

#include <algorithm>
#include <cmath>

namespace alienorum
{
    // Bounded visual scenarios, normalized at terrestrial pressure and gravity.
    inline double plant_pressure_scale(double pressure_atm)
    {
        if (!std::isfinite(pressure_atm)) return 1;
        return std::clamp(std::log1p(std::max(0.0, pressure_atm)) / std::log(2.0), .25, 4.0);
    }

    inline double plant_gravity_scale(double relative_gravity)
    {
        return std::isfinite(relative_gravity) ? std::clamp(relative_gravity, .1, 10.0) : 1;
    }
}
