
#include <math.h>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <memory>
#include <limits>
#include <charconv>
#include "celestial.h"
#include "shore.h"
#include "lavafissures.h"
#include "wdcolor.h"
#include "../loaders.h"

using namespace alienorum;

CelestialObject **cels, *mycenobj = nullptr;
std::vector<std::vector<CelestialObject*>> first_letter_index;
std::map<std::string,std::vector<CelestialObject*>> constellation_index;

std::vector<CelestialObject*> lsyscache;

bool *celskip, *discinstead;
double *vmag_cache, *bloomrad_cache, *angular_radius;
CelestialLocation here;
double azimuth_correction = 0, bigcel_sz = 0;
typedef struct my_jpeg_error_mgr * my_error_ptr;
Locale *is_a_locale_under_cursor = nullptr, *selected_locale = nullptr;
CelestialObject *biggest_cel = nullptr;

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

double alienorum::CelestialObject::stellar_day()
{
    if (!sidereal_rotational_period || !orbit || !orbit->period) 
        return sidereal_rotational_period;
    bool is_retrograde = (obliquity > _pi);
    double adjustment = is_retrograde ? (1.0 + (sidereal_rotational_period / orbit->period))
                                      : (1.0 - (sidereal_rotational_period / orbit->period));
    if (adjustment == 0.0) return sidereal_rotational_period;               // if tidal locked
    return sidereal_rotational_period / adjustment;
}

CelestialObject::CelestialObject()
{
    memset(name, 0, 32*sizeof(char));
}

CelestialObject::~CelestialObject()
{
    if (merged_day_map)
    {
        delete merged_day_map;
        merged_day_map = nullptr;
    }
    if (merged_night_map)
    {
        delete merged_night_map;
        merged_night_map = nullptr;
    }

    Map *maps[5] = { surf_map, cloud_map, night_map, ring_map, ringx_map };
    for (int m = 0; m < 5; m++)
    {
        if (!maps[m])
        {
            continue;
        }
        for (int n = m + 1; n < 5; n++)
        {
            if (maps[n] == maps[m])
            {
                maps[n] = nullptr;
            }
        }
        delete maps[m];
    }
    surf_map = nullptr;
    cloud_map = nullptr;
    night_map = nullptr;
    ring_map = nullptr;
    ringx_map = nullptr;
}

Map* CelestialObject::get_day_map()
{
    if (show_clouds && transparent_clouds && surf_map)
    {
        if (cloud_map && cloud_map->is_complete())
        {
            if (!merged_day_map || merged_day_cloud_gen != cloud_map->gen || merged_day_surf_gen != surf_map->gen)
            {
                if (!merged_day_map)
                {
                    merged_day_map = new Map(this);
                }
                if (!merged_day_map->create_merged_day(surf_map, cloud_map))
                {
                    return surf_map;
                }
                merged_day_cloud_gen = cloud_map->gen;
                merged_day_surf_gen = surf_map->gen;
            }
            return merged_day_map;
        }
        else
        {
            return surf_map;
        }
    }

    if (!show_clouds)
    {
        return surf_map ? surf_map : cloud_map;
    }

    if (transparent_clouds && surf_map && (!cloud_map || !cloud_map->is_complete()))
    {
        return surf_map;
    }

    return cloud_map ? cloud_map : surf_map;
}

Map* CelestialObject::get_night_map()
{
    if (show_clouds && transparent_clouds && cloud_map && cloud_map->is_complete() && (night_map || uses_rocky_map(type)))
    {
        if (!merged_night_map || merged_night_cloud_gen != cloud_map->gen || (night_map && merged_night_map_gen != night_map->gen))
        {
            if (!merged_night_map)
            {
                merged_night_map = new Map(this);
            }
            if (!merged_night_map->create_merged_night(night_map, cloud_map))
            {
                return night_map;
            }
            merged_night_cloud_gen = cloud_map->gen;
            merged_night_map_gen = night_map ? night_map->gen : 0;
        }
        return merged_night_map;
    }
    return night_map;
}

