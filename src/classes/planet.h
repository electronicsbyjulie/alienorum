
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
        double albedo = 0;

        // Null means the body genuinely has no atmosphere, which is what the three former
        // members (surface_pressure, atmospheric_tau, atmospheric_particulates) used to say by
        // holding zero. The accessors below preserve that reading for the many sites that only
        // want the number, so they do not each have to null-check.
        Atmosphere *atm = nullptr;

        double get_surface_pressure() const { return atm ? atm->surface_pressure : 0; }   // Pa. For gas giants, at the top of the cloud deck if known.
        double get_atmospheric_tau();                                                     // How well the atmosphere absorbs thermal infrared. Thickness dependent.
        double get_particulates() const { return atm ? atm->particulates : 0; }           // Colorimetric: how much of the sky color repeats the surface color vs. Rayleigh scattering.

        // Creates the Atmosphere on first use. Note it default-constructs, so surface_pressure
        // starts at oneatm rather than zero -- callers are expected to set what they mean.
        Atmosphere* ensure_atmosphere() { if (!atm) atm = new Atmosphere(this); return atm; }

        // Extra opposition brightening, in magnitudes, over and above what phase_brightness()
        // already gives the body from its own surface type. Zero -- the value every world saved
        // so far carries -- means "no extra", not "no surge". Give it a positive value for a
        // surface that backscatters unusually hard: fresh, porous, deeply shadowed frost of the
        // Enceladus sort, whose last two degrees before opposition are worth most of a magnitude
        // on their own.
        double opposition_surge = 0;
        double amt_lit = 0;                                 // Geometric: the fraction of the disc we see lit, 1 at full and 0 at new.
        double J2 = 0;
        double ring_radius = 0;
        double ring_inner_radius = 0;
        double ring_mean_opacity = 0;
        int asteroid_no = 0;                                // Zero if major planet or moon.
        bool lock_type = false;

        void setup_atm_ring_props();
        void apply_cosmic_shoreline();
        bool estimate_habitability();
        void set_color_from_type(bool HZ);
        void classify();
        void classify(bool HZ, bool mass_and_rad_known = false, bool color_known = false);
                                                            // set the type, e.g. for exoplanets
        void estimate_radius();                             // if mass known
        void estimate_rotation();                           // if not known, e.g. exoplanets
        double viewer_reflectance_magnitude(CelestialLocation seen_from, double phase = -1, double sourceabsmagn = -1e9, double sourcedist = 0);
        double phase_brightness(double alpha);              // Fraction of the body's opposition brightness left at phase angle alpha (radians).
        double phase_slope_parameter();                     // The IAU G, governing how fast a regolith world dims off opposition.
        double cloud_deck_fraction();                       // 0 for bare ground, 1 for a world we only ever see the top of the weather of.
        void estimate_albedo();                             // if radius and abs mag known
        void estimate_albedo_and_absmagn();                 // if not known, e.g. exoplanets
        void update_location(double tmnow);                 // Only applicable if we have an orbit; otherwise just return.
        double est_bolometric_flux(double t_eff = 0);
        double estimate_bond_albedo();                      // for want of an actual parameter.
        double equilibrium_temperature();
        double estimate_surface_temperature();
        double temperature_at_pressure(double pressure_pa);  // Same greenhouse formula, at an arbitrary reference level.
        bool is_in_con_HZ();                                // True if planet is within the conservative habitable zone.
        double estimate_bump_scale();
        double estimate_scale_height();                     // meters; 0 if airless
        void incline_exo_orbit(double sys_solincl, double sys_solnode);
        double atmospheric_refraction(double altitiude);
        double atmospheric_horizon_lift();

        Planet();
        ~Planet() { if (orbit) delete orbit; if (atm) delete atm; }

        json to_json();
        bool from_json(json j);

        bool guess_has_rings();
        void generate_ring_parameters(bool guarantee_rings = false);        // e.g. if we have a map file

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

Point refract_true_point(Point pt);
Point refract_true_point(Point pt, double alt_rad);

extern std::vector<alienorum::AstorbRow> astorb;

#endif
