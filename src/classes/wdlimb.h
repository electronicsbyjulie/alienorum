#ifndef ALIENORUM_WDLIMB_H
#define ALIENORUM_WDLIMB_H

#include "star.h"
#include "wdcolor.h"
#include <array>

namespace alienorum
{
    // Columns 0-2: Claret coefficients for R/G/B. Column 3: inverse means and
    // one common exposure scale. A zero final element selects the quadratic fallback.
    inline bool wd_limb_profile(Star *star, float output[16])
    {
        std::fill(output, output + 16, 0);
        static const auto profiles = []()
        {
            std::map<std::string, std::array<double, 15>> result;
            std::ifstream input("catalogs/WD_limb_profiles.dat");
            std::string line;
            while (std::getline(input, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream row(line);
                std::string id;
                std::array<double, 15> values = {};
                row >> id;
                bool valid = true;
                for (double &value : values)
                {
                    if (!(row >> value) || !std::isfinite(value)) valid = false;
                }
                for (int i = 12; i < 15; ++i) if (values[i] <= 0) valid = false;
                if (valid) result[id] = values;
            }
            return result;
        }();
        if (!star) return false;
        Color average;
        if (!star->resolve_wd_color(average)) return false;
        std::array<double, 15> profile;
        auto found = profiles.find(star->gaia_wd_name);
        if (found != profiles.end()) profile = found->second;
        else
        {
            if (star->spectral_type[0] != 'D' || star->spectral_type[1] != 'A' ||
                !std::isfinite(star->temperature) || star->temperature < 7000 || star->temperature > 20000) return false;
            for (size_t i = 2; star->spectral_type[i]; ++i)
            {
                const char c = star->spectral_type[i];
                if ((c < '0' || c > '9') && c != '.' && c != 'Z') return false;
            }
            const int lower = star->temperature <= 10000 ? 7000 : 10000;
            const int upper = star->temperature <= 10000 ? 10000 : 20000;
            auto a = profiles.find("DA" + std::to_string(lower));
            auto b = profiles.find("DA" + std::to_string(upper));
            if (a == profiles.end() || b == profiles.end()) return false;
            const double f = (star->temperature - lower) / (upper - lower);
            for (int i = 0; i < 15; ++i) profile[i] = a->second[i] + f * (b->second[i] - a->second[i]);
        }
        for (int i = 0; i < 12; ++i) output[i] = profile[i];
        for (int i = 0; i < 3; ++i) output[12 + i] = 1 / profile[12 + i];
        const double center = std::max({average.red * output[12], average.green * output[13], average.blue * output[14]});
        if (!(center > 0)) return false;
        output[15] = 1 / center;
        return true;
    }

    inline Color wd_limb_linear(const float profile[16], Color average, double mu)
    {
        mu = std::clamp(mu, 0.0, 1.0);
        const double root = std::sqrt(mu);
        const double terms[4] = {1 - root, 1 - mu, 1 - mu * root, 1 - mu * mu};
        double intensity[3];
        for (int channel = 0; channel < 3; ++channel)
        {
            double shape = 1;
            for (int k = 0; k < 4; ++k) shape -= profile[4 * channel + k] * terms[k];
            intensity[channel] = std::max(0.0, shape) * profile[12 + channel] * profile[15];
        }
        return Color(average.red * intensity[0], average.green * intensity[1], average.blue * intensity[2]);
    }
}

#endif
