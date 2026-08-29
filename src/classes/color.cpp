#include <math.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "point.h"
#include "color.h"
#include "nlohmann/json.hpp"
#include "imgui/imgui.h"

using namespace alienorum;

using namespace std;
double global_brightness = default_brightness, sky_mag_shift = 0;
double global_inverse_gamma = 1.0 / default_gamma;
AlienStyle global_style;
bool redlight_mode = false;
std::map<int, RGB3Byte> sky_grad;

double Color::luminance()
{
    return _lum_r_comp * red + _lum_g_comp * green + _lum_b_comp * blue;
}

Color Color::color_from_magnitude_indices(double Vmag, double BV)
{
    return color_from_magnitude_indices(Vmag, BV, BV);
}

Color Color::color_from_magnitude_indices(double Vmag, double BV, double VR)
{
    Color c;
    double BV_literal = BV+bv_correction, VR_literal = VR+bv_correction,            // TODO:
        brightness = global_brightness * pow(magnbase, -Vmag) * 128;

    c.green = 1;
    c.blue = pow(magnbase, -BV_literal);
    c.red = pow(magnbase,   VR_literal);

    double lum = c.luminance(), invlum = 1.0 / lum;
    c.red   *= brightness * invlum;
    c.green *= brightness * invlum;
    c.blue  *= brightness * invlum;

    return c;
}

RGB3Byte Color::rgb_from_color(Color c, double mult)
{
    RGB3Byte result;
    int red, green, blue;

    if (mult < 0)
    {
        // Normalize
        mult = 1.0 / fmax(c.red, fmax(c.green, c.blue));
    }

    red   = 255 * fmin(1.0, pow(c.red   * mult, global_inverse_gamma));
    green = 255 * fmin(1.0, pow(c.green * mult, global_inverse_gamma));
    blue  = 255 * fmin(1.0, pow(c.blue  * mult, global_inverse_gamma));

    if (redlight_mode)
    {
        red = min(255, (int)(red + 0.5 * green + 0.3 * blue));
        green *= 0.333;
        blue *= 0.333;
    }

    result.r = std::min(255, red);
    result.g = std::min(255, green);
    result.b = std::min(255, blue);
    return result;
}

ImU32 Color::black_to_transparent(ImU32 input)
{
    int a = input >> 24;
    int r = input&0xff, g = (input&0xff00)>>8, b = (input&0xff0000)>>16;
    if (whtbkgd)
    {
        double least = fmax(fmax(r,g),b);
        if (least > 0)
        {
            double range = 255.0 - least;
            double normalize = 255.0 / range;
            a *= range/255;
            r = fmax(0, 255 - (255-r) * normalize);
            g = fmax(0, 255 - (255-g) * normalize);
            b = fmax(0, 255 - (255-b) * normalize);
        }
    }
    else
    {
        double highest = fmax(fmax(r,g),b);

        // Black has no channel to normalize against, and turning black into nothing is the whole
        // job of this function, so answer that directly. Left to the arithmetic below it divides
        // 255 by zero, and the infinity that comes back multiplies a zero channel into a NaN --
        // which is not clamped by the cast to int, it becomes INT_MIN, and INT_MIN packs itself
        // back into the returned color as a half-opaque black. That is a hole rather than a
        // colour: alpha 128 with no light in it, painted over whatever was behind it and
        // interpolated across every triangle the vertex belongs to.
        if (!(highest > 0)) return 0;

        if (highest < 255)
        {
            double normalize = 255.0 / highest;
            a *= (highest/255);
            r *= normalize;
            g *= normalize;
            b *= normalize;
        }
    }
    return (a<<24) + (b<<16) + (g<<8) + r;
}

ImU32 alienorum::Color::adjust_alpha(ImU32 input, double tgtv)
{
    int r = input&0xff, g = (input&0xff00)>>8, b = (input&0xff0000)>>16;
    double lum = (_lum_r_comp * r + _lum_g_comp * g + _lum_b_comp * b) * 0.00392;
    if (whtbkgd) lum = 1.0 - lum;
    double new_alpha = tgtv / lum;
    int a = fmax(0, fmin(255, new_alpha*255));
    return (a<<24) + (b<<16) + (g<<8) + r;
}

// WCAG 2.x relative luminance of one linear-light channel, c in [0,1]. See
// https://www.w3.org/TR/WCAG21/#dfn-relative-luminance
static double wcag_channel(double c)
{
    return (c <= 0.03928) ? c/12.92 : pow((c+0.055)/1.055, 2.4);
}

