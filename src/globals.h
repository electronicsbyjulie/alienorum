#ifndef _Globals
#define _Globals

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <filesystem>
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <algorithm> 
#include <thread>
#include <chrono>
#include <stdio.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_image.h>
#ifdef _WIN32
// WIN32_LEAN_AND_MEAN excludes windows.h's COM/OLE headers (objidl.h, oaidl.h), which
// otherwise collide with std::byte -- pulled into the global namespace by `using namespace
// std` below -- and fail to compile under mingw's C++17 libstdc++.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>        // SetProcessDPIAware()
#endif
#include "classes/misc.h"
#include "classes/color.h"
#include "classes/serial.h"
#include "classes/cat.h"
#include "include/igfd/ImGuiFileDialog.h"

using namespace std;

extern SDL_Window* window;
extern char lookfor[name_max_len], edit_name[name_max_len], looksat[name_max_len], lookast[name_max_len], lookloc[name_max_len];
extern bool edtname_dirty, lookfor_notfound;
extern std::vector<int> drawnblocks[drawn_cache_split][drawn_cache_split];
extern std::filesystem::path p;
extern std::string load_univ, setjd;
extern bool catalogs_found, fullscreen;
extern int num_galaxies, num_stars, num_planets, num_moons, num_asteroids, num_comets, num_sat;
extern float dispcx, dispcy;
extern int frames_without_mousemove, num_stars_in_box, editidx, addcenidx, themes_selected_idx;
extern double txtyscale, txtycompact, edit_sma, edit_incl, edit_eccn, edit_argperi, edit_epoch,
    edit_node, edit_manom, edit_period, edit_eqincl, edit_equinox, edit_precnode, edit_procargperi;
extern bool is_click, is_dbl_click;
extern double frame_dur, best_frame_dur, scrollhold;
extern bool splash, menu, magnitude_test, redo_proper_motions, fdlg_shown;
extern CelestialObject npdummy;
extern char xplorfor[name_max_len];
extern CelestialObject *last_xplored_cen, *last_neighb_cen;

#endif