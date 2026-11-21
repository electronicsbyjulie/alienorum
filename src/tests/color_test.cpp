#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <random>
#include <cstring>
#include "../classes/color.h"
#include "../classes/star.h"
#include "../classes/wdcolor.h"
#include "../classes/browncolor.h"
#include "../classes/mdwarfcolor.h"

using namespace alienorum;
using json = nlohmann::json;

class RealisticStarColorEnvironment : public ::testing::Environment
{
    void SetUp() override { fancy_star_col = false; }
};
static auto* realistic_star_color_environment = ::testing::AddGlobalTestEnvironment(new RealisticStarColorEnvironment());

TEST(ColorTest, FalseColorAndFireRampsPackEightBitsPerChannel)
{
    const auto pack = [](Color color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(color.red, color.green, color.blue, 1));
    };
    const auto relief = Color::false_from_unity_range;
    EXPECT_EQ(pack(relief(-1)), IM_COL32_BLACK);
    EXPECT_EQ(pack(relief(.1)), IM_COL32(0, 0, 64, 255));
    EXPECT_EQ(pack(relief(.2)), IM_COL32(0, 0, 128, 255));
    EXPECT_EQ(pack(relief(.4)), IM_COL32(0, 170, 170, 255));
    EXPECT_EQ(pack(relief(.6)), IM_COL32(0, 191, 0, 255));
    EXPECT_EQ(pack(relief(.8)), IM_COL32(255, 255, 0, 255));
    EXPECT_EQ(pack(relief(2)), IM_COL32_WHITE);

    const auto fire = Color::fire_from_unity_range;
    EXPECT_EQ(pack(fire(-1)), IM_COL32_BLACK);
    EXPECT_EQ(pack(fire(1.0 / 12)), IM_COL32(0, 0, 64, 255));
    EXPECT_EQ(pack(fire(1.0 / 6)), IM_COL32(0, 0, 128, 255));
    EXPECT_EQ(pack(fire(2.0 / 6)), IM_COL32(170, 0, 170, 255));
    EXPECT_EQ(pack(fire(.5)), IM_COL32(255, 0, 0, 255));
    EXPECT_EQ(pack(fire(4.0 / 6)), IM_COL32(255, 128, 0, 255));
    EXPECT_EQ(pack(fire(5.0 / 6)), IM_COL32(255, 255, 0, 255));
    EXPECT_EQ(pack(fire(2)), IM_COL32_WHITE);
}

TEST(ColorTest, GlareUsesIndependentRedGreenIndex)
{
    struct Restore
    {
        bool saved = fancy_star_col;
        ~Restore() { fancy_star_col = saved; }
    } restore;
    fancy_star_col = true;
    Star star;
    star.BV_color = .8;
    for (double vr : {0.0, .3, 1.2, -.2})
    {
        star.VR_color = vr;
        const double offset = .15;
        Color expected = Color::color_from_magnitude_indices(2, star.BV_color + offset,
            (vr != 0 ? vr : star.BV_color) + offset);
        Color stellar = Color::color_from_star(2, &star, star.BV_color + offset);
        const Color fallback = Color::color_from_planet(2, &star, offset);
        EXPECT_NEAR(stellar.red / stellar.green, expected.red / expected.green, 1e-12);
        EXPECT_NEAR(fallback.red / fallback.green, expected.red / expected.green, 1e-12);
        EXPECT_NEAR(stellar.blue / stellar.green, expected.blue / expected.green, 1e-12);
        EXPECT_NEAR(fallback.blue / fallback.green, expected.blue / expected.green, 1e-12);
        EXPECT_NEAR(stellar.luminance(), expected.luminance(), 1e-12);
    }
}

TEST(ColorTest, MiraDiscAndTextureTintStayNormalizedThroughTheCycle)
{
    Star star;
    strcpy(star.spectral_type, "M5e");
    star.variability_type = "M";
    star.variability_period = 333.8 * oneday;
    star.minmag = 3;
    star.maxmag = 9;
    star.BV_color = 1.4;
    star.VR_color = 1.1;
    double bright_red_green = 0;
    for (double phase : {0.0, .25, .5, .75, 1.0})
    {
        star.apply_mira_pulsation(phase);
        star.apparent_magnitude = 6 - 3 * cos(2 * _pi * phase);
        Color surface = Color::color_from_star(4.2, &star, star.BV_color);
        const RGB3 pixel = Color::rgb_from_color(surface, -1, 1, false);
        EXPECT_EQ(std::max({pixel.r, pixel.g, pixel.b}), 255);
        const Color tint = Color::reflected_daylight(&star, 0);
        EXPECT_DOUBLE_EQ(std::max({tint.red, tint.green, tint.blue}), 1);
        // A red-peak texture texel keeps its full channel in both sphere renderers.
        EXPECT_DOUBLE_EQ(255 * tint.red, 255);
        if (phase == 0) bright_red_green = surface.red / surface.green;
        if (phase == .5) EXPECT_GT(surface.red / surface.green, bright_red_green);
    }
}