static double wcag_luminance(double r, double g, double b)
{
    return 0.2126*wcag_channel(r) + 0.7152*wcag_channel(g) + 0.0722*wcag_channel(b);
}

static double wcag_contrast(double La, double Lb)
{
    double hi = fmax(La, Lb), lo = fmin(La, Lb);
    return (hi+0.05) / (lo+0.05);
}

// Contrast ratio of (r,g,b) alpha-blended at `alpha` over a solid background of luminance
// bg_lum (0 for black, 1 for white -- the only backgrounds this app composites onto) against
// that same background.
static double wcag_blended_contrast(double r, double g, double b, double alpha, double bg_lum)
{
    double cr = alpha*r + (1-alpha)*bg_lum, cg = alpha*g + (1-alpha)*bg_lum, cb = alpha*b + (1-alpha)*bg_lum;
    return wcag_contrast(wcag_luminance(cr, cg, cb), bg_lum);
}

ImU32 alienorum::Color::ensure_wcag_contrast(ImU32 input, bool white_bg, double min_ratio, double max_ratio, bool allow_hue_shift)
{
    int a = (input>>24)&0xff, b = (input&0xff0000)>>16, g = (input&0xff00)>>8, r = input&0xff;
    double rf = r/255.0, gf = g/255.0, bf = b/255.0, af = a/255.0;
    double bg_lum = white_bg ? 1.0 : 0.0;

    double ratio = wcag_blended_contrast(rf, gf, bf, af, bg_lum);
    if (ratio >= min_ratio && (max_ratio <= 0 || ratio <= max_ratio)) return input;

    double new_alpha = af;
    if (ratio < min_ratio)
    {
        // Raise alpha toward opaque until the blend clears min_ratio.
        if (wcag_blended_contrast(rf, gf, bf, 1.0, bg_lum) < min_ratio)
        {
            new_alpha = 1.0;
        }
        else
        {
            double lo = af, hi = 1.0;
            for (int i=0; i<32; i++)
            {
                double mid = 0.5*(lo+hi);
                if (wcag_blended_contrast(rf, gf, bf, mid, bg_lum) < min_ratio) lo = mid; else hi = mid;
            }
            new_alpha = hi;
        }

        if (allow_hue_shift && wcag_blended_contrast(rf, gf, bf, new_alpha, bg_lum) < min_ratio)
        {
            // Opaque still is not enough -- this hue itself reads too close to the
            // background. Push it toward the opposite extreme, preserving its direction,
            // until the shifted color alone clears min_ratio.
            double ext = white_bg ? 0.0 : 1.0;
            double tlo = 0.0, thi = 1.0;
            for (int i=0; i<32; i++)
            {
                double mid = 0.5*(tlo+thi);
                double tr = rf+(ext-rf)*mid, tg = gf+(ext-gf)*mid, tb = bf+(ext-bf)*mid;
                if (wcag_blended_contrast(tr, tg, tb, 1.0, bg_lum) < min_ratio) tlo = mid; else thi = mid;
            }
            rf += (ext-rf)*thi; gf += (ext-gf)*thi; bf += (ext-bf)*thi;
            new_alpha = 1.0;
        }
    }
    else
    {
        // Too harsh: dial alpha back toward the background until it reads as subtle again,
        // without letting it fade past a visibility floor.
        const double alpha_floor = 0.08;
        double lo = 0.0, hi = af;
        for (int i=0; i<32; i++)
        {
            double mid = 0.5*(lo+hi);
            if (wcag_blended_contrast(rf, gf, bf, mid, bg_lum) > max_ratio) hi = mid; else lo = mid;
        }
        new_alpha = fmax(alpha_floor, lo);
    }

    int nr = (int)round(fmax(0.0, fmin(255.0, rf*255)));
    int ng = (int)round(fmax(0.0, fmin(255.0, gf*255)));
    int nb = (int)round(fmax(0.0, fmin(255.0, bf*255)));
    int na = (int)round(fmax(0.0, fmin(255.0, new_alpha*255)));
    return (na<<24) + (nb<<16) + (ng<<8) + nr;
}

json Color::to_json()
{
    return
    {
        {"red", red},
        {"green", green},
        {"blue", blue}
    };
}

