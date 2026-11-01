#include "objectsearch.h"
#include "planet.h"
#include "star.h"
#include "cons.h"
#include "../globals.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace alienorum;

namespace
{
    struct TargetRegistry
    {
        std::mutex mutex;
        uint64_t sequence = 0;
        std::unordered_map<const CelestialObject*, uint64_t> tokens;
    };

    TargetRegistry& targets()
    {
        static TargetRegistry registry;
        return registry;
    }

    uint64_t target_token(const CelestialObject* object)
    {
        auto& registry = targets();
        std::lock_guard<std::mutex> lock(registry.mutex);
        auto entry = registry.tokens.find(object);
        if (entry != registry.tokens.end()) return entry->second;
        return registry.tokens.emplace(object, ++registry.sequence).first->second;
    }

    std::string type_name(const CelestialObject& object)
    {
        switch (object.typeclass())
        {
            case class_star: return "star";
            case class_planet: return "planet";
            case class_moon: return "moon";
            case class_satellite: return "satellite";
            case class_comet: return "comet";
            case class_galaxy: return "galaxy";
            default: return "object";
        }
    }

    ObjectSearchMatch describe(CelestialObject& object)
    {
        ObjectSearchMatch match;
        match.index = object.seqno;
        match.address = &object;
        match.primary_name = object.name;
        match.name = object.name;
        match.type = type_name(object);
        if (object.typeclass() == class_star)
        {
            const auto& star = static_cast<const Star&>(object);
            if (!star.local_name.empty()) match.name = star.local_name;
        }
        if (object.orbit && object.orbit->center) match.parent = object.orbit->center->name;
        return match;
    }

    void add_identity(std::vector<std::string>& names, const std::string& text)
    {
        if (text.empty()) return;
        auto key = object_search_key(text);
        if (key.empty() || std::find(names.begin(), names.end(), key) != names.end()) return;
        names.push_back(std::move(key));
    }

    std::vector<std::string> identities(CelestialObject& object)
    {
        std::vector<std::string> names;
        add_identity(names, object.name);
        add_identity(names, object.origname);
        if (object.typeclass() == class_star)
        {
            auto& star = static_cast<Star&>(object);
            const char component = star.get_component();
            const auto add_designation = [&](const std::string& base)
            {
                if (!component || component == 'A') add_identity(names, base);
                if (component) add_identity(names, base + component);
            };
            for (const auto& alias : star.catalog_aliases) add_identity(names, alias);
            for (const std::string& alias : {star.local_name, std::string(star.Bayer), std::string(star.Flamsteed),
                std::string(star.Gliese), star.alienorumid, star.WD, star.gaia_wd_name, star.CCDM})
                add_identity(names, alias);
            for (const auto& identifier : {std::make_pair("HD", star.HD), std::make_pair("HIP", star.HIP),
                std::make_pair("HR", star.HR), std::make_pair("SAO", star.SAO), std::make_pair("SB9", star.SB9)})
            {
                if (!identifier.second) continue;
                const std::string base = std::string(identifier.first) + std::to_string(identifier.second);
                add_designation(base);
            }
            // Include full Greek names and numbered Bayer forms even if the stored label is abbreviated.
            const int letter = star.BayerGrkno >= 100 ? (star.BayerGrkno - 100) / 10 : star.BayerGrkno;
            const std::string number = star.BayerGrkno >= 100 ? std::to_string(star.BayerGrkno % 10) : "";
            for (const auto& constellation : constellations)
            {
                if (letter < 0 && star.FlamsteedNo <= 0 && star.GouldNo <= 0) break;
                if (object_search_key(star.constellation) != object_search_key(constellation.abbrev)) continue;
                for (const auto& suffix : {constellation.abbrev, constellation.name})
                {
                    if (letter >= 0 && letter < 24)
                    {
                        add_designation(Greek_letter[letter] + number + suffix);
                        add_designation(Greek_letter[letter].substr(0, 3) + number + suffix);
                    }
                    if (star.FlamsteedNo > 0) add_designation(std::to_string(star.FlamsteedNo) + suffix);
                    if (star.GouldNo > 0) add_designation(std::to_string(star.GouldNo) + "G." + suffix);
                }
                break;
            }
        }
        if (object.typeclass() == class_planet || object.typeclass() == class_moon)
        {
            const auto& planet = static_cast<const Planet&>(object);
            add_identity(names, planet.catalog_designation);
            add_identity(names, planet.display_designation);
            // A published host alias plus a planet suffix identifies the same planet.
            const auto& raw_designation = !planet.display_designation.empty() ? planet.display_designation : planet.origname;
            const auto designation = object_search_key(raw_designation);
            const size_t end = raw_designation.find_last_not_of(" \t\r\n");
            const bool suffix = end != std::string::npos && end > 0
                && (std::isspace(static_cast<unsigned char>(raw_designation[end - 1]))
                    || std::isdigit(static_cast<unsigned char>(raw_designation[end - 1])));
            if (object.orbit && object.orbit->center && object.orbit->center->typeclass() == class_star
                && suffix && designation.size() > 1
                && designation.back() >= 'b' && designation.back() <= 'z')
            {
                for (const auto& host : identities(*object.orbit->center)) add_identity(names, host + designation.back());
            }
        }
        return names;
    }

