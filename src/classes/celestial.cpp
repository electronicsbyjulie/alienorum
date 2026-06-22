
#include <math.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include "celestial.h"
#include "star.h"
#include "planet.h"

using namespace alienorum;

CelestialObject **cels, *mycenobj = nullptr;
std::vector<std::vector<CelestialObject*>> first_letter_index;
std::map<std::string,std::vector<CelestialObject*>> constellation_index;

bool *celskip, *discinstead;
double *vmag_cache, *bloomrad_cache, *angular_radius;
CelestialLocation here;
double azimuth_correction = 0;
typedef struct my_jpeg_error_mgr * my_error_ptr;

CelestialObject::CelestialObject()
{
    memset(name, 0, 32*sizeof(char));
}

CelestialObject *CelestialObject::get_light_center()
{
    if (!orbit || !orbit->center)
    {
        if (type == star) return this;
        return nullptr;
    }

    CelestialObject *light_center = this;
    int i;
    for (i=0; i<5; i++)
    {
        cel_obj_class cls = light_center->typeclass();
        if (cls == class_star) break;
        if (cls == class_planet || cls == class_moon)
        {
            light_center = light_center->orbit->center;
            if (!light_center)
            {
                std::cerr << "Cannot find light source for " << name << std::endl;
                throw 0xbadda7a;
            }
            // std::cout << "Light source for " << name << " is " << light_center->name << std::endl;
        }
        else break;
    }
    return light_center;
}

double CelestialObject::viewer_magnitude(CelestialLocation seen_from)
{
    cel_obj_class cls = typeclass();
    if (cls == class_planet || cls == class_moon)       // TODO: Some hot jupiters are both reflective and self luminous.
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

alienorum::Orbit::~Orbit()
{
    if (osculating) delete[] osculating;
}

bool alienorum::Orbit::read_osc_elements(std::string cel_name)
{
    num_osc = 0;
    std::string filename = std::string("ephemerides/") + cel_name + std::string(".txt");
    if (!file_exists(filename.c_str()))
    {
        std::string gzfilename = filename + std::string(".gz");
        if (file_exists(gzfilename.c_str()))
        {
            #ifdef _WIN32
            std::string cmd = (std::string)"7z e -y " + gzfilename;
            #else
            std::string cmd = (std::string)"gunzip -k " + gzfilename;
            #endif
            std::cout << cmd << std::endl;
            std::system(cmd.c_str());
        }
        if (!file_exists(filename.c_str())) return false;
    }

    osculating = OsculatingElement::read_from_file(filename, &num_osc);
    #ifdef DEBUG
    std::cout << "Read " << num_osc << " osculating ephemerides for " << cel_name << std::endl << std::flush;
    #endif
    return (num_osc > 0);
}

void alienorum::Orbit::interpolate_osculating_e(double for_epoch, double &n, double &i, double &w, double &a, double &e, double &m, double &p, double& precn, double& procarg, double& effe)
{
    long int h = num_osc - 1;
    if (!num_osc || for_epoch < osculating[0].epoch || for_epoch > osculating[h].epoch)
    {
        // If no osculating parameters, just use mean.
        n       = ascending_node;
        i       = inclination;
        w       = arg_periapsis;
        a       = semimajor_axis;
        e       = eccentricity;
        m       = mean_anomaly;
        p       = period;
        precn   = prec_node;
        procarg = proc_argperi;
        effe    = epoch;
        return;
    }

    // A maybe faster way to search.
    long int l = num_osc/2, j = l/2;
    while (1)                           // DANGER!
    {
        if (l <= 0) break;
        if ((unsigned)l >= num_osc) break;
        if (!j) break;

        #if 0
        std::cout << std::fixed << l << "/" << num_osc << ": " << osculating[l].epoch << " vs. " << for_epoch << " vs. "
            << osculating[l+1].epoch << " +/-" << j << std::endl << std::flush;
        #endif
        if (osculating[l].epoch > for_epoch) l -= j;
        else if (osculating[l+1].epoch < for_epoch) l += j;
        else if (osculating[l].epoch <= for_epoch && osculating[l+1].epoch >= for_epoch) break;
        j /= 2;
    }
    long int k = l - 13;
    if (k < 0) k = 0;
    h = l + 13;
    for (j = k; j <= h; j++)
    {
        if (osculating[j].epoch <= for_epoch && osculating[j+1].epoch >= for_epoch)
        {
            double coeff1 = (for_epoch - osculating[j].epoch) / (osculating[j+1].epoch - osculating[j].epoch), coeff0 = 1.0 - coeff1;
            // n = coeff0 * osculating[j].ascending_node   + coeff1 * osculating[j+1].ascending_node;
            n = interpolate_angles(osculating[j].ascending_node, osculating[j+1].ascending_node, coeff1);
            i = coeff0 * osculating[j].inclination      + coeff1 * osculating[j+1].inclination;
            // w = coeff0 * osculating[j].arg_perifocus    + coeff1 * osculating[j+1].arg_perifocus;
            w = interpolate_angles(osculating[j].arg_perifocus, osculating[j+1].arg_perifocus, coeff1);
            a = coeff0 * osculating[j].semimajor_axis   + coeff1 * osculating[j+1].semimajor_axis;
            e = coeff0 * osculating[j].eccentricity     + coeff1 * osculating[j+1].eccentricity;
            // m = coeff0 * osculating[j].mean_anomaly     + coeff1 * osculating[j+1].mean_anomaly;
            m = 0; // interpolate_angles(osculating[j].mean_anomaly, osculating[j+1].mean_anomaly, coeff1);
            p = coeff0 * osculating[j].period           + coeff1 * osculating[j+1].period;
            precn = procarg = 0;
            effe = osculating[j].T_periapsis;
            return;
        }
    }

    #if 0
    std::cout << std::fixed << k << "-" << h << "/" << num_osc << ": " << osculating[k].epoch << " vs. " << for_epoch << " vs. "
        << osculating[h].epoch << " +/-" << j << std::endl << std::flush;
    #endif
    assert(false);
}

void Orbit::compute_period(double mm)
{
    if (!center) return;
    if (!center->mass)
    {
        switch (center->type)
        {
            case galaxy:
            // TODO;
            return;

            case star:
            ((Star*)(center))->estimate_mass();
            break;

            case hot_jupiter:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius) * hot_jupiter_density;
            break;

            case gas_giant: case ice_giant:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / jupiter_radius) * jupiter_mass;
            break;

            case rocky:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / earth_radius) * earth_mass;
            break;

            default:
            // TODO:
            return;
        }
    }

    double mass = center->mass + mm;
    period = (_pi+_pi) * sqrt(semimajor_axis*semimajor_axis*semimajor_axis/(G*mass));
}

