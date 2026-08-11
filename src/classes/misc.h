
#ifndef _AlienorumMisc
#define _AlienorumMisc

#include <string>
#include <vector>
#include <queue>
#include <ctime>
#include <thread>
#include <mutex>
#include <cURLpp_single/cURLpp.hpp>
#include "imgui/imgui.h"
#include "nlohmann/json.hpp"
// #include "EasyBMP/EasyBMP.hpp"
#include "noise.h"

#ifdef _WIN32
#define _FILESLASH "\\"
#else
#define _FILESLASH "/"
#endif
#define _FSSTR std::string(_FILESLASH)

#define name_max_len 40

using namespace alienorum;
using json = nlohmann::json;

#define _pi 3.141592653589793238462643383
#define half_pi (_pi * 0.5)
#define light_year 9460730472580800.0
#define light_year_sq (light_year*light_year)
#define parsec 3.08567758128E+16
#define AU 149597870700.0
#define invAU (1.0 / AU)
#define oneday 86400
#define oneyear (365 * oneday + 5 * 3600 + 48 * 60 + 45)
#define sidereal_year (365.256363004 * oneday)
#define J2000 2451544.5
#define speed_of_light 299792458.0
#define G 6.6743015e-14
#define kB 1.380649e-23
#define STEFAN_BOLTZMANN 5.670374419e-8
#define STEFAN_BOLTZMANN_NORM 4.11659e-11
#define Planck 6.62607015e-34
#define water_freezing 273.15
#define oneatm 101325
#define inv_oneatm (1.0 / 101325)

// https://doi.org/10.1051/0004-6361/202348690
#define rocky_mass_cutoff (4.37 * earth_mass)
#define giant_mass_cutoff (127.0 * earth_mass)
#define rocky_density_cutoff 3.0
#define giant_density_cutoff 1.8
#define icy_T_cutoff 200
#define lava_T_cutoff 1300

#define lunar_mass 7.349E+22
#define earth_mass 5.97217e+27
#define jupiter_mass 1.898e+30
#define solar_mass 1.988475e+33
#define hot_jupiter_density 2.22e+5
#define earth_radius 6.371e+6
#define jupiter_radius 6.9886e+7
#define solar_radius 6.95700e+8
#define earth_absmag (-3.86)
#define earth_albedo 0.434
#define fiftyseven (180.0/_pi)
#define fiftyseventh (_pi/180)
#define arcminute (fiftyseventh / 60)
#define arcsecond (arcminute / 60)
#define color_ref_temp 9758.5
#define sun_temp 5778
#define solar_lum_W 3.828e26
#define U_band 3.6e-7
#define B_band 4.4e-7
#define V_band 5.5e-7
#define R_band 5.9e-7

// https://en.wikipedia.org/wiki/Galactic_plane
#define galactic_north_RA_J2000 ((12.0 + 51.0 / 60 + 26.282 / 3600) * 15 * fiftyseventh)
#define galactic_north_Decl_J2000 ((27.0 + 7.0 / 60 + 42.01 / 3600) * fiftyseventh)

// Sgr A*, i.e. the direction of the galactic centre from here, and the Sun's distance to it.
#define galactic_center_RA_J2000 ((17.0 + 45.0 / 60 + 40.04 / 3600) * 15 * fiftyseventh)
#define galactic_center_Decl_J2000 (-(29.0 + 0.0 / 60 + 28.1 / 3600) * fiftyseventh)
#define sun_to_galactic_center (8.2 * 1000 * 3.26156)               // light years

// The Milky Way's own disc. It is in the UNGC (table1.dat line 808, "Milky Way", at Sgr A*), but
// with no axis ratio and no position angle -- reasonably enough, since neither can be measured from
// inside -- so read_UNGC_catalog() puts these on it by name.
//
// system_plane_from_incl_and_node() builds its pole as axis*(-cos i) + normal*sin(i), so its i is
// 180 degrees minus the angle from the line of sight to the pole. Measured against galactic_north
// (point.cpp -- this program's north, the IAU's south, chosen so the Galaxy turns prograde) that
// angle is 89.9538 degrees, making the exact inclination 90.046. The 90.12 below is the value
// asked for and lands 0.074 degrees off it; 90.046 would land on it to four decimal places.
//
// The position angle was solved numerically against that same function. It is the same for any
// inclination, the node being a rotation about the line of sight.
#define milky_way_inclination (90.046 * fiftyseventh)
#define milky_way_position_angle (31.3955 * fiftyseventh)

