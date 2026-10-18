#ifndef ALIENORUM_WDCOLOR_H
#define ALIENORUM_WDCOLOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <fstream>
#include <sstream>
#include <map>
#include "color.h"

namespace alienorum
{
    enum class WdAtmosphere
    {
        Hydrogen,
        Helium
    };

    // Hollands et al. (2026), Tables C1 and C2, log g = 8.
    // https://doi.org/10.1093/rasti/rzag069 (CC BY 4.0).
    // Published display hex values retain more precision than the rounded x/y columns.
    struct WdAtmosphereNode
    {
        double temperature;
        uint32_t srgb;
        constexpr WdAtmosphereNode(double t, uint32_t rgb) : temperature(t), srgb(rgb)
        {
        }
    };

    inline constexpr WdAtmosphereNode wd_hydrogen_track[] =
    {
        WdAtmosphereNode(3000, 0xFFBF68),
        WdAtmosphereNode(3250, 0xFFC26F),
        WdAtmosphereNode(3500, 0xFFC677),
        WdAtmosphereNode(3750, 0xFFCA80),
        WdAtmosphereNode(4000, 0xFFCF8B),
        WdAtmosphereNode(4250, 0xFFD497),
        WdAtmosphereNode(4500, 0xFFDAA5),
        WdAtmosphereNode(4750, 0xFFE0B4),
        WdAtmosphereNode(5000, 0xFFE6C3),
        WdAtmosphereNode(5250, 0xFFEBD4),
        WdAtmosphereNode(5500, 0xFFF0E3),
        WdAtmosphereNode(6000, 0xFFF7FA),
        WdAtmosphereNode(6500, 0xF5F2FF),
        WdAtmosphereNode(7000, 0xEAECFF),
        WdAtmosphereNode(7500, 0xE1E7FF),
        WdAtmosphereNode(8000, 0xDBE3FF),
        WdAtmosphereNode(8500, 0xD5E0FF),
        WdAtmosphereNode(9000, 0xD0DEFF),
        WdAtmosphereNode(9500, 0xCCDCFF),
        WdAtmosphereNode(10000, 0xC9DAFF),
        WdAtmosphereNode(10500, 0xC5D9FF),
        WdAtmosphereNode(11000, 0xC2D8FF),
        WdAtmosphereNode(11500, 0xBFD6FF),
        WdAtmosphereNode(12000, 0xBCD5FF),
        WdAtmosphereNode(12500, 0xBAD4FF),
        WdAtmosphereNode(13000, 0xB8D2FF),
        WdAtmosphereNode(13500, 0xB6D1FF),
        WdAtmosphereNode(14000, 0xB4D0FF),
        WdAtmosphereNode(14500, 0xB3CEFF),
        WdAtmosphereNode(15000, 0xB1CDFF),
        WdAtmosphereNode(15500, 0xB0CCFF),
        WdAtmosphereNode(16000, 0xAFCBFF),
        WdAtmosphereNode(16500, 0xAECAFF),
        WdAtmosphereNode(17000, 0xADC9FF),
        WdAtmosphereNode(20000, 0xA8C5FF),
        WdAtmosphereNode(25000, 0xA3C0FF),
        WdAtmosphereNode(30000, 0x9FBCFF),
        WdAtmosphereNode(35000, 0x9DB9FF),
        WdAtmosphereNode(40000, 0x9CB8FF),
        WdAtmosphereNode(45000, 0x9BB7FF),
        WdAtmosphereNode(50000, 0x9BB7FF),
        WdAtmosphereNode(55000, 0x9AB6FF),
        WdAtmosphereNode(60000, 0x9AB6FF),
        WdAtmosphereNode(65000, 0x9AB6FF),
        WdAtmosphereNode(70000, 0x9AB5FF),
        WdAtmosphereNode(75000, 0x99B5FF),
        WdAtmosphereNode(80000, 0x99B5FF),
        WdAtmosphereNode(85000, 0x99B5FF),
        WdAtmosphereNode(90000, 0x99B5FF),
        WdAtmosphereNode(100000, 0x98B4FF),
        WdAtmosphereNode(110000, 0x98B4FF),
        WdAtmosphereNode(120000, 0x98B4FF),
        WdAtmosphereNode(130000, 0x98B4FF),
        WdAtmosphereNode(140000, 0x98B3FF),
    };

