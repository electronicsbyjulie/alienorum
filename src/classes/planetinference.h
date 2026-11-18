#ifndef ALIENORUM_PLANETINFERENCE_H
#define ALIENORUM_PLANETINFERENCE_H

#include <mutex>
#include "nlohmann/json.hpp"

namespace alienorum
{
    class Star;
    class Planet;
    extern thread_local bool planet_environments_deferred;
    std::recursive_mutex& planet_inference_mutex(const Planet& planet);
    nlohmann::json snapshot_joint_planet_inference(const Planet& planet);

    class ScopedDeferredPlanetEnvironments
    {
        bool previous;
        public:
        ScopedDeferredPlanetEnvironments() : previous(planet_environments_deferred)
        {
            planet_environments_deferred = true;
        }
        ~ScopedDeferredPlanetEnvironments()
        {
            planet_environments_deferred = previous;
        }
        ScopedDeferredPlanetEnvironments(const ScopedDeferredPlanetEnvironments&) = delete;
        ScopedDeferredPlanetEnvironments& operator=(const ScopedDeferredPlanetEnvironments&) = delete;
    };
    // Deterministic joint scenario selection, with conditional weights and alternatives
    // stored in classification_evidence. Supplied environmental values remain authoritative.
    // Bulk catalog classification reuses existing results without starting an initial fit.
    // Texture workers and explicit requests allow the first calculation.
    bool apply_joint_planet_inference(Star& host, Planet& planet, bool allow_initial_calculation = true);
}

#endif