void Orbit::compute_semimajor_axis(double mm)
{
    if (!center) return;
    if (!center->mass)
    {
        switch (center->type)
        {
            case galaxy:
            // TODO;
            return;

            case star:
            ((Star*)(center))->estimate_mass();
            break;

            case hot_jupiter:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius) * hot_jupiter_density;
            break;

            case gas_giant: case ice_giant:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / jupiter_radius) * jupiter_mass;
            break;

            case rocky:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / earth_radius) * earth_mass;
            break;

            default:
            // TODO:
            return;
        }
    }

    double mass = center->mass + mm;
    semimajor_axis = pow(G*mass*period*period / (_pi*_pi*4), 1.0/3);
}

void Orbit::compute_center_mass(double mm)
{
    double a3_over_gm = period / (_pi+_pi);
    a3_over_gm *= a3_over_gm;
    double GM = semimajor_axis*semimajor_axis*semimajor_axis / a3_over_gm;
    center->mass = (GM / G) - mm;
}

std::string CelestialObject::RA_as_hms(double seen_equinox)
{
    double RA = right_ascension * fiftyseven / 15 - seen_equinox;
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

std::string CelestialObject::RA_as_hms(CelestialLocation seen_from, double seen_equinox)
{
    double relRA = RA_as_radians(seen_from, seen_equinox) * fiftyseven / 15;
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

void CelestialObject::RA_from_hms(std::string ra_hms)
{
    const char* c = ra_hms.c_str();
    int i, j=0;
    double h, m, s;

    for (i=0; c[i]; i++)
    {
        if (c[i] < '0' || c[i] > '9')
        {
            if (j & 1) j++;
            continue;
        }
        else if (!j) h = c[i] - '0';
        else if (j==1) h = h*10 + (c[i] - '0');
        else if (j==2) m = c[i] - '0';
        else if (j==3) m = m*10 + (c[i] - '0');
        else if (j==4) s = c[i] - '0';
        else if (j==5)
        {
            s = s*10 + atof(&c[i]);
            break;
        }

        j++;
    }

    right_ascension = (h + m/60 + s/3600) * 15 * fiftyseventh;
}

void CelestialObject::Decl_from_degms(std::string decl_degms)
{
    const char* c = decl_degms.c_str();
    int i, j=0, sign;
    double d, m, s;

    for (i=0; c[i]; i++)
    {
        if (c[i] == '+')
        {
            sign = 1;
            continue;
        }
        else if (c[i] == '-')
        {
            sign = -1;
            continue;
        }
        else if (c[i] < '0' || c[i] > '9')
        {
            if (j & 1) j++;
            continue;
        }
        else if (!j) d = c[i] - '0';
        else if (j==1) d = d*10 + (c[i] - '0');
        else if (j==2) m = c[i] - '0';
        else if (j==3) m = m*10 + (c[i] - '0');
        else if (j==4) s = c[i] - '0';
        else if (j==5)
        {
            s = s*10 + atof(&c[i]);
            break;
        }

        j++;
    }

    declination = (d + m/60 + s/3600) * sign * fiftyseventh;
}

double CelestialObject::RA_as_radians(CelestialLocation seen_from, double seen_equinox)
{
    Point relloc = (location.system_center - seen_from.system_center) + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    double result = std::fmod(find_angle(relloc.z, -relloc.x) - seen_equinox + azimuth_correction, _pi*2);
    if (result < 0) result += _pi*2;
    return result;
}

double CelestialObject::Decl_as_radians(CelestialLocation seen_from)
{
    Point relloc = (location.system_center - seen_from.system_center) + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    double result = find_angle(sqrt(relloc.x*relloc.x+relloc.z*relloc.z), relloc.y);
    if (result > _pi/2) result -= _pi*2;
    return result;
}

std::string CelestialObject::scaled_distance(CelestialLocation fromwhere, bool is_low_orbit_sat)
{
    double r = location.distance_to(fromwhere), dispr = r;
    if (is_low_orbit_sat) r -= volumetric_mean_radius;
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
    towrite["obliquity"] = obliquity * fiftyseven;
    towrite["location"] = location.to_json();
    towrite["lon_J2000_offset"] = lon_J2000_offset*fiftyseven;
    towrite["mass"] = mass;

    // want these to alphabetize to the top.
    towrite["!name"] = name;
    if (!origname.size()) origname = name;
    towrite["!origname"] = origname;
    towrite["!origcenname"] = origcenname;

    towrite["oblateness"] = oblateness;
    if (orbit) towrite["orbit"] = orbit->to_json();
    towrite["precession"] = precession * oneyear;
    towrite["RI_color"] = RI_color;
    towrite["right_ascension"] = right_ascension * fiftyseven;
    towrite["sidereal_rotational_period"] = sidereal_rotational_period / oneday;
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
        if (str.size() >= name_max_len) str = str.substr(0, name_max_len-1);
        strcpy(name, str.c_str());
    } catch (...) { ; }
    try { j.at("!origname").get_to(origname); } catch (...) { ; }
    try { j.at("!origcenname").get_to(origcenname); } catch (...) { ; }
    try { j.at("typeclass").get_to(_class); } catch (...) { ; }
    try { j.at("absolute_magnitude").get_to(absolute_magnitude); } catch (...) { ; }
    try { j.at("BV_color").get_to(BV_color); } catch (...) { ; }
    try { j.at("declination").get_to(declination); declination *= fiftyseventh; } catch (...) { ; }
    try { j.at("distance").get_to(distance); distance *= light_year; } catch (...) { ; }
    try { j.at("distance_known").get_to(distance_known); if (!distance_known && !distance) distance = 1e4+light_year; } catch (...) { ; }
    try { j.at("epoch").get_to(epoch); } catch (...) { ; }
    try { j.at("equinox").get_to(equinox); equinox *= fiftyseventh; } catch (...) { ; }
    try
    {
        j.at("obliquity").get_to(obliquity); obliquity *= fiftyseventh;
        known_poles = true;
    } catch (...) { ; }
    try
    {
        json j1 = j.at("location");
        location.from_json(j1);
    } catch (...) { ; }
    try { j.at("lon_J2000_offset").get_to(lon_J2000_offset); lon_J2000_offset *= fiftyseventh; } catch (...) { ; }
    try { j.at("mass").get_to(mass); } catch (...) { ; }
    try { j.at("oblateness").get_to(oblateness); } catch (...) { ; }
    try
    {
        json j1 = j.at("orbit");
        if (orbit) delete orbit;
        orbit = new Orbit();
        orbit->from_json(j1);
    } catch (...) { ; }
    try { j.at("precession").get_to(precession); } catch (...) { ; }
    try { j.at("RI_color").get_to(RI_color); } catch (...) { ; }
    try { j.at("right_ascension").get_to(right_ascension); right_ascension *= fiftyseventh; } catch (...) { ; }
    try { j.at("sidereal_rotational_period").get_to(sidereal_rotational_period); sidereal_rotational_period *= oneday; } catch (...) { ; }
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

    estimated_poles = true;

    return true;
}

void CelestialObject::update_orbit_location(double tmnow, Rotation* crp)
{
    if (!orbit || !orbit->center || !orbit->period) return;
    location.system_center = orbit->center->location.system_center;
    if (!lock_system_plane) location.local_system_plane = orbit->center->location.local_system_plane;

    double tmnow_as_epoch = (tmnow - J2000_TIME_T)/oneday + J2000;
    double N, I, W, A, e, m, P, PN, PW, EFFE;
    orbit->interpolate_osculating_e(tmnow_as_epoch, N, I, W, A, e, m, P, PN, PW, EFFE);

    // Calculate orbit radians per second and seconds since epoch
    double rads_sec = (_pi * 2) / P;
    double seconds_since_epoch = (tmnow_as_epoch - EFFE)*oneday;

    // Precess the ascending node and process the arg peri
    double node_adjustment = seconds_since_epoch * -PN;
    double peri_adjustment = seconds_since_epoch *  PW;
    double node = N + node_adjustment;
    double argperi = W + peri_adjustment;

    // Calculate current Mean Anomaly
    double M = m + rads_sec * seconds_since_epoch - node_adjustment - peri_adjustment;
    M = std::fmod(M, 2.0 * _pi);

    // Solve for Eccentric Anomaly
    double E = solve_Kepler(M, e);

    // Calculate position in orbital plane (x', y')
    double x_plane = A * (std::cos(E) - e);
    double y_plane = A * std::sqrt(1.0 - e * e) * std::sin(E);

    // Rotate to 3D Heliocentric Coordinates
    double cosO = std::cos(node);
    double sinO = std::sin(node);
    double cosw = std::cos(argperi);
    double sinw = std::sin(argperi);
    double cosi = std::cos(I);
    double sini = std::sin(I);

    double x = (-sinO * cosw -  cosO * sinw * cosi) * x_plane + ( sinO * sinw -  cosO * cosw * cosi) * y_plane;
    double y = (                       sinw * sini) * x_plane + (                       cosw * sini) * y_plane;
    double z = ( cosO * cosw + -sinO * sinw * cosi) * x_plane + (-cosO * sinw + -sinO * cosw * cosi) * y_plane;

    Point orbit_pole = yaxis;
    orbit_pole = rotate3D(orbit_pole, center, Point(sinO, 0, -cosO), I);
    if (crp) orbit_pole = rotate3D(orbit_pole, center, crp->v, -crp->a);
    else orbit_pole = rotate3D(orbit_pole, center, location.local_system_plane.v, -location.local_system_plane.a);
    location.orbital_plane = align_points_3d(orbit_pole, yaxis, center);

    // Precess the equinox
    equinox_eff = equinox - precession * seconds_since_epoch;
    Point pole = yaxis;
    pole = rotate3D(pole, center, location.orbital_plane.v, -location.orbital_plane.a);
    pole = rotate3D(pole, center, Point(sin(equinox_eff), 0, -cos(equinox_eff)), -obliquity);
    if (!lock_equatorial_plane) location.equatorial_plane = align_points_3d(pole, yaxis, center);

    if (_class == class_moon && !crp)
    {
        std::cerr << "CelestialObject::update_orbit_location() called on moon " << name
            << " of planet " << orbit->center->name
            << " of star " << orbit->center->orbit->center->name
            << " without Laplace plane." << std::endl << std::flush;
        throw 0xbadc0de;
    }

    assert(!std::isnan(x) && !std::isnan(y) && !std::isnan(z));

    Point result;
    if (crp) result = rotate3D(Point(x,y,z), center, crp->v, -crp->a);
    else result = rotate3D(Point(x,y,z), center, location.local_system_plane.v, -location.local_system_plane.a);

    location.local_position = result + orbit->center->location.local_position;
}

json Orbit::to_json()
{
    std::string cenname;
    if (center) cenname = center->name;
    return
    {
        {"center_name", cenname},
        {"ascending_node", ascending_node*fiftyseven},
        {"inclination", inclination*fiftyseven},
        {"semimajor_axis", semimajor_axis},
        {"eccentricity", eccentricity},
        {"arg_periapsis", arg_periapsis*fiftyseven},
        {"prec_node", prec_node*fiftyseven*oneyear},
        {"proc_argperi", proc_argperi*fiftyseven*oneyear},
        {"mean_anomaly", mean_anomaly*fiftyseven},
        {"epoch", epoch},
        {"period", period/oneday},
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
    try { j.at("period").get_to(period); period *= oneday; } catch (...) { ; }
    try { j.at("prec_node").get_to(prec_node); prec_node *= fiftyseventh / oneyear; } catch (...) { ; }
    try { j.at("proc_argperi").get_to(proc_argperi); proc_argperi *= fiftyseventh / oneyear; } catch (...) { ; }
    try
    {
        json j1 = j.at("laplace");
        laplace.from_json(j1);
    } catch (...) { ; }

    return true;
}

METHODDEF(void)
alienorum_jpeg_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

bool Map::load_from_jpeg(std::string filename, bool as_bump, double bump_scale)
{
    mtx.lock();
    struct jpeg_decompress_struct cinfo;
    struct my_jpeg_error_mgr jerr;
    FILE * infile;
    unsigned int row_stride;

    if ((infile = fopen(filename.c_str(), "rb")) == NULL)
    {
        fprintf(stderr, "can't open %s\n", filename.c_str());
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = alienorum_jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer))
    {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    (void) jpeg_read_header(&cinfo, TRUE);
    (void) jpeg_start_decompress(&cinfo);

    if (as_bump)
    {
        if (image_height != cinfo.image_height || image_width != cinfo.image_width)
        {
            std::cerr << "Bump map must have same resolution as surface map." << std::endl;
            jpeg_destroy_decompress(&cinfo);
            fclose(infile);
            return false;
        }
        bump_data = new double[allocated];
    }
    else
    {
        image_height = cinfo.image_height;
        image_width = cinfo.image_width;
        lat_scale = lon_scale = image_width / (_pi * 2);
        inv_lat_scale = 1.0 / lat_scale;
        inv_lon_scale = 1.0 / lon_scale;
        long toalloc = image_height * image_width;
        std::cout << "Allocating " << toalloc << " pixels for " << filename << std::endl;
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
    }
    mtx.unlock();

    row_stride = cinfo.output_width * cinfo.output_components;
    jpeg_image_buffer = (*cinfo.mem->alloc_sarray)
            ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    unsigned int i, j;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        j = cinfo.output_scanline * image_width;
        assert(j >= 0);
        (void) jpeg_read_scanlines(&cinfo, jpeg_image_buffer, 1);
        for (i=0; i<row_stride; i+=cinfo.output_components)
        {
            assert(j < allocated);

            if (as_bump)
            {
                // Allow false color bump maps using the visual luminance as the elevation for better granularity
                bump_data[j] = bump_scale *
                            (( 0.001137 * jpeg_image_buffer[0][i]
                             + 0.002196 * jpeg_image_buffer[0][i+1]
                             + 0.000588 * jpeg_image_buffer[0][i+2])
                             - 0.5);
            }
            else
            {
                red_data[j] = jpeg_image_buffer[0][i];
                green_data[j] = jpeg_image_buffer[0][i+1];
                blue_data[j] = jpeg_image_buffer[0][i+2];
            }
            j++;
        }
    }

    (void) jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    return true;
}

bool Map::load_from_png(std::string filename, bool as_bump, double bump_scale)
{
    mtx.lock();
    png_structp png_ptr;
    png_infop info_ptr;
    FILE *fp;

    if ((fp = fopen(filename.c_str(), "rb")) == NULL)
        return false;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
        nullptr, nullptr, nullptr);

    if (png_ptr == NULL)
    {
        fclose(fp);
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
    {
        fclose(fp);
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        /* Free all of the memory associated with the png_ptr and info_ptr */
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        /* If we get here, we had a problem reading the file */
        return false;
    }

    png_init_io(png_ptr, fp);
    png_read_png(png_ptr, info_ptr, 0, NULL);

    auto bytes_per_row = png_get_rowbytes( png_ptr, info_ptr );
    if (as_bump)
    {
        if (image_height != png_get_image_height( png_ptr, info_ptr ) || image_width != png_get_image_width( png_ptr, info_ptr ))
        {
            std::cerr << "Bump map must have same resolution as surface map." << std::endl;
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            fclose(fp);
            return false;
        }
        bump_data = new double[allocated];
    }
    else
    {
        image_height = png_get_image_height( png_ptr, info_ptr );
        image_width = png_get_image_width( png_ptr, info_ptr );
        lat_scale = (double)image_height / _pi;
        lon_scale = (double)image_width / (_pi * 2);
        inv_lat_scale = 1.0 / lat_scale;
        inv_lon_scale = 1.0 / lon_scale;
        int toalloc = image_height*bytes_per_row;
        std::cout << "Allocating " << toalloc << " pixels for " << filename << std::endl;
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
    }
    mtx.unlock();

    int bytes_per_pixel = png_get_channels(png_ptr, info_ptr) * (png_get_bit_depth(png_ptr, info_ptr) / 8);

    png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

    if (bytes_per_pixel == 3)
    {
        // RGB3Byte
        unsigned int x, y, i=0;
        for (y=0; y<image_height; y++)
        {
            for (x=0; x<image_width; x++)
            {
                png_bytep pixel = &(row_pointers[y][x * bytes_per_pixel]);

                if (as_bump)
                {
                    // Allow false color bump maps using the visual luminance as the elevation for better granularity
                    png_bytep pixel = &(row_pointers[y][x * bytes_per_pixel]);
                    bump_data[i] = bump_scale *
                            (( 0.001137 * pixel[0]
                             + 0.002196 * pixel[1]
                             + 0.000588 * pixel[2])
                             - 0.5);
                }
                else
                {
                    red_data[i] = pixel[0];
                    green_data[i] = pixel[1];
                    blue_data[i++] = pixel[2];
                }
            }
        }
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
    }
    else if (bytes_per_pixel == 1)
    {
        // Grayscale.
        unsigned int x, y, i=0;
        for (y=0; y<image_height; y++)
        {
            for (x=0; x<image_width; x++)
            {
                png_bytep pixel = &(row_pointers[y][x * bytes_per_pixel]);

                if (as_bump)
                {
                    png_bytep pixel = &(row_pointers[y][x * bytes_per_pixel]);
                    bump_data[i] = bump_scale * ( 0.00392 * pixel[0]) - 0.5;
                }
                else
                {
                    red_data[i] = green_data[i] = blue_data[i] = pixel[0];
                }
                i++;
            }
        }
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
    }
    else
    {
        std::cerr << "Unsupported byte depth " << bytes_per_pixel << " for " << filename << std::endl;
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return false;
    }

    return true;
}

bool Map::save_to_png(std::string filename)
{
    if (!image_width || !image_height || !green_data)
    {
        std::cerr << "Error: no map data to save." << std::endl;
        return false;
    }

    FILE *fp = fopen(filename.c_str(), "wb");
    if (!fp)
    {
        std::cerr << "Failed to open " << filename << " for writing." << std::endl;
        return false;
    }

    // Initialize the write struct
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        fclose(fp);
        std::cerr << "Failed to initialize PNG write struct." << std::endl;
        return false;
    }

    // Initialize the info struct
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        std::cerr << "Failed to initialize PNG info struct." << std::endl;
        return false;
    }

    // Set up error handling (required by libpng)
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        std::cerr << "Something went wrong." << std::endl;
        return false;
    }

    // Set up the output stream
    png_init_io(png_ptr, fp);

    // Configure image properties
    png_set_IHDR(
        png_ptr,
        info_ptr,
        image_width,
        image_height,
        8,                          // 8 bits per channel
        PNG_COLOR_TYPE_RGB,         // RGB3Byte channels (3 bytes per pixel)
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    // Map the 1D buffer to row pointers
    png_bytepp row_pointers = (png_bytepp)malloc(sizeof(png_bytep) * image_height);
    if (!row_pointers)
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        std::cerr << "Failed to map row pointers." << std::endl;
        return false;
    }

    // Create a buffer to hold the data to be written
    png_bytep buffer = (png_bytep)malloc(image_width * image_height * 3);

    // Populate the buffer with data.
    unsigned int row_stride = image_width * 3;               // 3 bytes per pixel for RGB3Byte
    for (unsigned int y = 0; y < image_height; y++)
    {
        int iy = image_width*y;
        int ry = row_stride*y;
        for (unsigned int x = 0; x < image_width; x++)
        {
            int rx = x*3;
            buffer[ry + rx   ] = red_data[iy + x];
            buffer[ry + rx +1] = green_data[iy + x];
            buffer[ry + rx +2] = blue_data[iy + x];
        }
    }

    // Set up the row pointers to point to the buffer.
    for (unsigned int y = 0; y < image_height; y++)
    {
        row_pointers[y] = &buffer[y * row_stride];
    }

    // Write data to file
    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    // Clean up resources
    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    return true;
}

