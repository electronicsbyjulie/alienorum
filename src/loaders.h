
#ifndef _Loaders
#define _Loaders

#include "globals.h"
#include <functional>

// Texture loads and regeneration share lifetime tracking; shutdown waits before freeing maps.
bool spawn_texture_task(std::function<void()> task);
void wait_for_texture_tasks();
// Call spawn_texture_load() rather than launching load_textures() directly.
void load_textures(CelestialObject *cel);
void spawn_texture_load(CelestialObject *cel);
void save_textures(CelestialObject *cel);
bool establish_project_root();
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
void start_astorb_background_load();
void link_astorb_with_cels();
void start_exoplanet_background_load();
void load_exoplanets_worker();

extern int sats_added, sat_errors;
extern std::atomic<bool> batch_sats_running;

#endif
