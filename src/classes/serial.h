#ifndef _AlienorumSerialization
#define _AlienorumSerialization

#include <fstream>
#include "galaxy.h"
#include "star.h"
#include "planet.h"
#include "moon.h"
#include "comet.h"
#include "satellite.h"

namespace alienorum
{
    class Serialization
    {
        public:
        static bool save_all(std::fstream& fs, CelestialObject **cels, bool only_edited = false);
        static bool load_all(std::fstream& fs, CelestialObject **cels, unsigned int max, bool as_user_added = true);
    };
}

int find_object(const char *search_term, bool only_stars = false, double mag_limit = 9e29, int Levenshtein_requirement = 3);

#endif

