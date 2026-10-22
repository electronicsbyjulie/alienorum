#ifndef ALIENORUM_CHEMISTRY_H
#define ALIENORUM_CHEMISTRY_H

#include <cmath>
#include <string>

namespace alienorum
{
    class Star;
    struct MetallicityPrior
    {
        bool applicable = false;
        double baseline = NAN, probability = NAN;
        std::string reason = "unavailable";
    };
    MetallicityPrior planet_envelope_prior(const Star& host, double earth_masses, double irradiation, double mass_relative_error = NAN);
}

#endif
