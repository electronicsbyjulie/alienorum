#ifndef ALIENORUM_BROWNCOLOR_H
#define ALIENORUM_BROWNCOLOR_H

#include <algorithm>
#include <cmath>
#include <random>
#include "star.h"
#include "wdcolor.h"
#include "noise.h"

namespace alienorum
{
    struct BrownColorNode
    {
        double temperature, x, y;
    };

    // Cranmer (2021), doi:10.3847/2515-5172/ac225c, data doi:10.5281/zenodo.5293307.
    // CC BY 4.0; attribution and model assumptions: catalogs/brown_dwarf_colors.txt.
    // Sonora spectra at log g=5, solar composition; the 3000 K PHOENIX reference
    // supplies a smooth display transition above the 2400 K Sonora grid boundary.
    inline constexpr BrownColorNode brown_color_grid[] =
    {
        {200, 0.6808674, 0.2934780},
        {225, 0.6727583, 0.2904855},
        {250, 0.6606888, 0.2853265},
        {275, 0.6414716, 0.2766649},
        {300, 0.6105328, 0.2621503},
        {325, 0.5553714, 0.2350453},
        {350, 0.4650889, 0.1890981},
        {375, 0.3549773, 0.1317039},
        {400, 0.2564154, 0.0793512},
        {425, 0.2319113, 0.0660877},
        {450, 0.2031764, 0.0500293},
        {475, 0.1864549, 0.0403594},
        {500, 0.1777903, 0.0350668},
        {525, 0.1717107, 0.0313187},
        {550, 0.1682122, 0.0290204},
        {575, 0.1661644, 0.0275906},
        {600, 0.1650500, 0.0267253},
        {650, 0.1639780, 0.0257946},
        {700, 0.1635208, 0.0255174},
        {750, 0.1631996, 0.0256031},
        {800, 0.1629220, 0.0259508},
        {850, 0.1626069, 0.0264830},
        {900, 0.1623201, 0.0271512},
        {950, 0.1621415, 0.0280328},
        {1000, 0.1620986, 0.0290557},
        {1100, 0.1626730, 0.0316730},
        {1200, 0.1646239, 0.0353402},
        {1300, 0.1691561, 0.0406320},
        {1400, 0.1775208, 0.0484235},
        {1500, 0.1922247, 0.0605431},
        {1600, 0.2164198, 0.0792174},
        {1700, 0.2517609, 0.1061518},
        {1800, 0.2884332, 0.1376763},
        {1900, 0.3158863, 0.1703483},
        {2000, 0.3273129, 0.1972142},
        {2100, 0.3258772, 0.2165389},
        {2200, 0.3187372, 0.2308578},
        {2300, 0.3112398, 0.2433013},
        {2400, 0.3041545, 0.2540498},
        {3000, 0.4118083, 0.4016770},
    };

    inline Color brown_linear_color(double temperature)
    {
        if (!std::isfinite(temperature) || temperature <= 0) temperature = 1000;
        const auto first = std::begin(brown_color_grid), last = std::end(brown_color_grid);
        temperature = std::clamp(temperature, first->temperature, (last-1)->temperature);
        auto upper = std::lower_bound(first, last, temperature,
            [](const BrownColorNode& node, double value) { return node.temperature < value; });
        double x = upper->x, y = upper->y;
        if (upper != first)
        {
            const auto lower = upper-1;
            const double mix = (temperature-lower->temperature) / (upper->temperature-lower->temperature);
            x = lower->x + mix*(upper->x-lower->x);
            y = lower->y + mix*(upper->y-lower->y);
        }
        const double X = x/y, Z = (1-x-y)/y;
        Color color(3.2406*X-1.5372-.4986*Z, -.9689*X+1.8758+.0415*Z, .0557*X-.2040+1.0570*Z);
        // Move out-of-gamut chromaticities toward neutral white before normalization.
        const double white = -std::min({0.0, color.red, color.green, color.blue});
        color.red += white; color.green += white; color.blue += white;
        const double peak = std::max({color.red, color.green, color.blue});
        return Color(color.red/peak, color.green/peak, color.blue/peak);
    }

    inline double brown_temperature(Star* star)
    {
        if (std::isfinite(star->temperature) && star->temperature > 0) return star->temperature;
        const char type = star->spectral_type[0];
        const double subtype = star->spectral_type[1] >= '0' && star->spectral_type[1] <= '9'
            ? star->spectral_type[1]-'0' : 5;
        if (type == 'L') return 2300-100*subtype;
        if (type == 'T') return 1300-85*subtype;
        if (type == 'Y') return std::max(200.0, 450-50*subtype);
        return Star::temperature_from_BV(star->BV_color);
    }

