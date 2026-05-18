
#ifndef _Planet
#define _Planet

#include "celestial.h"

// Not sure about this number at all.
#define phase_exponent 1.319507911

// Includes planets, moons, asteroids, comets, KBOs, etc.
// If it's natural and orbits a star, and isn't a star itself,
// odds are it goes in this class.
class Planet : public CelestialObject
{
    public:
    double albedo;
    Color color;
    double period = 0;                            // seconds
    double surface_pressure = 0;                        // For gas giants, pressure at the to pof the cloud deck if known.
    double opposition_surge = 0;                        // TODO: A full moon is 13 times as bright, or 2.7 magnitudes brighter, compared to a quarter moon.
    double amt_lit = 0;

    double viewer_reflectance_magnitude(CelestialLocation seen_from);
    void update_location(double tmnow);                 // Only applicable if we have an orbit; otherwise just return.

    Planet();
};

#endif
