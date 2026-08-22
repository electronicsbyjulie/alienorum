#include <gtest/gtest.h>
#include <math.h>
#include "../classes/point.h"

using namespace alienorum;

// =====================================================================
// Point & Vector Math Tests
// =====================================================================

TEST(PointTest, MagnitudeAndScaling)
{
    Point p(3.0, 4.0, 0.0);
    
    // 3-4-5 triangle check
    EXPECT_DOUBLE_EQ(p.magnitude(), 5.0);
    EXPECT_DOUBLE_EQ(p.squared_magnitude(), 25.0);

    // Scale to magnitude of 10
    p.scale(10.0);
    EXPECT_DOUBLE_EQ(p.x, 6.0);
    EXPECT_DOUBLE_EQ(p.y, 8.0);
    EXPECT_DOUBLE_EQ(p.z, 0.0);
    EXPECT_DOUBLE_EQ(p.magnitude(), 10.0);
}

TEST(PointTest, DistanceTo)
{
    Point p1(10.0, 0.0, 0.0);
    Point p2(-10.0, 0.0, 0.0);
    
    EXPECT_DOUBLE_EQ(p1.distance_to(p2), 20.0);
    
    Point p3(10.0, 10.0, 10.0);
    EXPECT_DOUBLE_EQ(p1.distance_to(p3), sqrt(200.0));
}

TEST(PointTest, OperatorOverloads)
{
    Point p1(1.0, 2.0, 3.0);
    Point p2(4.0, 5.0, 6.0);
    
    Point p3 = p1 + p2;
    EXPECT_DOUBLE_EQ(p3.x, 5.0);
    EXPECT_DOUBLE_EQ(p3.y, 7.0);
    EXPECT_DOUBLE_EQ(p3.z, 9.0);
    
    Point p4 = p2 - p1;
    EXPECT_DOUBLE_EQ(p4.x, 3.0);
    EXPECT_DOUBLE_EQ(p4.y, 3.0);
    EXPECT_DOUBLE_EQ(p4.z, 3.0);
    
    Point p5 = p1 * 2.0;
    EXPECT_DOUBLE_EQ(p5.x, 2.0);
    EXPECT_DOUBLE_EQ(p5.y, 4.0);
    EXPECT_DOUBLE_EQ(p5.z, 6.0);
}

// =====================================================================
// Box Bounds Tests
// =====================================================================

TEST(BoxTest, PointInBox)
{
    Box b;
    b.corner1 = Point(-10.0, -10.0, -10.0);
    b.corner2 = Point(10.0, 10.0, 10.0);

    // Inside
    EXPECT_TRUE(b.point_in_box(Point(0.0, 0.0, 0.0)));
    EXPECT_TRUE(b.point_in_box(Point(5.0, -5.0, 5.0)));
    
    // Exactly on edge/corner
    EXPECT_TRUE(b.point_in_box(Point(10.0, 10.0, 10.0)));
    EXPECT_TRUE(b.point_in_box(Point(-10.0, 5.0, 10.0)));

    // Outside
    EXPECT_FALSE(b.point_in_box(Point(11.0, 0.0, 0.0)));
    EXPECT_FALSE(b.point_in_box(Point(0.0, -15.0, 0.0)));
}

// =====================================================================
// 3D Geometry & Rotation Tests
// =====================================================================

TEST(GeometryMathTest, Rotate3D)
{
    Point p(1.0, 0.0, 0.0);
    Point origin(0.0, 0.0, 0.0);
    Point z_axis(0.0, 0.0, 1.0);
    
    // Rotate point (1,0,0) around Z-axis by 90 degrees (PI/2)
    // By the right-hand rule, it should move to (0,1,0)
    Point rotated = rotate3D(p, origin, z_axis, half_pi);
    
    EXPECT_NEAR(rotated.x, 0.0, 1e-10);
    EXPECT_NEAR(rotated.y, 1.0, 1e-10);
    EXPECT_NEAR(rotated.z, 0.0, 1e-10);

    // Rotate 180 degrees (PI)
    Point rotated_180 = rotate3D(p, origin, z_axis, half_pi * 2.0);
    EXPECT_NEAR(rotated_180.x, -1.0, 1e-10);
    EXPECT_NEAR(rotated_180.y, 0.0, 1e-10);
}

TEST(GeometryMathTest, FindAngle)
{
    // find_angle(dx, dy) evaluates atan2(dy, dx) normalized to 0 - 2PI
    EXPECT_DOUBLE_EQ(find_angle(1.0, 0.0), 0.0);
    
    EXPECT_DOUBLE_EQ(find_angle(0.0, 1.0), half_pi);
    EXPECT_DOUBLE_EQ(find_angle(-1.0, 0.0), half_pi * 2.0);
    EXPECT_DOUBLE_EQ(find_angle(0.0, -1.0), half_pi * 3.0);
}

// =====================================================================
// CelestialLocation Coordinate Nesting Tests
// =====================================================================

TEST(CelestialLocationTest, NestedDistanceCalculation)
{
    CelestialLocation loc1;
    loc1.galactic_center = Point(0.0, 0.0, 0.0);
    loc1.system_center = Point(0.0, 0.0, 0.0);
    loc1.local_position = Point(0.0, 0.0, 0.0);

    CelestialLocation loc2;
    loc2.galactic_center = Point(0.0, 0.0, 0.0);
    loc2.system_center = Point(0.0, 0.0, 0.0);
    
    // Test pure local offset
    loc2.local_position = Point(100.0, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(loc1.distance_to(loc2), 100.0);
    
    // Test pure system offset
    loc2.local_position = Point(0.0, 0.0, 0.0);
    loc2.system_center = Point(0.0, 50.0, 0.0);
    EXPECT_DOUBLE_EQ(loc1.distance_to(loc2), 50.0);
    
    // Test combination of system and local offset
    loc2.system_center = Point(100.0, 0.0, 0.0);
    loc2.local_position = Point(50.0, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(loc1.distance_to(loc2), 150.0);
}

TEST(CelestialLocationTest, OperatorSubtraction)
{
    CelestialLocation loc1, loc2;
    loc1.galactic_center = Point(5.0, 0.0, 0.0);
    loc1.system_center = Point(10.0, 0.0, 0.0);
    loc1.local_position = Point(100.0, 0.0, 0.0);

    loc2.galactic_center = Point(2.0, 0.0, 0.0);
    loc2.system_center = Point(3.0, 0.0, 0.0);
    loc2.local_position = Point(25.0, 0.0, 0.0);

    CelestialLocation result = loc1 - loc2;
    
    EXPECT_DOUBLE_EQ(result.galactic_center.x, 3.0);
    EXPECT_DOUBLE_EQ(result.system_center.x, 7.0);
    EXPECT_DOUBLE_EQ(result.local_position.x, 75.0);
}