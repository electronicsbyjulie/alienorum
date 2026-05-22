
#ifndef _Color
#define _Color

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
    static RGB disc_rgb_from_color(Color c, double disc_radius = 1);                // Disc radius = size in pixels of disk drawn on screen.

    static ImU32 black_to_transparent(ImU32 input);
    json to_json();
    bool from_json(json j);
    Color() {}
    Color(double r, double g, double b) { red=r; green=g; blue=b; }
};

extern double global_brightness;
extern bool redlight_mode;
extern double drawblxscalex, drawblxscaley;
extern int *bx_cache, *by_cache;

void set_gamma(double new_gamma);
double get_gamma();
__uint32_t rgba_apply_redlight(__uint32_t input);

#endif
