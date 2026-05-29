
#include <math.h>
#include <string>
#include <cctype>
#include <iostream>
#include <algorithm>
#include <sstream>
#include "point.h"

Point center(0,0,0), xaxis(1e37, 0, 0), yaxis(0, 1e37, 0), zaxis(0, 0, 1e37);

Point solar_north    = Point::from_ra_dec(solar_north_RA_J2000,    solar_north_Decl_J2000,    light_year);
Point ecliptic_north = Point::from_ra_dec(ecliptic_north_RA_J2000, ecliptic_north_Decl_J2000, light_year);
Point galactic_north = Point::from_ra_dec(galactic_north_RA_J2000, galactic_north_Decl_J2000, light_year);

Rotation ICRF_to_ecliptic = align_points_3d(ecliptic_north, yaxis, center);

Point velocity;

Point::Point(double newx, double newy, double newz)
{
    x = newx;
    y = newy;
    z = newz;
}

Point::Point(CelestialLocation &cel)
{
    *this = cel.local_position + cel.system_center;
}

Point Point::operator+(Point other)
{
    return Point(x+other.x, y+other.y, z+other.z);
}

Point &Point::operator+=(Point other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
}

Point Point::operator-(Point other)
{
    return Point(x-other.x, y-other.y, z-other.z);
}

Point &Point::operator-=(Point other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
}

Point Point::operator*(double multiplier)
{
    return Point(x*multiplier, y*multiplier, z*multiplier);
}

Point &Point::operator*=(double multiplier)
{
    x -= multiplier;
    y -= multiplier;
    z -= multiplier;
    return *this;
}

double Point::distance_to(Point other) const
{
    Point temp = other - *this;
    return sqrt(temp.x*temp.x + temp.y*temp.y + temp.z*temp.z);
}

Cartesian2D::Cartesian2D(Point pt, double az, double alt, double m)
{
    if (az) pt = rotate3D(pt, center, yaxis, -az);
    if (alt) pt = rotate3D(pt, center, xaxis, alt);
    if (pt.z < 0)
    {
        x = y = -1e29;
    }
    else
    {
        x = pt.x / pt.z * m;
        y = -pt.y / pt.z * m;
    }
}

Point::Point(Cartesian2D cart, double r, double az, double alt, double m)
{
    z = r;
    x = cart.x * z / m;
    y = -cart.y * z / m;
    if (alt) *this = rotate3D(*this, center, xaxis, -alt);
    if (az) *this = rotate3D(*this, center, yaxis, az);
}

// https://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
double Point::get_distance_to_line(Point a, Point b) const
{
    float r2 = pow(a.distance_to(b), 2);
    if (!r2) return distance_to(a);

    float t = fmax(0, fmin(1,  ((x - a.x) * (b.x - a.x) + (y - a.y) * (b.y - a.y) + (z - a.z) * (b.z - a.z)) / r2));
    Point p(a.x + t * (b.x-a.x), a.y + t * (b.y-a.y), a.z + t * (b.z-a.z));

    return distance_to(p);
}

Cartesian2D Cartesian2D::operator+(Cartesian2D other)
{
    return Cartesian2D(x + other.x, y + other.y);
}

Cartesian2D &Cartesian2D::operator+=(Cartesian2D other)
{
    x += other.x;
    y += other.y;
    return *this;
}

Cartesian2D Cartesian2D::operator*(double multiplier)
{
    return Cartesian2D(x * multiplier, y * multiplier);
}

Cartesian2D &Cartesian2D::operator*=(double multiplier)
{
    x *= multiplier;
    y *= multiplier;
    return *this;
}

Cartesian2D Cartesian2D::operator/(double divisor)
{
    double multiplier = 1.0 / divisor;
    return Cartesian2D(x * multiplier, y * multiplier);
}

Cartesian2D &Cartesian2D::operator/=(double divisor)
{
    double multiplier = 1.0 / divisor;
    x *= multiplier;
    y *= multiplier;
    return *this;
}

double find_angle(double dx, double dy)
{
    double angle = atan2(dy,dx);
    if (angle < 0)
    {
        angle += 2 * M_PI;
    }
    return angle;
}

double find_3D_angle(Point A, Point B, Point source)
{
    Point lA = A - source;
    lA.scale(1);
    Point lB = B - source;
    lB.scale(1);

    // https://stackoverflow.com/questions/1211212/how-to-calculate-an-angle-from-three-points
    double P12 = lA.magnitude();
    double P13 = lB.magnitude();
    double P23 = lA.distance_to(lB);

    double param = (P12*P12 + P13*P13 - P23*P23)/(2 * P12 * P13+.00000000001);
    if (param < -1) param = -1;
    if (param >  1) param =  1;
    double retval = acos(param);
    if (isnan(retval))
    {
        std::cerr << "P12 " << P12 << " P13 " << P13 << " P23 " << P23 << std::endl;
        throw 0xbad9a9;
    }
    return retval;
}

