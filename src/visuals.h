
#ifndef _Visuals
#define _Visuals

void draw_ra_dec_lines();

int draw_sphere(CelestialObject *cel, double arad);

void draw_objects();

void draw_sky_gradient();

void draw_cons_lines();

void draw_mouse_cursor(ImGuiIO &io);

#endif