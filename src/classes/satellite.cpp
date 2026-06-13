
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include "satellite.h"
#include "serial.h"

std::vector<SatSource> sat_sources;
std::vector<SatRecord> sat_data;

Satellite::Satellite()
{
    _class = class_satellite;
    type = artificial;
}

void Satellite::update_location(double tmnow)
{
    if (!orbit || !orbit->center) return;
    Rotation center_equator = orbit->center->location.equatorial_plane;
    orbit->center->onscreen = true;
    if (orbit && orbit->period) update_orbit_location(tmnow, &center_equator);
}

json Satellite::to_json()
{
    json towrite = CelestialObject::to_json();

    if (bstar) towrite["bstar"] = bstar;

    return towrite;
}

bool Satellite::from_json(json j)
{
    CelestialObject::from_json(j);
    try { j.at("bstar").get_to(bstar); } catch (...) { ; }
    return true;
}

json SatSource::to_json()
{
    std::string iso_string;
    std::ostringstream oss;

    std::tm* tm_local = std::localtime(&last_accessed);
    oss << std::put_time(tm_local, "%Y-%m-%d %H:%M:%S");
    iso_string = oss.str();

    json j;
    j["URL"] = url;
    j["LocalName"] = local_name;
    j["LastAccessed"] = iso_string;
    j["Type"] = is_supplemental ? "supplemental" : "master";
    return j;
}

bool SatSource::from_json(json j)
{
    try
    {
        j.at("URL").get_to(url);
        j.at("LocalName").get_to(local_name);

        std::string type;
        j.at("Type").get_to(type);
        is_supplemental = (!strcmp(type.c_str(), "supplemental"));

        std::string iso_string;
        j.at("LastAccessed").get_to(iso_string);
        last_accessed = from_iso_string(iso_string, "%Y-%m-%d %H:%M:%S");
    }
    catch (...)
    {
        #ifdef DEBUG
        assert(false);
        #endif
        return false;
    }

    return true;
}

bool SatSource::read_sources_json()
{
    std::fstream fs("catalogs/sat/sources.json", std::ios::in);
    json j;
    fs >> j;

    int i, n = j.size();
    for (i=0; i<n; i++)
    {
        try
        {
            json j1 = j.at(i);
            SatSource s;
            if (!s.from_json(j1)) return false;
            sat_sources.push_back(s);
        }
        catch (...)
        {
            fs.close();
            #ifdef DEBUG
            assert(false);
            #endif
            return false;
        }
    }

    fs.close();
    return true;
}

bool SatSource::update_sources_json()
{
    json j;
    int i, n = sat_sources.size();

    for (i=0; i<n; i++)
    {
        json j1 = sat_sources[i].to_json();
        j[i] = j1;
    }

    std::filesystem::path bak_name = "catalogs/sat/sources.bak.json";
    std::filesystem::path real_name = "catalogs/sat/sources.json";
    std::error_code ec;

    std::filesystem::remove(bak_name);                          // don't care if doesn't succeed; failure = nothing to delete = expected
    std::filesystem::rename(real_name, bak_name, ec);
    if (ec)
    {
        std::cerr << "ERROR - failed to back up sources.json." << std::endl << std::flush;
        return false;
    }

    std::fstream fs(real_name.c_str(), std::ios::out);
    fs << j.dump(4);
    fs.close();

    return true;
}

std::string SatSource::csv_fname()
{
    return std::string("catalogs/sat/") + local_name + std::string(".csv");
}

int SatSource::data_age_hours()
{
    std::string csvfname = csv_fname();
    if (file_exists(csvfname.c_str()))
    {
        std::filesystem::file_time_type ft = std::filesystem::last_write_time(csvfname.c_str());
        auto system_tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t mt = std::chrono::system_clock::to_time_t(system_tp);
        if (last_accessed < mt) last_accessed = mt;
    }
    std::time_t now = std::time(nullptr);
    std::time_t age = now - last_accessed;

    return age/3600;
}

