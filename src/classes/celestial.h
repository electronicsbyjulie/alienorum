#ifndef _CelestialObject
#define _CelestialObject

#include <string>
#include <stdio.h>
#include <setjmp.h>
#include "jpeglib.h"
#include "png.h"
#include "point.h"
#include "color.h"

enum cel_obj_type
{
    // The types are numbered by hierarchy in case of name conflicts, so that e.g. stars cannot orbit moons etc.
    galaxy = 0x100,
    star = 0x200,
    gas_giant = 0x300,
    ice_giant = 0x301,
    hot_jupiter = 0x302,
    rocky = 0x400,
    icy = 0x401,
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


struct my_jpeg_error_mgr
{
    struct jpeg_error_mgr pub;	/* "public" fields */
    jmp_buf setjmp_buffer;	/* for return to caller */
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
    void compute_period(double my_mass = 0);
    void compute_semimajor_axis(double my_mass = 0);
    void compute_center_mass(double my_mass = 0);
    json to_json();
    bool from_json(json j);
};

class Map
{
    protected:
    JSAMPARRAY jpeg_image_buffer = nullptr;         // Points to large array of R,G,B-order data
    // EasyBMP::Image *bmp;

    unsigned char *red_data = nullptr, *green_data = nullptr, *blue_data = nullptr;
    int image_height = 0;                           // Number of rows in image
    int image_width = 0;                            // Number of columns in image
    int allocated = 0;
    double lat_scale, lon_scale, inv_lat_scale, inv_lon_scale;

    public:
    bool load_from_bmp(std::string filename);
    bool load_from_jpeg(std::string filename);
    bool load_from_png(std::string filename);

    RGB color_at(double latitude, double longitude);
    void generate_rocky_map(int latitude_resolution, double BV_color, bool has_water);
    void generate_gas_giant_map(int latitude_resolution, double BV_color);
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
    double obliquity = 0;                       // Equatorial inclination to orbit or to plane of Earth's sky. RADIANS!
    double equinox = 0;                         // RADIANS!
    double equinox_eff = 0;
    double precession = 0;                      // radians/second
    double distance = 0;                        // meters
    bool distance_known = false;
    bool known_poles = false;
    bool estimated_poles = false;

    double epoch = J2000;                       // JD
    double absolute_magnitude = 0;
    double UB_color = 0;
    double BV_color = 0;
    double VR_color = 0;
    double RI_color = 0;

    bool user_added = false;
    bool user_edited = false;

    cel_obj_type type = star;
    char name[32];

    Map *surf_map = nullptr, *bump_map = nullptr, *cloud_map = nullptr,
        *night_map = nullptr, *ring_map = nullptr, *ringx_map = nullptr;
    float drawnx=-1e9, drawny=-1e9, disc_size = 0;
    bool looked_for_maps = false;
    bool onscreen = false;

    CelestialObject();
    virtual ~CelestialObject() = default;
    CelestialLocation location;
    bool lock_equatorial_plane = false;
    Orbit* orbit = nullptr;                     // Most stars won't have an orbit, unless we get into stellar orbital mechanics.
    CelestialObject *cenobj = nullptr;
    Point tmprel;

    CelestialObject* get_light_center();
    double get_equatorial_radius();
    double viewer_magnitude(CelestialLocation seen_from);
    static double distance_from_magnitudes(double apparent, double absolute);
    std::string RA_as_hms(double seen_equinox);
    std::string Decl_as_degms();
    std::string RA_as_hms(CelestialLocation seen_from, double seen_equinox);
    std::string Decl_as_degms(CelestialLocation seen_from);
    void RA_from_hms(std::string);
    void Decl_from_degms(std::string);
    double RA_as_radians(CelestialLocation seen_from, double seen_equinox);
    double Decl_as_radians(CelestialLocation seen_from);
    std::string scaled_distance(CelestialLocation fromwhere);
    json to_json();
    bool from_json(json j);

    protected:
    void update_orbit_location(double tmnow, Rotation* custom_reference_plane = nullptr);

    public:
    inline cel_obj_class typeclass() const { return  _class; };
};

extern CelestialObject **cels, *mycenobj;
extern bool *celskip, *discinstead;
extern double *vmag_cache, *bloomrad_cache, *angular_radius;
extern CelestialLocation here;

#endif