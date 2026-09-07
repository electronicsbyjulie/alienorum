
#ifndef _Visuals
#define _Visuals

#include "globals.h"

#define bloom_exponent 2.5
#define bloom_softness 0.6
#define max_flare 503
#define antenna_height 5
#define panel_width 6
#define panel_tilt 2

void draw_ra_dec_lines();

int draw_sphere(CelestialObject *cel, double arad);
int draw_sphere_gpu(CelestialObject *cel, double arad);
int draw_satellite_icon(ImVec2 xycoord, ImU32 satcol);
bool draw_one_object(int i); // return false if not drawn for any reason
void draw_galaxy_band();     // the disc the viewer is standing inside, wrapped across the sky
void draw_objects();
void draw_system_view();
void draw_sunclock();
void find_horizon();
void draw_horizon();
void draw_sky_gradient();
void draw_cons_lines();
void draw_mouse_cursor(ImGuiIO &io);

void draw_cloudy_sky();

// Unlinking the universe (the star-collision branch of draw_sphere()) hands the objects to the
// first of these; the second actually frees them, and has to be called once a frame from the main
// loop because it can only do so after any background texture loads have finished. See their
// definitions in visuals.cpp for the whole story.
void release_universe_objects();
void reap_released_objects();

// Eclipses seen from the ground rather than from space. refresh_eclipse_casters() rebuilds the
// short list of bodies able to cast a shadow (called once per frame, early, since both the sky's
// own brightness and every disc drawn afterwards ask it); eclipse_obscuration() answers how much
// of a light source's disc is hidden from the *observer* right now, which is what dims the
// daylight, brings the stars out, and finally uncovers the corona.
void refresh_eclipse_casters();
double eclipse_obscuration(CelestialObject *light);

// The other side of the same coin: how much of its star's light a body is receiving where it
// actually stands, 0 to 1, given whatever else is in the way -- 1 whenever nothing is, which is
// nearly always. Averaged over the body's whole cross-section rather than taken at its center,
// because what this feeds is an apparent magnitude, and a magnitude is a measure of total light:
// Earth's shadow swallows the Moon whole, while the Moon's shadow costs Earth a few percent.
double eclipse_illumination(CelestialObject *cel);

// The light source currently being eclipsed for the observer, and by how much (0-1), as found by
// the daylight computation in compute_object_draw_coordinates(). Null and 0 the rest of the time,
// which is very nearly always.
extern CelestialObject *eclipsed_light;
extern double eclipsed_fraction;

#define max_bloomrad 10
#define global_font_size (8.647 * 1.776)

extern double hz_y, jay, appmag, bloomrad, flare, theta, lmasslim;
extern ImVec2 xycoord;
extern ImFont *global_font, *Greek_font;
extern const char *Greek_symbol_mapping;

#endif