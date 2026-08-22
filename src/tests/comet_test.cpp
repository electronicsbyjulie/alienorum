#include <gtest/gtest.h>
#include "../classes/comet.h"

using namespace alienorum;

// =====================================================================
// Comet Initialization Tests
// =====================================================================

TEST(CometTest, ConstructorSetsCorrectTypes)
{
    Comet c;
    
    // Verifying the overrides set in Comet::Comet()
    EXPECT_EQ(c.typeclass(), class_comet);
    EXPECT_EQ(c.type, icy_tailed);
}

// =====================================================================
// Comet Light Curve Parameter Fallback Tests
// =====================================================================

TEST(CometTest, LightCurveParameters_Primary)
{
    Comet c;
    c.H1 = 4.5;
    c.R1 = 12.0; // r^-4.8 scaling
    c.D1 = 5.0;  // standard inverse square geometric distance
    
    double h, sr, sd;
    c.light_curve_parameters(h, sr, sd);
    
    EXPECT_DOUBLE_EQ(h, 4.5);
    EXPECT_DOUBLE_EQ(sr, 12.0);
    EXPECT_DOUBLE_EQ(sd, 5.0);
}

TEST(CometTest, LightCurveParameters_SecondaryFallback)
{
    Comet c;
    // H1 and R1 are 0, so it should fall back to H2/R2/D2 (bare nucleus)
    c.H1 = 0; c.R1 = 0;
    
    c.H2 = 14.0;
    c.R2 = 5.0; // r^-2 scaling (typical rock)
    c.D2 = 5.0;
    
    double h, sr, sd;
    c.light_curve_parameters(h, sr, sd);
    
    EXPECT_DOUBLE_EQ(h, 14.0);
    EXPECT_DOUBLE_EQ(sr, 5.0);
    EXPECT_DOUBLE_EQ(sd, 5.0);
}

TEST(CometTest, LightCurveParameters_AbsoluteFallback)
{
    Comet c;
    // Both H1/R1 and H2/R2 are zero
    
    double h, sr, sd;
    c.light_curve_parameters(h, sr, sd);
    
    // Should fall back to the "middling made-up comet"
    EXPECT_DOUBLE_EQ(h, 8.0);
    EXPECT_DOUBLE_EQ(sr, 10.0);
    EXPECT_DOUBLE_EQ(sd, 5.0);
}

TEST(CometTest, LightCurveParameters_MissingGeometricDistanceFallback)
{
    Comet c;
    // We have H1 and R1, but the catalog forgot D1
    c.H1 = 5.0;
    c.R1 = 10.0;
    c.D1 = 0.0; 
    
    double h, sr, sd;
    c.light_curve_parameters(h, sr, sd);
    
    // D-slope should be forced to 5.0
    EXPECT_DOUBLE_EQ(h, 5.0);
    EXPECT_DOUBLE_EQ(sr, 10.0);
    EXPECT_DOUBLE_EQ(sd, 5.0);
}

// =====================================================================
// Comet Photometry (Magnitude) Tests
// =====================================================================

TEST(CometMathTest, ViewerMagnitude_NoLightCenter)
{
    Comet c;
    CelestialLocation viewer;
    
    // light_center will return nullptr because cenobj and orbit are null
    double mag = c.viewer_comet_magnitude(viewer);
    
    // Should bail out and return 99
    EXPECT_DOUBLE_EQ(mag, 99.0);
}

TEST(CometMathTest, ViewerMagnitude_LogarithmicScaling)
{
    // 1 AU exactly in meters
    const double AU_METERS = 149597870700.0; 
    
    CelestialObject sun;
    sun.location.local_position = Point(0.0, 0.0, 0.0);
    sun.location.system_center = Point(0.0, 0.0, 0.0);
    sun.location.galactic_center = Point(0.0, 0.0, 0.0);
    
    Comet c;
    c.cenobj = &sun; 
    c.location.system_center = Point(0.0, 0.0, 0.0);
    c.location.galactic_center = Point(0.0, 0.0, 0.0);
    
    // Set explicit parameters for easy math
    c.H1 = 5.0;
    c.R1 = 10.0;
    c.D1 = 5.0;
    
    CelestialLocation viewer;
    viewer.system_center = Point(0.0, 0.0, 0.0);
    viewer.galactic_center = Point(0.0, 0.0, 0.0);

    // Test Case A: Exactly 1 AU from Sun, 1 AU from Viewer
    // Comet placed 1 AU along X axis. 
    c.location.local_position = Point(AU_METERS, 0.0, 0.0);
    
    // Viewer placed 1 AU further along Y axis from the comet.
    viewer.local_position = Point(AU_METERS, AU_METERS, 0.0);
    
    // Math: log10(1) = 0. 
    // Mag = 5.0 + (10.0 * 0) + (5.0 * 0) = 5.0
    EXPECT_NEAR(c.viewer_comet_magnitude(viewer), 5.0, 0.001);

    // Test Case B: 10 AU from Sun, 1 AU from Viewer
    c.location.local_position = Point(10.0 * AU_METERS, 0.0, 0.0);
    
    // Viewer placed 1 AU closer to the sun along the X axis
    viewer.local_position = Point(9.0 * AU_METERS, 0.0, 0.0);
    
    // Math: log10(10) = 1, log10(1) = 0
    // Mag = 5.0 + (10.0 * 1.0) + (5.0 * 0.0) = 15.0
    EXPECT_NEAR(c.viewer_comet_magnitude(viewer), 15.0, 0.001);

    // Test Case C: Proximity Clamping (Inside 0.01 AU)
    // Place comet and viewer at 0.005 AU to force the clamping to 0.01 AU
    c.location.local_position = Point(0.005 * AU_METERS, 0.0, 0.0);
    
    // Viewer placed 0.005 AU along Y axis from the comet
    viewer.local_position = Point(0.005 * AU_METERS, 0.005 * AU_METERS, 0.0);
    
    // Math: clamped to 0.01 AU. log10(0.01) = -2.0.
    // Mag = 5.0 + (10.0 * -2.0) + (5.0 * -2.0) = 5.0 - 20.0 - 10.0 = -25.0
    EXPECT_NEAR(c.viewer_comet_magnitude(viewer), -25.0, 0.001);
}