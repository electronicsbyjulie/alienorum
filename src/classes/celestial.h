#ifndef _CelestialObject
#define _CelestialObject

#include <string>
#include "point.h"
#include "color.h"

enum cel_obj_type
{
    // The types are numbered by hierarchy in case of name conflicts, so that e.g. stars cannot orbit moons etc.
    galaxy = 0x100,
    star = 0x200,
    gas_giant = 0x300,
    ice_giant = 0x301,
    rocky = 0x400,
    comet = 0x401,
    artificial = 0x500
};

enum cel_obj_class
{
    class_unknown,
    class_galaxy,
    class_star,
    class_planet,
    class_moon,
    class_satellite
};

class CelestialObject;

class Orbit
{
    public:
    CelestialObject* center = nullptr;
    std::string center_name;                    // Only used for loading from universe file.
    double ascending_node = 0;                  // RADIANS!
    double inclination = 0;                     // RADIANS!
    double semimajor_axis = 0;
    double eccentricity = 0;
    double arg_periapsis = 0;                   // RADIANS!

    double prec_node = 0;                       // radians/second
    double proc_argperi = 0;                    // radians/second

    double mean_anomaly = 0;                    // RADIANS!
    double epoch = J2000;                       // JD
    double period = 0;                          // seconds

    Rotation laplace;

    CelestialLocation compute_3d_location(double epoch);
    json to_json();
    bool from_json(json j);
};

class CelestialObject
{
    protected:
    cel_obj_class _class = class_unknown;

    public:
    double mass = 0;                            // grams
    double volumetric_mean_radius = 0;          // meters
    double oblateness = 0;
    double sidereal_rotational_period = 0;      // seconds
    double right_ascension = 0;                 // RADIANS!
    double declination = 0;                     // RADIANS!
    double inclination = 0;                     // Equatorial. RADIANS!
    double equinox = 0;                         // RADIANS!
    double precession = 0;                      // radians/second
    double distance = 0;                        // meters
    bool distance_known = false;

    double epoch = J2000;                       // JD
    double absolute_magnitude = 0;
    double UB_color = 0;
    double BV_color = 0;
    double VR_color = 0;
    double RI_color = 0;

    cel_obj_type type = star;
    char name[32];

    float drawnx=-1e9, drawny=-1e9;

    CelestialObject();
    CelestialLocation location;
    Orbit* orbit = nullptr;                     // Most stars won't have an orbit, unless we get into stellar orbital mechanics.

    double viewer_magnitude(CelestialLocation seen_from);
    static double distance_from_magnitudes(double apparent, double absolute);
    std::string RA_as_hms();
    std::string Decl_as_degms();
    std::string RA_as_hms(CelestialLocation seen_from);
    std::string Decl_as_degms(CelestialLocation seen_from);
    double RA_as_radians(CelestialLocation seen_from);
    double Decl_as_radians(CelestialLocation seen_from);
    std::string scaled_distance(CelestialLocation fromwhere);
    json to_json();
    bool from_json(json j);

    protected:
    void update_orbit_location(double tmnow, Rotation* custom_reference_plane = nullptr);

    public:
    cel_obj_class typeclass() const { return  _class; };
};

extern CelestialObject **cels;
extern bool *celskip;
extern double *vmag_cache, *magrad_cache;
extern CelestialLocation here;

#endif