TEST(ColorTest, MiraMolecularColorPreservesPhysicalTemperatureAndFlareScaling)
{
    Star star;
    strcpy(star.spectral_type, "M5e");
    star.variability_type = "M";
    star.variability_period = 333.8 * oneday;
    star.minmag = 3;
    star.maxmag = 9;
    star.BV_color = 1.4;
    star.VR_color = 1.1;
    star.apply_mira_pulsation(.5);
    star.apparent_magnitude = 9;
    const double physical_temperature = star.temperature;
    const Color molecular = Color::color_from_star(2, &star, star.BV_color);
    const Color faint = Color::color_from_star(5, &star, star.BV_color);
    EXPECT_NEAR(faint.red / molecular.red, pow(magnbase, -3), 1e-12);
    EXPECT_NEAR(faint.green / molecular.green, pow(magnbase, -3), 1e-12);
    EXPECT_NEAR(faint.blue / molecular.blue, pow(magnbase, -3), 1e-12);
    // Compare the same reference palette with the molecular-layer adjustment inactive.
    star.apparent_magnitude = 0;
    const Color continuum = Color::color_from_star(2, &star, star.BV_color);
    EXPECT_GT(molecular.red / molecular.green, continuum.red / continuum.green);
    EXPECT_DOUBLE_EQ(star.temperature, physical_temperature);
    EXPECT_DOUBLE_EQ(star.BV_color, 1.4);
    EXPECT_DOUBLE_EQ(star.VR_color, 1.1);
}

TEST(ColorTest, WdRenderingPreservesPaperHueAndMagnitudeScaling)
{
    struct RestoreDisplay
    {
        double gamma = global_inverse_gamma;
        bool redlight = redlight_mode;
        ~RestoreDisplay()
        {
            global_inverse_gamma = gamma;
            redlight_mode = redlight;
        }
    } restore;
    global_inverse_gamma = 1.0 / default_gamma;
    redlight_mode = false;
    Star s;
    strcpy(s.spectral_type, "DA2");
    s.temperature = 10000;
    s.BV_color = 0;
    Color rendered = Color::color_from_star(4.2, &s, s.BV_color);
    Color old = Color::color_from_magnitude_indices(4.2, s.BV_color);
    EXPECT_NEAR(rendered.luminance(), old.luminance(), old.luminance() * 1e-12);
    RGB3 rgb = Color::rgb_from_color(rendered, -1);
    EXPECT_NEAR(rgb.r, 0xc9, 1);
    EXPECT_NEAR(rgb.g, 0xda, 1);
    EXPECT_NEAR(rgb.b, 0xff, 1);
    s.WD = "WD1328+648";
    s.gaia_wd_name = "WDJ133001.17+643523.69";
    rendered = Color::color_from_star(4.2, &s, s.BV_color);
    rgb = Color::rgb_from_color(rendered, -1);
    EXPECT_NEAR(rgb.r, 0xdd, 1);
    EXPECT_NEAR(rgb.g, 0xff, 1);
    EXPECT_NEAR(rgb.b, 0xb8, 1);
    const Color reddened = Color::color_from_star(4.2, &s, s.BV_color + 0.5);
    EXPECT_GT(reddened.red / reddened.green, rendered.red / rendered.green);
    EXPECT_LT(reddened.blue / reddened.green, rendered.blue / rendered.green);
    EXPECT_DOUBLE_EQ(s.BV_color, 0);
}

TEST(ColorTest, UnsupportedStarsKeepTheirExistingColorConversion)
{
    Star s;
    s.temperature = 6000;
    s.BV_color = 0.6;
    for (const char *type : {"DQ", "DZ", "DC"})
    {
        strcpy(s.spectral_type, type);
        Color before = Color::color_from_magnitude_indices(5, s.BV_color);
        Color after = Color::color_from_star(5, &s, s.BV_color);
        EXPECT_DOUBLE_EQ(after.red, before.red);
        EXPECT_DOUBLE_EQ(after.green, before.green);
        EXPECT_DOUBLE_EQ(after.blue, before.blue);
    }
}

// =====================================================================
// RGB3 Tests
// =====================================================================

TEST(RGB3ByteTest, InitializationAndLuminance)
{
    RGB3 black;
    EXPECT_EQ(black.r, 0);
    EXPECT_EQ(black.g, 0);
    EXPECT_EQ(black.b, 0);
    EXPECT_DOUBLE_EQ(black.luminance(), 0.0);

    RGB3 white(255, 255, 255);
    EXPECT_EQ(white.r, 255);
    EXPECT_EQ(white.g, 255);
    EXPECT_EQ(white.b, 255);
    
    // Luminance = 0.29*255 + 0.56*255 + 0.15*255 = 255.0
    EXPECT_DOUBLE_EQ(white.luminance(), 255.0);
    
    RGB3 red_only(100, 0, 0);
    EXPECT_DOUBLE_EQ(red_only.luminance(), 29.0); // 100 * 0.29
}

