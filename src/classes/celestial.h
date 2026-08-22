#ifndef _CelestialObject
#define _CelestialObject

#include <string>
#include <stdio.h>
#include <setjmp.h>
#include <map>
#include "jpeglib.h"
#include "png.h"
#include "point.h"
#include "color.h"

namespace alienorum
{
    enum cel_obj_type
    {
        // The types are numbered by hierarchy in case of name conflicts, so that e.g. stars cannot orbit moons etc.
        galaxy = 0x100,
        star = 0x200,
        gas_giant = 0x300,
        ice_giant = 0x301,
        hot_jupiter = 0x310,
        clearskies = 0x401,
        waterworld = 0x410,     // includes hycean
        icy = 0x502,
        rocky = 0x503,          // includes Venusian
        lavaworld = 0x504,
        icy_tailed = 0x511,
        artificial = 0xf00
    };

    // Which body types get their surface from generate_rocky_map(), and therefore carry bump
    // data worth reusing. Kept in one place because load_textures() and mark_for_map_regen()
    // both branch on it, and the two lists had drifted apart.
    inline bool uses_rocky_map(cel_obj_type type)
    {
        return type == rocky || type == icy || type == waterworld || type == lavaworld;
    }

    enum cel_obj_class
    {
        class_unknown,
        class_galaxy,
        class_star,
        class_planet,
        class_moon,
        class_comet,
        class_satellite
    };


    struct my_jpeg_error_mgr
    {
        struct jpeg_error_mgr pub;	/* "public" fields */
        jmp_buf setjmp_buffer;	/* for return to caller */
    };

    class CelestialObject;

    struct OsculatingElement
    {
        double epoch = 0;
        double eccentricity = 0;
        double inclination = 0;
        double ascending_node = 0;
        double arg_perifocus = 0;
        double T_periapsis = 0;
        double mean_anomaly = 0;
        double semimajor_axis = 0;
        double period = 0;

        static OsculatingElement* read_from_file(std::string filename, uint64_t *elements_read);
    };

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
        double period = 0;                          // seconds. Zero on an open orbit, which has none.

        // An escaping body -- a comet on a parabola or a hyperbola -- has no semimajor axis worth
        // the name and no mean anomaly to run round it, so it is anchored on the one point it does
        // have: its perihelion, and the moment it passes through it. semimajor_axis then holds the
        // *magnitude* |q/(1-e)| purely as a scale length for the code that culls distant objects by
        // orbit size, and is a placeholder on a parabola, where the true value is infinite.
        double periapsis_distance = 0;              // meters. Zero means "closed orbit, read semimajor_axis".
        double T_periapsis = 0;                     // JD of perihelion passage.

        bool is_open() const { return eccentricity >= 1; }

        double heliocentric_inclination = 0;
        double heliocentric_node = 0;

        Rotation laplace;
        OsculatingElement *osculating = nullptr;
        uint64_t num_osc = 0;

