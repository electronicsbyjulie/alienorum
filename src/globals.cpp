
#include "globals.h"

using namespace alienorum;

// IMPORTANT: Any variables defined here must also be declared extern in globals.h.
SDL_Window* window;
char lookfor[name_max_len], edit_name[name_max_len], looksat[name_max_len], lookast[name_max_len], lookcomet[name_max_len], lookloc[name_max_len];
bool edtname_dirty=false, lookfor_notfound = false;
std::filesystem::path p = "catalogs";
std::string load_univ = "", setjd = "";
bool catalogs_found = false, fullscreen = false;
int num_galaxies=0, num_stars=0, num_planets=0, num_moons=0, num_asteroids=0, num_comets=0, num_sat=0;
float dispcx, dispcy;
int frames_without_mousemove = 0, num_stars_in_box, editidx=-1, addcenidx=-1, themes_selected_idx=-1;
double txtyscale, txtycompact, edit_sma, edit_incl, edit_eccn, edit_argperi, edit_epoch,
    edit_node, edit_manom, edit_period, edit_eqincl, edit_equinox, edit_precnode, edit_procargperi;
bool is_click = false, is_dbl_click = false;
double frame_dur = 0, best_frame_dur = 1e9, scrollhold = 0;
bool splash = true, menu = false, magnitude_test = false, redo_proper_motions = true, fdlg_shown = false;
bool dittrsa = false;
std::atomic<bool> is_reloading(false);
bool take_snapshot = false;
CelestialObject npdummy;
char xplorfor[name_max_len];
CelestialObject *last_xplored_cen = nullptr, *last_neighb_cen = nullptr;
bool play_rise_set_sound = true;
std::string rise_sound_path = default_rise_sound_path;
std::string set_sound_path = default_set_sound_path;
std::thread reload_thread;
std::thread save_tex_thread;
std::thread batch_sat_thread;
std::thread check_sats_thread;
std::thread astorb_thread;
std::atomic<bool> astorb_loading(false);
std::atomic<bool> astorb_loaded(false);
std::atomic<bool> astorb_load_failed(false);
std::atomic<bool> astorb_cancel(false);
std::atomic<float> astorb_load_progress(0.0f);
std::atomic<size_t> astorb_rows_loaded(0);
std::thread exoplanet_load_thread;
std::atomic<bool> exoplanet_loading(false);
std::atomic<float> exoplanet_load_progress(0.0f);
std::atomic<int> exoplanet_count_loaded(0);
bool exoplanet_load_wnd = false;