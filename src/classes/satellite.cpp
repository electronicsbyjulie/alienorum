
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <unordered_map>
#include "satellite.h"
#include "serial.h"
#include "../vendor/sgp4/SGP4.h"

using namespace alienorum;

std::vector<SatSource> sat_sources;
std::vector<SatRecord> sat_data;
std::map<uint32_t, SatSource*> best_source;

struct Satellite::Sgp4State
{
    elsetrec record{};
};

static double satellite_epoch_seconds(const std::string& iso_epoch)
{
    double seconds = from_iso_string(iso_epoch, "%Y-%m-%dT%H:%M:%S");
    if (iso_epoch.size() <= 19 || iso_epoch[19] != '.') return seconds;
    double fraction = 0, place = .1;
    for (size_t i = 20; i < iso_epoch.size() && iso_epoch[i] >= '0' && iso_epoch[i] <= '9'; ++i)
    {
        fraction += (iso_epoch[i] - '0') * place;
        place *= .1;
    }
    return seconds + fraction;
}

Satellite::Satellite()
{
    _class = class_satellite;
    type = artificial;
}

void Satellite::initialize_sgp4()
{
    if (!orbit || !std::isfinite(gp_epoch_seconds) || !(mean_motion > 0) ||
        !(orbit->eccentricity >= 0 && orbit->eccentricity < 1)) return;
    constexpr double minutes_per_day = 1440;
    const double epoch = (gp_epoch_seconds - J2000_TIME_T) / oneday + (J2000 - 2433281.5);
    const double motion_unit = 2 * _pi / minutes_per_day;
    auto model = std::make_shared<Sgp4State>();
    if (SGP4Funcs::sgp4init(wgs72, 'i', "00000", epoch, bstar,
            gp_motion_dot * motion_unit / minutes_per_day,
            gp_motion_ddot * motion_unit / (minutes_per_day * minutes_per_day),
            orbit->eccentricity, orbit->arg_periapsis, orbit->inclination, orbit->mean_anomaly,
            mean_motion * 60, orbit->ascending_node, model->record) && !model->record.error)
    {
        sgp4_state = std::move(model);
    }
}

void Satellite::update_location(double tmnow)
{
    if (!orbit || !orbit->center) return;
    Rotation center_equator = orbit->center->location.equatorial_plane;
    if (angular_radius && !orbit->center->onscreen && orbit->center->seqno >= 0 &&
        angular_radius[orbit->center->seqno] <= 1)
        angular_radius[orbit->center->seqno] = 1.1;                             // just enough to get it going without slowing down the render.
    if (!sgp4_state && std::isfinite(gp_epoch_seconds)) initialize_sgp4();
    if (sgp4_state)
    {
        // A fresh state permits time jumps and backward playback, including deep-space orbits.
        elsetrec record = sgp4_state->record;
        double position[3], velocity[3];
        if (!SGP4Funcs::sgp4(record, (tmnow - gp_epoch_seconds) / 60, position, velocity)) return;
        update_orbit_location(tmnow, &center_equator);
        if (deleted) return;
        // TEME starts at the equinox. The scene uses +Z at RA zero and +Y at the pole.
        const Point equatorial(-1000 * position[1], 1000 * position[2], 1000 * position[0]);
        location.local_position = orbit->center->location.local_position
            + rotate3D(equatorial, center, center_equator.v, -center_equator.a);
        return;
    }
    update_orbit_location(tmnow, &center_equator);
}

json Satellite::to_json()
{
    json towrite = CelestialObject::to_json();

    if (bstar) towrite["bstar"] = bstar;
    if (std::isfinite(gp_epoch_seconds))
    {
        towrite["sgp4"] = {{"epoch_utc_seconds", gp_epoch_seconds}, {"mean_motion_radians_per_second", mean_motion},
            {"mean_motion_dot", gp_motion_dot}, {"mean_motion_ddot", gp_motion_ddot}};
    }

    return towrite;
}

