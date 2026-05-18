#ifndef _CelestialObject
#define _CelestialObject

#include <string>
#include "point.h"
#include "color.h"

enum cel_obj_type
{
    galaxy,
    star,
    gas_giant,
    ice_giant,
    rocky,
    comet,
    artificial
};

class CelestialObject;

class Orbit
{
    public:
    CelestialObject* center = nullptr;
    double ascending_node = 0;                  // RADIANS!
    double inclination = 0;                     // RADIANS!
    double omega = 0;                           // RADIANS!
    double semimajor_axis = 0;
    double eccentricity = 0;
    double arg_periapsis = 0;                   // RADIANS!
    double mean_anomaly = 0;                    // RADIANS!
    double epoch = J2000;                       // JD
    double period = 0;                    // seconds

    CelestialLocation compute_3d_location(double epoch);
};

class CelestialObject
{
    public:
    double mass = 0;                            // grams
    double volumetric_mean_radius = 0;          // meters
    double oblateness = 0;
    double sidereal_rotational_period = 0;      // seconds
    double right_ascension = 0;                 // RADIANS!
    double declination = 0;                     // RADIANS!
    double inclination = 0;                     // Equatorial. RADIANS!
    double equinox = 0;                         // RADIANS!
    double distance = 0;                        // meters
    bool distance_known = false;
    double epoch = J2000;                       // JD
    double absolute_magnitude = 0;
    double UB_color = 0;
    double BV_color = 0;
    double VR_magnitude = 0;
    double RI_magnitude = 0;

    cel_obj_type type = star;
    char name[32];

    float drawnx=-1e9, drawny=-1e9;

    CelestialObject() {};
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

    protected:
    void update_orbit_location(double tmnow);
};

extern CelestialObject **cels;
extern bool *celskip;
extern double *vmag_cache, *magrad_cache;
extern CelestialLocation here;

#endif