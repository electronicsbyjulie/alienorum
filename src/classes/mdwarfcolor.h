#ifndef ALIENORUM_MDWARFCOLOR_H
#define ALIENORUM_MDWARFCOLOR_H

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "star.h"
#include "wdcolor.h"

namespace alienorum
{
    inline bool is_m_dwarf_color_source(const Star* star)
    {
        if (!star || star->spectral_type[0] != 'M') return false;
        if (std::strchr(star->spectral_type, 'I') || std::strchr(star->spectral_type, '+')) return false;
        if (star->is_white_dwarf() || star->is_mira_variable()) return false;
        if (star->mass > 0 && star->mass < .075*solar_mass) return false;
        if (star->volumetric_mean_radius > 2*solar_radius) return false;
        if (star->absolute_magnitude != 0 && star->absolute_magnitude < 4) return false;
        return true;
    }

    inline double m_dwarf_temperature(const Star* star)
    {
        if (std::isfinite(star->temperature) && star->temperature > 0) return star->temperature;
        // The same M0--M9 calibration as catalogs/mainseq.dat, independent of B-V.
        constexpr double temperatures[] = {3850, 3660, 3560, 3430, 3210, 3060, 2810, 2680, 2570, 2380};
        char* end = nullptr;
        double subtype = std::strtod(star->spectral_type+1, &end);
        if (end == star->spectral_type+1 || !std::isfinite(subtype)) subtype = 5;
        subtype = std::clamp(subtype, 0.0, 9.0);
        const int lower = static_cast<int>(subtype), upper = std::min(lower+1, 9);
        return temperatures[lower] + (subtype-lower)*(temperatures[upper]-temperatures[lower]);
    }

    // Cranmer (2021), doi:10.3847/2515-5172/ac225c; doi:10.5281/zenodo.5293307.
    // PHOENIX solar-composition spectra at log g=5; CC BY 4.0.
    // Attribution and display assumptions: catalogs/m_dwarf_colors.txt.
    struct MColorNode
    {
        double temperature, x, y;
    };
    inline constexpr MColorNode m_color_grid[] =
    {
        {2300, 0.4625470, 0.4135083},
        {2400, 0.4498565, 0.4083114},
        {2500, 0.4390401, 0.4070086},
        {2600, 0.4200607, 0.4043304},
        {2700, 0.4107726, 0.4030941},
        {2800, 0.4112965, 0.4035113},
        {2900, 0.4120717, 0.4028490},
        {3000, 0.4118083, 0.4016770},
        {3100, 0.4114705, 0.4003671},
        {3200, 0.4102969, 0.3987167},
        {3300, 0.4088451, 0.3969581},
        {3400, 0.4075079, 0.3952689},
        {3500, 0.4065383, 0.3936974},
        {3600, 0.4058216, 0.3922326},
        {3700, 0.4049769, 0.3908096},
        {3800, 0.4038850, 0.3893831},
        {3900, 0.4019387, 0.3875762},
    };

    inline Color m_dwarf_linear_color(double temperature)
    {
        if (!std::isfinite(temperature) || temperature <= 0) temperature = 3060;
        const auto first = std::begin(m_color_grid), last = std::end(m_color_grid);
        temperature = std::clamp(temperature, first->temperature, (last-1)->temperature);
        const auto upper = std::lower_bound(first, last, temperature,
            [](const MColorNode& node, double value) { return node.temperature < value; });
        double x = upper->x, y = upper->y;
        if (upper != first)
        {
            const auto lower = upper-1;
            const double mix = (temperature-lower->temperature)/(upper->temperature-lower->temperature);
            x = lower->x + mix*(upper->x-lower->x);
            y = lower->y + mix*(upper->y-lower->y);
        }
        const double X = x/y, Z = (1-x-y)/y;
        Color color(3.2406*X-1.5372-.4986*Z, -.9689*X+1.8758+.0415*Z, .0557*X-.2040+1.0570*Z);
        const double white = -std::min({0.0, color.red, color.green, color.blue});
        color.red += white; color.green += white; color.blue += white;
        color.normalize(1);
        return color;
    }

    inline bool m_dwarf_color_source(Star* star, Color& linear)
    {
        if (!is_m_dwarf_color_source(star)) return false;
        linear = m_dwarf_linear_color(m_dwarf_temperature(star));
        return true;
    }
}

#endif
