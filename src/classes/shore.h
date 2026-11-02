#ifndef _Shore
#define _Shore

#include "star.h"
#include "planet.h"

namespace alienorum
{
    // Scenario inventories are fractions of the entire planet's mass. CO2 is expressed
    // as molecular CO2-equivalent mass, not elemental carbon. Defaults describe rocky
    // planets with approximately terrestrial volatile budgets, not universal abundances.
    struct AtmosphericInventory
    {
        double age_gyr = 4.5;
        double co2_mass_fraction = 1.45e-4;
        double nitrogen_mass_fraction = 1e-6;
        double water_mass_fraction = 2.3e-4;
        double weathering_efficiency = 1;
        double xuv_scale = 1;

        bool from_json(const json& value);
        json to_json() const;
    };

    struct AtmosphericEstimate
    {
        bool valid = false;
        double pressure = 0;
        double co2_moles = 0, nitrogen_moles = 0, water_moles = 0;
        json diagnostics = nullptr;
    };

    class CosmicShore
    {
        public:
        static double calculate_escape_velocity(const Planet &p);
        static double calculate_unified_metric(Star &s, Planet &p);
        // Simulation ceiling for inferred pressures, in Pa; not a physical maximum.
        static constexpr double pressure_ceiling = 1e8;
        static AtmosphericInventory default_inventory(const Star& s);
        static AtmosphericEstimate estimate_atmosphere(Star& s, Planet& p);
        static AtmosphericEstimate estimate_atmosphere(Star& s, Planet& p,
            const AtmosphericInventory& inventory);
    };
}

#endif
