#ifndef _Housekeeping
#define _Housekeeping

#include "globals.h"

#pragma once
void refresh_star_visibilities();
void set_viewer_surface_location(bool also_set_plane);
void set_viewer_location_and_plane();
Point viewer_displacement(const Point& motion, double elapsed_seconds);
bool planetary_system_is_active(const CelestialObject* cel);
void invalidate_frame_objects();
const std::vector<int>& frame_object_candidates();
const std::vector<int>& frame_active_world_candidates();
const std::vector<int>& frame_draw_candidates();
const std::vector<int>& frame_orbit_candidates();
const std::vector<int>& frame_bright_candidates();
const std::vector<int>& frame_pick_candidates();
extern bool picking_cpu_reference;
bool compute_object_location(CelestialObject *cel, bool* relative_valid = nullptr);
void compute_object_draw_coordinates();
void set_center_objects();
#endif