    std::string numeric_part(const std::string& text)
    {
        size_t first = text.find_first_of("0123456789");
        if (first == std::string::npos) return "";
        if (first > 0 && (text[first - 1] == '+' || text[first - 1] == '-')) --first;
        const size_t last = text.find_last_of("0123456789");
        // Retain punctuation and all trailing component qualifiers as well as the digits.
        return text.substr(first, last - first + 1) + text.substr(last + 1);
    }

    int bounded_distance(const std::string& a, const std::string& b, int limit)
    {
        if (std::abs(static_cast<int>(a.size()) - static_cast<int>(b.size())) > limit) return limit + 1;
        std::vector<int> previous(b.size() + 1), older(b.size() + 1), row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) previous[j] = static_cast<int>(j);
        for (size_t i = 1; i <= a.size(); ++i)
        {
            std::fill(row.begin(), row.end(), limit + 1);
            row[0] = static_cast<int>(i);
            const size_t first = i > static_cast<size_t>(limit) ? i - limit : 1;
            const size_t last = std::min(b.size(), i + limit);
            int best = limit + 1;
            for (size_t j = first; j <= last; ++j)
            {
                row[j] = std::min({previous[j] + 1, row[j - 1] + 1,
                    previous[j - 1] + (a[i - 1] != b[j - 1])});
                if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                    row[j] = std::min(row[j], older[j - 2] + 1);
                best = std::min(best, row[j]);
            }
            if (best > limit) return limit + 1;
            older.swap(previous);
            previous.swap(row);
        }
        return previous.back();
    }

    bool ranked_before(const ObjectSearchMatch& a, const ObjectSearchMatch& b)
    {
        return std::tie(a.relative_distance, a.distance, a.name, a.type, a.parent, a.primary_name, a.index)
            < std::tie(b.relative_distance, b.distance, b.name, b.type, b.parent, b.primary_name, b.index);
    }
}

std::string alienorum::object_search_key(const std::string& text)
{
    if (text.empty()) return "";
    std::string key;
    key.reserve(text.size());
    for (unsigned char ch : text)
    {
        if (std::isspace(ch)) continue;
        key += ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
    }
    // These catalog spellings share one namespace; Wolf and Woolley remain separate.
    if (key.size() > 6 && key.compare(0, 6, "gliese") == 0
        && std::isdigit(static_cast<unsigned char>(key[6]))) key.replace(0, 6, "gj");
    else if (key.size() > 2 && key.compare(0, 2, "gl") == 0 && std::isdigit(static_cast<unsigned char>(key[2])))
        key.replace(0, 2, "gj");
    for (const char* prefix : {"hd", "hip", "hr", "sao", "ngc", "ic", "pgc", "ugc", "m"})
    {
        const size_t start = std::strlen(prefix);
        if (key.compare(0, start, prefix) || key.size() <= start
            || !std::isdigit(static_cast<unsigned char>(key[start]))) continue;
        size_t end = start;
        while (end + 1 < key.size() && key[end] == '0'
            && std::isdigit(static_cast<unsigned char>(key[end + 1]))) ++end;
        key.erase(start, end - start);
        break;
    }
    if (key.compare(0, 2, "wd") == 0)
    {
        const std::string wd = wd_identifier_key(key);
        if (!wd.empty())
        {
            key = wd;
            for (char& ch : key) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        }
    }
    return key;
}

