
#ifndef _Loaders
#define _Loaders

#include "globals.h"

// Do not call load_textures() directly, and do not start a thread on it by hand: it keeps
// texture_loads_pending in step on the way out, and only spawn_texture_load() puts an entry there
// on the way in. Call spawn_texture_load() instead -- it is a no-op on an object whose maps have
// already been looked for, so the "if (!cel->looked_for_maps)" test at the call site is its job
// now rather than the caller's.
void load_textures(CelestialObject *cel);
void spawn_texture_load(CelestialObject *cel);
void save_textures(CelestialObject *cel);
bool look_for_catalogs();
bool save_universe();
// The default belongs here rather than on the definition in loaders.cpp: put there, it is only
// visible to code below that point in that one file, so alienorum.cpp could not use it.
bool load_universe(std::string universe_fname = "universe.json");
void load_catalogs();
void read_cons_lines();
void cache_cons_lines();
void add_batch_satellites(std::vector<std::string> listlines);
void load_stuff();
void reload_stuff();
bool save_user_json();

extern int sats_added, sat_errors;
extern std::atomic<bool> batch_sats_running;

#endif