// =====================================================================
// Color Math & Manipulation Tests
// =====================================================================

TEST(ColorTest, Normalize)
{
    Color c(0.2, 0.4, 0.8);
    
    // Normalize to 1.0. Max channel is 0.8. 
    // Multiplier = 1.0 / 0.8 = 1.25.
    c.normalize(1.0);
    
    EXPECT_DOUBLE_EQ(c.red, 0.25);
    EXPECT_DOUBLE_EQ(c.green, 0.50);
    EXPECT_DOUBLE_EQ(c.blue, 1.00);
}

TEST(ColorTest, Saturate)
{
    Color c(0.5, 0.5, 0.5);
    
    // A perfectly grey color's luminance is equal to its channels.
    // Saturating it should do nothing.
    c.saturate(2.0);
    EXPECT_DOUBLE_EQ(c.red, 0.5);
    EXPECT_DOUBLE_EQ(c.green, 0.5);
    EXPECT_DOUBLE_EQ(c.blue, 0.5);
    
    Color c2(1.0, 0.0, 0.0);
    double lum = c2.luminance(); // 0.29
    
    // Desaturate completely (saturation = 0)
    c2.saturate(0.0);
    EXPECT_DOUBLE_EQ(c2.red, lum);
    EXPECT_DOUBLE_EQ(c2.green, lum);
    EXPECT_DOUBLE_EQ(c2.blue, lum);
}

TEST(ColorTest, AdjustAlpha)
{
    // Claude hates PTSD sufferers.
    // 0xAARRGGBB format. Pure red, 100% opaque.
    ImU32 opaque_red = IM_COL32(255, 0, 0, 255); // ImGui macro puts A in highest byte

    // Red's own luminance (~0.29) is below the 0.5 target, so no alpha short of fully
    // opaque reaches it -- this exercises the saturation clamp.
    ImU32 red_at_half_visibility = Color::adjust_alpha(opaque_red, 0.5);

    int extracted_a = (red_at_half_visibility >> 24) & 0xFF;
    int extracted_r = red_at_half_visibility & 0xFF;

    EXPECT_EQ(extracted_a, 255);
    EXPECT_EQ(extracted_r, 255);

    // A fully bright color's luminance is ~1, so alpha lands right at the target instead of
    // saturating -- this exercises the actual division.
    ImU32 opaque_white = IM_COL32(255, 255, 255, 255);
    ImU32 white_at_half_visibility = Color::adjust_alpha(opaque_white, 0.5);
    int white_a = (white_at_half_visibility >> 24) & 0xFF;

    EXPECT_EQ(white_a, 127);
}

// =====================================================================
// Global State & Redlight Mode Tests
// =====================================================================

TEST(ColorGlobalsTest, GammaSetAndGet)
{
    // Assuming default starts somewhere, let's explicitly set it.
    set_gamma(2.2);
    EXPECT_DOUBLE_EQ(get_gamma(), 2.2);
    
    set_gamma(1.0);
    EXPECT_DOUBLE_EQ(get_gamma(), 1.0);
}

TEST(ColorGlobalsTest, RedlightMode_FloatConversion)
{
    float r = 100.0f;
    float g = 90.0f;
    float b = 60.0f;
    
    // Mathematical definition from your function:
    // r = r + 0.5*g + 0.3*b
    // g = g / 3
    // b = b / 3
    
    rgb_apply_redlight(&r, &g, &b);
    
    // Expected R = 100 + 45 + 18 = 163
    EXPECT_FLOAT_EQ(r, 163.0f);
    EXPECT_FLOAT_EQ(g, 30.0f);
    EXPECT_FLOAT_EQ(b, 20.0f);
}

TEST(ColorGlobalsTest, RedlightMode_RgbaUIntConversion)
{
    // Start with redlight mode OFF
    redlight_mode = false;
    uint32_t input = IM_COL32(100, 90, 60, 255);
    
    uint32_t output_off = rgba_apply_redlight(input);
    EXPECT_EQ(output_off, input); // Should be unchanged
    
    // Turn redlight mode ON
    redlight_mode = true;
    uint32_t output_on = rgba_apply_redlight(input);
    
    int out_r = output_on & 0xFF;
    int out_g = (output_on & 0xFF00) >> 8;
    int out_b = (output_on & 0xFF0000) >> 16;
    int out_a = (output_on & 0xFF000000) >> 24;
    
    // Expected based on the float test above (clamped to int)
    EXPECT_EQ(out_r, 163);
    EXPECT_EQ(out_g, 30);
    EXPECT_EQ(out_b, 20);
    EXPECT_EQ(out_a, 255); // Alpha shouldn't change
    
    // Reset global state for other tests
    redlight_mode = false;
}

// =====================================================================
// Serialization (JSON)
// =====================================================================