// The UNGC leaves the a26 column blank for the Milky Way as well, for the same reason, so there is
// no angular size to work a disc radius out of. This is the optical disc -- what the D25 isophote
// would enclose if it could be measured from outside -- at the usual 15 kpc.
#define milky_way_radius (15.0 * 1000 * 3.26156)                    // light years

// https://en.wikipedia.org/wiki/Poles_of_astronomical_bodies
#define solar_north_RA_J2000 (286.13 * fiftyseventh)
#define solar_north_Decl_J2000 (63.87 * fiftyseventh)

// https://en.wikipedia.org/wiki/Orbital_pole
#define ecliptic_north_RA_J2000 (18.00 * 15 * fiftyseventh)
#define ecliptic_north_Decl_J2000 ((66.0 + 33.0 / 60 + 38.55 / 3600) * fiftyseventh)

#define MAX_CELOBJS 524288
#define MAX_SPLASH_STARS 5381
#define MAX_HD 359083
#define MAX_HIP 120416
#define _USE_CCDM 1
#define _ALLOW_CCDM_ADDITIONS 0

// If 1, render filled (non-wireframe) planetary/lunar discs as GPU-shaded texture-mapped
// spheres (see GPU_SPHERE_RENDERING_PLAN.md). If 0, use the original CPU lat/lon-grid
// polygon renderer.
#define ALIENORUM_GPU_SPHERES 1
#define default_brightness 1.0
#define default_gamma 1.0
#define target_frame_rate 30
const std::time_t J2000_TIME_T = 946684800;
#define nlbltyp 11
#define lbltype_brightest 0
#define lbltype_intrinsic 1
#define lbltype_nearby 2
#define lbltype_Bayer 3
#define lbltype_Flamsteed 4
#define lbltype_Gould 5
#define lbltype_sunlike 6
#define lbltype_planets 7
#define lbltype_planethz 8
#define lbltype_binary 9
#define lbltype_knpole 10
#define nceltyp 5
#define _filter_Hipparcos_stars_appmag 0
#define _filter_Hipparcos_stars_absmag 0
#define _cursor_fade 2
#define normal_best_mag_limit 6.5

#define starlight 0.03
#define sphere_rad_threshold (0.2 * fiftyseventh)
#define gossamer_rings 0.08
#define zero_isnt_really_zero 9e-298

// For sun clock
#define ico_sz 13
#define circ_sz 7
#define ln_spc 10

#define NUM_VIEWMODES 4
enum ViewMode
{
    vm_skyatlas = 0,
    vm_horizon = 1,
    vm_sunclock = 2,
    vm_skymap = 3,
    vm_model = 4                // Not implemented yet.
};

#define NUM_VPLANES 4
enum ViewerPlaneMode
{
    vplane_local,
    vplane_ICRF,
    vplane_ecliptic,
    vplane_galactic
};

extern double magnbase, invlogmagnbase;
extern std::string Greek_letter[24];
extern uint32_t xonsm[13];
extern const std::string WHITESPACE;

std::string ltrim(const std::string &s);
std::string rtrim(const std::string &s);
std::string trim(const std::string &s);
double frand(double lmin, double lmax);
std::string Greek_from_abbrev(char* abbrev);
std::string Greek_from_abbrev(std::string abbrev);
double blackbody_flux(double temperature, double wavelength);               // Kelvins and meters.
int Damerau_Levenshtein(const std::string &s1, const std::string &s2);
bool is_digit_or_dot(char);
bool contains_digits_or_dots(const char*);
bool has_same_numbers(const char*, const char*);
std::string lop_component(const char* name);
bool file_exists(const char* fname);
std::time_t file_age(const char* fname);                                    // seconds

