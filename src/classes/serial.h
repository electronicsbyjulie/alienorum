#ifndef _AlienorumSerialization
#define _AlienorumSerialization

#include <fstream>
#include <functional>
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
        static bool finish_region_saves();
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
            using SnapshotObserver = std::function<void(CelestialObject*, const std::string&)>;
            static json snapshot(CelestialObject **cels, bool only_edited = false, const SnapshotObserver& observer = {});
            static bool save_file(const std::string& path, CelestialObject **cels, bool only_edited, bool background);
            static bool finish_file_saves();
            // Named region pairs share one index and are validated before import.
            static bool save_region_files(const std::string& index_path, CelestialObject** objects, bool only_edited, bool background, bool show_progress = false);
            static bool load_file(const std::string& path, CelestialObject** objects, unsigned int max, bool as_user_added = true);
            static bool load_document(json document, CelestialObject** objects, unsigned int max, bool as_user_added = true);
            static std::string filename_part(const std::string& text);
            static bool save_all(std::fstream &fs, CelestialObject **cels, bool only_edited = false);
            static bool load_all(std::fstream &fs, CelestialObject **cels, unsigned int max, bool as_user_added = true);
    };
}

int find_object(const char *search_term, bool only_stars = false, double mag_limit = 9e29,
    int Levenshtein_requirement = 3, const alienorum::ExactStarSearchIndex *index = nullptr);

#endif

