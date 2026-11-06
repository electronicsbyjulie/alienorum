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

namespace alienorum
{
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
        for (const char* separator : {" & ", " and ", "/", ";", "|"})
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

    inline std::vector<std::string> exoplanet_alias_keys(const nlohmann::json& row)
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
        std::unordered_map<std::string, std::vector<std::string>> links;
        std::set<std::string> rejected;

    public:
        void add(const nlohmann::json& row)
        {
            if (!row.is_object()) return;
            const auto keys = exoplanet_alias_keys(row);
            if (keys.empty()) return;
            for (const auto& key : keys)
            {
                links[key].push_back(keys.front());
                links[keys.front()].push_back(key);
                if (rejected_exoplanet_status(row)) rejected.insert(key);
            }
            if (row.contains("source_measurements") && row["source_measurements"].is_array())
                for (const auto& original : row["source_measurements"]) add(original);
        }

        void resolve()
        {
            std::vector<std::string> queue(rejected.begin(), rejected.end());
            for (size_t i = 0; i < queue.size(); ++i)
                for (const auto& alias : links[queue[i]])
                    if (rejected.insert(alias).second) queue.push_back(alias);
        }

        bool excludes(const nlohmann::json& row) const
        {
            if (rejected_exoplanet_status(row)) return true;
            for (const auto& key : exoplanet_alias_keys(row))
                if (rejected.count(key)) return true;
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
        nlohmann::json result = nlohmann::json::array();
        for (size_t i = 1; i < records.size(); ++i)
        {
            const auto& values = records[i];
            if (values.size() == 1 && values[0].empty()) continue;
            if (values.size() != header.size()) return nullptr;
            result.push_back({{"pl_name", values[name]}, {"planet_status", values[status]},
                {"alt_target_name", aliases < values.size() ? values[aliases] : ""}, {"source_catalog", "EU"}});
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