bool download_file(std::string URL, std::string save_path);
std::vector<std::string> parse_csv_row(const char* data);
time_t from_iso_string(std::string iso_string, const char* format = nullptr);
double fBm(double x, double y, double z, int octaves, double lacunarity, double gain);
double ridged_fBm(double x, double y, double z, int octaves, double lacunarity, double gain);
double probability_density_function(double x, double mean, double stddev);
int sgn(double f);
std::string Roman(int num);

// Takes velocity in m/s and computes the ratio of Δt(moving)/Δt(stationary). The result will always be <= 1.
double compute_time_dilation(double velocity);

// For orbits.
double solve_Kepler(double M, double e);

// Sun clock conversions
// Subtract dispcx or dispcy first.
// Both functions return RADIANS!
double lon_from_x(double x);
double lat_from_y(double y);
void enforce_y_pan_limit();

std::string elapsed_time(time_t start, time_t end);

std::string cons_from_alienorumid(const std::string alienorumid);
int grkno_from_abbrev(const char *abbrev);

double atmospheric_tau(double normalized_pressure,
    double co2_fraction,        // Carbon dioxide
    double ch4_fraction,        // Methane
    double h2o_fraction,        // Water vapor
    double n2o_fraction = 0,    // Nitrous Oxide
    double o3_fraction  = 0,    // Ozone
    double so2_fraction = 0,    // Sulfur Dioxide
    double h2s_fraction = 0,    // Hydrogen Sulfide
    double co_fraction  = 0,    // Carbon Monoxide
    double hcn_fraction = 0,    // Hydrogen Cyanide
    double h2_fraction = 0,     // Hydrogen
    double nh3_fraction = 0,    // Ammonia
    double c2h6_fraction = 0    // Ethane
    );

extern const char *lbltypes[nlbltyp], *celtypes[nceltyp];
extern int cbolbls_selected_idx, cboceltyp_selected_idx;
extern double bv_correction;

long long micronow();

// APP STATUS AND SETTINGS
extern std::string loading_msg, viewer_theme;
extern std::vector<std::string> themes;
extern std::mutex mtx;
extern const char* vmtext[NUM_VIEWMODES];
extern const char* vptext[NUM_VPLANES];
extern ViewerPlaneMode vplane_mode;
extern ViewMode view_mode;
extern int ncelobjs, selected, trackidx, cursor_size, circle_size, xaorngsim, objinfwnd_hei, timeout_ms, lmx, lmy, whereami, iamhome, took_off_from,
    tookoff_countdown, nsatobjs, is_an_obj_under_cursor, planets_lblcut, celidx_sel_in_sysxplor, first_sat, inside_galaxy_idx;
extern double azimuth, altitude, spin, global_gamma, zoom, mag_limit_adjusted, vm, vmfr, obj_magn_under_cursor, velocmag, JDnow, lbllsys_mass_lim,
    neighb_rthresh, viewer_lat, viewer_lon, viewer_home_lat, viewer_home_lon, viewer_gamma;
extern bool done, show_grid, show_consln, show_xonsm, show_labels, show_orbits, lbl_localsys, show_sats, show_axes, satview_upsidedown,
    show_localsys, label_galaxies, show_galaxy_band, is_mouse_over_window, draggable, dragging, dragged, viewchanged, updating_sats, editing, 
    generating_fic_texture, focus_findbox, whtbkgd, objinfwnd, statuswnd, objedtwnd, astwnd, satwnd, addcelwnd, hide_mouse, searched,
    draw_actual_conslines, explorer, neighborhood, locwnd, show_taucalc, randomize_txgen, save_viewer_latlon, have_Gliese, have_BSC, have_HIP,
    have_Uranio, have_WD, have_CCDM, have_SB9, have_astorb, have_exo, have_RC3, have_UNGC, noexo, nosats, keyprobe, mouse_over_menu, menu_clicked,
    radio_silence;
extern std::string objname, objinfo, viewer_locale;
extern double simnow, npaz, luminous_flux, sclk_scale;
extern double appmagn_lblcut, absmagn_lblcut, distance_lblcut, intrinsic_cutoff, sphere_quality;
extern float has_water, veg_min_temp, veg_max_temp;
extern int menu_ht, vegetation_r, vegetation_g, vegetation_b;
extern char lblcut0[256], lblcut1[256], lblcut2[256];
extern const char* compass[16];
extern PerlinNoise pn;

#endif