bool Color::from_json(json j)
{
    try { j.at("red").get_to(red); } catch (...) { ; }
    try { j.at("green").get_to(green); } catch (...) { ; }
    try { j.at("blue").get_to(blue); } catch (...) { ; }
    return true;
}

void alienorum::Color::normalize(double level)
{
    double m = fmax(fmax(red, green), blue);
    double invm = 1.0 / m;
    double mult = invm * level;
    red   *= mult;
    green *= mult;
    blue  *= mult;
}

void alienorum::Color::saturate(double saturation)
{
    double lum = luminance();
    red   = lum + saturation * (red   - lum);
    green = lum + saturation * (green - lum);
    blue  = lum + saturation * (blue  - lum);
}

void set_gamma(double new_gamma)
{
    global_inverse_gamma = 1.0 / new_gamma;
}

double get_gamma()
{
    return 1.0 / global_inverse_gamma;
}

void rgb_apply_redlight(float *r, float *g, float *b)
{
    *r += 0.5 * (*g) + 0.3 * (*b);
    if (*r > 0xFF) *r = 0xFF;
    *g /= 3;
    *b /= 3;
}

uint32_t rgba_apply_redlight(uint32_t i)
{
    if (!redlight_mode) return i;
    float r = (i & 0xFF), g = (i & 0xFF00) >> 8, b = (i & 0xFF0000) >> 16;
    rgb_apply_redlight(&r, &g, &b);
    return uint32_t((i & 0xFF000000) + (uint32_t)r + ((uint32_t)g << 8) + ((uint32_t)b << 16));
}

ImVec4 rgba_apply_redlight(ImVec4 i)
{
    if (!redlight_mode) return i;
    float r = i.x, g = i.y, b = i.z, a = i.w;
    rgb_apply_redlight(&r, &g, &b);
    return ImVec4(r, g, b, a);
}

void apply_default_style()
{
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    // Lines without rgba_apply_redlight() added haven't been changed yet from ImGui defaults.
    colors[ImGuiCol_Text]                   = redlight_mode ? ImVec4(0.90f, 0.05f, 0.00f, 1.00f) : global_style.text_color;
    colors[ImGuiCol_TextDisabled]           = redlight_mode ? ImVec4(0.60f, 0.05f, 0.00f, 1.00f) : global_style.graytext_color;
    colors[ImGuiCol_WindowBg]               = rgba_apply_redlight(global_style.window_bg_color);
    colors[ImGuiCol_ChildBg]                = rgba_apply_redlight(global_style.input_bg_color);
    colors[ImGuiCol_PopupBg]                = rgba_apply_redlight(global_style.window_bg_color);
    colors[ImGuiCol_Border]                 = rgba_apply_redlight(global_style.border_color);
    colors[ImGuiCol_BorderShadow]           = rgba_apply_redlight(global_style.border_shadow);
    colors[ImGuiCol_FrameBg]                = rgba_apply_redlight(global_style.frame_bg_color);
    colors[ImGuiCol_FrameBgHovered]         = rgba_apply_redlight(global_style.frame_bg_hover);
    colors[ImGuiCol_FrameBgActive]          = rgba_apply_redlight(global_style.frame_bg_active);
    colors[ImGuiCol_TitleBg]                = rgba_apply_redlight(global_style.title_bg_color);
    colors[ImGuiCol_TitleBgActive]          = rgba_apply_redlight(global_style.title_bg_active);
    colors[ImGuiCol_TitleBgCollapsed]       = rgba_apply_redlight(global_style.title_bg_collapsed);
    colors[ImGuiCol_MenuBarBg]              = rgba_apply_redlight(global_style.menu_bg_color);
    colors[ImGuiCol_ScrollbarBg]            = rgba_apply_redlight(global_style.scroll_bg_color);
    colors[ImGuiCol_ScrollbarGrab]          = rgba_apply_redlight(global_style.scroll_grab_color);
    colors[ImGuiCol_ScrollbarGrabHovered]   = rgba_apply_redlight(global_style.scroll_hov_color);
    colors[ImGuiCol_ScrollbarGrabActive]    = rgba_apply_redlight(global_style.scroll_acv_color);
    colors[ImGuiCol_CheckMark]              = rgba_apply_redlight(global_style.checkmark_color);
    colors[ImGuiCol_SliderGrab]             = rgba_apply_redlight(global_style.slider_color);
    colors[ImGuiCol_SliderGrabActive]       = rgba_apply_redlight(global_style.slider_acv_color);
    colors[ImGuiCol_Button]                 = rgba_apply_redlight(global_style.button_color);
    colors[ImGuiCol_ButtonHovered]          = rgba_apply_redlight(global_style.button_hov_color);
    colors[ImGuiCol_ButtonActive]           = rgba_apply_redlight(global_style.button_acv_color);
    colors[ImGuiCol_Header]                 = ImVec4(0.40f, 0.40f, 0.90f, 0.45f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.53f, 0.53f, 0.87f, 0.80f);
    colors[ImGuiCol_Separator]              = rgba_apply_redlight(global_style.sep_color);
    colors[ImGuiCol_SeparatorHovered]       = rgba_apply_redlight(global_style.sep_color);
    colors[ImGuiCol_SeparatorActive]        = rgba_apply_redlight(global_style.sep_color);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.78f, 0.82f, 1.00f, 0.60f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.78f, 0.82f, 1.00f, 0.90f);
    colors[ImGuiCol_InputTextCursor]        = rgba_apply_redlight(global_style.text_cursor_color);
    colors[ImGuiCol_TabHovered]             = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_Tab]                    = colors[ImGuiCol_TitleBg];
    colors[ImGuiCol_TabSelected]            = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TabSelectedOverline]    = colors[ImGuiCol_FrameBgHovered];
    colors[ImGuiCol_TabDimmed]              = rgba_apply_redlight(global_style.grayed_color);
    colors[ImGuiCol_TabDimmedSelected]      = rgba_apply_redlight(global_style.grayed_brighter_color);
    colors[ImGuiCol_TabDimmedSelectedOverline] = rgba_apply_redlight(global_style.grayed_brighter_color);
    colors[ImGuiCol_PlotLines]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.27f, 0.27f, 0.38f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.45f, 1.00f);   // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);   // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    colors[ImGuiCol_TextLink]               = colors[ImGuiCol_HeaderActive];
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
    colors[ImGuiCol_TreeLines]              = colors[ImGuiCol_Border];
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_DragDropTargetBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_UnsavedMarker]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_NavCursor]              = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

