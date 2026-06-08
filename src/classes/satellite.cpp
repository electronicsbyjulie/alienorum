
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

Satellite::Satellite()
{
    _class = class_satellite;
    type = artificial;
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
    if (auto_load) j["AutoLoad"] = auto_load;
    return j;
}

bool SatSource::from_json(json j)
{
    try
    {
        j.at("URL").get_to(url);
        j.at("LocalName").get_to(local_name);
        std::string iso_string;
        j.at("LastAccessed").get_to(iso_string);

        std::istringstream iss(iso_string);
        std::tm tm_struct = {};

        iss >> std::get_time(&tm_struct, "%Y-%m-%d %H:%M:%S");

        if (iss.fail())
        {
            std::cerr << "FAILED to parse datetime " << iso_string << std::endl;
        }
        else
        {
            last_accessed = std::mktime(&tm_struct);
        }
    }
    catch (...)
    {
        #ifdef DEBUG
        assert(false);
        #endif
        return false;
    }

    try { j.at("AutoLoad").get_to(auto_load); } catch (...) { auto_load = false; }

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

    std::fstream fs("catalogs/sat/sources.json", std::ios::out);
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
    csv_header = parse_csv_row(buffer);
    while (fgets(buffer, 16382, fp))
    {
        csv_rows.push_back(parse_csv_row(buffer));
    }
    fclose(fp);
    return true;
}

int SatSource::num_satellites()
{
    return csv_rows.size();
}

std::string SatSource::sat_name(unsigned int idx)
{
    if (idx >= csv_rows.size()) return std::string();
    return csv_rows[idx][0];
}

bool SatSource::populate(Satellite *sat, unsigned int idx)
{
    if (!sat) return false;
    if (idx >= csv_rows.size()) return false;
    if (sat->typeclass() != class_satellite) return false;

    std::vector<std::string> row = csv_rows[0];
    strcpy(sat->name, row[0].c_str());
    if (!sat->orbit) sat->orbit = new Orbit;
    sat->orbit->center = cels[find_object("Earth")];

    std::istringstream iss(row[2]);
    std::tm tm_struct = {};

    iss >> std::get_time(&tm_struct, "%Y-%m-%dT%H:%M:%S");

    if (iss.fail())
    {
        std::cerr << "FAILED to parse epoch " << row[2] << std::endl;
    }
    else
    {
        sat->epoch = sat->orbit->epoch = std::mktime(&tm_struct);
    }

    sat->orbit->eccentricity = atof(row[4].c_str());
    sat->orbit->inclination = atof(row[5].c_str());
    sat->orbit->ascending_node = atof(row[6].c_str());
    sat->orbit->arg_periapsis = atof(row[7].c_str());
    sat->orbit->mean_anomaly = atof(row[8].c_str());
    sat->orbit->mean_anomaly = atof(row[8].c_str());
    sat->bstar = atof(row[14].c_str());

    return true;
}