bool SatSource::download_data()
{
    std::string outfname = csv_fname();
    std::time_t age = data_age_hours() * 3600;

    // Under no circumstances should the code ever attempt to access the same remote file twice in two hours.
    // See: https://celestrak.org/NORAD/documentation/gp-data-formats.php#addendum
    if (age < sat_download_interval)
    {
        std::cout << outfname << " is already current." << std::endl << std::flush;
        return true;
    }

    curlpp::Cleanup cleanup;
    curlpp::Easy request;

    request.setOpt(new curlpp::options::Url(url));
    request.setOpt(new curlpp::options::Timeout(60));
    request.setOpt(new curlpp::options::FollowLocation(true));

    std::list<std::string> headers;
    headers.push_back("User-Agent: Alienorum (https://github.com/electronicsbyjulie/alienorum)");
    request.setOpt(new curlpp::options::HttpHeader(headers));

    std::ostringstream response;
    request.setOpt(new curlpp::options::WriteStream(&response));

    std::cout << "Downloading " << outfname << "..." << std::flush;
    last_accessed = std::time(nullptr);
    request.perform();
    last_accessed = std::time(nullptr);
    long response_code = curlpp::infos::ResponseCode::get(request);

    std::cout << " HTTP/" << response_code << std::endl << std::flush;
    if (response_code == 200)
    {
        std::string data = response.str();

        std::fstream fs(outfname.c_str(), std::ios::out);
        if (!fs)
        {
            std::cerr << "FAILED to write " << outfname << std::endl << std::flush;
            return false;
        }

        fs << data;
        fs.close();
    }
    else
    {
        std::cerr << "FAILED to download satellite orbits: HTTP " << response_code << std::endl
            << response.str() << std::endl << std::flush;
        return false;
    }

    return true;
}

bool SatSource::read_csv_data()
{
    std::string csvfname = csv_fname();
    FILE *fp = fopen(csvfname.c_str(), "r");
    if (!fp) return false;
    char buffer[16384];
    fgets(buffer, 16382, fp);
    std::vector<std::string> csv_header = parse_csv_row(buffer);

    int i, j, n = sat_data.size();
    while (fgets(buffer, 16382, fp))
    {
        std::vector<std::string> row = parse_csv_row(buffer);
        i = 0;

        if (is_supplemental)
        {
            __uint32_t norad_id = atoi(row[11].c_str());
            bool found = false;
            for (j=0; j<n; j++)
            {
                if (sat_data[j].NORAD_CAT_ID == norad_id)
                {
                    if (!sat_data[j].catalog.size()) sat_data[j].catalog = local_name;
                    else sat_data[j].catalog += std::string(" ") + local_name;
                    i = 2;
                    sat_data[j].EPOCH = row[i++];
                    sat_data[j].MEAN_MOTION = atof(row[i++].c_str());
                    sat_data[j].ECCENTRICITY = atof(row[i++].c_str());
                    sat_data[j].INCLINATION = atof(row[i++].c_str());
                    sat_data[j].RA_OF_ASC_NODE = atof(row[i++].c_str());
                    sat_data[j].ARG_OF_PERICENTER = atof(row[i++].c_str());
                    sat_data[j].MEAN_ANOMALY = atof(row[i++].c_str());
                    sat_data[j].EPHEMERIS_TYPE = atoi(row[i++].c_str());
                    sat_data[j].CLASSIFICATION_TYPE = row[i++];
                    i++;
                    sat_data[j].ELEMENT_SET_NO = atoi(row[i++].c_str());
                    sat_data[j].REV_AT_EPOCH = atoi(row[i++].c_str());
                    sat_data[j].BSTAR = atof(row[i++].c_str());
                    sat_data[j].MEAN_MOTION_DOT = atof(row[i++].c_str());
                    sat_data[j].MEAN_MOTION_DDOT = atof(row[i++].c_str());
                    found = true;

                    break;
                }
            }
            if (!found)
            {
                i = 0;
                SatRecord sr;
                sr.catalog = local_name;
                sr.OBJECT_NAME = row[i++];
                sr.OBJECT_ID = row[i++];
                sr.EPOCH = row[i++];
                sr.MEAN_MOTION = atof(row[i++].c_str());
                sr.ECCENTRICITY = atof(row[i++].c_str());
                sr.INCLINATION = atof(row[i++].c_str());
                sr.RA_OF_ASC_NODE = atof(row[i++].c_str());
                sr.ARG_OF_PERICENTER = atof(row[i++].c_str());
                sr.MEAN_ANOMALY = atof(row[i++].c_str());
                sr.EPHEMERIS_TYPE = atoi(row[i++].c_str());
                sr.CLASSIFICATION_TYPE = row[i++];
                sr.NORAD_CAT_ID = atoi(row[i++].c_str());
                sr.ELEMENT_SET_NO = atoi(row[i++].c_str());
                sr.REV_AT_EPOCH = atoi(row[i++].c_str());
                sr.BSTAR = atof(row[i++].c_str());
                sr.MEAN_MOTION_DOT = atof(row[i++].c_str());
                sr.MEAN_MOTION_DDOT = atof(row[i++].c_str());
                sr.ORBIT_CENTER = "EA";
            }
        }
        else
        {
            SatRecord sr;
            sr.OBJECT_NAME = row[i++];
            sr.OBJECT_ID = row[i++];
            sr.NORAD_CAT_ID = atoi(row[i++].c_str());
            sr.OBJECT_TYPE = row[i++];
            sr.OPS_STATUS_CODE = row[i++];
            sr.OWNER = row[i++];
            sr.LAUNCH_DATE = row[i].size() ? from_iso_string(row[i], "%Y-%m-%d") : 0; i++;
            sr.LAUNCH_SITE = row[i++];
            sr.DECAY_DATE = row[i].size() ? from_iso_string(row[i], "%Y-%m-%d") : 0; i++;
            sr.PERIOD = atof(row[i++].c_str());
            sr.INCLINATION = atof(row[i++].c_str());
            sr.APOGEE = atof(row[i++].c_str());
            sr.PERIGEE = atof(row[i++].c_str());
            sr.RCS = atof(row[i++].c_str());
            sr.DATA_STATUS_CODE = row[i++];
            sr.ORBIT_CENTER = row[i++];
            sr.ORBIT_TYPE = row[i++];

            sat_data.push_back(sr);
        }
    }

    fclose(fp);
    return true;
}

