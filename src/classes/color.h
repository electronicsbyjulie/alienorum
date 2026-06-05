
#ifndef _Color
#define _Color

#include "imgui/imgui.h"

#define drawn_cache_split 25

struct RGB
{
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

class Color
{
    public:
    double red = 0;
    double green = 0;
    double blue = 0;

    double luminance();
    static Color color_from_magnitude_indices(double Vmag, double BV);
    static Color color_from_magnitude_indices(double Vmag, double BV, double VR);
    static RGB rgb_from_color(Color c, double bloom_radius = 0);                    // Bloom radius = distance in pixels from center.
    static RGB disc_rgb_from_color(Color c, double disc_radius = 1);                // Disc radius = size in pixels of disc drawn on screen.

    static ImU32 black_to_transparent(ImU32 input);
    json to_json();
    bool from_json(json j);
    Color() {}
    Color(double r, double g, double b) { red=r; green=g; blue=b; }
};

class AlienStyle
{
    public:
    ImU32 cursor_color1 = IM_COL32(96, 0, 24, 76);
    ImU32 cursor_color2 = IM_COL32(160, 20, 20, 76);
    ImU32 cursor_color3 = IM_COL32(255, 48, 0, 76);
    ImU32 grid_color = IM_COL32(255, 0, 0, 96);
    ImU32 grid_color_brighter = IM_COL32(255, 0, 0, 140);
    ImU32 ecliptic_color = IM_COL32(0, 192, 255, 96);
    ImU32 consline_color = IM_COL32(64, 64, 255, 128);
    ImU32 conslbl_color = IM_COL32(255, 192, 0, 128);
    ImU32 selected_color = IM_COL32(0, 255, 96, 192);
    ImU32 selected_orbit_color = IM_COL32(0, 255, 96, 64);
    ImU32 objlbl_color = IM_COL32(64, 255, 0, 176);
    ImVec4 text_color = ImVec4(0.80f, 0.90f, 0.95f, 1.00f);
    ImVec4 graytext_color = ImVec4(0.90f, 0.60f, 0.10f, 1.00f);
    ImVec4 window_bg_color = ImVec4(0.00f, 0.03f, 0.06f, 0.97f);
    ImVec4 input_bg_color = ImVec4(0.00f, 0.05f, 0.10f, 0.00f);
    ImVec4 border_color = ImVec4(0.00f, 0.00f, 0.70f, 0.50f);
    ImVec4 border_shadow = ImVec4(0.00f, 0.00f, 0.05f, 0.00f);
    ImVec4 frame_bg_color = ImVec4(0.00f, 0.21f, 0.44f, 0.39f);
    ImVec4 frame_bg_hover = ImVec4(0.47f, 0.47f, 0.69f, 0.40f);
    ImVec4 frame_bg_active = ImVec4(0.42f, 0.41f, 0.64f, 0.69f);
    ImVec4 title_bg_color = ImVec4(0.13f, 0.37f, 0.53f, 0.93f);
    ImVec4 title_bg_active = ImVec4(0.21f, 0.44f, 0.67f, 0.94f);
    ImVec4 title_bg_collapsed = ImVec4(0.00f, 0.10f, 0.80f, 0.81f);
    ImVec4 menu_bg_color = ImVec4(0.20f, 0.20f, 0.60f, 0.80f);
    ImVec4 scroll_bg_color = ImVec4(0.20f, 0.35f, 0.40f, 0.60f);
    ImVec4 scroll_grab_color = ImVec4(0.40f, 0.65f, 0.80f, 0.30f);
    ImVec4 scroll_hov_color = ImVec4(0.40f, 0.65f, 0.80f, 0.40f);
    ImVec4 scroll_acv_color = ImVec4(0.41f, 0.68f, 0.80f, 0.60f);
    ImVec4 checkmark_color = ImVec4(0.60f, 0.90f, 0.40f, 0.60f);
    ImVec4 slider_color = ImVec4(1.00f, 0.80f, 0.30f, 0.30f);
    ImVec4 slider_acv_color = ImVec4(1.00f, 0.80f, 0.20f, 0.60f);
    ImVec4 button_color = ImVec4(0.29f, 0.50f, 0.10f, 0.62f);
    ImVec4 button_hov_color = ImVec4(0.33f, 0.52f, 0.11f, 0.79f);
    ImVec4 button_acv_color = ImVec4(0.37f, 0.53f, 0.18f, 1.00f);
    ImVec4 sep_color = ImVec4(0.80f, 0.10f, 0.10f, 0.60f);
    ImVec4 text_cursor_color = ImVec4(0.90f, 0.05f, 0.08f, 1.00f);

    bool load(std::string theme_name);
};

extern double global_brightness;
extern bool redlight_mode;
extern double drawblxscalex, drawblxscaley;
extern int *bx_cache, *by_cache;
extern AlienStyle global_style;

void set_gamma(double new_gamma);
double get_gamma();
void rgb_apply_redlight(float *r, float *g, float *b);
__uint32_t rgba_apply_redlight(__uint32_t input);
ImVec4 rgba_apply_redlight(ImVec4 input);
void apply_default_style();

#endif
