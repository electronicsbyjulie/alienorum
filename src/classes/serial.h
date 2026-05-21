#ifndef _AlienorumSerialization
#define _AlienorumSerialization

#include <fstream>
#include "galaxy.h"
#include "star.h"
#include "planet.h"
#include "moon.h"

class Serialization
{
    public:
    static bool save_string(FILE *out_file, std::string str);
    static std::string load_string(FILE *in_file);
    static bool save_all(std::fstream& fs, CelestialObject **cels);
    static bool load_all(std::fstream& fs, CelestialObject **cels, int max = MAX_CELOBJS);
};

#endif
