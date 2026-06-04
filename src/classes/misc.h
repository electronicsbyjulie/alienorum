
#ifndef _AlienorumMisc
#define _AlienorumMisc

#include <string>
#include <vector>
#include <ctime>
#include <thread>
#include <mutex>
#include "imgui/imgui.h"
#include "nlohmann/json.hpp"
// #include "EasyBMP/EasyBMP.hpp"
#include "noise.h"

using json = nlohmann::json;

#define light_year 9460730472580800.0
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
#define Planck 6.62607015e-34
#define earth_mass 5.972e+27
#define jupiter_mass 1.898e+30
#define solar_mass 1.989e+33
#define earth_radius 6.371e+6
#define jupiter_radius 6.9886e+7
#define solar_radius 6.95700e+8
#define earth_absmag (-3.86)
#define earth_albedo 0.434
#define fiftyseven (180.0/M_PI)
#define fiftyseventh (M_PI/180)
#define arcminute (fiftyseventh / 60)
#define arcsecond (arcminute / 60)
#define color_ref_temp 9758.5
#define sun_temp 5778
#define Rsun 695700000
#define Msun 1.988475e+33
#define U_band 3.6e-7
#define B_band 4.4e-7
#define V_band 5.5e-7
#define R_band 5.9e-7

// https://en.wikipedia.org/wiki/Galactic_plane
#define galactic_north_RA_J2000 ((12.0 + 51.0 / 60 + 26.282 / 3600) * 15 * fiftyseventh)
#define galactic_north_Decl_J2000 ((27.0 + 7.0 / 60 + 42.01 / 3600) * fiftyseventh)

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
#define default_brightness 1.0
#define default_gamma 1.0
#define target_frame_rate 30
const std::time_t J2000_TIME_T = 946684800;
#define nlbltyp 8
#define nceltyp 5
#define _filter_Hipparcos_stars_appmag 0
#define _filter_Hipparcos_stars_absmag 0
#define _cursor_fade 2
#define starlight 0.03
#define gossamer_rings 0.08

extern double magnbase, invlogmagnbase;
extern std::string Greek_letter[24];
extern __uint32_t xonsm[13];
extern const std::string WHITESPACE;
extern std::vector<std::string> consname, consabbrev, consgen;

std::string ltrim(const std::string &s);
std::string rtrim(const std::string &s);
std::string trim(const std::string &s);
double frand(double lmin, double lmax);
int Grkno_from_abbrev(char* abbrev);
std::string Greek_from_abbrev(char* abbrev);
std::string Greek_from_abbrev(std::string abbrev);
double blackbody_flux(double temperature, double wavelength);               // Kelvins and meters.
int Damerau_Levenshtein(const std::string &s1, const std::string &s2);
bool is_digit_or_dot(char);
bool contains_digits_or_dots(const char*);
bool has_same_numbers(const char*, const char*);
std::string lop_component(const char* name);
bool file_exists(const char* fname);

double fBm(double x, double y, double z, int octaves, double lacunarity, double gain);
int sgn(double f);

// Takes velocity in m/s and computes the ratio of Δt(moving)/Δt(stationary). The result will always be <= 1.
double compute_time_dilation(double velocity);

// For orbits.
double solve_Kepler(double M, double e);

extern const char *lbltypes[nlbltyp], *celtypes[nceltyp];
extern int cbolbls_selected_idx, cboceltyp_selected_idx;
extern double bv_correction;

long long micronow();

// APP STATUS AND SETTINGS
extern std::string loading_msg;
extern std::mutex mtx;
extern int ncelobjs, selected, trackidx, cursor_size, circle_size, xaorngsim, objinfwnd_hei, timeout_ms, lmx, lmy, whereami, iamhome,
    is_an_obj_under_cursor, planets_lblcut;
extern double azimuth, altitude, spin, global_gamma, zoom, vm, vmfr, obj_magn_under_cursor, velocmag, JDnow, lbllsys_mass_lim;
extern bool show_grid, show_consln, show_xonsm, show_labels, show_orbits, lbl_localsys, is_mouse_over_window, dragging, dragged, viewchanged,
    objinfwnd, statuswnd, objedtwnd, addcelwnd, hide_mouse, searched, draw_actual_conslines;
extern ImU32 cursor_color, cursor_color1, cursor_color2, cursor_color3, grid_color, grid_color_brighter, ecliptic_color, consline_color,
    conslbl_color, selected_color, selected_orbit_color, objlbl_color;
extern std::string objname, objinfo;
extern double simnow;
extern double appmagn_lblcut, absmagn_lblcut, distance_lblcut, intrinsic_cutoff, sphere_quality;
extern char lblcut0[256], lblcut1[256], lblcut2[256];
extern PerlinNoise pn;

#endif
