#ifndef ALIENORUM_EXOPLANETSTATUS_H
#define ALIENORUM_EXOPLANETSTATUS_H

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "nlohmann/json.hpp"
#include "numeric.h"

namespace alienorum
{
    // Keep only fields used for identity and classification, including nested originals.
    inline nlohmann::json read_exoplanet_status_catalog(std::istream& input, bool include_orbits = false)
    {
        return nlohmann::json::parse(input, [include_orbits](int, nlohmann::json::parse_event_t event, nlohmann::json& value)
        {
            if (event != nlohmann::json::parse_event_t::key) return true;
            const auto& key = value.get_ref<const std::string&>();
            return key == "pl_name" || key == "hostname" || key == "alt_target_name"
                || key == "alternate_names" || key == "catalog_aliases" || key == "source_measurements"
                || key == "planet_status" || key == "koi_disposition" || key == "disposition"
                || key == "tfopwg_disp" || key == "k2c_disp" || key == "pl_controv_flag"
                || (include_orbits && (key == "source_catalog" || key == "pl_orbper"
                    || key == "ra" || key == "dec" || key == "sy_dist"));
        }, false);
    }

    inline std::string exoplanet_status_key(const std::string& value)
    {
        std::string name = value;
        const size_t length = name.size();
        if (length > 3 && name.back() >= 'b' && name.back() <= 'z' && name[length - 2] == 'A' && name[length - 3] == ' ')
            name.erase(length - 2, 1);
        else if (length > 4 && name.back() >= 'b' && name.back() <= 'z' && name[length - 2] == ' '
            && name[length - 3] == 'A' && name[length - 4] == ' ') name.erase(length - 3, 2);
        std::string result;
        for (unsigned char ch : name)
            if (!std::isspace(ch) && ch != '-' && ch != '_') result += std::tolower(ch);
        if (result.rfind("gj", 0) == 0) result.replace(0, 2, "gl");
        if (result.rfind("alpha", 0) == 0) result.replace(0, 5, "alf");
        return result;
    }

    inline bool tentative_exoplanet_status(const nlohmann::json& row)
    {
        const auto controversial = row.find("pl_controv_flag");
        if (controversial != row.end() && (*controversial == 1 || *controversial == true ||
            *controversial == "1")) return true;
        for (const char* field : {"planet_status", "koi_disposition", "disposition", "tfopwg_disp", "k2c_disp"})
        {
            const auto value = row.find(field);
            if (value == row.end() || !value->is_string()) continue;
            const auto key = exoplanet_status_key(value->get_ref<const std::string&>());
            if (key == "candidate" || key == "unconfirmed" || key == "controversial" ||
                key == "planetarycandidate" || key == "pc") return true;
        }
        return false;
    }

    inline bool rejected_exoplanet_status(const nlohmann::json& row)
    {
        for (const char* field : {"planet_status", "koi_disposition", "disposition", "tfopwg_disp", "k2c_disp"})
        {
            const auto value = row.find(field);
            if (value == row.end() || !value->is_string()) continue;
            const auto key = exoplanet_status_key(value->get<std::string>());
            if (key == "retracted" || key == "refuted" || key == "falsepositive"
                || key == "falsepositiveplanet" || key == "fp" || key == "fpp") return true;
        }
        return false;
    }

    inline std::vector<std::string> exoplanet_name_list(std::string text)
    {
        for (const char* separator : {" & ", " and ", "#", "/", ";", "|"})
        {
            size_t position = 0;
            while ((position = text.find(separator, position)) != std::string::npos)
                text.replace(position, strlen(separator), ",");
        }
        std::vector<std::string> names;
        std::istringstream input(text);
        std::string name, host;
        while (std::getline(input, name, ','))
        {
            const auto first = name.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
            if (name.size() == 1 && name[0] >= 'b' && name[0] <= 'z' && !host.empty()) name = host + name;
            const auto space = name.find_last_of(' ');
            if (space != std::string::npos && space + 2 == name.size() && std::islower(name.back()))
                host = name.substr(0, space + 1);
            names.push_back(name);
        }
        return names;
    }

