#ifndef _Satellite
#define _Satellite

#include "celestial.h"

#define sat_download_interval (3600 * 2)
#define sat_retry_interval 300

class Satellite : public CelestialObject
{
    public:
    double bstar;

    Satellite();
    void update_location(double tmnow);
    json to_json();
    bool from_json(json j);
};

class SatSource
{
    public:
    std::string url = "";
    std::string local_name = "";
    std::time_t last_accessed = 0;                      // seconds since 1970
    bool auto_load = false;

    json to_json();
    bool from_json(json j);

    static bool read_sources_json();
    static bool update_sources_json();

    std::string csv_fname();
    int data_age_hours();
    bool download_data();
    bool read_csv_data();
    int num_satellites();
    std::string sat_name(unsigned int idx);
    bool populate(Satellite* sat, unsigned int idx);

    protected:
    std::vector<std::string> csv_header;
    std::vector<std::vector<std::string>> csv_rows;
};

extern std::vector<SatSource> sat_sources;

#endif