unsigned int Map::idx_of(double lat, double lon)
{
    while (!lat_scale) std::this_thread::sleep_for(std::chrono::milliseconds(29));

    lon = fmod(lon+_pi, _pi*2);
    if (lon < 0) lon += _pi*2;
    if (lat < -half_pi) lat = -half_pi;
    else if (lat > half_pi) lat = half_pi;

    double xf = lon * lon_scale, yf = (half_pi-lat) * lat_scale;
    unsigned int x0 = floor(xf), y1 = ceil(yf);
    long y0idx = image_width * y1;

    if (y0idx < 0) y0idx = 0;
    if (y0idx > allocated-image_width) y0idx = allocated-image_width;
    if (x0 < 0) x0 = 0;
    if (x0 >= image_width) x0 = image_width-1;

    return y0idx+x0;
}

RGB3Byte Map::color_at(double lat, double lon)
{
    RGB3Byte result;
    if (blue_data)
    {
        unsigned int idx = idx_of(lat, lon);
        result.r = red_data[idx];
        result.g = green_data[idx];
        result.b = blue_data[idx];
    }
    else
    {
        result.r = result.g = result.b = 255;
    }
    return result;
}

double Map::elevation_at(double lat, double lon)
{
    if (bump_data)
    {
        unsigned int idx = idx_of(lat, lon);
        if (idx >= allocated) return 0;
        return (isnan(bump_data[idx]) || isinf(bump_data[idx])) ? 0 : bump_data[idx];
    }
    else return 0.0;
}