    inline std::vector<std::string> exoplanet_alias_keys(const nlohmann::json& row, bool include_measurements = true)
    {
        std::vector<std::string> keys;
        for (const char* field : {"pl_name", "alt_target_name", "alternate_names"})
        {
            const auto value = row.find(field);
            if (value != row.end() && value->is_string())
                for (const auto& name : exoplanet_name_list(value->get<std::string>()))
                    keys.push_back(exoplanet_status_key(name));
        }
        for (const char* field : {"catalog_aliases", "source_measurements"})
        {
            if (!include_measurements && strcmp(field, "source_measurements") == 0) continue;
            const auto values = row.find(field);
            if (values == row.end() || !values->is_array()) continue;
            for (const auto& value : *values)
            {
                if (value.is_string()) keys.push_back(exoplanet_status_key(value.get<std::string>()));
                else if (value.is_object())
                {
                    const auto nested = exoplanet_alias_keys(value);
                    keys.insert(keys.end(), nested.begin(), nested.end());
                }
            }
        }
        return keys;
    }

    class ExoplanetRejections
    {
        struct OrbitClaim
        {
            std::vector<std::string> names;
            std::string provider;
            double period, ra, dec, distance;
        };
        std::unordered_map<std::string, std::vector<std::string>> links;
        std::unordered_map<std::string, std::set<std::string>> direct_rejections;
        std::vector<OrbitClaim> claims;
        std::set<std::pair<std::string, std::string>> alias_conflicts;
        std::set<std::string> rejected;

        bool conflicts(const std::string& first, const std::string& second) const
        {
            return alias_conflicts.count(std::minmax(first, second));
        }

    public:
        void add(const nlohmann::json& row, const std::string& provider = {})
        {
            if (!row.is_object()) return;
            const auto keys = exoplanet_alias_keys(row, false);
            if (keys.empty()) return;
            const bool excluded = rejected_exoplanet_status(row);
            const auto field = row.find("source_catalog");
            const std::string source = provider.empty() && field != row.end() && field->is_string()
                ? field->get<std::string>() : provider;
            const auto values = row.find("source_measurements");
            const bool merged = values != row.end() && values->is_array() && !values->empty();
            if (!merged || excluded)
            {
                for (const auto& key : keys)
                {
                    links[key].push_back(keys.front());
                    links[keys.front()].push_back(key);
                    if (excluded) direct_rejections[key].insert(source);
                }
            }
            const auto number = [&](const char* name)
            {
                const auto value = row.find(name);
                return value != row.end() && value->is_number() ? value->get<double>() : NAN;
            };
            if (merged)
            {
                for (const auto& original : *values)
                {
                    if (!original.is_object()) continue;
                    const auto original_names = exoplanet_alias_keys(original, false);
                    if (!original_names.empty())
                    {
                        links[keys.front()].push_back(original_names.front());
                        links[original_names.front()].push_back(keys.front());
                    }
                    add(original);
                }
            }
            else if (!excluded && !source.empty())
            {
                const double period = number("pl_orbper"), distance = number("sy_dist");
                const double ra = number("ra"), dec = number("dec");
                if (period > 0 && distance > 0 && std::isfinite(ra) && std::isfinite(dec))
                    claims.push_back({keys, source, period, ra, dec, distance});
            }
        }