    inline bool brown_color_source(Star* star, Color& linear)
    {
        if (!star) return false;
        // Reject ordinary stars before any photometric regime calculation.
        static const double boundary = log(blackbody_flux(2300, V_band)
            / blackbody_flux(2300, B_band))*invlogmagnbase;
        const bool cool = star->temperature > 0 ? star->temperature < 2300
            : star->BV_color > boundary-bv_correction;
        if (!(star->mass > 0 && star->mass < .075*solar_mass)
            && !(star->mass <= 0 && cool)) return false;
        if (stellar_regime(star) != regime_substellar) return false;
        linear = brown_linear_color(brown_temperature(star));
        return true;
    }

    inline void estimate_brown_rotation(Star* star)
    {
        if (star->sidereal_rotational_period != 0) return;
        Color linear;
        if (!brown_color_source(star, linear)) return;
        // Extend the gas-giant mass estimate, without applying planetary tidal heuristics.
        double period = star->mass > 1 ? 1.11e6/log(star->mass) : 4*3600;
        period = std::clamp(period, 1.5*3600, 12*3600.0);
        if (star->mass > 0 && star->volumetric_mean_radius > 0)
        {
            const double radius = star->volumetric_mean_radius;
            const double breakup = 2*_pi*sqrt(radius*radius*radius/(G*star->mass));
            if (std::isfinite(breakup)) period = std::max(period, 1.3*breakup);
        }
        star->sidereal_rotational_period = period;
        star->stellar_rotation = {{"period_seconds", period}, {"estimated", true},
            {"source", "gas-giant mass scaling with breakup limit"}};
    }

    inline Color brown_display_color(double temperature)
    {
        const Color linear = brown_linear_color(temperature);
        return Color(pow(wd_srgb_encode(linear.red), default_gamma),
            pow(wd_srgb_encode(linear.green), default_gamma), pow(wd_srgb_encode(linear.blue), default_gamma));
    }

    struct BrownWeather
    {
        double temperature, bands, contrast, scale, width, storm_lat, storm_lon;
        double phase[3];

        explicit BrownWeather(Star* star)
        {
            temperature = brown_temperature(star);
            std::mt19937_64 random(star->texture_seed());
            const auto draw = [&]() { return double(random() >> 11)*0x1.0p-53; };
            for (double& value : phase) value = 30*draw();
            storm_lat = (draw()-.5)*1.5;
            storm_lon = draw()*2*_pi;
            const double period = fabs(star->sidereal_rotational_period);
            bands = std::clamp(8*sqrt(8*3600/(period > 0 ? period : 8*3600)), 4.0, 18.0);
            const double radius = star->volumetric_mean_radius > 0 ? star->volumetric_mean_radius : jupiter_radius;
            const double gravity = star->mass > 0 ? G*star->mass/(radius*radius) : 1000;
            const double gravity_factor = pow(std::clamp(gravity/1000, .2, 5.0), .25);
            scale = 5*gravity_factor;
            width = .18/gravity_factor;
            contrast = .25 + .6*exp(-pow((temperature-1450)/650, 2)) + .2*exp(-pow((temperature-400)/150, 2));
            if (std::isfinite(star->metallicity)) contrast *= std::clamp(pow(10.0, .12*star->metallicity), .6, 1.4);
        }
    };

    inline Color brown_weather_color(const BrownWeather& weather, double latitude, double longitude)
    {
        const double c = cos(latitude), x = c*cos(longitude), y = sin(latitude), z = c*sin(longitude);
        const double patch = fBm(x*weather.scale+weather.phase[0], y*weather.scale+weather.phase[1],
            z*weather.scale+weather.phase[2], 2, 2, .5)-.5;
        const double band = sin(weather.bands*latitude + weather.phase[0] + c*.7*patch);
        const double u = atan2(sin(longitude-weather.storm_lon), cos(longitude-weather.storm_lon))
            * cos(weather.storm_lat)/weather.width;
        const double v = (latitude-weather.storm_lat)/weather.width;
        const double r2 = u*u+v*v;
        const double vortex = c*c*exp(-r2)*sin(2*atan2(v,u)+4*sqrt(r2));
        const double shade = std::clamp(.78+weather.contrast*(.14*band+.18*patch+.22*vortex), .35, 1.0);
        const Color hue = brown_display_color(weather.temperature*(1+.025*weather.contrast*(band+patch)));
        return Color(hue.red*shade, hue.green*shade, hue.blue*shade);
    }
}

#endif
