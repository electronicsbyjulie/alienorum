#ifndef _Satellite
#define _Satellite

#include "celestial.h"

class SatSource
{
    public:
    std::string url;
    double last_accessed;

    static bool read_sources_json();
    static bool update_sources_json();
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

#endif