        void resolve()
        {
            alias_conflicts.clear();
            // A publication can reuse a letter for a different orbit. Disambiguate
            // rejection aliases using the rejecting provider's own active orbits.
            for (const auto& claim : claims)
            {
                if (claim.names.size() < 2 || direct_rejections.count(claim.names.front())) continue;
                for (const auto& alias : claim.names)
                {
                    if (alias.size() < 2) continue;
                    const auto rejection = direct_rejections.find(alias);
                    if (rejection == direct_rejections.end() || rejection->second.size() != 1) continue;
                    const auto& provider = *rejection->second.begin();
                    if (provider.empty() || provider == claim.provider) continue;
                    std::string counterpart;
                    for (const auto& active : claims)
                    {
                        if (active.provider != provider || direct_rejections.count(active.names.front())) continue;
                        const auto& active_name = active.names.front();
                        if (active_name.size() != alias.size() || active_name.compare(0, alias.size() - 1,
                            alias, 0, alias.size() - 1) != 0) continue;
                        if (std::abs(active.period - claim.period) > .005 * claim.period
                            || std::abs(active.distance - claim.distance) > .05 * claim.distance) continue;
                        const double east = std::remainder(active.ra - claim.ra, 360.0)
                            * std::cos(claim.dec * 0.017453292519943295);
                        if (std::hypot(east, active.dec - claim.dec) > 1.0 / 60) continue;
                        if (!counterpart.empty() && counterpart != active.names.front())
                        {
                            counterpart.clear();
                            break;
                        }
                        counterpart = active.names.front();
                    }
                    if (!counterpart.empty())
                    {
                        alias_conflicts.insert(std::minmax(claim.names.front(), alias));
                        alias_conflicts.insert(std::minmax(counterpart, alias));
                    }
                }
            }
            rejected.clear();
            for (const auto& entry : direct_rejections) rejected.insert(entry.first);
            std::vector<std::string> queue(rejected.begin(), rejected.end());
            for (size_t i = 0; i < queue.size(); ++i)
                for (const auto& alias : links[queue[i]])
                    if (!conflicts(queue[i], alias) && rejected.insert(alias).second) queue.push_back(alias);
        }

        bool excludes(const nlohmann::json& row) const
        {
            if (rejected_exoplanet_status(row)) return true;
            const auto keys = exoplanet_alias_keys(row, false);
            if (!keys.empty() && rejected.count(keys.front())) return true;
            const auto originals = row.find("source_measurements");
            if (originals != row.end() && originals->is_array() && !originals->empty())
            {
                for (const auto& original : *originals)
                    if (original.is_object() && excludes(original)) return true;
                return false;
            }
            for (const auto& key : keys)
                if (rejected.count(key) && !conflicts(keys.front(), key)) return true;
            return false;
        }
    };

