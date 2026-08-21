
#include <math.h>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include "celestial.h"
#include "shore.h"

using namespace alienorum;

CelestialObject **cels, *mycenobj = nullptr;
std::vector<std::vector<CelestialObject*>> first_letter_index;
std::map<std::string,std::vector<CelestialObject*>> constellation_index;

bool *celskip, *discinstead;
double *vmag_cache, *bloomrad_cache, *angular_radius;
CelestialLocation here;
double azimuth_correction = 0;
typedef struct my_jpeg_error_mgr * my_error_ptr;
Locale *is_a_locale_under_cursor = nullptr, *selected_locale = nullptr;

int alienorum::CelestialObject::cel_rand()
{
    std::uniform_int_distribution<int> dist;
    return dist(rng);
}

double alienorum::CelestialObject::cel_frand(double min, double max)
{
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng);
}

CelestialObject::CelestialObject()
{
    memset(name, 0, 32*sizeof(char));
}

double alienorum::CelestialObject::get_horizon_angle()
{
    double d = tmprel.magnitude();
    return acos(get_equatorial_radius() / fmax(d, 1e-29));
}

double alienorum::CelestialObject::get_horizon_distance()
{
    return tmprel.magnitude() - volumetric_mean_radius * cos(get_horizon_angle());
}

double alienorum::CelestialObject::timeofday()
{
    if (!orbit || !is_tidal_locked())
    {
        double rads_sec = sidereal_rotational_period ? ((_pi * 2) / sidereal_rotational_period) : 0;
        double seconds_since_epoch = (simnow - J2000_TIME_T) + (((double)J2000 - epoch)*oneday);
        double result = rads_sec * seconds_since_epoch - lon_J2000_offset;
        _currTOD = fmod(result, _pi*2);

        if (orbit)
        {
            _currTOD += orbit->ascending_node;
            _currTOD += orbit->arg_periapsis;
            _currTOD += orbit->mean_anomaly;
        }
        _currTOD = fmod(_currTOD, _pi*2);
        if (_currTOD < 0) _currTOD += _pi*2;
    }

    return _currTOD;
}

int alienorum::CelestialObject::read_locales(std::string fn)
{
    bool upside_down = !strcmp(name, "Venus");          // kludge
    std::fstream fs(fn, std::ios::in);
    if (fs)
    {
        json llocales;
        fs >> llocales;
        fs.close();

        for (auto& [planet, plocs] : llocales.items())
        {
            if (!strcmp(planet.c_str(), name))
            {
                int result = read_locales_json(plocs);

                if (upside_down)
                {
                    for (int i=0; i<result; i++)
                    {
                        locales[i].lat = -locales[i].lat;
                        if (locales[i].lon < 0) locales[i].lon += _pi*2;
                        locales[i].lon = _pi*2 - locales[i].lon;
                    }
                }

                return result;
            }
        }
    }
    return 0;
}

int alienorum::CelestialObject::read_locales_json(json fj)
{
    if (locales) delete[] locales;
    int i, j=0, n = fj.size();
    locales = new Locale[n];
    nlocales = n;
    for (i=0; i<n; i++)
    {
        try
        {
            Locale l(fj[i]);
            locales[i] = l;
            j++;
        }
        catch (...)
        {
            std::cerr << "ERROR loading locale: " << fj.dump(4) << std::endl;
            locales[i].name = "";
            locales[i].lat = locales[i].lon = 0;
        }
    }
    return j;
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
        if (cls == class_planet || cls == class_moon || cls == class_satellite || cls == class_comet)
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
    std::string filename = std::string("ephemerides") + _FSSTR + cel_name + std::string(".txt");
    if (!file_exists(filename.c_str()))
    {
        std::string gzfilename = filename + std::string(".gz");
        if (file_exists(gzfilename.c_str()))
        {
            extract_archive(gzfilename.c_str());
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
            double pdays = p / oneday;
            double T_peri_j1 = osculating[j+1].T_periapsis, T_peri_j1mp = T_peri_j1 - pdays, T_peri_j1pp = T_peri_j1 + pdays;
            if (     fabs(osculating[j].T_periapsis - T_peri_j1mp) < fabs(osculating[j].T_periapsis - T_peri_j1)) T_peri_j1 = T_peri_j1mp;
            else if (fabs(osculating[j].T_periapsis - T_peri_j1pp) < fabs(osculating[j].T_periapsis - T_peri_j1)) T_peri_j1 = T_peri_j1pp;
            effe = coeff0 * osculating[j].T_periapsis + coeff1 * T_peri_j1;
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
    if (is_open()) { period = 0; return; }               // Nothing to compute: it never comes back.
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

            case waterworld:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / earth_radius / 1.7) * earth_mass * 6;             // LHS 1140 b
            break;

            case icy:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / 2631200.0644) * 1.4819e+23;                       // Ganymede
            break;

            case rocky:
            case lavaworld:
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
    if (is_open()) return;                               // An open orbit's scale is its perihelion, set where it is read.
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

            case waterworld:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / earth_radius / 1.7) * earth_mass * 6;             // LHS 1140 b
            break;

            case icy:
            if (!center->volumetric_mean_radius) return;
            center->mass = sphere_volume(center->volumetric_mean_radius / 2631200.0644) * 1.4819e+23;                       // Ganymede
            break;

            case rocky:
            case lavaworld:
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
        + std::string(seconds<9.95 ? "0" : "")
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
        + std::string(seconds<9.5 ? "0" : "")
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
    Point relloc = (location.galactic_center - seen_from.galactic_center) * light_year * 1e+6
        + (location.system_center - seen_from.system_center)
        + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    double result = std::fmod(find_angle(relloc.z, -relloc.x) - seen_equinox + azimuth_correction, _pi*2);
    if (result < 0) result += _pi*2;
    return result;
}

double CelestialObject::Decl_as_radians(CelestialLocation seen_from)
{
    Point relloc = (location.galactic_center - seen_from.galactic_center) * light_year * 1e+6
        + (location.system_center - seen_from.system_center)
        + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    double result = find_angle(sqrt(relloc.x*relloc.x+relloc.z*relloc.z), relloc.y);
    if (result > _pi/2) result -= _pi*2;
    return result;
}

