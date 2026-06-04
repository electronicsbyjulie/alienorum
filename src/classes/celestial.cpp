
#include <math.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "celestial.h"
#include "star.h"
#include "planet.h"

CelestialObject **cels, *mycenobj = nullptr;
bool *celskip, *discinstead;
double *vmag_cache, *bloomrad_cache, *angular_radius;
CelestialLocation here;
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

    CelestialObject *light_center = orbit->center;
    int i;
    for (i=0; i<5; i++)
    {
        if (light_center->type == rocky || light_center->type == ice_giant || light_center->type == gas_giant)
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
    period = (M_PI+M_PI) * sqrt(semimajor_axis*semimajor_axis*semimajor_axis/(G*mass));
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
    semimajor_axis = pow(G*mass*period*period / (M_PI*M_PI*4), 1.0/3);
}

void Orbit::compute_center_mass(double mm)
{
    double a3_over_gm = period / (M_PI+M_PI);
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
    double result = std::fmod(find_angle(relloc.z, -relloc.x) - seen_equinox, M_PI*2);
    if (result < 0) result += M_PI*2;
    return result;
}

double CelestialObject::Decl_as_radians(CelestialLocation seen_from)
{
    Point relloc = (location.system_center - seen_from.system_center) + (location.local_position - seen_from.local_position);
    relloc = rotate3D(relloc, center, seen_from.equatorial_plane.v, seen_from.equatorial_plane.a);
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
    towrite["obliquity"] = obliquity * fiftyseven;
    towrite["location"] = location.to_json();
    towrite["mass"] = mass;
    towrite["!name"] = name;                    // want this to alphabetize to the top.
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

    return true;
}

