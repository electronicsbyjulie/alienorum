#ifndef ALIENORUM_BARYCENTER_H
#define ALIENORUM_BARYCENTER_H

#include <memory>

namespace alienorum
{
    class CelestialObject;
    class CelestialLocation;

    // Physical hosts remain unchanged; this index describes their dynamical subsystems.
    void rebuild_barycentric_systems();
    void invalidate_barycentric_systems();
    void refresh_barycentric_systems(CelestialObject* body = nullptr);
    void forget_barycentric_body(CelestialObject* body);
    bool update_barycentric_location(CelestialObject* body, double time);
    bool barycentric_evaluation_active();
    bool barycentric_system_center(CelestialObject* body, CelestialLocation& result);

    // Orbit guides use the local subsystem's current position, rather than sweeping its
    // entire journey around more distant hosts into a moon's or planet's orbit guide.
    class BarycentricOrbitPath
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

        public:
        BarycentricOrbitPath(CelestialObject* body, double reference_time);
        ~BarycentricOrbitPath();
        bool sample(double time);
    };
}

#endif
