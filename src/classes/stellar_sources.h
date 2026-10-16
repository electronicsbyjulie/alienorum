#ifndef ALIENORUM_STELLAR_SOURCES_H
#define ALIENORUM_STELLAR_SOURCES_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "star.h"

namespace alienorum
{
    struct StellarReferenceSource
    {
        std::string name;
        std::string url;
        std::string directory;
        std::string format;
        std::vector<std::string> columns;
    };

    std::string stellar_catalog_link(const Star *star);
    void restore_stellar_component_membership(CelestialObject **objects);
    int apply_gaia_reference_astrometry(CelestialObject **objects, const std::string &cache_directory);
    std::vector<StellarReferenceSource> stellar_reference_sources();
    std::string stellar_identifier_key(const std::string &identifier);
    std::vector<std::string> stellar_table_fields(const std::string &line, char delimiter = '\t');
    nlohmann::json reconcile_stellar_sources(CelestialObject **objects, const std::string &cache_directory,
        bool allow_download = true);
}

#endif