    inline nlohmann::json parse_eu_planet_status(const std::string& text)
    {
        // CSV fields may contain quoted commas, doubled quotes, and newlines.
        std::vector<std::vector<std::string>> records;
        std::vector<std::string> record;
        std::string field;
        bool quoted = false;
        for (size_t i = 0; i <= text.size(); ++i)
        {
            const char ch = i < text.size() ? text[i] : '\n';
            if (ch == '"')
            {
                if (quoted && i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
                else quoted = !quoted;
            }
            else if (!quoted && (ch == ',' || ch == '\n'))
            {
                record.push_back(field);
                field.clear();
                if (ch == '\n')
                {
                    records.push_back(record);
                    record.clear();
                }
            }
            else if (ch != '\r') field += ch;
        }
        if (quoted || records.empty()) return nullptr;
        const auto& header = records.front();
        const auto index = [&](const char* key) { return std::find(header.begin(), header.end(), key) - header.begin(); };
        const size_t name = index("name"), status = index("planet_status"), aliases = index("alternate_names");
        if (name == header.size() || status == header.size()) return nullptr;
        const std::pair<const char*, const char*> numeric_fields[] = {
            {"orbital_period", "pl_orbper"}, {"semi_major_axis", "pl_orbsmax"}, {"eccentricity", "pl_orbeccen"},
            {"inclination", "pl_orbincl"}, {"inclination_error_min", "pl_orbinclerr2"},
            {"inclination_error_max", "pl_orbinclerr1"}, {"omega", "pl_orblper"}, {"tperi", "pl_orbtper"},
            {"tzero_tr", "pl_tranmid"}, {"mass", "pl_bmassj"}, {"mass_sini", "pl_msinij"},
            {"mass_error_min", "pl_bmassjerr2"}, {"mass_error_max", "pl_bmassjerr1"},
            {"radius", "pl_radj"}, {"radius_error_min", "pl_radjerr2"}, {"radius_error_max", "pl_radjerr1"},
            {"ra", "ra"}, {"dec", "dec"}, {"star_distance", "sy_dist"}, {"mag_v", "sy_vmag"},
            {"star_mass", "st_mass"}, {"star_radius", "st_rad"}, {"star_teff", "st_teff"},
            {"star_metallicity", "st_met"}, {"star_age", "st_age"}
        };
        const std::pair<const char*, const char*> text_fields[] = {
            {"star_name", "hostname"}, {"star_sp_type", "st_spectype"}, {"detection_type", "detection_type"}
        };
        nlohmann::json result = nlohmann::json::array();
        for (size_t i = 1; i < records.size(); ++i)
        {
            const auto& values = records[i];
            if (values.size() == 1 && values[0].empty()) continue;
            if (values.size() != header.size()) return nullptr;
            nlohmann::json row = {{"pl_name", values[name]}, {"planet_status", values[status]},
                {"alt_target_name", aliases < values.size() ? values[aliases] : ""}, {"source_catalog", "EU"}};
            if (tentative_exoplanet_status(row))
            {
                for (const auto& [column, key] : numeric_fields)
                {
                    const size_t position = index(column);
                    double value = 0;
                    if (position < values.size() && parse_numeric_value(values[position], value)) row[key] = value;
                }
                for (const auto& [column, key] : text_fields)
                {
                    const size_t position = index(column);
                    if (position < values.size()) row[key] = values[position];
                }
                const size_t stellar_aliases = index("star_alternate_names");
                if (stellar_aliases < values.size())
                {
                    for (const auto& alias : exoplanet_name_list(values[stellar_aliases]))
                    {
                        if (alias.rfind("HIP ", 0) == 0) row["hip_name"] = alias;
                        if (alias.rfind("HD ", 0) == 0) row["hd_name"] = alias;
                    }
                }
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    inline nlohmann::json parse_nasa_planet_status(std::string text)
    {
        const auto start = text.find("name=\"refuted\"");
        const auto end = text.find("name=\"reinstated\"", start);
        if (start == std::string::npos || end == std::string::npos || text.find("name=\"false_pos\"", start) > end)
            return nullptr;
        text = text.substr(start, end - start);
        text = std::regex_replace(text, std::regex("<a[^>]*href=\"[^\"]*(?:adsabs|iopscience)[^\"]*\"[^>]*>[\\s\\S]*?</a>", std::regex::icase), "");
        text = std::regex_replace(text, std::regex("&amp;"), "&");
        text = std::regex_replace(text, std::regex("&nbsp;"), " ");
        const std::regex item("<li[^>]*>([\\s\\S]*?)</li>", std::regex::icase);
        const std::regex tags("<[^>]*>");
        nlohmann::json result = nlohmann::json::array();
        for (auto it = std::sregex_iterator(text.begin(), text.end(), item); it != std::sregex_iterator(); ++it)
        {
            const std::string value = std::regex_replace((*it)[1].str(), tags, "");
            const std::regex parentheses("\\(([^()]*)\\)");
            std::string primary = std::regex_replace(value, parentheses, "");
            // Some reference links contain a closing parenthesis without its opener.
            for (char& ch : primary) if (ch == '(' || ch == ')') ch = ' ';
            auto names = exoplanet_name_list(primary);
            for (auto alias_it = std::sregex_iterator(value.begin(), value.end(), parentheses);
                alias_it != std::sregex_iterator(); ++alias_it)
            {
                std::string alias = (*alias_it)[1].str();
                if (alias.rfind("a.k.a. ", 0) == 0) alias.erase(0, 7);
                if (std::regex_match(alias, std::regex(".*[0-9A-Za-z][ ]+[b-z]"))) names.push_back(alias);
            }
            // Separate planets in one list item are not aliases of each other.
            for (const auto& name : names)
                result.push_back({{"pl_name", name}, {"planet_status", "Retracted"}, {"source_catalog", "NASA"}});
        }
        return result.empty() ? nlohmann::json(nullptr) : result;
    }
}
#endif
