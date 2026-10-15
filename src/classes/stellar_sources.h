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
        std::string format;
        std::vector<std::string> columns;
    };

    std::vector<StellarReferenceSource> stellar_reference_sources();
    std::string stellar_identifier_key(const std::string &identifier);
    std::vector<std::string> stellar_table_fields(const std::string &line, char delimiter = '\t');
    nlohmann::json reconcile_stellar_sources(CelestialObject **objects, const std::string &cache_directory,
        bool allow_download = true);
}

#endif
