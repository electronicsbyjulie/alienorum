
#ifndef _Visuals
#define _Visuals

#include "globals.h"

#define bloom_exponent 2.5
#define antenna_height 5
#define panel_width 6
#define panel_tilt 2

void draw_ra_dec_lines();

int draw_sphere(CelestialObject *cel, double arad);
bool draw_one_object(int i);                            // return false if not drawn for any reason
void draw_objects();
void draw_sky_gradient();
void draw_cons_lines();
void draw_mouse_cursor(ImGuiIO &io);

#define max_bloomrad 10
#define global_font_size (8.647 * 1.776)

extern double jay, appmag, bloomrad, flare, theta, lmasslim;
extern ImVec2 xycoord;
extern ImFont *global_font, *Greek_font;
extern const char *Greek_symbol_mapping;

#endif