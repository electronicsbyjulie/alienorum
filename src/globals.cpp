
#include "globals.h"

// IMPORTANT: Any variables defined here must also be declared extern in globals.h.
SDL_Window* window;
char lookfor[40], edit_name[40], looksat[40], lookast[40];
bool edtname_dirty=false;
std::vector<int> drawnblocks[drawn_cache_split][drawn_cache_split];
std::filesystem::path p = "catalogs";
std::string load_univ = "", setjd = "";
bool catalogs_found = false, fullscreen = false;
int num_galaxies=0, num_stars=0, num_planets=0, num_moons=0, num_asteroids=0, num_comets=0, num_sat=0;
float dispcx, dispcy;
int frames_without_mousemove = 0, num_stars_in_box, editidx=-1, addcenidx=-1, themes_selected_idx=-1;
double txtyscale, txtycompact, edit_sma, edit_incl, edit_eccn, edit_argperi, edit_epoch,
    edit_node, edit_manom, edit_period, edit_eqincl, edit_equinox, edit_precnode, edit_procargperi;
bool is_click;
double frame_dur = 0, best_frame_dur = 1e9, scrollhold = 0;
bool splash = true, magnitude_test = false, redo_proper_motions = true, fdlg_shown = false;
CelestialObject npdummy;
char xplorfor[40];
CelestialObject *last_xplored_cen = nullptr;