// TODO: Make this also work with Moon class width/depth somehow.
double CelestialObject::get_equatorial_radius()
{
    return volumetric_mean_radius * pow(1.0 - oblateness, 0.333);
}

void Map::generate_rocky_map(CelestialObject *cel)
{
    assert(cel->typeclass() == class_planet || cel->typeclass() == class_moon);
    mtx.lock();
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    __uint128_t __ = (__uint128_t)rand() << 64 | (__uint128_t)rand();
    ___ = __;

    Planet *p = (Planet*)cel;
    int lr = cel->fictitious_map_height;
    double BV = cel->BV_color;
    if (randomize_txgen)
    {
        has_water = veg_height = mtn_height = 0;
        ice_amount = 0.03;
    }

    double T_surf = p->estimate_surface_temperature();
    const double Tboil = water_freezing + 100;
    if (p->is_in_con_HZ()
        && cel->mass > 0.02 * earth_mass)                               // Based on Titan's mass.
    {
        double max_atm_pressure = cel->mass / 4.86731e+24 * 9.3e+6;     // Based on Venus.
        if (!p->surface_pressure) p->surface_pressure = max_atm_pressure * pow(10, frand(-7, 0)) * pow(frand(0,1), 4);
        #ifdef DEBUG
            std::cout << "Surface pressure: " << (p->surface_pressure / 101325) << " atm." << std::endl << std::flush;
            std::cout << "Surface temperature: " << T_surf << " K." << std::endl << std::flush;
        #endif

        // Constants for water b.p.
        const double R = 8.314;                                         // J/(mol*K)
        const double DELTA_H_VAP = 40660.0;                             // J/mol
        const double P1 = 1.0e+5;                                       // Reference pressure

        // Clausius-Clapeyron calculation
        double inv_T1 = 1.0 / Tboil;
        double gas_constant_ratio = R / DELTA_H_VAP;
        double pressure_log = std::log(p->surface_pressure / P1);

        double inv_T2 = inv_T1 - (gas_constant_ratio * pressure_log);
        double T_boil = 1.0 / inv_T2;

        if (randomize_txgen)
        {
            if (T_surf < 0.9 * water_freezing)
            {
                has_water = 1;
                ice_amount = 1;
            }
            else if (T_surf < T_boil * 1.1)
            {
                double max_water = pow((T_boil*1.1 - T_surf) / (T_boil*1.1 - 0.9*water_freezing), 0.2);
                has_water = frand(0, max_water);
                ice_amount = fmin(1, fmax(0, pow((T_boil - T_surf) / (T_boil - water_freezing), 10)));
                veg_height = (has_water > 0.1) ? (has_water + 0.03) : 0;
                mtn_height = (veg_height ?: has_water) + has_water*0.29; // 1.0 - ((1.0 - has_water) * 0.8 * ice_amount);
            }
            else
            {
                has_water = 0;
                // TODO: Overcast Venusian-style cloud map.
            }
        }

        std::cout << "Water level: " << has_water << "." << std::endl << std::flush;
        std::cout << "Polar ice: " << ice_amount << "." << std::endl << std::flush;
        std::cout << "Vegetation level: " << veg_height << "." << std::endl << std::flush;
        std::cout << "Mountain level: " << mtn_height << "." << std::endl << std::flush;
    }

    int octaves = 5 + (rand() % 4);
    double lacbase = sqrt(fmax(1, log(cel->volumetric_mean_radius)));
    double lacunarity = frand(0.51*lacbase, 0.53*lacbase);
    double gain = has_water ? 0.5 : 2.5;
    double scale = frand(has_water ? 1.5 : 0.2, has_water ? 2.9 : 0.8);             // Controls feature sizes (smaller scale = larger continents)

    Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
    RGB3Byte rgb = Color::rgb_from_color(col, -1);

    int radd = (int)(0.15*rgb.r), gadd = (int)(0.15*rgb.g), badd = (int)(0.15*rgb.b);

    bool create_bump = (bump_data == nullptr);
    if (create_bump)
    {
        image_height = lr;
        image_width = image_height * 2;
        allocated = image_height * image_width;
        bump_data = new double[allocated];
        memset(bump_data, 0, allocated*sizeof(double));
    }
    else
    {
        lr = cel->fictitious_map_height = image_height;
    }

    allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];

    lat_scale = (double)image_height / _pi;
    lon_scale = (double)image_width / (_pi * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;
    double bump_scale = p->estimate_bump_scale(), inv_bump_scale = 1.0 / bump_scale;
    std::cout << "Allocated " << allocated << " pixels for fictitious rocky map." << std::endl;
    mtx.unlock();

    double inv_h2o_level = 0, phi, psi, theta, u, v, nx, ny, nz, height_value, r_weight, T_base, T_local, sh;
    unsigned int x, y;
    int idx;
    if (has_water && randomize_txgen)
    {
        RGB3Byte veg_color = generate_vegetation_color();
        vegetation_r = veg_color.r;
        vegetation_g = veg_color.g;
        vegetation_b = veg_color.b;
    }
    inv_h2o_level = 1.0 / has_water;

    bool tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star 
        && (fabs((p->sidereal_rotational_period / p->orbit->period) - 1) < 0.01);

    for (y = 0; y < image_height; ++y)
    {
        // Convert screen pixel coordinates to spherical angles
        v = (double)y / image_height;
        theta = v * _pi; // Latitude angle from 0 to PI

        for (x = 0; x < image_width; ++x)
        {
            u = (double)x / image_width;
            phi = u * 2.0 * _pi; // Longitude angle from 0 to 2PI

            // Map 2D texture coordinates to a 3D Sphere surface to avoid seam/polar stretching
            nx = sin(theta) * cos(phi);
            ny = sin(theta) * sin(phi);
            nz = cos(theta);

            if (tidal_locked_to_star) psi = find_3D_angle(Point(nx,ny,nz), xaxis, center);

            idx = y * image_width + x;

            // Get noise value for this point on the sphere
            height_value = create_bump ? fBm(nx * scale, ny * scale, nz * scale, octaves, lacunarity, gain)
                : fmin(1, fmax(0, (inv_bump_scale * bump_data[idx] + 0.5)));

            if (create_bump) bump_data[idx] = bump_scale * (height_value - 0.5);

            if (has_water)
            {
                r_weight = height_value;

                T_base = tidal_locked_to_star
                    ? (T_surf + 128.0 - 256.0 * cos(psi*0.5))
                    : (T_surf - 40.0 + 70.0 * sin(theta));
                T_local = T_base - 62.5 * fmax(0, height_value - has_water);
                if (T_local < water_freezing)
                {
                    // Polar and elevation ice
                    red_data[idx] = 167 + 67 * r_weight;
                    green_data[idx] = 181 + 57 * r_weight;
                    blue_data[idx] = 190 + 63 * r_weight;
                    // if (create_bump) bump_data[idx] = fmax(0, bump_data[idx]);
                }
                // Biome allocation based on height thresholds
                else if (height_value < has_water && (T_local < Tboil))
                {   // Ocean
                    sh = height_value*inv_h2o_level;
                    sh *= (Tboil - T_base) / (Tboil - water_freezing);
                    sh = pow(sh, 20);                                                           // shallowness multiplied to show water optical density
                    red_data[idx] = (12+16*sh);
                    green_data[idx] = (24+168*sh);
                    blue_data[idx] = (192+32*sh);
                    // if (create_bump) bump_data[idx] = 0;
                }
                else if (T_local > veg_max_temp)
                {   // Beach or desert sand
                    red_data[idx] = 220 * r_weight;
                    green_data[idx] = 200 * r_weight;
                    blue_data[idx] = 150 * r_weight;
                }
                else if (T_local >= veg_min_temp && (!tidal_locked_to_star || psi >= half_pi))          // vegetation only on the day side
                {   // Forests
                    red_data[idx] = vegetation_r * r_weight;
                    green_data[idx] = vegetation_g * r_weight;
                    blue_data[idx] = vegetation_b * r_weight;
                }
                else
                {   // Mountains
                    red_data[idx] = 110 * r_weight;
                    green_data[idx] = 90 * r_weight;
                    blue_data[idx] = 75 * r_weight;
                }
            }
            else
            {
                // Lifeless planet or moon
                r_weight = height_value;
                red_data[idx] = (unsigned char)(fmin(255, rgb.r * r_weight + radd));
                green_data[idx] = (unsigned char)(fmin(255, rgb.g * r_weight + gadd));
                blue_data[idx] = (unsigned char)(fmin(255, rgb.b * r_weight + badd));
            }

            // TODO: This does not work.
            if (__ != ___)
            {
                std::cout << "Abort previous rocky map." << std::endl << std::flush;
                return;
            }
        }
    }
}