#define _dbg_veg_color 0

double col_frand(std::mt19937 *rng, double min, double max)
{
    std::uniform_real_distribution<double> dist(min, max);
    return dist(*rng);
}

int col_rand(std::mt19937 *rng)
{
    std::uniform_int_distribution<int> dist;
    return dist(*rng);
}

RGB3Byte generate_vegetation_color(std::mt19937 *rng)
{
    // Don't assume alien vegetation is green!

    // Define a longpass wavelength somewhere in the UV or blue
    double lp = col_frand(rng, 300, 490);

    // Define up to 4 absorption bands (Earth vegetation has two, red and blue)
    int nbands = 1 + (col_rand(rng)%3);
    #if _dbg_veg_color
    std::cout << "Generating vegetation color from longpass " << lp << " plus " << nbands << " absorption bands." << std::endl;
    #endif

    double bandl[nbands], bandi[nbands], maxband=0;
    int i;
    for (i=0; i<nbands; i++)
    {
        // Favor shorter wavelengths, but allow up to near infrared.
        // Make the longest band the most intense to allow for a strong red edge.
        bandl[i] = (i?bandl[i-1]:lp)*1.03 + col_frand(rng, 5, col_frand(rng, 50, 250));
        bandi[i] = col_frand(rng, i?bandi[i-1]:0.1, 1);
        if (bandi[i] > maxband) maxband = bandi[i];

        #if _dbg_veg_color
        std::cout << "Band " << (i+1) << ": " << bandl[i] << "nm, intensity=" << bandi[i] << std::endl;
        #endif
    }

    // Make sure at least one of the bands is intense.
    if (maxband < 0.8)
    {
        double normalize = col_frand(rng, 0.8, 1.0);
        double multiply = normalize / maxband;
        for (i=0; i<nbands; i++) bandi[i] *= multiply;

        #if _dbg_veg_color
        std::cout << "Bands normalized to " << normalize << std::endl;
        #endif
    }

    // Start with a basic straw color.
    double r=0.89, g=0.85, b=0.44;
    double probability_scale = 25.0 / probability_density_function(lp, lp, 29);

    #if _dbg_veg_color
    std::cout << "Reflectance spectrum of vegetation:" << std::endl;
    #endif

    // Sample the spectrum to determine total rgb absorption.
    double spectrum[1000];
    int l;
    double rtot=0, gtot=0, btot=0;
    for (l=250; l<1000; l++)
    {
        probability_scale = 25.0 / probability_density_function(lp, lp, 29);
        spectrum[l] = (lp<l) ? (1.0 / (1.0 + probability_scale * probability_density_function(l, lp, 29))) : 0;

        for (i=0; i<nbands; i++)
        {
            double halfwidth = 0.04*bandl[i];
            probability_scale = 10.0 / probability_density_function(bandl[i], bandl[i], halfwidth);
            spectrum[l] *= (1.0 / (1.0 + bandi[i] * probability_scale * probability_density_function(l, bandl[i], halfwidth)));
        }

        if (l >= 400 && l < 510) btot += spectrum[l];
        if (l >= 480 && l < 600) gtot += spectrum[l];
        if (l >= 560 && l < 710) rtot += spectrum[l];
    }

    #if _dbg_veg_color
    for (i=20; i>=0; i--)
    {
        for (l=250; l<1000; l+=5)
        {
            if (!l) std::cout << "\x1b[38;5;99m";
            else if (l == 400) std::cout << "\x1b[38;5;21m";
            else if (l == 480) std::cout << "\x1b[38;5;51m";
            else if (l == 510) std::cout << "\x1b[38;5;46m";
            else if (l == 570) std::cout << "\x1b[38;5;226m";
            else if (l == 590) std::cout << "\x1b[38;5;208m";
            else if (l == 610) std::cout << "\x1b[38;5;196m";
            else if (l == 700) std::cout << "\x1b[38;5;89m";

            if (spectrum[l] >= 0.05*i) std::cout << "*";
            else std::cout << " ";
        }
        std::cout << "\x1b[0m" << std::endl;
    }
    #endif

    rtot /= 150;
    gtot /= 120;
    btot /= 110;

    #if _dbg_veg_color
    std::cout << "Bands multiply rgb by " << std::setprecision(3) << rtot << "," << gtot << "," << btot << std::endl;
    #endif

    r *= rtot; g *= gtot; b *= btot;

    return { (unsigned char)(r*255), (unsigned char)(g*255), (unsigned char)(b*255) };
}

