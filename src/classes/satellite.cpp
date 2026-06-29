
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include "satellite.h"
#include "serial.h"

using namespace alienorum;

std::vector<SatSource> sat_sources;
std::vector<SatRecord> sat_data;
std::map<uint32_t, SatSource*> best_source;

Satellite::Satellite()
{
    _class = class_satellite;
    type = artificial;
}

void Satellite::update_location(double tmnow)
{
    if (!orbit || !orbit->center) return;
    Rotation center_equator = orbit->center->location.equatorial_plane;
    if (!orbit->center->onscreen && angular_radius[orbit->center->seqno] <= 1)
        angular_radius[orbit->center->seqno] = 1.1;                             // just enough to get it going without slowing down the render.
    if (orbit->period) update_orbit_location(tmnow, &center_equator);
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
    json j;
    j["URL"] = url;
    j["LocalName"] = local_name;
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
    std::fstream fs("catalogs" _FILESLASH "sat" _FILESLASH "sources.json", std::ios::in);
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

std::string SatSource::csv_fname()
{
    return std::string("catalogs" _FILESLASH "sat") + _FSSTR + local_name + std::string(".csv");
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
        std::time_t now = std::time(nullptr);
        std::time_t age = now - mt;
        return age/3600;
    }
    else return 1e5;
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

    return download_file(url, outfname);
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
    _nsatellites = 0;
    while (fgets(buffer, 16382, fp)) _nsatellites++;
    fseek(fp, 0, SEEK_SET);

    while (fgets(buffer, 16382, fp))
    {
        std::vector<std::string> row = parse_csv_row(buffer);
        i = 0;

        if (!strcmp(row[0].c_str(), "OBJECT_NAME")) continue;               // Ignore headers.

        if (is_supplemental)
        {
            uint32_t norad_id = atoi(row[11].c_str());
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

                    if (best_source.find(norad_id) == best_source.end()
                        || !best_source[norad_id]
                        || _nsatellites < best_source[norad_id]->_nsatellites
                        )
                    {
                        best_source[norad_id] = this;
                        // if (norad_id == 20580) std::cout << "Best catalog for " << sat_data[j].OBJECT_NAME << " is " << local_name << " (" << _nsatellites << " sats)." << std::endl;
                    }

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
                sat_data.push_back(sr);
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

    SatRecord& sr = sat_data[idx];
    strcpy(sat->name, sr.OBJECT_NAME.c_str());
    if (!sat->orbit) sat->orbit = new Orbit;
    int cenidx;

    uint32_t norad_id = sr.NORAD_CAT_ID;
    if (best_source.find(norad_id) != best_source.end() && best_source[norad_id])
    {
        SatSource *src = best_source[norad_id];
        int h = src->data_age_hours();
        if (h > 6)
        {
            std::cout << src->csv_fname() << " is " << h << " hours old; requesting update..." << std::endl;
            src->download_data();
            src->read_csv_data();
        }
    }

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

    sat->mean_motion = sr.MEAN_MOTION * 2.0 * _pi / oneday;                // convert to radians/second.
    sat->orbit->eccentricity = sr.ECCENTRICITY;
    sat->orbit->inclination = sr.INCLINATION * fiftyseventh;
    sat->orbit->ascending_node = sr.RA_OF_ASC_NODE * fiftyseventh;
    sat->orbit->arg_periapsis = sr.ARG_OF_PERICENTER * fiftyseventh;
    sat->orbit->mean_anomaly = sr.MEAN_ANOMALY * fiftyseventh;
    sat->orbit->period = sr.PERIOD * 60;
    sat->orbit->compute_semimajor_axis(sat->mass);
    sat->bstar = sr.BSTAR;

    if (sat->orbit->center->typeclass() == class_planet || sat->orbit->center->typeclass() == class_moon)
    {
        Planet* pl = (Planet*)sat->orbit->center;

        // Precession of the nodes.
        double p = sat->orbit->semimajor_axis * (1.0 - sat->orbit->eccentricity*sat->orbit->eccentricity);
        double paren = pl->get_equatorial_radius() / p;
        paren *= paren;
        double common_term = pl->J2 * paren * sat->mean_motion;
        double cos_incl = cos(sat->orbit->inclination);
        sat->orbit->prec_node = 1.5 * common_term * cos_incl;

        // Procession of the arg peri
        sat->orbit->proc_argperi = 0.75 * common_term * (5.0 * cos_incl * cos_incl - 1);
    }

    return true;
}