void Map::generate_gas_giant_map(int lr, double BV)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    mtx.lock();
    __uint128_t __ = (__uint128_t)rand() << 64 | (__uint128_t)rand();
    ___ = __;
    image_height = lr;
    image_width = image_height * 2;

    allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];
    lat_scale = image_height / _pi;
    lon_scale = image_width / (_pi * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;
    std::cout << "Allocated " << allocated << " pixels for fictitious gas giant map." << std::endl;

    Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
    RGB3Byte rgb = Color::rgb_from_color(col, -1);

    double variability = frand(0, 0.666);
    int num_bands = rand() % 9 + 7, i;
    auto bands = std::make_unique<RGB3Byte[]>(num_bands);

    bool add_storm = frand(0, 1) < 0.2;
    double stormlat, stormlon, distToStormX, distToStormY, stormDist = 1e29;

    stormlat = frand(0.3, 0.7);
    stormlon = frand(0, 1);
    mtx.unlock();

    for (i=0; i<num_bands; i++)
    {
        double rmult, gmult, bmult;

        rmult = 1.0 - frand(0, variability);
        bmult = 1.0 - frand(0, variability);
        gmult = frand(fmin(rmult, bmult), fmax(rmult, bmult));

        bands[i].r = rgb.r * rmult;
        bands[i].g = rgb.g * gmult;
        bands[i].b = rgb.b * bmult;
    }

    double scale = 2.5;

    for (unsigned int y = 0; y < image_height; ++y)
    {
        double v = (double)y / image_height;
        double theta = v * _pi;

        for (unsigned int x = 0; x < image_width; ++x)
        {
            double u = (double)x / image_width;
            double phi = u * 2.0 * _pi;

            // 3D Sphere projection
            double nx = sin(theta) * cos(phi) * scale;
            double ny = sin(theta) * sin(phi) * scale;
            double nz = cos(theta) * scale * 8;

            // Domain Warping: Use noise to distort the coordinates horizontally
            // This creates the swirling, fluid look of gas clouds
            double distortX = fBm(nx, ny, nz, 4, 2.0, 0.5) * 1.5;
            double distortY = fBm(nx + 5.2, ny + 1.3, nz + 2.7, 4, 2.0, 0.5) * 0.1;

            // Apply distortion primarily along the X/longitude axis to emulate wind bands
            double finalNoise = fBm(nx + distortX * 4.0, ny + distortY, nz, 6, 2.0, 0.55);

            if (add_storm)
            {
                distToStormX = (u - stormlon) * 2.0 * _pi;
                distToStormY = (v - stormlat) * _pi;
                // Elliptical distance formula
                stormDist = sqrt((distToStormX * distToStormX) * 2.5 + (distToStormY * distToStormY) * 10.0);
            }

            int idx = y * image_width + x;

            if (stormDist < 0.3)
            {
                // We are inside the storm; blend into dark colors
                double stormBlend = (0.3 - stormDist) / 0.3; // 1 at center, 0 at edge
                // Swirl the storm inside
                double stormNoise = fBm(nx * 3.0, ny * 3.0, nz * 3.0, 3, 2.0, 0.5);

                red_data[idx] = (unsigned char)(180 * stormNoise + 60);
                green_data[idx] = (unsigned char)(40 * stormNoise + 10);
                blue_data[idx] = (unsigned char)(30 * stormNoise + 10);

                // Linear interpolation blending storm with background bands
                red_data[idx] = (unsigned char)(red_data[idx] * stormBlend + bands[0].r * (1.0 - stormBlend));
                green_data[idx] = (unsigned char)(green_data[idx] * stormBlend + bands[0].g * (1.0 - stormBlend));
                blue_data[idx] = (unsigned char)(blue_data[idx] * stormBlend + bands[0].b * (1.0 - stormBlend));
            }
            else
            {
                // Regular band calculation based on the warped noise
                // Map finalNoise [0, 1] to the band array
                double bandVal = fmod(fabs(v - 0.5) * 2 * num_bands + finalNoise * 1.3, num_bands);
                if (bandVal < 0) bandVal += num_bands;
                int bandIdx = (int)floor(bandVal);
                double t = bandVal - bandIdx; // fractional part for linear interpolation

                int nextBandIdx = (bandIdx + 1) % num_bands;

                // Interpolate colors between bands for smooth transitions
                red_data[idx] = (unsigned char)((1.0 - t) * bands[bandIdx].r + t * bands[nextBandIdx].r);
                green_data[idx] = (unsigned char)((1.0 - t) * bands[bandIdx].g + t * bands[nextBandIdx].g);
                blue_data[idx] = (unsigned char)((1.0 - t) * bands[bandIdx].b + t * bands[nextBandIdx].b);
            }

            if (___ != __) return;
        }
    }
}

