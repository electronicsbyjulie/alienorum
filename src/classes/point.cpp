
#include <math.h>
#include <string>
#include <cctype>
#include <iostream>
#include <algorithm>
#include <sstream>
#include "point.h"
#include "color.h"

using namespace alienorum;

Point center(0,0,0), xaxis(1e37, 0, 0), yaxis(0, 1e37, 0), zaxis(0, 0, 1e37);

Point solar_north    = Point::from_ra_dec(solar_north_RA_J2000,    solar_north_Decl_J2000,    light_year);
Point ecliptic_north = Point::from_ra_dec(ecliptic_north_RA_J2000, ecliptic_north_Decl_J2000, light_year);
// Deliberately the IAU's SOUTH galactic pole, negated into this program's north.
//
// Astronomers picked their north galactic pole by its Earth-based declination, and the result is
// that the Galaxy turns the wrong way round it: the Sun runs towards galactic longitude 90, so by
// the right-hand rule the Galaxy's angular momentum points at the IAU *south* pole and the rotation
// reads as retrograde. Every other spin axis in this program is defined by the rotation itself, so
// following the IAU here would make the Galaxy the one object that turns backwards.
//
// Flipping it costs nothing and buys consistency: the Earth and the rest of the solar system simply
// come out upside down relative to the published convention. galactic_north_RA_J2000 and
// galactic_north_Decl_J2000 (misc.h) still hold the IAU values, for anything that has to speak to
// the outside world in published galactic coordinates.
Point galactic_north = Point::from_ra_dec(galactic_north_RA_J2000, galactic_north_Decl_J2000, light_year) * -1.0;
Point milky_way_center_Mly;

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
    *this = cel.local_position + cel.system_center + cel.galactic_center * light_year * 1e+6;
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

Point Point::operator-(const Point other)
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
    if (view_mode == vm_skymap)
    {
        // Don't know yet if we're going to die without scene esquilax and hazard mouth connection.
        double ra = std::fmod(find_angle(pt.z, -pt.x) + _pi /* - seen_equinox + azimuth_correction */ + az, _pi*2);
        if (ra < 0) ra += _pi*2;
        double decl = std::fmod(find_angle(sqrt(pt.x*pt.x+pt.z*pt.z), pt.y) - alt, _pi*2);
        if (decl > _pi/2) decl -= _pi*2;
        x = (1.0 - ra/_pi) * zoom;
        y = -decl/_pi * zoom;
    }
    else
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
    double r2 = pow(a.distance_to(b), 2);
    if (!r2) return distance_to(a);

    double t = fmax(0, fmin(1, ((x - a.x) * (b.x - a.x) + (y - a.y) * (b.y - a.y) + (z - a.z) * (b.z - a.z)) / r2));
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

double Cartesian2D::distance_to(Cartesian2D other)
{
    double dx = x - other.x, dy = y - other.y;              // don't have to fabs() because (-x)^2 = x^2.
    return sqrt(dx*dx+dy*dy);
}

double find_angle(double dx, double dy)
{
    double angle = atan2(dy,dx);
    if (angle < 0)
    {
        angle += 2 * _pi;
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
        assert(!isnan(retval));
    }
    return retval;
}

double Point::magnitude() const
{
    return sqrt(x*x + y*y + z*z);
}

double Point::squared_magnitude() const
{
    return x*x + y*y + z*z;
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
    if (a1 > _pi) a1 -= _pi*2;
    double a2 = find_angle(npt2.x, npt2.y);
    if (a2 > _pi) a2 -= _pi*2;
    return a2 - a1;
}

double interpolate_angles(double theta0, double theta1, double coeff1)
{
    double coeff0 = 1.0 - coeff1;
    double delta = fabs(theta0 - theta1);
    if (theta0 < theta1)
    {
        if (fabs(theta0 + _pi + _pi - theta1) < delta) theta0 += _pi+_pi;
    }
    else if (fabs(theta0 - _pi - _pi - theta1) < delta) theta0 -= _pi+_pi;

    return coeff0 * theta0 + coeff1 * theta1;
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
    pole.scale(1);

    // Then orient
    pole = rotate3D(pole, center, axis, ascending_node);

    // Then realign the points for the new pole
    return align_points_3d(pole, yaxis, center);
}

void incl_and_node_from_system_plane(Rotation plane, double& out_inclination, double& out_ascending_node, Point system_center)
{
    Point pole = rotate3D(yaxis, center, plane.v, -plane.a);
    pole.scale(1);

    // Get the normal and the axis
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

    // 1. Recover the inclination
    double dot_axis = (pole.x * axis.x) + (pole.y * axis.y) + (pole.z * axis.z);

    // FIX 3: Clamp to [-1, 1] to prevent floating-point precision 'nan'
    if (dot_axis > 1.0)  dot_axis = 1.0;
    if (dot_axis < -1.0) dot_axis = -1.0;

    out_inclination = std::acos(-dot_axis);

    // 2. Recover the ascending node
    double dot_normal = (pole.x * normal.x) + (pole.y * normal.y) + (pole.z * normal.z);

    // Create a binormal vector (axis x normal)
    Point binormal;
    binormal.x = (axis.y * normal.z) - (axis.z * normal.y);
    binormal.y = (axis.z * normal.x) - (axis.x * normal.z);
    binormal.z = (axis.x * normal.y) - (axis.y * normal.x);

    double dot_binormal = (pole.x * binormal.x) + (pole.y * binormal.y) + (pole.z * binormal.z);

    out_ascending_node = std::atan2(dot_binormal, dot_normal);

    // If zero inclination, set zero node. Otherwise normalize node to 0-360 degrees.
    if (fabs(sin(out_inclination)) < 1e-3) out_ascending_node = 0;
    else if (out_ascending_node < 0.0)
    {
        out_ascending_node += 2.0 * _pi;
    }
}
double distance(ImVec2 a, ImVec2 b)
{
    double dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx*dx+dy*dy);
}

