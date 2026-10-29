
#ifndef _Visuals
#define _Visuals

#include "performance_options.h"
#include "globals.h"

#define bloom_exponent 2.5
#define bloom_softness 0.6
#define max_flare 503
#define antenna_height 5
#define panel_width 6
#define panel_tilt 2

// Terrain rock field tuning parameters
#define ROCK_COUNT              256     // Number of rocks in the field
#define ROCK_MIN_DIST           1.2     // Minimum rock distance from viewer (meters)
#define ROCK_MAX_DIST           100     // Maximum rock distance from viewer (meters)
#define ROCK_MIN_SIZE           0.06    // Minimum rock width (meters)
#define ROCK_MAX_SIZE           1.85    // Maximum boulder width (meters)
#define ROCK_PEBBLE_THRESH      3.5     // Screen pixel threshold below which rocks are drawn as pebbles
#define ROCK_ASPECT_VENUS       0.28    // Height/width aspect ratio for Venusian slabs
#define ROCK_ASPECT_DEFAULT     0.62    // Height/width aspect ratio for standard rocks
#define ROCK_ASPECT_ICY         0.72    // Height/width aspect ratio for icy blocks
#define ROCK_ASPECT_LAVA        0.30    // Height/width aspect ratio for crust rafts and lava crags
#define ROCK_FACET_COUNT        40      // Number of half-icosphere mesh facets for high-poly rocks
#define ROCK_JAGGEDNESS         0.25    // Maximum random displacement fraction for rock vertices
#define ROCK_SHADOW_MAX_MULT    10.3    // Maximum shadow length multiplier relative to height
#define ROCK_SHADOW_ALPHA_MOON  250     // Base shadow alpha for airless/Moon worlds
#define ROCK_SHADOW_ALPHA_MARS  224     // Base shadow alpha for thin-atmosphere/Mars worlds
#define ROCK_SHADOW_ALPHA_VENUS  96     // Base shadow alpha for dense-atmosphere/Venus worlds
#define ROCK_SHADOW_ALPHA_DEF   208     // Base shadow alpha for default worlds

// Sedentary vegetation lifeform tuning parameters
#define PLANT_TRUNK_HEIGHT      22.2    // Stalk / trunk height multiplier relative to base scale
#define PLANT_TRUNK_WIDTH       0.44    // Trunk width multiplier relative to base scale
#define PLANT_CROWN_SIZE        2.85    // Crown horizontal radius multiplier relative to base scale
#define PLANT_CROWN_OBLATENESS  0.35    // Crown oblateness ratio (vertical radius / horizontal radius)
#define PLANT_TRUNK_COLOR_R     115     // Trunk base red component (brown-gray)
#define PLANT_TRUNK_COLOR_G     100     // Trunk base green component (brown-gray)
#define PLANT_TRUNK_COLOR_B     85      // Trunk base blue component (brown-gray)
#define PLANT_MAX_DIST        1000      // Maximum tree distance from viewer (meters)

// Water wave tuning parameters
#define WAVE_STEPS_PER_ROW      96      // Horizontal resolution of wave render (steps per row)
#define WAVE_NUM_ROWS           81      // Number of wave depth rows
#define WAVE_NEAR_DIST          1.0     // Nearest wave distance (meters)
#define WAVE_PERSPECTIVE_EXP    1.5     // Perspective distribution exponent (near-to-horizon row spacing)
#define WAVE_PERSPECTIVE_OFFSET 0.2

// Water sun glint / glistening tuning parameters
#define WAVE_GLINT_LOW_SUN_ELEV 0.35    // Elevation angle (radians, ~20 deg) below which sunset glint path activates
#define WAVE_GLINT_PATH_WIDTH   0.14    // Angular half-width fraction of sunset glint column
#define WAVE_GLINT_CREST_THRESH 0.05    // Wave crest threshold for glint reflections when sun is low

// Planetary water rain
#define rain_cloudiness_threshold 0.35
#define RAIN_MULTIPLIER 503

// Cloud display parameters
#define cumulus_day_swing 1.3
#define cumulus_day_exponent 0.666
#define cumulus_day_maximum 0.71

