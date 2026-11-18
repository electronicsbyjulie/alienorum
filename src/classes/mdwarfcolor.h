#ifndef ALIENORUM_MDWARFCOLOR_H
#define ALIENORUM_MDWARFCOLOR_H

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "star.h"
#include "wdcolor.h"
#include "neighborhoodfilter.h"

namespace alienorum
{
    inline bool is_m_dwarf_color_source(const Star* star)
    {
        if (!star || !std::strchr(star->spectral_type, 'M')) return false;
        const auto spectrum = neighborhood_spectrum(star->spectral_type);
        if (spectrum.type != 'M' || std::strchr(star->spectral_type, '+')) return false;
        const auto lum = spectrum.luminosity;
        if (lum != NeighborhoodLuminosity::unknown && lum != NeighborhoodLuminosity::v) return false;
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
        double subtype = neighborhood_spectrum(star->spectral_type).subtype;
        if (!std::isfinite(subtype) || subtype < 0) subtype = 5;
        subtype = std::clamp(subtype, 0.0, 9.0);
        const int lower = static_cast<int>(subtype), upper = std::min(lower+1, 9);
        return temperatures[lower] + (subtype-lower)*(temperatures[upper]-temperatures[lower]);
    }

    // Cranmer (2021), doi:10.3847/2515-5172/ac225c, data:10.5281/zenodo.5293307.
    // PHOENIX reference chromaticities, CC BY 4.0. See catalogs/m_dwarf_colors.txt.
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
        {4000, 0.3986014, 0.3845491},
        {4100, 0.3947896, 0.3817957},
        {4200, 0.3904634, 0.3790313},
        {4300, 0.3853910, 0.3758779},
        {4400, 0.3796583, 0.3726156},
        {4500, 0.3739623, 0.3696843},
        {4600, 0.3684950, 0.3669742},
        {4700, 0.3633182, 0.3644206},
        {4800, 0.3587166, 0.3621944},
        {4900, 0.3541675, 0.3598210},
        {5000, 0.3500689, 0.3575723},
        {5100, 0.3459676, 0.3549858},
        {5200, 0.3422978, 0.3524955},
        {5300, 0.3388325, 0.3499103},
        {5400, 0.3357514, 0.3474697},
        {5500, 0.3327172, 0.3448626},
        {5600, 0.3296833, 0.3421099},
        {5700, 0.3266999, 0.3392684},
        {5800, 0.3243544, 0.3369843},
        {5900, 0.3221736, 0.3347916},
        {6000, 0.3192532, 0.3317525},
        {6100, 0.3168432, 0.3291862},
        {6200, 0.3144155, 0.3265558},
        {6300, 0.3123431, 0.3242761},
        {6400, 0.3099165, 0.3215709},
        {6500, 0.3060550, 0.3172400},
        {6600, 0.3039277, 0.3148275},
        {6700, 0.3020032, 0.3126417},
        {6800, 0.3000452, 0.3104454},
        {6900, 0.2981600, 0.3083409},
        {7000, 0.2963399, 0.3063169},
        {7200, 0.2926373, 0.3022120},
        {7400, 0.2892654, 0.2984936},
        {7600, 0.2873951, 0.2964529},
        {7800, 0.2833044, 0.2918525},
        {8000, 0.2799840, 0.2878331},
        {8200, 0.2768877, 0.2839136},
        {8400, 0.2741197, 0.2804146},
        {8600, 0.2727582, 0.2788454},
        {8800, 0.2709677, 0.2765606},
        {9000, 0.2693804, 0.2744845},
        {9200, 0.2680242, 0.2726733},
        {9400, 0.2668333, 0.2710550},
        {9600, 0.2657816, 0.2696047},
        {9800, 0.2648790, 0.2683446},
        {10000, 0.2640851, 0.2672248},
        {10200, 0.2633916, 0.2662342},
        {10400, 0.2627860, 0.2653590},
        {10600, 0.2622544, 0.2645810},
        {10800, 0.2617890, 0.2638901},
        {11000, 0.2613798, 0.2632734},
        {11200, 0.2610172, 0.2627185},
        {11400, 0.2606939, 0.2622158},
        {11600, 0.2605527, 0.2619380},
        {11800, 0.2602783, 0.2615039},
        {12000, 0.2600213, 0.2610963},
        {12300, 0.2433969, 0.2386147},
        {15700, 0.2389688, 0.2318007},
        {20600, 0.2350278, 0.2240722},
        {31400, 0.2290561, 0.2133312},
        {41400, 0.2209608, 0.1974685},
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

    inline bool is_m_color_source(const Star* star)
    {
        return star && std::strchr(star->spectral_type, 'M') && !star->is_white_dwarf()
            && !(star->mass > 0 && star->mass < .075*solar_mass)
            && neighborhood_spectrum(star->spectral_type).type == 'M'
            && !std::strchr(star->spectral_type, '+');
    }

    inline bool m_dwarf_color_source(Star* star, Color& linear)
    {
        if (!is_m_color_source(star)) return false;
        linear = m_dwarf_linear_color(m_dwarf_temperature(star));
        return true;
    }
    inline bool stellar_reference_color(Star* star, Color& linear)
    {
        if (!star || star->is_white_dwarf()) return false;
        const bool m = is_m_color_source(star);
        double temperature = m ? m_dwarf_temperature(star) : star->estimate_temperature();
        if (!(star->temperature > 0) && !star->spectral_type[0])
            temperature = Star::interpolate_mseq_temp(Star::get_mseqidx_from_BV(star->BV_color));
        if (!(temperature > 0) || !std::isfinite(temperature)) return false;
        linear = m_dwarf_linear_color(temperature);
        return true;
    }

}

#endif
