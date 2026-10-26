#include <math.h>
#include <algorithm>
#include <array>
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
#include "star.h"
#include "wdcolor.h"

using namespace alienorum;

using namespace std;
double global_brightness = default_brightness, sky_mag_shift = 0;
double global_inverse_gamma = 1.0 / default_gamma;
AlienStyle global_style;
bool redlight_mode = false;
std::map<int, RGB3> sky_grad;

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
    return color_from_magnitude_indices(Vmag, BV, VR, global_brightness, bv_correction);
}

Color Color::color_from_magnitude_indices(double Vmag, double BV, double VR,
    double brightness_scale, double color_correction)
{
    Color c;
    double BV_literal = BV+color_correction, VR_literal = VR+color_correction,            // TODO:
        brightness = brightness_scale * pow(magnbase, -Vmag) * 128;

    c.green = 1;
    c.blue = pow(magnbase, -BV_literal);
    c.red = pow(magnbase,   VR_literal);

    double lum = c.luminance(), invlum = 1.0 / lum;
    c.red   *= brightness * invlum;
    c.green *= brightness * invlum;
    c.blue  *= brightness * invlum;

    return c;
}

Color Color::color_from_star(double Vmag, Star *star, double fallback_BV)
{
    Color photometric = color_from_magnitude_indices(Vmag, fallback_BV);
    Color linear;
    if (!star || !star->resolve_wd_color(linear)) return photometric;
    const double yellowing = fallback_BV - star->BV_color;
    linear.red *= std::pow(magnbase, yellowing);
    linear.blue *= std::pow(magnbase, -yellowing);
    linear.normalize(1);
    // Adapt the paper's sRGB to the renderer's existing power-gamma color domain.
    // At the default gamma the final encoding reproduces the published sRGB hue.
    Color published(std::pow(wd_srgb_encode(linear.red), default_gamma),
        std::pow(wd_srgb_encode(linear.green), default_gamma),
        std::pow(wd_srgb_encode(linear.blue), default_gamma));
    const double source_luminance = published.luminance();
    if (!(source_luminance > 0)) return photometric;
    const double scale = photometric.luminance() / source_luminance;
    published.red *= scale;
    published.green *= scale;
    published.blue *= scale;
    return published;
}

RGB3 Color::rgb_from_color(Color c, double mult)
{
    return rgb_from_color(c, mult, global_inverse_gamma, redlight_mode);
}

