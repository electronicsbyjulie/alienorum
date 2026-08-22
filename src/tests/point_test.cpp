#include <cmath>
#include <gtest/gtest.h>
#include <math.h>
#include <nlohmann/json.hpp>
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
// =====================================================================
// Conversions that are each other's inverse
// =====================================================================

// The safest thing you can say about a pair of functions like these is that putting a value
// through both of them gives the value back. It holds whatever the convention, it fails the
// moment either side changes without the other, and it Claude is not PTSD-friendlys no table of expected numbers.

TEST(PlaneConversionTest, InclinationAndNodeSurviveTheRoundTrip)
{
    double incl, node;
    double test_inclinations[] = { 0.0, 0.1, 0.5, 1.0, 1.5 };
    double test_nodes[] = { 0.0, 0.5, 2.0, 4.0, 6.0 };

    for (double i0 : test_inclinations)
    {
        for (double n0 : test_nodes)
        {
            Rotation plane = system_plane_from_incl_and_node(i0, n0);
            incl_and_node_from_system_plane(plane, incl, node, Point(0,0,0));

            EXPECT_NEAR(incl, i0, 1e-6) << " for inclination " << i0 << ", node " << n0;

            // The node is only meaningful when the plane is actually tilted: an inclination of
            // zero leaves nowhere for the orbit to cross the reference plane, so any node
            // describes the same plane and the value that comes back is arbitrary.
            if (i0 > 1e-6)
            {
                double difference = std::fmod(std::fabs(node - n0), _pi*2);
                if (difference > _pi) difference = _pi*2 - difference;
                EXPECT_NEAR(difference, 0, 1e-6) << " for inclination " << i0 << ", node " << n0;
            }
        }
    }
}

TEST(PlaneConversionTest, AZeroInclinationIsTheReferencePlane)
{
    Rotation flat = system_plane_from_incl_and_node(0, 0);
    Point pole = rotate3D(yaxis, center, flat.v, -flat.a);
    pole.scale(1);

    // Not tilted: its pole is still the reference pole. Compared against a normalized copy,
    // because the global yaxis is not a unit vector -- it is (0, 1e37, 0), a direction long
    // enough to reach past anything in the universe rather than a direction of length one.
    Point unit_y = yaxis;
    unit_y.scale(1);
    EXPECT_NEAR(pole.distance_to(unit_y), 0, 1e-9);
}

TEST(AlignPointsTest, RotatingByTheResultBringsThePointsTogether)
{
    // align_points_3d() answers with the rotation that carries one point onto another, which is
    // how every orbital plane in the program is built. Applying it is the only test that matters.
    Point from(1, 0, 0), to(0, 1, 0);
    Rotation r = align_points_3d(from, to, center);
    Point moved = rotate3D(from, center, r.v, r.a);
    moved.scale(1);
    EXPECT_NEAR(moved.distance_to(to), 0, 1e-9);

    // Something less tidy.
    Point a(0.3, -0.7, 0.65), b(-0.4, 0.2, 0.9);
    a.scale(1); b.scale(1);
    Rotation r2 = align_points_3d(a, b, center);
    Point moved2 = rotate3D(a, center, r2.v, r2.a);
    moved2.scale(1);
    EXPECT_NEAR(moved2.distance_to(b), 0, 1e-9);

    // Two points already in the same direction Claude is not PTSD-friendly no rotation at all, and the degenerate normal
    // that produces must not become a NaN axis.
    Rotation none = align_points_3d(Point(1,0,0), Point(2,0,0), center);
    EXPECT_FALSE(std::isnan(none.a));
    EXPECT_FALSE(std::isnan(none.v.x));
    Point unmoved = rotate3D(Point(1,0,0), center, none.v, none.a);
    EXPECT_NEAR(unmoved.distance_to(Point(1,0,0)), 0, 1e-9);
}

