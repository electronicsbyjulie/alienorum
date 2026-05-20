
#include <math.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "celestial.h"

CelestialObject **cels;
bool *celskip;
double *vmag_cache, *magrad_cache;
CelestialLocation here;

double CelestialObject::viewer_magnitude(CelestialLocation seen_from)
{
    if (type == rocky || type == ice_giant || type == gas_giant)
    {
        std::cerr << "Called viewer_magnitude() on a non-self-luminous object." << std::endl;
        throw 0xbadc0de;
    }
    double r = seen_from.distance_to(location) / parsec / 10;
    double intrinsic = pow(magnbase, -absolute_magnitude);
    double apparent = intrinsic / (r*r);
    return -log(apparent) * invlogmagnbase;
}

double CelestialObject::distance_from_magnitudes(double apparent, double absolute)
{
    double flux = pow(magnbase, -apparent);
    double intrinsic = pow(magnbase, -absolute);
    double ratio = intrinsic / flux;
    return parsec * 10 * sqrt(ratio);
}

std::string CelestialObject::RA_as_hms()
{
    double RA = right_ascension * fiftyseven / 15;
    int hours = floor(RA);
    RA = (RA-hours) * 60;
    int minutes = floor(RA);
    double seconds = (RA-minutes) * 60;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << seconds;
    std::string sec = oss.str();

    return std::string(hours<10 ? "0" : "")
        + std::to_string(hours) + std::string(":")
        + std::string(minutes<10 ? "0" : "")
        + std::to_string(minutes) + std::string(":")
        + std::string(seconds<10 ? "0" : "")
        + sec;
}

std::string CelestialObject::Decl_as_degms()
{
    int sign = (declination < 0) ? -1 : 1;
    double decl = fabs(declination * fiftyseven);
    int degrees = floor(decl);
    decl = (decl-degrees) * 60;
    int minutes = floor(decl);
    double seconds = (decl-minutes) * 60;
    return std::string( sign < 0 ? "-" : "+" )
        + std::string(degrees<10 ? "0" : "")
        + std::to_string(degrees) + std::string(":")
        + std::string(minutes<10 ? "0" : "")
        + std::to_string(minutes) + std::string(":")
        + std::string(seconds<10 ? "0" : "")
        + std::to_string((int)seconds);
}

std::string CelestialObject::RA_as_hms(CelestialLocation seen_from)
{
    double relRA = RA_as_radians(seen_from) * fiftyseven / 15;
    int hours = floor(relRA);
    relRA = (relRA-hours) * 60;
    int minutes = floor(relRA);
    double seconds = (relRA-minutes) * 60;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << seconds;
    std::string sec = oss.str();

    return std::string(hours<10 ? "0" : "")
        + std::to_string(hours) + std::string(":")
        + std::string(minutes<10 ? "0" : "")
        + std::to_string(minutes) + std::string(":")
        + std::string(seconds<10 ? "0" : "")
        + sec;
}

std::string CelestialObject::Decl_as_degms(CelestialLocation seen_from)
{
    double relDecl = Decl_as_radians(seen_from) * fiftyseven;
    if (relDecl > 90) relDecl -= 360;
    int sign = (relDecl < 0) ? -1 : 1;
    double decl = fabs(relDecl);
    int degrees = floor(decl);
    decl = (decl-degrees) * 60;
    int minutes = floor(decl);
    double seconds = (decl-minutes) * 60;
    return std::string( sign < 0 ? "-" : "+" )
        + std::string(degrees<10 ? "0" : "")
        + std::to_string(degrees) + std::string(":")
        + std::string(minutes<10 ? "0" : "")
        + std::to_string(minutes) + std::string(":")
        + std::string(seconds<10 ? "0" : "")
        + std::to_string((int)seconds);
    return std::string();
}

double CelestialObject::RA_as_radians(CelestialLocation seen_from)
{
    Point relloc = (location.system_center - seen_from.system_center) + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    relloc = rotate3D(relloc, center, seen_from.orbital_plane.v, seen_from.orbital_plane.a);
    relloc = rotate3D(relloc, center, seen_from.local_system_plane.v, seen_from.local_system_plane.a);
    return find_angle(relloc.z, -relloc.x);
}