void CelestialObject::update_orbit_location(double tmnow, Rotation* crp)
{
    if (!orbit || !orbit->center || !orbit->period) return;
    location.system_center = orbit->center->location.system_center;
    location.local_system_plane = orbit->center->location.local_system_plane;

    // Calculate orbit radians per second and seconds since epoch
    double rads_sec = (M_PI * 2) / orbit->period;
    double seconds_since_epoch = (tmnow - J2000_TIME_T) + ((J2000 - epoch)*oneday);

    // Precess the ascending node and process the arg peri
    double node_adjustment = seconds_since_epoch * -orbit->prec_node;
    double peri_adjustment = seconds_since_epoch *  orbit->proc_argperi;
    double node = orbit->ascending_node + node_adjustment;
    double argperi = orbit->arg_periapsis + peri_adjustment;

    // Calculate current Mean Anomaly
    double M = orbit->mean_anomaly + rads_sec * seconds_since_epoch - node_adjustment - peri_adjustment;
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

    Point orbit_pole = yaxis;
    orbit_pole = rotate3D(orbit_pole, center, Point(sinO, 0, -cosO), orbit->inclination);
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
            << " without Laplace plane." << std::endl;
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

bool Map::load_from_jpeg(std::string filename)
{
    struct jpeg_decompress_struct cinfo;
    struct my_jpeg_error_mgr jerr;
    FILE * infile;
    int row_stride;

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

    image_height = cinfo.image_height;
    image_width = cinfo.image_width;
    lat_scale = image_height / M_PI;
    lon_scale = image_width / (M_PI * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;
    long toalloc = image_height * image_width;
    std::cout << "Allocating " << toalloc << " pixels for " << filename << std::endl;
    red_data = new unsigned char[toalloc];
    green_data = new unsigned char[toalloc];
    blue_data = new unsigned char[toalloc];
    allocated = toalloc;

    row_stride = cinfo.output_width * cinfo.output_components;
    jpeg_image_buffer = (*cinfo.mem->alloc_sarray)
            ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    int i, j;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        j = cinfo.output_scanline * image_width;
        assert(j >= 0);
        (void) jpeg_read_scanlines(&cinfo, jpeg_image_buffer, 1);
        for (i=0; i<row_stride; i+=cinfo.output_components)
        {
            assert(j < toalloc);
            red_data[j] = jpeg_image_buffer[0][i];
            green_data[j] = jpeg_image_buffer[0][i+1];
            blue_data[j] = jpeg_image_buffer[0][i+2];
            j++;
        }
    }

    (void) jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    return true;
}

bool Map::load_from_png(std::string filename)
{
    png_structp png_ptr;
    png_infop info_ptr;
    unsigned int sig_read = 0;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type;
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
    image_height = png_get_image_height( png_ptr, info_ptr );
    image_width = png_get_image_width( png_ptr, info_ptr );
    lat_scale = image_height / M_PI;
    lon_scale = image_width / (M_PI * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;

    int bytes_per_pixel = png_get_channels(png_ptr, info_ptr) * (png_get_bit_depth(png_ptr, info_ptr) / 8);

    png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

    if (bytes_per_pixel == 3)
    {
        // RGB
        int toalloc = image_height*bytes_per_row;
        std::cout << "Allocating " << toalloc << " pixels for " << filename << std::endl;
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
        int x, y, i=0;
        for (y=0; y<image_height; y++)
        {
            for (x=0; x<image_width; x++)
            {
                png_bytep pixel = &(row_pointers[y][x * bytes_per_pixel]);
                red_data[i] = pixel[0];
                green_data[i] = pixel[1];
                blue_data[i++] = pixel[2];
            }
        }
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
    }
    else if (bytes_per_pixel == 1)
    {
        // Grayscale.
        int toalloc = image_height*bytes_per_row;
        std::cout << "Allocating " << toalloc << " pixels for " << filename << std::endl;
        red_data = new unsigned char[toalloc];
        green_data = new unsigned char[toalloc];
        blue_data = new unsigned char[toalloc];
        allocated = toalloc;
        int x, y, i=0;
        for (y=0; y<image_height; y++)
        {
            for (x=0; x<image_width; x++)
            {
                png_bytep pixel = &(row_pointers[y][x * bytes_per_pixel]);
                red_data[i] = green_data[i] = blue_data[i] = pixel[0];
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

RGB Map::color_at(double lat, double lon)
{
    RGB result;

    assert(lat_scale > 0);
    assert(lon_scale > 0);

    lon = fmod(lon, M_PI*2);
    if (lon < 0) lon += M_PI*2;
    if (lat < -M_PI_2) lat = -M_PI_2;
    else if (lat > M_PI_2) lat = M_PI_2;
    if (blue_data)
    {
        double xf = lon * lon_scale, yf = (M_PI_2-lat) * lat_scale;
        int x0 = floor(xf), x1 = ceil(xf), y0 = floor(yf), y1 = ceil(yf);
        long y0idx = image_width * y1, y1idx = y0idx + image_width;

        if (y0idx < 0) y0idx = 0;
        if (y0idx > allocated-image_width) y0idx = allocated-image_width;
        if (x0 < 0) x0 = 0;
        if (x0 >= image_width) x0 = image_width-1;

        result.r = red_data[y0idx+x0];
        result.g = green_data[y0idx+x0];
        result.b = blue_data[y0idx+x0];
    }
    else
    {
        result.r = result.g = result.b = 255;
    }
    return result;
}

void Map::generate_rocky_map(int lr, double BV, bool has_water)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    int octaves = 5 + (rand() % 4);
    double lacunarity = frand(1.0, 2.9);
    double gain = has_water ? 0.5 : 2.5;
    double scale = frand(has_water ? 1.5 : 0.2, has_water ? 2.9 : 0.8);             // Controls feature sizes (smaller scale = larger continents)

    Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
    RGB rgb = Color::rgb_from_color(col, -1);

    image_height = lr;
    image_width = image_height * 2;

    allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];
    lat_scale = image_height / M_PI;
    lon_scale = image_width / (M_PI * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;
    std::cout << "Allocated " << allocated << " pixels for fictitious rocky map." << std::endl;

    int vegr, vegg, vegb;
    if (has_water)
    {
        vegr = 224 * pow(frand(0, 1), 0.4);
        vegg = 192 * pow(frand(0, 1), 1.7);
        vegb = 176 * pow(frand(0, 1), 2.9);
        if (vegr < vegg && vegr < vegb) vegb /= 1.8;
        if (vegg < vegr && vegg < vegb) vegg /= 2.1;
        if (vegb < vegr && vegb < vegg) vegb /= 3.7;
    }

    for (int y = 0; y < image_height; ++y)
    {
        // Convert screen pixel coordinates to spherical angles
        double v = (double)y / image_height;
        double theta = v * M_PI; // Latitude angle from 0 to PI

        for (int x = 0; x < image_width; ++x)
        {
            double u = (double)x / image_width;
            double phi = u * 2.0 * M_PI; // Longitude angle from 0 to 2PI

            // Map 2D texture coordinates to a 3D Sphere surface to avoid seam/polar stretching
            double nx = sin(theta) * cos(phi);
            double ny = sin(theta) * sin(phi);
            double nz = cos(theta);

            // Get noise value for this point on the sphere
            double heightValue = fBm(nx * scale, ny * scale, nz * scale, octaves, lacunarity, gain);

            int idx = y * image_width + x;

            if (has_water)
            {
                double r_weight = heightValue;

                // Biome allocation based on height thresholds
                if (heightValue < 0.45)
                {   // Deep Ocean
                    red_data[idx] = 10 * r_weight;
                    green_data[idx] = 30 * r_weight;
                    blue_data[idx] = 120 * r_weight;
                }
                else if (heightValue < 0.50)
                {   // Shallow Coast
                    red_data[idx] = 30 * r_weight;
                    green_data[idx] = 90 * r_weight;
                    blue_data[idx] = 180 * r_weight;
                }
                else if (heightValue < 0.53)
                {   // Beach / Sand
                    red_data[idx] = 220 * r_weight;
                    green_data[idx] = 200 * r_weight;
                    blue_data[idx] = 150 * r_weight;
                }
                else if (heightValue < 0.70)
                {   // Grassland / Lowlands
                    // Don't assume alien vegetation is green!
                    red_data[idx] = vegr * r_weight;
                    green_data[idx] = vegg * r_weight;
                    blue_data[idx] = vegb * r_weight;
                }
                else if (heightValue < 0.85)
                {   // Mountains (Dirt / Rock)
                    red_data[idx] = 110 * r_weight;
                    green_data[idx] = 90 * r_weight;
                    blue_data[idx] = 75 * r_weight;
                }
                else
                {   // Snowy Peaks
                    red_data[idx] = 240 * r_weight;
                    green_data[idx] = 240 * r_weight;
                    blue_data[idx] = 255 * r_weight;
                }
            }
            else
            {
                // Moon or Dead Desert Planet (Grayscale / Basalt / Rust)
                // Let's make an iron-rich desert world (Mars-like)
                double r_weight = heightValue;
                red_data[idx] = (unsigned char)(rgb.r * r_weight + 40);
                green_data[idx] = (unsigned char)(rgb.g * r_weight + 20);
                blue_data[idx] = (unsigned char)(rgb.b * r_weight + 10);
            }
        }
    }
}

void Map::generate_gas_giant_map(int lr, double BV)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    image_height = lr;
    image_width = image_height * 2;

    allocated = image_height * image_width;
    red_data = new unsigned char[allocated];
    green_data = new unsigned char[allocated];
    blue_data = new unsigned char[allocated];
    lat_scale = image_height / M_PI;
    lon_scale = image_width / (M_PI * 2);
    inv_lat_scale = 1.0 / lat_scale;
    inv_lon_scale = 1.0 / lon_scale;
    std::cout << "Allocated " << allocated << " pixels for fictitious gas giant map." << std::endl;

    Color col = Color::color_from_magnitude_indices(BV+bv_correction*2, BV);
    RGB rgb = Color::rgb_from_color(col, -1);

    double variability = frand(0, 0.666);
    int num_bands = rand() % 9 + 7, i;
    RGB bands[num_bands];

    bool add_storm = frand(0, 1) < 0.2;
    double stormlat, stormlon, distToStormX, distToStormY, stormDist = 1e29;

    stormlat = frand(0.3, 0.7);
    stormlon = frand(0, 1);

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

    for (int y = 0; y < image_height; ++y)
    {
        double v = (double)y / image_height;
        double theta = v * M_PI;

        for (int x = 0; x < image_width; ++x)
        {
            double u = (double)x / image_width;
            double phi = u * 2.0 * M_PI;

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
                distToStormX = (u - stormlon) * 2.0 * M_PI;
                distToStormY = (v - stormlat) * M_PI;
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
        }
    }
}