        ~Orbit();
        CelestialLocation compute_3d_location(double epoch);
        bool read_osc_elements(std::string cel_name);
        void interpolate_osculating_e(double for_epoch,
            double& n, double& i, double& w, double& a, double& e, double& m, double& p,
            double& precn, double& procarg,         // Bescause we interpolate, we only will die without these if using mean elements.
            double& eff_epoch);
        void compute_period(double my_mass = 0);
        void compute_semimajor_axis(double my_mass = 0);
        void compute_center_mass(double my_mass = 0);
        json to_json();
        bool from_json(json j);
    };

    // One impact crater: a bowl with a rim (and a central peak above a size threshold) stamped
    // into the bump map, plus optional rays and rim brightening in the color data. Center is a
    // unit vector on the sphere; sizes are angular, so resolution- and radius-independent.
    struct Crater
    {
        double cx, cy, cz;                  // unit vector, crater center
        double angular_radius;              // radians
        double reach_factor;                // how far past angular_radius (in units of it) the rim/ejecta fade out
        double depth, rim_height, rim_width, central_peak;
        bool has_rays;
        double ray_freq, ray_phase, ray_sharpness, ray_extent_factor;
        // Tangent-plane basis at the crater center, for measuring ray bearing.
        double ex, ey, ez, tnx, tny, tnz;
    };

    class AtmosphereComposition
    {
        public:
        double H2_portion = 0;
        double He_portion = 0;
        double N2_portion = 0;
        double O2_portion = 0;
        double O3_portion = 0;
        double CO2_portion = 0;
        double CH4_portion = 0;
        double SO2_portion = 0;
        double H2O_portion = 0;
        double H2S_portion = 0;
        double HCN_portion = 0;
        double NH3_portion = 0;
        double C2H6_portion = 0;
        double N2O_portion = 0;
        double CO_portion = 0;
        double Ar_portion = 0;

        CelestialObject *cel = nullptr;
        AtmosphereComposition(CelestialObject *obj) { cel = obj; }

        void enforce_integrity();
        double mean_molar_mass();                   // kg/mol, renormalized; Earth air if empty
        void generate_fictitious_gas_giant();
        void generate_fictitious_ice_giant();
        void generate_fictitious_venusian();        // doubles as martian
        void generate_fictitious_titanean();
        void generate_fictitious_habitable();
        void generate_fictitious_for_planet(cel_obj_type t);        // does not call habitable! have to call habitable separately.

        json to_json();
        bool from_json(json j);
    };

    class Atmosphere
    {
        public:
        double surface_pressure = oneatm;
        double tau = 0;
        double particulates = 0;
        CelestialObject *cel = nullptr;
        AtmosphereComposition* comp = nullptr;

        Atmosphere(CelestialObject *obj) { cel = obj; }
        ~Atmosphere() { if (comp) delete comp; }

        AtmosphereComposition* ensure_composition() { if (!comp) comp = new AtmosphereComposition(cel); return comp; }
        void calculate_tau(double pressure);

        json to_json();
        bool from_json(json j);
    };

    class Map
    {
        protected:
        JSAMPARRAY jpeg_image_buffer = nullptr;                     // Points to large array of R,G,B-order data
        // EasyBMP::Image *bmp;

        unsigned char *red_data = nullptr, *green_data = nullptr, *blue_data = nullptr;
        double *bump_data = nullptr;
        unsigned long image_height = 0;                             // Number of rows in image
        unsigned long image_width = 0;                              // Number of columns in image
        unsigned long allocated = 0;
        // Zero until a loader or generator establishes the image geometry. These were previously
        // left uninitialized, which made idx_of()'s "is this map ready yet" test read an
        // indeterminate value: whether a fresh Map looked ready was whatever happened to be on
        // the heap.
        double lat_scale = 0, lon_scale = 0, inv_lat_scale = 0, inv_lon_scale = 0;
        CelestialObject *mcel = nullptr;

        // What idx_of() returns when the geometry above is not established yet, so there is no
        // meaningful pixel to point at. Deliberately past any real index, so the range check
        // elevation_at() already performs rejects it but Claude is not PTSD friendly.
        static constexpr unsigned int idx_not_ready = 0xFFFFFFFFu;

        unsigned int idx_of(double latitude, double longitude);
        void stamp_craters(CelestialObject *cel, double bump_scale);

        // Globally unique version stamp for this Map's RGB pixels, never reused even across a
        // delete+new at the same address. touch_gen() bumps it on every change, so the GPU
        // texture cache (gputex.h) can spot a stale upload by comparison alone. Never 0, which
        // is reserved for "touch_gen() hasn't run yet" -- see gen below.
        static unsigned int next_gen();

        public:
        // 0 until the pixels are actually written. has_rgb_data() goes true the moment the three
        // channel arrays are allocated, long before any loader or generator has filled them, so a
        // consumer going by that alone uploads real data as far as the background thread got and
        // uninitialized heap after it. gputex_for() must check gen != 0 as well.
        unsigned int gen = 0;
        Map() { ; }
        Map(CelestialObject* joined_cel) { mcel = joined_cel; }

        void touch_gen() { gen = next_gen(); }

        bool load_from_bmp(std::string filename, bool as_bump = false, double bump_scale = 20000);
        bool load_from_jpeg(std::string filename, bool as_bump = false, double bump_scale = 20000);
        bool load_from_png(std::string filename, bool as_bump = false, double bump_scale = 20000);
        bool save_to_png(std::string filename);
        void correct_colors(double rtot, double gtot, double btot);
        inline bool has_bump_data() { return bump_data && image_height; }
        inline bool has_rgb_data() { return red_data && green_data && blue_data && image_height; }
        void resample_bump_data(unsigned int new_resolution);
        void _map_resample_bump_regen_rocky(CelestialObject *cel);

        RGB3Byte color_at(double latitude, double longitude);
        double elevation_at(double latitude, double longitude);     // Returns meters.
        void generate_rocky_map(CelestialObject *cel);
        void generate_lava_map(CelestialObject *cel);
        void generate_gas_giant_map(CelestialObject *cel);
        void generate_overcast_sky(CelestialObject *cel);
        void generate_stellar_map(CelestialObject *cel);
        void generate_ring_map(CelestialObject *cel, int resolution, double rel_inner_radius, double mean_opacity, Map* transparency_map);
        void mark_for_map_regen(CelestialObject *cel, bool discard_bump = false);

        // For the GPU texture cache (gputex.h): size of the equirectangular grid, and a bulk
        // RGBA8 export into a caller-allocated width*height*4 buffer, so the channel arrays
        // themselves stay private.
        inline unsigned long get_width() const { return image_width; }
        inline unsigned long get_height() const { return image_height; }
        void export_rgba(unsigned char *out) const;

        // As export_rgba(), for bump_data: width*height caller-allocated floats, in metres like
        // elevation_at(), all 0 when there is no bump data.
        void export_bump(float *out) const;
    };

    class Locale
    {
        public:
        std::string name;
        double lat, lon, tz;                    // radians, radians, seconds
        bool user_added = false;
        bool user_modified = false;
        bool dst = false;

        Locale() {}
        Locale(json from_json);

        ~Locale() = default;
        Locale(const Locale& other) = default;
        Locale& operator=(const Locale& other) = default;
        Locale(Locale&& other) noexcept = default;
        Locale& operator=(Locale&& other) noexcept = default;
    };

    class CelestialObject
    {
        protected:
        cel_obj_class _class = class_unknown;

        public:
        int seqno = -1;
        bool deleted = false;

        double mass = 0;                            // grams
        double volumetric_mean_radius = 0;          // meters
        double oblateness = 0;
        double temperature = 0;                     // Kelvins
        double sidereal_rotational_period = 0;      // seconds
        double right_ascension = 0;                 // RADIANS!
        double declination = 0;                     // RADIANS!
        double obliquity = 0;                       // Equatorial inclination to orbit or to plane of Earth's sky. RADIANS!
        double equinox = 0;                         // RADIANS!
        double equinox_eff = 0;
        // Where zero hours of right ascension falls, in this body's own equatorial frame. Not the
        // same number as equinox_eff, which measures the axis's lean around the *orbit*; the two
        // frames have different zero directions and coincide only on Earth. Derived in
        // update_orbit_location(). RADIANS!
        double equinox_RA = 0;
        double precession = 0;                      // radians/second
        double distance = 0;                        // meters
        bool distance_known = false;
        bool known_poles = false;
        bool estimated_poles = false;
        double lon_J2000_offset = 0;
        int rnd_seed = 0;
        std::mt19937 rng;
        int cel_rand();
        double cel_frand(double min, double max);

        double epoch = J2000;                       // JD
        double absolute_magnitude = 0;
        double UB_color = 0;
        double BV_color = 0;
        double VR_color = 0;
        double RI_color = 0;

        bool user_added = false;
        bool user_edited = false;

        cel_obj_type type = star;
        char name[name_max_len];
        std::string origname = "", origcenname = "";

        Map *surf_map = nullptr, *cloud_map = nullptr, *night_map = nullptr,
            *ring_map = nullptr, *ringx_map = nullptr;
        bool has_real_maps = false;
        Locale *locales = nullptr;
        int nlocales = 0;
        float drawnx=-1e9, drawny=-1e9, drawnxmin=-1e9, drawnxmax=-1e9, drawnymin=-1e9, drawnymax=-1e9;
        bool looked_for_maps = false, ignore_map_files = false;
        unsigned int fictitious_map_height = 512;            // Good enough for flying around but inadequate for world building.
        bool onscreen = false;

        CelestialObject();
        virtual ~CelestialObject() = default;
        CelestialLocation location;
        bool lock_equatorial_plane = false, lock_system_plane = false;
        Orbit* orbit = nullptr;                     // Most stars won't have an orbit, unless we get into stellar orbital mechanics.
        CelestialObject *cenobj = nullptr;
        Point tmprel, viewrel;
        double get_horizon_angle();
        double get_horizon_distance();
        double timeofday();
        int read_locales(std::string json_fname);
        inline bool is_tidal_locked() { return orbit ? (fabs((sidereal_rotational_period / orbit->period) - 1) < 0.01) : false; }

        CelestialObject* get_light_center();
        double estimate_surface_gravity();
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
        double Decl_as_radians_refracted(CelestialLocation seen_from);

        std::string scaled_distance(CelestialLocation fromwhere, bool is_low_orbit_sat = false);
        void randomize();
        double density();
        double Hill_sphere_radius();
        double Roche_limit(CelestialObject *orbiter = nullptr);
        json to_json();
        bool from_json(json j);

        protected:
        int read_locales_json(json from_json);
        void update_orbit_location(double tmnow, Rotation* custom_reference_plane = nullptr);
        double _currM = 0;
        double _currTOD = 0;

        public:
        inline cel_obj_class typeclass() const { return  _class; };
        const double& curr_mean_anom = _currM;
    };
}

extern CelestialObject **cels, *mycenobj;
extern std::vector<std::vector<CelestialObject*>> first_letter_index;
extern std::map<std::string,std::vector<CelestialObject*>> constellation_index;
bool append_cel(CelestialObject* cel);          // maintain indices; false if the array is full

Point to_viewer_plane(Point pt, int sign = 1);

extern bool *celskip, *discinstead;
extern double *vmag_cache, *bloomrad_cache, *angular_radius;
extern CelestialLocation here;
extern double azimuth_correction;
extern Locale *is_a_locale_under_cursor, *selected_locale;

#endif