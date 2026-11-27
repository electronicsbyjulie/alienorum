#pragma once

#include <cmath>
#include "misc.h"

namespace alienorum
{
    struct DisplayUnit
    {
        double base_per_unit;
        const char* symbol;

        double from_base(double value) const { return value / base_per_unit; }
        double to_base(double value) const { return value * base_per_unit; }
    };

    inline constexpr DisplayUnit metres{1, "m"}, kilometres{1000, "km"};
    inline constexpr DisplayUnit feet{METRES_PER_FOOT, "ft"}, miles{METRES_PER_MILE, "mi"};
    inline constexpr DisplayUnit kilograms{1000, "kg"}, pounds{GRAMS_PER_POUND, "lb"}, short_tons{GRAMS_PER_SHORT_TON, "ton"};
    inline constexpr DisplayUnit metres_per_second{1, "m/s"};
    inline constexpr DisplayUnit kilometres_per_hour{1000.0 / 3600, "km/h"};
    inline constexpr DisplayUnit miles_per_hour{METRES_PER_SECOND_PER_MPH, "mph"};

    inline DisplayUnit length_display_unit(double metres_value, DisplayUnit metric)
    {
        if (!use_imperial_units) return metric;
        return std::abs(metres_value) >= miles.base_per_unit ? miles : feet;
    }

    inline DisplayUnit mass_display_unit(double grams)
    {
        if (!use_imperial_units) return kilograms;
        return grams >= short_tons.base_per_unit ? short_tons : pounds;
    }

    inline double local_temperature_display(double kelvin)
    {
        const double celsius = kelvin - water_freezing;
        return use_imperial_units ? celsius * FAHRENHEIT_PER_CELSIUS_DEGREE + WATER_FREEZING_FAHRENHEIT : celsius;
    }
}