void alienorum::Map::mark_for_map_regen(CelestialObject *cel)
{
    if (bump_data && cel->type == rocky)
    {
        if (red_data) delete[] red_data;
        if (green_data) delete[] green_data;
        if (blue_data) delete[] blue_data;
        red_data = green_data = blue_data = nullptr;
        generate_rocky_map(cel);
    }
    else
    {
        bool go_ahead = false;
        if (cel->surf_map == this)
        {
            cel->surf_map = nullptr;
            go_ahead = true;
        }
        else if (cel->cloud_map == this)
        {
            cel->cloud_map = nullptr;
            go_ahead = true;
        }
        else if (cel->night_map == this)
        {
            cel->night_map = nullptr;
            go_ahead = true;
        }

        if (go_ahead)
        {
            delete this; return;            // CAREFUL!!!!! See: https://isocpp.org/wiki/faq/freestore-mgmt#delete-this
        }
    }
}

void append_cel(CelestialObject *cel)
{
    if (ncelobjs >= MAX_CELOBJS-1) return;

    cel->origname = cel->name;
    if (cel->orbit && cel->orbit->center) cel->origcenname = cel->orbit->center->name;

    cels[ncelobjs] = cel;
    cel->seqno = ncelobjs;
    ncelobjs++;
    cels[ncelobjs] = 0;
}