Rotation tilt_plane_to_heliocentric_inclination(Point system_center, Rotation original, double helioincl)
{
    // Find the pole of the original plane.
    Point pole = rotate3D(yaxis, center, original.v, original.a);
    pole.scale(light_year);

    // Get how far in what direction to rotate that pole for a zero heliocentric inclination.
    Rotation tmp = align_points_3d(system_center + pole, center, system_center);

    // Rotate the pole for the desired inclination and return its plane.
    pole = rotate3D(pole, center, tmp.v, tmp.a - helioincl);
    return align_points_3d(yaxis, pole, center);
}

void elements_in_new_reference_plane(Rotation original, Rotation reference, double &out_new_inclination, double &out_new_node)
{
    #if 0
    Point pole = rotate3D(yaxis, center, original.v, original.a);
    pole.scale(1);
    Point refpole = rotate3D(yaxis, center, reference.v, reference.a);
    refpole.scale(1);
    Rotation tmp = align_points_3d(pole, refpole, center);
    out_new_inclination = tmp.a;
    out_new_node = find_angle_along_vector(zaxis, tmp.v, center, refpole);
    #else
    // 1. Get the planet's universal pole vector
    Point pole = rotate3D(yaxis, center, original.v, original.a);
    pole.scale(1);
    
    // 2. Un-rotate the planet's pole by the reference plane's rotation.
    // This shifts us into a local frame where the reference plane is perfectly flat.
    Point local_pole = rotate3D(pole, center, reference.v, -reference.a);
    
    // Defensive guard: Scale to 1 to strip out any 1e37 amplification from rotate3D
    local_pole.scale(1);

    // 3. Recover the local inclination
    // In this frame, the reference pole is the universal yaxis. 
    // The local inclination is just the angle between local_pole and yaxis.
    double cos_i = local_pole.y; 
    if (cos_i > 1.0)  cos_i = 1.0;
    if (cos_i < -1.0) cos_i = -1.0;
    out_new_inclination = std::acos(cos_i);

    // 4. Recover the local ascending node
    // The local node is the angle of the pole's projection in the local X-Z plane,
    // measuring from the local x-axis (xaxis) toward the local z-axis (zaxis).
    out_new_node = std::atan2(local_pole.z, local_pole.x) + _pi;

    // Normalize the node to [0, 2*pi)
    if (out_new_node < 0.0)
    {
        out_new_node += 2.0 * _pi;
    }

    // 5. Handle Gimbal Lock for perfectly face-on orbits
    if (out_new_inclination < 1e-7 || out_new_inclination > (_pi - 1e-7))
    {
        out_new_node = 0.0;
    }
    #endif
}

void wrapped_line(ImVec2 term1, ImVec2 term2, ImU32 color, ImGuiIO& io)
{
    wrapped_line(term1, term2, color, 1, io);
}

