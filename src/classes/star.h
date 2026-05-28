
#ifndef _Star
#define _Star

#include <cstdint>
#include <math.h>
#include "celestial.h"

class StarMulti;

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
    std::string CCDM;
    char ccdm_compseq = 0;
    StarMulti* multisys = nullptr;
    char get_component();
    void set_component(char comp, Star* compA);

    __uint32_t HR = 0;                      // Harvard Revised catalog number
    __uint32_t HD = 0;                      // Henry Draper catalog number
    __uint32_t HIP = 0;                     // Hipparcos catalog number
    __uint32_t SAO = 0;                     // USNO/SAO catalog number
    __uint32_t SB9 = 0;                     // 9th Catalogue of Spectroscopic Binary Orbits designation
    char Bonn_survey[3] = {0,0,0};          // BD = Bonn, CD = Cordoba, CP = Cape Town
    char Bonn_survey_sign = '+';
    int Bonn_survey_declination = 0;        // Declination category
    __uint32_t Bonn_survey_sequential = 0;  // Serial number by right ascension.

    bool is_orbit_multiple = false;
    bool has_planets = false;
    bool tmp_vis_flag;                      // Used only for rendering.

    Star();
    ~Star();

    void update_location(double tmnow);     // Apply proper motion and re-derive 3D coordinates from the result.
    void rename_from_Bayer_Flamsteed();
    bool is_sunlike();
    bool is_in_visible_box(Point seen_from);
    bool is_really_truly_in_visible_box(Point seen_from);
    void make_universally_visible();

    double estimate_temperature();          // Based on MK spectral type code
    double estimate_mass();
    double estimate_BV();                   // Blackbody value from estimated temperature from MK spectral type
    double estimate_UB();                   // "

    double estimate_radius();
    void gotta_be_named_something();
    json to_json();
    bool from_json(json j);
    void make_companion_of(Star* primary, char comp = 'B');

protected:
    Box visible_area;
    bool visible_area_set = false;
    bool _is_in_visible_range = true;
};

class StarMulti
{
    public:
    ~StarMulti();
    void add_member(Star* s, char comp);
    Star* get_member(char comp);
    char is_member(Star* s);
    void unlink();                          // Call this before deleting object and before deleting any stars.

    protected:
    Star** members = nullptr;
    char allocated = 0;
};

void rename_all_from_Bayer_Flamsteed();
void Gliese_doubles_fix();

#endif