Point to_viewer_plane(Point pt, int sign)
{
    pt = rotate3D(pt, center, here.equatorial_plane.v, here.equatorial_plane.a*sign);
    return pt;
}

OsculatingElement *alienorum::OsculatingElement::read_from_file(std::string filename, uint64_t *elements_read)
{
    if (elements_read) *elements_read = 0;
    std::filesystem::path path = filename.c_str();
    unsigned long int fs = std::filesystem::file_size(path);
    if (!fs) return nullptr;

    unsigned long int num_elements = fs/372;                    // should be plenty.
    OsculatingElement *result = new OsculatingElement[num_elements];

    FILE *fp = fopen(filename.c_str(), "r");
    if (!fp) return nullptr;

    long int i = -1;
    char buffer[1024];
    bool reading = false;
    char *variable;
    while (fgets(buffer, 1022, fp))
    {
        if (strstr(buffer, "$$SOE")) reading = true;
        else if (strstr(buffer, "$$EOE")) break;
        else if (reading)
        {
            if (buffer[0] >= '0' && buffer[0] <= '9')
            {
                i++;
                result[i].epoch = atof(buffer);
            }
            else
            {
                if ((variable = strstr(buffer, "EC="))) result[i].eccentricity = atof(&variable[3]);
                if ((variable = strstr(buffer, "IN="))) result[i].inclination = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "OM="))) result[i].ascending_node = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "W ="))) result[i].arg_perifocus = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "Tp="))) result[i].T_periapsis = atof(&variable[3]);
                if ((variable = strstr(buffer, "MA="))) result[i].mean_anomaly = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "A ="))) result[i].semimajor_axis = atof(&variable[3]) * 1000;
                if ((variable = strstr(buffer, "PR="))) result[i].period = atof(&variable[3]);
            }
        }
    }

    result[i].period = 0;               // make sure; we'll depend on this later.

    if (elements_read) *elements_read = i;
    return result;
}