bool AlienStyle::load(std::string theme)
{
    std::string filename = "assets" _FILESLASH "themes.json";
    fstream fs(filename.c_str(), std::ios::in);
    if (!fs) return false;

    int i, n;
    float r, g, b, a;
    json j;

    fs >> j;
    n = j.size();
    if (!n) return false;

    themes.clear();
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        themes.push_back(it.key());
    }

    for (i=0; i<n; i++)
    {
        if (!strcasecmp(theme.c_str(), themes[i].c_str()))
        {
            try
            {
                j = j.at(themes[i].c_str());

                try
                {
                    json l = j.at("cursor");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    cursor_color1 = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    float r1 = fmax(0, fmin(1.0, 1.666*r + 0.2*b - 0.2*g)),
                        g1 = fmax(0, fmin(1.0, 1.666*g + 0.2*r - 0.2*b)),
                        b1 = fmax(0, fmin(1.0, 1.666*b + 0.2*g - 0.2*r));
                    cursor_color2 = IM_COL32(255*r1, 255*g1, 255*b1, 255*a);
                    r = fmax(0, fmin(1.0, 1.666*r1 + 0.2*b1 - 0.2*g1));
                    g = fmax(0, fmin(1.0, 1.666*g1 + 0.2*r1 - 0.2*b1));
                    b = fmax(0, fmin(1.0, 1.666*b1 + 0.2*g1 - 0.2*r1));
                    cursor_color3 = IM_COL32(255*r, 255*g, 255*b, 255*a);

                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b);
                    double normalize = 1.0 / fmax(fmax(r, g), b);
                    r *= normalize;
                    g *= normalize;
                    b *= normalize;

                    text_cursor_color = ImVec4(r, g, b, 1.00f);
                }
                catch (...) { ; }
                try
                {
                    json l = j.at("grid");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    grid_color = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    grid_color_brighter = IM_COL32(255*r, 255*g, 255*b, min(255, (int)(255*a*1.5)));
                    sep_color = ImVec4(0.8*r, 0.8*g, 0.8*b, 0.60f);
                }
                catch (...) { ; }
                try
                {
                    json l = j.at("ecliptic");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    ecliptic_color = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    input_bg_color = ImVec4(0.1*r, 0.1*g, 0.1*b, 0.00f);
                    window_bg_color = ImVec4(0.1*r, 0.1*g, 0.1*b, 0.97);
                    title_bg_color = ImVec4(0.5*r, 0.5*g, 0.5*b, 0.93f);
                    title_bg_active = ImVec4(0.6*r, 0.6*g, 0.6*b, 0.94f);
                    title_bg_collapsed = ImVec4(0.8*r, 0.8*g, 0.8*b, 0.81f);
                }
                catch (...) { ; }
                try
                {
                    json l = j.at("consline");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    consline_color = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    border_color = ImVec4(0.5*r, 0.5*g, 0.5*b, 0.50f);
                    menu_bg_color = ImVec4(0.5*r, 0.5*g, 0.5*b, 0.93f);
                }
                catch (...) { ; }
                try
                {
                    json l = j.at("conslbl");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    conslbl_color           = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    grayed_color            = ImVec4(0.35*r, 0.37*g, 0.67*b, 1.00f);
                    grayed_brighter_color   = ImVec4(0.41*r, 0.44*g, 0.81*b, 1.00f);
                }
                catch (...) { ; }
                try
                {
                    json l = j.at("selected");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    selected_color = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    selected_orbit_color = IM_COL32(255*r, 255*g, 255*b, 85*a);
                }
                catch (...) { ; }
                try
                {
                    json l = j.at("label");
                    l.at(0).get_to(r); l.at(1).get_to(g); l.at(2).get_to(b); l.at(3).get_to(a);
                    objlbl_color = IM_COL32(255*r, 255*g, 255*b, 255*a);
                    button_color = ImVec4(0.37*r, 0.37*g, 0.37*b, 0.62f);
                    button_hov_color = ImVec4(0.44*r, 0.44*g, 0.44*b, 0.62f);
                    button_acv_color = ImVec4(0.53*r, 0.53*g, 0.53*b, 0.62f);
                }
                catch (...) { ; }

                return true;
            }
            catch (...)
            {
                return false;
            }
        }
    }
    return false;
}

