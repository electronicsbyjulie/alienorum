#include <cstdio>
#include <iostream>
#include "../src/classes/cat.h"
#include "../src/classes/browncatalog.h"
#include "../src/loaders.h"

using namespace alienorum;

class AuditCatalogReader : public CatalogReader
{
    public:
    using CatalogReader::exorow_read_line;
    using CatalogReader::resolve_or_create_exostar;
    using CatalogReader::add_exoplanet_from_row;
};

int main()
{
    cels = new CelestialObject*[MAX_CELOBJS]();
    Star::load_main_seq_dat();
    read_cons_lines();
    AuditCatalogReader reader;
    const int initial = reader.read_condensed_star_cat();
    if (initial <= 0) return 1;
    ExactStarSearchIndex lookup;
    json report = {{"initial_objects", initial}, {"duplicates_created", json::array()},
        {"new_hosts", json::array()}};
    bool loaded_stars = false;
    unsigned int planets = 0;
    std::map<int, std::vector<int>> planet_ids;
    for (const char* path : {"catalogs/exoplanets_stars.dat", "catalogs/exoplanets_planets.dat"})
    {
        FILE* input = fopen(path, "r");
        if (!input) return 1;
        ExoRow row;
        while (reader.exorow_read_line(input, row))
        {
            const int existing = find_object(row.hostname.c_str(), true, 9e29, 0, &lookup);
            bool created = false;
            Star* host = reader.resolve_or_create_exostar(row, loaded_stars, &created, &lookup);
            if (loaded_stars && host)
                reader.add_exoplanet_from_row(row, host, planet_ids, planets, &lookup);
            if (!created || !host) continue;
            report["new_hosts"].push_back(row.hostname);
            if (existing >= 0 && cels[existing] != host)
            {
                report["duplicates_created"].push_back({{"host", row.hostname},
                    {"original", cels[existing]->name}, {"source", path},
                    {"ultracool", static_cast<Star*>(cels[existing])->stellar_parameters.is_object()
                        ? static_cast<Star*>(cels[existing])->stellar_parameters.value("catalog", "") : ""}});
            }
        }
        fclose(input);
        loaded_stars = true;
    }
    report["duplicate_count"] = report["duplicates_created"].size();
    report["planets_loaded"] = planets;
    const int trappist = find_object("TRAPPIST-1", true, 9e29, 0, &lookup);
    int trappist_stars = 0, trappist_planets = 0;
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->typeclass() == class_star && !strcmp(cels[index]->name, "TRAPPIST-1")) ++trappist_stars;
        if (trappist >= 0 && cels[index]->typeclass() == class_planet && cels[index]->orbit
            && cels[index]->orbit->center == cels[trappist]) ++trappist_planets;
    }
    report["trappist_stars"] = trappist_stars;
    report["trappist_planets"] = trappist_planets;
    std::cout << report.dump(2) << '\n';
    return 0;
}
