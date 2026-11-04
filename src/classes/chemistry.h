#ifndef ALIENORUM_CHEMISTRY_H
#define ALIENORUM_CHEMISTRY_H

#include <cmath>
#include <string>
#include "nlohmann/json.hpp"

namespace alienorum
{
    class Star;
    struct MetallicityPrior
    {
        bool applicable = false;
        bool class_balanced = false;
        double baseline = NAN, probability = NAN;
        double empirical_weight = 1;
        bool formation_scenario = false;
        std::string reason = "unavailable";
    };
    MetallicityPrior planet_envelope_prior(const Star& host, double earth_masses, double irradiation, double mass_relative_error = NAN);
    const nlohmann::json& planet_formation_chemistry(const Star& host);
}

#endif