ImVec2 alienorum::Cloud::find_draw_coordinates(double planet_radius)
{
    Point viewer_location = Point::from_ra_dec(viewer_lon, viewer_lat, planet_radius);
    Point cloud_location = Point::from_ra_dec(longitude, latitude, core_dist);
    Point rel = cloud_location - viewer_location;
    Rotation viewer_plane = align_points_3d(viewer_location, yaxis, center);
    Point relrot = rotate3D(rel, center, viewer_plane.v, viewer_plane.a);
    distance = rel.magnitude();

    Cartesian2D cart(relrot, azimuth, altitude, zoom);

    ImGuiIO& io = ImGui::GetIO();
    double dispcx = io.DisplaySize.x * 0.5, dispcy = io.DisplaySize.y * 0.5;

    return ImVec2(dispcx + cart.x * io.DisplaySize.x, dispcy + cart.y * io.DisplaySize.x);
}

void alienorum::Cloud::draw(double planet_radius)
{
    ImVec2 drawcen = find_draw_coordinates(planet_radius);
    if (drawcen.x < -1000 || drawcen.y < -500) return;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list) return;

    ImGuiIO& io = ImGui::GetIO();
    double dw = width / distance * io.DisplaySize.x * zoom, dh = height / distance * io.DisplaySize.x * zoom;

    // Seed pseudo-randomness based on position so the cloud shape is stable across frames
    mtx.lock();
    // std::srand(seed);
    mtx.unlock();

    std::vector<CloudParticle> base_layer;
    std::vector<CloudParticle> core_layer;
    std::vector<CloudParticle> highlight_layer;

    // 1. Generate the Darker, Flatter Base Layer (Ground Perspective)
    int base_count = 15;
    for (int i = 0; i < base_count; ++i)
    {
        float t = (float)i / (float)(base_count - 1);
        // Distribute horizontally across the cloud width
        float off_x = (t - 0.5f) * dw * 0.8f;
        // Keep the base mostly flat along the bottom
        float off_y = (dh * 0.25f) + ((rand() % 100) / 100.0f - 0.5f) * (dh * 0.1f);

        float rad_x = (dw * 0.25f) + ((rand() % 100) / 100.0f) * (dw * 0.15f);
        float rad_y = (dh * 0.2f) + ((rand() % 100) / 100.0f) * (dh * 0.1f);

        // Base color: Darker grey-blue shadow with low opacity (alpha 40-70)
        int alpha = 40 + (rand() % 30);
        ImU32 col = IM_COL32((int)(0.55*color.r), (int)(0.61*color.g), (int)(0.69*color.b), alpha);

        base_layer.push_back({{off_x, off_y}, {rad_x, rad_y}, col, 0.0f});
    }

    // 2. Generate the Dense Core Layer (Builds volume upwards)
    int core_count = 35;
    for (int i = 0; i < core_count; ++i)
    {
        // Concentrated toward the center and bulging upward
        float off_x = ((rand() % 100) / 100.0f - 0.5f) * dw * 0.7f;
        float off_y = ((rand() % 100) / 100.0f - 0.6f) * dh * 0.4f; // higher up than base

        float rad_x = (dw * 0.2f) + ((rand() % 100) / 100.0f) * (dw * 0.15f);
        float rad_y = (dh * 0.25f) + ((rand() % 100) / 100.0f) * (dh * 0.15f);

        // Core color: Soft greyish-white (alpha 50-90)
        int alpha = 25 + (rand() % 40);
        ImU32 col = IM_COL32((int)(0.84*color.r), (int)(0.87*color.g), (int)(0.90*color.b), alpha);

        core_layer.push_back({{off_x, off_y}, {rad_x, rad_y}, col, 0.0f});
    }

    // 3. Generate the Top Highlights (Sunlit edges)
    int highlight_count = 25;
    for (int i = 0; i < highlight_count; ++i)
    {
        // Biased toward the top peaks of the cloud
        float off_x = ((rand() % 100) / 100.0f - 0.5f) * dw * 0.6f;
        float off_y = -dh * 0.3f - ((rand() % 100) / 100.0f) * dh * 0.3f; 

        // Smaller, rounder puffs for the "silver lining" effect
        float rad_x = (dw * 0.12f) + ((rand() % 100) / 100.0f) * (dw * 0.1f);
        float rad_y = (dh * 0.12f) + ((rand() % 100) / 100.0f) * (dh * 0.1f);

        // Highlight color: Warm bright white/cream, slightly more opaque (alpha 80-130)
        int alpha = 40 + (rand() % 50);
        ImU32 col = IM_COL32((int)(0.98*color.r), (int)(0.98*color.g), (int)(0.96*color.b), alpha);

        highlight_layer.push_back({{off_x, off_y}, {rad_x, rad_y}, col, 0.0f});
    }

    // --- DRAWING PROGRESSION ---
    // Render back-to-front to allow alpha blending to create depth

    // Draw Bases
    for (const auto& p : base_layer)
    {
        draw_list->AddEllipseFilled(ImVec2(drawcen.x + p.offset.x, drawcen.y + p.offset.y), p.radius, p.color, p.rotation, 24);
    }

    // Draw Core Volume
    for (const auto& p : core_layer)
    {
        draw_list->AddEllipseFilled(ImVec2(drawcen.x + p.offset.x, drawcen.y + p.offset.y), p.radius, p.color, p.rotation, 24);
    }

    // Draw Sunlit Top Caps
    for (const auto& p : highlight_layer)
    {
        draw_list->AddEllipseFilled(ImVec2(drawcen.x + p.offset.x, drawcen.y + p.offset.y), p.radius, p.color, p.rotation, 24);
    }

    // 4. Soften the Ground Base Line
    // Adding a few thin, faint horizontal lines near the bottom blends the cloud into the atmospheric haze
    int haze_lines = 4;
    for (int i = 0; i < haze_lines; ++i)
    {
        float y_pos = drawcen.y + (dh * 0.25f) + (i * 3.0f);
        ImVec2 p1(drawcen.x - (dw * 0.45f), y_pos);
        ImVec2 p2(drawcen.x + (dw * 0.45f), y_pos);

        // Very faint, matching the sky/base shadow mix
        ImU32 haze_col = IM_COL32(150, 165, 185, 15 - (i * 3)); 
        draw_list->AddLine(p1, p2, haze_col, 4.0f);
    }
}

void alienorum::RGB3Byte::invert_luminance()
{
    double least = fmin(fmin(r, g), b);
    r -= least;
    g -= least;
    b -= least;
}