bool SatSource::populate(Satellite *sat, unsigned int idx)
{
    if (!sat) return false;
    if (idx >= sat_data.size()) return false;
    if (sat->typeclass() != class_satellite) return false;

    SatRecord sr = sat_data[idx];
    strcpy(sat->name, sr.OBJECT_NAME.c_str());
    if (!sat->orbit) sat->orbit = new Orbit;
    int cenidx;

    if (!strcmp(sr.ORBIT_CENTER.c_str(), "EA")) cenidx = find_object("Earth");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "EM")) cenidx = find_object("Earth");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "SU")) cenidx = 0;
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "SS")) cenidx = 0;
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "MO")) cenidx = find_object("Moon");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "ME")) cenidx = find_object("Mercury");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "VE")) cenidx = find_object("Venus");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "MA")) cenidx = find_object("Mars");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "JU")) cenidx = find_object("Jupiter");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "SA")) cenidx = find_object("Saturn");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "UR")) cenidx = find_object("Uranus");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "NE")) cenidx = find_object("Neptune");
    else if (!strcmp(sr.ORBIT_CENTER.c_str(), "PL")) cenidx = find_object("Pluto");
    else
    {
        std::cout << "Unable to add satellite: unsupported orbit center." << std::endl << std::flush;
        return false;
    }

    sat->orbit->center = cels[cenidx];
    sat->cenobj = sat->orbit->center->cenobj;
    sat->epoch = sat->orbit->epoch = (double)(from_iso_string(sr.EPOCH, "%Y-%m-%dT%H:%M:%S") - J2000_TIME_T) / oneday + J2000;

    sat->mass = 1e3;                        // unknown
    sat->volumetric_mean_radius = 5;        // unknown
    sat->absolute_magnitude = 50;           // unknown

    sat->orbit->eccentricity = sr.ECCENTRICITY;
    sat->orbit->inclination = sr.INCLINATION * fiftyseventh;
    sat->orbit->ascending_node = sr.RA_OF_ASC_NODE * fiftyseventh;
    sat->orbit->arg_periapsis = sr.ARG_OF_PERICENTER * fiftyseventh;
    sat->orbit->mean_anomaly = sr.MEAN_ANOMALY * fiftyseventh;
    sat->orbit->period = sr.PERIOD * 60;
    sat->orbit->compute_semimajor_axis(sat->mass);
    sat->bstar = sr.BSTAR;

    return true;
}
