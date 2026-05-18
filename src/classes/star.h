
#ifndef _Star
#define _Star

#include <cstdint>
#include "celestial.h"

class Star : public CelestialObject
{
    public:
    double proper_motion_RA = 0;            // radians / second
    double proper_motion_decl = 0;          // radians / second
    double radial_velocity = 0;             // meters / second
    double apparent_magnitude;              // visual/550nm
    double parallax = 0;                    // radians

    char spectral_type[32];
    char Bayer[32];
    char Flamsteed[32];
    char Gliese[16];
    int BayerGrkno = -1;
    int FlamsteedNo = -1;
    char constellation[32];

    __uint32_t HR = 0;                      // Harvard Revised catalog number
    __uint32_t HD = 0;                      // Henry Draper catalog number
    __uint32_t HIP = 0;                     // Hipparcos catalog number
    __uint32_t SAO = 0;                     // USNO/SAO catalog number

    double estimate_temperature();          // kelvin
    void update_location(double tmnow);     // Apply proper motion and re-derive 3D coordinates from the result.
    void rename_from_Bayer_Flamsteed();
    bool is_sunlike();
    bool is_in_visible_box(Point seen_from);
    void make_universally_visible();

    protected:
    Box visible_area;
    bool visible_area_set = false;
};

void rename_all_from_Bayer_Flamsteed();
void Gliese_doubles_fix();

#endif