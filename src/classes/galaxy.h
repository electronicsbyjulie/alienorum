#ifndef _Galaxy
#define _Galaxy

#include "celestial.h"

namespace alienorum
{
    class Galaxy : public CelestialObject
    {
        public:
        // Hubble stage T, on the de Vaucouleurs scale the RC3 and the UNGC both use: -6 is a
        // compact elliptical, 0 an S0, 3 an Sb, 5 an Sc, 10 an irregular. This is to a galaxy
        // roughly what the MK spectral type is to a star -- the one number that decides what it
        // ought to look like -- so it is worth carrying even while galaxies still draw as points.
        double morphological_T = 0;
        bool T_known = false;

        // Major axis at the 25 mag/arcsec2 isophote (the RC3's D25, the UNGC's a26), in RADIANS
        // to match right_ascension/declination rather than the arcminutes both catalogs print.
        double angular_diameter = 0;

        // Minor/major. 1 is round or face-on, and small values are edge-on. Together with the
        // position angle this is what will let a galaxy be drawn as an oriented ellipse instead of
        // a flat blob: the ratio gives the inclination, the angle gives the roll.
        double axis_ratio = 1;
        double position_angle = 0;                  // RADIANS, of the major axis. 0 when unknown.
        bool position_angle_known = false;

        double apparent_magnitude = 0;              // total B magnitude as catalogued
        double radial_velocity = 0;                 // m/s, heliocentric. 0 when unknown.
        uint32_t PGC = 0;                           // Principal Galaxies Catalogue number, 0 if none
        char morph_type[16];                        // as printed: ".SAS3.." (RC3) or "Ir" (UNGC)

        Galaxy();
        json to_json();
        bool from_json(json j);
    };
}

#endif