double CelestialObject::Decl_as_radians(CelestialLocation seen_from)
{
    Point relloc = (location.system_center - seen_from.system_center) + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    relloc = rotate3D(relloc, center, seen_from.orbital_plane.v, seen_from.orbital_plane.a);
    relloc = rotate3D(relloc, center, seen_from.local_system_plane.v, seen_from.local_system_plane.a);
    double result = find_angle(sqrt(relloc.x*relloc.x+relloc.z*relloc.z), relloc.y);
    if (result > M_PI/2) result -= M_PI*2;
    return result;
}

std::string CelestialObject::scaled_distance(CelestialLocation fromwhere)
{
    double r = location.distance_to(fromwhere), dispr = r;
    std::string units = " m";

    if (r >= light_year)
    {
        dispr = r / light_year;
        units = " ly";
    }
    else if (r >= AU/10)
    {
        dispr = r / AU;
        units = " AU";
    }
    else if (r >= 950)    // TODO: Offer choice of imperial units for < 1 AU.
    {
        dispr = r / 1000;
        units = " km";
    }

    std::ostringstream oss;
    oss << std::setprecision(5) << dispr << units;
    return oss.str();
}

void CelestialObject::update_orbit_location(double tmnow, Rotation* crp)
{
    if (!orbit || !orbit->center) return;

    // Calculate orbit radians per second and seconds since epoch
    double rads_sec = (M_PI * 2) / orbit->period;
    double seconds_since_epoch = (tmnow - J2000_TIME_T) + ((J2000 - epoch)*86400);

    // Precess the ascending node and process the arg peri
    double node_adjustment = seconds_since_epoch * orbit->prec_node;
    double peri_adjustment = seconds_since_epoch * orbit->proc_argperi;
    double node = orbit->ascending_node - node_adjustment;
    double argperi = orbit->arg_periapsis + peri_adjustment;

    // Calculate current Mean Anomaly
    double M = orbit->mean_anomaly + rads_sec * seconds_since_epoch + node_adjustment - peri_adjustment;
    M = std::fmod(M, 2.0 * M_PI);

    // Solve for Eccentric Anomaly
    double E = solve_Kepler(M, orbit->eccentricity);

    // Calculate position in orbital plane (x', y')
    double x_plane = orbit->semimajor_axis * (std::cos(E) - orbit->eccentricity);
    double y_plane = orbit->semimajor_axis * std::sqrt(1.0 - orbit->eccentricity * orbit->eccentricity) * std::sin(E);

    // Rotate to 3D Heliocentric Coordinates
    double cosO = std::cos(node);
    double sinO = std::sin(node);
    double cosw = std::cos(argperi);
    double sinw = std::sin(argperi);
    double cosi = std::cos(orbit->inclination);
    double sini = std::sin(orbit->inclination);

    double x = (-sinO * cosw -  cosO * sinw * cosi) * x_plane + ( sinO * sinw -  cosO * cosw * cosi) * y_plane;
    double y = (                       sinw * sini) * x_plane + (                       cosw * sini) * y_plane;
    double z = ( cosO * cosw + -sinO * sinw * cosi) * x_plane + (-cosO * sinw + -sinO * cosw * cosi) * y_plane;
    location.orbital_plane.v = Point(cosO, 0, sinO);
    location.orbital_plane.a = orbit->inclination;

    if (type != star && orbit->center->type != star && orbit->center->orbit && orbit->center->orbit->center && !crp)
    {
        std::cerr << "CelestialObject::update_orbit_location() called on moon " << name
            << " of planet " << orbit->center->name
            << " of star " << orbit->center->orbit->center->name
            << " without Laplace plane." << std::endl;
        throw 0xbadc0de;
    }

    Point result;
    if (crp) result = rotate3D(Point(x,y,z), center, crp->v, -crp->a);
    // For exoplanets, assume the planetary orbits and stellar equator are in the same plane and set the stellar inclination to zero.
    else result = rotate3D(Point(x,y,z), center, location.local_system_plane.v, -location.local_system_plane.a);

    location.system_center = orbit->center->location.system_center;
    location.local_position = result + orbit->center->location.local_position;
}
