#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <random>
#include "../classes/color.h"

using namespace alienorum;
using json = nlohmann::json;

// =====================================================================
// RGB3Byte Tests
// =====================================================================

TEST(RGB3ByteTest, InitializationAndLuminance)
{
    RGB3Byte black;
    EXPECT_EQ(black.r, 0);
    EXPECT_EQ(black.g, 0);
    EXPECT_EQ(black.b, 0);
    EXPECT_DOUBLE_EQ(black.luminance(), 0.0);

    RGB3Byte white(255, 255, 255);
    EXPECT_EQ(white.r, 255);
    EXPECT_EQ(white.g, 255);
    EXPECT_EQ(white.b, 255);
    
    // Luminance = 0.29*255 + 0.56*255 + 0.15*255 = 255.0
    EXPECT_DOUBLE_EQ(white.luminance(), 255.0);
    
    RGB3Byte red_only(100, 0, 0);
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
        RGB3Byte veg = generate_vegetation_color(&rng);
        
        EXPECT_GE(veg.r, 0);
        EXPECT_LE(veg.r, 255);
        
        EXPECT_GE(veg.g, 0);
        EXPECT_LE(veg.g, 255);
        
        EXPECT_GE(veg.b, 0);
        EXPECT_LE(veg.b, 255);
    }
}