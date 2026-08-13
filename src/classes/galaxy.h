#ifndef _Galaxy
#define _Galaxy

#include "celestial.h"

namespace alienorum
{
    struct GalaxyBand
    {
        std::vector<double> road1_gra, road1_gdecl, road1_dist, road2_gra, road2_gdecl, road2_dist;

        int load_dat_file(std::string fname);
        int create_fictitious();
    };

    class Galaxy : public CelestialObject
    {
        public:
        // Hubble stage T on the de Vaucouleurs scale, as used by both RC3 and UNGC: -6 compact
        // elliptical, 0 S0, 3 Sb, 5 Sc, 10 irregular. To a galaxy roughly what the MK spectral
        // type is to a star -- the one number deciding what it should look like.
        double morphological_T = 0;
        bool T_known = false;

        // Major axis at the 25 mag/arcsec2 isophote (the RC3's D25, the UNGC's a26), in RADIANS
        // to match right_ascension/declination rather than the arcminutes both catalogs print.
        double angular_diameter = 0;

        // Minor/major axis ratio: 1 is round or face-on, small values edge-on. With the position
        // angle, this is what makes an oriented ellipse -- ratio gives inclination, angle roll.
        double axis_ratio = 1;
        double position_angle = 0;                  // RADIANS, of the major axis. 0 when unknown.
        bool position_angle_known = false;

        // 0 is face-on, pi/2 edge-on. Measured where the UNGC supplies one, otherwise deprojected
        // from the axis ratio; see galaxy_inclination() in cat.cpp for which and why.
        double inclination = 0;                     // RADIANS!

        double apparent_magnitude = 0;              // total B magnitude as catalogued
        double radial_velocity = 0;                 // m/s, heliocentric. 0 when unknown.
        uint32_t PGC = 0;                           // Principal Galaxies Catalogue number, 0 if none
        char morph_type[16];                        // as printed: ".SAS3.." (RC3) or "Ir" (UNGC)

        GalaxyBand band;

        Galaxy();
        json to_json();
        bool from_json(json j);
    };
}

#endif
