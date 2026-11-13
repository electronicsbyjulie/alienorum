#pragma once

#include <cstdint>
#include <cmath>
#include "nlohmann/json.hpp"

namespace alienorum
{
    struct GiantReflectanceInput
    {
        uint32_t seed = 0;
        double equivalent_axis_au = NAN;
        double earth_masses = NAN;
        double gravity_m_s2 = NAN;
        double stellar_temperature_k = 5778;
        double stellar_metallicity_dex = NAN;
        double carbon_relative_to_iron_dex = NAN;
        double methane_fraction = NAN;
        double haze = NAN;
        bool neptune = false;
    };

    // An estimated, full-phase reflection spectrum reduced to B-V and V-R.
    // The JSON carries its assumptions and band colors across saved universes.
    nlohmann::json estimate_giant_reflectance(const GiantReflectanceInput& input);
    bool valid_giant_reflectance(const nlohmann::json& model, double bv);
}