TEST(ColorTest, JsonSerialization)
{
    Color original(0.1, 0.2, 0.3);
    
    json j = original.to_json();
    
    EXPECT_DOUBLE_EQ(j["red"], 0.1);
    EXPECT_DOUBLE_EQ(j["green"], 0.2);
    EXPECT_DOUBLE_EQ(j["blue"], 0.3);
    
    Color restored;
    bool success = restored.from_json(j);
    
    EXPECT_TRUE(success);
    EXPECT_DOUBLE_EQ(restored.red, 0.1);
    EXPECT_DOUBLE_EQ(restored.green, 0.2);
    EXPECT_DOUBLE_EQ(restored.blue, 0.3);
}

// =====================================================================
// Procedural Generation Checks
// =====================================================================

TEST(ColorProceduralTest, VegetationColorBounds)
{
    // We can't easily test randomness, but we CAN test that the generator
    // strictly returns valid byte bounds (0-255) regardless of the random path.
    std::mt19937 rng(42); // Seeded for determinism
    
    for (int i = 0; i < 100; i++)
    {
        RGB3 veg = generate_vegetation_color(&rng);
        
        EXPECT_GE(veg.r, 0);
        EXPECT_LE(veg.r, 255);
        
        EXPECT_GE(veg.g, 0);
        EXPECT_LE(veg.g, 255);
        
        EXPECT_GE(veg.b, 0);
        EXPECT_LE(veg.b, 255);
    }
}

TEST(ColorProceduralTest, LeafSpectrumHasRedAbsorptionAndAnInfraredReflectancePlateau)
{
    VegetationSpectrum leaf;
    leaf.band_count = 2;
    leaf.centers_nm = {430, 680};
    leaf.widths_nm = {18, 18};
    leaf.depths = {3, 3};
    leaf.visible_scattering = 0.20;
    leaf.infrared_scattering = 0.60;
    leaf.red_edge_nm = 720;
    leaf.red_edge_width_nm = 8;
    EXPECT_GT(leaf.reflectance_at(550), leaf.reflectance_at(430) * 5);
    EXPECT_GT(leaf.reflectance_at(800), leaf.reflectance_at(680) * 10);
    EXPECT_GT(leaf.reflectance_at(800), leaf.reflectance_at(550) * 2);
    EXPECT_NEAR(leaf.reflectance_at(1000), 0.60, 1e-6);
    const RGB3 rgb = leaf.visible_rgb();
    EXPECT_GT(rgb.g, rgb.r);
    EXPECT_GT(rgb.g, rgb.b);
}

TEST(ColorProceduralTest, InfraredReflectanceDoesNotBecomeVisibleRedLight)
{
    VegetationSpectrum leaf;
    leaf.red_edge_nm = 1000;
    leaf.infrared_scattering = 0.5;
    const RGB3 before = leaf.visible_rgb();
    const double infrared = leaf.reflectance_at(1200);
    leaf.infrared_scattering = 0.8;
    const RGB3 after = leaf.visible_rgb();
    EXPECT_EQ(before.r, after.r);
    EXPECT_EQ(before.g, after.g);
    EXPECT_EQ(before.b, after.b);
    EXPECT_GT(leaf.reflectance_at(1200), infrared + 0.25);
}

TEST(ColorProceduralTest, GeneratedSpectraHaveBoundedReflectanceAndHostSensitiveEdges)
{
    std::mt19937 cool_rng(42), hot_rng(42);
    double cool_edges = 0, hot_edges = 0;
    for (int i = 0; i < 256; i++)
    {
        const auto cool = generate_vegetation_spectrum(&cool_rng, 3000);
        const auto hot = generate_vegetation_spectrum(&hot_rng, 10000);
        cool_edges += cool.red_edge_nm;
        hot_edges += hot.red_edge_nm;
        for (const auto &leaf : {cool, hot})
        {
            for (double wavelength = 350; wavelength <= 1400; wavelength += 10)
            {
                const double reflectance = leaf.reflectance_at(wavelength);
                EXPECT_GE(reflectance, 0);
                EXPECT_LE(reflectance, 1);
            }
            EXPECT_NEAR(leaf.reflectance_at(1400), leaf.infrared_scattering, 1e-6);
        }
    }
    EXPECT_GT(cool_edges, hot_edges);
}

TEST(ColorProceduralTest, VegetationAbsorptionRespondsToStellarSpectrum)
{
    std::mt19937 cool_rng(42), hot_rng(42), repeat_rng(42);
    int cool_blue = 0, hot_blue = 0, different = 0;
    for (int i = 0; i < 512; i++)
    {
        const RGB3 cool = generate_vegetation_color(&cool_rng, 3000);
        const RGB3 hot = generate_vegetation_color(&hot_rng, 10000);
        const RGB3 repeat = generate_vegetation_color(&repeat_rng, 3000);
        EXPECT_EQ(cool.r, repeat.r);
        EXPECT_EQ(cool.g, repeat.g);
        EXPECT_EQ(cool.b, repeat.b);
        cool_blue += cool.b;
        hot_blue += hot.b;
        different += cool.r != hot.r || cool.g != hot.g || cool.b != hot.b;
    }
    // Hotter hosts favor blue absorption, leaving less blue reflected on average.
    EXPECT_GT(cool_blue, hot_blue);
    EXPECT_GT(different, 256);
}

