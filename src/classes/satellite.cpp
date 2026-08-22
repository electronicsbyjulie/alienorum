
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
    update_orbit_location(tmnow, &center_equator);
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

    try { j.at("AlwaysCheck").get_to(always_check); } catch (...) { ; }                     // Optional parameter.

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

bool alienorum::SatSource::check_satcat_and_latest()
{
    updating_sats = true;
    bool anything_updated = false;
    int i, n = sat_sources.size();
    for (i=0; i<n; i++)
    {
        if (sat_sources[i].always_check && sat_sources[i].data_age_hours() >= 24)
        {
            sat_sources[i].download_data();
            sat_sources[i].read_csv_data();
            anything_updated = true;
        }
    }
    updating_sats = false;
    return anything_updated;
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
        return (int)floor(file_age(csvfname.c_str())/3600);
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

    // std::cout << "DOWNLOAD FILE " << url << std::endl << std::flush;

    return download_file(url, outfname);
}

bool SatSource::read_csv_data()
{
    std::string csvfname = csv_fname();
    FILE *fp = fopen(csvfname.c_str(), "r");
    if (!fp) return false;
    char buffer[16384];
    char* wgaf = fgets(buffer, 16382, fp);
    std::vector<std::string> csv_header = parse_csv_row(buffer);
    bool do_exist_checks = sat_data.size();                                 // Don't bog down the initial load with expensive std::find_if() calls.

    int i, j, n = sat_data.size();
    _nsatellites = 0;
    while (fgets(buffer, 16382, fp)) _nsatellites++;
    fseek(fp, 0, SEEK_SET);

    while (fgets(buffer, 16382, fp))
    {
        std::vector<std::string> row = parse_csv_row(buffer);
        i = 0;

        if (!strcmp(row[0].c_str(), "OBJECT_NAME")) continue;               // Ignore headers.
        // std::cout << buffer << std::flush;

        if (is_supplemental)
        {
            uint32_t norad_id = atoi(row[11].c_str());
            norad_catids.push_back(norad_id);
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
            uint32_t norad_id = atoi(row[2].c_str());
            int index = 0;

            bool exists = false;
            if (do_exist_checks)
            {
                auto it = std::find_if(sat_data.begin(), sat_data.end(), [norad_id](const SatRecord& sr)
                {
                    return sr.NORAD_CAT_ID == norad_id;
                });
                exists = (it != sat_data.end());
                if (exists)
                {
                    auto idx = std::distance(sat_data.begin(), it);
                    index = idx;
                }
            }

            if (!exists)
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
            else
            {
                sat_data[index].OBJECT_NAME = row[i++];
                sat_data[index].OBJECT_ID = row[i++];
                sat_data[index].NORAD_CAT_ID = atoi(row[i++].c_str());
                sat_data[index].OBJECT_TYPE = row[i++];
                sat_data[index].OPS_STATUS_CODE = row[i++];
                sat_data[index].OWNER = row[i++];
                sat_data[index].LAUNCH_DATE = row[i].size() ? from_iso_string(row[i], "%Y-%m-%d") : 0; i++;
                sat_data[index].LAUNCH_SITE = row[i++];
                sat_data[index].DECAY_DATE = row[i].size() ? from_iso_string(row[i], "%Y-%m-%d") : 0; i++;
                sat_data[index].PERIOD = atof(row[i++].c_str());
                sat_data[index].INCLINATION = atof(row[i++].c_str());
                sat_data[index].APOGEE = atof(row[i++].c_str());
                sat_data[index].PERIGEE = atof(row[i++].c_str());
                sat_data[index].RCS = atof(row[i++].c_str());
                sat_data[index].DATA_STATUS_CODE = row[i++];
                sat_data[index].ORBIT_CENTER = row[i++];
                sat_data[index].ORBIT_TYPE = row[i++];
            }
        }
    }

    fclose(fp);
    return true;
}

bool alienorum::SatSource::contains_sat(uint32_t norad_cat_id)
{
    int i, n = norad_catids.size();
    for (i=0; i<n; i++) if (norad_catids[i] == norad_cat_id) return true;
    return false;
}

bool SatSource::populate(Satellite *sat, unsigned int idx, int hours_threshold)
{
    if (!sat) return false;
    if (idx >= sat_data.size()) return false;
    if (sat->typeclass() != class_satellite) return false;
    if (hours_threshold < 6) hours_threshold = 6;

    SatRecord& sr = sat_data[idx];
    strcpy(sat->name, sr.OBJECT_NAME.c_str());
    if (!sat->orbit) sat->orbit = new Orbit;
    int cenidx;

    uint32_t norad_id = sr.NORAD_CAT_ID;

    // If any source for the indicated sat is newer than the threshold, use it and don't bother to download best.
    bool download_best = true;

    int i, n = sat_sources.size();
    for (i=0; download_best && i<n; i++) if (sat_sources[i].contains_sat(norad_id))
    {
        int h = sat_sources[i].data_age_hours();
        if (h < hours_threshold)
        {
            download_best = false;
            // std::cout << sat_sources[i].csv_fname() << " is " << h << " hours old; skipping update." << std::endl;
        }
    }

    if (download_best && best_source.find(norad_id) != best_source.end() && best_source[norad_id])
    {
        SatSource *src = best_source[norad_id];
        int h = src->data_age_hours();
        if (h >= hours_threshold)
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

    nsatobjs++;
    return true;
}
