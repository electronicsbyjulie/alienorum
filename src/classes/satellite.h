#ifndef _Satellite
#define _Satellite

#include "celestial.h"

#define sat_download_interval (3600 * 2)
#define sat_retry_interval 300

class SatSource
{
    public:
    std::string url;
    std::string local_name;
    std::time_t last_accessed = 0;                      // seconds since 1970
    bool auto_load = false;

    json to_json();
    bool from_json(json j);

    static bool read_sources_json();
    static bool update_sources_json();

    bool download_data();
};

class Satellite : public CelestialObject
{
    void update_orbit_location(double tmnow);

    public:

    Satellite();
    void update_location(double tmnow);
    json to_json();
    bool from_json(json j);
};

extern std::vector<SatSource> sat_sources;

#endif