TEST(ColorProceduralTest, VegetationUsesSolarSpectrumForUnknownStellarTemperatures)
{
    for (double temperature : {0.0, -1.0, std::nan(""), HUGE_VAL})
    {
        std::mt19937 fallback_rng(17), solar_rng(17);
        for (int i = 0; i < 16; i++)
        {
            const RGB3 fallback = generate_vegetation_color(&fallback_rng, temperature);
            const RGB3 solar = generate_vegetation_color(&solar_rng);
            EXPECT_EQ(fallback.r, solar.r);
            EXPECT_EQ(fallback.g, solar.g);
            EXPECT_EQ(fallback.b, solar.b);
        }
    }
}

TEST(ColorProceduralTest, VegetationPreservesPaletteDiversityAroundBothTestHosts)
{
    for (double temperature : {5100.0, 6000.0})
    {
        std::mt19937 rng(42);
        int dark = 0, blue = 0, green = 0, bright = 0;
        constexpr int count = 1024;
        for (int i = 0; i < count; i++)
        {
            const RGB3 color = generate_vegetation_color(&rng, temperature);
            const int maximum = std::max({color.r, color.g, color.b});
            dark += maximum < 70;
            blue += color.b > color.r && color.b > color.g;
            green += color.g > color.r && color.g > color.b;
            bright += maximum >= 100;
        }
        // Pigment mixtures retain green and blue foliage alongside dark and bright
        // palettes, rather than prescribing one hue for either host.
        EXPECT_GT(dark, count / 10);
        EXPECT_GT(blue, count / 10);
        EXPECT_GT(green, count / 50);
        EXPECT_GT(bright, count / 10);
    }
}

TEST(ColorProceduralTest, PurpleVegetationOutnumbersGreenByTwoToThreeAcrossStellarSpectra)
{
    for (double temperature : {3000.0, 5100.0, 6000.0, 7500.0, 10000.0})
    {
        std::mt19937 rng(173);
        int green = 0, purple = 0, other = 0;
        for (int i = 0; i < 8192; i++)
        {
            const RGB3 color = generate_vegetation_color(&rng, temperature);
            const double hue = Color::hue_from_rgb(color.r, color.g, color.b) * 180 / std::acos(-1);
            const bool saturated = Color::sat_from_rgb(color.r, color.g, color.b) >= 0.15;
            if (saturated && hue >= 80 && hue < 160) green++;
            else if (saturated && hue >= 260 && hue < 330) purple++;
            else other++;
        }
        SCOPED_TRACE(temperature);
        EXPECT_GT(green, 100);
        EXPECT_GT(purple, green * 2);
        EXPECT_LT(purple, green * 3);
        EXPECT_GT(other, 4096);
    }
}

TEST(ColorTest, ExplicitTextureColorsIgnoreLiveDisplaySettings)
{
    struct RestoreView
    {
        double brightness = global_brightness, gamma = global_inverse_gamma;
        bool redlight = redlight_mode;
        ~RestoreView()
        {
            global_brightness = brightness;
            global_inverse_gamma = gamma;
            redlight_mode = redlight;
        }
    } restore;
    const Color expected = Color::color_from_magnitude_indices(0, 1.5, 1.5, 1, -.6);
    const RGB3 rgb = Color::rgb_from_color(expected, -1, 1, false);
    for (double brightness : {.001, 1.0, 1000000.0})
    {
        global_brightness = brightness;
        global_inverse_gamma = .2;
        redlight_mode = true;
        const Color actual = Color::color_from_magnitude_indices(0, 1.5, 1.5, 1, -.6);
        EXPECT_DOUBLE_EQ(actual.red, expected.red);
        EXPECT_DOUBLE_EQ(actual.green, expected.green);
        EXPECT_DOUBLE_EQ(actual.blue, expected.blue);
        const RGB3 pixel = Color::rgb_from_color(actual, -1, 1, false);
        EXPECT_EQ(pixel.r, rgb.r);
        EXPECT_EQ(pixel.g, rgb.g);
        EXPECT_EQ(pixel.b, rgb.b);
        Color displayed = Color::color_from_magnitude_indices(0, 1.5, 1.5);
        EXPECT_NEAR(displayed.luminance(), brightness * 128, brightness * 1e-10);
    }
}

