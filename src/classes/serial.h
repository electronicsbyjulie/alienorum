#ifndef _AlienorumSerialization
#define _AlienorumSerialization

#include "galaxy.h"
#include "star.h"
#include "planet.h"
#include "moon.h"

// IMPORTANT: Update this when making ANY change to the CelestialObject, Star, Planet, or Galaxy classes.
#define _serial_version 0xb0ad1cea + 260518011

class Serialization
{
    public:
    static bool save_string(FILE *out_file, std::string str);
    static std::string load_string(FILE *in_file);
    static bool save_object(FILE *out_file, CelestialObject *cel);
    static bool save_all(FILE *out_file, CelestialObject **cels);
    static CelestialObject* load_object(FILE *in_file, CelestialObject **cels_for_orbit_center_linkage);
    static bool load_all(FILE *in_file, CelestialObject **cels, int max = MAX_CELOBJS);
};

#endif