bool Satellite::from_json(json j)
{
    CelestialObject::from_json(j);
    sgp4_state.reset();
    gp_epoch_seconds = NAN;
    try { j.at("bstar").get_to(bstar); } catch (...) { ; }
    if (j.contains("sgp4") && j["sgp4"].is_object())
    {
        const auto& gp = j["sgp4"];
        gp_epoch_seconds = gp.value("epoch_utc_seconds", static_cast<double>(NAN));
        mean_motion = gp.value("mean_motion_radians_per_second", 0.0);
        gp_motion_dot = gp.value("mean_motion_dot", 0.0);
        gp_motion_ddot = gp.value("mean_motion_ddot", 0.0);
    }
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
    if (!fgets(buffer, 16382, fp)) { fclose(fp); return false; }

    int i;
    std::unordered_map<uint32_t, size_t> record_indices;
    record_indices.reserve(sat_data.size());
    for (size_t index = 0; index < sat_data.size(); ++index)
    {
        record_indices.emplace(sat_data[index].NORAD_CAT_ID, index);
    }
    _nsatellites = 0;
    while (fgets(buffer, 16382, fp)) _nsatellites++;
    fseek(fp, 0, SEEK_SET);

    while (fgets(buffer, 16382, fp))
    {
        if (abort_load) { fclose(fp); return false; }
        std::vector<std::string> row = parse_csv_row(buffer);
        i = 0;

        if (row.size() < 17) continue;
        if (!strcmp(row[0].c_str(), "OBJECT_NAME")) continue;               // Ignore headers.
        // std::cout << buffer << std::flush;

        if (is_supplemental)
        {
            uint32_t norad_id = atoi(row[11].c_str());
            norad_catids.push_back(norad_id);
            const auto record = record_indices.find(norad_id);
            if (record != record_indices.end())
            {
                const size_t j = record->second;
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
                if (best_source.find(norad_id) == best_source.end()
                    || !best_source[norad_id]
                    || _nsatellites < best_source[norad_id]->_nsatellites
                    )
                {
                    best_source[norad_id] = this;
                    // if (norad_id == 20580) std::cout << "Best catalog for " << sat_data[j].OBJECT_NAME << " is " << local_name << " (" << _nsatellites << " sats)." << std::endl;
                }
            }
            else
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
                record_indices.emplace(norad_id, sat_data.size());
                sat_data.push_back(sr);
            }
        }
        else
        {
            uint32_t norad_id = atoi(row[2].c_str());
            const auto record = record_indices.find(norad_id);

            if (record == record_indices.end())
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

                record_indices.emplace(norad_id, sat_data.size());
                sat_data.push_back(sr);
            }
            else
            {
                const size_t index = record->second;
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

    int cenidx;

    uint32_t norad_id = sat_data[idx].NORAD_CAT_ID;

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

    const SatRecord& sr = sat_data[idx];
    snprintf(sat->name, sizeof(sat->name), "%s", sr.OBJECT_NAME.c_str());
    sat->namelen = strlen(sat->name);
    if (!sat->orbit) sat->orbit = new Orbit;
    cenidx = -1;
    for (int i = 0; cels[i]; ++i)
    {
        const auto& codes = cels[i]->orbit_center_codes;
        if (std::find(codes.begin(), codes.end(), sr.ORBIT_CENTER) != codes.end())
        {
            cenidx = i;
            break;
        }
    }
    if (cenidx < 0)
    {
        std::cout << "Unable to add satellite: unsupported orbit center." << std::endl << std::flush;
        return false;
    }

    sat->orbit->center = cels[cenidx];
    sat->cenobj = sat->orbit->center->cenobj;
    const double epoch_seconds = satellite_epoch_seconds(sr.EPOCH);
    sat->epoch = sat->orbit->epoch = (epoch_seconds - J2000_TIME_T) / oneday + J2000;

    sat->mass = 1e3;                        // unknown
    sat->volumetric_mean_radius = 5;        // unknown
    sat->absolute_magnitude = 50;           // unknown

    sat->mean_motion = sr.MEAN_MOTION * 2.0 * _pi / oneday;                // convert to radians/second.
    sat->orbit->eccentricity = sr.ECCENTRICITY;
    sat->orbit->inclination = sr.INCLINATION * fiftyseventh;
    sat->orbit->ascending_node = sr.RA_OF_ASC_NODE * fiftyseventh;
    sat->orbit->arg_periapsis = sr.ARG_OF_PERICENTER * fiftyseventh;
    sat->orbit->mean_anomaly = sr.MEAN_ANOMALY * fiftyseventh;
    sat->orbit->period = sr.PERIOD > 0 ? sr.PERIOD * 60 : sr.MEAN_MOTION > 0 ? oneday / sr.MEAN_MOTION : 0;
    sat->orbit->compute_semimajor_axis(sat->mass);
    sat->bstar = sr.BSTAR;
    sat->sgp4_state.reset();
    sat->gp_epoch_seconds = sr.ORBIT_CENTER == "EA" && sr.EPHEMERIS_TYPE == 0 && sr.MEAN_MOTION > 0
        ? epoch_seconds : NAN;
    sat->gp_motion_dot = sr.MEAN_MOTION_DOT;
    sat->gp_motion_ddot = sr.MEAN_MOTION_DDOT;
    sat->initialize_sgp4();

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
