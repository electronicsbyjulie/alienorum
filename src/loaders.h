
#ifndef _Loaders
#define _Loaders

#include "globals.h"

void load_textures(CelestialObject *cel);
void save_textures(CelestialObject *cel);
bool look_for_catalogs();
bool save_universe();
bool load_universe(std::string universe_fname);
void load_catalogs();
void read_cons_lines();
void cache_cons_lines();
void add_batch_satellites(std::vector<std::string> listlines);
void load_stuff();
void reload_stuff();
bool save_user_json();

extern int sats_added, sat_errors;

#endif