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
        double surface_water_share = 1;
        double basin_relief_m = 2500; // RMS relief at Earth gravity; a scenario assumption.
        double nitrogen_frost_albedo = 0.8;
        double nitrogen_frost_emissivity = 0.67;
        double cold_temperature_k = 0; // Zero selects the independent energy-balance proxy.

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

    struct SurfaceWaterEstimate
    {
        bool valid = false;
        float level = 0;
        json diagnostics = nullptr;
    };

    struct ThermalOrbit;

    class CosmicShore
    {
        public:
        static double calculate_escape_velocity(const Planet &p);
        static double calculate_unified_metric(Star &s, Planet &p);
        // Simulation ceiling for inferred pressures, in Pa; not a physical maximum.
        static constexpr double pressure_ceiling = 1e8;
        static AtmosphericInventory default_inventory(const Star& s);
        static AtmosphericInventory default_inventory(Star& s, Planet& p);
        static json input_state(Star& s, Planet& p, const AtmosphericInventory& inventory);
        static const json& envelope_scenario(Star& s, Planet& p, const AtmosphericInventory& inventory);
        static AtmosphericEstimate estimate_atmosphere(Star& s, Planet& p);
        static AtmosphericEstimate estimate_atmosphere(Star& s, Planet& p,
            const AtmosphericInventory& inventory);
        // Periodic thermal and phase history for the currently inspected body.
        static json seasonal_volatiles(const json& budget, const ThermalOrbit& orbit,
            double mean_k, double response_seconds, double flux_reference = 1);
        // Heights are normalized terrain samples with equal surface-area weights.
        static SurfaceWaterEstimate estimate_surface_water(const Planet& p, const json& budget,
            const std::vector<double>& heights = {});
    };
}

#endif