TEST(BrownDwarfColor, SpectrumHueAndBrightness)
{
    const Color violet = brown_linear_color(1000);
    EXPECT_GT(violet.blue, violet.red);
    EXPECT_GT(violet.red, violet.green);
    const Color cold = brown_linear_color(200);
    EXPECT_GT(cold.red, cold.blue);
    for (double temperature = 200; temperature <= 3000; temperature += 7)
    {
        const Color color = brown_linear_color(temperature);
        EXPECT_GE(std::min({color.red, color.green, color.blue}), 0);
        EXPECT_DOUBLE_EQ(std::max({color.red, color.green, color.blue}), 1);
    }
    Star star;
    star.mass = .04*solar_mass;
    star.temperature = 1000;
    star.BV_color = 2;
    const Color bright = Color::color_from_star(0, &star, star.BV_color);
    const Color faint = Color::color_from_star(3, &star, star.BV_color);
    EXPECT_NEAR(faint.red/bright.red, pow(magnbase, -3), 1e-12);
    EXPECT_NEAR(faint.blue/bright.blue, pow(magnbase, -3), 1e-12);
    EXPECT_GT(bright.blue, bright.red);
    Color source;
    EXPECT_TRUE(brown_color_source(&star, source));
    star.mass = .1*solar_mass;
    EXPECT_FALSE(brown_color_source(&star, source));
    star.mass = 0;
    star.temperature = 0;
    star.BV_color = log(blackbody_flux(1800, V_band)/blackbody_flux(1800, B_band))
        *invlogmagnbase-bv_correction;
    EXPECT_TRUE(brown_color_source(&star, source));
    EXPECT_NEAR(brown_temperature(&star), 1800, 1e-7);
    strcpy(star.spectral_type, "DA");
    EXPECT_FALSE(brown_color_source(&star, source));
}

TEST(BrownDwarfColor, RepeatableCloudMapAndSphericalContinuity)
{
    Star star;
    star.mass = .04*solar_mass;
    star.temperature = 1400;
    star.rnd_seed = 271;
    star.fictitious_map_height = 32;
    const auto random_before = star.rng;
    const BrownWeather weather(&star);
    for (double latitude : {-_pi/2, -.4, .4, _pi/2})
    {
        const Color a = brown_weather_color(weather, latitude, 0);
        const Color b = brown_weather_color(weather, latitude, 2*_pi);
        EXPECT_NEAR(a.red, b.red, 1e-12);
        EXPECT_NEAR(a.blue, b.blue, 1e-12);
    }
    const Color pole_a = brown_weather_color(weather, _pi/2, 0);
    const Color pole_b = brown_weather_color(weather, _pi/2, 2);
    EXPECT_NEAR(pole_a.red, pole_b.red, 1e-12);
    Map first(&star), second(&star);
    first.generate_stellar_map(&star);
    second.generate_stellar_map(&star);
    ASSERT_TRUE(first.is_complete());
    EXPECT_EQ(first.get_height(), 32UL);
    EXPECT_EQ(first.get_width(), 64UL);
    std::vector<unsigned char> a(32*64*4), b(a.size());
    first.export_rgba(a.data());
    second.export_rgba(b.data());
    EXPECT_EQ(a, b);
    unsigned char peak = 0;
    bool varied = false;
    for (size_t i = 0; i < a.size(); i += 4)
    {
        peak = std::max({peak, a[i], a[i+1], a[i+2]});
        if (a[i] != a[0] || a[i+1] != a[1] || a[i+2] != a[2]) varied = true;
    }
    EXPECT_EQ(peak, 255);
    EXPECT_TRUE(varied);
    EXPECT_EQ(star.rng, random_before);
    EXPECT_EQ(star.night_map, nullptr);
    star.rnd_seed = 272;
    const BrownWeather other(&star);
    EXPECT_NE(weather.phase[0], other.phase[0]);
}

TEST(BrownDwarfColor, EstimatedRotationAdvancesAndPreservesSuppliedPeriods)
{
    Star star;
    star.mass = .05*solar_mass;
    star.temperature = 1000;
    star.volumetric_mean_radius = jupiter_radius;
    estimate_brown_rotation(&star);
    ASSERT_GT(star.sidereal_rotational_period, 2*3600);
    ASSERT_LT(star.sidereal_rotational_period, 8*3600);
    EXPECT_TRUE(star.stellar_rotation.value("estimated", false));
    const double saved_time = simnow;
    const double first = star.timeofday();
    simnow += star.sidereal_rotational_period/4;
    const double second = star.timeofday();
    simnow = saved_time;
    EXPECT_NEAR(fmod(second-first+2*_pi, 2*_pi), _pi/2, 1e-8);
    star.sidereal_rotational_period = 7*3600;
    estimate_brown_rotation(&star);
    EXPECT_DOUBLE_EQ(star.sidereal_rotational_period, 7*3600);
    Star ordinary;
    ordinary.mass = solar_mass;
    ordinary.temperature = sun_temp;
    estimate_brown_rotation(&ordinary);
    EXPECT_DOUBLE_EQ(ordinary.sidereal_rotational_period, 0);
}