void wrapped_line(ImVec2 term1, ImVec2 term2, ImU32 color, float thickness, ImGuiIO& io)
{
    int dcx = (int)io.DisplaySize.x / 2;
    if ((view_mode == vm_skymap || view_mode == vm_sunclock)
        && fabs(term1.x - term2.x) > zoom*dcx
        && ((term1.x < dcx && term2.x > dcx)
            ||
            (term1.x > dcx && term2.x < dcx)
           )
        )
    {
        ImVec2 term3=term2, term4=term1;

        if (term3.x > dcx) term3.x -= dcx*2;
        else term3.x += dcx*2;
        if (term4.x > dcx) term4.x -= dcx*2;
        else term4.x += dcx*2;

        ImGui::GetBackgroundDrawList()->AddLine(term1, term3, rgba_apply_redlight(color), thickness);
        ImGui::GetBackgroundDrawList()->AddLine(term2, term4, rgba_apply_redlight(color), thickness);
    }
    else
    {
        ImGui::GetBackgroundDrawList()->AddLine(term1, term2, rgba_apply_redlight(color), thickness);
    }
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
        rot.a = _pi;
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

std::ostream &operator<<(std::ostream &os, const Rotation &r)
{
    os << r.v.printable();
    os << "/" << (r.a * fiftyseven);
    return os;
}

std::ostream &operator<<(std::ostream &os, const ImVec2 &v)
{
    os << v.x << "," << v.y;
    return os;
}

bool operator==(const Point &p, const Point &q)
{
    return p.x == q.x && p.y == q.y && p.z == q.z;
}

double CelestialLocation::distance_to(CelestialLocation other)
{
    Point relloc = (galactic_center - other.galactic_center) * light_year * 1e+6
    + (system_center - other.system_center)
    + (local_position - other.local_position);
    return relloc.magnitude();
}

double CelestialLocation::squared_distance_to(CelestialLocation other)
{
    Point relloc = (galactic_center - other.galactic_center) * light_year * 1e+6
    + (system_center - other.system_center)
    + (local_position - other.local_position);
    return relloc.squared_magnitude();
}

CelestialLocation CelestialLocation::operator-(CelestialLocation other)             // it sure is nice that this fuction does its job!
{
    CelestialLocation result = *this;
    result.galactic_center -= other.galactic_center;
    result.system_center -= other.system_center;
    result.local_position -= other.local_position;
    return result;
}

CelestialLocation &CelestialLocation::operator-=(CelestialLocation other)
{
    galactic_center -= other.galactic_center;
    system_center -= other.system_center;
    local_position -= other.local_position;
    return *this;
}

json CelestialLocation::to_json()
{
    return
    {
        {"galactic_center", galactic_center.to_json()},
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
        json j1 = j.at("galactic_center");
        galactic_center.from_json(j1);
    } catch (...) { ; }
    try
    {
        json j1 = j.at("system_center");
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


// Calculates precession angles based on IAU 1976 formulation
// T0 and T are in Julian years from J2000.0
void get_Earth_precession_angles(double T0, double T, double& zeta, double& z, double& theta)
{
    double t = (T - T0) * 0.01;

    // convert coefficients to radians
    zeta  = ((2306.2181 + 1.39656 * T0 - 0.000139 * T0 * T0) * t
            + (0.30188 - 0.000344 * T0) * t * t
            + 0.017998 * t * t * t) * arcsecond;

    z     = ((2306.2181 + 1.39656 * T0 - 0.000139 * T0 * T0) * t
            + (1.09468 + 0.00066 * T0) * t * t
            + 0.018203 * t * t * t) * arcsecond;

    theta = ((2004.3109 - 0.8533 * T0 - 0.000217 * T0 * T0) * t
            - (0.42665 + 0.000217 * T0) * t * t
            - 0.041833 * t * t * t) * arcsecond;
}

// Rotates a vector using the calculated precession angles
Point apply_precession_rotation(const Point& vec, double zeta, double z, double theta)
{
    // Row components of the IAU 1976 precession matrix (P = Rz(-z) * Ry(theta) * Rz(-zeta))
    double r11 = std::cos(zeta) * std::cos(theta) * std::cos(z) - std::sin(zeta) * std::sin(z);
    double r12 = -std::sin(zeta) * std::cos(theta) * std::cos(z) - std::cos(zeta) * std::sin(z);
    double r13 = -std::sin(theta) * std::cos(z);

    double r21 = std::cos(zeta) * std::cos(theta) * std::sin(z) + std::sin(zeta) * std::cos(z);
    double r22 = -std::sin(zeta) * std::cos(theta) * std::sin(z) + std::cos(zeta) * std::cos(z);
    double r23 = -std::sin(theta) * std::sin(z);

    double r31 = std::cos(zeta) * std::sin(theta);
    double r32 = -std::sin(zeta) * std::sin(theta);
    double r33 = std::cos(theta);

    return Point(   r11 * vec.x + r12 * vec.y + r13 * vec.z,
                    r21 * vec.x + r22 * vec.y + r23 * vec.z,
                    r31 * vec.x + r32 * vec.y + r33 * vec.z);
}

// Main conversion function
void convert_to_J2000(const double RA_radians, const double Decl_radians, double input_year, double& RA_J2000, double& Decl_J2000, bool is_besselian)
{
    double T0; // Starting epoch in Julian years from J2000.0

    if (is_besselian)
    {
        // Convert Besselian year (B1950) to Julian years relative to J2000
        // B1950.0 is exactly JD 2433282.42345905
        // J2000.0 is exactly JD 2451545.0
        double jd_b1950 = 2433282.42345905;
        double jd_input = jd_b1950 + (input_year - 1950.0) * 365.242198781; // Besselian year length
        T0 = (jd_input - 2451545.0) / 365.25;
    }
    else
    {
        // Convert Julian year (J1991.25) to Julian Centuries relative to J2000
        T0 = (input_year - 2000.0);
    }

    double T = 0.0; // Target epoch is J2000.0, which means T = 0

    double zeta, z, theta;
    get_Earth_precession_angles(T0, T, zeta, z, theta);

    Point initial_vector = Point::from_ra_dec(RA_radians, Decl_radians, light_year, 0);
    Point final_vector = apply_precession_rotation(initial_vector, zeta, z, theta);

    RA_J2000 = std::fmod(find_angle(final_vector.z, -final_vector.x), _pi*2);
    Decl_J2000 = find_angle(sqrt(final_vector.x*final_vector.x+final_vector.z*final_vector.z), final_vector.y);
}