// Shoreline and other surface boundary params
#define shore_displacement 8.0
// Horizon GPU stages: enable each stage independently for comparison.
#ifndef horizon_gpu_ground_painting
#define horizon_gpu_ground_painting 1
#endif
#ifndef horizon_gpu_terrain_rasterization
// Opt-in: compare performance on the target GPU before enabling by default.
#define horizon_gpu_terrain_rasterization 0
#endif
#ifndef horizon_gpu_mesh_generation
// Opt-in: precision-preserving noise is slower on older integrated GPUs.
#define horizon_gpu_mesh_generation 0
#endif
#ifndef distant_biome_pixel_sampling
#define distant_biome_pixel_sampling 1    // Set to 0 to restore adaptive sampling near the horizon.
#endif
extern bool horizon_terrain_cpu_reference;
extern bool scenery_cpu_reference;
extern bool water_cpu_reference;
struct HorizonWaterStats
{
    unsigned int geometry_builds = 0, bounds_builds = 0, samples = 0;
    unsigned int clipped_points = 0, land_points = 0, uniform_water_points = 0, color_misses = 0;
};
const HorizonWaterStats& horizon_water_stats();
struct HorizonTerrainLodStats
{
    unsigned int patches = 0, accepted_patches = 0, triangles_removed = 0, horizon_segments_removed = 0;
};
const HorizonTerrainLodStats& horizon_terrain_lod_stats();
size_t prefetch_surface_tiles(double milliseconds);
bool surface_prefetch_pending();
size_t local_surface_tile_sample_count();

struct RockInstance
{
    #if PERF_P043
    size_t scenery_index;
    #endif
    bool crown_hidden;
    double dist;
    double ground_dist;
    double theta;
    double screen_x;
    double screen_y;
    double crown_x;
    double crown_y;
    double crown_rx;
    double crown_ry;
    double trunk_w_top;
    double trunk_top_x;
    double trunk_top_y;
    double trunk_w_base;
    double width;
    double height;
    double size;
    double lift_px;
    double opacity;
    uint32_t seed_val;
    bool base_valid;
    bool vegetation;
    bool crown_above_eye;
    double aspect;
    alienorum::RGB3 surface_color;
};

void draw_ra_dec_lines();

int draw_sphere(CelestialObject *cel, double arad);
int draw_sphere_gpu(CelestialObject *cel, double arad);
void draw_ring_gpu(CelestialObject *cel, double arad = 0.0);
int draw_satellite_icon(ImVec2 xycoord, ImU32 satcol);
bool draw_single_object(int i); // return false if not drawn for any reason
alienorum::Star* stellar_display_target(alienorum::Star* component);
void draw_galaxy_band();     // the disc the viewer is standing inside, wrapped across the sky
void draw_objects();
void draw_system_view();
void draw_sunclock();
void find_horizon();
void draw_local_rings();
void draw_horizon();
void draw_sky_gradient();
void draw_cons_lines();
void draw_cached_labels();
void draw_mouse_cursor(ImGuiIO &io);

void draw_cloudy_sky();

// Unlinking the universe (the star-collision branch of draw_sphere()) hands the objects to the
// first of these; the second actually frees them, and has to be called once a frame from the main
// loop because it can only do so after any background texture loads have finished. See their
// definitions in visuals.cpp for the whole story.
void release_universe_objects();
void reap_released_objects();

// Eclipses seen from the ground rather than from space. refresh_eclipse_casters() rebuilds the
// short list of bodies able to cast a shadow (called once per frame, early, since both the sky's
// own brightness and every disc drawn afterwards ask it); eclipse_obscuration() answers how much
// of a light source's disc is hidden from the *observer* right now, which is what dims the
// daylight, brings the stars out, and finally uncovers the corona.
void refresh_eclipse_casters();
double eclipse_obscuration(CelestialObject *light);

// The other side of the same coin: how much of its star's light a body is receiving where it
// actually stands, 0 to 1, given whatever else is in the way -- 1 whenever nothing is, which is
// nearly always. Averaged over the body's whole cross-section rather than taken at its center,
// because what this feeds is an apparent magnitude, and a magnitude is a measure of total light:
// Earth's shadow swallows the Moon whole, while the Moon's shadow costs Earth a few percent.
double eclipse_illumination(CelestialObject *cel);

// The light source currently being eclipsed for the observer, and by how much (0-1), as found by
// the daylight computation in compute_object_draw_coordinates(). Null and 0 the rest of the time,
// which is very nearly always.
extern CelestialObject *eclipsed_light;
extern double eclipsed_fraction;

// NEVER bloom point sources into a giant disc - keep this constant low.
#define max_bloomrad 3

extern double hz_y, jay, appmag, bloomrad, flare, theta, lmasslim;
extern ImVec2 xycoord;
extern ImFont *global_font, *Greek_font;
extern const char *Greek_symbol_mapping;

#endif
