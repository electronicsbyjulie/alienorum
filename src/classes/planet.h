
#ifndef _Planet
#define _Planet

#include "celestial.h"

// Not sure about this number at all.
#define phase_exponent 1.319507911

namespace alienorum
{
    // Includes planets, minor planets, asteroids, KBOs.
    class Planet : public CelestialObject
    {
        public:
        double albedo;
        double surface_pressure = 0;                        // For gas giants, pressure at the top of the cloud deck if known.
        double atmospheric_tau  = 0;                        // Earth value. How well the atmosphere absorbs thermal infrared. Thickness dependent.
        double opposition_surge = 0;                        // TODO: A full moon is 13 times as bright, or 2.7 magnitudes brighter, compared to a quarter moon.
        double amt_lit = 0;
        double J2;
        double ring_radius = 0;
        int asteroid_no = 0;                                // Zero if major planet or moon.

        void classify();
        void classify(bool HZ);                             // set the type, e.g. for exoplanets
        void estimate_radius();                             // if mass known
        void estimate_rotation();                           // if not known, e.g. exoplanets
        double viewer_reflectance_magnitude(CelestialLocation seen_from, double phase = -1, double sourceabsmagn = -1e9, double sourcedist = 0);
        void estimate_albedo();                             // if radius and abs mag known
        void estimate_albedo_and_absmagn();                 // if not known, e.g. exoplanets
        void update_location(double tmnow);                 // Only applicable if we have an orbit; otherwise just return.
        double est_bolometric_flux(double t_eff = 0);
        double estimate_surface_temperature();
        bool is_in_con_HZ();                                // True if planet is within the conservative habitable zone.
        double estimate_bump_scale();

        Planet();
        ~Planet() { if (orbit) delete orbit; }

        json to_json();
        bool from_json(json j);

        protected:
        bool cache_in_cons_hz;
        double cached_in_cons_hz = -1;
    };

    struct AstorbRow
    {
        uint32_t number;
        std::string name;
        float diam = 0, sma = 0, incl = 0;
        Planet *cel = nullptr;
    };
}

extern std::vector<alienorum::AstorbRow> astorb;

#endif