double alienorum::CelestialObject::Decl_as_radians_refracted(CelestialLocation seen_from)
{
    Point relloc = (location.galactic_center - seen_from.galactic_center) * light_year * 1e+6
        + (location.system_center - seen_from.system_center)
        + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
    relloc = refract_true_point(relloc);
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

void alienorum::CelestialObject::randomize()
{
    union
    {
        unsigned long long hash;
        unsigned int seed;
    };

    if (!rnd_seed)
    {
        hash = 0xb0ad * log(mass) + 0x1cea * log(volumetric_mean_radius);
        if (orbit) hash += 0xeb00dae * log(orbit->semimajor_axis) + 0xefac00ee * orbit->arg_periapsis;
        rnd_seed = seed;
    }
    rng.seed(rnd_seed);
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
    towrite["precession"] = _pi * 2 / precession / oneyear;
    towrite["RI_color"] = RI_color;
    towrite["right_ascension"] = right_ascension * fiftyseven;
    towrite["sidereal_rotational_period"] = sidereal_rotational_period / oneday;
    towrite["type"] = type;
    towrite["typeclass"] = typeclass();
    towrite["UB_color"] = UB_color;
    towrite["user_added"] = user_added;
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
    try { j.at("precession").get_to(precession); precession = _pi * 2 / (precession * oneyear); } catch (...) { ; }
    try { j.at("RI_color").get_to(RI_color); } catch (...) { ; }
    try { j.at("right_ascension").get_to(right_ascension); right_ascension *= fiftyseventh; } catch (...) { ; }
    try { j.at("sidereal_rotational_period").get_to(sidereal_rotational_period); sidereal_rotational_period *= oneday; } catch (...) { ; }
    try { j.at("type").get_to(type); } catch (...) { ; }
    try { j.at("UB_color").get_to(UB_color); } catch (...) { ; }
    try { j.at("user_added").get_to(user_added); } catch (...) { ; }
    try { j.at("volumetric_mean_radius").get_to(volumetric_mean_radius); } catch (...) { ; }
    try { j.at("VR_color").get_to(VR_color); } catch (...) { ; }

    // Blanks to copy-paste when adding a field here or to another class's from_json():
    //   plain:      try { j.at("").get_to(); } catch (...) { ; }
    //   object:     try { json j1 = j.at(""); .from_json(j1); } catch (...) { ; }
    //   object ptr: try { json j1 = j.at(""); = new (); ->from_json(j1); } catch (...) { ; }
    //   char[]:     try { std::string s; j.at("").get_to(s); strcpy(, s.c_str()); } catch (...) { ; }
    // TODO: Convert the char[] fields back to std::strings.

    estimated_poles = true;

    return true;
}

void CelestialObject::update_orbit_location(double tmnow, Rotation* crp)
{
    if (!orbit || !orbit->center) return;
    if (deleted = orbit->center->deleted) return;           // assignment not comparison.
    location.galactic_center = orbit->center->location.galactic_center;
    location.system_center = orbit->center->location.system_center;
    if (!lock_system_plane) location.local_system_plane = orbit->center->location.local_system_plane;

    if (!orbit->period && !orbit->num_osc && !orbit->is_open())
    {
        if (!orbit->center->mass)
        {
            cel_obj_class cls = orbit->center->typeclass();
            if (cls == class_star) orbit->center->mass = ((Star*)orbit->center)->estimate_mass();
            if (cls == class_planet || cls == class_moon)
            {
                ((Planet*)orbit->center)->estimate_radius();
                orbit->center->mass = pow(volumetric_mean_radius / earth_radius, 3) * earth_mass;
            }
        }

        if (!orbit->semimajor_axis) orbit->semimajor_axis = location.distance_to(orbit->center->location);
        if (!orbit->semimajor_axis) orbit->semimajor_axis = AU * 10;

        orbit->period = oneyear * sqrt(pow(orbit->semimajor_axis/AU, 3)) / (orbit->center->mass / solar_mass);
        // std::cout << name << " estimated orbit period=" << (orbit->period / oneday) << " days." << std::endl;
    }

    double tmnow_as_epoch = (tmnow - J2000_TIME_T)/oneday + J2000;
    double N, I, W, A, e, m, P, PN, PW, EFFE;
    orbit->interpolate_osculating_e(tmnow_as_epoch, N, I, W, A, e, m, P, PN, PW, EFFE);

    double rads_sec = sidereal_rotational_period ? ((_pi * 2) / sidereal_rotational_period) : 0;
    double seconds_since_epoch = (tmnow_as_epoch - EFFE)*oneday;

    _currTOD = rads_sec * seconds_since_epoch - lon_J2000_offset;

    if (orbit && is_tidal_locked())
    {
        _currTOD += N;
        _currTOD += W;
        _currTOD += m;
        _currTOD += _pi;
    }
    _currTOD = fmod(_currTOD, _pi*2);
    if (_currTOD < 0) _currTOD += _pi*2;

    rads_sec = P ? ((_pi * 2) / P) : 0;

    // Precess the ascending node and process the arg peri
    double node_adjustment = seconds_since_epoch * -PN;
    double peri_adjustment = seconds_since_epoch *  PW;
    double node = N + node_adjustment;
    double argperi = W + peri_adjustment;
    double x_plane, y_plane;

    if (e < 1)
    {
        // Calculate current Mean Anomaly
        _currM = m + rads_sec * seconds_since_epoch - node_adjustment - peri_adjustment;
        _currM = std::fmod(_currM, 2.0 * _pi);

        // Solve for Eccentric Anomaly
        double E = solve_Kepler(_currM, e);

        x_plane = A * (std::cos(E) - e);
        y_plane = A * std::sqrt(1.0 - e * e) * std::sin(E);
    }
    else
    {
        double mu = G * (orbit->center->mass ? orbit->center->mass : solar_mass);
        double q = orbit->periapsis_distance;
        if (q <= 0) q = A * std::abs(1.0 - e);
        if (q <= 0) q = AU;

        double T0 = orbit->T_periapsis ? orbit->T_periapsis : EFFE;
        double dt = (tmnow_as_epoch - T0) * oneday;

        if (e > 1)
        {
            double a = q / (e - 1.0);
            double n = std::sqrt(mu / (a*a*a));
            double H = solve_Kepler_hyperbolic(n * dt, e);
            _currM = n * dt;

            x_plane = a * (e - std::cosh(H));
            y_plane = a * std::sqrt(e*e - 1.0) * std::sinh(H);
        }
        else
        {
            double n = std::sqrt(mu / (2.0*q*q*q));
            double D = solve_Barker(n * dt);
            _currM = n * dt;

            x_plane = q * (1.0 - D*D);
            y_plane = 2.0 * q * D;
        }
    }

    // Rotate to 3D Heliocentric Coordinates
    double cosO = std::cos(node);
    double sinO = std::sin(node);
    double cosw = std::cos(argperi);
    double sinw = std::sin(argperi);
    double cosi = std::cos(I);
    double sini = std::sin(I);

    double x = (-sinO * cosw - cosO * sinw * cosi) * x_plane + ( sinO * sinw - cosO * cosw * cosi) * y_plane;
    double y = (                      sinw * sini) * x_plane + (                      cosw * sini) * y_plane;
    double z = ( cosO * cosw - sinO * sinw * cosi) * x_plane + (-cosO * sinw - sinO * cosw * cosi) * y_plane;

    if (!orbit->heliocentric_inclination && !orbit->heliocentric_node)
    {
        Point orbit_pole = yaxis;
        orbit_pole = rotate3D(orbit_pole, center, Point(sinO, 0, -cosO), I);
        if (crp) orbit_pole = rotate3D(orbit_pole, center, crp->v, -crp->a);
        else orbit_pole = rotate3D(orbit_pole, center, location.local_system_plane.v, -location.local_system_plane.a);
        location.orbital_plane = align_points_3d(orbit_pole, yaxis, center);
    }

    // Precess the equinox
    equinox_eff = equinox - precession * seconds_since_epoch;
    Point pole = yaxis;
    pole = rotate3D(pole, center, Point(sin(equinox_eff), 0, -cos(equinox_eff)), -obliquity);
    pole = rotate3D(pole, center, location.orbital_plane.v, -location.orbital_plane.a);
    if (!lock_equatorial_plane) location.equatorial_plane = align_points_3d(pole, yaxis, center);

    Point orb_pole = rotate3D(yaxis, center, location.orbital_plane.v, -location.orbital_plane.a);
    Point eq_pole  = rotate3D(yaxis, center, location.equatorial_plane.v, -location.equatorial_plane.a);
    orb_pole.scale(1);
    eq_pole.scale(1);
    Point ascending(orb_pole.y*eq_pole.z - orb_pole.z*eq_pole.y,
                    orb_pole.z*eq_pole.x - orb_pole.x*eq_pole.z,
                    orb_pole.x*eq_pole.y - orb_pole.y*eq_pole.x);

    double l_equinox_RA;
    if (ascending.magnitude() > 1e-9)
    {
        ascending = rotate3D(ascending, center, location.equatorial_plane.v, location.equatorial_plane.a);
        l_equinox_RA = find_angle(ascending.z, -ascending.x);
    }
    else l_equinox_RA = equinox_eff;
    if (fabs(equinox_RA - l_equinox_RA) > 0.01 * fiftyseventh) equinox_RA = l_equinox_RA;           // cache it so ra/dec lines don't jiggle in sky map mode.

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
        {"periapsis_distance", periapsis_distance},
        {"T_periapsis", T_periapsis},
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
    try { j.at("periapsis_distance").get_to(periapsis_distance); } catch (...) { ; }
    try { j.at("T_periapsis").get_to(T_periapsis); } catch (...) { ; }
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
  my_error_ptr myerr = (my_error_ptr) cinfo->err;
  (*cinfo->err->output_message) (cinfo);
  longjmp(myerr->setjmp_buffer, 1);
}

bool Map::load_from_jpeg(std::string filename, bool as_bump, double bump_scale)
{
    std::cout << "Loading " << filename << " as " << (as_bump ? "texture" : "bump") << "..." << std::endl;
    mtx.lock();
    struct jpeg_decompress_struct cinfo;
    struct my_jpeg_error_mgr jerr;
    FILE * infile;
    unsigned int row_stride;

    // Venus rotates retrograde, which means actually its north pole points roughly in the direction of its orbit's south pole and vice versa.
    // But the texture maps put retrograde north at the top. We use prograde north internally, so the maps have to be inverted at load.
    bool upside_down = filename.find("Venus") != std::string::npos;                 // shame on me for hard coding this - TODO: create a field in planets.json

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

    unsigned int i, j, i0, j0;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        j0 = cinfo.output_scanline * image_width;
        (void) jpeg_read_scanlines(&cinfo, jpeg_image_buffer, 1);
        i0 = 0;
        for (i=0; i<row_stride; i+=cinfo.output_components)
        {
            j = j0 + i0;
            if (j >= allocated) break;
            if (upside_down) j = allocated-1-j;

            if (as_bump)
            {
                // Allow false color bump maps using the visual luminance as the elevation for better granularity
                double lum = (cinfo.output_components >= 3)
                    ? (   0.001137 * jpeg_image_buffer[0][i]
                        + 0.002196 * jpeg_image_buffer[0][i+1]
                        + 0.000588 * jpeg_image_buffer[0][i+2])
                    : (   0.003921 * jpeg_image_buffer[0][i]);   // 1/255, matching the RGB weights' sum
                bump_data[j] = bump_scale * (lum - 0.5);
            }
            else if (cinfo.output_components >= 3)
            {
                red_data[j]   = jpeg_image_buffer[0][i];
                green_data[j] = jpeg_image_buffer[0][i+1];
                blue_data[j]  = jpeg_image_buffer[0][i+2];
            }
            else
            {
                red_data[j] = green_data[j] = blue_data[j] = jpeg_image_buffer[0][i];
            }
            i0++;
        }
    }

    (void) jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    if (!as_bump) touch_gen();
    return true;
}

