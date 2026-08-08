
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
void draw_sunclock();
void find_horizon();
void draw_horizon();
void draw_sky_gradient();
void draw_cons_lines();
void draw_mouse_cursor(ImGuiIO &io);

void draw_cloudy_sky();

#define max_bloomrad 10
#define global_font_size (8.647 * 1.776)

extern double hz_y, jay, appmag, bloomrad, flare, theta, lmasslim;
extern ImVec2 xycoord;
extern ImFont *global_font, *Greek_font;
extern const char *Greek_symbol_mapping;

#endif