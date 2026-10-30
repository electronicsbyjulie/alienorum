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
        explicit ExactStarSearchIndex(int first_star = 0);
        void add(int index, const Star &star);
        std::vector<int> candidates(const char *term) const;
    };

    class Serialization
    {
        public:
            struct SaveContext
            {
                json header;
                std::unordered_map<std::string, size_t> frames;
            };
            static json compact_record(json record, SaveContext& context);
            static json expand_record(const json& record, const json& header);
            static json compact_document(json records);
            static json expand_document(json document);
            static json output_record(json record);
            static json expand_output_record(json record);
            static bool save_one(std::fstream &fs, CelestialObject *cel, json &allobjs);
            static json snapshot(CelestialObject **cels, bool only_edited = false);
            static bool save_file(const std::string& path, CelestialObject **cels, bool only_edited, bool background);
            static bool finish_file_saves();
            static bool save_all(std::fstream &fs, CelestialObject **cels, bool only_edited = false);
            static bool load_all(std::fstream &fs, CelestialObject **cels, unsigned int max, bool as_user_added = true);
    };
}

int find_object(const char *search_term, bool only_stars = false, double mag_limit = 9e29,
    int Levenshtein_requirement = 3, const alienorum::ExactStarSearchIndex *index = nullptr);

#endif