TEST(MDwarfColor, SpectralSubtypeOverridesMisleadingIndicesForDisplay)
{
    Star star;
    strcpy(star.spectral_type, "M3.5");
    star.mass = .3*solar_mass;
    star.volumetric_mean_radius = .017621*solar_radius;
    star.absolute_magnitude = 11.2;
    star.BV_color = .18;
    star.VR_color = .07;
    star.is_bv_observed = true;
    EXPECT_NEAR(star.estimate_temperature(), 3320, 1e-12);
    EXPECT_EQ(stellar_regime(&star), regime_stellar);
    const Color bright = Color::color_from_star(0, &star, star.BV_color);
    const Color faint = Color::color_from_star(3, &star, star.BV_color);
    EXPECT_GT(bright.red, bright.green);
    EXPECT_GT(bright.green, bright.blue);
    EXPECT_NEAR(faint.red/bright.red, pow(magnbase, -3), 1e-12);
    const Color reddened = Color::color_from_star(0, &star, star.BV_color+.3);
    EXPECT_GT(reddened.red/reddened.blue, bright.red/bright.blue);
    EXPECT_DOUBLE_EQ(star.BV_color, .18);
    EXPECT_DOUBLE_EQ(star.VR_color, .07);
    star.temperature = 3500;
    EXPECT_DOUBLE_EQ(m_dwarf_temperature(&star), 3500);
    star.temperature = 0;
    star.BV_color = 0;
    star.estimate_BV();
    EXPECT_DOUBLE_EQ(star.BV_color, 0);
    star.is_bv_observed = false;
    star.estimate_BV();
    EXPECT_GT(star.BV_color + bv_correction, .5);
}

TEST(MDwarfColor, ScopeExcludesGiantsMirasCompositesAndSubstellarMasses)
{
    Star star;
    for (const char* type : {"M3V", "M3.5", "M7Ve"})
    {
        strcpy(star.spectral_type, type);
        EXPECT_TRUE(is_m_dwarf_color_source(&star));
    }
    for (const char* type : {"M3III", "M3IV", "M2Iab", "M3V+DA", "G2V", "DA"})
    {
        strcpy(star.spectral_type, type);
        EXPECT_FALSE(is_m_dwarf_color_source(&star));
    }
    strcpy(star.spectral_type, "M5e");
    star.variability_type = "M";
    star.variability_period = 300*oneday;
    EXPECT_FALSE(is_m_dwarf_color_source(&star));
    star.variability_type.clear();
    star.mass = .04*solar_mass;
    EXPECT_FALSE(is_m_dwarf_color_source(&star));
}

TEST(MDwarfColor, TextureUsesTheSameSpectrumAndStaysNormalized)
{
    Star star;
    strcpy(star.spectral_type, "M3V");
    star.mass = .3*solar_mass;
    star.volumetric_mean_radius = .3*solar_radius;
    star.absolute_magnitude = 11;
    star.BV_color = .18;
    star.rnd_seed = 731;
    star.fictitious_map_height = 16;
    Map map(&star);
    map.generate_stellar_map(&star);
    ASSERT_TRUE(map.is_complete());
    std::vector<unsigned char> pixels(map.get_width()*map.get_height()*4);
    map.export_rgba(pixels.data());
    unsigned char peak = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        EXPECT_GE(pixels[i], pixels[i+1]);
        EXPECT_GT(pixels[i+1], pixels[i+2]);
        peak = std::max({peak, pixels[i], pixels[i+1], pixels[i+2]});
    }
    EXPECT_EQ(peak, 255);
    EXPECT_DOUBLE_EQ(star.BV_color, .18);
}

TEST(MDwarfColor, RealisticPaletteAndPrefixesAgreeAcrossDisplayPaths)
{
    Star star;
    star.mass = .2*solar_mass;
    star.absolute_magnitude = 12;
    star.temperature = 0;
    for (const char* type : {"M4V", "dM4e", "sdM4", "esdM4", "usdM4"})
    {
        strcpy(star.spectral_type, type);
        ASSERT_TRUE(is_m_dwarf_color_source(&star));
        EXPECT_DOUBLE_EQ(m_dwarf_temperature(&star), 3210);
        const Color color = Color::color_from_star(0, &star, star.BV_color);
        const Color linear = m_dwarf_linear_color(3210);
        Color expected(wd_srgb_encode(linear.red), wd_srgb_encode(linear.green), wd_srgb_encode(linear.blue));
        expected.normalize(1);
        Color normalized = color;
        normalized.normalize(1);
        EXPECT_NEAR(normalized.red, expected.red, 1e-12);
        EXPECT_NEAR(normalized.green, expected.green, 1e-12);
        EXPECT_NEAR(normalized.blue, expected.blue, 1e-12);
        const RGB3 swatch = wd_srgb_from_linear(m_dwarf_linear_color(3210));
        const RGB3 disc = Color::rgb_from_color(color, -1, 1, false);
        EXPECT_NEAR(swatch.r, disc.r, 1);
        EXPECT_NEAR(swatch.g, disc.g, 1);
        EXPECT_NEAR(swatch.b, disc.b, 1);
    }
}