double Point::magnitude() const
{
    return sqrt(x*x + y*y + z*z);
}

void Point::scale(double new_magn)
{
    double old_magn = magnitude();
    if (!old_magn) return;
    double multiplier = new_magn / old_magn;
    x *= multiplier;
    y *= multiplier;
    z *= multiplier;
}

json Point::to_json()
{
    return {{"x", x}, {"y", y}, {"z", z}};
}

bool Point::from_json(json j)
{
    try { j.at("x").get_to(x); } catch (...) { ; }
    try { j.at("y").get_to(y); } catch (...) { ; }
    try { j.at("z").get_to(z); } catch (...) { ; }
    return true;
}

Point Point::from_ra_dec(double right_ascension, double declination, double distance, double node)
{
    double x, y, z;
    x = distance * -sin(right_ascension+node) * cos(declination);
    y = distance *  sin(declination);
    z = distance *  cos(right_ascension+node) * cos(declination);
    return Point(x, y, z);
}

double find_angle_along_vector(Point pt1, Point pt2, Point source, Point v)
{
    Point lpt1 = pt1 - source;
    Point lpt2 = pt2 - source;

    // Rotate points so v becomes Z axis.
    Point cen;
    Point z(0,0,1);
    Rotation rots = align_points_3d(v, z, cen);
    Point npt1 = rotate3D(lpt1, cen, rots.v, rots.a);
    Point npt2 = rotate3D(lpt2, cen, rots.v, rots.a);

    // Return the XY angle between the points.
    npt1.z = 0;
    npt2.z = 0;
    // return find_3d_angle(&npt1, &npt2, &z);
    double a1 = find_angle(npt1.x, npt1.y);
    if (a1 > M_PI) a1 -= M_PI*2;
    double a2 = find_angle(npt2.x, npt2.y);
    if (a2 > M_PI) a2 -= M_PI*2;
    return a2 - a1;
}

Point rotate3D(Point point, Point source, Point axis, double theta)
{
    // Originally from https://web.archive.org/web/20131229124319/http://inside.mines.edu/fs_home/gmurray/ArbitraryAxisRotation/

    double axisr = axis.magnitude();
    if (!axisr) return point;

    double x = point.x, y = point.y, z = point.z;
    double u = axis.x / axisr, v = axis.y / axisr, w = axis.z / axisr;
    double a, b, c;
    a = source.x;
    b = source.y;
    c = source.z;
    double u2 = u*u;
    double v2 = v*v;
    double w2 = w*w;
    double sint = sin(theta), cost = cos(theta), _1_cost = (1.0 - cost);

    double x1 = (a * (v2+w2) - u * (b*v + c*w - u*x - v*y - w*z)) * _1_cost
               + x * cost
               + (-c*v + b*w - w*y + v*z) * sint;

    double y1 = (b * (u2+w2) - v * (a*u + c*w - u*x - v*y - w*z)) * _1_cost
               + y * cost
               + ( c*u - a*w + w*x - u*z) * sint;

    double z1 = (c * (u2+v2) - w * (a*u + b*v - u*x - v*y - w*z)) * _1_cost
               + z * cost
               + (-b*u + a*v - v*x + u*y) * sint;

    Point pt(x1,y1,z1);
    return pt;
}

Rotation system_plane_from_incl_and_node(double inclination, double ascending_node, Point system_center)
{
    Point normal, axis;
    if (system_center.magnitude())
    {
        normal = compute_normal(center, system_center, yaxis);
        axis = system_center - center;
    }
    else
    {
        normal = xaxis;
        axis = center - yaxis;
    }
    normal.scale(1);
    axis.scale(1);

    // Incline
    Point pole = axis * -cos(inclination) + normal * sin(inclination);

    // Then orient
    pole = rotate3D(pole, center, axis, ascending_node);

    /*
    // First, solve for inclination
    Rotation inclined;
    if (system_center.magnitude())
        inclined = align_points_3d(center,
            Point( 0, cos(inclination) * light_year*1e9, sin(inclination) * light_year*1e9 ),
            system_center);
    else inclined = align_points_3d(yaxis,
            Point( 0, cos(inclination) * light_year*1e9, sin(inclination) * light_year*1e9 ),
            center);

    // Then incline the stars' pole
    Point pole = rotate3D(yaxis, center, inclined.v, -inclined.a);

    // Then rotate along the Sun-star axis
    if (1) // system_center.magnitude())
    {
        Point axis = (system_center.magnitude()) ? (system_center - center) : yaxis;
        pole = rotate3D(pole, center, axis, (ascending_node - M_PI/2));
    }
        */

    // Then realign the points for the new pole
    return align_points_3d(pole, yaxis, center);
}