    inline constexpr WdAtmosphereNode wd_helium_track[] =
    {
        WdAtmosphereNode(3000, 0xFF9E3B),
        WdAtmosphereNode(3250, 0xFFA84C),
        WdAtmosphereNode(3500, 0xFFB462),
        WdAtmosphereNode(3750, 0xFFBF76),
        WdAtmosphereNode(4000, 0xFFC989),
        WdAtmosphereNode(4250, 0xFFD29D),
        WdAtmosphereNode(4500, 0xFFDBB0),
        WdAtmosphereNode(4750, 0xFFE2C2),
        WdAtmosphereNode(5000, 0xFFE8D1),
        WdAtmosphereNode(5250, 0xFFEDDF),
        WdAtmosphereNode(5500, 0xFFF1EA),
        WdAtmosphereNode(6000, 0xFFF8FD),
        WdAtmosphereNode(6500, 0xF4F2FF),
        WdAtmosphereNode(7000, 0xE9EBFF),
        WdAtmosphereNode(7500, 0xE1E6FF),
        WdAtmosphereNode(8000, 0xDAE2FF),
        WdAtmosphereNode(8500, 0xD5DEFF),
        WdAtmosphereNode(9000, 0xD0DBFF),
        WdAtmosphereNode(9500, 0xCBD8FF),
        WdAtmosphereNode(10000, 0xC7D6FF),
        WdAtmosphereNode(11000, 0xC0D1FF),
        WdAtmosphereNode(12000, 0xBBCEFF),
        WdAtmosphereNode(13000, 0xB7CBFF),
        WdAtmosphereNode(14000, 0xB3C9FF),
        WdAtmosphereNode(15000, 0xB1C8FF),
        WdAtmosphereNode(16000, 0xAFC7FF),
        WdAtmosphereNode(17000, 0xADC7FF),
        WdAtmosphereNode(18000, 0xACC7FF),
        WdAtmosphereNode(19000, 0xAAC6FF),
        WdAtmosphereNode(20000, 0xA9C6FF),
        WdAtmosphereNode(21000, 0xA8C5FF),
        WdAtmosphereNode(22000, 0xA7C5FF),
        WdAtmosphereNode(23000, 0xA7C4FF),
        WdAtmosphereNode(24000, 0xA6C4FF),
        WdAtmosphereNode(25000, 0xA6C4FF),
        WdAtmosphereNode(26000, 0xA5C3FF),
        WdAtmosphereNode(27000, 0xA4C2FF),
        WdAtmosphereNode(28000, 0xA4C2FF),
        WdAtmosphereNode(29000, 0xA3C1FF),
        WdAtmosphereNode(30000, 0xA2C0FF),
        WdAtmosphereNode(31000, 0xA2C0FF),
        WdAtmosphereNode(32000, 0xA1BFFF),
        WdAtmosphereNode(33000, 0xA1BEFF),
        WdAtmosphereNode(34000, 0xA0BEFF),
        WdAtmosphereNode(35000, 0xA0BDFF),
        WdAtmosphereNode(36000, 0x9FBDFF),
        WdAtmosphereNode(37000, 0x9FBCFF),
        WdAtmosphereNode(38000, 0x9EBCFF),
        WdAtmosphereNode(39000, 0x9EBCFF),
        WdAtmosphereNode(40000, 0x9DBBFF),
    };

    inline double wd_srgb_decode(double value)
    {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    }