TEST(MDwarfColor, GiantsAndDwarfsShareARealisticReferencePalette)
{
    Star star;
    star.temperature = 3500;
    star.mass = solar_mass;
    Color expected;
    for (const char* type : {"M2V", "sdM2", "dM2e", "M2Ib", "M2III"})
    {
        strcpy(star.spectral_type, type);
        const Color color = Color::color_from_star(0, &star, star.BV_color);
        const Color linear = m_dwarf_linear_color(3500);
        const RGB3 swatch = wd_srgb_from_linear(linear);
        const RGB3 disc = Color::rgb_from_color(color, -1, 1, false);
        EXPECT_NEAR(swatch.g, disc.g, 1);
        EXPECT_NEAR(swatch.b, disc.b, 1);
    }
}

TEST(StarColorModeTest, OrdinaryStarsUseTheSelectedPaletteAndKeepMagnitudeScaling)
{
    Star::load_main_seq_dat();
    Star star;
    star.mass = solar_mass;
    for (const char* type : {"A0V", "G2V", "K5V", "M4V", "sdM4"})
    {
        strcpy(star.spectral_type, type);
        star.BV_color = type[0] == 'M' || type[0] == 's' ? 1.6 : .65;
        star.temperature = type[0] == 'A' ? 10000 : type[0] == 'G' ? 5800 : type[0] == 'K' ? 4400 : 3210;
        Color realistic = Color::color_from_star(0, &star, star.BV_color, false);
        Color pretty = Color::color_from_star(0, &star, star.BV_color, true);
        EXPECT_GT(fabs(pretty.red/pretty.green-realistic.red/realistic.green), .01);
        EXPECT_NEAR(realistic.luminance(), pretty.luminance(), realistic.luminance()*1e-12);
        const Color faint = Color::color_from_star(3, &star, star.BV_color, true);
        EXPECT_NEAR(faint.red/pretty.red, pow(magnbase, -3), 1e-12);
        EXPECT_DOUBLE_EQ(star.BV_color, type[0] == 'M' || type[0] == 's' ? 1.6 : .65);
    }
}

TEST(StarColorModeTest, GreenWhiteDwarfReachesFullSpreadAndBrownDwarfStaysIdentical)
{
    Star star;
    star.WD = "WD1328+648";
    star.gaia_wd_name = "WDJ133001.17+643523.69";
    star.temperature = 10000;
    const Color realistic = Color::color_from_star(0, &star, 0, false);
    const Color pretty = Color::color_from_star(0, &star, 0, true);
    const auto spread = [](Color color)
    {
        color.normalize(1);
        return std::max({color.red, color.green, color.blue})-std::min({color.red, color.green, color.blue});
    };
    EXPECT_GT(spread(pretty), spread(realistic));
    EXPECT_DOUBLE_EQ(spread(pretty), 1.0);
    star.WD.clear(); star.gaia_wd_name.clear();
    strcpy(star.spectral_type, "T6");
    star.temperature = 1000;
    star.mass = .04*solar_mass;
    const Color brown_real = Color::color_from_star(0, &star, 0, false);
    const Color brown_pretty = Color::color_from_star(0, &star, 0, true);
    EXPECT_DOUBLE_EQ(brown_real.red, brown_pretty.red);
    EXPECT_DOUBLE_EQ(brown_real.green, brown_pretty.green);
    EXPECT_DOUBLE_EQ(brown_real.blue, brown_pretty.blue);
}

TEST(StarColorModeTest, TextureTintChangesModeWithoutRebuildingTheMap)
{
    struct Restore
    {
        bool saved = fancy_star_col;
        ~Restore() { fancy_star_col = saved; }
    } restore;
    Star star;
    strcpy(star.spectral_type, "M4V");
    star.mass = .2*solar_mass;
    star.BV_color = 1.6;
    const Color base = Color::color_from_star(0, &star, star.BV_color, false);
    fancy_star_col = false;
    const Color neutral = Color::stellar_texture_tint(&star);
    EXPECT_DOUBLE_EQ(neutral.red, 1);
    EXPECT_DOUBLE_EQ(neutral.green, 1);
    EXPECT_DOUBLE_EQ(neutral.blue, 1);
    fancy_star_col = true;
    const Color tint = Color::stellar_texture_tint(&star);
    Color baked = base;
    baked.normalize(1);
    baked.red *= tint.red; baked.green *= tint.green; baked.blue *= tint.blue;
    Color expected = Color::color_from_star(0, &star, star.BV_color, true);
    expected.normalize(1);
    EXPECT_NEAR(baked.red, expected.red, 1e-12);
    EXPECT_NEAR(baked.green, expected.green, 1e-12);
    EXPECT_NEAR(baked.blue, expected.blue, 1e-12);
}