Rotation align_points_3d(Point point, Point align, Point center)
{
    Point n = compute_normal(point, align, center);
    double nr = n.magnitude();

    if (nr < 1e-13)
    {
        Point lpt, lan;

        lpt = point;
        lan = align;

        lpt.scale(1);
        lan.scale(1);

        Rotation rot;
        if (lpt.distance_to(lan) < 1e-13)
        {
            rot.v = n;
            rot.a = 0;
            return rot;
        }

        Point pt(0,0,1);
        n = compute_normal(point, align, pt);
        if (nr < 1e-6)
        {
            pt = Point(0,0,1);
            n = compute_normal(point, align, pt);
        }

        rot.v = n;
        rot.a = M_PI;
        return rot;
    }

    // Find the 3D angle between pp and pl relative to center.
    double theta = find_3D_angle(point, align, center);
    // cout << " theta = " << theta << " ";

    // Rotate pl positively or negatively along that normal by the found angle, and use the better of the two values.
    Point plus  = rotate3D(point, center, n,  theta);
    Point minus = rotate3D(point, center, n, -theta);

    double rplus  = plus.distance_to(align);
    double rminus = minus.distance_to(align);

    double angle;
    if (rplus <= rminus) angle =  theta;
    else                 angle = -theta;

    Rotation rot;
    rot.v = n;
    rot.a = angle;
    return rot;
}

Point compute_normal(Point pt1, Point pt2, Point pt3)
{
    Point U = pt2 - pt1;
    Point V = pt3 - pt1;

    return Point(U.y * V.z - U.z * V.y,
                 U.z * V.x - U.x * V.z,
                 U.x * V.y - U.y * V.x
                );
}

std::string Point::printable() const
{
    std::stringstream buffer;
    buffer << "[" << std::setprecision(13) << x << ","  << std::setprecision(13) << y << ","  << std::setprecision(13) << z << "]";
    return buffer.str();
}

std::ostream& operator<<(std::ostream& os, const Point& p)
{
    os << p.printable();
    return os;
}

bool operator==(const Point &p, const Point &q)
{
    return p.x == q.x && p.y == q.y && p.z == q.z;
}

double CelestialLocation::distance_to(CelestialLocation other)
{
    Point relloc = (system_center - other.system_center) + (local_position - other.local_position);
    return relloc.magnitude();
}

CelestialLocation CelestialLocation::operator-(CelestialLocation other)             // it sure is nice that this fuction does its job!
{
    CelestialLocation result = *this;
    result.system_center -= other.system_center;
    result.local_position -= other.local_position;
    return result;
}

CelestialLocation &CelestialLocation::operator-=(CelestialLocation other)
{
    system_center -= other.system_center;
    local_position -= other.local_position;
    return *this;
}

json CelestialLocation::to_json()
{
    return
    {
        {"system_center", system_center.to_json()},
        {"local_position", local_position.to_json()},
        {"local_system_plane", local_system_plane.to_json()},
        {"orbital_plane", orbital_plane.to_json()},
        {"equatorial_plane", equatorial_plane.to_json()}
    };
}

bool CelestialLocation::from_json(json j)
{
    try
    {
        json j1 = j.at("");
        system_center.from_json(j1);
    } catch (...) { ; }
    try
    {
        json j1 = j.at("local_position");
        local_position.from_json(j1);
    } catch (...) { ; }
    try
    {
        json j1 = j.at("local_system_plane");
        local_system_plane.from_json(j1);
    } catch (...) { ; }
    try
    {
        json j1 = j.at("orbital_plane");
        orbital_plane.from_json(j1);
    } catch (...) { ; }
    try
    {
        json j1 = j.at("equatorial_plane");
        equatorial_plane.from_json(j1);
    } catch (...) { ; }
    return true;
}

bool Box::point_in_box(Point pt)
{
    return (pt.x >= corner1.x && pt.x <= corner2.x
         && pt.y >= corner1.y && pt.y <= corner2.y
         && pt.z >= corner1.z && pt.z <= corner2.z
            );
}

json Box::to_json()
{
    return
    {
        {"corner1", corner1.to_json()},
        {"corner2", corner2.to_json()}
    };
}

bool Box::from_json(json j)
{
    try
    {
        json j1 = j.at("corner1");
        corner1.from_json(j1);
    } catch (...) { ; }
    try
    {
        json j1 = j.at("corner1");
        corner1.from_json(j1);
    } catch (...) { ; }
    return true;
}

json Rotation::to_json()
{
    return {{"v", v.to_json()}, {"a", a*fiftyseven}};
}

bool Rotation::from_json(json j)
{
    try
    {
        json j1 = j.at("v");
        v.from_json(j1);
    } catch (...) { ; }
    try { j.at("a").get_to(a); a*=fiftyseventh; } catch (...) { ; }
    return true;
}
