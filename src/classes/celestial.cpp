
#include <math.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "celestial.h"

CelestialObject **cels;
bool *celskip;
double *vmag_cache, *magrad_cache;
CelestialLocation here;

CelestialObject::CelestialObject()
{
    memset(name, 0, 32*sizeof(char));
}

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

json CelestialObject::to_json()
{
    json towrite = json::object();
    towrite["absolute_magnitude"] = absolute_magnitude;
    towrite["BV_color"] = BV_color;
    towrite["declination"] = declination * fiftyseven;
    towrite["distance"] = distance / light_year;
    towrite["distance_known"] = distance_known;
    towrite["epoch"] = epoch;
    towrite["equinox"] = equinox * fiftyseven;
    towrite["inclination"] = inclination * fiftyseven;
    towrite["location"] = location.to_json();
    towrite["mass"] = mass;
    towrite["!name"] = name;                    // want this to alphabetize to the top.
    towrite["oblateness"] = oblateness;
    if (orbit) towrite["orbit"] = orbit->to_json();
    towrite["precession"] = precession * year;
    towrite["RI_color"] = RI_color;
    towrite["right_ascension"] = right_ascension * fiftyseven;
    towrite["sidereal_rotational_period"] = sidereal_rotational_period / 86400;
    towrite["type"] = type;
    towrite["typeclass"] = typeclass();
    towrite["UB_color"] = UB_color;
    towrite["volumetric_mean_radius"] = volumetric_mean_radius;
    towrite["VR_color"] = VR_color;

    return towrite;
}

bool CelestialObject::from_json(json j)
{
    try
    {
        std::string str;
        j.at("!name").get_to(str);
        strcpy(name, str.c_str());
    } catch (...) { ; }
    try { j.at("typeclass").get_to(_class); } catch (...) { ; }
    try { j.at("absolute_magnitude").get_to(absolute_magnitude); } catch (...) { ; }
    try { j.at("BV_color").get_to(BV_color); } catch (...) { ; }
    try { j.at("declination").get_to(declination); declination *= fiftyseventh; } catch (...) { ; }
    try { j.at("distance").get_to(distance); distance *= light_year; } catch (...) { ; }
    try { j.at("distance_known").get_to(distance_known); if (!distance_known && !distance) distance = 1e4+light_year; } catch (...) { ; }
    try { j.at("epoch").get_to(epoch); } catch (...) { ; }
    try { j.at("equinox").get_to(equinox); equinox *= fiftyseventh; } catch (...) { ; }
    try { j.at("inclination").get_to(inclination); inclination *= fiftyseventh; } catch (...) { ; }
    try
    {
        json j1 = j.at("location");
        location.from_json(j1);
    } catch (...) { ; }
    try { j.at("mass").get_to(mass); } catch (...) { ; }
    try { j.at("oblateness").get_to(oblateness); } catch (...) { ; }
    try
    {
        json j1 = j.at("orbit");
        orbit = new Orbit();
        orbit->from_json(j1);
    } catch (...) { ; }
    try { j.at("precession").get_to(precession); } catch (...) { ; }
    try { j.at("RI_color").get_to(RI_color); } catch (...) { ; }
    try { j.at("right_ascension").get_to(right_ascension); right_ascension *= fiftyseventh; } catch (...) { ; }
    try { j.at("sidereal_rotational_period").get_to(sidereal_rotational_period); sidereal_rotational_period /= 86400; } catch (...) { ; }
    try { j.at("type").get_to(type); } catch (...) { ; }
    try { j.at("UB_color").get_to(UB_color); } catch (...) { ; }
    try { j.at("volumetric_mean_radius").get_to(volumetric_mean_radius); } catch (...) { ; }
    try { j.at("VR_color").get_to(VR_color); } catch (...) { ; }

    // Here's a blank for adding more fields (copy-paste into other classes' from_json() functions):
    // try { j.at("").get_to(); } catch (...) { ; }

    // Here's a blank for adding more object fields:
    /* try
    {
        json j1 = j.at("");
        .from_json(j1);
    } catch (...) { ; } */

    // And a blank for adding more object pointer fields:
    /* try
    {
        json j1 = j.at("");
         = new ();
        ->from_json(j1);
    } catch (...) { ; } */

    // All that trouble to turn std::strings into char arrays because they weren't serializable, and guess what...
    // we wanted the std::strings after all. Smh. TODO: Convert all the char[]s back to std::strings.
    /* try
    {
        std::string str;
        j.at("").get_to(str);
        strcpy(, str.c_str());
    } catch (...) { ; } */

    return true;
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

    if (_class == class_moon && !crp)
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

json Orbit::to_json()
{
    return
    {
        {"center_name", center->name},
        {"ascending_node", ascending_node*fiftyseven},
        {"inclination", inclination*fiftyseven},
        {"semimajor_axis", semimajor_axis},
        {"eccentricity", eccentricity},
        {"arg_periapsis", arg_periapsis*fiftyseven},
        {"prec_node", prec_node*fiftyseven*year},
        {"proc_argperi", proc_argperi*fiftyseven*year},
        {"mean_anomaly", mean_anomaly*fiftyseven},
        {"epoch", epoch},
        {"period", period/86400},
        {"laplace", laplace.to_json()}
    };
}

bool Orbit::from_json(json j)
{
    try { j.at("center_name").get_to(center_name); } catch (...) { ; }
    try { j.at("ascending_node").get_to(ascending_node); ascending_node *= fiftyseventh; } catch (...) { ; }
    try { j.at("inclination").get_to(inclination); inclination *= fiftyseventh; } catch (...) { ; }
    try { j.at("semimajor_axis").get_to(semimajor_axis); } catch (...) { ; }
    try { j.at("eccentricity").get_to(eccentricity); } catch (...) { ; }
    try { j.at("arg_periapsis").get_to(arg_periapsis); arg_periapsis *= fiftyseventh; } catch (...) { ; }
    try { j.at("mean_anomaly").get_to(mean_anomaly); mean_anomaly *= fiftyseventh; } catch (...) { ; }
    try { j.at("epoch").get_to(epoch); } catch (...) { ; }
    try { j.at("period").get_to(period); period *= 86400; } catch (...) { ; }
    try { j.at("prec_node").get_to(prec_node); prec_node *= fiftyseventh / year; } catch (...) { ; }
    try { j.at("proc_argperi").get_to(proc_argperi); proc_argperi *= fiftyseventh / year; } catch (...) { ; }
    try
    {
        json j1 = j.at("laplace");
        laplace.from_json(j1);
    } catch (...) { ; }

    return true;
}