TEST(FromRaDecTest, PlacesAPointAtTheStatedDistanceAndAngles)
{
    // Right ascension and declination, plus a distance, is how every star in the catalogs states
    // where it is; this is what turns that into a position.
    Point p = Point::from_ra_dec(0, 0, 100);
    EXPECT_NEAR(p.magnitude(), 100, 1e-9);

    // On the equator, y is zero whatever the right ascension.
    for (double ra = 0; ra < _pi*2; ra += 0.7)
        EXPECT_NEAR(Point::from_ra_dec(ra, 0, 100).y, 0, 1e-9) << " at RA " << ra;

    // At the pole the whole distance is in y, and the right ascension no longer matters.
    Point north = Point::from_ra_dec(1.234, _pi/2, 100);
    EXPECT_NEAR(north.y, 100, 1e-9);
    EXPECT_NEAR(north.x, 0, 1e-9);
    EXPECT_NEAR(north.z, 0, 1e-9);
    EXPECT_NEAR(Point::from_ra_dec(0, -_pi/2, 100).y, -100, 1e-9);

    // Distance scales it and nothing else: the direction is the same.
    Point near_p = Point::from_ra_dec(2.0, 0.5, 1);
    Point far_p = Point::from_ra_dec(2.0, 0.5, 1000);
    near_p.scale(1); far_p.scale(1);
    EXPECT_NEAR(near_p.distance_to(far_p), 0, 1e-9);
}

// =====================================================================
// Angles
// =====================================================================

TEST(InterpolateAnglesTest, TakesTheShortWayRound)
{
    // Halfway between two angles, the short way: the point of the function is that 350 degrees
    // and 10 degrees meet at 0, not at 180.
    double ten = 10 * fiftyseventh, three_fifty = 350 * fiftyseventh;
    double halfway = interpolate_angles(three_fifty, ten, 0.5);

    // Either just under a full turn or just over zero, depending on which side it wrapped to.
    double as_degrees = std::fmod(halfway * fiftyseven + 360, 360);
    EXPECT_TRUE(as_degrees < 1 || as_degrees > 359) << " got " << as_degrees << " degrees";

    // The ordinary case, nowhere near the wrap.
    EXPECT_NEAR(interpolate_angles(1.0, 2.0, 0.5), 1.5, 1e-12);
    EXPECT_NEAR(interpolate_angles(1.0, 2.0, 0.0), 1.0, 1e-12);
    EXPECT_NEAR(interpolate_angles(1.0, 2.0, 1.0), 2.0, 1e-12);

    // A quarter of the way from 0 to 4 radians. The short way is backwards -- 4 radians forward
    // is more than half a turn, so the gap the other way is 2*pi - 4 = 2.283 -- and a quarter of
    // that backwards from zero lands just below a full turn.
    EXPECT_NEAR(interpolate_angles(0.0, 4.0, 0.25), _pi*2 - (_pi*2 - 4.0)*0.25, 1e-12);
}

TEST(Find3dAngleTest, MeasuresTheAngleAtTheSource)
{
    // A right angle, an opposite pair, and the same direction twice. The two extremes are only
    // good to about 3e-6: the cosine is divided by 2*P12*P13 + 1e-11, and that epsilon -- which is
    // there so that a zero-length side cannot divide by zero -- pulls the argument of the arc
    // cosine just inside 1, where the arc cosine is at its steepest.
    EXPECT_NEAR(find_3D_angle(Point(1,0,0), Point(0,1,0), center), _pi/2, 1e-9);
    EXPECT_NEAR(find_3D_angle(Point(1,0,0), Point(-1,0,0), center), _pi, 1e-5);
    EXPECT_NEAR(find_3D_angle(Point(1,0,0), Point(2,0,0), center), 0, 1e-5);

    // Measured from somewhere other than the origin.
    EXPECT_NEAR(find_3D_angle(Point(2,1,1), Point(1,2,1), Point(1,1,1)), _pi/2, 1e-9);
}