RGB3 Color::rgb_from_color(Color c, double mult, double inverse_gamma, bool apply_redlight)
{
    RGB3 result;
    int red, green, blue;

    if (mult < 0)
    {
        // Normalize
        mult = 1.0 / fmax(c.red, fmax(c.green, c.blue));
    }

    red   = 255 * fmin(1.0, pow(c.red   * mult, inverse_gamma));
    green = 255 * fmin(1.0, pow(c.green * mult, inverse_gamma));
    blue  = 255 * fmin(1.0, pow(c.blue  * mult, inverse_gamma));

    if (apply_redlight)
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

double alienorum::Color::hue_from_rgb(double r, double g, double b)
{
    // 1. Normalize RGB values to the range [0.0, 1.0]
    r = r / 255.0f;
    g = g / 255.0f;
    b = b / 255.0f;

    // 2. Find the minimum and maximum values
    double cmax = std::max({r, g, b});
    double cmin = std::min({r, g, b});
    double diff = cmax - cmin;

    double hue = 0.0f;

    // 3. If cmax and cmin are equal, the color is a shade of gray (monochrome)
    if (diff == 0.0f)
    {
        return 0.0f; 
    }

    // 4. Calculate Hue based on which channel is the maximum
    if (cmax == r)
    {
        hue = 60.0f * std::fmod(((g - b) / diff), 6.0f);
    } else if (cmax == g)
    {
        hue = 60.0f * (((b - r) / diff) + 2.0f);
    } else if (cmax == b)
    {
        hue = 60.0f * (((r - g) / diff) + 4.0f);
    }

    // 5. Handle negative values to ensure a range of [0, 360)
    if (hue < 0.0f)
    {
        hue += 360.0f;
    }

    return hue * fiftyseventh;
}

double alienorum::Color::sat_from_rgb(double r, double g, double b)
{
    double least = fmin(fmin(r, g), b), most = fmax(fmax(r, g), b);
    if (!most) return 0;
    return (most-least) / most;
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

/**
 * Calculates the shift of magnitude B-V (yellowing) due to the diffusion of Rayleigh.
 * 
 * @param surface_pressure surface pressure in Pascals (Pa).
 * @param zenith_angle_rad apparent angle of object from zenith.
 * @return the resulting B-V magnitude shift.
 */
double alienorum::Color::compute_atmospheric_yellowing(double surface_pressure, double zenith_angle_rad = 0.0)
{
    double surface_pressure_hpa = surface_pressure / 100;

    if (zenith_angle_rad > half_pi) zenith_angle_rad = _pi - zenith_angle_rad;

    // Calculate the mass of air (approximation, valid for z < 60°)
    double airmass = 1.0 / (std::cos(zenith_angle_rad) + 0.025);

    // Constants standards of the atmosphere terrestrial
    const double standard_pressure = 1013.25;
    const double standard_color_excess_per_airmass = 0.14; // (kB - kV)

    // The shift varies linearely with the coloumn of pressure and the mass of air
    double bv_shift = standard_color_excess_per_airmass * (surface_pressure_hpa / standard_pressure) * airmass;

    return bv_shift * 0.2;
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

std::ostream &operator<<(std::ostream &os, const RGB3 &rgb)
{
    os << (int)rgb.r << "," << (int)rgb.g << "," << (int)rgb.b;
    return os;
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

VegetationSpectrum generate_vegetation_spectrum(std::mt19937 *rng, double stellar_temperature)
{
    // Don't assume alien vegetation is green!
    if (!std::isfinite(stellar_temperature) || stellar_temperature <= 0) stellar_temperature = sun_temp;
    stellar_temperature = std::clamp(stellar_temperature, 1000.0, 100000.0);

    // Pigment diversity is procedural; stellar photon flux biases band placement.
    // This models intrinsic reflectance, with illumination applied by the renderer.
    constexpr int samples = 131;
    std::array<double, samples> weights{}, visible_weights{};
    double peak = 0;
    for (int sample = 0; sample < samples; sample++)
    {
        const double wavelength = 350.0 + sample * 5.0;
        weights[sample] = wavelength * blackbody_flux(stellar_temperature, wavelength * 1e-9);
        peak = std::max(peak, weights[sample]);
    }
    for (int sample = 0; sample < samples; sample++)
    {
        weights[sample] = 0.20 + 0.80 * weights[sample] / peak;
        const double wavelength = 350.0 + sample * 5.0;
        if (wavelength >= 400 && wavelength <= 700) visible_weights[sample] = weights[sample];
    }
    std::discrete_distribution<int> band_distribution(weights.begin(), weights.end());
    std::discrete_distribution<int> visible_distribution(visible_weights.begin(), visible_weights.end());

    // Two primary absorption peaks plus zero to three accessory pigments.
    // Retain a visible peak even when the other pigments favor the infrared.
    // Widths and concentrations describe a hypothetical mixture, not measured
    // alien chemistry or a prediction that every organism absorbs all colors.
    VegetationSpectrum spectrum;
    spectrum.red_edge_nm = 700;
    const int nbands = spectrum.band_count = 2 + col_rand(rng) % 4;
    const double concentration = std::exp(col_frand(rng, std::log(0.6), std::log(5.0)));
    auto &centers = spectrum.centers_nm;
    auto &widths = spectrum.widths_nm;
    auto &depths = spectrum.depths;
    for (int band = 0; band < nbands; band++)
    {
        const int sample = band == 0 ? visible_distribution(*rng) : band_distribution(*rng);
        centers[band] = 350.0 + 5.0 * sample + col_frand(rng, -2.5, 2.5);
        widths[band] = band < 2 ? col_frand(rng, 10.0, 25.0) : col_frand(rng, 20.0, 45.0);
        depths[band] = concentration * col_frand(rng, 0.5, 1.5);
        // Place the structural edge beyond the absorption envelope. Its position
        // follows the host-biased pigments, with 700 nm as our leaf-like lower bound.
        spectrum.red_edge_nm = std::max(spectrum.red_edge_nm, centers[band] + 2 * widths[band]);
    }

    // Begin with a neutral scattering surface rather than a prescribed leaf hue.
    // UV protection ends before blue; blue reflectance is controlled by pigments.
    spectrum.visible_scattering = col_frand(rng, 0.15, 0.65);
    spectrum.infrared_scattering = std::min(0.90, spectrum.visible_scattering + 0.25);
    spectrum.uv_edge_nm = col_frand(rng, 320.0, 380.0);
    return spectrum;
}

double VegetationSpectrum::reflectance_at(double wavelength_nm) const
{
    if (!std::isfinite(wavelength_nm) || wavelength_nm <= 0) return 0;
    double optical_depth = 0;
    for (int band = 0; band < std::clamp(band_count, 0, 5); band++)
    {
        if (widths_nm[band] <= 0) continue;
        const double offset = (wavelength_nm - centers_nm[band]) / widths_nm[band];
        optical_depth += std::max(0.0, depths[band]) * std::exp(-0.5 * offset * offset);
    }
    // A phenomenological tissue-scattering edge and an infrared plateau, separate
    // from pigment absorption. This is a leaf-like spectrum, not a heat-balance model.
    const double edge = 1.0 / (1.0 + std::exp((red_edge_nm - wavelength_nm) / std::max(0.1, red_edge_width_nm)));
    const double scattering = visible_scattering + (infrared_scattering - visible_scattering) * edge;
    const double uv_transmission = 1.0 / (1.0 + std::exp((uv_edge_nm - wavelength_nm) / 10.0));
    return std::clamp(scattering * uv_transmission * std::exp(-optical_depth), 0.0, 1.0);
}

RGB3 VegetationSpectrum::visible_rgb() const
{
    double rtot = 0, gtot = 0, btot = 0;
    for (int wavelength = 400; wavelength < 710; wavelength++)
    {
        const double reflectance = reflectance_at(wavelength);
        if (wavelength < 510) btot += reflectance;
        if (wavelength >= 480 && wavelength < 600) gtot += reflectance;
        if (wavelength >= 560) rtot += reflectance;
    }

    // Average the existing RGB response windows without normalizing brightness:
    // broad absorption must remain dark rather than becoming a saturated bright hue.
    return { (unsigned char)(255 * rtot / 150),
             (unsigned char)(255 * gtot / 120),
             (unsigned char)(255 * btot / 110) };
}

RGB3 generate_vegetation_color(std::mt19937 *rng, double stellar_temperature)
{
    const auto family = [](const RGB3 &color)
    {
        // Exclude nearly neutral palettes from the green/purple population balance.
        if (Color::sat_from_rgb(color.r, color.g, color.b) < 0.15) return -1;
        const double hue = Color::hue_from_rgb(color.r, color.g, color.b) / fiftyseventh;
        if (hue >= 80 && hue < 160) return 0;
        if (hue >= 260 && hue < 330) return 1;
        return -1;
    };

    const RGB3 original = generate_vegetation_spectrum(rng, stellar_temperature).visible_rgb();
    const int original_family = family(original);
    if (original_family < 0) return original;

    // Artistic population preference: five purple worlds for every two green.
    // Resample pigment mixtures under the same host spectrum, preserving spectrally
    // generated reflectance instead of tinting RGB. Other hue families pass through.
    const int preferred_family = col_frand(rng, 0, 7) < 5 ? 1 : 0;
    if (original_family == preferred_family) return original;
    for (int attempt = 0; attempt < 128; attempt++)
    {
        const RGB3 candidate = generate_vegetation_spectrum(rng, stellar_temperature).visible_rgb();
        if (family(candidate) == preferred_family) return candidate;
    }
    return original;
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

void alienorum::RGB3::invert_luminance()
{
    double least = fmin(fmin(r, g), b);
    r -= least;
    g -= least;
    b -= least;
}