    inline bool wd_hydrogen_gravity_color(double temperature, double logg, Color &linear)
    {
        static const auto grid = []()
        {
            std::map<double, std::map<double, Color>> result;
            std::ifstream input("catalogs/WD_hydrogen_grid.dat");
            std::string line;
            while (std::getline(input, line))
            {
                if (line.empty() || line[0] == '#') continue;
                double t, g, r, green, b;
                std::istringstream row(line);
                if (row >> t >> g >> r >> green >> b && std::isfinite(t + g + r + green + b) &&
                    t > 0 && r > 0 && green > 0 && b > 0) result[t][g] = Color(r, green, b);
            }
            return result;
        }();
        if (grid.empty() || !std::isfinite(temperature + logg) ||
            temperature < grid.begin()->first || temperature > grid.rbegin()->first) return false;
        const auto mix = [](Color a, Color b, double f)
        {
            return Color(a.red + f * (b.red - a.red), a.green + f * (b.green - a.green), a.blue + f * (b.blue - a.blue));
        };
        const auto at_gravity = [&](const auto &row, double g, Color &value)
        {
            if (g < row.begin()->first || g > row.rbegin()->first) return false;
            auto hi = row.lower_bound(g);
            if (hi->first == g) value = hi->second;
            else
            {
                auto lo = std::prev(hi);
                value = mix(lo->second, hi->second, (g - lo->first) / (hi->first - lo->first));
            }
            return true;
        };
        auto hi = grid.lower_bound(temperature), lo = hi;
        if (hi->first != temperature) --lo;
        Color a, b, reference_a, reference_b;
        if (!at_gravity(lo->second, logg, a) || !at_gravity(hi->second, logg, b) ||
            !at_gravity(lo->second, 8, reference_a) || !at_gravity(hi->second, 8, reference_b)) return false;
        const double f = hi == lo ? 0 : (temperature - lo->first) / (hi->first - lo->first);
        Color target = mix(a, b, f), reference = mix(reference_a, reference_b, f);
        linear.red *= target.red / reference.red;
        linear.green *= target.green / reference.green;
        linear.blue *= target.blue / reference.blue;
        linear.normalize(1);
        return true;
    }

    inline double wd_srgb_encode(double value)
    {
        value = std::clamp(value, 0.0, 1.0);
        return value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    }

    inline Color wd_linear_from_srgb(uint32_t rgb)
    {
        return Color(wd_srgb_decode(((rgb >> 16) & 255) / 255.0),
            wd_srgb_decode(((rgb >> 8) & 255) / 255.0), wd_srgb_decode((rgb & 255) / 255.0));
    }

    inline RGB3 wd_srgb_from_linear(const Color &linear)
    {
        return RGB3((unsigned char)std::lround(255 * wd_srgb_encode(linear.red)),
            (unsigned char)std::lround(255 * wd_srgb_encode(linear.green)),
            (unsigned char)std::lround(255 * wd_srgb_encode(linear.blue)));
    }

    inline bool wd_atmosphere_color(double temperature, WdAtmosphere atmosphere, Color &linear)
    {
        if (!std::isfinite(temperature)) return false;
        const WdAtmosphereNode *first, *last;
        if (atmosphere == WdAtmosphere::Hydrogen)
        {
            first = std::begin(wd_hydrogen_track);
            last = std::end(wd_hydrogen_track);
        }
        else if (atmosphere == WdAtmosphere::Helium)
        {
            first = std::begin(wd_helium_track);
            last = std::end(wd_helium_track);
        }
        else return false;
        if (temperature < first->temperature || temperature > (last - 1)->temperature) return false;
        const auto upper = std::lower_bound(first, last, temperature,
            [](const WdAtmosphereNode &node, double t)
            {
                return node.temperature < t;
            });
        if (upper->temperature == temperature)
        {
            linear = wd_linear_from_srgb(upper->srgb);
            return true;
        }
        const auto lower = upper - 1;
        const double fraction = (temperature - lower->temperature) / (upper->temperature - lower->temperature);
        const Color a = wd_linear_from_srgb(lower->srgb), b = wd_linear_from_srgb(upper->srgb);
        linear = Color(a.red + fraction * (b.red - a.red), a.green + fraction * (b.green - a.green),
            a.blue + fraction * (b.blue - a.blue));
        return true;
    }
}

#endif