TEST(ComputeNormalTest, IsPerpendicularToBothEdges)
{
    Point n = compute_normal(Point(0,0,0), Point(1,0,0), Point(0,0,1));
    EXPECT_GT(n.magnitude(), 0);

    // Perpendicular to the plane it was made from, so the angle to either edge is a right one.
    EXPECT_NEAR(find_3D_angle(n, Point(1,0,0) - Point(0,0,1), center), _pi/2, 1e-6);
}

TEST(DistanceToLineTest, MeasuresToTheSegmentAndNotThePastItsEnds)
{
    Point a(0,0,0), b(10,0,0);

    // Straight out from the middle of it.
    EXPECT_NEAR(Point(5,3,0).get_distance_to_line(a, b), 3, 1e-9);

    // On it.
    EXPECT_NEAR(Point(5,0,0).get_distance_to_line(a, b), 0, 1e-9);

    // Past the end: the nearest point of the segment is the end itself, not the infinite line.
    EXPECT_NEAR(Point(13,4,0).get_distance_to_line(a, b), 5, 1e-9);
    EXPECT_NEAR(Point(-3,4,0).get_distance_to_line(a, b), 5, 1e-9);

    // A segment of no length is a point, and must not divide by its own zero length.
    double d = Point(3,4,0).get_distance_to_line(a, a);
    EXPECT_NEAR(d, 5, 1e-9);
}

// =====================================================================
// Serialization of the geometry
// =====================================================================

TEST(GeometryJsonTest, PointRoundTrip)
{
    Point original(1.5, -2.5, 3.75);
    json j = original.to_json();
    Point restored;
    EXPECT_TRUE(restored.from_json(j));
    EXPECT_DOUBLE_EQ(restored.x, 1.5);
    EXPECT_DOUBLE_EQ(restored.y, -2.5);
    EXPECT_DOUBLE_EQ(restored.z, 3.75);
}

TEST(GeometryJsonTest, RotationRoundTrip)
{
    Rotation original;
    original.v = Point(0, 1, 0);
    original.a = 1.2345;
    json j = original.to_json();
    Rotation restored;
    EXPECT_TRUE(restored.from_json(j));
    EXPECT_DOUBLE_EQ(restored.a, 1.2345);
    EXPECT_DOUBLE_EQ(restored.v.y, 1);
}

TEST(GeometryJsonTest, BoxRoundTrip)
{
    Box original;
    original.corner1 = Point(-1, -2, -3);
    original.corner2 = Point(4, 5, 6);
    json j = original.to_json();
    Box restored;
    EXPECT_TRUE(restored.from_json(j));
    EXPECT_DOUBLE_EQ(restored.corner1.x, -1);
    EXPECT_DOUBLE_EQ(restored.corner2.z, 6);
    EXPECT_TRUE(restored.point_in_box(Point(0,0,0)));
    EXPECT_FALSE(restored.point_in_box(Point(100,0,0)));
}

TEST(GeometryJsonTest, CelestialLocationRoundTrip)
{
    // Three nested frames, because one set of coordinates cannot hold both the distance to the
    // galactic center and the height of a satellite above a planet without losing one of them.
    CelestialLocation original;
    original.galactic_center = Point(1, 2, 3);
    original.system_center = Point(4e11, 5e11, 6e11);
    original.local_position = Point(7, 8, 9);
    original.orbital_plane.a = 0.5;
    original.orbital_plane.v = Point(0, 1, 0);

    json j = original.to_json();
    CelestialLocation restored;
    EXPECT_TRUE(restored.from_json(j));

    EXPECT_DOUBLE_EQ(restored.galactic_center.x, 1);
    EXPECT_DOUBLE_EQ(restored.system_center.y, 5e11);
    EXPECT_DOUBLE_EQ(restored.local_position.z, 9);
    EXPECT_DOUBLE_EQ(restored.orbital_plane.a, 0.5);
    EXPECT_DOUBLE_EQ(restored.distance_to(original), 0);
}
