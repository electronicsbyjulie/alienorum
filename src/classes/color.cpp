#include <math.h>
#include <algorithm>
#include <iostream>
#include "point.h"
#include "color.h"

using namespace std;
double global_brightness = default_brightness;
double global_inverse_gamma = 1.0 / default_gamma;
bool redlight_mode = false;

double drawblxscalex, drawblxscaley;
int *bx_cache = new int[MAX_CELOBJS], *by_cache = new int[MAX_CELOBJS];

double Color::luminance()
{
    return 0.29 * red + 0.57 * green + 0.14 * blue;
}

Color Color::color_from_magnitude_indices(double Vmag, double BV)
{
    Color c;
    double BV_literal = BV+bv_correction, brightness = global_brightness * pow(magnbase, -Vmag) * 128;

    c.green = 1;
    c.blue = pow(magnbase, -BV_literal);
    c.red = pow(magnbase,  BV_literal);

    double lum = c.luminance(), invlum = 1.0 / lum;
    c.red   *= brightness * invlum;
    c.green *= brightness * invlum;
    c.blue  *= brightness * invlum;

    // Literal B-V indices look too saturated on the screen.
    // c.red = (c.red + c.green) / 2;
    // c.blue = fmax(c.blue, (c.blue + c.green) / 2);

    return c;
}

RGB Color::rgb_from_color(Color c, double bloom_radius)
{
    RGB result;
    int red, green, blue;

    double circ = 2.0 * M_PI * bloom_radius;
    double invcirc = 1.0 / circ;

    red   = 255 * fmin(1.0, pow(c.red   * invcirc, global_inverse_gamma));
    green = 255 * fmin(1.0, pow(c.green * invcirc, global_inverse_gamma));
    blue  = 255 * fmin(1.0, pow(c.blue  * invcirc, global_inverse_gamma));

    if (redlight_mode)
    {
        red = min(255, (int)(red + 0.5 * green + 0.3 * blue));
        green /= 3;
        blue /= 3;
    }

    result.r = max(0, red);
    result.g = max(0, green);
    result.b = max(0, blue);
    return result;
}

ImU32 Color::black_to_transparent(ImU32 input)
{
    int a = input >> 24;
    int r = input&0xff, g = (input&0xff00)>>8, b = (input&0xff0000)>>16;
    double highest = fmax(fmax(r,g),b);
    if (highest < 255)
    {
        double normalize = 255.0 / highest;
        a *= (highest/255);
        r *= normalize;
        g *= normalize;
        b *= normalize;
    }
    return (a<<24) + (b<<16) + (g<<8) + r;
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

void set_gamma(double new_gamma)
{
    global_inverse_gamma = 1.0 / new_gamma;
}

double get_gamma()
{
    return 1.0 / global_inverse_gamma;
}

__uint32_t rgba_apply_redlight(__uint32_t i)
{
    if (!redlight_mode) return i;
    __uint32_t r = (i & 0xFF), g = (i & 0xFF00) >> 8, b = (i & 0xFF0000) >> 16;
    r += 0.5 * g + 0.3 * b;
    if (r > 0xFF) r = 0xFF;
    g /= 3;
    b /= 3;
    return __uint32_t((i & 0xFF000000) + r + (g << 8) + (b << 16));
}
