#ifndef _AlienorumSerialization
#define _AlienorumSerialization

#include <fstream>
#include <map>
#include <unordered_map>
#include "galaxy.h"
#include "star.h"
#include "planet.h"
#include "moon.h"
#include "comet.h"
#include "satellite.h"

namespace alienorum
{
    // Scoped to one import. Candidate lists retain catalog order; find_object
    // still applies the identifier, component, and constellation rules.
    class ExactStarSearchIndex
    {
        std::map<std::string, std::vector<int>> names;
        std::unordered_map<std::string, std::vector<int>> aliases, gliese;
        std::unordered_map<int, std::vector<int>> greek, numbers;
        std::vector<int> white_dwarfs;

        public:
        ExactStarSearchIndex();
        void add(int index, const Star &star);
        std::vector<int> candidates(const char *term) const;
    };

    class Serialization
    {
        public:
            static bool save_one(std::fstream &fs, CelestialObject *cel, json &allobjs);
            static bool save_all(std::fstream &fs, CelestialObject **cels, bool only_edited = false);
            static bool load_all(std::fstream &fs, CelestialObject **cels, unsigned int max, bool as_user_added = true);
    };
}

int find_object(const char *search_term, bool only_stars = false, double mag_limit = 9e29,
    int Levenshtein_requirement = 3, const alienorum::ExactStarSearchIndex *index = nullptr);

#endif