double alienorum::CelestialObject::get_horizon_angle()
{
    double d = tmprel.magnitude();
    return acos(std::clamp(get_equatorial_radius() / fmax(d, 1e-29), 0.0, 1.0));
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

double alienorum::CelestialObject::local_solar_time(double lon)
{
    CelestialObject *lcen = get_light_center();
    if (!lcen || lcen == this)
    {
        return -1.0;
    }

    Point star_rel = (lcen->location.galactic_center - location.galactic_center) * light_year * 1e+6
        + (lcen->location.system_center - location.system_center)
        + (lcen->location.local_position - location.local_position);

    if (star_rel.squared_magnitude() == 0)
    {
        return -1.0;
    }

    Point star_eq = rotate3D(star_rel, center, location.equatorial_plane.v, location.equatorial_plane.a);
    Point star_body = rotate3D(star_eq, center, yaxis, timeofday());
    double star_lon = find_angle(star_body.z, -star_body.x);
    double H = lon - star_lon;
    double solar_angle = std::fmod(H + _pi, _pi * 2);
    if (solar_angle < 0)
    {
        solar_angle += _pi * 2;
    }

    return solar_angle;
}

const Locale* CelestialObject::nearest_locale(double latitude, double longitude, double max_distance) const
{
    // Viewer coordinates are radians; locale coordinates are stored in degrees.
    if (!locales || !(volumetric_mean_radius > 0) || !std::isfinite(volumetric_mean_radius)
        || !std::isfinite(latitude) || !std::isfinite(longitude) || fabs(latitude) > _pi / 2
        || !std::isfinite(max_distance) || max_distance < 0) return nullptr;
    const Locale* closest = nullptr;
    double closest_distance = max_distance;
    for (int i = 0; i < nlocales; ++i)
    {
        const Locale& locale = locales[i];
        if (locale.name.empty() || !std::isfinite(locale.lat) || !std::isfinite(locale.lon) || fabs(locale.lat) > 90) continue;
        const double lat = locale.lat * fiftyseventh;
        const double dlat = (lat - latitude) / 2;
        const double dlon = (locale.lon * fiftyseventh - longitude) / 2;
        const double haversine = sin(dlat) * sin(dlat) + cos(latitude) * cos(lat) * sin(dlon) * sin(dlon);
        const double angle = 2 * asin(sqrt(std::clamp(haversine, 0.0, 1.0)));
        const double distance = volumetric_mean_radius * angle;
        if (distance <= closest_distance)
        {
            closest = &locale;
            closest_distance = distance;
        }
    }
    return closest;
}

int alienorum::CelestialObject::read_locales(std::string fn)
{
    locales_loaded = true;
    bool upside_down = map_inverted;
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
                    for (int i=0; i<nlocales; i++)
                    {
                        locales[i].lat = -locales[i].lat;
                        locales[i].lon = std::remainder(-locales[i].lon, 360.0);
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
    locales_loaded = true;
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
    const auto parent = [](CelestialObject* object) -> CelestialObject*
    {
        if (!object || object->typeclass() == class_star || !object->orbit) return nullptr;
        return object->orbit->center;
    };
    CelestialObject* object = this;
    CelestialObject* fast = this;
    while (object)
    {
        if (object->typeclass() == class_star) return object;
        object = parent(object);
        fast = parent(parent(fast));
        if (object && object == fast) return nullptr;
    }
    return nullptr;
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

    delete[] osculating;
    osculating = OsculatingElement::read_from_file(filename, &num_osc);
    #ifdef DEBUG
    std::cout << "Read " << num_osc << " osculating ephemerides for " << cel_name << std::endl << std::flush;
    #endif
    return (num_osc > 0);
}

void alienorum::Orbit::interpolate_osculating_e(double for_epoch, double &n, double &i, double &w, double &a, double &e, double &m, double &p, double& precn, double& procarg, double& effe)
{
    long int h = num_osc - 1;
    if (num_osc < 2 || for_epoch < osculating[0].epoch || for_epoch > osculating[h].epoch)
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

    // Find a bracketing pair without reading beyond the final sample.
    auto upper = std::upper_bound(osculating, osculating + num_osc, for_epoch,
        [](double epoch, const OsculatingElement& sample) { return epoch < sample.epoch; });
    long int j = upper == osculating + num_osc ? num_osc - 2 : upper - osculating - 1;
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
    if (!period) return;
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
    return radians_to_hms(right_ascension - seen_equinox);
}

std::string CelestialObject::Decl_as_degms()
{
    return radians_to_degms(declination);
}

std::string CelestialObject::RA_as_hms(CelestialLocation seen_from, double seen_equinox)
{
    return radians_to_hms(RA_as_radians(seen_from, seen_equinox));
}

std::string CelestialObject::Decl_as_degms(CelestialLocation seen_from)
{
    return radians_to_degms(Decl_as_radians(seen_from));
}

void CelestialObject::RA_from_hms(std::string ra_hms)
{
    const char* c = ra_hms.c_str();
    int i, j=0;
    // Zero, so that a string missing a field -- "12:34", or an empty edit box -- reads as zero
    // rather than as whatever the stack was holding.
    double h = 0, m = 0, s = 0;

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
    // As above, and the sign with them: a declination written without one -- which is how a
    // northern declination is usually typed -- never assigned it at all.
    int i, j=0, sign = 1;
    double d = 0, m = 0, s = 0;

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
    bool airy_rock = view_mode == vm_horizon && whereami > 0 && uses_rocky_map(cels[whereami]->type) && ((Planet*)cels[whereami])->get_surface_pressure();
    if (!airy_rock) return Decl_as_radians(seen_from);
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

    if (r >= light_year * 1e9)
    {
        dispr = (r / light_year) * 1e-9;
        units = " Gly";
    }
    else if (r >= light_year * 1e6)
    {
        dispr = (r / light_year) * 1e-6;
        units = " Mly";
    }
    else if (r >= light_year)
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
    oss << std::fixed << std::setprecision(5) << dispr << units;
    return oss.str();
}

void alienorum::CelestialObject::randomize()
{
    if (!rnd_seed)
    {
        // Reduce in floating point before conversion, including negative or large inputs.
        auto low_bits = [](double value) -> uint32_t
        {
            if (!std::isfinite(value)) return 0;
            double reduced = std::fmod(std::trunc(value), 4294967296.0);
            if (reduced < 0) reduced += 4294967296.0;
            return static_cast<uint32_t>(reduced);
        };
        auto positive_log = [](double value) { return value > 0 && std::isfinite(value) ? std::log(value) : 0.0; };
        uint32_t hash = low_bits(0xb0ad * positive_log(mass) + 0x1cea * positive_log(volumetric_mean_radius));
        if (orbit)
        {
            hash = low_bits(static_cast<double>(hash) + 0xeb00dae * positive_log(orbit->semimajor_axis)
                + (std::isfinite(orbit->arg_periapsis) ? 0xefac00ee * orbit->arg_periapsis : 0.0));
        }
        rnd_seed = hash;
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
    towrite["home_world"] = home_world;
    towrite["map_inverted"] = map_inverted;
    towrite["rnd_seed"] = rnd_seed;
    towrite["fictitious_map_height"] = fictitious_map_height;
    towrite["orbit_center_codes"] = orbit_center_codes;
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
    // Written as the period in years, which is how the object editor states it and how a person
    // thinks of it -- 25,772 for Earth. Zero is not a period but the absence of one, and dividing
    // by it produced an infinity that nlohmann writes out as a bare null; the guard is the same
    // one dialogs.cpp and cat.cpp already put on this conversion.
    towrite["precession"] = precession ? (_pi * 2 / precession / oneyear) : 0;
    towrite["RI_color"] = RI_color;
    towrite["right_ascension"] = right_ascension * fiftyseven;
    towrite["sidereal_rotational_period"] = sidereal_rotational_period / oneday;
    // Kelvins. Written only when there is one, because zero is what the derivations that fill
    // this in -- Star::estimate_temperature(), Planet::equilibrium_temperature() -- read as
    // "nobody has established a temperature for this body, work one out". Saving a zero would
    // say the same thing, but saving nothing keeps it out of the files of bodies that have none.
    if (temperature) towrite["temperature"] = temperature;
    towrite["transparent_clouds"] = transparent_clouds;
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
    home_world = j.value("home_world", false);
    map_inverted = j.value("map_inverted", false);
    rnd_seed = j.value("rnd_seed", 0);
    fictitious_map_height = j.value("fictitious_map_height", 768u);
    orbit_center_codes = j.value("orbit_center_codes", std::vector<std::string>{});
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

    try
    {
        j.at("precession").get_to(precession);
        precession = precession ? (_pi * 2 / (precession * oneyear)) : 0;
    } catch (...) { ; }
    try { j.at("RI_color").get_to(RI_color); } catch (...) { ; }
    try { j.at("right_ascension").get_to(right_ascension); right_ascension *= fiftyseventh; } catch (...) { ; }
    try { j.at("sidereal_rotational_period").get_to(sidereal_rotational_period); sidereal_rotational_period *= oneday; } catch (...) { ; }
    // A stated temperature stands: the derivations above return it unchanged rather than working
    // one out, which is the point of saving it -- a body whose temperature was measured, edited by
    // hand, or arrived at through a chain this session will not repeat comes back with it intact.
    try { j.at("temperature").get_to(temperature); } catch (...) { ; }
    try { j.at("transparent_clouds").get_to(transparent_clouds); } catch (...) { ; }
    try { j.at("TransparentClouds").get_to(transparent_clouds); } catch (...) { ; }
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

void alienorum::CelestialObject::mark_all_maps_for_regen()
{
    looked_for_maps = false;
    ignore_map_files = true;
    merged_day_cloud_gen = merged_day_surf_gen = merged_night_cloud_gen = merged_night_map_gen = 0;
    if (surf_map)
    {
        surf_map->mark_for_map_regen(this);
    }
    if (cloud_map)
    {
        cloud_map->mark_for_map_regen(this);
    }
    if (night_map)
    {
        if (type != lavaworld && !has_real_maps && typeclass() != class_star)
        {
            delete night_map;
            night_map = nullptr;
        }
        else
        {
            night_map->mark_for_map_regen(this);
        }
    }
    if (merged_day_map)
    {
        delete merged_day_map;
        merged_day_map = nullptr;
    }
    if (merged_night_map)
    {
        delete merged_night_map;
        merged_night_map = nullptr;
    }
}

bool alienorum::CelestialObject::operator<(const CelestialObject &other) const          // Return true if this < other.
{
    // Is same cenobj? If not, return cenobj comparison.
    if (cenobj != other.cenobj) return *cenobj < *(other.cenobj);

    // Check class. Galaxy > star > planet/asteroid > moon > comet > artificial, reversed because we use this function for sort order.
    const cel_obj_class cls = typeclass(), ocls = other.typeclass();
    if (cls != ocls) return cls < ocls;

    // Host star gets priority.
    if (!orbit && other.orbit) return true;
    if (orbit && !other.orbit) return false;

    // If same class, compare centers of orbit.
    const CelestialObject *orbcen = (orbit ? orbit->center : this), *oorbcen = (other.orbit ? other.orbit->center : &other);
    if (orbcen != oorbcen) return *orbcen < *oorbcen;

    // If same center of orbit, compare semimajor axes.
    const double sma = (orbit ? orbit->semimajor_axis : 0), osma = (other.orbit ? other.orbit->semimajor_axis : 0);
    if (fabs(sma - osma) > fmax(sma, osma)*1e-29) return sma < osma;

    // If somehow still same semimajor axes, compare masses first, then radii, then names.
    if (fabs(mass - other.mass) > fmax(mass, other.mass)*1e-29) return mass < other.mass;
    if (fabs(volumetric_mean_radius - other.volumetric_mean_radius) > fmax(volumetric_mean_radius, other.volumetric_mean_radius)*1e-29)
        return volumetric_mean_radius < other.volumetric_mean_radius;
    int namecmp = strcmp(name, other.name);
    return namecmp < 0;
}

void CelestialObject::update_orbit_location(double tmnow, Rotation* crp)
{
    if (!orbit || !orbit->center) return;
    if ((deleted = orbit->center->deleted)) return;           // assignment not comparison.
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

    if (typeclass() != class_star || (!orbit->heliocentric_inclination && !orbit->heliocentric_node))
    {
        Point orbit_pole = yaxis;
        orbit_pole = rotate3D(orbit_pole, center, Point(sinO, 0, -cosO), I);
        if (crp)
        {
            orbit_pole = rotate3D(orbit_pole, center, crp->v, -crp->a);
        }
        else
        {
            orbit_pole = rotate3D(orbit_pole, center, location.local_system_plane.v, -location.local_system_plane.a);
        }
        location.orbital_plane = align_points_3d(orbit_pole, yaxis, center);
    }
    else if (!location.orbital_plane.a && !location.orbital_plane.v.magnitude())
    {
        location.orbital_plane = location.local_system_plane;
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
        {"heliocentric_inclination", heliocentric_inclination*fiftyseven},
        {"heliocentric_node", heliocentric_node*fiftyseven},
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
    heliocentric_inclination = j.value("heliocentric_inclination", 0.0) * fiftyseventh;
    heliocentric_node = j.value("heliocentric_node", 0.0) * fiftyseventh;
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

namespace
{
    // Bilinear-resamples an equirectangular grid of bump/elevation values from one resolution to
    // another: longitude (x) wraps around, latitude (y) clamps at the poles rather than wrapping.
    // Used when a bump map file's resolution doesn't match its surface map's, so the mismatched
    // file can still be used (scaled to fit) instead of being rejected outright.
    void resample_equirect(const std::vector<double> &src, unsigned long src_w, unsigned long src_h,
        double *dst, unsigned long dst_w, unsigned long dst_h)
    {
        for (unsigned long y = 0; y < dst_h; y++)
        {
            double sy = (dst_h > 1) ? ((double)y * (src_h - 1) / (double)(dst_h - 1)) : 0.0;
            unsigned long y0 = (unsigned long)sy;
            unsigned long y1 = std::min(y0 + 1, src_h - 1);
            double fy = sy - y0;

            for (unsigned long x = 0; x < dst_w; x++)
            {
                double sx = (double)x * src_w / (double)dst_w;
                unsigned long x0 = (unsigned long)sx % src_w;
                unsigned long x1 = (x0 + 1) % src_w;
                double fx = sx - (unsigned long)sx;

                double v00 = src[y0*src_w + x0], v01 = src[y0*src_w + x1];
                double v10 = src[y1*src_w + x0], v11 = src[y1*src_w + x1];
                double v0 = v00 + (v01 - v00) * fx;
                double v1 = v10 + (v11 - v10) * fx;
                dst[y*dst_w + x] = v0 + (v1 - v0) * fy;
            }
        }
    }
}

bool Map::load_from_jpeg(std::string filename, bool as_bump, double bump_scale)
{
    if (!as_bump)
    {
        surface_materials.clear();
        procedurally_generated = false;
    }
    std::cout << "Loading " << filename << " as " << (as_bump ? "texture" : "bump") << "..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
    mtx.lock();
    struct jpeg_decompress_struct cinfo;
    struct my_jpeg_error_mgr jerr;
    FILE * infile;
    unsigned int row_stride;

    bool upside_down = mcel && mcel->map_inverted;

    bool bump_resample = false;
    unsigned long bump_src_w = 0, bump_src_h = 0;
    std::vector<double> bump_src_data;

    try
    {
        if ((infile = fopen(filename.c_str(), "rb")) == NULL)
        {
            fprintf(stderr, "can't open %s\n", filename.c_str());
            mtx.unlock();
            return false;
        }

        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = alienorum_jpeg_error_exit;

        if (setjmp(jerr.setjmp_buffer))
        {
            jpeg_destroy_decompress(&cinfo);
            fclose(infile);
            mtx.unlock();
            return false;
        }

        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, infile);
        (void) jpeg_read_header(&cinfo, TRUE);
        (void) jpeg_start_decompress(&cinfo);

        if (as_bump)
        {
            bump_src_w = cinfo.image_width;
            bump_src_h = cinfo.image_height;
            bump_resample = (image_height != bump_src_h || image_width != bump_src_w);
            if (bump_resample)
            {
                std::cout << "Resampling bump map (" << bump_src_w << "x" << bump_src_h
                    << ") to match surface map (" << image_width << "x" << image_height << ")." << std::endl;
                bump_src_data.resize((size_t)bump_src_w * bump_src_h);
            }
            else
            {
                bump_data = new double[allocated];
            }
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
    }
    catch (...)
    {
        mtx.unlock();
        return false;
    }
    mtx.unlock();

    row_stride = cinfo.output_width * cinfo.output_components;
    jpeg_image_buffer = (*cinfo.mem->alloc_sarray)
            ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    // While a bump map is being resampled, indices below run over its own (mismatched)
    // resolution rather than the surface map's -- bump_src_data is sized for that geometry, and
    // gets remapped into bump_data (sized for the surface map) only after the full image is in.
    unsigned long bump_line_width = bump_resample ? bump_src_w : image_width;
    unsigned long bump_limit = bump_resample ? (bump_src_w * bump_src_h) : allocated;

    unsigned int i, j, i0, j0;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        j0 = cinfo.output_scanline * (as_bump ? bump_line_width : image_width);
        (void) jpeg_read_scanlines(&cinfo, jpeg_image_buffer, 1);
        i0 = 0;
        for (i=0; i<row_stride; i+=cinfo.output_components)
        {
            j = j0 + i0;
            unsigned long limit = as_bump ? bump_limit : allocated;
            if (j >= limit) break;
            if (upside_down) j = limit-1-j;

            if (as_bump)
            {
                // Allow false color bump maps using the visual luminance as the elevation for better granularity
                double lum = (cinfo.output_components >= 3)
                    ? (   0.001137 * jpeg_image_buffer[0][i]
                        + 0.002196 * jpeg_image_buffer[0][i+1]
                        + 0.000588 * jpeg_image_buffer[0][i+2])
                    : (   0.003921 * jpeg_image_buffer[0][i]);   // 1/255, matching the RGB weights' sum
                double val = bump_scale * (lum - 0.5);
                if (bump_resample) bump_src_data[j] = val;
                else bump_data[j] = val;
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

    if (as_bump && bump_resample)
    {
        bump_data = new double[allocated];
        resample_equirect(bump_src_data, bump_src_w, bump_src_h, bump_data, image_width, image_height);
    }

    if (!as_bump) touch_gen();
    return true;
}

bool Map::load_from_png(std::string filename, bool as_bump, double bump_scale)
{
    if (!as_bump)
    {
        surface_materials.clear();
        procedurally_generated = false;
    }
    std::cout << "Loading " << filename << " as " << (as_bump ? "texture" : "bump") << "..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
    mtx.lock();
    png_structp png_ptr;
    png_infop info_ptr;
    FILE *fp;

    bool upside_down = mcel && mcel->map_inverted;

    unsigned long bump_src_w = 0, bump_src_h = 0;
    bool bump_resample = false;
    std::vector<double> bump_src_data;

    try
    {    
        if ((fp = fopen(filename.c_str(), "rb")) == NULL)
        {
            mtx.unlock();
            return false;
        }

        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
            nullptr, nullptr, nullptr);

        if (png_ptr == NULL)
        {
            fclose(fp);
            mtx.unlock();
            return false;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL)
        {
            fclose(fp);
            png_destroy_read_struct(&png_ptr, NULL, NULL);
            mtx.unlock();
            return false;
        }

        if (setjmp(png_jmpbuf(png_ptr)))
        {
            /* Free all of the memory associated with the png_ptr and info_ptr */
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            fclose(fp);
            /* If we get here, we had a problem reading the file */
            mtx.unlock();
            return false;
        }

        png_init_io(png_ptr, fp);
        png_read_png(png_ptr, info_ptr, 0, NULL);

        if (as_bump)
        {
            bump_src_w = png_get_image_width(png_ptr, info_ptr);
            bump_src_h = png_get_image_height(png_ptr, info_ptr);
            bump_resample = (image_height != bump_src_h || image_width != bump_src_w);
            if (bump_resample)
            {
                std::cout << "Resampling bump map (" << bump_src_w << "x" << bump_src_h
                    << ") to match surface map (" << image_width << "x" << image_height << ")." << std::endl;
                bump_src_data.resize((size_t)bump_src_w * bump_src_h);
            }
            else
            {
                bump_data = new double[allocated];
            }
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
    }
    catch (...)
    {
        mtx.unlock();
        return false;
    }
    mtx.unlock();

    int bytes_per_pixel = png_get_channels(png_ptr, info_ptr) * (png_get_bit_depth(png_ptr, info_ptr) / 8);

    png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

    // Generated maps preserve categorical biomes in compressed PNG metadata.
    // Ordinary imported textures have no labels and retain the color fallback.
    if (!as_bump)
    {
        png_textp entries = nullptr;
        const int count = png_get_text(png_ptr, info_ptr, &entries, nullptr);
        for (int entry = 0; entry < count; entry++)
        {
            const png_text &text = entries[entry];
            if (text.key && text.text && std::strcmp(text.key, "Alienorum Procedural Texture v1") == 0)
            {
                procedurally_generated = std::strcmp(text.text, "1") == 0;
                continue;
            }
            if (std::strcmp(text.key, "Alienorum Surface Materials v1") != 0
                || !text.text) continue;
            std::vector<SurfaceMaterial> decoded(allocated);
            bool valid = true;
            size_t total = 0;
            const char *cursor = text.text, *end = text.text + text.text_length;
            while (cursor < end)
            {
                size_t run = 0;
                const auto number = std::from_chars(cursor, end, run);
                cursor = number.ptr;
                if (number.ec != std::errc() || end - cursor < 3 || cursor[0] != ':'
                    || cursor[1] < '0' || cursor[1] > '3' || cursor[2] != ';'
                    || run == 0 || run > allocated - total)
                {
                    valid = false;
                    break;
                }
                std::fill(decoded.begin() + total, decoded.begin() + total + run,
                    static_cast<SurfaceMaterial>(cursor[1] - '0'));
                total += run;
                cursor += 3;
            }
            if (!valid || total != allocated) continue;
            if (upside_down) std::reverse(decoded.begin(), decoded.end());
            surface_materials = std::move(decoded);
            // Continue so independent provenance metadata is read as well.
        }
    }

    // As with load_from_jpeg: while a bump map is being resampled, iteration runs over its own
    // (mismatched) resolution rather than the surface map's, into bump_src_data, which gets
    // remapped into bump_data (sized for the surface map) only after the full image is in.
    unsigned long iter_w = as_bump ? bump_src_w : image_width;
    unsigned long iter_h = as_bump ? bump_src_h : image_height;

    if (bytes_per_pixel == 3 || bytes_per_pixel == 4)
    {
        // RGB or RGBA
        unsigned long x, y, i=0;
        for (y=0; y<iter_h; y++)
        {
            for (x=0; x<iter_w; x++)
            {
                png_bytep pixel;
                if (upside_down) pixel = &(row_pointers[iter_h-1-y][(iter_w-1-x) * bytes_per_pixel]);
                else pixel = &(row_pointers[y][x * bytes_per_pixel]);

                if (as_bump)
                {
                    // Allow false color bump maps using the visual luminance as the elevation for better granularity
                    double val = bump_scale *
                            (( 0.001137 * pixel[0]
                             + 0.002196 * pixel[1]
                             + 0.000588 * pixel[2])
                             - 0.5);
                    if (bump_resample) bump_src_data[i] = val;
                    else bump_data[i] = val;
                }
                else
                {
                    red_data[i] = pixel[0];
                    green_data[i] = pixel[1];
                    blue_data[i] = pixel[2];
                }
                i++;
            }
        }
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
    }
    else if (bytes_per_pixel == 1)
    {
        // Grayscale.
        unsigned long x, y, i=0;
        for (y=0; y<iter_h; y++)
        {
            for (x=0; x<iter_w; x++)
            {
                png_bytep pixel;
                if (upside_down) pixel = &(row_pointers[iter_h-1-y][(iter_w-1-x) * bytes_per_pixel]);
                else pixel = &(row_pointers[y][x * bytes_per_pixel]);

                if (as_bump)
                {
                    double val = bump_scale * (pixel[0] / 255.0 - 0.5);
                    if (bump_resample) bump_src_data[i] = val;
                    else bump_data[i] = val;
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

    if (as_bump && bump_resample)
    {
        bump_data = new double[allocated];
        resample_equirect(bump_src_data, bump_src_w, bump_src_h, bump_data, image_width, image_height);
    }

    if (!as_bump) touch_gen();
    return true;
}

bool Map::save_to_png(std::string filename, bool as_bump)
{
    if (!image_width || !image_height || image_width > PNG_UINT_31_MAX || image_height > PNG_UINT_31_MAX
        || image_width > std::numeric_limits<size_t>::max() / image_height
        || image_width * image_height > std::numeric_limits<size_t>::max() / 3
        || allocated < image_width * image_height
        || (as_bump ? !bump_data : (!red_data || !green_data || !blue_data)))
    {
        std::cerr << "Error: no map data to save." << std::endl;
        return false;
    }

    // Construct storage before setjmp so libpng errors return through normal C++ cleanup.
    std::vector<png_byte> buffer;
    std::vector<png_bytep> row_pointers;
    std::string material_text;
    try
    {
        buffer.resize(image_width * image_height * 3);
        row_pointers.resize(image_height);
        if (!as_bump && surface_materials.size() == image_width * image_height)
        {
            // Run lengths keep high-resolution biome metadata compact before zTXt compression.
            for (size_t index = 0; index < surface_materials.size();)
            {
                size_t end = index + 1;
                while (end < surface_materials.size() && surface_materials[end] == surface_materials[index]) end++;
                material_text += std::to_string(end - index) + ':'
                    + static_cast<char>('0' + static_cast<unsigned char>(surface_materials[index])) + ';';
                index = end;
            }
        }
    }
    catch (const std::exception&)
    {
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
        PNG_COLOR_TYPE_RGB,         // RGB3 channels (3 bytes per pixel)
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    double bump_scale = 1;
    double bump_max = -1e209, bump_min = 1e209;
    if (as_bump)
    {
        for (unsigned long i=0; i<allocated; i++)
        {
            if (!std::isfinite(bump_data[i])) continue;
            if (bump_data[i] > bump_max) bump_max = bump_data[i];
            if (bump_data[i] < bump_min) bump_min = bump_data[i];
        }

        bump_scale = bump_max > bump_min ? 255.0 / (bump_max - bump_min) : 0;
    }

    // Populate the buffer with data.
    size_t row_stride = image_width * 3;               // 3 bytes per pixel for RGB3
    for (unsigned int y = 0; y < image_height; y++)
    {
        size_t iy = image_width*y;
        size_t ry = row_stride*y;
        for (unsigned int x = 0; x < image_width; x++)
        {
            size_t rx = x*3;
            if (as_bump)
            {
                const double value = bump_scale > 0 && std::isfinite(bump_data[iy + x])
                    ? std::clamp((bump_data[iy + x] - bump_min) * bump_scale, 0.0, 255.0) : 128;
                buffer[ry + rx] = buffer[ry + rx + 1] = buffer[ry + rx + 2] = value;
            }
            else
            {
                buffer[ry + rx   ] = red_data[iy + x];
                buffer[ry + rx +1] = green_data[iy + x];
                buffer[ry + rx +2] = blue_data[iy + x];
            }
        }
    }

    // Set up the row pointers to point to the buffer.
    for (unsigned int y = 0; y < image_height; y++)
    {
        row_pointers[y] = &buffer[y * row_stride];
    }

    // Preserve material identity without altering RGB or transparency in image viewers.
    if (!material_text.empty())
    {
        png_text text{};
        text.compression = PNG_TEXT_COMPRESSION_zTXt;
        text.key = const_cast<char*>("Alienorum Surface Materials v1");
        text.text = material_text.data();
        text.text_length = material_text.size();
        png_set_text(png_ptr, info_ptr, &text, 1);
    }

    if (!as_bump && procedurally_generated)
    {
        png_text provenance{};
        provenance.compression = PNG_TEXT_COMPRESSION_NONE;
        provenance.key = const_cast<char*>("Alienorum Procedural Texture v1");
        provenance.text = const_cast<char*>("1");
        provenance.text_length = 1;
        png_set_text(png_ptr, info_ptr, &provenance, 1);
    }

    // Write data to file
    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, row_pointers.data());
    png_write_end(png_ptr, NULL);

    // Clean up resources
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return fclose(fp) == 0;
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

void Map::overlay_cloud_map(Map *clouds)
{
    if (!clouds || !clouds->is_complete() || !has_rgb_data())
    {
        return;
    }

    if (!inv_lat_scale && lat_scale)
    {
        inv_lat_scale = 1.0 / lat_scale;
    }
    if (!inv_lon_scale && lon_scale)
    {
        inv_lon_scale = 1.0 / lon_scale;
    }
    if (!lat_scale && image_height)
    {
        lat_scale = (double)image_height / _pi;
        inv_lat_scale = 1.0 / lat_scale;
    }
    if (!lon_scale && image_width)
    {
        lon_scale = (double)image_width / (_pi * 2);
        inv_lon_scale = 1.0 / lon_scale;
    }

    for (unsigned long y = 0; y < image_height; y++)
    {
        double lat = half_pi - (y + 0.5) * inv_lat_scale;
        for (unsigned long x = 0; x < image_width; x++)
        {
            double lon = (x + 0.5) * inv_lon_scale - _pi;
            RGB3 c_rgb = clouds->color_at(lat, lon);
            double alpha = fmin(1.0, fmax(0.0, c_rgb.luminance() / 255.0));
            if (alpha > 0.0)
            {
                unsigned long idx = y * image_width + x;
                double inv_a = 1.0 - alpha;
                red_data[idx] = (unsigned char)std::clamp((int)(red_data[idx] * inv_a + c_rgb.r), 0, 255);
                green_data[idx] = (unsigned char)std::clamp((int)(green_data[idx] * inv_a + c_rgb.g), 0, 255);
                blue_data[idx] = (unsigned char)std::clamp((int)(blue_data[idx] * inv_a + c_rgb.b), 0, 255);
            }
        }
    }
    touch_gen();
}

void Map::obscure_with_clouds(Map *clouds)
{
    if (!clouds || !clouds->is_complete() || !has_rgb_data())
    {
        return;
    }

    if (!inv_lat_scale && lat_scale)
    {
        inv_lat_scale = 1.0 / lat_scale;
    }
    if (!inv_lon_scale && lon_scale)
    {
        inv_lon_scale = 1.0 / lon_scale;
    }
    if (!lat_scale && image_height)
    {
        lat_scale = (double)image_height / _pi;
        inv_lat_scale = 1.0 / lat_scale;
    }
    if (!lon_scale && image_width)
    {
        lon_scale = (double)image_width / (_pi * 2);
        inv_lon_scale = 1.0 / lon_scale;
    }

    for (unsigned long y = 0; y < image_height; y++)
    {
        double lat = half_pi - (y + 0.5) * inv_lat_scale;
        for (unsigned long x = 0; x < image_width; x++)
        {
            double lon = (x + 0.5) * inv_lon_scale - _pi;
            RGB3 c_rgb = clouds->color_at(lat, lon);
            double alpha = fmin(1.0, fmax(0.0, c_rgb.luminance() / 255.0));
            if (alpha > 0.0)
            {
                unsigned long idx = y * image_width + x;
                double inv_a = 1.0 - alpha;
                red_data[idx]   = (unsigned char)std::clamp((int)(red_data[idx] * inv_a), 0, 255);
                green_data[idx] = (unsigned char)std::clamp((int)(green_data[idx] * inv_a), 0, 255);
                blue_data[idx]  = (unsigned char)std::clamp((int)(blue_data[idx] * inv_a), 0, 255);
            }
        }
    }
    touch_gen();
}

Map::~Map()
{
    if (red_data)
    {
        delete[] red_data;
        red_data = nullptr;
    }
    if (green_data)
    {
        delete[] green_data;
        green_data = nullptr;
    }
    if (blue_data)
    {
        delete[] blue_data;
        blue_data = nullptr;
    }
    if (bump_data)
    {
        delete[] bump_data;
        bump_data = nullptr;
    }
}

bool Map::create_merged_day(Map *surf, Map *clouds)
{
    if (!surf || !surf->has_rgb_data() || !clouds || !clouds->is_complete())
    {
        return false;
    }

    unsigned long w = surf->get_width();
    unsigned long h = surf->get_height();
    unsigned long toalloc = w * h;

    if (allocated != toalloc || !red_data || !green_data || !blue_data)
    {
        if (red_data)
        {
            delete[] red_data;
        }
        if (green_data)
        {
            delete[] green_data;
        }
        if (blue_data)
        {
            delete[] blue_data;
        }
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
    }

    image_width = w;
    image_height = h;
    lat_scale = (double)image_height / _pi;
    lon_scale = (double)image_width / (_pi * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;

    for (unsigned long y = 0; y < image_height; y++)
    {
        double lat = half_pi - (y + 0.5) * inv_lat_scale;
        for (unsigned long x = 0; x < image_width; x++)
        {
            unsigned long idx = y * image_width + x;
            unsigned char sr = surf->red_data[idx];
            unsigned char sg = surf->green_data[idx];
            unsigned char sb = surf->blue_data[idx];

            double lon = (x + 0.5) * inv_lon_scale - _pi;
            RGB3 c_rgb = clouds->color_at(lat, lon);
            double alpha = fmin(1.0, fmax(0.0, c_rgb.luminance() / 255.0));
            if (alpha > 0.0)
            {
                double inv_a = 1.0 - alpha;
                sr = (unsigned char)std::clamp((int)(sr * inv_a + c_rgb.r), 0, 255);
                sg = (unsigned char)std::clamp((int)(sg * inv_a + c_rgb.g), 0, 255);
                sb = (unsigned char)std::clamp((int)(sb * inv_a + c_rgb.b), 0, 255);
            }

            red_data[idx] = sr;
            green_data[idx] = sg;
            blue_data[idx] = sb;
        }
    }

    touch_gen();
    return true;
}

bool Map::create_merged_night(Map *night, Map *clouds)
{
    if (!clouds || !clouds->is_complete())
    {
        return false;
    }

    unsigned long w = 0, h = 0;
    if (night && night->has_rgb_data())
    {
        w = night->get_width();
        h = night->get_height();
    }
    else
    {
        w = clouds->get_width();
        h = clouds->get_height();
    }

    unsigned long toalloc = w * h;

    if (allocated != toalloc || !red_data || !green_data || !blue_data)
    {
        if (red_data)
        {
            delete[] red_data;
        }
        if (green_data)
        {
            delete[] green_data;
        }
        if (blue_data)
        {
            delete[] blue_data;
        }
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
    }

    image_width = w;
    image_height = h;
    lat_scale = (double)image_height / _pi;
    lon_scale = (double)image_width / (_pi * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;

    bool has_night = (night && night->has_rgb_data());

    // Dark blue night equivalent for clouds over night side
    const double kNightCloudR = 12.0;
    const double kNightCloudG = 18.0;
    const double kNightCloudB = 36.0;

    for (unsigned long y = 0; y < image_height; y++)
    {
        double lat = half_pi - (y + 0.5) * inv_lat_scale;
        for (unsigned long x = 0; x < image_width; x++)
        {
            unsigned long idx = y * image_width + x;
            double nr = has_night ? night->red_data[idx] : 0.0;
            double ng = has_night ? night->green_data[idx] : 0.0;
            double nb = has_night ? night->blue_data[idx] : 0.0;

            double lon = (x + 0.5) * inv_lon_scale - _pi;
            RGB3 c_rgb = clouds->color_at(lat, lon);
            double alpha = fmin(1.0, fmax(0.0, c_rgb.luminance() / 255.0));
            if (alpha > 0.0)
            {
                double inv_a = 1.0 - alpha;
                nr = nr * inv_a + kNightCloudR * alpha;
                ng = ng * inv_a + kNightCloudG * alpha;
                nb = nb * inv_a + kNightCloudB * alpha;
            }

            red_data[idx] = (unsigned char)std::clamp((int)nr, 0, 255);
            green_data[idx] = (unsigned char)std::clamp((int)ng, 0, 255);
            blue_data[idx] = (unsigned char)std::clamp((int)nb, 0, 255);
        }
    }

    touch_gen();
    return true;
}

std::uint32_t Map::uniform_region(unsigned long left, unsigned long top, int level)
{
    const unsigned long first = top * image_width + left;
    const std::uint32_t material = surface_materials.size() == allocated
        ? static_cast<std::uint32_t>(surface_materials[first]) : 0;
    const std::uint32_t target = red_data[first] | (std::uint32_t(green_data[first]) << 8)
        | (std::uint32_t(blue_data[first]) << 16) | (material << 24);
    if (!level) return target;
    const std::uint64_t key = (std::uint64_t(first) << 6) | level;
    const auto found = uniform_regions.find(key);
    if (found != uniform_regions.end()) return found->second;
    const unsigned long size = 1ul << level;
    const unsigned long step = level == 6 ? 8 : 1;
    std::uint32_t result = target;
    for (unsigned long row = top; row < std::min(image_height, top + size); row += step)
    {
        for (unsigned long col = left; col < std::min(image_width, left + size); col += step)
        {
            if (uniform_region(col, row, level == 6 ? 3 : 0) != target)
            {
                result = UINT32_MAX;
                break;
            }
        }
        if (result == UINT32_MAX) break;
    }
    // Bound memory even after a long journey over a high-resolution world.
    if (uniform_regions.size() >= 16384) uniform_regions.clear();
    uniform_regions.emplace(key, result);
    return result;
}

bool Map::region_matches(unsigned long left, unsigned long top, unsigned long right, unsigned long bottom,
    unsigned long tile_left, unsigned long tile_top, int level, std::uint32_t target)
{
    const unsigned long size = 1ul << level;
    if (tile_left > right || tile_top > bottom || tile_left + size <= left || tile_top + size <= top) return true;
    const std::uint32_t signature = uniform_region(tile_left, tile_top, level);
    if (signature == target) return true;
    if (!level || (tile_left >= left && tile_top >= top
        && std::min(image_width, tile_left + size) <= right + 1
        && std::min(image_height, tile_top + size) <= bottom + 1)) return false;
    const int child = level == 6 ? 3 : 0;
    const unsigned long step = 1ul << child;
    for (unsigned long row = tile_top; row < std::min(image_height, tile_top + size); row += step)
    {
        for (unsigned long col = tile_left; col < std::min(image_width, tile_left + size); col += step)
        {
            if (!region_matches(left, top, right, bottom, col, row, child, target)) return false;
        }
    }
    return true;
}

bool Map::color_within_cap(double lat, double lon, double angle, RGB3 &color, bool allow_equal_pixels)
{
    if (generating_fic_texture || !is_complete() || !allocated
        || !std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(angle)
        || angle < 0 || fabs(lat) + angle >= half_pi) return false;

    // A spherical cap fits inside this latitude/longitude rectangle. Decline at
    // poles and the date line so pixel indices remain monotonic across it.
    const double longitude_span = angle / cos(fabs(lat) + angle);
    double wrapped = fmod(lon + _pi, 2 * _pi);
    if (wrapped < 0) wrapped += 2 * _pi;
    if (wrapped <= longitude_span || wrapped + longitude_span >= 2 * _pi) return false;
    const double center = wrapped - _pi;
    const unsigned int first = idx_of(lat + angle, center - longitude_span);
    const unsigned int last = idx_of(lat - angle, center + longitude_span);
    if (first == idx_not_ready || first >= allocated || last >= allocated) return false;
    color = RGB3(red_data[first], green_data[first], blue_data[first]);
    if (first != last)
    {
        if (!allow_equal_pixels) return false;
        const unsigned long left = first % image_width, right = last % image_width;
        const unsigned long top = first / image_width, bottom = last / image_width;
        if (left > right || top > bottom) return false;
        if (uniform_region_generation != gen)
        {
            uniform_regions.clear();
            uniform_region_generation = gen;
        }
        const std::uint32_t target = uniform_region(left, top, 0);
        for (unsigned long row = top / 64 * 64; row <= bottom; row += 64)
        {
            for (unsigned long column = left / 64 * 64; column <= right; column += 64)
            {
                if (!region_matches(left, top, right, bottom, column, row, 6, target)) return false;
            }
        }
    }
    return true;
}

std::pair<RGB3, SurfaceMaterial> Map::surface_pixel(unsigned long column, unsigned long row) const
{
    const auto index = row * image_width + column;
    if (!is_complete() || column >= image_width || row >= image_height || index >= allocated)
        return {RGB3(0, 8, 24), SurfaceMaterial::unknown};
    return {RGB3(red_data[index], green_data[index], blue_data[index]),
        surface_materials.size() == allocated ? surface_materials[index] : SurfaceMaterial::unknown};
}

std::array<double, 4> Map::surface_pixel_bounds(unsigned long column, unsigned long row) const
{
    if (!(lat_scale > 0) || !(lon_scale > 0)) return {0, 0, 0, 0};
    // idx_of uses floor for longitude and ceil for latitude; the polar rows are clipped.
    const double north = row == 0 ? half_pi : std::clamp(half_pi - (row - 1) / lat_scale, -half_pi, half_pi);
    const double south = row + 1 >= image_height ? -half_pi : std::clamp(half_pi - row / lat_scale, -half_pi, half_pi);
    const double west = column == 0 ? -_pi : std::clamp(column / lon_scale - _pi, -_pi, _pi);
    const double east = column + 1 >= image_width ? _pi : std::clamp((column + 1) / lon_scale - _pi, -_pi, _pi);
    return {south, north, west, east};
}

SurfaceMaterial Map::material_at(double latitude, double longitude)
{
    if (generating_fic_texture || !is_complete() || surface_materials.size() != allocated
        || !std::isfinite(latitude) || !std::isfinite(longitude)) return SurfaceMaterial::unknown;
    const unsigned int index = idx_of(latitude, longitude);
    if (index == idx_not_ready || index >= surface_materials.size()) return SurfaceMaterial::unknown;
    return surface_materials[index];
}

RGB3 Map::color_at(double lat, double lon)
{
    RGB3 result;
    if (generating_fic_texture && (!has_rgb_data() || !allocated))
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
        if (idx >= allocated)
        {
            idx = allocated - 1;
        }
        result.r = red_data   ? red_data[idx]   : 255;
        result.g = green_data ? green_data[idx] : 255;
        result.b = blue_data  ? blue_data[idx]  : 255;
    }
    return result;
}

RGB3 Map::color_at_bilinear(double lat, double lon) const
{
    if (!is_complete() || !image_width || allocated < image_width * image_height
        || !std::isfinite(lat) || !std::isfinite(lon))
    {
        return RGB3(0, 0, 0);
    }

    // Use the same angular grid as color_at(), interpolating across the longitude seam.
    double wrapped_lon = fmod(lon + _pi, 2.0 * _pi);
    if (wrapped_lon < 0)
    {
        wrapped_lon += 2.0 * _pi;
    }
    const double xf = wrapped_lon * lon_scale;
    const double yf = std::clamp((half_pi - lat) * lat_scale, 0.0, (double)(image_height - 1));
    const double x_floor = floor(xf);
    const unsigned long x0 = (unsigned long)x_floor % image_width;
    const unsigned long x1 = (x0 + 1) % image_width;
    const unsigned long y0 = (unsigned long)floor(yf);
    const unsigned long y1 = std::min(y0 + 1, image_height - 1);
    const double tx = xf - x_floor;
    const double ty = yf - y0;

    auto channel = [&](const unsigned char *data) -> unsigned char
    {
        const double top = data[y0 * image_width + x0] * (1.0 - tx) + data[y0 * image_width + x1] * tx;
        const double bottom = data[y1 * image_width + x0] * (1.0 - tx) + data[y1 * image_width + x1] * tx;
        return (unsigned char)std::clamp(top * (1.0 - ty) + bottom * ty + 0.5, 0.0, 255.0);
    };
    return RGB3(channel(red_data), channel(green_data), channel(blue_data));
}

double Map::elevation_at_bilinear(double lat, double lon) const
{
    if (!bump_data || !image_width || !image_height || allocated < image_width * image_height
        || !std::isfinite(lat) || !std::isfinite(lon))
    {
        return 0.0;
    }
    double wrapped_lon = fmod(lon + _pi, 2.0 * _pi);
    if (wrapped_lon < 0.0)
    {
        wrapped_lon += 2.0 * _pi;
    }
    const double xf = wrapped_lon * lon_scale;
    const double yf = std::clamp((half_pi - lat) * lat_scale, 0.0, (double)(image_height - 1));
    const double x_floor = floor(xf);
    const unsigned long x0 = (unsigned long)x_floor % image_width;
    const unsigned long x1 = (x0 + 1) % image_width;
    const unsigned long y0 = (unsigned long)floor(yf);
    const unsigned long y1 = std::min(y0 + 1, image_height - 1);
    const double tx = xf - x_floor, ty = yf - y0;
    auto sample = [&](unsigned long x, unsigned long y) -> double
    {
        const double height = bump_data[y * image_width + x];
        return std::isfinite(height) ? height : 0.0;
    };
    const double top = sample(x0, y0) * (1.0 - tx) + sample(x1, y0) * tx;
    const double bottom = sample(x0, y1) * (1.0 - tx) + sample(x1, y1) * tx;
    return top * (1.0 - ty) + bottom * ty;
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
    if (typeclass() == class_star &&
        (!(volumetric_mean_radius > 0) || !std::isfinite(volumetric_mean_radius)) &&
        stellar_regime(this) == regime_degenerate)
    {
        static_cast<Star*>(this)->estimate_radius();
    }
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
    if (render_saved_planet(cel, [this](CelestialObject* working) { generate_rocky_map(working); })) return;
    Planet *p = (Planet*)cel;
    bool want_overcast_sky = p->preserve_loaded_parameters && p->overcast;
    assert(cel->typeclass() == class_planet || cel->typeclass() == class_moon);
    std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
    bool create_bump;
    double bump_scale, inv_bump_scale;
    mtx.lock();
    try
    {
        cel->randomize();
        generating_fic_texture = true;

        int lr = cel->fictitious_map_height;
        double BV = cel->BV_color;
        if (BV < 0.8) BV = 0.8;                 // most rocks aren't blue
        bool force_has_water = false;
        if (!p->preserve_loaded_parameters && (cel->type == waterworld || cel->type == hycean))
        {
            p->has_water = 1;
            force_has_water = true;
        }

        if (!p->preserve_loaded_parameters) p->temperature = 0;
        double T_surf = p->estimate_surface_temperature();
        const double Tboil = water_freezing+100;                                     // Reference pressure

        // Constants for water b.p.
        const double R = 8.314;                                         // J/(mol*K)
        const double DELTA_H_VAP = 40660.0;                             // J/mol
        const double P1 = 1.0e+5;  

        // Clausius-Clapeyron calculation
        double inv_T1 = 1.0 / Tboil;
        double gas_constant_ratio = R / DELTA_H_VAP;
        double pressure = p->get_surface_pressure();
        double pressure_log = std::log(pressure / P1);

        bool life_possible = p->estimate_habitability();
        T_surf = p->estimate_surface_temperature();

        if (!p->preserve_loaded_parameters && !force_has_water && pressure >= 5*oneatm && T_surf >= 400)
        {
            p->has_water = 0;
            life_possible = false;
            want_overcast_sky = (p->cloud_map == nullptr);                                     // for a roasting Venus-like planet with a thick cloud layer.
            p->transparent_clouds = false;
            p->overcast = true;
        }
        else if (!p->preserve_loaded_parameters && !force_has_water && pressure < 150)
        {
            p->has_water = 0;
            life_possible = false;
        }

        int octaves = 5 + (cel->cel_rand() % 4);
        double lacbase = sqrt(fmax(1, log(cel->volumetric_mean_radius)));
        double lacunarity = p->cel_frand(0.51*lacbase, 0.53*lacbase);
        double gain = p->has_water ? 0.5 : 2.5;
        double scale = p->cel_frand(p->has_water ? 1.5 : 0.2, p->has_water ? 2.9 : 0.8);             // Controls feature sizes (smaller scale = larger continents)

        Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
        RGB3 rgb = Color::rgb_from_color(col, -1);

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

        create_bump = (bump_data == nullptr);
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

        // Keep generated biome identity independent of its visible pigment color.
        surface_materials.assign(allocated, SurfaceMaterial::ground);

        lat_scale = (double)image_height / _pi;
        lon_scale = (double)image_width / (_pi * 2);
        inv_lat_scale = 1.0 / lat_scale;
        inv_lon_scale = 1.0 / lon_scale;
        bump_scale = p->m_bump_scale = p->estimate_bump_scale();
        inv_bump_scale = 1.0 / bump_scale;
        std::cout << "Allocated " << allocated << " pixels for fictitious rocky " << cel->name << " map." << std::endl;

        double inv_h2o_level = 0, phi, theta, u, v, nx, ny, nz, height_value, r_weight, T_local, sh;
        
        // Lower multiplier creates massive, continental-scale province shifts
        double province_scale = scale * 0.15; 
        double warp_scale = scale * 0.5;     
        double warp_strength = 0.8;          
        double albedo_value, grain_value, border_noise;

        double province_pos, province_t, rmult, gmult, bmult;
        double edge_dist, mottle_strength, ms, invms, mottle_noise, hue_noise;
        unsigned int x, y;
        int idx, province_idx, neighbor_province_idx, mottled_idx;
        if (!p->preserve_loaded_parameters && p->has_water && life_possible && randomize_txgen
            && !p->vegetation_r && !p->vegetation_g && !p->vegetation_b)
        {
            CelestialObject *light_center = cel->get_light_center();
            const double stellar_temperature = light_center && light_center->typeclass() == class_star
                ? static_cast<Star*>(light_center)->estimate_temperature() : sun_temp;
            RGB3 veg_color = generate_vegetation_color(&cel->rng, stellar_temperature);
            p->vegetation_r = veg_color.r;
            p->vegetation_g = veg_color.g;
            p->vegetation_b = veg_color.b;
        }
        if (p->has_water) inv_h2o_level = 1.0 / p->has_water;

        if (!p->has_water) life_possible = false;

        bool tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star 
            && p->is_tidal_locked();

        const LocalClimate climate = p->local_climate();

        // A small per-planet palette of terrain "provinces" -- bright highlands against darker
        // basalt or tholin -- so the surface reads as distinct patches with ragged borders rather
        // than one continuous gradient.
        const bool enable_provinces = true;
        int num_provinces = 2 + (cel->cel_rand() % 3);                                  // 2-4 provinces
        double border_roughness = p->cel_frand(1.3, 2.9);                               // how jagged the border itself is
        double border_noise_scale = province_scale * p->cel_frand(3.5, 7.0);
        double mottle_zone = p->cel_frand(0.25, 0.4);                                   // how far the speckling reaches into each province; 1 = whole planet.
        unsigned int dither_seed = (unsigned int)cel->cel_rand();                       // per-planet salt for the per-pixel mottle hash
        double hue_scale = province_scale * p->cel_frand(2.0, 4.0);                     // sub-regions within a single province
        double hue_amount = p->cel_frand(0.1, 0.3);                                     // subtle warm/cool wobble, green left alone
        double province_variability = p->cel_frand(0.25, 0.65);
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
                province_rmult[p_i] = 1.0 - stain * 0.53;
                province_gmult[p_i] = 1.0 - stain * 0.67;
                province_bmult[p_i] = 1.0 - stain * 0.81;
            }
            else if (cold_icy_world)
            {
                // Otherwise just brightness variation across the icy background -- no hue shift.
                double p_mult = 0.9 - p->cel_frand(0, 0.4);
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

                // cap to prevent weird colors
                p_gmult = fmin(p_gmult, 1.1 * p_rmult);
                p_bmult = fmin(p_bmult, 1.1 * p_gmult);

                province_rmult[p_i] = p_rmult;
                province_gmult[p_i] = p_gmult;
                province_bmult[p_i] = p_bmult;
            }
        }

        for (y = 0; y < image_height; ++y)
        {
            if (done || abort_load) { generating_fic_texture = false; return; }
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

                idx = y * image_width + x;
                if (idx >= allocated)
                {
                    mtx.unlock();
                    return;
                }

                // Generate a low-frequency 3D warp vector
                double warp_x = fBm(nx * warp_scale + 12.3, ny * warp_scale + 4.1, nz * warp_scale + 9.8, 3, lacunarity, 0.5);
                double warp_y = fBm(nx * warp_scale + 3.1, ny * warp_scale + 19.2, nz * warp_scale + 2.4, 3, lacunarity, 0.5);
                double warp_z = fBm(nx * warp_scale + 8.5, ny * warp_scale + 7.7, nz * warp_scale + 14.6, 3, lacunarity, 0.5);
                
                // Offset the primary coordinates using the warp vector
                double wx = nx * scale + (warp_x - 0.5) * warp_strength;
                double wy = ny * scale + (warp_y - 0.5) * warp_strength;
                double wz = nz * scale + (warp_z - 0.5) * warp_strength;

                // Spatially modulate roughness (0.0 = smooth maria, 1.0 = rugged highlands)
                double roughness_mask = fBm(nx * province_scale * 0.8, ny * province_scale * 0.8, nz * province_scale * 0.8, 2, lacunarity, 0.5);
                double local_gain = gain * (0.3 + 0.7 * roughness_mask); 

                // Get warped, locally-modulated height value
                height_value = create_bump ? fBm(wx, wy, wz, octaves, lacunarity, local_gain)
                    : fmin(1.0, fmax(0.0, (inv_bump_scale * bump_data[idx] + 0.5)));

                if (create_bump) bump_data[idx] = bump_scale * (height_value - 0.5);

                // Tie albedo directly into the warped coordinate space
                albedo_value = fBm(wx * (province_scale / scale) + 5.2, wy * (province_scale / scale) + 1.3, wz * (province_scale / scale) + 2.7,
                    4, lacunarity, local_gain);

                if (enable_provinces)
                {
                    // Apply warped coordinates and mask the high-frequency ridges
                    // Use standard fBm. ridged_fBm mathematically generates veins/marrow.
                    grain_value = fBm(wx * 14.0 + 91.7, wy * 14.0 + 43.1, wz * 14.0 + 17.9,
                        4, lacunarity, local_gain);
                    grain_value = (grain_value - 0.5) * roughness_mask + 0.5; 
                    r_weight = fmin(1.0, fmax(0.0, 0.55 + 0.9 * (grain_value - 0.5)));
                    border_noise = fBm(nx * border_noise_scale + 61.4, ny * border_noise_scale + 8.8, nz * border_noise_scale + 29.6,
                        3, lacunarity, gain);

                    double albedo_stretched = fmin(1.0, fmax(0.0, (albedo_value - 0.5) * 2.2 + 0.5));
                    province_pos = fmod(albedo_stretched * num_provinces + (border_noise - 0.5) * border_roughness, (double)num_provinces);
                    if (province_pos < 0) province_pos += num_provinces;
                    province_idx = (int)province_pos;
                    if (province_idx >= num_provinces) province_idx = num_provinces - 1;
                    province_t = province_pos - province_idx;

                    edge_dist = fmin(province_t, 1.0 - province_t);
                    
                    // Cap mottle_zone below 0.5 so the blend finishes before the midpoint neighbor swap
                    mottle_zone = fmin(mottle_zone, 0.49); 
                    mottle_strength = fmax(0.0, 1.0 - edge_dist / mottle_zone);

                    // Adjust the multiplier (e.g., 15.0 for large chunks, 50.0 for fine sand)
                    double mottle_scale = scale * cel->cel_frand(60, 90); 
                    
                    // Evaluate in 3D space with arbitrary offsets to prevent correlation
                    mottle_noise = fBm(wx * mottle_scale + 11.1, wy * mottle_scale + 22.2, wz * mottle_scale + 33.3, 
                                       3, lacunarity, local_gain);

                    // Map mottle_strength [0..1] to an offset of [-0.75 .. 0.0]
                    // At the exact border (1.0), offset is 0.0 (pure 50/50 noise).
                    // At the inner edge (0.0), offset is -0.75 (heavily clamped to base province).
                    double blend_factor = mottle_noise + (mottle_strength - 1.0) * 0.75;
                    
                    blend_factor = fmax(0.0, fmin(1.0, blend_factor));
                    blend_factor = blend_factor * blend_factor * (3.0 - 2.0 * blend_factor);

                    double blend1 = 1.0 - blend_factor;
                    
                    neighbor_province_idx = (province_t < 0.5)
                        ? (province_idx - 1 + num_provinces) % num_provinces
                        : (province_idx + 1) % num_provinces;
                    
                    rmult = blend1 * province_rmult[province_idx] + blend_factor * province_rmult[neighbor_province_idx];
                    gmult = blend1 * province_gmult[province_idx] + blend_factor * province_gmult[neighbor_province_idx];
                    bmult = blend1 * province_bmult[province_idx] + blend_factor * province_bmult[neighbor_province_idx];

                    hue_noise = fBm(nx * hue_scale + 71.2, ny * hue_scale + 34.9, nz * hue_scale + 6.1,
                        3, lacunarity, gain);
                    double hue_t = 2.0 * (hue_noise - 0.5);
                    rmult *= (1.0 + hue_amount * hue_t);
                    bmult *= (1.0 - hue_amount * hue_t);
                }
                else
                {
                    grain_value = fBm(wx * 14.0 + 91.7, wy * 14.0 + 43.1, wz * 14.0 + 17.9,
                        4, lacunarity, local_gain);
                    grain_value = (grain_value - 0.5) * roughness_mask + 0.5; 
                    r_weight = fmin(1.0, fmax(0.0, 0.55 + 0.9 * (grain_value - 0.5)));
                    border_noise = fBm(nx * border_noise_scale + 61.4, ny * border_noise_scale + 8.8, nz * border_noise_scale + 27.6,
                        3, lacunarity, gain);
                }

                if (p && p->has_water)
                {
                    T_local = climate.temperature(nx, ny, nz, bump_data[idx]);
                    if (cel->type == waterworld || cel->type == hycean)
                    {
                        surface_materials[idx] = SurfaceMaterial::water;
                        // Deep ocean
                        red_data[idx] = fmin(255, 12+16*height_value);
                        green_data[idx] = fmin(255, 24+24*height_value);
                        blue_data[idx] = fmin(255, 32+128*height_value);
                    }
                    // Biome allocation based on height thresholds
                    else if (height_value < p->has_water && T_local < Tboil)
                    {   // Ocean
                        surface_materials[idx] = SurfaceMaterial::water;
                        sh = height_value*inv_h2o_level;
                        // sh *= (Tboil - T_base) / (Tboil - water_freezing);
                        sh = pow(sh, 3);                                                           // shallowness multiplied to show water optical density
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
                        && (p->vegetation_r || p->vegetation_g || p->vegetation_b)
                        && (!tidal_locked_to_star || nx * climate.sun_x + ny * climate.sun_y + nz * climate.sun_z > 0))                               // vegetation only on the day side
                    {   // Forests
                        surface_materials[idx] = SurfaceMaterial::vegetation;
                        red_data[idx] = fmin(255, p->vegetation_r * r_weight);
                        green_data[idx] = fmin(255, p->vegetation_g * r_weight);
                        blue_data[idx] = fmin(255, p->vegetation_b * r_weight);
                    }
                    else
                    {
                        red_data[idx] = fmin(255, 110 * rmult * r_weight);
                        green_data[idx] = fmin(255, 90 * gmult * r_weight);
                        blue_data[idx] = fmin(255, 75 * bmult * r_weight);
                    }
                    const double frost = LocalClimate::frost_fraction(T_local);
                    if (frost >= 0.5) surface_materials[idx] = SurfaceMaterial::ground;
                    red_data[idx] = (1 - frost) * red_data[idx] + frost * (192 + 63 * r_weight);
                    green_data[idx] = (1 - frost) * green_data[idx] + frost * (224 + 31 * r_weight);
                    blue_data[idx] = (1 - frost) * blue_data[idx] + frost * (240 + 15 * r_weight);
                }
                else
                {
                    // Lifeless planet or moon
                    red_data[idx] = (unsigned char)(fmin(255, rgb.r * rmult * r_weight + radd));
                    green_data[idx] = (unsigned char)(fmin(255, rgb.g * gmult * r_weight + gadd));
                    blue_data[idx] = (unsigned char)(fmin(255, rgb.b * bmult * r_weight + badd));
                }
            }
        }

        if (!p->preserve_loaded_parameters) cel->BV_color = 0.9 - p->has_water;

    }
    catch (...)
    {
        generating_fic_texture = false;
        mtx.unlock();
        return;
    }

    mtx.unlock();
    generating_fic_texture = false;
    if (create_bump) stamp_craters(cel, bump_scale);
    procedurally_generated = true;
    touch_gen();

    // Deferred to the end so as to avoid the mutex lock.
    if (want_overcast_sky && !p->cloud_map)
    {
        p->cloud_map = new Map(cel);
        p->cloud_map->generate_overcast_sky(cel);
    }
    generate_surface_rings(cel);
    cel->generate_water_clouds();
}

void Map::generate_surface_rings(CelestialObject *cel)
{
    Planet *p = static_cast<Planet*>(cel);
    if (cel->typeclass() == class_planet)
    {
        p->generate_ring_parameters();
    }
    else if (!p->preserve_loaded_parameters)
    {
        p->ring_radius = 0;
    }

    if (p->ring_radius)
    {
        // Generate a ring texture and a ring transparency map using ring_inner_radius/ring_radius and ring_mean_opacity.
        if (p->ring_map)
        {
            delete p->ring_map;
        }
        if (p->ringx_map)
        {
            delete p->ringx_map;
        }

        p->ring_map = new Map(p);
        p->ringx_map = new Map(p);

        p->ring_map->generate_ring_map(p, p->ring_radius / p->volumetric_mean_radius * 1024,
            p->ring_inner_radius/p->ring_radius, p->ring_mean_opacity, p->ringx_map);
    }
}

void alienorum::Map::generate_lava_map(CelestialObject *cel, bool is_night)
{
    if (render_saved_planet(cel, [this, is_night](CelestialObject* working) { generate_lava_map(working, is_night); })) return;
    std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
    std::unique_lock<std::mutex> lock(mtx);
    try
    {
        generating_fic_texture = true;
        gen = 0;
        Planet *p = (Planet*)cel;
        cel->randomize();

        // The day map owns the relief; the night map follows the same terrain.
        Map *surface = is_night ? cel->surf_map : nullptr;
        if (is_night && (!surface || surface == this || !surface->is_complete() || !surface->has_bump_data()))
        {
            generating_fic_texture = false;
            return;
        }
        const bool create_bump = !is_night && !bump_data;
        if (is_night)
        {
            image_height = surface->image_height;
            image_width = surface->image_width;
            delete[] bump_data;
            bump_data = nullptr;
        }
        else if (create_bump)
        {
            image_height = cel->fictitious_map_height;
            image_width = image_height * 2;
        }
        else
        {
            cel->fictitious_map_height = image_height;
        }

        lat_scale = (double)image_height / _pi;
        lon_scale = (double)image_width / (_pi * 2);
        inv_lat_scale = 1.0 / lat_scale;
        inv_lon_scale = 1.0 / lon_scale;

        allocated = image_height * image_width;
        delete[] red_data;
        delete[] green_data;
        delete[] blue_data;
        red_data = green_data = blue_data = nullptr;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];

        const double bump_scale = p->m_bump_scale = p->estimate_bump_scale();
        const double inv_bump_scale = bump_scale > 0.0 ? 1.0 / bump_scale : 0.0;
        if (!bump_data) bump_data = new double[allocated];
        if (is_night) memcpy(bump_data, surface->bump_data, allocated * sizeof(double));
        std::cout << "Allocated " << allocated << " pixels for fictitious lava "
            << (is_night ? "night" : "day") << " map." << std::endl;
        lock.unlock();

        const double terrain_scale = cel->cel_frand(1.8, 3.2);
        const double offset_x = cel->cel_frand(0.0, 100.0);
        const double offset_y = cel->cel_frand(0.0, 100.0);
        const double offset_z = cel->cel_frand(0.0, 100.0);

        double tempK = p->estimate_surface_temperature();
        if (!std::isfinite(tempK) || tempK <= 0.0) tempK = 1350.0;
        const bool tidal_locked = p->orbit && p->orbit->center
            && p->orbit->center->type == star && p->is_tidal_locked();
        // Both textures cover the globe. The renderer selects the actual illuminated
        // hemisphere, so heating follows the map's lighting state, not a fixed longitude.
        const double local_temperature = tempK * (is_night ? (tidal_locked ? 0.60 : 0.75) : 1.25);
        const double heat = local_temperature / (local_temperature + 1500.0);
        const double emission = is_night ? (0.10 + 0.22 * heat) : (0.65 + 0.35 * heat);
        LavaFissures lava_fissures(static_cast<std::uint32_t>(cel->rnd_seed));

        // Fill in glowing hot lava.
        int x, y, y1, idx;
        double height;
        RGB3 rgb;
        for (y = 0; y < image_height; y++)
        {
            if (done || abort_load)
            {
                generating_fic_texture = false;
                return;
            }
            y1 = y * image_width;
            double v = (double)y / (double)image_height;
            double theta = v * _pi;
            double sin_th = sin(theta);
            double cos_th = cos(theta);

            for (x = 0; x < image_width; x++)
            {
                idx = y1 + x;
                double u = (double)x / (double)image_width;
                double phi = u * 2.0 * _pi;
                double nx = sin_th * cos(phi);
                double ny = sin_th * sin(phi);
                double nz = cos_th;

                const double total_fissure = lava_fissures.intensity(nx, ny, nz);
                if (create_bump)
                {
                    const double warp = fBm(nx * 2.0 + offset_x, ny * 2.0 + offset_y,
                        nz * 2.0 + offset_z, 3, 2.0, 0.5) - 0.5;
                    height = fBm(nx * terrain_scale + offset_x + warp,
                        ny * terrain_scale + offset_y + warp,
                        nz * terrain_scale + offset_z + warp, 6, 2.0, 0.5);
                    // Basalt highlands surround smooth lowlands and recessed fractures.
                    height = std::clamp(0.5 + (height - 0.5) * 1.8 - total_fissure * 0.08, 0.0, 1.0);
                    bump_data[idx] = bump_scale * (height - 0.5);
                }
                else
                {
                    height = std::clamp(0.5 + inv_bump_scale * bump_data[idx], 0.0, 1.0);
                }

                // Volcanic calderas and mantle plume hotspots.
                const double vent_val = sin(nx * 6.5 + 12.1) * cos(ny * 6.5 + 34.5) * sin(nz * 6.5 + 56.7);
                const double vent_intensity = vent_val > 0.64
                    ? pow((vent_val - 0.64) / 0.36, 2.0) : 0.0;

                // Magma crust fraction:
                // Chilled crust forms dark basalt rafts with low thermal emission.
                // Active incandescence is concentrated in fissures, caldera hotspots,
                // and lowland magma basins.
                double lava_activity = fmax(total_fissure, vent_intensity * 0.85);
                if (height < 0.44)
                {
                    double basin_dep = (0.44 - height) / 0.44;
                    lava_activity = fmax(lava_activity, basin_dep * 0.75);
                }
                // Temperature scales emission without filling the crust with a uniform
                // molten color. Fractures, basins, and calderas retain their contrast.

                // Chilled crust: dark basalt with faint crimson warmth near cracks
                if (lava_activity < 0.08)
                {
                    double t_crust = lava_activity / 0.08;
                    rgb.r = (unsigned char)(8 + 24 * t_crust);
                    rgb.g = (unsigned char)(3 + 8 * t_crust);
                    rgb.b = (unsigned char)(2 + 4 * t_crust);
                }
                else if (lava_activity < 0.35)
                {
                    // Incandescent cherry-red to deep vermilion
                    double t = (lava_activity - 0.08) / 0.27;
                    rgb.r = (unsigned char)fmin(255.0, 32.0 + 195.0 * t);
                    rgb.g = (unsigned char)fmin(255.0, 11.0 + 45.0 * t);
                    rgb.b = (unsigned char)fmin(255.0, 6.0 + 10.0 * t);
                }
                else if (lava_activity < 0.75)
                {
                    // Vibrant molten orange to fiery gold
                    double t = (lava_activity - 0.35) / 0.40;
                    rgb.r = 255;
                    rgb.g = (unsigned char)fmin(255.0, 56.0 + 120.0 * t);
                    rgb.b = (unsigned char)(16.0 + 14.0 * t);
                }
                else
                {
                    // Intense incandescent golden-amber core (never solid white)
                    double t = fmin(1.0, (lava_activity - 0.75) / 0.25);
                    rgb.r = 255;
                    rgb.g = (unsigned char)(176.0 + 14.0 * t);
                    rgb.b = (unsigned char)(30.0 + 10.0 * t);
                }

                rgb.r = (unsigned char)(rgb.r * emission);
                rgb.g = (unsigned char)(rgb.g * emission);
                rgb.b = (unsigned char)(rgb.b * emission);

                if (!is_night)
                {
                    // Reflected basalt remains visible in daylight; night carries emission.
                    const double crust = (1.0 - lava_activity) * (0.6 + 0.4 * height);
                    rgb.r = (unsigned char)fmin(255.0, rgb.r + 34.0 * crust);
                    rgb.g = (unsigned char)fmin(255.0, rgb.g + 30.0 * crust);
                    rgb.b = (unsigned char)fmin(255.0, rgb.b + 32.0 * crust);
                }

                red_data[idx] = rgb.r;
                green_data[idx] = rgb.g;
                blue_data[idx] = rgb.b;
            }
        }
    }
    catch (...)
    {
        generating_fic_texture = false;
        return;
    }

    generating_fic_texture = false;
    procedurally_generated = true;
    touch_gen();
    if (!is_night) generate_surface_rings(cel);
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

    int num_craters = (int)((p->type == lavaworld ? 3 : 300) * bombardment_factor);
    if (num_craters < 1) return;

    double planet_radius = cel->volumetric_mean_radius;
    double min_diam = 50.0;                                                 // meters.
    double max_diam = fmin(planet_radius * 0.3, 900000.0);                  // cap basins at ~900 km or 30% of the planet, whichever is smaller

    std::vector<Crater> craters(num_craters);
    for (int i = 0; i < num_craters; ++i)
    {
        if (done || abort_load) { generating_fic_texture = false; return; }
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
                        // The brightening wants the rim's profile, which is the Gaussian on its
                        // own -- dividing the scaled height back out again only reintroduces
                        // c.rim_height as a divisor, and fmax() cannot guard that: for a
                        // negative rim height it returns 1e-6 rather than the height, turning a
                        // number that belongs in (0,1] into billions of the wrong sign.
                        double t = (r - 1.0) / c.rim_width;
                        double rim_profile = exp(-t * t);
                        bump_delta = c.rim_height * rim_profile;
                        color_mult += 0.15 * rim_profile;
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
    if (render_saved_planet(cel, [this](CelestialObject* working) { generate_gas_giant_map(working); })) return;
    int num_bands, i, lr;
    bool tidal_locked_to_star;
    bool add_storm;
    double stormlat, stormlon;
    double variability, BV;
    Color col;
    RGB3 rgb;
    std::unique_ptr<alienorum::RGB3 []> bands;
    Planet *p = (Planet*)cel;
    cel->randomize();

    std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(123,259)));
    mtx.lock();
    try
    {
        generating_fic_texture = true;

        lr = cel->fictitious_map_height;
        BV = cel->BV_color;
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

        if (!p->preserve_loaded_parameters)
            p->ensure_atmosphere()->ensure_composition()->generate_fictitious_for_planet(p->type);
        tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star 
            && (fabs((p->sidereal_rotational_period / p->orbit->period) - 1) < 0.01);

        cel->randomize();
        variability = cel->cel_frand(0.2, 0.4);
        num_bands = cel->cel_rand() % 9 + 7;
        if (cel->type == clearskies)
        {
            variability = 0;
            std::cout << p->name << " BV=" << BV << std::endl;
        }
        else if (cel->type == ice_giant)
        {
            num_bands = std::max(2, num_bands/4);
            variability /= 4;
            // BV -= 0.5;
        }
        bands = std::make_unique<RGB3[]>(num_bands);

        col = Color::color_from_magnitude_indices(BV+bv_correction, BV);
        col.normalize(1);
        bool rlm = redlight_mode;
        redlight_mode = false;
        rgb = Color::rgb_from_color(col, pow(p->albedo, 0.25));
        redlight_mode = rlm;
        /* std::cout << cel->name << " BV=" << BV
            << " color=" << col.red << "," << col.green << "," << col.blue << " albedo=" << p->albedo << " rgb=" << rgb << std::endl; */

        add_storm = !tidal_locked_to_star && (cel->type != clearskies) && (cel->cel_frand(0, 1) < 0.2);
        stormlat = cel->cel_frand(0.3, 0.7);
        stormlon = cel->cel_frand(0.2, 0.8);            // storms get cut off if straddling the dateline
    }
    catch (...)
    {
        generating_fic_texture = false;
        mtx.unlock();
        return;
    }
    mtx.unlock();

    double dist_to_storm_x, dist_to_storm_y, storm_dist = 1e29, storm_size = cel->cel_frand(0.29, 0.71);
    int sgnstormlat = sgn(stormlat - 0.5);               // Negative for northern hemisphere; we'll see why when we do the swirl calculation.

    for (i=0; i<num_bands; i++)
    {
        double rmult, gmult, bmult;

        rmult = 1.0 - cel->cel_frand(0, 0.5*variability);
        bmult = 1.0 - cel->cel_frand(0, variability);
        if (rgb.r > rgb.b && rmult < bmult)
        {
            double swap = bmult;
            bmult = rmult;
            rmult = swap;
        }
        gmult = cel->cel_frand( fmin(rmult, bmult), fmax(rmult, bmult) );

        bands[i].r = rgb.r * rmult;
        bands[i].g = rgb.g * gmult;
        bands[i].b = rgb.b * bmult;
        // std::cout << "Band " << i << " " << bands[i] << std::endl;
    }

    double scale = 2.5, u, v, theta, phi, psi, sin_theta, nx, ny, nz, distortX, distortY, final_noise, band_val, t;
    double zscale = tidal_locked_to_star ? scale : (scale * 1.5);
    double swirl_amount = 8 * sgnstormlat;

    for (unsigned int y = 0; y < image_height; ++y)
    {
        if (done || abort_load) { generating_fic_texture = false; return; }
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
            final_noise = fBm(nx + distortX * 4.0, ny + distortY * 2.5, nz, 6, 2.0, 0.55);

            if (add_storm)
            {
                dist_to_storm_x = (u - stormlon) * 2.0 * _pi;
                dist_to_storm_y = (v - stormlat) * _pi;
                // Elliptical distance formula
                storm_dist = sqrt((dist_to_storm_x * dist_to_storm_x) * 2.5 + (dist_to_storm_y * dist_to_storm_y) * 10.0);
                // if (storm_dist < storm_size) final_noise = 0;
            }

            int idx = y * image_width + x;

            double veff = v;
            if (storm_dist < storm_size*2)
            {
                double vbend = 1.0 - fmax(0, storm_dist - storm_size) / storm_size;
                veff += (stormlat-v) * vbend * vbend * vbend;
            }

            if (tidal_locked_to_star)
            {
                add_storm = false;
                // Bands will occur in order of distance to the star, not by latitude as with solar system gas giants.
                band_val = fmod(psi / _pi * num_bands + final_noise * 1.3, num_bands);
            }
            else
            {
                band_val = fmod(fabs(veff - 0.5) * 2 * num_bands + final_noise * 1.3, num_bands);
            }

            if (band_val < 0) band_val += num_bands;
            int band_idx = (int)floor(band_val);
            t = band_val - band_idx; // fractional part for linear interpolation
            double t1 = 1.0 - t;
            int next_band_idx = (band_idx + 1) % num_bands;

            // Interpolate colors between bands for smooth transitions
            red_data[idx] = (unsigned char)(t1 * bands[band_idx].r + t * bands[next_band_idx].r);
            green_data[idx] = (unsigned char)(t1 * bands[band_idx].g + t * bands[next_band_idx].g);
            blue_data[idx] = (unsigned char)(t1 * bands[band_idx].b + t * bands[next_band_idx].b);

            if (add_storm && (storm_dist < storm_size))
            {
                // We are inside the storm; blend into dark colors
                double storm_blend = (storm_size - storm_dist) / storm_size; // 1 at center, 0 at edge
                double sbl1 = 1.0 - storm_blend;

                double swirlx = dist_to_storm_x / storm_dist, swirly = dist_to_storm_y / storm_dist;

                // The following creates a clockwise swirl by default. Coriolis effect dictates CCW in the northern
                // hemisphere, so that's why sgnstormlat has to go negative in the north.
                double swirledx = swirlx * cos(storm_blend*swirl_amount) + swirly * sin(storm_blend*swirl_amount),
                       swirledy = swirly * cos(storm_blend*swirl_amount) - swirlx * sin(storm_blend*swirl_amount);

                // Swirl the storm inside
                double storm_noise = fBm(swirledx * 2.0, swirledy * 2.0, nz * 1.0, 5, 2.0, 0.5) /* * sbl1 * 3 */;
                double sn1 = 1.0 - storm_noise;

                double storm_red   = (unsigned char)fmin(255, bands[next_band_idx].r * storm_noise + 128);
                double storm_green = (unsigned char)fmin(255, bands[next_band_idx].g * storm_noise + 72);
                double storm_blue  = (unsigned char)fmin(255, bands[next_band_idx].b * storm_noise + 40);

                // Linear interpolation blending storm with background bands
                red_data[idx]   = (unsigned char)(storm_red   * storm_blend + red_data[idx]   * sbl1);
                green_data[idx] = (unsigned char)(storm_green * storm_blend + green_data[idx] * sbl1);
                blue_data[idx]  = (unsigned char)(storm_blue  * storm_blend + blue_data[idx]  * sbl1);
            }
        }
    }

    generating_fic_texture = false;
    procedurally_generated = true;
    touch_gen();
    p->generate_ring_parameters();

    if (p->ring_radius)
    {
        // Generate a ring texture and a ring transparency map using ring_inner_radius/ring_radius and ring_mean_opacity.
        if (p->ring_map) delete p->ring_map;
        if (p->ringx_map) delete p->ringx_map;

        p->ring_map = new Map(p);
        p->ringx_map = new Map(p);

        p->ring_map->generate_ring_map(p, p->ring_radius / p->volumetric_mean_radius * 1024,
            p->ring_inner_radius/p->ring_radius, p->ring_mean_opacity, p->ringx_map);
    }
}

void alienorum::Map::generate_overcast_sky(CelestialObject *cel)
{
    cel->randomize();
    Planet *p = (Planet*)cel;
    double zonal, scale, shear, sweep_exp, warp_amt, polar_k, contrast, mottle_scale, mottle_amt;
    bool tidal_locked_to_star;
    RGB3 rgb, deep;

    std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
    mtx.lock();
    try
    {
        generating_fic_texture = true;
        int lr = cel->fictitious_map_height;
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

        rgb = RGB3( (unsigned char)cel->cel_frand(242, 249), (unsigned char)cel->cel_frand(234, 243), (unsigned char)cel->cel_frand(214, 228) );

        tidal_locked_to_star = p->orbit && p->orbit->center && p->orbit->center->type == star
            && (fabs((p->sidereal_rotational_period / p->orbit->period) - 1) < 0.01);

        cel->randomize();

        deep = RGB3((unsigned char)(rgb.r * cel->cel_frand(0.78, 0.86)),
                    (unsigned char)(rgb.g * cel->cel_frand(0.66, 0.75)),
                    (unsigned char)(rgb.b * cel->cel_frand(0.44, 0.56)));

        zonal = cel->cel_frand(5.0, 9.0);
        scale = cel->cel_frand(1.6, 2.6);

        shear = cel->cel_frand(1.5, 4.0);
        sweep_exp = cel->cel_frand(1.0, 1.6);

        warp_amt = cel->cel_frand(0.5, 1.1);
        polar_k = cel->cel_frand(1.8, 3.6);               // how tightly the bright hood hugs the poles
        contrast = cel->cel_frand(0.38, 0.62);            // still low -- this is haze, not weather

        mottle_scale = scale * cel->cel_frand(2.5, 4.5);
        mottle_amt = cel->cel_frand(0.04, 0.10);
    }
    catch (...)
    {
        generating_fic_texture = false;
        mtx.unlock();
        return;
    }
    mtx.unlock();

    double u, v, theta, phi, phi_s, sin_theta, cos_theta, nx, ny, nz;
    double ax, ay, az, wx, wy, n, band, t, mottle, psi;
    unsigned int x, y, idx;

    for (y = 0; y < image_height; ++y)
    {
        if (done || abort_load) { generating_fic_texture = false; return; }
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
    procedurally_generated = true;
    touch_gen();
}

void CelestialObject::generate_water_clouds()
{
    if (render_saved_planet(this, [](CelestialObject* working) { working->generate_water_clouds(); })) return;
    // Surface regeneration and the loader can both reach this fallback.
    std::lock_guard<std::mutex> lock(mtx);
    const cel_obj_class cls = typeclass();
    if ((cls != class_planet && cls != class_moon) || !uses_rocky_map(type)
        || type == lavaworld || cloud_map || !surf_map || !surf_map->is_complete()
        || done || abort_load)
    {
        return;
    }

    const Planet* planet = static_cast<Planet*>(this);
    if (planet->preserve_loaded_parameters && (!transparent_clouds || overcast)) return;

    try
    {
        std::unique_ptr<Map> clouds(new Map(this));
        if (clouds->generate_water_cloud_map(this))
        {
            transparent_clouds = true;
            ((Planet*)this)->overcast = false;
            cloud_map = clouds.release();
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Water cloud generation failed for " << name << ": " << error.what() << std::endl;
    }
    generating_fic_texture = false;
}

bool Map::generate_water_cloud_map(CelestialObject *cel)
{
    bool generated = false;
    if (render_saved_planet(cel, [this, &generated](CelestialObject* working)
        {
            generated = generate_water_cloud_map(working);
        })) return generated;
    // It was a humid autumn evening when I had the AI write this code... Indigenous People's Day 2026 (fuck Columbus),
    // and the doors to the house were open so the warm humid Phoenix air was permeating everything. I could hear rain
    // coming and going outside. So here I am testing the new exo-clouds, viewing from the surface of HD 40307 g, this
    // huge planet estimated by the algorithm to be a waterworld, and I'm speedboating around its huge ocean, zooming
    // under clouds and seeing the rain come and go on my screen, hanging out in the eye of a hurricane, and it's all
    // so fitting because the clouds and storms on my screen look exactly how the room feels.
    Planet *p = (Planet*)cel;
    const double pressure = p->get_surface_pressure();
    if (!(p->has_water > 0) || !std::isfinite(p->has_water)
        || !std::isfinite(pressure) || pressure < 611.657)
    {
        return false;
    }

    const double surface_temp = p->estimate_surface_temperature();
    if (!(surface_temp > 0) || !std::isfinite(surface_temp))
    {
        return false;
    }

    // Match the existing approximate pressure correction for water's boiling point.
    // The critical temperature caps this approximation at high pressure.
    const double inverse_boil = 1.0 / (water_freezing + 100.0)
        - (8.314 / 40660.0) * std::log(pressure / 1.0e5);
    const double boiling_temp = inverse_boil > 0 ? std::min(647.096, 1.0 / inverse_boil) : 647.096;
    const LocalClimate climate = p->local_climate();
    Map *surface = cel->surf_map;
    const bool full_ocean = cel->type == waterworld || cel->type == hycean;
    const bool sample_oceans = surface->has_bump_data() && p->m_bump_scale > 0;
    bool liquid_ocean = false;

    // Sample actual ocean elevations when available. Imported color maps without
    // elevation data use the stored water amount and the same regional temperatures.
    for (unsigned long row = 0; row < 32 && !liquid_ocean; ++row)
    {
        const unsigned long sy = row * surface->image_height / 32;
        const double theta = sy * _pi / surface->image_height;
        for (unsigned long col = 0; col < 64; ++col)
        {
            const unsigned long sx = col * surface->image_width / 64;
            if (sample_oceans && !full_ocean)
            {
                const double height = surface->bump_data[sy * surface->image_width + sx]
                    / p->m_bump_scale + 0.5;
                if (!std::isfinite(height) || height >= p->has_water)
                {
                    continue;
                }
            }
            const double phi = sx * 2.0 * _pi / surface->image_width;
            const double local_temp = climate.temperature(sin(theta) * cos(phi), sin(theta) * sin(phi),
                cos(theta), climate.sea_level_m);
            if (local_temp >= water_freezing && local_temp < boiling_temp)
            {
                liquid_ocean = true;
                break;
            }
        }
    }
    if (!liquid_ocean || !cel->fictitious_map_height)
    {
        return false;
    }

    generating_fic_texture = true;
    // Weather varies by real-world hour and planet name, independently of terrain.
    const std::uint64_t weather_hour = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::hours>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    std::uint32_t planet_hash = 2166136261u;
    for (const char *c = cel->name; *c; ++c)
    {
        planet_hash ^= static_cast<unsigned char>(*c);
        planet_hash *= 16777619u;
    }
    std::seed_seq weather_seed
    {
        planet_hash,
        static_cast<std::uint32_t>(weather_hour),
        static_cast<std::uint32_t>(weather_hour >> 32),
        static_cast<std::uint32_t>(0x63a9d27bu)
    };
    std::mt19937 cloud_rng(weather_seed);
    std::uniform_real_distribution<double> offset(0.0, 128.0);
    const double ox = offset(cloud_rng);
    const double oy = offset(cloud_rng);
    const double oz = offset(cloud_rng);

    struct CloudStorm
    {
        Point center;
        Point east;
        Point north;
        double radius;
        double cos_radius;
        double orientation;
        double twist;
        double bend;
        double width;
        bool compact;
    };
    std::vector<CloudStorm> storms;
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> system_count(7, 10);
    const int storm_count = system_count(cloud_rng);
    const double rotation_sign = cel->sidereal_rotational_period < 0 ? -1.0 : 1.0;
    for (int i = 0; i < storm_count; ++i)
    {
        CloudStorm storm;
        storm.compact = (i % 4 == 0);
        storm.radius = storm.compact ? 0.08 + 0.05 * unit(cloud_rng) : 0.14 + 0.10 * unit(cloud_rng);
        storm.cos_radius = cos(storm.radius);
        storm.orientation = 2.0 * _pi * unit(cloud_rng);
        const double hemisphere = (i % 2 == 0) ? 1.0 : -1.0;
        storm.twist = hemisphere * rotation_sign * (5.0 + 3.0 * unit(cloud_rng));
        storm.bend = 0.35 + 0.45 * unit(cloud_rng);
        storm.width = storm.compact ? 0.08 : 0.10 + 0.06 * unit(cloud_rng);

        // Keep systems apart so their outer wisps can merge without piling up cores.
        // These latitude ranges describe an artistic weather pattern, not a climate solver.
        for (int attempt = 0; attempt < 48; ++attempt)
        {
            const double lat = hemisphere * (storm.compact
                ? 0.20 + 0.32 * unit(cloud_rng) : 0.44 + 0.65 * unit(cloud_rng));
            const double lon = 2.0 * _pi * unit(cloud_rng);
            storm.center = Point(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
            storm.east = Point(-sin(lon), cos(lon), 0.0);
            storm.north = Point(-sin(lat) * cos(lon), -sin(lat) * sin(lon), cos(lat));
            bool overlaps = false;
            for (const CloudStorm& other : storms)
            {
                const double separation_cos = storm.center.x * other.center.x
                    + storm.center.y * other.center.y + storm.center.z * other.center.z;
                if (separation_cos > cos(0.88 * (storm.radius + other.radius)))
                {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps)
            {
                storms.push_back(storm);
                break;
            }
        }
    }

    image_height = cel->fictitious_map_height;
    image_width = image_height * 2;
    allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];
    lat_scale = image_height / _pi;
    lon_scale = image_width / (2.0 * _pi);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;

    auto smooth = [](double low, double high, double value) -> double
    {
        const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    auto octaves_for = [this](double scale, int limit) -> int
    {
        // Stop the finest octave before its features fall below roughly two pixels.
        const double ratio = image_height / (2.0 * _pi * scale);
        return std::clamp(1 + (int)floor(log2(std::max(1.0, ratio))), 1, limit);
    };
    const int body_octaves = octaves_for(4.0, 5);
    const int detail_octaves = octaves_for(18.0, 4);
    const int wisp_octaves = octaves_for(36.0, 4);
    const int depth_octaves = octaves_for(2.6, 4);
    const int cell_octaves = octaves_for(30.0, 3);

    for (unsigned long y = 0; y < image_height; ++y)
    {
        if (done || abort_load)
        {
            return false;
        }
        const double theta = y * inv_lat_scale;
        const double ring_radius = sin(theta);
        const double nz = cos(theta);
        for (unsigned long x = 0; x < image_width; ++x)
        {
            const double phi = x * inv_lon_scale;
            const double nx = ring_radius * cos(phi);
            const double ny = ring_radius * sin(phi);

            double flow_x = nx;
            double flow_y = ny;
            double flow_z = nz;
            double storm_shape = 0.0;
            double storm_weight = 0.0;
            for (const CloudStorm& storm : storms)
            {
                const double axis_dot = std::clamp(nx * storm.center.x
                    + ny * storm.center.y + nz * storm.center.z, -1.0, 1.0);
                if (axis_dot <= storm.cos_radius)
                {
                    continue;
                }

                // Angular distance and a tangent basis work across the map seam and poles.
                const double angle = acos(axis_dot);
                const double radius = angle / storm.radius;
                const double envelope = 1.0 - smooth(0.72, 1.0, radius);
                const double east = nx * storm.east.x + ny * storm.east.y + nz * storm.east.z;
                const double north = nx * storm.north.x + ny * storm.north.y + nz * storm.north.z;
                const double tangent_scale = angle > 1.0e-8
                    ? angle / (sin(angle) * storm.radius) : 1.0 / storm.radius;
                const double winding = storm.twist * (1.0 - radius) * (1.0 - radius);
                const double turn_cos = cos(winding);
                const double turn_sin = sin(winding);
                const double turned_east = turn_cos * east - turn_sin * north;
                const double turned_north = turn_sin * east + turn_cos * north;
                const double orientation_cos = cos(storm.orientation);
                const double orientation_sin = sin(storm.orientation);
                const double u = tangent_scale * (orientation_cos * turned_east - orientation_sin * turned_north);
                const double v = tangent_scale * (orientation_sin * turned_east + orientation_cos * turned_north);

                // Pull the underlying noise around the same vortex as its cloud bands.
                // Both displacement and its slope fade out at the system boundary.
                flow_x += envelope * (axis_dot * storm.center.x
                    + turned_east * storm.east.x + turned_north * storm.north.x - nx);
                flow_y += envelope * (axis_dot * storm.center.y
                    + turned_east * storm.east.y + turned_north * storm.north.y - ny);
                flow_z += envelope * (axis_dot * storm.center.z
                    + turned_east * storm.east.z + turned_north * storm.north.z - nz);

                const double front_distance = u - storm.bend * (v * v - 0.15);
                double bands = 1.0 - smooth(storm.width * 0.5, storm.width * 1.8, fabs(front_distance));
                double head;
                if (storm.compact)
                {
                    // Two wound arms and a small clear eye within a cloud shield.
                    const double eye = smooth(0.035, 0.11, radius);
                    head = 0.85 * exp(-radius * radius / 0.075);
                    bands = std::max(bands, head) * eye;
                }
                else
                {
                    // A broad cloud head feeds into one long curved frontal tail.
                    bands *= smooth(-0.40, -0.05, v);
                    const double head_u = (u + 0.10) / 0.30;
                    const double head_v = (v + 0.18) / 0.43;
                    head = exp(-head_u * head_u - head_v * head_v);
                    bands = std::max(bands, head);
                }
                storm_shape = std::max(storm_shape, envelope * bands);
                storm_weight = std::max(storm_weight, envelope);
            }

            // All fields live in 3D, so longitude wraps and polar samples converge.
            const double warp_x = fBm(flow_x * 2.0 + ox, flow_y * 2.0 + oy, flow_z * 2.0 + oz, 3, 2.0, 0.5) - 0.5;
            const double warp_y = fBm(flow_x * 2.0 + ox + 37.1, flow_y * 2.0 + oy + 11.7,
                flow_z * 2.0 + oz + 53.3, 3, 2.0, 0.5) - 0.5;
            const double warp_z = fBm(flow_x * 2.0 + ox + 73.9, flow_y * 2.0 + oy + 61.2,
                flow_z * 2.0 + oz + 19.4, 3, 2.0, 0.5) - 0.5;
            const double wx = flow_x + 0.35 * warp_x;
            const double wy = flow_y + 0.35 * warp_y;
            const double wz = flow_z + 0.35 * warp_z;
            const double broad = fBm(wx * 4.0 + ox, wy * 4.0 + oy, wz * 4.0 + oz,
                body_octaves, 2.0, 0.5);
            const double detail = fBm(wx * 18.0 + ox + 17.2, wy * 18.0 + oy + 49.6,
                wz * 18.0 + oz + 31.8, detail_octaves, 2.0, 0.5);

            // Unequal scales stretch fine noise into filaments, with a soft fringe
            // around broad cloud patches and along the wound fronts.
            const double filament = fBm(wx * 9.0 + ox + 81.3, wy * 9.0 + oy + 24.5,
                wz * 36.0 + oz + 67.9, wisp_octaves, 2.0, 0.5);
            const double body = smooth(0.48, 0.57, broad + 0.22 * (detail - 0.5));
            const double fringe = smooth(0.42, 0.50, broad) * smooth(0.49, 0.60, filament);
            const double organized = storm_weight * smooth(0.12, 0.80,
                storm_shape + 0.30 * (detail - 0.5) + 0.16 * (filament - 0.5));

            // Thickness varies independently of the cloud footprint. Sample the same
            // wound coordinates so variations follow the fronts instead of cutting across them.
            const double column = smooth(0.32, 0.68,
                fBm(wx * 2.6 + ox + 43.7, wy * 2.6 + oy + 91.1, wz * 2.6 + oz + 7.9,
                    depth_octaves, 2.0, 0.5));
            const double cells = smooth(0.34, 0.66,
                fBm(wx * 30.0 + ox + 29.3, wy * 30.0 + oy + 5.7, wz * 30.0 + oz + 113.2,
                    cell_octaves, 2.0, 0.5));
            const double body_depth = (0.55 + 3.80 * column) * (0.55 + 0.80 * cells);
            const double wisp_depth = (0.12 + 0.65 * column) * (0.80 + 0.40 * cells);
            const double storm_depth = (1.20 + 3.20 * column + 1.50 * storm_shape) * (0.70 + 0.60 * cells);

            // Convert dimensionless optical depth to opacity. Fractional coverage keeps
            // feathered edges soft even when the interior of a cloud becomes opaque.
            const double body_opacity = body * (1.0 - exp(-body_depth));
            const double wisp_opacity = 0.65 * fringe * (1.0 - exp(-wisp_depth));
            const double storm_opacity = organized * (1.0 - exp(-storm_depth));
            const double background_opacity = std::max(body_opacity, wisp_opacity) * (1.0 - 0.80 * storm_weight);
            const double opacity = std::clamp(std::max(background_opacity, storm_opacity), 0.0, 1.0);
            // Grayscale is premultiplied white: the existing map compositor also reads it as alpha.
            const unsigned char brightness = (unsigned char)(255.0 * opacity + 0.5);
            const unsigned long idx = y * image_width + x;
            red_data[idx] = brightness;
            green_data[idx] = brightness;
            blue_data[idx] = brightness;
        }
    }

    procedurally_generated = true;
    touch_gen();
    return true;
}

void Map::generate_stellar_map(CelestialObject *cel)
{
    assert(cel->typeclass() == class_star);
    double T_eff, mass_solar, f_obl, radius_m, mass_g, surface_g, logg_cgs, p_rot_days, tau_conv_days, rossby, activity, spot_dT, gd_beta, gd_mean,
        scale_height, d_granule, granule_correction, cells_across, gran_scale, gran_nyquist, gran_amp, gran_dT, spot_base_deg, net_scale, plage_gain,
        net_gain, mean_spot_rad, edge_scale, edge_amount, fil_scale, bv_at_teff, ref_max, inv_ref_max;
    const double atomic_mass_unit = 1.66053906660e-27;                  // kg
    std::vector<Point> spot_axis;
    std::vector<double> spot_radius;
    bool fully_convective, convective_envelope, spotted, granulated, polar_regime, has_faculae;
    Star *star = static_cast<Star*>(cel);
    Color wd_linear;
    const bool has_wd_color = star->resolve_wd_color(wd_linear);
    int num_groups, num_spots;
    Color ref_col;
    Map *fac = nullptr;

    // Blackbody B-V, to vary the hue according to local temperature.
    auto bv_of = [](double T) -> double
    {
        return log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
    };
    
    try
    {
        cel->randomize();

        std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
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
        T_eff = (cel->temperature > 0) ? cel->temperature : Star::temperature_from_BV(cel->BV_color);
        if (!(T_eff > 0) || isnan(T_eff) || isinf(T_eff)) T_eff = sun_temp;

        mass_solar = cel->mass / solar_mass;
        fully_convective = (mass_solar > 0 && mass_solar < 0.35);
        convective_envelope = (T_eff < 7000.0);
        spotted = (regime == regime_stellar) && convective_envelope;

        radius_m = (regime == regime_degenerate) ? Star::degenerate_radius(cel->mass)
                                                        : cel->volumetric_mean_radius;
        if (!(radius_m > 0)) radius_m = solar_radius;
        mass_g = (cel->mass > 0) ? cel->mass : solar_mass;
        surface_g = G * mass_g / (radius_m * radius_m);
        logg_cgs = log10(fmax(1e-6, surface_g) * 100.0);

        p_rot_days = cel->sidereal_rotational_period / oneday;
        tau_conv_days = 12.0 * pow(fmax(3000.0, fmin(7000.0, T_eff)) / sun_temp, -2.5);
        rossby = (p_rot_days > 0) ? (p_rot_days / tau_conv_days)
                                        : ((logg_cgs < 3.5) ? cel->cel_frand(2.5, 9.0) : cel->cel_frand(0.4, 3.0));
        activity = fmin(1.0, 0.35 / fmax(0.05, rossby));
        spot_dT = fmax(150.0, fmin(0.541 * T_eff - 1323.0, 0.45 * T_eff));
        gd_beta = convective_envelope ? 0.08 : 0.25;
        f_obl = fmax(0.0, fmin(0.45, cel->oblateness));
        gd_mean = 1.0 - 4.0 * f_obl / 3.0;

        scale_height = kB * T_eff / (1.3 * atomic_mass_unit * surface_g);
        d_granule = 10.0 * scale_height;

        granule_correction = 8;

        if (d_granule > 0.5 * radius_m) d_granule = 0.5 * radius_m;
        cells_across = 2.0 * radius_m / fmax(1.0, d_granule) * granule_correction;

        granulated = (regime == regime_stellar) && convective_envelope;
        gran_scale = cells_across / _pi;
        gran_nyquist = (double)image_width / (6.0 * _pi) * granule_correction;
        gran_amp = 1.0;
        if (gran_scale > gran_nyquist)
        {
            gran_amp = fmax(0.25, gran_nyquist / gran_scale);
            gran_scale = gran_nyquist;
        }

        gran_dT = granulated
            ? T_eff * 0.035 * sqrt(fmax(0.5, fmin(1.8, T_eff / sun_temp))) * gran_amp : 0.0;

        spot_base_deg = 0.35 + 7.0 * activity;
        num_groups = spotted ? (int)(activity * 7.0 + cel->cel_frand(0.8, 1.5)) : 0;
        polar_regime = (rossby < 0.12);

        for (int gi = 0; gi < num_groups; gi++)
        {
            if (done || abort_load) { generating_fic_texture = false; return; }
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
        num_spots = (int)spot_axis.size();

        has_faculae = spotted && (activity > 0.02);
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
        net_scale = fmax(0.7, gran_scale / 20.0);
        plage_gain = fmin(0.55, 0.20 + 0.5 * activity);           // excess over the photosphere
        net_gain = fmin(0.30, 0.08 + 0.35 * activity);

        mean_spot_rad = fiftyseventh * fmax(0.2, spot_base_deg);
        edge_scale = fmax(4.0, 2.5 / mean_spot_rad);
        edge_amount = cel->cel_frand(0.45, 0.7);
        fil_scale = edge_scale * 3.5;                         // penumbral filaments

        // Blackbody B-V, to vary the hue according to local temperature.
        auto bv_of = [](double T) -> double
        {
            return log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
        };

        bv_at_teff = bv_of(T_eff);
        ref_col = has_wd_color
            ? wd_linear
            : Color::color_from_magnitude_indices(0, cel->BV_color);
        ref_max = fmax(ref_col.red, fmax(ref_col.green, ref_col.blue));
        inv_ref_max = (ref_max > 0) ? (1.0 / ref_max) : 1.0;
    }
    catch (...)
    {
        generating_fic_texture = false;
        mtx.unlock();
        return;
    }
    mtx.unlock();

    double theta, phi, sin_theta, nx, ny, nz;
    double T_local, gd_factor, umbra, dist, warped;
    unsigned int x, y, idx;

    for (y = 0; y < image_height; ++y)
    {
        theta = ((double)y / image_height) * _pi;            // 0 at the north pole, Pi at the south pole
        sin_theta = sin(theta);
        
        gd_factor = pow((1.0 - 2.0 * f_obl * sin_theta * sin_theta) / gd_mean, gd_beta);

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

            double lum = pow(T_local / T_eff, 1.6);
            RGB3 rgb;
            if (has_wd_color)
            {
                rgb = wd_srgb_from_linear(Color(wd_linear.red * lum,
                    wd_linear.green * lum, wd_linear.blue * lum));
            }
            else
            {
                double bv_local = cel->BV_color + (bv_of(T_local) - bv_at_teff);
                rgb = Color::rgb_from_color(
                    Color::color_from_magnitude_indices(0, bv_local), lum * inv_ref_max);
            }

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
    procedurally_generated = true;
    touch_gen();
}

void alienorum::Map::generate_ring_map(CelestialObject *cel, int res, double rir, double mo, Map *xmap)
{
    assert(cel);
    cel_obj_class cls = cel->typeclass();
    assert(cls == class_planet);

    try
    {
        cel->ring_map = this;
        if (xmap) cel->ringx_map = xmap;
        else xmap = cel->ringx_map;

        assert(xmap);
        cel->randomize();

        std::this_thread::sleep_for(std::chrono::milliseconds((int)frand(13,29)));
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
    }
    catch (...)
    {
        generating_fic_texture = false;
        mtx.unlock();
        return;
    }
    mtx.unlock();
    generating_fic_texture = false;

    RGB3 rgb, xrgb;
    int x, y, idx;
    int inx = rir * image_width;

    double red = cel->cel_frand(15, 60), dred = 0, oe = 1, doe = 0;
    const double redstep = 0.1, oestep = 0.00666;

    double inv_img_wid = 1.0 / image_width;
    for (x=0; x<image_width; x++)
    {
        if (done || abort_load) { generating_fic_texture = false; return; }
        double fx = inv_img_wid * x;

        // keep colors pale like Saturn, just change redness
        rgb.r = 250;
        rgb.g = 250 - 0.5*red;
        rgb.b = 240 - red;
        double sigma = sigmoid((double)(x-inx) * 0.05);
        xrgb.r = xrgb.g = xrgb.b = (sigma >= 0.05)
            ? (255 - (255.0 * mo
                * sigma                                 // inner bound
                * pow(1.0-fx, 0.1)                      // taper outer bound
                * pow(cel->cel_frand(0.4,0.6), oe)      // detail
              ))
            : 255;
        // std::cout << cel->name << " ring generate " << x << "/" << image_width << " σ=" << sigma << " xrgb=" << (int)(xrgb.g) << std::endl;

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
    procedurally_generated = true;
    touch_gen();
    xmap->touch_gen();
}

void alienorum::Map::_map_resample_bump_regen_rocky(CelestialObject *cel)
{
    if (render_saved_planet(cel, [this](CelestialObject* working) { _map_resample_bump_regen_rocky(working); })) return;
    surface_materials.clear();
    unsigned char *lred = red_data, *lgreen = green_data, *lblue = blue_data;
    red_data = green_data = blue_data = nullptr;
    if (lred  ) delete[] lred;
    if (lgreen) delete[] lgreen;
    if (lblue ) delete[] lblue;
    resample_bump_data(cel->fictitious_map_height);
    if (cel->type == lavaworld)
    {
        generate_lava_map(cel);
        if (!cel->night_map)
        {
            cel->night_map = new Map(cel);
        }
        cel->night_map->generate_lava_map(cel, true);
    }
    else
    {
        generate_rocky_map(cel);
        if (cel->night_map && !cel->has_real_maps && cel->typeclass() != class_star)
        {
            delete cel->night_map;
            cel->night_map = nullptr;
        }
    }
}

void alienorum::Map::mark_for_map_regen(CelestialObject *cel, bool discard_bump)
{
    if (abort_load.load()) return;
    if (discard_bump && bump_data)
    {
        double *old_bump = bump_data;
        bump_data = nullptr;
        delete[] old_bump;
    }

    if (this == cel->surf_map && bump_data && uses_rocky_map(cel->type))
    {
        gen = 0;
        mcel = cel;
        spawn_texture_task([map = this, cel]() { map->_map_resample_bump_regen_rocky(cel); });
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
    bool is_fluid = orbiter && (orbiter->type == waterworld || orbiter->type == hycean
        || uses_gaseous_map(orbiter->type) || orbiter->type == star);
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
    lsyscache.clear();

    int j=0;
    for (int i=ncelobjs-1; i>0; i--)
    {
        if (cels[i] == cel) return false;
        j++;
        if (j > 13) break;
    }

    cels[ncelobjs] = cel;
    cel->seqno = ncelobjs;
    ncelobjs++;
    cels[ncelobjs] = 0;
    if (cel->typeclass() == class_planet) static_cast<Planet*>(cel)->sync_host_statistics();
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
    std::ifstream input(filename);
    if (!input) return nullptr;
    std::vector<OsculatingElement> samples;
    long int i = -1;
    std::string line;
    bool reading = false;
    char *variable;
    while (std::getline(input, line))
    {
        char* buffer = line.data();
        if (strstr(buffer, "$$SOE")) reading = true;
        else if (strstr(buffer, "$$EOE")) break;
        else if (reading)
        {
            if (buffer[0] >= '0' && buffer[0] <= '9')
            {
                samples.emplace_back();
                i++;
                samples[i].epoch = atof(buffer);
            }
            else if (i >= 0)
            {
                if ((variable = strstr(buffer, "EC="))) samples[i].eccentricity = atof(&variable[3]);
                if ((variable = strstr(buffer, "IN="))) samples[i].inclination = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "OM="))) samples[i].ascending_node = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "W ="))) samples[i].arg_perifocus = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "Tp="))) samples[i].T_periapsis = atof(&variable[3]);
                if ((variable = strstr(buffer, "MA="))) samples[i].mean_anomaly = atof(&variable[3]) * fiftyseventh;
                if ((variable = strstr(buffer, "A ="))) samples[i].semimajor_axis = atof(&variable[3]) * 1000;
                if ((variable = strstr(buffer, "PR="))) samples[i].period = atof(&variable[3]);
            }
        }
    }

    if (input.bad() || samples.empty()) return nullptr;
    for (size_t j = 1; j < samples.size(); ++j)
    {
        if (!(samples[j].epoch > samples[j - 1].epoch)) return nullptr;
    }
    auto result = std::make_unique<OsculatingElement[]>(samples.size());
    std::copy(samples.begin(), samples.end(), result.get());
    if (elements_read) *elements_read = samples.size();
    return result.release();
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

        const char* plusminus = strchr(tzcstr, '+');
        if (!plusminus) plusminus = strchr(tzcstr, '-');
        if (!plusminus) throw 0xbadda7a;

        int sign = (*plusminus == '-') ? -1 : 1;

        int hours = atoi(&plusminus[1]);
        const char* colon = strchr(plusminus, ':');
        if (!colon) throw 0xbadda7a;

        int minutes = atoi(&colon[1]);

        tz = (60 * minutes + 3600 * hours) * sign;

        if (strchr(tzcstr, 'D')) dst_rule = dst_us_ca;
        else if (strchr(tzcstr, 'E')) dst_rule = dst_eu_uk;
        else if (strchr(tzcstr, 'A')) dst_rule = dst_au;
        else if (strchr(tzcstr, 'Z')) dst_rule = dst_nz;
        else dst_rule = dst_none;
    }
    catch (...)
    {
        ;
    }
}

// TODO: also scale up if partial atmosphere.
void alienorum::AtmosphereComposition::enforce_integrity()
{
    // A negative portion is not a thin atmosphere but a nonsensical one, and it hides itself in
    // the sum below: a negative and a positive can total exactly 1 while neither can exist. Floor
    // them before anything is measured, so the normalization works on quantities that are real.
    if (H2_portion   < 0) H2_portion   = 0;
    if (He_portion   < 0) He_portion   = 0;
    if (N2_portion   < 0) N2_portion   = 0;
    if (O2_portion   < 0) O2_portion   = 0;
    if (O3_portion   < 0) O3_portion   = 0;
    if (CO2_portion  < 0) CO2_portion  = 0;
    if (CH4_portion  < 0) CH4_portion  = 0;
    if (SO2_portion  < 0) SO2_portion  = 0;
    if (H2O_portion  < 0) H2O_portion  = 0;
    if (H2S_portion  < 0) H2S_portion  = 0;
    if (HCN_portion  < 0) HCN_portion  = 0;
    if (NH3_portion  < 0) NH3_portion  = 0;
    if (C2H6_portion < 0) C2H6_portion = 0;
    if (N2O_portion  < 0) N2O_portion  = 0;
    if (CO_portion   < 0) CO_portion   = 0;
    if (Ar_portion   < 0) Ar_portion   = 0;

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

void alienorum::AtmosphereComposition::generate_fictitious_venusian()       // also covers Mars-type atmospheres
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
    if (cel->type == hycean)
    {
        generate_fictitious_gas_giant();
        return;
    }

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

void alienorum::AtmosphereComposition::generate_fictitious_for_planet(cel_obj_type t)       // does NOT cover habitable atmospheres - that's a separate call.
{
    if (t == gas_giant || t == hycean) generate_fictitious_gas_giant();
    else if (t == ice_giant) generate_fictitious_ice_giant();
    else if (t == rocky) generate_fictitious_venusian();
    else if (t == icy) generate_fictitious_titanean();
}
