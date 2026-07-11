#ifndef _Housekeeping
#define _Housekeeping

#include "globals.h"

#pragma once
void refresh_star_visibilities();
void set_viewer_surface_location(bool also_set_plane);
void set_viewer_location_and_plane();
bool compute_object_location(CelestialObject *cel);
void compute_object_draw_coordinates();
void set_center_objects();
#endif
