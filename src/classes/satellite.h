#ifndef _Satellite
#define _Satellite

#include "celestial.h"

#define sat_download_interval (3600 * 2)
#define sat_retry_interval 300

struct SatRecord
{
    std::string catalog = "";
    std::string OBJECT_NAME;
    std::string OBJECT_ID;
    __uint32_t NORAD_CAT_ID;
    std::string OBJECT_TYPE;
    std::string OPS_STATUS_CODE;
    std::string OWNER;
    time_t LAUNCH_DATE;
    std::string LAUNCH_SITE;
    time_t DECAY_DATE;
    double PERIOD;
    double INCLINATION;
    double APOGEE;
    double PERIGEE;
    double RCS;
    std::string DATA_STATUS_CODE;
    std::string ORBIT_CENTER;
    std::string ORBIT_TYPE;
    std::string EPOCH;
    double MEAN_MOTION;
    double ECCENTRICITY;
    double RA_OF_ASC_NODE;
    double ARG_OF_PERICENTER;
    double MEAN_ANOMALY;
    __uint32_t EPHEMERIS_TYPE;
    std::string CLASSIFICATION_TYPE;
    __uint32_t ELEMENT_SET_NO;
    __uint32_t REV_AT_EPOCH;
    double BSTAR;
    double MEAN_MOTION_DOT;
    double MEAN_MOTION_DDOT;
};

class Satellite : public CelestialObject
{
    public:
    double bstar;
    double mean_motion;

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
    bool is_supplemental = false;

    json to_json();
    bool from_json(json j);

    static bool read_sources_json();
    static bool update_sources_json();

    std::string csv_fname();
    int data_age_hours();
    bool download_data();
    bool read_csv_data();
    static bool populate(Satellite* sat, unsigned int idx);
};

extern std::vector<SatSource> sat_sources;
extern std::vector<SatRecord> sat_data;

#endif