ObjectSearchResponse alienorum::search_objects(const std::string& query, size_t display_limit)
{
    ObjectSearchResponse response;
    const std::string key = object_search_key(query);
    if (key.empty()) return response;
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (!cels) { response.state = ObjectSearchState::absent; return response; }
    // Build a fresh, owned snapshot per submission. Renames and alias changes are
    // reflected immediately without mutation hooks or a stale persistent index.
    struct Record { int index; std::vector<std::string> names; };
    std::vector<Record> records;
    records.reserve(ncelobjs);
    std::unordered_set<std::string> short_names;
    for (int i = 0; i < ncelobjs; ++i)
    {
        if (!cels[i] || cels[i]->deleted) continue;
        auto names = identities(*cels[i]);
        for (const auto& name : names)
        {
            const bool primary = name == object_search_key(cels[i]->name);
            const bool local = cels[i]->typeclass() == class_star
                && name == object_search_key(static_cast<Star*>(cels[i])->local_name);
            const bool catalog_number = std::any_of(name.begin(), name.end(), [](unsigned char ch) { return std::isdigit(ch); });
            if (name.size() == 2 && (primary || local || catalog_number)) short_names.insert(name);
        }
        if (std::find(names.begin(), names.end(), key) != names.end()) response.matches.push_back(describe(*cels[i]));
        records.push_back({i, std::move(names)});
    }
    if (key.size() < OBJECT_SEARCH_MIN_QUERY_LENGTH && !short_names.count(key))
    {
        response.matches.clear();
        response.state = ObjectSearchState::rejected;
        return response;
    }
    if (!response.matches.empty())
    {
        response.exact = true;
        response.total = response.matches.size();
    }
    else if (key.size() >= OBJECT_SEARCH_MIN_QUERY_LENGTH)
    {
        const std::string numbers = numeric_part(key);
        const size_t end = query.find_last_not_of(" \t\r\n");
        char component = end != std::string::npos && end > 0
            && std::isalpha(static_cast<unsigned char>(query[end]))
            && std::isspace(static_cast<unsigned char>(query[end - 1])) ? key.back() : 0;
        if (!component && !key.empty() && key.back() >= 'a' && key.back() <= 'z')
        {
            for (const auto& constellation : constellations)
            {
                const std::string suffix = object_search_key(constellation.abbrev) + key.back();
                if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    component = key.back();
                    break;
                }
            }
        }
        const int maximum = key.size() <= OBJECT_SEARCH_SHORT_QUERY_LENGTH
            ? std::min(OBJECT_SEARCH_MAX_EDIT_DISTANCE, OBJECT_SEARCH_SHORT_QUERY_MAX_DISTANCE)
            : OBJECT_SEARCH_MAX_EDIT_DISTANCE;
        for (const auto& record : records)
        {
            int best_distance = maximum + 1;
            double best_relative = 2;
            for (const auto& name : record.names)
            {
                if (numeric_part(name) != numbers) continue;
                if (component && name.back() != component) continue;
                const size_t length = std::max(key.size(), name.size());
                const int limit = std::min(maximum, static_cast<int>(std::floor(length * OBJECT_SEARCH_MAX_RELATIVE_DISTANCE)));
                if (std::abs(static_cast<int>(key.size()) - static_cast<int>(name.size())) > limit) continue;
                const int distance = bounded_distance(key, name, limit);
                if (distance > limit) continue;
                const double relative = static_cast<double>(distance) / length;
                if (std::tie(relative, distance) < std::tie(best_relative, best_distance))
                {
                    best_distance = distance;
                    best_relative = relative;
                }
            }
            if (best_distance > maximum) continue;
            ++response.total;
            auto match = describe(*cels[record.index]);
            match.distance = best_distance;
            match.relative_distance = best_relative;
            response.matches.push_back(std::move(match));
            std::sort(response.matches.begin(), response.matches.end(), ranked_before);
            if (response.matches.size() > std::max(size_t(1), display_limit)) response.matches.pop_back();
        }
    }
    std::sort(response.matches.begin(), response.matches.end(), ranked_before);
    // Qualify colliding display labels without adding another column.
    std::unordered_map<std::string, size_t> labels;
    for (const auto& match : response.matches) ++labels[object_search_key(match.name) + ":" + match.type];
    for (auto& match : response.matches)
    {
        match.token = target_token(match.address);
        if (labels[object_search_key(match.name) + ":" + match.type] > 1)
        {
            const std::string qualifier = !match.parent.empty() ? match.parent : match.primary_name;
            match.name += " (" + qualifier + ")";
        }
    }
    response.state = response.total == 0 ? ObjectSearchState::absent
        : response.total == 1 ? ObjectSearchState::single : ObjectSearchState::multiple;
    if (ncelobjs && cels[0])
    {
        response.anchor = describe(*cels[0]);
        response.anchor.token = target_token(cels[0]);
    }
    return response;
}

CelestialObject* alienorum::resolve_object_search_match(const ObjectSearchMatch& match)
{
    if (!cels || match.index < 0 || match.index >= ncelobjs || cels[match.index] != match.address) return nullptr;
    auto& registry = targets();
    std::lock_guard<std::mutex> lock(registry.mutex);
    const auto entry = registry.tokens.find(match.address);
    if (entry == registry.tokens.end() || entry->second != match.token) return nullptr;
    CelestialObject* object = cels[match.index];
    return object && !object->deleted && match.primary_name == object->name ? object : nullptr;
}

void alienorum::retire_object_search_target(const CelestialObject* object)
{
    auto& registry = targets();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.tokens.erase(object);
}