bool Map::load_from_png(std::string filename, bool as_bump, double bump_scale)
{
    std::cout << "Loading " << filename << " as " << (as_bump ? "texture" : "bump") << "..." << std::endl;
    mtx.lock();
    png_structp png_ptr;
    png_infop info_ptr;
    FILE *fp;

    bool upside_down = filename.find("Venus") != std::string::npos;                 // shame on me for hard coding this - TODO: create a field in planets.json

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
        // One byte per pixel per channel array: red_data/green_data/blue_data are indexed as a
        // plain width*height grid by idx_of()/export_rgba(), never by row byte-stride.
        long toalloc = image_height*image_width;
        std::cout << "Allocating " << toalloc << " pixels for " << filename << std::endl;
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
    }
    mtx.unlock();

    int bytes_per_pixel = png_get_channels(png_ptr, info_ptr) * (png_get_bit_depth(png_ptr, info_ptr) / 8);

    png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

    if (bytes_per_pixel == 3 || bytes_per_pixel == 4)
    {
        // RGB or RGBA
        unsigned int x, y, i=0;
        for (y=0; y<image_height; y++)
        {
            for (x=0; x<image_width; x++)
            {
                png_bytep pixel;
                if (upside_down) pixel = &(row_pointers[image_height-1-y][(image_width-1-x) * bytes_per_pixel]);
                else pixel = &(row_pointers[y][x * bytes_per_pixel]);

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
                    blue_data[i] = pixel[2];
                    i++;
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

    if (!as_bump) touch_gen();
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

unsigned int Map::next_gen()
{
    static std::atomic<unsigned int> counter{0};
    return ++counter;
}

unsigned int Map::idx_of(double lat, double lon)
{
    if (!lat_scale) return idx_not_ready;

    lon = fmod(lon+_pi, _pi*2);
    if (lon < 0) lon += _pi*2;
    if (lat < -half_pi) lat = -half_pi;
    else if (lat > half_pi) lat = half_pi;

    if (!image_width || allocated < image_width) return idx_not_ready;

    double xf = lon * lon_scale, yf = (half_pi-lat) * lat_scale;

    if (!(xf >= 0)) xf = 0;                 // also catches NaN, which every comparison would fail
    if (!(yf >= 0)) yf = 0;

    unsigned long x0 = (unsigned long)floor(xf);
    unsigned long y1 = (unsigned long)ceil(yf);
    if (x0 >= image_width) x0 = image_width - 1;

    unsigned long last_row = (allocated - image_width) / image_width;
    if (y1 > last_row) y1 = last_row;

    return (unsigned int)(y1 * image_width + x0);
}

void alienorum::Map::resample_bump_data(unsigned int new_resolution)
{
    if (!has_bump_data()) return;
    if (image_height == new_resolution) return;
    generating_fic_texture = true;
    double scale = (double)image_height / new_resolution;
    double *bump_data_old = bump_data;
    long old_height = image_height, old_width = image_width;
    image_height = new_resolution;
    image_width = image_height * 2;
    long toalloc = image_height * image_width;
    bump_data = new double[toalloc];
    // std::cout << "Resample bump data from " << allocated << " to " << toalloc << std::endl;

    int octaves = 5;
    double lacunarity = 2.9, gain = 0.9, noise_scale = 2.9;


    long x, y, iy, iy1, iynew, xo, yo, xo1, yo1;
    double oldx, oldy, iox, iox1, ioy, ioy1, b;
    double bump_scale = mcel
        ? ( (mcel->typeclass() == class_planet || mcel->typeclass() == class_moon)
            ? ((Planet*)mcel)->estimate_bump_scale()
            : 1)
        : 1;

    double u, v, theta, phi, nx, ny, nz;
    for (y = 0; y < image_height; y++)
    {
        oldy = scale * y;
        yo = floor(oldy);
        yo1 = ceil(oldy);
        ioy1 = oldy - floor(oldy);
        ioy = 1.0 - ioy1;

        if (yo1 >= old_height) yo1 = old_height - 1;            // clamp at pole

        iy = yo * old_width;
        iy1 = yo1 * old_width;
        iynew = y * image_width;

        // Convert screen pixel coordinates to spherical angles
        v = (double)y / image_height;
        theta = v * _pi; // Latitude angle from 0 to PI

        for (x = 0; x < image_width; x++)
        {
            oldx = scale * x;
            xo = floor(oldx);
            xo1 = ceil(oldx);
            iox1 = oldx - floor(oldx);
            iox = 1.0 - iox1;

            if (xo1 >= old_width) xo1 -= old_width;             // wraparound

            u = (double)x / image_width;
            phi = u * 2.0 * _pi; // Longitude angle from 0 to 2PI

            // Map 2D texture coordinates to a 3D Sphere surface to avoid seam/polar stretching
            nx = sin(theta) * cos(phi);
            ny = sin(theta) * sin(phi);
            nz = cos(theta);

            b   = iox * ioy * bump_data_old[iy + xo]
                + iox1 * ioy * bump_data_old[iy + xo1]
                + iox * ioy1 * bump_data_old[iy1 + xo]
                + iox1 * ioy1 * bump_data_old[iy1 + xo1]
                + bump_scale * 0.1 * fBm(nx * noise_scale, ny * noise_scale, nz * noise_scale, octaves, lacunarity, gain);
            bump_data[iynew + x] = b;
        }
    }

    #if 0
    // Smooth out the noise
    long xm1, xp1, ym1, yp1, iym1, iyp1, i;
    for (y = 0; y < image_height; y++)
    {
        iy = y * image_width;
        ym1 = y-1; if (ym1 < 0) ym1 = 0;
        yp1 = y+1; if (yp1 >= image_height) yp1 = image_height - 1;
        iym1 = ym1 * image_width;
        iyp1 = yp1 * image_width;
        for (x = 0; x < image_width; x++)
        {
            xm1 = x - 1; if (xm1 < 0) xm1 += image_width;
            xp1 = x + 1; if (xp1 >= image_width) xp1 -= image_width;

            b = ( bump_data[iy + x]
                + 0.5  * bump_data[iym1 + x]
                + 0.5  * bump_data[iyp1 + x]
                + 0.5  * bump_data[iy + xm1]
                + 0.5  * bump_data[iy + xp1]
                + 0.25 * bump_data[iym1 + xm1]
                + 0.25 * bump_data[iyp1 + xm1]
                + 0.25 * bump_data[iym1 + xp1]
                + 0.25 * bump_data[iyp1 + xp1]
                ) * 0.25;
            bump_data[iy + x] = b;
        }
    }
    #endif

    allocated = toalloc;
    delete[] bump_data_old;
    generating_fic_texture = false;
}

void Map::export_rgba(unsigned char *out) const
{
    unsigned long n = image_width * image_height;
    for (unsigned long i = 0; i < n; i++)
    {
        out[i*4+0] = red_data   ? red_data[i]   : 255;
        out[i*4+1] = green_data ? green_data[i] : 255;
        out[i*4+2] = blue_data  ? blue_data[i]  : 255;
        out[i*4+3] = 255;
    }
}

void Map::export_bump(float *out) const
{
    unsigned long n = image_width * image_height;
    for (unsigned long i = 0; i < n; i++)
        out[i] = bump_data ? (float)bump_data[i] : 0.0f;
}

RGB3Byte Map::color_at(double lat, double lon)
{
    RGB3Byte result;
    if (generating_fic_texture)
    {
        result.r = result.g = result.b = 255;
    }
    else
    {
        unsigned int idx = idx_of(lat, lon);
        if (idx == idx_not_ready || !allocated)
        {
            result.r = result.g = result.b = 255;
            return result;
        }
        if (idx >= allocated) idx = allocated - 1;
        result.r = red_data   ? red_data[idx]   : 255;
        result.g = green_data ? green_data[idx] : 255;
        result.b = blue_data  ? blue_data[idx]  : 255;
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

double CelestialObject::estimate_surface_gravity()
{
    return (mass / earth_mass) / pow(volumetric_mean_radius / earth_radius, 2);
}

// TODO: Make this also work with Moon class width/depth somehow.
double CelestialObject::get_equatorial_radius()
{
    double polar_ratio = 1.0 - oblateness;
    if (!(polar_ratio > 0)) return volumetric_mean_radius;
    return volumetric_mean_radius / pow(polar_ratio, 1.0/3.0);
}

void alienorum::Atmosphere::calculate_tau(double pressure)
{
    if (!comp) return;                      // void function: nothing to return but control
    tau = atmospheric_tau(pressure*0.000009869,
        comp->CO2_portion, comp->CH4_portion, comp->H2O_portion, comp->N2O_portion,
        comp->O3_portion,  comp->SO2_portion, comp->H2S_portion, comp->CO_portion,
        comp->HCN_portion, comp->H2_portion,  comp->NH3_portion, comp->C2H6_portion);
}

void Map::generate_rocky_map(CelestialObject *cel)
{
    assert(cel->typeclass() == class_planet || cel->typeclass() == class_moon);
    mtx.lock();
    cel->randomize();
    generating_fic_texture = true;

    Planet *p = (Planet*)cel;
    int lr = cel->fictitious_map_height;
    double BV = cel->BV_color;
    if (BV < 0.8) BV = 0.8;                 // most rocks aren't blue
    if (cel->type == waterworld)
    {
        has_water = 1;
    }
    else if (randomize_txgen)
    {
        has_water = 0;
    }

    p->temperature = 0;
    double T_surf = p->estimate_surface_temperature();
    const double Tboil = water_freezing+100;                                     // Reference pressure

    // Constants for water b.p.
    const double R = 8.314;                                         // J/(mol*K)
    const double DELTA_H_VAP = 40660.0;                             // J/mol
    const double P1 = 1.0e+5;  

    // Clausius-Clapeyron calculation
    double inv_T1 = 1.0 / Tboil;
    double gas_constant_ratio = R / DELTA_H_VAP;
    double pressure_log = std::log(p->get_surface_pressure() / P1);

    double inv_T2 = inv_T1 - (gas_constant_ratio * pressure_log);
    double T_boil = 1.0 / inv_T2;
    // std::cout << "At " << (p->get_surface_pressure() / oneatm) << " atmospheres, water boils at " << T_boil << " K." << std::endl;

    bool life_possible = false;

    if (!p->get_surface_pressure())
    {
        double shoreline = CosmicShore::calculate_unified_metric(*(Star*)(p->get_light_center()), *p);
        double max_atm_pressure = (shoreline < 0) ? 0 : (pow(10, shoreline) * 503);
        if (isinf(max_atm_pressure)) max_atm_pressure = 0;
        p->ensure_atmosphere()->surface_pressure = p->cel_frand(0.1, 1) * max_atm_pressure;
    }

    AtmosphereComposition *ac = p->ensure_atmosphere()->ensure_composition();
    life_possible = (p->is_in_con_HZ() && cel->mass > 0.02 * earth_mass);       // Based on Titan's mass.
    if (randomize_txgen)
    {
        if (life_possible)
        {
            if (!show_taucalc) ac->generate_fictitious_habitable();
            p->atm->calculate_tau(p->get_surface_pressure());
            p->temperature = 0;
            T_surf = p->estimate_surface_temperature();
        }
        else if (!show_taucalc) ac->generate_fictitious_for_planet(p->type);
    }
    life_possible = (life_possible
        && p->get_surface_pressure() >= 600
        && T_surf > 0.9*water_freezing && T_surf < 320
        && p->get_surface_pressure() < oneatm*2000);

    p->atm->calculate_tau(p->get_surface_pressure());
    // std::cout << p->name << " tau=" << p->get_atmospheric_tau() << std::endl;

    if (life_possible)
    {
        p->temperature = 0;
        T_surf = p->estimate_surface_temperature();
        #ifdef DEBUG
            std::cout << "Surface pressure: " << (p->get_surface_pressure() / 101325) << " atm." << std::endl << std::flush;
            std::cout << "Surface temperature: " << T_surf << " K." << std::endl << std::flush;
        #endif

        if (randomize_txgen)
        {
            if (T_surf < 0.9 * water_freezing)
            {
                has_water = 1;
            }
            else if (T_surf < T_boil * 1.1)
            {
                double max_water = pow((T_boil*1.1 - T_surf) / (T_boil*1.1 - 0.9*water_freezing), 0.2);
                has_water = p->cel_frand(0, max_water);
            }
        }

        ac->H2O_portion = 0.014 * has_water;
        p->temperature = 0;
        p->atm->calculate_tau(p->get_surface_pressure());
        T_surf = p->estimate_surface_temperature();

        life_possible = (has_water >= 0.05
            && p->get_surface_pressure() >= 600
            && T_surf > 0.9*water_freezing && T_surf < 320
            && p->get_surface_pressure() < oneatm*2000);

        if (randomize_txgen)
        {
            if (life_possible)
            {
                if (!show_taucalc) ac->generate_fictitious_habitable();
                p->atm->calculate_tau(p->get_surface_pressure());
            }
        }
    }
    p->temperature = 0;
    p->atm->calculate_tau(p->get_surface_pressure());
    T_surf = p->estimate_surface_temperature();

    bool want_overcast_sky = false;
    if (p->get_surface_pressure() >= 5*oneatm && T_surf >= 400)
    {
        has_water = 0;
        want_overcast_sky = (p->cloud_map == nullptr);                                     // for a roasting Venus-like planet with a thick cloud layer.
    }

    int octaves = 5 + (cel->cel_rand() % 4);
    double lacbase = sqrt(fmax(1, log(cel->volumetric_mean_radius)));
    double lacunarity = p->cel_frand(0.51*lacbase, 0.53*lacbase);
    double gain = has_water ? 0.5 : 2.5;
    double scale = p->cel_frand(has_water ? 1.5 : 0.2, has_water ? 2.9 : 0.8);             // Controls feature sizes (smaller scale = larger continents)

    Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
    RGB3Byte rgb = Color::rgb_from_color(col, -1);

    // Tholins only survive on cold, distant bodies (Pluto, Triton, the icy moons), staining an
    // otherwise pale crust. Elsewhere provinces stay a neutral tint of the base rock color.
    bool cold_icy_world = (cel->type == icy) && (T_surf < 150.0);
    if (cold_icy_world)
    {
        rgb.r = (unsigned char)(rgb.r * 0.15 + 235 * 0.85);
        rgb.g = (unsigned char)(rgb.g * 0.15 + 235 * 0.85);
        rgb.b = (unsigned char)(rgb.b * 0.15 + 240 * 0.85);
    }

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

    double inv_h2o_level = 0, phi, psi, theta, u, v, nx, ny, nz, height_value, r_weight, T_base, T_local, sh;
    double province_scale = scale * 0.4, albedo_value, grain_value, border_noise;
    double province_pos, province_t, rmult, gmult, bmult;
    double edge_dist, mottle_strength, mottle_noise, hue_noise;
    unsigned int x, y;
    int idx, province_idx, neighbor_province_idx, mottled_idx;
    if (has_water && randomize_txgen && !vegetation_r && !vegetation_g && !vegetation_b)
    {
        RGB3Byte veg_color = generate_vegetation_color(&cel->rng);
        vegetation_r = veg_color.r;
        vegetation_g = veg_color.g;
        vegetation_b = veg_color.b;
    }
    if (has_water) inv_h2o_level = 1.0 / has_water;

    bool tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star 
        && p->is_tidal_locked();

    double Tswing = 256.0 / (p->get_surface_pressure() * 3.5e-5), halfswing = Tswing*0.5;

    // A small per-planet palette of terrain "provinces" -- bright highlands against darker
    // basalt or tholin -- so the surface reads as distinct patches with ragged borders rather
    // than one continuous gradient.
    const bool enable_provinces = true;
    int num_provinces = 2 + (cel->cel_rand() % 3);                                    // 2-4 provinces
    double border_roughness = p->cel_frand(1.3, 2.9);                               // how jagged the border itself is
    double border_noise_scale = province_scale * p->cel_frand(3.5, 7.0);
    double mottle_zone = p->cel_frand(0.64, 1.3);                                   // how far the speckling reaches into each province
    unsigned int dither_seed = (unsigned int)cel->cel_rand();                         // per-planet salt for the per-pixel mottle hash
    double hue_scale = province_scale * p->cel_frand(2.0, 4.0);                     // sub-regions within a single province
    double hue_amount = p->cel_frand(0.1, 0.3);                                     // subtle warm/cool wobble, green left alone
    double province_variability = p->cel_frand(0.1, 0.25);
    double province_rmult[4] = {1.0, 1.0, 1.0, 1.0};
    double province_gmult[4] = {1.0, 1.0, 1.0, 1.0};
    double province_bmult[4] = {1.0, 1.0, 1.0, 1.0};
    int tholin_province = cold_icy_world ? (num_provinces - 1) : -1;
    for (int p_i = 1; p_i < num_provinces; ++p_i)
    {
        if (p_i == tholin_province)
        {
            // Reddish-brown tholin staining: red retained, blue heavily suppressed.
            double stain = p->cel_frand(0.5, 0.85);
            province_rmult[p_i] = 1.0 - stain * 0.25;
            province_gmult[p_i] = 1.0 - stain * 0.55;
            province_bmult[p_i] = 1.0 - stain * 0.85;
        }
        else if (cold_icy_world)
        {
            // Otherwise just brightness variation across the icy background -- no hue shift.
            double p_mult = 0.9 - p->cel_frand(0, 0.2);
            province_rmult[p_i] = p_mult;
            province_gmult[p_i] = p_mult;
            province_bmult[p_i] = p_mult;
        }
        else
        {
            double p_rmult = fmax(0.05, 0.8 + p->cel_frand(-province_variability, province_variability));
            double p_bmult = fmax(0.05, 0.8 + p->cel_frand(-province_variability, province_variability));
            double lo = fmin(p_rmult, p_bmult), hi = fmax(p_rmult, p_bmult);
            double p_gmult = p->cel_frand(lo, lo + 0.5 * (hi - lo));
            province_rmult[p_i] = p_rmult;
            province_gmult[p_i] = p_gmult;
            province_bmult[p_i] = p_bmult;
        }
    }

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
            if (idx >= allocated) return;

            // Get noise value for this point on the sphere
            height_value = create_bump ? fBm(nx * scale, ny * scale, nz * scale, octaves, lacunarity, gain)
                : fmin(1, fmax(0, (inv_bump_scale * bump_data[idx] + 0.5)));

            if (create_bump) bump_data[idx] = bump_scale * (height_value - 0.5);

            albedo_value = fBm(nx * province_scale + 5.2, ny * province_scale + 1.3, nz * province_scale + 2.7,
                4, lacunarity, gain);

            if (enable_provinces)
            {
                grain_value = ridged_fBm(nx * scale * 14.0 + 91.7, ny * scale * 14.0 + 43.1, nz * scale * 14.0 + 17.9,
                    4, lacunarity, gain);
                r_weight = fmin(1.0, fmax(0.0, 0.55 + 0.9 * (grain_value - 0.5)));

                border_noise = fBm(nx * border_noise_scale + 61.4, ny * border_noise_scale + 8.8, nz * border_noise_scale + 27.6,
                    3, lacunarity, gain);

                double albedo_stretched = fmin(1.0, fmax(0.0, (albedo_value - 0.5) * 2.2 + 0.5));
                province_pos = fmod(albedo_stretched * num_provinces + (border_noise - 0.5) * border_roughness, (double)num_provinces);
                if (province_pos < 0) province_pos += num_provinces;
                province_idx = (int)province_pos;
                if (province_idx >= num_provinces) province_idx = num_provinces - 1;
                province_t = province_pos - province_idx;

                edge_dist = fmin(province_t, 1.0 - province_t);
                mottle_strength = fmax(0.0, 1.0 - edge_dist / mottle_zone);
                {
                    unsigned int mh = x * 374761393u + y * 668265263u + dither_seed;
                    mh = (mh ^ (mh >> 13)) * 1274126177u;
                    mh ^= (mh >> 16);
                    mottle_noise = (double)(mh & 0xFFFFFFu) / (double)0xFFFFFFu;
                }
                neighbor_province_idx = (province_t < 0.5)
                    ? (province_idx - 1 + num_provinces) % num_provinces
                    : (province_idx + 1) % num_provinces;
                mottled_idx = (mottle_noise < mottle_strength * 0.65) ? neighbor_province_idx : province_idx;
                rmult = province_rmult[mottled_idx];
                gmult = province_gmult[mottled_idx];
                bmult = province_bmult[mottled_idx];

                hue_noise = fBm(nx * hue_scale + 71.2, ny * hue_scale + 34.9, nz * hue_scale + 6.1,
                    3, lacunarity, gain);
                double hue_t = 2.0 * (hue_noise - 0.5);
                rmult *= (1.0 + hue_amount * hue_t);
                bmult *= (1.0 - hue_amount * hue_t);
            }
            else
            {
                grain_value = fBm(nx * scale * 6.0 + 91.7, ny * scale * 6.0 + 43.1, nz * scale * 6.0 + 17.9,
                    2, lacunarity, gain);
                r_weight = fmin(1.0, fmax(0.0, 0.75 * albedo_value + 0.25 * grain_value));
                rmult = gmult = bmult = 1.0;
            }

            if (has_water)
            {
                T_base = tidal_locked_to_star
                    ? (T_surf + halfswing - Tswing * cos(psi*0.5))
                    : (T_surf - halfswing + Tswing * sin(theta));
                T_local = T_base - Tswing * fmax(0, height_value - has_water);
                if (height_value < has_water && (T_local < water_freezing))
                {
                    // Polar and elevation ice
                    red_data[idx] = fmin(255, 167 + 67 * r_weight);
                    green_data[idx] = fmin(255, 181 + 57 * r_weight);
                    blue_data[idx] = fmin(255, 190 + 63 * r_weight);
                    // if (create_bump) bump_data[idx] = fmax(0, bump_data[idx]);
                }
                else if (cel->type == waterworld)
                {
                    // Deep ocean
                    red_data[idx] = fmin(255, 12+16*height_value);
                    green_data[idx] = fmin(255, 24+24*height_value);
                    blue_data[idx] = fmin(255, 32+128*height_value);
                }
                // Biome allocation based on height thresholds
                else if (height_value < has_water && (T_local < Tboil))
                {   // Ocean
                    sh = height_value*inv_h2o_level;
                    sh *= (Tboil - T_base) / (Tboil - water_freezing);
                    sh = pow(sh, 20);                                                           // shallowness multiplied to show water optical density
                    red_data[idx] = fmin(255, 12+16*sh);
                    green_data[idx] = fmin(255, 24+168*sh);
                    blue_data[idx] = fmin(255, 192+32*sh);
                    // if (create_bump) bump_data[idx] = 0;
                }
                else if (T_local > veg_max_temp)
                {   // Beach or desert sand
                    red_data[idx] = fmin(255, 220 * rmult * r_weight);
                    green_data[idx] = fmin(255, 200 * gmult * r_weight);
                    blue_data[idx] = fmin(255, 150 * bmult * r_weight);
                }
                else if (life_possible && T_local >= veg_min_temp
                    && (!tidal_locked_to_star || psi >= half_pi))                               // vegetation only on the day side
                {   // Forests
                    red_data[idx] = fmin(255, vegetation_r * r_weight);
                    green_data[idx] = fmin(255, vegetation_g * r_weight);
                    blue_data[idx] = fmin(255, vegetation_b * r_weight);
                }
                else
                {   // Mountains
                    red_data[idx] = fmin(255, 110 * rmult * r_weight);
                    green_data[idx] = fmin(255, 90 * gmult * r_weight);
                    blue_data[idx] = fmin(255, 75 * bmult * r_weight);
                }
            }
            else
            {
                // Lifeless planet or moon
                red_data[idx] = (cel->type == lavaworld)
                    ? ((unsigned char)(128 + fmin(127, rgb.r * rmult * r_weight + radd)))
                    : ((unsigned char)(fmin(255, rgb.r * rmult * r_weight + radd)));
                green_data[idx] = (unsigned char)(fmin(255, rgb.g * gmult * r_weight + gadd));
                blue_data[idx] = (unsigned char)(fmin(255, rgb.b * bmult * r_weight + badd));
            }
        }
    }

    cel->BV_color = 0.9 - has_water;

    if (create_bump) stamp_craters(cel, bump_scale);

    mtx.unlock();
    generating_fic_texture = false;
    touch_gen();

    // Deferred to the end so as to avoid the mutex lock.
    if (want_overcast_sky && !p->cloud_map)
    {
        p->cloud_map = new Map(cel);
        p->cloud_map->generate_overcast_sky(cel);
    }
    p->generate_ring_parameters();
}

void alienorum::Map::generate_lava_map(CelestialObject *cel)
{
    mtx.lock();
    generating_fic_texture = true;
    Planet *p = (Planet*)cel;

    // Copy size and allocate arrays.
    Map *rm = cel->surf_map;
    assert(rm);
    image_height = rm->image_height;
    image_width = rm->image_width;

    lat_scale = (double)image_height / _pi;
    lon_scale = (double)image_width / (_pi * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;

    allocated = image_height * image_width;
    assert(allocated == rm->allocated);
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];

    // Copy bump data.
    double bump_scale = p->estimate_bump_scale(), inv_bump_scale = 1.0 / bump_scale;
    bump_data = new double[allocated];
    memcpy(bump_data, rm->bump_data, allocated * sizeof(double));
    std::cout << "Allocated " << allocated << " pixels for fictitious night map." << std::endl;
    mtx.unlock();

    // Based on temperature, calculate the degree of lava glow.
    double tempK = p->estimate_surface_temperature(), ltemp;
    const double glow_amt = 6.0 / blackbody_flux(tempK, R_band);
    std::cout << glow_amt << std::endl;
    double Tswing = tempK * 0.1 / (1.0 + p->get_surface_pressure() * 3.5e-5);

    // Fill in glowing hot lava.
    int x, y, y1, idx;
    double height;
    RGB3Byte rgb;
    for (y=0; y<image_height; y++)
    {
        y1 = y * image_width;
        for (x=0; x<image_width; x++)
        {
            idx = y1 + x;
            height = 0.5 + inv_bump_scale * bump_data[idx];
            ltemp = tempK - Tswing * height;

            // if (!x) std::cout << ltemp << " -> " << (glow_amt * blackbody_flux(ltemp, R_band)) << std::endl;
            rgb.r = fmin(255, glow_amt * blackbody_flux(ltemp*1.25, R_band));            // exaggerate the colors for effect.
            rgb.g = fmin(255, glow_amt * blackbody_flux(ltemp, V_band));
            rgb.b = fmin(255, glow_amt * blackbody_flux(ltemp, U_band));

            red_data[idx] = rgb.r;
            green_data[idx] = rgb.g;
            blue_data[idx] = rgb.b;
        }
    }

    generating_fic_texture = false;
    touch_gen();
}

void Map::stamp_craters(CelestialObject *cel, double bump_scale)
{
    assert(cel->typeclass() == class_planet || cel->typeclass() == class_moon);
    Planet *p = (Planet*)cel;

    double atmosphere_factor = 1.0 / (1.0 + p->get_surface_pressure() * inv_oneatm * 3.0);
    double belt_factor = 1.0;
    CelestialObject *lc = p->get_light_center();
    if (lc && lc->type == star)
    {
        Star *host_star = (Star*)lc;
        if (host_star->has_disk && host_star->disk_inner_edge_sma > 0)
        {
            const double our_kuiper_inner_edge = 30.0 * AU;                 // Neptune's orbit, roughly
            belt_factor = fmin(4.0, fmax(1.0, sqrt(our_kuiper_inner_edge / host_star->disk_inner_edge_sma)));
        }
    }
    double bombardment_factor = atmosphere_factor * belt_factor;

    int num_craters = (int)((p->type == lavaworld ? 3 : 3000) * bombardment_factor);
    if (num_craters < 1) return;

    double planet_radius = cel->volumetric_mean_radius;
    double min_diam = 50.0;                                                 // meters.
    double max_diam = fmin(planet_radius * 0.3, 900000.0);                  // cap basins at ~900 km or 30% of the planet, whichever is smaller

    std::vector<Crater> craters(num_craters);
    for (int i = 0; i < num_craters; ++i)
    {
        Crater &c = craters[i];

        double z = cel->cel_frand(-1, 1), phi = cel->cel_frand(0, 2 * _pi), r = sqrt(fmax(0.0, 1 - z * z));
        c.cx = r * cos(phi);
        c.cy = r * sin(phi);
        c.cz = z;

        // Heavily skewed toward small craters, like real crater size-frequency distributions.
        double diam = min_diam + (max_diam - min_diam) * pow(cel->cel_frand(0, 1), 5);
        c.angular_radius = fmax(1e-4, (diam * 0.5) / planet_radius);

        c.depth = bump_scale * cel->cel_frand(0.3, 0.9);
        c.rim_height = c.depth * cel->cel_frand(0.15, 0.35);
        c.rim_width = cel->cel_frand(0.15, 0.3);
        c.reach_factor = 1.0 + 3.0 * c.rim_width;
        c.central_peak = (diam > max_diam * 0.5) ? c.depth * cel->cel_frand(0.2, 0.4) : 0.0;

        c.has_rays = (diam > max_diam * 0.67) && (cel->cel_frand(0, 1) < 0.01 * fmin(1.0, bombardment_factor));
        c.ray_freq = cel->cel_frand(5, 10);
        c.ray_phase = cel->cel_frand(0, 2 * _pi);
        c.ray_sharpness = cel->cel_frand(6, 14);
        c.ray_extent_factor = c.reach_factor + cel->cel_frand(3.0, 7.0);        // rays reach much farther than the rim/ejecta blanket

        double refx = 0, refy = 0, refz = 1;
        if (fabs(c.cz) > 0.9) { refx = 1; refy = 0; refz = 0; }
        c.ex = refy * c.cz - refz * c.cy;
        c.ey = refz * c.cx - refx * c.cz;
        c.ez = refx * c.cy - refy * c.cx;
        double elen = sqrt(c.ex * c.ex + c.ey * c.ey + c.ez * c.ez);
        c.ex /= elen; c.ey /= elen; c.ez /= elen;
        c.tnx = c.cy * c.ez - c.cz * c.ey;
        c.tny = c.cz * c.ex - c.cx * c.ez;
        c.tnz = c.cx * c.ey - c.cy * c.ex;
    }

    for (const Crater &c : craters)
    {
        double theta_c = acos(fmax(-1.0, fmin(1.0, c.cz)));
        double phi_c = atan2(c.cy, c.cx);
        if (phi_c < 0) phi_c += 2 * _pi;

        double reach = c.angular_radius * (c.has_rays ? c.ray_extent_factor : c.reach_factor);
        double theta_lo = fmax(0.0, theta_c - reach), theta_hi = fmin(_pi, theta_c + reach);
        long y_lo = (long)(theta_lo / _pi * image_height) - 1;
        long y_hi = (long)(theta_hi / _pi * image_height) + 1;
        if (y_lo < 0) y_lo = 0;
        if (y_hi >= (long)image_height) y_hi = image_height - 1;

        for (long y = y_lo; y <= y_hi; ++y)
        {
            double theta = ((double)y / image_height) * _pi;
            double sin_theta = sin(theta);
            double phi_reach = (sin_theta < 0.05) ? _pi : fmin(_pi, reach / sin_theta);

            long x_span = (long)(phi_reach / (2 * _pi) * image_width) + 1;
            long x_center = (long)(phi_c / (2 * _pi) * image_width);

            for (long xi = -x_span; xi <= x_span; ++xi)
            {
                long x = ((x_center + xi) % (long)image_width + (long)image_width) % (long)image_width;
                double u = (double)x / image_width, phi = u * 2.0 * _pi;
                double px = sin(theta) * cos(phi), py = sin(theta) * sin(phi), pz = cos(theta);

                double dot = fmax(-1.0, fmin(1.0, px * c.cx + py * c.cy + pz * c.cz));
                double d = acos(dot);
                double r = d / c.angular_radius;
                if (r > c.reach_factor && !(c.has_rays && r <= c.ray_extent_factor)) continue;

                double bump_delta = 0.0, color_mult = 1.0;
                if (r < c.reach_factor)
                {
                    if (r < 1.0)
                    {
                        // Bowl floor, with an optional central peak for larger/complex craters.
                        bump_delta = c.depth * (r * r - 1.0);
                        if (c.central_peak > 0) bump_delta += c.central_peak * exp(-(r / 0.15) * (r / 0.15));
                    }
                    else
                    {
                        // Raised rim, fading out into the terrain. Kept subtle: most craters are
                        // old, and real standout brightness belongs to the rays below.
                        double t = (r - 1.0) / c.rim_width;
                        bump_delta = c.rim_height * exp(-t * t);
                        color_mult += 0.15 * (bump_delta / fmax(1e-6, c.rim_height));
                    }
                }

                unsigned long idx = y * image_width + x;
                if (idx >= allocated) continue;

                // Rays are ejecta outside the crater, reaching far past the rim and fading in
                // gradually in thin bright streaks.
                int rayr = 0, rayg = 0, rayb = 0;
                if (red_data[idx] > 0.6*blue_data[idx])                 // No rays on the ocean floor.
                {
                    if (c.has_rays && r > 1.0)
                    {
                        double ray_r = (r - 1.0) / (c.ray_extent_factor - 1.0);
                        if (ray_r <= 1.0)
                        {
                            double comp_e = px * c.ex + py * c.ey + pz * c.ez;
                            double comp_n = px * c.tnx + py * c.tny + pz * c.tnz;
                            double bearing = atan2(comp_e, comp_n);
                            double lobe = pow(fabs(cos(c.ray_freq * (bearing - c.ray_phase))), c.ray_sharpness);
                            double ray_falloff = pow(fmax(0.0, 1.0 - ray_r), 2.0);
                            double ray_strength = 0.35 * lobe * ray_falloff;
                            rayr = 250 * ray_strength;
                            rayg = 244 * ray_strength;
                            rayb = 236 * ray_strength;
                            color_mult *= (1.0 - ray_strength);
                        }
                    }
                }

                bump_data[idx] += bump_delta;
                if (color_mult != 1.0)
                {
                    red_data[idx]   = (unsigned char)fmin(255, red_data[idx]   * color_mult + rayr);
                    green_data[idx] = (unsigned char)fmin(255, green_data[idx] * color_mult + rayg);
                    blue_data[idx]  = (unsigned char)fmin(255, blue_data[idx]  * color_mult + rayb);
                }
            }
        }
    }
}

void Map::generate_gas_giant_map(CelestialObject *cel)
{
    cel->randomize();

    mtx.lock();
    generating_fic_texture = true;
    int lr = cel->fictitious_map_height;
    double BV = cel->BV_color;
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

    Planet *p = (Planet*)cel;
    Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
    RGB3Byte rgb = Color::rgb_from_color(col, p->albedo);

    p->ensure_atmosphere()->ensure_composition()->generate_fictitious_for_planet(p->type);

    bool tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star 
        && (fabs((p->sidereal_rotational_period / p->orbit->period) - 1) < 0.01);

    cel->randomize();
    double variability = cel->cel_frand(0, 0.666);
    int num_bands = cel->cel_rand() % 9 + 7, i;
    if (cel->type == ice_giant)
    {
        num_bands = std::max(2, num_bands/4);
        variability /= 4;
    }
    auto bands = std::make_unique<RGB3Byte[]>(num_bands);

    bool add_storm = !tidal_locked_to_star && (cel->cel_frand(0, 1) < 0.2);
    double stormlat, stormlon, distToStormX, distToStormY, stormDist = 1e29, stormSize = cel->cel_frand(0.29, 0.71);

    stormlat = cel->cel_frand(0.3, 0.7);
    stormlon = cel->cel_frand(0, 1);
    mtx.unlock();

    for (i=0; i<num_bands; i++)
    {
        double rmult, gmult, bmult;

        rmult = 1.0 - cel->cel_frand(0, variability);
        bmult = 1.0 - cel->cel_frand(0, variability);
        gmult = cel->cel_frand(fmin(rmult, bmult), fmax(rmult, bmult));

        bands[i].r = rgb.r * rmult;
        bands[i].g = rgb.g * gmult;
        bands[i].b = rgb.b * bmult;
    }

    double scale = 2.5, u, v, theta, phi, psi, sin_theta, nx, ny, nz, distortX, distortY, finalNoise, bandVal, t;
    double zscale = tidal_locked_to_star ? scale : (scale * 1.5);

    for (unsigned int y = 0; y < image_height; ++y)
    {
        v = (double)y / image_height;
        theta = v * _pi;

        for (unsigned int x = 0; x < image_width; ++x)
        {
            u = (double)x / image_width;
            phi = u * 2.0 * _pi;

            // 3D Sphere projection
            nx = sin(theta) * cos(phi) * scale;
            ny = sin(theta) * sin(phi) * scale;
            nz = cos(theta) * zscale;

            if (tidal_locked_to_star)
            {
                sin_theta = sin(theta);

                phi -= 2.9e+5 / cel->sidereal_rotational_period 
                    * sin_theta * sin_theta * sin_theta * sin_theta * sin_theta
                    * sin_theta * sin_theta * sin_theta * sin_theta * sin_theta
                    * sin_theta * sin_theta * sin_theta * sin_theta * sin_theta;
                nx = sin(theta) * cos(phi) * scale;
                ny = sin(theta) * sin(phi) * scale;

                psi = find_3D_angle(Point(nx,ny,nz), xaxis, center);
            }

            distortX = fBm(nx, ny, nz, 4, 2.0, 0.5) * 1.5;
            distortY = fBm(nx + 5.2, ny + 1.3, nz + 2.7, 4, 2.0, 0.5) * 1.3;
            // Apply distortion primarily along the X/longitude axis to emulate wind bands
            finalNoise = fBm(nx + distortX * 4.0, ny + distortY * 2.5, nz, 6, 2.0, 0.55);

            if (add_storm)
            {
                distToStormX = (u - stormlon) * 2.0 * _pi;
                distToStormY = (v - stormlat) * _pi;
                // Elliptical distance formula
                stormDist = sqrt((distToStormX * distToStormX) * 2.5 + (distToStormY * distToStormY) * 10.0);
            }

            int idx = y * image_width + x;

            if (stormDist < stormSize)
            {
                // We are inside the storm; blend into dark colors
                double stormBlend = (stormSize - stormDist) / stormSize; // 1 at center, 0 at edge
                // Swirl the storm inside
                double stormNoise = fBm(nx * 3.0, ny * 3.0, nz * 3.0, 3, 2.0, 0.5);

                red_data[idx] = (unsigned char)(120 * stormNoise + 25);
                green_data[idx] = (unsigned char)(90 * stormNoise + 15);
                blue_data[idx] = (unsigned char)(60 * stormNoise + 10);

                // Linear interpolation blending storm with background bands
                red_data[idx] = (unsigned char)(red_data[idx] * stormBlend + bands[0].r * (1.0 - stormBlend));
                green_data[idx] = (unsigned char)(green_data[idx] * stormBlend + bands[0].g * (1.0 - stormBlend));
                blue_data[idx] = (unsigned char)(blue_data[idx] * stormBlend + bands[0].b * (1.0 - stormBlend));
            }
            else
            {
                if (tidal_locked_to_star)
                {
                    // Bands will occur in order of distance to the star, not by latitude as with solar system gas giants.
                    bandVal = fmod(psi / _pi * num_bands + finalNoise * 1.3, num_bands);
                }
                else
                {
                    bandVal = fmod(fabs(v - 0.5) * 2 * num_bands + finalNoise * 1.3, num_bands);
                }

                if (bandVal < 0) bandVal += num_bands;
                int bandIdx = (int)floor(bandVal);
                t = bandVal - bandIdx; // fractional part for linear interpolation

                int nextBandIdx = (bandIdx + 1) % num_bands;

                // Interpolate colors between bands for smooth transitions
                red_data[idx] = (unsigned char)((1.0 - t) * bands[bandIdx].r + t * bands[nextBandIdx].r);
                green_data[idx] = (unsigned char)((1.0 - t) * bands[bandIdx].g + t * bands[nextBandIdx].g);
                blue_data[idx] = (unsigned char)((1.0 - t) * bands[bandIdx].b + t * bands[nextBandIdx].b);
            }
        }
    }
    generating_fic_texture = false;
    touch_gen();
    p->generate_ring_parameters();
}

void alienorum::Map::generate_overcast_sky(CelestialObject *cel)
{
    cel->randomize();

    mtx.lock();
    generating_fic_texture = true;
    int lr = cel->fictitious_map_height;
    double BV = cel->BV_color;
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
    std::cout << "Allocated " << allocated << " pixels for fictitious overcast sky map." << std::endl;

    Planet *p = (Planet*)cel;
    RGB3Byte rgb( (unsigned char)cel->cel_frand(242, 249), (unsigned char)cel->cel_frand(234, 243), (unsigned char)cel->cel_frand(214, 228) );

    bool tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star
        && (fabs((p->sidereal_rotational_period / p->orbit->period) - 1) < 0.01);

    cel->randomize();

    RGB3Byte deep((unsigned char)(rgb.r * cel->cel_frand(0.78, 0.86)),
                  (unsigned char)(rgb.g * cel->cel_frand(0.66, 0.75)),
                  (unsigned char)(rgb.b * cel->cel_frand(0.44, 0.56)));

    double zonal = cel->cel_frand(5.0, 9.0);
    double scale = cel->cel_frand(1.6, 2.6);

    double shear = cel->cel_frand(1.5, 4.0);
    double sweep_exp = cel->cel_frand(1.0, 1.6);

    double warp_amt = cel->cel_frand(0.5, 1.1);
    double polar_k = cel->cel_frand(1.8, 3.6);               // how tightly the bright hood hugs the poles
    double contrast = cel->cel_frand(0.38, 0.62);            // still low -- this is haze, not weather

    double mottle_scale = scale * cel->cel_frand(2.5, 4.5);
    double mottle_amt = cel->cel_frand(0.04, 0.10);
    mtx.unlock();

    double u, v, theta, phi, phi_s, sin_theta, cos_theta, nx, ny, nz;
    double ax, ay, az, wx, wy, n, band, t, mottle, psi;
    unsigned int x, y, idx;

    for (y = 0; y < image_height; ++y)
    {
        v = (double)y / image_height;
        theta = v * _pi;
        sin_theta = sin(theta);
        cos_theta = cos(theta);                     // = sin(latitude), signed, +1 at the north pole

        for (x = 0; x < image_width; ++x)
        {
            u = (double)x / image_width;
            phi = u * 2.0 * _pi;

            phi_s = phi + shear * pow(fabs(cos_theta), sweep_exp);
            nx = sin_theta * cos(phi_s);
            ny = sin_theta * sin(phi_s);
            nz = cos_theta;

            idx = y * image_width + x;
            if (idx >= allocated) break;

            // Anisotropic sampling: slow along longitude, ordinary along latitude.
            ax = nx * scale / zonal;
            ay = ny * scale / zonal;
            az = nz * scale;

            // Domain warping, itself stretched the same way, for the wispy drawn-out filaments a
            // plain fBm never produces on its own.
            wx = fBm(ax + 5.2, ay + 1.3, az + 2.7, 3, 2.0, 0.5) - 0.5;
            wy = fBm(ax + 9.1, ay + 4.4, az + 7.3, 3, 2.0, 0.5) - 0.5;
            n = fBm(ax + warp_amt * wx * 3.0, ay + warp_amt * wx * 3.0, az + warp_amt * wy * 0.8,
                4, 2.0, 0.55);

            if (tidal_locked_to_star)
            {
                psi = find_3D_angle(Point(nx, ny, nz), xaxis, center);
                band = pow(fmax(0.0, cos(psi)) * 0.5 + 0.5, polar_k);
            }
            else
            {
                band = pow(fabs(cos_theta), polar_k);       // 0 at the equator, 1 at the poles
            }

            mottle = fBm(nx * mottle_scale + 31.7, ny * mottle_scale + 12.9, nz * mottle_scale + 44.1,
                2, 2.0, 0.5) - 0.5;

            t = band + contrast * (n - 0.5) + mottle_amt * mottle;
            if (t < 0) t = 0;
            else if (t > 1) t = 1;

            red_data[idx]   = (unsigned char)(deep.r + (rgb.r - deep.r) * t);
            green_data[idx] = (unsigned char)(deep.g + (rgb.g - deep.g) * t);
            blue_data[idx]  = (unsigned char)(deep.b + (rgb.b - deep.b) * t);
        }
    }

    generating_fic_texture = false;
    touch_gen();
}

void Map::generate_stellar_map(CelestialObject *cel)
{
    assert(cel->typeclass() == class_star);
    cel->randomize();

    mtx.lock();
    generating_fic_texture = true;

    image_height = cel->fictitious_map_height;
    image_width = image_height * 2;
    allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];
    lat_scale = (double)image_height / _pi;
    lon_scale = (double)image_width / (_pi * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;
    std::cout << "Allocated " << allocated << " pixels for fictitious stellar map." << std::endl;

    stellar_regime_t regime = stellar_regime(cel);

    double T_eff = (cel->temperature > 0) ? cel->temperature : Star::temperature_from_BV(cel->BV_color);
    if (!(T_eff > 0) || isnan(T_eff) || isinf(T_eff)) T_eff = sun_temp;

    double mass_solar = cel->mass / solar_mass;
    bool fully_convective = (mass_solar > 0 && mass_solar < 0.35);
    bool convective_envelope = (T_eff < 7000.0);
    bool spotted = (regime == regime_stellar) && convective_envelope;

    double radius_m = (regime == regime_degenerate) ? Star::degenerate_radius(cel->mass)
                                                    : cel->volumetric_mean_radius;
    if (!(radius_m > 0)) radius_m = solar_radius;
    double mass_g = (cel->mass > 0) ? cel->mass : solar_mass;
    double surface_g = G * mass_g / (radius_m * radius_m);
    double logg_cgs = log10(fmax(1e-6, surface_g) * 100.0);

    double p_rot_days = cel->sidereal_rotational_period / oneday;
    double tau_conv_days = 12.0 * pow(fmax(3000.0, fmin(7000.0, T_eff)) / sun_temp, -2.5);
    double rossby = (p_rot_days > 0) ? (p_rot_days / tau_conv_days)
                                     : ((logg_cgs < 3.5) ? cel->cel_frand(2.5, 9.0) : cel->cel_frand(0.4, 3.0));
    double activity = fmin(1.0, 0.35 / fmax(0.05, rossby));
    double spot_dT = fmax(150.0, fmin(0.541 * T_eff - 1323.0, 0.45 * T_eff));
    double gd_beta = convective_envelope ? 0.08 : 0.25;
    double f_obl = fmax(0.0, fmin(0.45, cel->oblateness));
    double gd_mean = 1.0 - 4.0 * f_obl / 3.0;

    const double atomic_mass_unit = 1.66053906660e-27;                  // kg
    double scale_height = kB * T_eff / (1.3 * atomic_mass_unit * surface_g);
    double d_granule = 10.0 * scale_height;

    double granule_correction = 8;

    if (d_granule > 0.5 * radius_m) d_granule = 0.5 * radius_m;
    double cells_across = 2.0 * radius_m / fmax(1.0, d_granule) * granule_correction;

    bool granulated = (regime == regime_stellar) && convective_envelope;
    double gran_scale = cells_across / _pi;
    double gran_nyquist = (double)image_width / (6.0 * _pi) * granule_correction;
    double gran_amp = 1.0;
    if (gran_scale > gran_nyquist)
    {
        gran_amp = fmax(0.25, gran_nyquist / gran_scale);
        gran_scale = gran_nyquist;
    }

    double gran_dT = granulated
        ? T_eff * 0.035 * sqrt(fmax(0.5, fmin(1.8, T_eff / sun_temp))) * gran_amp : 0.0;

    double spot_base_deg = 0.35 + 7.0 * activity;
    int num_groups = spotted ? (int)(activity * 7.0 + cel->cel_frand(0.8, 1.5)) : 0;
    bool polar_regime = (rossby < 0.12);

    std::vector<Point> spot_axis;
    std::vector<double> spot_radius;
    for (int gi = 0; gi < num_groups; gi++)
    {
        double glat;
        if (polar_regime && cel->cel_frand(0, 1) < 0.55)
            glat = (cel->cel_frand(0, 1) < 0.5 ? 1 : -1) * cel->cel_frand(fiftyseventh * 55, half_pi);
        else if (fully_convective)
            glat = asin(cel->cel_frand(-1, 1));                          // uniform in area, not in latitude
        else
            glat = (cel->cel_frand(0, 1) < 0.5 ? 1 : -1) * cel->cel_frand(fiftyseventh * 5, fiftyseventh * 35);

        double glon = cel->cel_frand(0, _pi * 2);
        double spread = fiftyseventh * spot_base_deg * cel->cel_frand(2.0, 5.0);
        int members = 2 + (cel->cel_rand() % 4);

        for (int mi = 0; mi < members; mi++)
        {
            // A group is much wider than it is tall: the spread in longitude dominates.
            double slat = fmax(-half_pi, fmin(half_pi, glat + cel->cel_frand(-0.4, 0.4) * spread));
            double slon = glon + cel->cel_frand(-1.0, 1.0) * spread * 2.2 / fmax(0.25, cos(glat));
            double cos_slat = cos(slat);
            spot_axis.push_back(Point(cos_slat * cos(slon), cos_slat * sin(slon), sin(slat)));

            // The head of the group is the main spot; the rest are subordinate to it.
            double rad_deg = spot_base_deg * (mi == 0 ? cel->cel_frand(0.8, 1.6) : cel->cel_frand(0.25, 0.7));
            spot_radius.push_back(fiftyseventh * rad_deg);
        }
    }
    int num_spots = (int)spot_axis.size();

    bool has_faculae = spotted && (activity > 0.02);
    Map *fac = nullptr;
    if (has_faculae && !cel->night_map)
    {
        fac = new Map(cel);
        fac->image_height = image_height;
        fac->image_width = image_width;
        fac->allocated = allocated;
        fac->red_data = new unsigned char[allocated];
        fac->green_data = new unsigned char[allocated];
        fac->blue_data = new unsigned char[allocated];
        fac->lat_scale = lat_scale;
        fac->lon_scale = lon_scale;
        fac->inv_lat_scale = inv_lat_scale;
        fac->inv_lon_scale = inv_lon_scale;
        cel->night_map = fac;
    }
    double net_scale = fmax(0.7, gran_scale / 20.0);
    double plage_gain = fmin(0.55, 0.20 + 0.5 * activity);           // excess over the photosphere
    double net_gain = fmin(0.30, 0.08 + 0.35 * activity);

    double mean_spot_rad = fiftyseventh * fmax(0.2, spot_base_deg);
    double edge_scale = fmax(4.0, 2.5 / mean_spot_rad);
    double edge_amount = cel->cel_frand(0.45, 0.7);
    double fil_scale = edge_scale * 3.5;                         // penumbral filaments

    // Blackbody B-V, to vary the hue according to local temperature.
    auto bv_of = [](double T) -> double
    {
        return log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
    };

    double bv_at_teff = bv_of(T_eff);
    Color ref_col = Color::color_from_magnitude_indices(0, cel->BV_color);
    double ref_max = fmax(ref_col.red, fmax(ref_col.green, ref_col.blue));
    double inv_ref_max = (ref_max > 0) ? (1.0 / ref_max) : 1.0;
    mtx.unlock();

    double theta, phi, sin_theta, nx, ny, nz, lat, cos_lat;
    double T_local, gd_factor, umbra, dist, warped;
    unsigned int x, y, idx;

    for (y = 0; y < image_height; ++y)
    {
        theta = ((double)y / image_height) * _pi;            // 0 at the north pole, Pi at the south pole
        lat = half_pi - theta;
        sin_theta = sin(theta);
        cos_lat = sin_theta;

        gd_factor = pow((1.0 - 2.0 * f_obl * cos_lat * cos_lat) / gd_mean, gd_beta);

        for (x = 0; x < image_width; ++x)
        {
            phi = ((double)x / image_width) * _pi * 2.0;
            nx = sin_theta * cos(phi);
            ny = sin_theta * sin(phi);
            nz = cos(theta);

            idx = y * image_width + x;
            if (idx >= allocated) break;

            T_local = T_eff * gd_factor;

            if (gran_dT > 0)
            {
                double cell = ridged_fBm(nx * gran_scale, ny * gran_scale, nz * gran_scale, 3, 2.1, 0.5);
                T_local -= gran_dT * (1.0 - cell);
            }

            double plage = 0;
            if (num_spots)
            {
                warped = edge_amount * (fBm(nx * edge_scale, ny * edge_scale, nz * edge_scale, 5, 2.2, 0.55) - 0.5) * 2.0;
                for (int i = 0; i < num_spots; i++)
                {
                    dist = acos(fmax(-1.0, fmin(1.0, nx * spot_axis[i].x + ny * spot_axis[i].y + nz * spot_axis[i].z)));
                    double r = spot_radius[i] * (1.0 + warped);
                    if (r <= 0) continue;

                    if (has_faculae && dist >= r && dist < 2.6 * r)
                    {
                        double pt = (dist - r) / (1.6 * r);
                        plage = fmax(plage, (1.0 - pt) * (1.0 - pt));
                    }

                    if (dist >= r) continue;

                    if (dist < 0.45 * r)
                    {
                        umbra = 1.0;                                    // umbra: full deficit
                    }
                    else
                    {
                        double t = (dist - 0.45 * r) / (0.55 * r);
                        double fil = fBm(nx * fil_scale, ny * fil_scale, nz * fil_scale, 3, 2.0, 0.5);
                        umbra = 0.38 * (1.0 - t) * (0.7 + 0.6 * fil);
                    }
                    T_local = fmin(T_local, T_eff * gd_factor - spot_dT * umbra);
                    plage = 0;                                          // no plage on the spot itself
                }
            }

            if (T_local < 1000.0) T_local = 1000.0;

            double bv_local = cel->BV_color + (bv_of(T_local) - bv_at_teff);
            double lum = pow(T_local / T_eff, 1.6);

            RGB3Byte rgb = Color::rgb_from_color(
                Color::color_from_magnitude_indices(0, bv_local), lum * inv_ref_max);

            red_data[idx]   = rgb.r;
            green_data[idx] = rgb.g;
            blue_data[idx]  = rgb.b;

            if (fac)
            {
                double net = ridged_fBm(nx * net_scale + 13.7, ny * net_scale + 4.1, nz * net_scale + 29.3,
                    2, 2.0, 0.5);
                net = fmax(0.0, (net - 0.72) / 0.28);               // keep only the ridges

                double excess = fmin(1.0, plage_gain * plage + net_gain * net);
                fac->red_data[idx]   = (unsigned char)fmin(255.0, rgb.r * excess);
                fac->green_data[idx] = (unsigned char)fmin(255.0, rgb.g * excess);
                fac->blue_data[idx]  = (unsigned char)fmin(255.0, rgb.b * excess);
            }
        }
    }

    generating_fic_texture = false;
    if (fac) fac->touch_gen();
    touch_gen();
}

void alienorum::Map::generate_ring_map(CelestialObject *cel, int res, double rir, double mo, Map *xmap)
{
    assert(cel);
    cel_obj_class cls = cel->typeclass();
    assert(cls == class_planet || cls == class_moon);

    cel->ring_map = this;
    if (xmap) cel->ringx_map = xmap;
    else xmap = cel->ringx_map;

    assert(xmap);
    cel->randomize();

    mtx.lock();
    generating_fic_texture = true;

    xmap->image_height = image_height = 29;             // TODO: Decrease this for release.
    xmap->image_width = image_width = res;
    xmap->allocated = allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];
    xmap->red_data = new unsigned char[allocated];
    xmap->green_data = new unsigned char[allocated];
    xmap->blue_data = new unsigned char[allocated];
    xmap->lat_scale = lat_scale = (double)image_height / _pi;
    xmap->lon_scale = lon_scale = (double)image_width / (_pi * 2);
    xmap->inv_lat_scale = inv_lat_scale = 1.0 / lat_scale;
    xmap->inv_lon_scale = inv_lon_scale = 1.0 / lon_scale;
    std::cout << "Allocated " << allocated << " pixels for fictitious ring map." << std::endl;
    mtx.unlock();
    generating_fic_texture = false;

    RGB3Byte rgb, xrgb;
    int x, y, idx;
    int inx = rir * image_width;

    double red = cel->cel_frand(15, 60), dred = 0, oe = 1, doe = 0;
    const double redstep = 0.1, oestep = 0.00666;

    double inv_img_wid = 1.0 / image_width;
    for (x=0; x<image_width; x++)
    {
        double fx = inv_img_wid * x;

        // keep colors pale like Saturn, just change redness
        rgb.r = 250;
        rgb.g = 250 - 0.5*red;
        rgb.b = 240 - red;
        xrgb.r = xrgb.g = xrgb.b = 255 - (255.0 * mo
            * sigmoid((double)(x-inx) * 0.05)       // inner bound
            * pow(1.0-fx, 0.1)                      // taper outer bound
            * pow(cel->cel_frand(0.4,0.6), oe)      // detail
            );
        
        // wandering drunkard on color
        red += dred;
        dred += cel->cel_frand(-redstep,redstep);
        if (red < 0) { red = 0; dred = fabs(dred); }
        else if (red > 215) dred = -fabs(dred);
        dred *= 0.97;

        // wandering drunkard on opacity exponent
        oe *= (1.0 + doe);
        doe += cel->cel_frand(-oestep, oestep);
        if (oe < 0.3) { oe = 0.3; doe = fabs(doe); }
        if (oe > 3) doe = -fabs(doe);
        doe *= 0.97;

        for (y=0; y<image_height; y++)
        {
            idx = x + y * image_width;
            red_data[idx] = rgb.r;
            green_data[idx] = rgb.g;
            blue_data[idx] = rgb.b;
            xmap->red_data[idx] = xrgb.r;
            xmap->green_data[idx] = xrgb.g;
            xmap->blue_data[idx] = xrgb.b;
        }
    }

    generating_fic_texture = false;
    touch_gen();
    xmap->touch_gen();
}

void alienorum::Map::_map_resample_bump_regen_rocky(CelestialObject *cel)
{
    unsigned char *lred = red_data, *lgreen = green_data, *lblue = blue_data;
    red_data = green_data = blue_data = nullptr;
    if (lred  ) delete[] lred;
    if (lgreen) delete[] lgreen;
    if (lblue ) delete[] lblue;
    resample_bump_data(cel->fictitious_map_height);
    generate_rocky_map(cel);
    if (cel->type == lavaworld && !cel->night_map)
    {
        cel->night_map = new Map(cel);
        cel->night_map->generate_lava_map(cel);
    }
}

void _resample_bump_regen_rocky(Map *map, CelestialObject *cel)
{
    map->_map_resample_bump_regen_rocky(cel);
}

void alienorum::Map::mark_for_map_regen(CelestialObject *cel, bool discard_bump)
{
    if (discard_bump && bump_data)
    {
        double *old_bump = bump_data;
        bump_data = nullptr;
        delete[] old_bump;
    }

    if (bump_data && uses_rocky_map(cel->type))
    {
        mcel = cel;
        std::thread tregen(_resample_bump_regen_rocky, this, cel);
        tregen.detach();
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

double alienorum::CelestialObject::density()
{
    if (!volumetric_mean_radius || !mass) return 0;
    return mass / sphere_volume(volumetric_mean_radius) * 1e-6;
}

double alienorum::CelestialObject::Hill_sphere_radius()
{
    if (!orbit || !orbit->center) return 0.0;
    if (orbit->eccentricity < 0.0 || orbit->eccentricity >= 1.0) return 0.0;
    return orbit->semimajor_axis * (1.0 - orbit->eccentricity) * std::cbrt(mass / (3.0 * orbit->center->mass));
}

double alienorum::CelestialObject::Roche_limit(CelestialObject* orbiter)
{
    double primary_density = density(), orbiter_density = orbiter ? orbiter->density() : 3.35;          // Default = lunar density
    if (primary_density <= 0.0 || orbiter_density <= 0.0) return 0.0;

    // Multiplier constant changes based on rigidity
    bool is_fluid = orbiter && (orbiter->type == waterworld
        || orbiter->type == gas_giant || orbiter->type == ice_giant || orbiter->type == hot_jupiter || orbiter->type == star);
    double constant = is_fluid ? 2.44 : std::cbrt(2.0);

    return constant * volumetric_mean_radius * std::cbrt(primary_density / orbiter_density);
}

bool append_cel(CelestialObject *cel)
{
    if (ncelobjs >= MAX_CELOBJS-1)
    {
        static bool complained = false;
        if (!complained)
        {
            std::cerr << "Object limit (" << MAX_CELOBJS << ") reached; no more can be added." << std::endl;
            complained = true;
        }
        return false;
    }

    cel->origname = cel->name;
    if (cel->orbit && cel->orbit->center) cel->origcenname = cel->orbit->center->name;

    if (first_sat < 0 && cel->typeclass() == class_satellite) first_sat = ncelobjs;

    cels[ncelobjs] = cel;
    cel->seqno = ncelobjs;
    ncelobjs++;
    cels[ncelobjs] = 0;
    return true;
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

alienorum::Locale::Locale(json fj)
{
    // Not using a try block because if the JSON is not valid, we want to prevent object creation.
    fj["name"].get_to(name);
    fj["latitude"].get_to(lat);
    fj["longitude"].get_to(lon);

    // Optional parameters do get a try block.
    try
    {
        std::string tzstr;
        fj["timezone"].get_to(tzstr);
        const char* tzcstr = tzstr.c_str();
        const int l = strlen(tzcstr);

        const char* plusminus = strchr(tzcstr, '+');
        if (!plusminus) plusminus = strchr(tzcstr, '-');
        if (!plusminus) throw 0xbadda7a;

        int sign = (*plusminus == '-') ? -1 : 1;

        int hours = atoi(&plusminus[1]);
        const char* colon = strchr(plusminus, ':');
        if (!colon) throw 0xbadda7a;

        int minutes = atoi(&colon[1]);

        tz = (60 * minutes + 3600 * hours) * sign;
    }
    catch (...)
    {
        ;
    }
}

void alienorum::AtmosphereComposition::enforce_integrity()
{
    double total = H2_portion + He_portion + N2_portion + O2_portion + O3_portion
        + CO2_portion + CH4_portion + SO2_portion + H2O_portion + H2S_portion
        + HCN_portion + NH3_portion + C2H6_portion + N2O_portion
        + CO_portion + Ar_portion;
    
    if (total > 1)
    {
        double multiplier = 1.0 / total;
        H2_portion *= multiplier;
        He_portion *= multiplier;
        N2_portion *= multiplier;
        O2_portion *= multiplier;
        O3_portion *= multiplier;
        CO2_portion *= multiplier;
        CH4_portion *= multiplier;
        SO2_portion *= multiplier;
        H2O_portion *= multiplier;
        H2S_portion *= multiplier;
        HCN_portion *= multiplier;
        NH3_portion *= multiplier;
        C2H6_portion *= multiplier;
        N2O_portion *= multiplier;
        CO_portion *= multiplier;
        Ar_portion *= multiplier;
    }
}

double alienorum::AtmosphereComposition::mean_molar_mass()
{
    const double earth_air = 0.0289644;
    double total = H2_portion + He_portion + N2_portion + O2_portion + O3_portion
        + CO2_portion + CH4_portion + SO2_portion + H2O_portion + H2S_portion
        + HCN_portion + NH3_portion + C2H6_portion + N2O_portion
        + CO_portion + Ar_portion;
    if (total <= 0) return earth_air;

    double sum = H2_portion*0.002016 + He_portion*0.004003 + N2_portion*0.028014
        + O2_portion*0.031998 + O3_portion*0.047997 + CO2_portion*0.044009
        + CH4_portion*0.016043 + SO2_portion*0.064064 + H2O_portion*0.018015
        + H2S_portion*0.034081 + HCN_portion*0.027025 + NH3_portion*0.017031
        + C2H6_portion*0.030069 + N2O_portion*0.044013 + CO_portion*0.028010
        + Ar_portion*0.039948;

    return sum / total;
}

void alienorum::AtmosphereComposition::generate_fictitious_gas_giant()
{
    double leftover = 1;
    He_portion = cel->cel_frand(0.01, 0.2);
    leftover -= He_portion;
    CH4_portion = cel->cel_frand(0.002, 0.005);
    leftover -= CH4_portion;
    NH3_portion = cel->cel_frand(0.0001, 0.0003);
    leftover -= NH3_portion;
    C2H6_portion = cel->cel_frand(0.000005, 0.000008);
    leftover -= C2H6_portion;
    H2O_portion = cel->cel_frand(0, 0.000005);
    leftover -= H2O_portion;
    H2_portion = leftover;

    enforce_integrity();
}

void alienorum::AtmosphereComposition::generate_fictitious_ice_giant()
{
    double leftover = 1;
    He_portion = cel->cel_frand(0.1, 0.3);
    leftover -= He_portion;
    CH4_portion = cel->cel_frand(0.001, 0.003);
    leftover -= CH4_portion;
    NH3_portion = cel->cel_frand(0.0001, 0.0003);
    leftover -= NH3_portion;
    C2H6_portion = cel->cel_frand(0.000005, 0.000008);
    leftover -= C2H6_portion;
    H2O_portion = cel->cel_frand(0, 0.000005);
    leftover -= H2O_portion;
    H2_portion = cel->cel_frand(0.7, 0.85);

    enforce_integrity();
}

void alienorum::AtmosphereComposition::generate_fictitious_venusian()
{
    double leftover = 1;
    leftover -= (N2_portion = cel->cel_frand(0.01, 0.1));
    leftover -= (SO2_portion = cel->cel_frand(0.0001, 0.001));
    leftover -= (Ar_portion = cel->cel_frand(0.00001, 0.0001));
    leftover -= (H2O_portion = cel->cel_frand(0.00001, 0.0001));
    leftover -= (H2S_portion = cel->cel_frand(0.000001, 0.00001));
    leftover -= (CO_portion = cel->cel_frand(0.00001, 0.00003));
    leftover -= (He_portion = cel->cel_frand(0.00001, 0.00002));
    CO2_portion = leftover;

    enforce_integrity();
}

void alienorum::AtmosphereComposition::generate_fictitious_titanean()
{
    double leftover = 1;
    leftover -= (CH4_portion = cel->cel_frand(0.01, 0.1));
    leftover -= (H2_portion = cel->cel_frand(0.001, 0.005));
    leftover -= (C2H6_portion = cel->cel_frand(0.00001, 0.01));
    N2_portion = leftover;

    enforce_integrity();
}

void alienorum::AtmosphereComposition::generate_fictitious_habitable()
{
    bool has_intense_volcanism = cel->cel_frand(0,1) < 0.4;
    bool has_free_oxygen = cel->cel_frand(0,1) < 0.03;           // yes I am an oxygen pessimist

    double leftover = 1;
    leftover -= (CH4_portion = cel->cel_frand(0, 0.0005));
    leftover -= (C2H6_portion = CH4_portion * cel->cel_frand(0.000001, 0.1));
    leftover -= (HCN_portion = cel->cel_frand(0, 0.001));
    leftover -= (NH3_portion = cel->cel_frand(0, 0.00001));
    leftover -= (Ar_portion = cel->cel_frand(0.00001, 0.005));
    leftover -= (CO2_portion = cel->cel_frand(0.00001, 0.01));
    leftover -= (CO_portion = CO2_portion * cel->cel_frand(0.00001, 0.01));
    leftover -= (H2O_portion = cel->cel_frand(0.001, 0.015));

    if (has_intense_volcanism)
    {
        leftover -= (SO2_portion = cel->cel_frand(0.0001, 0.001));
        leftover -= (H2S_portion = SO2_portion * cel->cel_frand(0.01, 0.5));
    }

    if (has_free_oxygen)
    {
        leftover -= (O2_portion = cel->cel_frand(0.0001, 0.7));
        leftover -= (O3_portion = O2_portion * cel->cel_frand(0.0001, 0.01));
        leftover -= (N2O_portion = O2_portion * cel->cel_frand(0.000001, 0.0001));
    }

    leftover -= (H2_portion = cel->cel_frand(0.001, 0.005));
    N2_portion = leftover;

    enforce_integrity();
}

void alienorum::AtmosphereComposition::generate_fictitious_for_planet(cel_obj_type t)
{
    if (t == gas_giant) generate_fictitious_gas_giant();
    else if (t == ice_giant) generate_fictitious_ice_giant();
    else if (t == rocky) generate_fictitious_venusian();
    else if (t == icy) generate_fictitious_titanean();
}
