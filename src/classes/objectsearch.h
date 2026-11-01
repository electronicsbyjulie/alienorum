#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define OBJECT_SEARCH_MIN_QUERY_LENGTH 3
#define OBJECT_SEARCH_MAX_RESULTS 10
#define OBJECT_SEARCH_MAX_EDIT_DISTANCE 6
#define OBJECT_SEARCH_MAX_RELATIVE_DISTANCE 0.50
#define OBJECT_SEARCH_SHORT_QUERY_LENGTH 4
#define OBJECT_SEARCH_SHORT_QUERY_MAX_DISTANCE 1

namespace alienorum
{
    class CelestialObject;

    struct ObjectSearchMatch
    {
        int index = -1;
        const CelestialObject* address = nullptr;
        uint64_t token = 0;
        std::string name, type, parent, primary_name;
        double relative_distance = 0;
        int distance = 0;
    };

    enum class ObjectSearchState { blank, rejected, absent, single, multiple };
    enum class ObjectSearchAction { select, focus, track, go, rise, set, edit };

    struct ObjectSearchResponse
    {
        ObjectSearchState state = ObjectSearchState::blank;
        bool exact = false;
        size_t total = 0;
        ObjectSearchMatch anchor;
        std::vector<ObjectSearchMatch> matches;
    };

    std::string object_search_key(const std::string& text);
    ObjectSearchResponse search_objects(const std::string& query, size_t display_limit = OBJECT_SEARCH_MAX_RESULTS);
    // Callers hold catalog_mutex while using the returned object.
    CelestialObject* resolve_object_search_match(const ObjectSearchMatch& match);
    void retire_object_search_target(const CelestialObject* object);
}
