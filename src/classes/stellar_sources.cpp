#include "stellar_sources.h"
#include "cons.h"
#include "../globals.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <curl/curl.h>

using namespace alienorum;
using json = nlohmann::json;

namespace
{
    using Row = std::map<std::string, std::string>;

    std::string lowercase(std::string value)
    {
        for (char &c : value)
        {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        return value;
    }

    std::string cell(const Row &row, const std::string &column)
    {
        auto found = row.find(lowercase(column));
        return found == row.end() ? "" : trim(found->second);
    }

    double number(const Row &row, const std::string &column)
    {
        const std::string value = cell(row, column);
        char *end = nullptr;
        const double parsed = strtod(value.c_str(), &end);
        return value.empty() || end == value.c_str() || *end || !std::isfinite(parsed) ? NAN : parsed;
    }

    size_t receive(void *data, size_t size, size_t count, void *destination)
    {
        std::ofstream &file = *static_cast<std::ofstream*>(destination);
        file.write(static_cast<char*>(data), size * count);
        return file ? size * count : 0;
    }

    bool fetch(const std::string &url, const std::filesystem::path &path)
    {
        if (radio_silence)
        {
            return false;
        }
        CURL *handle = curl_easy_init();
        if (!handle)
        {
            return false;
        }
        const auto temporary = path.string() + ".part";
        std::ofstream file(temporary, std::ios::binary);
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Alienorum stellar catalog reconciliation");
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &file);
        const CURLcode status = curl_easy_perform(handle);
        curl_easy_cleanup(handle);
        file.close();
        if (status != CURLE_OK || !std::filesystem::exists(temporary)
            || !std::filesystem::file_size(temporary))
        {
            std::filesystem::remove(temporary);
            return false;
        }
        std::filesystem::rename(temporary, path);
        return true;
    }

    size_t read_table(const std::filesystem::path &path, const StellarReferenceSource &source,
        const std::function<void(const Row&)> &consume)
    {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line))
        {
            if (line.find("QUERY_STATUS: OVERFLOW") != std::string::npos)
            {
                throw std::runtime_error("Truncated reference table: " + source.name);
            }
            if (!line.empty() && line[0] != '#' && line.find('\t') != std::string::npos)
            {
                break;
            }
        }
        if (line.find('\t') == std::string::npos)
        {
            throw std::runtime_error("Empty reference table: " + source.name);
        }
        const auto columns = stellar_table_fields(line);
        std::set<std::string> available;
        for (const auto &column : columns)
        {
            available.insert(lowercase(column));
        }
        for (const auto &column : source.columns)
        {
            if (!available.count(lowercase(column)))
            {
                throw std::runtime_error("Reference schema mismatch: " + source.name + " column " + column);
            }
        }
        size_t count = 0;
        while (std::getline(file, line))
        {
            if (trim(line) == "[EOD]")
            {
                break;
            }
            if (trim(line).empty() || line[0] == '#'
                || line.find_first_not_of("- \t\r") == std::string::npos)
            {
                continue;
            }
            auto balanced = [](const std::string &text)
            {
                bool quoted = false;
                for (size_t i = 0; i < text.size(); i++)
                {
                    if (text[i] == '"')
                    {
                        if (quoted && i + 1 < text.size() && text[i + 1] == '"')
                        {
                            i++;
                        }
                        else
                        {
                            quoted = !quoted;
                        }
                    }
                }
                return !quoted;
            };
            while (!balanced(line))
            {
                std::string continuation;
                if (!std::getline(file, continuation))
                {
                    throw std::runtime_error("Truncated multiline reference field: " + source.name);
                }
                line += '\n' + continuation;
            }
            const auto fields = stellar_table_fields(line);
            if (fields.size() != columns.size())
            {
                throw std::runtime_error("Reference row width mismatch: " + source.name);
            }
            Row row;
            for (size_t i = 0; i < columns.size(); i++)
            {
                row[lowercase(columns[i])] = fields[i];
            }
            consume(row);
            count++;
        }
        if (!count)
        {
            throw std::runtime_error("Reference table has no data rows: " + source.name);
        }
        return count;
    }

    double separation(Star *star, double ra, double dec)
    {
        if (!std::isfinite(ra) || !std::isfinite(dec))
        {
            return INFINITY;
        }
        const double latitude = dec * fiftyseventh;
        const double sine_lat = sin((latitude - star->declination) / 2.0);
        const double sine_lon = sin((ra * fiftyseventh - star->right_ascension) / 2.0);
        const double square = sine_lat * sine_lat + cos(latitude) * cos(star->declination) * sine_lon * sine_lon;
        return 2.0 * asin(sqrt(std::clamp(square, 0.0, 1.0))) * fiftyseven * 3600.0;
    }

    std::vector<std::string> identifiers(const std::string &text)
    {
        std::vector<std::string> values;
        std::stringstream input(text);
        std::string value;
        while (std::getline(input, value, '|'))
        {
            values.push_back(trim(value));
        }
        return values;
    }

    bool valid_spectral(const std::string &value)
    {
        if (value.empty() || value.size() > 15 || value.find('?') != std::string::npos)
        {
            return false;
        }
        for (unsigned char c : value)
        {
            if (c < 32 || c >= 127)
            {
                return false;
            }
        }
        return std::string("OBAFGKMLTYRNSCDW").find(value[0]) != std::string::npos
            || value.rfind("sd", 0) == 0 || value.rfind("esd", 0) == 0;
    }

    struct Context
    {
        CelestialObject **objects;
        json &report;
        std::vector<Star*> stars;
        std::unordered_map<std::string, std::vector<Star*>> aliases;
        std::unordered_map<Star*, std::string> object_ids;
        std::unordered_map<std::string, std::vector<Star*>> resolved_objects;
        std::unordered_map<std::string, std::string> object_types;
        std::unordered_map<std::string, std::vector<Star*>> spatial;
        std::unordered_map<Star*, std::vector<Star*>> children;
        std::map<std::string, Row> sbx_systems;
        std::map<std::string, std::vector<Star*>> sbx_members;
        std::map<std::string, std::pair<int, std::string>> sbx_ranks;
        std::unordered_map<Star*, std::pair<int, std::string>> spectral_ranks;

        static std::string spatial_key(double ra, double dec)
        {
            const double r = ra * fiftyseventh, d = dec * fiftyseventh;
            const double scale = 1.0 / (10.0 / 3600.0 * fiftyseventh);
            return std::to_string(int(floor(cos(d) * cos(r) * scale))) + ":"
                + std::to_string(int(floor(cos(d) * sin(r) * scale))) + ":"
                + std::to_string(int(floor(sin(d) * scale)));
        }

        Context(CelestialObject **input, json &output) : objects(input), report(output)
        {
            for (int i = 1; input[i]; i++)
            {
                if (input[i]->typeclass() != class_star || input[i]->deleted)
                {
                    continue;
                }
                Star *star = static_cast<Star*>(input[i]);
                stars.push_back(star);
                add_alias(star->name, star);
                add_alias(star->Gliese, star);
                add_alias(star->Bayer, star);
                add_alias(star->Flamsteed, star);
                const std::string display_key = stellar_identifier_key(star->name);
                const bool primary = !star->orbit || !star->orbit->center || star->get_component() == 'A';
                if (primary && display_key.size() > 1 && display_key.back() == 'A')
                {
                    add_alias(display_key.substr(0, display_key.size() - 1), star);
                }
                if (!display_key.empty() && std::isdigit(static_cast<unsigned char>(display_key.front())))
                {
                    add_alias("*" + display_key, star);
                    if (primary && display_key.back() == 'A')
                    {
                        add_alias("*" + display_key.substr(0, display_key.size() - 1), star);
                    }
                }
                if (star->HD)
                {
                    add_alias("HD " + std::to_string(star->HD), star);
                }
                if (star->HIP)
                {
                    add_alias("HIP " + std::to_string(star->HIP), star);
                }
                if (star->HR)
                {
                    add_alias("HR " + std::to_string(star->HR), star);
                }
                add_alias(bonn_survey_key(star->Bonn_survey, star->Bonn_survey_declination,
                    star->Bonn_survey_sequential), star);
                spatial[spatial_key(star->right_ascension * fiftyseven, star->declination * fiftyseven)].push_back(star);
            }
            for (Star *star : stars)
            {
                if (star->orbit && star->orbit->center && star->orbit->center->typeclass() == class_star)
                {
                    children[static_cast<Star*>(star->orbit->center)].push_back(star);
                }
            }
        }

        void add_alias(const std::string &value, Star *star)
        {
            const std::string key = stellar_identifier_key(value);
            if (!key.empty())
            {
                auto &matches = aliases[key];
                if (std::find(matches.begin(), matches.end(), star) == matches.end())
                {
                    matches.push_back(star);
                }
            }
        }

        std::vector<Star*> match(const std::vector<std::string> &names, double ra = NAN, double dec = NAN,
            bool positional = false)
        {
            std::set<Star*> candidates;
            for (const auto &name : names)
            {
                const auto found = aliases.find(stellar_identifier_key(name));
                if (found != aliases.end())
                {
                    candidates.insert(found->second.begin(), found->second.end());
                }
            }
            if (candidates.empty() && positional && std::isfinite(ra) && std::isfinite(dec))
            {
                const double r = ra * fiftyseventh, d = dec * fiftyseventh;
                const double scale = 1.0 / (10.0 / 3600.0 * fiftyseventh);
                const int x = floor(cos(d) * cos(r) * scale);
                const int y = floor(cos(d) * sin(r) * scale);
                const int z = floor(sin(d) * scale);
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        for (int dz = -1; dz <= 1; dz++)
                        {
                            const auto found = spatial.find(std::to_string(x + dx) + ":"
                                + std::to_string(y + dy) + ":" + std::to_string(z + dz));
                            if (found != spatial.end())
                            {
                                for (Star *star : found->second)
                                {
                                    if (separation(star, ra, dec) <= 2.0)
                                    {
                                        candidates.insert(star);
                                    }
                                }
                            }
                        }
                    }
                }
                if (candidates.size() != 1)
                {
                    return std::vector<Star*>();
                }
            }
            std::vector<Star*> result;
            for (Star *star : candidates)
            {
                const double motion = hypot(star->proper_motion_RA * cos(star->declination), star->proper_motion_decl)
                    * oneyear * 30.0 * fiftyseven * 3600.0;
                if (!star->deleted && (!std::isfinite(ra) || !std::isfinite(dec)
                    || separation(star, ra, dec) <= std::max(10.0, motion)))
                {
                    result.push_back(star);
                }
            }
            return result;
        }

        void provenance(Star *star, const std::string &field, const std::string &source,
            const Row &row, const json &value, const std::string &unit = "", double uncertainty = NAN)
        {
            json entry;
            entry["source"] = source;
            entry["value"] = value;
            entry["unit"] = unit;
            for (const std::string &key : std::vector<std::string>
                {
                    "oid", "bibcode", "sp_bibcode", "recno", "name", "simbadname", "wds", "sn", "source_id"
                })
            {
                const std::string text = cell(row, key);
                if (!text.empty())
                {
                    entry[key] = text;
                }
            }
            if (std::isfinite(uncertainty))
            {
                entry["uncertainty"] = uncertainty;
            }
            auto &object = report["provenance"][std::to_string(star->seqno)];
            object["id_before"] = star->alienorumid;
            object["HD"] = star->HD;
            object["HIP"] = star->HIP;
            object["fields"][field] = entry;
        }

        void adopt(Star *star, const std::string &field, double &target, double value,
            const std::string &source, const Row &row, const std::string &unit = "", double uncertainty = NAN)
        {
            if (!std::isfinite(value))
            {
                return;
            }
            if (!std::isfinite(target) || fabs(target - value) > 1e-9 * std::max(1e-300, fabs(value)))
            {
                json change;
                change["star"] = star->name;
                change["seqno"] = star->seqno;
                change["field"] = field;
                change["old"] = std::isfinite(target) ? json(target) : json();
                change["new"] = value;
                change["source"] = source;
                report["changes"].push_back(change);
            }
            target = value;
            provenance(star, field, source, row, value, unit, uncertainty);
        }

        void spectrum(Star *star, const std::string &value, const std::string &source, const Row &row,
            int rank = 0)
        {
            if (!valid_spectral(value))
            {
                return;
            }
            const std::string citation = cell(row, "bibcode").empty() ? cell(row, "sp_bibcode") : cell(row, "bibcode");
            const auto priority = std::make_pair(rank, citation);
            if (spectral_ranks.count(star) && spectral_ranks[star] > priority)
            {
                return;
            }
            spectral_ranks[star] = priority;
            if (value != star->spectral_type)
            {
                json change;
                change["star"] = star->name;
                change["field"] = "spectral_type";
                change["old"] = star->spectral_type;
                change["new"] = value;
                change["source"] = source;
                report["changes"].push_back(change);
                strcpy(star->spectral_type, value.c_str());
            }
            provenance(star, "spectral_type", source, row, value);
        }

        void name(Star *star, const std::string &value, const std::string &source, const Row &row)
        {
            if (value.empty() || value.size() > 39 || value.size() >= sizeof(star->name))
            {
                return;
            }
            for (unsigned char c : value)
            {
                if (c < 32 || c == 127)
                {
                    return;
                }
            }
            if (value != star->name)
            {
                json change;
                change["star"] = star->name;
                change["field"] = "name";
                change["old"] = star->name;
                change["new"] = value;
                change["source"] = source;
                report["changes"].push_back(change);
                strcpy(star->name, value.c_str());
                star->namelen = value.size();
                star->has_custom_name = true;
            }
            add_alias(value, star);
            provenance(star, "name", source, row, value);
        }

        void designation(Star *star, const std::vector<std::string> &names, const Row &row)
        {
            static const std::vector<std::string> greek
            {
                "ALF", "BET", "GAM", "DEL", "EPS", "ZET", "ETA", "THE", "IOT", "KAP", "LAM", "MU",
                "NU", "KSI", "OMI", "PI", "RHO", "SIG", "TAU", "UPS", "PHI", "KHI", "PSI", "OME"
            };
            static const std::vector<std::string> abbreviations
            {
                "Alp", "Bet", "Gam", "Del", "Eps", "Zet", "Eta", "The", "Iot", "Kap", "Lam", "Mu",
                "Nu", "Xi", "Omi", "Pi", "Rho", "Sig", "Tau", "Ups", "Phi", "Chi", "Psi", "Ome"
            };
            const std::regex flam("^\\*([0-9]+)([A-Z][A-Z][A-Z])([A-Z]?)$");
            const std::regex hd("^HD([0-9]+)[A-Z]?$");
            const std::regex hip("^HIP([0-9]+)[A-Z]?$");
            const std::regex gliese("^GJ([0-9]+(?:\\.[0-9]+)?)([A-Z]{0,3})$");
            std::string gliese_value;
            int gliese_score = -1;
            const std::string original_bayer = star->Bayer;
            int bayer_score = -1;
            for (const auto &alias : names)
            {
                const std::string key = stellar_identifier_key(alias);
                std::smatch match;
                if (std::regex_match(key, match, hd))
                {
                    const unsigned long id = strtoul(match[1].str().c_str(), nullptr, 10);
                    if (id > 0 && id <= MAX_HD && !star->HD
                        && (!hdcache || !hdcache[id] || hdcache[id] == star
                            || (star->multisys && hdcache[id]->multisys == star->multisys)))
                    {
                        star->HD = id;
                        if (hdcache && !hdcache[id])
                        {
                            hdcache[id] = star;
                        }
                        provenance(star, "HD", "SIMBAD", row, id);
                    }
                }
                if (std::regex_match(key, match, hip))
                {
                    const unsigned long id = strtoul(match[1].str().c_str(), nullptr, 10);
                    if (id > 0 && id <= MAX_HIP && !star->HIP
                        && (!hipcache || !hipcache[id] || hipcache[id] == star
                            || (star->multisys && hipcache[id]->multisys == star->multisys)))
                    {
                        star->HIP = id;
                        if (hipcache && !hipcache[id])
                        {
                            hipcache[id] = star;
                        }
                        provenance(star, "HIP", "SIMBAD", row, id);
                    }
                }
                if (std::regex_match(key, match, gliese))
                {
                    const int score = (match[2].str().empty() ? 0 : 1)
                        + (key == stellar_identifier_key(star->name) ? 2 : 0);
                    if (score > gliese_score)
                    {
                        gliese_value = "GJ " + match[1].str()
                            + (match[2].str().empty() ? "" : " " + match[2].str());
                        gliese_score = score;
                    }
                }
                if (std::regex_match(key, match, flam))
                {
                    const int id = atoi(match[1].str().c_str());
                    if (id > 0 && id <= 999)
                    {
                        star->FlamsteedNo = id;
                        const std::string cons_upper = match[2];
                        for (const auto &cons : constellations)
                        {
                            if (stellar_identifier_key(cons.abbrev) == cons_upper)
                            {
                                strcpy(star->constellation, cons.abbrev.c_str());
                            }
                        }
                        provenance(star, "FlamsteedNo", "SIMBAD", row, id);
                    }
                }
                if (key.empty() || key[0] != '*')
                {
                    continue;
                }
                for (size_t g = 0; g < greek.size(); g++)
                {
                    if (key.compare(1, greek[g].size(), greek[g]) != 0)
                    {
                        continue;
                    }
                    size_t position = 1 + greek[g].size(), end = position;
                    while (end < key.size() && std::isdigit(static_cast<unsigned char>(key[end])))
                    {
                        end++;
                    }
                    const std::string suffix = key.substr(end);
                    if (suffix.size() < 3 || suffix.size() > 4)
                    {
                        continue;
                    }
                    for (const auto &cons : constellations)
                    {
                        if (stellar_identifier_key(cons.abbrev) != suffix.substr(0, 3))
                        {
                            continue;
                        }
                        const int component = end == position ? 0 : atoi(key.substr(position, end - position).c_str());
                        const std::string value = abbreviations[g]
                            + (component ? std::to_string(component) : " ") + cons.abbrev;
                        const int score = (component ? 8 : 0) + (value == original_bayer ? 2 : 0)
                            + (suffix.size() == 4 ? 1 : 0);
                        if (value.size() <= 7 && score > bayer_score)
                        {
                            bayer_score = score;
                            strcpy(star->Bayer, value.c_str());
                            star->BayerGrkno = g;
                            strcpy(star->constellation, cons.abbrev.c_str());
                            provenance(star, "Bayer", "SIMBAD", row, value);
                        }
                    }
                }
            }
            if (!gliese_value.empty() && gliese_value.size() < sizeof(star->Gliese))
            {
                strcpy(star->Gliese, gliese_value.c_str());
                provenance(star, "Gliese", "SIMBAD", row, gliese_value);
            }
            // Repair explicit numeric display names from the object's own source identity.
            const std::string current = trim(star->name);
            if (current.rfind("HD", 0) == 0 && star->HD)
            {
                const char last = current.back();
                const std::string component = last >= 'A' && last <= 'Z' ? " " + std::string(1, last) : "";
                name(star, "HD" + std::to_string(star->HD) + component, "SIMBAD", row);
            }
            else if (!current.empty() && std::isdigit(static_cast<unsigned char>(current[0]))
                && star->FlamsteedNo > 0 && atoi(current.c_str()) != star->FlamsteedNo)
            {
                for (const auto &cons : constellations)
                {
                    if (cons.abbrev == star->constellation)
                    {
                        name(star, std::to_string(star->FlamsteedNo) + " " + cons.genitive, "SIMBAD", row);
                    }
                }
            }
        }

        void simbad(const Row &row)
        {
            const auto names = identifiers(cell(row, "ids"));
            const std::string oid = cell(row, "oid");
            object_types[oid] = cell(row, "otype");
            const auto matches = match(names, number(row, "ra"), number(row, "dec"));
            std::set<std::string> keys;
            for (const auto &alias : names)
            {
                keys.insert(stellar_identifier_key(alias));
            }
            auto identity_score = [&](Star *star)
            {
                return (star->HD && keys.count("HD" + std::to_string(star->HD)) ? 4 : 0)
                    + (star->HIP && keys.count("HIP" + std::to_string(star->HIP)) ? 3 : 0)
                    + (star->HR && keys.count("HR" + std::to_string(star->HR)) ? 2 : 0)
                    + (keys.count(stellar_identifier_key(star->name)) ? 4 : 0);
            };
            int best_score = 0;
            for (Star *star : matches)
            {
                best_score = std::max(best_score, identity_score(star));
            }
            for (Star *star : matches)
            {
                if (identity_score(star) < best_score)
                {
                    continue;
                }
                if (object_ids.count(star) && object_ids[star] != oid)
                {
                    json conflict;
                    conflict["star"] = star->name;
                    conflict["source"] = "SIMBAD";
                    conflict["reason"] = "Multiple resolved objects match existing identifiers";
                    report["conflicts"].push_back(conflict);
                    continue;
                }
                object_ids[star] = oid;
                resolved_objects[oid].push_back(star);
                for (const auto &alias : names)
                {
                    add_alias(alias, star);
                }
                designation(star, names, row);
                const std::string quality = cell(row, "sp_qual");
                spectrum(star, cell(row, "sp_type"), "SIMBAD", row,
                    quality == "D" || quality == "E" ? 5 : 20);
                const double v = number(row, "V"), b = number(row, "B"), u = number(row, "U");
                if (v >= -40 && v < 99)
                {
                    adopt(star, "V", star->apparent_magnitude, v, "SIMBAD", row, "mag");
                }
                if (std::isfinite(b) && std::isfinite(v) && fabs(b - v) < 5)
                {
                    adopt(star, "B-V", star->BV_color, b - v, "SIMBAD", row, "mag");
                    star->has_catalog_BV = true;
                }
                if (std::isfinite(u) && std::isfinite(b) && fabs(u - b) < 5)
                {
                    adopt(star, "U-B", star->UB_color, u - b, "SIMBAD", row, "mag");
                    star->has_catalog_UB = true;
                }
                const double velocity = number(row, "rvz_radvel");
                if (std::isfinite(velocity) && fabs(velocity) < 3000 && !star->radial_velocity)
                {
                    adopt(star, "radial_velocity", star->radial_velocity, velocity * 1000,
                        "SIMBAD", row, "m/s");
                }
            }
        }

        void mermilliod(const Row &row)
        {
            const std::string component = cell(row, "m_LID");
            if (!component.empty() && component != "1" && component != "A")
            {
                return;
            }
            const auto matches = match(identifiers(cell(row, "SimbadName")), number(row, "_RA"), number(row, "_DE"));
            if (matches.size() != 1)
            {
                return;
            }
            Star *star = matches.front();
            const double bv = number(row, "B-V"), ub = number(row, "U-B"), v = number(row, "Vmag");
            if (bv > -1 && bv < 5)
            {
                adopt(star, "B-V", star->BV_color, bv, "Mermilliod", row, "mag", number(row, "e_B-V"));
                star->has_catalog_BV = true;
            }
            if (ub > -3 && ub < 5)
            {
                adopt(star, "U-B", star->UB_color, ub, "Mermilliod", row, "mag", number(row, "e_U-B"));
                star->has_catalog_UB = true;
            }
            if (v >= -40 && v < 99 && cell(row, "var").empty() && !star->variability_period)
            {
                adopt(star, "V", star->apparent_magnitude, v, "Mermilliod", row, "mag", number(row, "e_Vmag"));
            }
        }

        void skiff(const Row &row)
        {
            const auto matches = match(identifiers(cell(row, "Name")), number(row, "RAJ2000"),
                number(row, "DEJ2000"), true);
            if (matches.size() != 1 || cell(row, "Bibcode").empty())
            {
                return;
            }
            spectrum(matches.front(), cell(row, "SpType"), "Skiff", row,
                cell(row, "n_SpType") == "+" ? 30 : 20);
        }

        void gaia(const Row &row)
        {
            const double plx = number(row, "parallax"), error = number(row, "parallax_error");
            const double quality = number(row, "ruwe");
            if (!(plx > 0) || !(error > 0) || error / plx > 0.2 || !(quality > 0 && quality <= 1.4)
                || number(row, "number_of_neighbours") != 1)
            {
                return;
            }
            auto matches = match(identifiers("HIP " + cell(row, "hip")));
            if (matches.size() != 1)
            {
                return;
            }
            Star *star = matches.front();
            adopt(star, "parallax", star->parallax, plx / 1000 / 3600 * fiftyseventh,
                "Gaia_DR3", row, "rad", error / 1000 / 3600 * fiftyseventh);
            adopt(star, "distance", star->distance, 1000.0 / plx * parsec, "Gaia_DR3", row, "m");
            star->distance_known = true;
            const auto family = children.find(star);
            if (family != children.end())
            {
                for (Star *companion : family->second)
                {
                    if (!companion->HIP || companion->HIP == star->HIP)
                    {
                        adopt(companion, "distance", companion->distance, star->distance, "Gaia_DR3", row, "m");
                        adopt(companion, "parallax", companion->parallax, star->parallax, "Gaia_DR3", row, "rad");
                        companion->distance_known = true;
                    }
                }
            }
            // Gaia coordinates and tangent proper motion are referred to epoch J2016.0.
            const double ra = number(row, "ra") * fiftyseventh;
            const double dec = number(row, "dec") * fiftyseventh;
            const double epoch = number(row, "ref_epoch");
            const double pmra = number(row, "pmra") / 1000 / 3600 * fiftyseventh / oneyear;
            const double pmdec = number(row, "pmdec") / 1000 / 3600 * fiftyseventh / oneyear;
            if (std::isfinite(ra) && std::isfinite(dec) && std::isfinite(epoch)
                && std::isfinite(pmra) && std::isfinite(pmdec))
            {
                const double elapsed = (2000.0 - epoch) * oneyear;
                Point east(-cos(ra), 0, -sin(ra));
                Point north(sin(ra) * sin(dec), cos(dec), -cos(ra) * sin(dec));
                const Point position = Point::from_ra_dec(ra, dec, 1.0)
                    + east * (pmra * elapsed) + north * (pmdec * elapsed);
                adopt(star, "RA_J2000", star->right_ascension,
                    fmod(atan2(-position.x, position.z) + 2 * _pi, 2 * _pi), "Gaia_DR3", row, "rad");
                adopt(star, "Dec_J2000", star->declination, atan2(position.y, hypot(position.x, position.z)),
                    "Gaia_DR3", row, "rad");
                adopt(star, "proper_motion_RA", star->proper_motion_RA, pmra / std::max(1e-12, fabs(cos(dec))),
                    "Gaia_DR3", row, "rad/s");
                adopt(star, "proper_motion_dec", star->proper_motion_decl, pmdec, "Gaia_DR3", row, "rad/s");
            }
            const double rv = number(row, "radial_velocity");
            if (std::isfinite(rv) && !star->radial_velocity)
            {
                adopt(star, "radial_velocity", star->radial_velocity, rv * 1000, "Gaia_DR3", row, "m/s");
            }
            const double temperature = number(row, "teff_gspphot");
            const double low = number(row, "teff_gspphot_lower"), high = number(row, "teff_gspphot_upper");
            if (temperature >= 2500 && temperature <= 50000 && low <= temperature && high >= temperature
                && (high - low) / temperature < 0.2)
            {
                adopt(star, "temperature", star->temperature, temperature, "Gaia_DR3_GSPPhot_model", row,
                    "K", (high - low) / 2);
            }
        }

        std::pair<Star*, Star*> pair(const std::string &wds, const std::string &components,
            double ra = NAN, double dec = NAN)
        {
            const std::string comp = components.empty() ? "AB" : components;
            if (comp != "AB" && comp != "A,B")
            {
                return std::make_pair(nullptr, nullptr);
            }
            auto primary = match(identifiers("WDS J" + wds + "A"), ra, dec, false);
            if (primary.empty())
            {
                primary = match(std::vector<std::string>(), ra, dec, true);
            }
            auto secondary = match(identifiers("WDS J" + wds + "B"));
            if (primary.size() != 1)
            {
                return std::make_pair(nullptr, nullptr);
            }
            Star *a = primary.front();
            Star *b = secondary.size() == 1 ? secondary.front() : nullptr;
            if (!b && a->multisys && a->multisys->get_member('A') == a)
            {
                b = a->multisys->get_member('B');
            }
            return std::make_pair(a, a != b ? b : nullptr);
        }

        void wds(const Row &row)
        {
            const std::string flags = cell(row, "Notes");
            if (flags.find('I') != std::string::npos)
            {
                return;
            }
            const auto members = pair(cell(row, "WDS"), cell(row, "Comp"), number(row, "RAJ2000"), number(row, "DEJ2000"));
            if (!members.first)
            {
                return;
            }
            provenance(members.first, "WDS_component", "WDS", row, "A");
            if (members.second)
            {
                provenance(members.second, "WDS_component", "WDS", row, "B");
            }
            // WDS includes optical pairs and explicitly marks nonvisual passbands.
            const bool visual = flags.find_first_of("BKR") == std::string::npos;
            if (visual)
            {
                const bool resolved_measurement = members.second && report["provenance"]
                    .contains(std::to_string(members.second->seqno))
                    && report["provenance"][std::to_string(members.second->seqno)]["fields"].contains("V");
                if (members.second && !resolved_measurement && number(row, "mag2") > -40 && number(row, "mag2") < 99)
                {
                    adopt(members.second, "V", members.second->apparent_magnitude,
                        number(row, "mag2"), "WDS", row, "mag");
                }
                if (number(row, "mag1") > -40 && number(row, "mag1") < 99
                    && !(members.first->apparent_magnitude >= -40 && members.first->apparent_magnitude < 99))
                {
                    adopt(members.first, "V", members.first->apparent_magnitude,
                        number(row, "mag1"), "WDS", row, "mag");
                }
            }
            if (flags.find('S') != std::string::npos && members.second)
            {
                json conflict;
                conflict["star"] = members.second->name;
                conflict["source"] = "WDS";
                conflict["reason"] = "Source marks this pair as nonphysical; no physical orbit adopted";
                report["conflicts"].push_back(conflict);
            }
        }

        void sbx_alias(const Row &row)
        {
            const std::string sn = cell(row, "sn");
            const std::string catalog = cell(row, "catalog");
            std::string designation = catalog + " " + cell(row, "identifier");
            if (catalog == "Gaia")
            {
                designation = "Gaia " + cell(row, "version") + " " + cell(row, "identifier");
            }
            const auto matches = match(identifiers(designation));
            auto &members = sbx_members[sn];
            for (Star *star : matches)
            {
                if (std::find(members.begin(), members.end(), star) == members.end())
                {
                    members.push_back(star);
                }
            }
        }

        void sbx_orbit(const Row &row)
        {
            const std::string sn = cell(row, "sn");
            auto found = sbx_members.find(sn);
            auto system = sbx_systems.find(sn);
            if (found == sbx_members.end() || found->second.size() != 1 || system == sbx_systems.end())
            {
                return;
            }
            const double period = number(row, "period"), eccentricity = number(row, "eccentricity");
            if (!(period > 0) || !(eccentricity >= 0 && eccentricity < 1))
            {
                return;
            }
            const double grade = number(row, "grade");
            const double legacy_grade = number(row, "grade_legacy");
            if ((std::isfinite(grade) && grade > 3) || (std::isfinite(legacy_grade) && legacy_grade > 3))
            {
                return;
            }
            const auto rank = std::make_pair(std::isfinite(grade) ? -int(grade) : -3, cell(row, "bibcode"));
            if (sbx_ranks.count(sn) && sbx_ranks[sn] > rank)
            {
                return;
            }
            Star *a = found->second.front();
            const std::string component = cell(system->second, "component");
            if ((!component.empty() && component != "AB" && component != "A,B") || !a->multisys
                || a->multisys->get_member('A') != a || a->multisys->get_member('C'))
            {
                return;
            }
            Star *b = a->multisys->get_member('B');
            if (!b || !b->orbit || b->orbit->center != a)
            {
                return;
            }
            sbx_ranks[sn] = rank;
            adopt(b, "orbital_period", b->orbit->period, period * oneday, "SBX", row, "s", number(row, "period_error") * oneday);
            adopt(b, "orbital_eccentricity", b->orbit->eccentricity, eccentricity, "SBX", row);
            const double omega = number(row, "periastron_argument");
            if (std::isfinite(omega))
            {
                // Spectroscopic arguments refer to the primary, 180 degrees from the secondary.
                adopt(b, "orbital_periapsis", b->orbit->arg_periapsis,
                    fmod(omega + 180.0, 360.0) * fiftyseventh, "SBX", row, "rad");
            }
            // A spectroscopic projected axis is not the relative visual semimajor axis.
            provenance(b, "projected_velocity_K1", "SBX", row, cell(row, "K1"), "km/s");
            provenance(b, "projected_velocity_K2", "SBX", row, cell(row, "K2"), "km/s");
            if (cell(system->second, "band2") == "V" && number(system->second, "mag2") > -40
                && number(system->second, "mag2") < 99)
            {
                adopt(b, "V", b->apparent_magnitude, number(system->second, "mag2"), "SBX", system->second, "mag");
            }
        }

        void debcat(const Row &row)
        {
            std::string identifier = cell(row, "System");
            std::replace(identifier.begin(), identifier.end(), '_', ' ');
            auto matches = match(identifiers(identifier));
            if (matches.size() != 1)
            {
                return;
            }
            Star *a = matches.front();
            if (!a->multisys || a->multisys->get_member('A') != a || a->multisys->get_member('C'))
            {
                return;
            }
            Star *b = a->multisys->get_member('B');
            if (!b)
            {
                return;
            }
            auto mass = [&](int component)
            {
                const double value = number(row, "logM" + std::to_string(component));
                return value > -9 ? pow(10.0, value) : NAN;
            };
            const double m1 = mass(1), m2 = mass(2);
            if (!(m1 > 0 && m2 > 0 && a->mass > 0 && b->mass > 0))
            {
                return;
            }
            const double direct = fabs(log((a->mass / b->mass) / (m1 / m2)));
            const double reverse = fabs(log((a->mass / b->mass) / (m2 / m1)));
            if (std::min(direct, reverse) > 0.2 || fabs(direct - reverse) < 0.05)
            {
                json conflict;
                conflict["star"] = a->name;
                conflict["source"] = "DEBCat";
                conflict["reason"] = "Component 1/2 assignment is ambiguous; physical measurements retained for review";
                conflict["measurements"] = row;
                report["conflicts"].push_back(conflict);
                return;
            }
            Star *first = direct < reverse ? a : b;
            Star *second = direct < reverse ? b : a;
            for (int component = 1; component <= 2; component++)
            {
                Star *star = component == 1 ? first : second;
                const std::string suffix = std::to_string(component);
                const double mass_log = number(row, "logM" + suffix);
                const double radius_log = number(row, "logR" + suffix);
                const double temp_log = number(row, "logT" + suffix);
                if (mass_log > -9)
                {
                    adopt(star, "mass", star->mass, pow(10.0, mass_log) * solar_mass, "DEBCat", row, "g");
                }
                if (radius_log > -9)
                {
                    adopt(star, "radius", star->volumetric_mean_radius, pow(10.0, radius_log) * solar_radius,
                        "DEBCat", row, "m");
                }
                if (temp_log > 2 && temp_log < 6)
                {
                    adopt(star, "temperature", star->temperature, pow(10.0, temp_log), "DEBCat", row, "K");
                }
            }
        }

        void orb6(const Row &row)
        {
            const double grade = number(row, "grade");
            if (!(grade >= 1 && grade <= 3))
            {
                return;
            }
            const auto members = pair(cell(row, "WDS"), cell(row, "Comp"));
            Star *a = members.first, *b = members.second;
            if (!a || !b || !(a->distance > 0) || !std::isfinite(a->distance))
            {
                return;
            }
            double period = number(row, "period"), axis = number(row, "axis");
            const std::string period_unit = cell(row, "period_unit"), axis_unit = cell(row, "axis_unit");
            if (period_unit == "y")
            {
                period *= oneyear;
            }
            else if (period_unit == "c")
            {
                period *= 100 * oneyear;
            }
            else if (period_unit == "d")
            {
                period *= oneday;
            }
            else if (period_unit == "h")
            {
                period *= 3600;
            }
            else if (period_unit == "m")
            {
                period *= 60;
            }
            else
            {
                return;
            }
            if (axis_unit == "m")
            {
                axis /= 1000;
            }
            else if (axis_unit == "u")
            {
                axis /= 1000000;
            }
            else if (axis_unit != "a")
            {
                return;
            }
            axis *= fiftyseventh / 3600 * a->distance;
            const double eccentricity = number(row, "eccentricity");
            const double inclination = number(row, "inclination");
            const double node = number(row, "node"), periapsis = number(row, "periapsis");
            if (!(period > 0 && axis > 0 && eccentricity >= 0 && eccentricity < 1)
                || !std::isfinite(period) || !std::isfinite(axis) || !std::isfinite(inclination)
                || !std::isfinite(node) || !std::isfinite(periapsis))
            {
                return;
            }
            if (b->orbit && b->orbit->center && b->orbit->center != a)
            {
                return;
            }
            if (!b->orbit)
            {
                b->orbit = new Orbit();
            }
            b->orbit->center = a;
            if (!a->multisys && !b->multisys)
            {
                a->set_component('A', a);
                b->set_component('B', a);
            }
            adopt(b, "orbital_period", b->orbit->period, period, "ORB6", row, "s");
            adopt(b, "orbital_axis", b->orbit->semimajor_axis, axis, "ORB6", row, "m");
            adopt(b, "orbital_eccentricity", b->orbit->eccentricity, eccentricity, "ORB6", row);
            adopt(b, "orbital_inclination", b->orbit->heliocentric_inclination,
                inclination * fiftyseventh, "ORB6", row, "rad");
            adopt(b, "orbital_node", b->orbit->heliocentric_node, node * fiftyseventh, "ORB6", row, "rad");
            adopt(b, "orbital_periapsis", b->orbit->arg_periapsis, periapsis * fiftyseventh, "ORB6", row, "rad");
            const double epoch = number(row, "periastron_time");
            const std::string epoch_unit = cell(row, "epoch_unit");
            double julian = NAN;
            if (epoch_unit == "d")
            {
                julian = epoch + 2400000;
            }
            else if (epoch_unit == "m")
            {
                julian = epoch + 2400000.5;
            }
            else if (epoch_unit == "y")
            {
                julian = 2415020.31352 + (epoch - 1900.0) * 365.242198781;
            }
            if (std::isfinite(julian))
            {
                adopt(b, "orbital_epoch", b->orbit->epoch, julian, "ORB6", row, "JD");
                adopt(b, "orbital_anomaly", b->orbit->mean_anomaly, 0, "ORB6", row, "rad");
            }
        }

        void iau(const Row &row)
        {
            std::vector<std::string> names;
            names.push_back(cell(row, "Simbad spelling"));
            names.push_back(cell(row, "Designation"));
            const std::string hip = cell(row, "HIP");
            if (!hip.empty() && hip != "-")
            {
                names.push_back("HIP " + hip);
            }
            auto matches = match(names, number(row, "RA"), number(row, "DEC"));
            if (matches.size() != 1)
            {
                return;
            }
            const std::string value = cell(row, "proper names");
            name(matches.front(), value, "IAU", row);
        }

        void vsx(const Row &row)
        {
            if (cell(row, "n_max") != "V" || cell(row, "n_min") != "V"
                || !cell(row, "u_max").empty() || !cell(row, "u_min").empty()
                || !cell(row, "f_min").empty())
            {
                return;
            }
            const auto matches = match(identifiers(cell(row, "Name")), number(row, "RAJ2000"), number(row, "DEJ2000"));
            if (matches.size() != 1)
            {
                return;
            }
            Star *star = matches.front();
            const double bright = number(row, "max"), faint = number(row, "min");
            if (bright > -40 && faint < 99 && bright <= faint)
            {
                adopt(star, "bright", star->minmag, bright, "VSX", row, "mag V");
                adopt(star, "faint", star->maxmag, faint, "VSX", row, "mag V");
            }
            const double period = number(row, "Period");
            if (period > 0 && cell(row, "l_Period").empty() && cell(row, "u_Period").empty()
                && !star->variability_period)
            {
                adopt(star, "variability_period", star->variability_period, period * oneday, "VSX", row, "s");
            }
        }

        void merge_duplicates()
        {
            size_t count = 0;
            for (const auto &entry : resolved_objects)
            {
                const auto &members = entry.second;
                if (members.size() < 2 || object_types[entry.first] == "**")
                {
                    continue;
                }
                Star *keep = members.front();
                for (size_t i = 1; i < members.size(); i++)
                {
                    Star *discard = members[i];
                    if (keep == discard || keep->deleted || discard->deleted
                        || (keep->HD && discard->HD && keep->HD != discard->HD)
                        || (keep->HIP && discard->HIP && keep->HIP != discard->HIP)
                        || (!keep->HD && !keep->HIP) || separation(keep,
                            discard->right_ascension * fiftyseven, discard->declination * fiftyseven) > 1)
                    {
                        continue;
                    }
                    if (!keep->multisys && discard->multisys)
                    {
                        std::swap(keep, discard);
                    }
                    if (discard->multisys || (discard->orbit && keep->orbit
                        && discard->orbit->center != keep->orbit->center))
                    {
                        continue;
                    }
                    for (int j = 0; objects[j]; j++)
                    {
                        if (objects[j]->orbit && objects[j]->orbit->center == discard)
                        {
                            objects[j]->orbit->center = keep;
                        }
                        if (objects[j]->cenobj == discard)
                        {
                            objects[j]->cenobj = keep;
                        }
                    }
                    if (!keep->orbit && discard->orbit)
                    {
                        keep->orbit = discard->orbit;
                        discard->orbit = nullptr;
                    }
                    if (hdcache && discard->HD && hdcache[discard->HD] == discard)
                    {
                        hdcache[discard->HD] = keep;
                    }
                    if (hipcache && discard->HIP && hipcache[discard->HIP] == discard)
                    {
                        hipcache[discard->HIP] = keep;
                    }
                    discard->deleted = true;
                    json merge;
                    merge["oid"] = entry.first;
                    merge["kept"] = keep->name;
                    merge["removed"] = discard->name;
                    merge["removed_id"] = discard->alienorumid;
                    report["merges"].push_back(merge);
                    count++;
                }
            }
            report["merged_rows"] = count;
        }
    };

    std::string html_text(const std::string &value)
    {
        std::string text;
        bool tag = false;
        for (size_t i = 0; i < value.size(); i++)
        {
            if (value[i] == '<')
            {
                tag = true;
            }
            else if (value[i] == '>')
            {
                tag = false;
            }
            else if (!tag)
            {
                if (value.compare(i, 6, "&nbsp;") == 0)
                {
                    text += ' ';
                    i += 5;
                }
                else if (value.compare(i, 5, "&amp;") == 0)
                {
                    text += '&';
                    i += 4;
                }
                else
                {
                    text += value[i];
                }
            }
        }
        return trim(text);
    }

    size_t read_iau(const std::filesystem::path &path, const std::function<void(const Row&)> &consume)
    {
        std::ifstream file(path);
        const std::string html((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t position = html.find("<table");
        if (position == std::string::npos)
        {
            throw std::runtime_error("IAU source has no name table");
        }
        const size_t end = html.find("</table>", position);
        std::vector<std::string> header;
        size_t count = 0;
        while ((position = html.find("<tr", position)) != std::string::npos && position < end)
        {
            const size_t finish = html.find("</tr>", position);
            if (finish == std::string::npos)
            {
                break;
            }
            const std::string row = html.substr(position, finish - position);
            std::vector<std::string> fields;
            size_t cell_position = 0;
            while ((cell_position = row.find(header.empty() ? "<th" : "<td", cell_position)) != std::string::npos)
            {
                const size_t begin = row.find('>', cell_position);
                const size_t close = row.find(header.empty() ? "</th>" : "</td>", begin);
                if (begin == std::string::npos || close == std::string::npos)
                {
                    break;
                }
                fields.push_back(html_text(row.substr(begin + 1, close - begin - 1)));
                cell_position = close + 5;
            }
            if (header.empty())
            {
                header = fields;
            }
            else if (fields.size() == header.size())
            {
                Row record;
                for (size_t i = 0; i < header.size(); i++)
                {
                    record[lowercase(header[i])] = fields[i];
                }
                consume(record);
                count++;
            }
            position = finish + 5;
        }
        if (!count)
        {
            throw std::runtime_error("IAU source contains no parsed star names");
        }
        return count;
    }

    size_t read_debcat(const std::filesystem::path &path, const std::function<void(const Row&)> &consume)
    {
        std::ifstream file(path);
        std::string line;
        std::vector<std::string> columns;
        size_t count = 0;
        while (std::getline(file, line))
        {
            std::istringstream stream(line);
            std::vector<std::string> fields;
            std::string value;
            while (stream >> value)
            {
                fields.push_back(value);
            }
            if (fields.empty())
            {
                continue;
            }
            if (fields.front() == "#")
            {
                columns.assign(fields.begin() + 1, fields.end());
                continue;
            }
            if (columns.empty() || columns.size() != fields.size())
            {
                throw std::runtime_error("DEBCat schema mismatch");
            }
            Row row;
            for (size_t i = 0; i < columns.size(); i++)
            {
                row[lowercase(columns[i])] = fields[i];
            }
            consume(row);
            count++;
        }
        return count;
    }

    size_t read_orb6(const std::filesystem::path &path, const std::function<void(const Row&)> &consume)
    {
        std::ifstream file(path);
        std::string line;
        size_t count = 0;
        while (std::getline(file, line))
        {
            if (line.size() < 245 || (line[9] != '+' && line[9] != '-'))
            {
                continue;
            }
            Row row;
            auto field = [&](const std::string &key, size_t start, size_t length)
            {
                row[lowercase(key)] = trim(line.substr(start, length));
            };
            field("WDS", 19, 10);
            field("period", 80, 12);
            field("period_unit", 92, 1);
            field("axis", 104, 10);
            field("axis_unit", 114, 1);
            field("inclination", 123, 9);
            field("node", 141, 9);
            field("periastron_time", 159, 15);
            field("epoch_unit", 174, 1);
            field("eccentricity", 186, 9);
            field("periapsis", 203, 9);
            field("grade", 233, 1);
            field("bibcode", 237, 8);
            const std::string discoverer = trim(line.substr(30, 15));
            const std::regex component(".*[0-9]\\s*([A-Za-z,\\-]+)$");
            std::smatch match;
            if (std::regex_match(discoverer, match, component))
            {
                row["comp"] = match[1];
            }
            consume(row);
            count++;
        }
        if (!count)
        {
            throw std::runtime_error("ORB6 source contains no parsed orbits");
        }
        return count;
    }
}

std::vector<std::string> alienorum::stellar_table_fields(const std::string &line, char delimiter)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++)
    {
        const char c = line[i];
        if (c == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
            {
                field += '"';
                i++;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else if (c == delimiter && !quoted)
        {
            fields.push_back(trim(field));
            field.clear();
        }
        else if (c != '\r')
        {
            field += c;
        }
    }
    if (quoted)
    {
        throw std::runtime_error("Unterminated quoted catalog field");
    }
    fields.push_back(trim(field));
    return fields;
}

std::string alienorum::stellar_identifier_key(const std::string &identifier)
{
    std::string key;
    for (unsigned char c : identifier)
    {
        if (!std::isspace(c))
        {
            key += std::toupper(c);
        }
    }
    if (key.rfind("NAME", 0) == 0)
    {
        key.erase(0, 4);
    }
    if (key.rfind("V*", 0) == 0)
    {
        key.erase(0, 2);
    }
    static const std::regex numbered("^(HD|HIP|HR|GJ)([0-9]+)(.*)$");
    static const std::regex dm("^(BD|CD|CP)([+-])([0-9]{2})([0-9]+)([A-Z]?)$");
    std::smatch match;
    const bool numeric_catalog = key.rfind("HD", 0) == 0 || key.rfind("HIP", 0) == 0
        || key.rfind("HR", 0) == 0 || key.rfind("GJ", 0) == 0;
    const bool dm_catalog = key.rfind("BD", 0) == 0 || key.rfind("CD", 0) == 0 || key.rfind("CP", 0) == 0;
    if (numeric_catalog && std::regex_match(key, match, numbered))
    {
        key = match[1].str() + std::to_string(strtoul(match[2].str().c_str(), nullptr, 10)) + match[3].str();
    }
    else if (dm_catalog && std::regex_match(key, match, dm))
    {
        key = match[1].str() + match[2].str() + match[3].str()
            + std::to_string(strtoul(match[4].str().c_str(), nullptr, 10)) + match[5].str();
    }
    return key;
}

std::vector<StellarReferenceSource> alienorum::stellar_reference_sources()
{
    std::vector<StellarReferenceSource> sources;
    auto add = [&](const std::string &name, const std::vector<std::string> &columns)
    {
        StellarReferenceSource source;
        source.name = name;
        source.format = "tsv";
        source.columns = columns;
        sources.push_back(source);
    };
    for (int page = 0; page < 12; page++)
    {
        add(page == 0 ? "SIMBAD" : "SIMBAD_" + std::to_string(page),
            std::vector<std::string>
            {
                "oid", "main_id", "ids", "ra", "dec", "sp_type"
            });
    }
    add("Mermilliod",
        std::vector<std::string>
        {
            "SimbadName", "m_LID", "Vmag", "B-V", "U-B"
        });
    add("Skiff",
        std::vector<std::string>
        {
            "Name", "SpType", "Bibcode", "RAJ2000", "DEJ2000"
        });
    add("WDS",
        std::vector<std::string>
        {
            "WDS", "Comp", "mag1", "mag2", "sep2", "pa2", "RAJ2000", "DEJ2000"
        });
    add("SBX_systems",
        std::vector<std::string>
        {
            "sn", "ra", "dec"
        });
    add("SBX_alias",
        std::vector<std::string>
        {
            "sn", "catalog", "identifier"
        });
    add("SBX_orbits",
        std::vector<std::string>
        {
            "sn", "period", "eccentricity", "bibcode"
        });
    add("Gaia_DR3",
        std::vector<std::string>
        {
            "source_id", "hip", "parallax", "parallax_error", "ruwe"
        });
    add("VSX",
        std::vector<std::string>
        {
            "OID", "Name", "max", "min", "n_max", "n_min", "Period"
        });
    StellarReferenceSource debcat;
    debcat.name = "DEBCat";
    debcat.format = "debcat";
    sources.push_back(debcat);
    StellarReferenceSource orb6;
    orb6.name = "ORB6";
    orb6.format = "orb6";
    sources.push_back(orb6);
    StellarReferenceSource iau;
    iau.name = "IAU";
    iau.format = "iau";
    sources.push_back(iau);
    std::ifstream manifest("catalogs/urls.dat");
    std::string line;
    while (std::getline(manifest, line))
    {
        std::istringstream fields(line);
        std::string group, filename, url;
        if (!(fields >> group >> filename >> url) || group != "reference")
        {
            continue;
        }
        for (auto &source : sources)
        {
            if (filename == source.name + "." + source.format)
            {
                source.url = url;
            }
        }
    }
    return sources;
}

json alienorum::reconcile_stellar_sources(CelestialObject **objects, const std::string &cache_directory,
    bool allow_download)
{
    json report;
    report["schema"] = 1;
    report["sources"] = json::array();
    report["changes"] = json::array();
    report["conflicts"] = json::array();
    report["provenance"] = json::object();
    report["merges"] = json::array();
    if (!objects)
    {
        return report;
    }
    std::filesystem::create_directories(cache_directory);
    Context context(objects, report);
    const auto sources = stellar_reference_sources();
    if (allow_download && !radio_silence)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::vector<std::future<bool>> downloads;
        for (const auto &source : sources)
        {
            const auto path = std::filesystem::path(cache_directory) / (source.name + "." + source.format);
            if (std::filesystem::exists(path))
            {
                continue;
            }
            if (source.url.empty())
            {
                std::cerr << "Missing reference endpoint in catalogs/urls.dat: " << source.name << std::endl;
                continue;
            }
            std::cout << "Fetching stellar reference " << source.name << std::endl;
            downloads.push_back(std::async(std::launch::async, [source, path]()
            {
                return fetch(source.url, path);
            }));
            if (downloads.size() == 3)
            {
                for (auto &download : downloads)
                {
                    download.get();
                }
                downloads.clear();
            }
        }
        for (auto &download : downloads)
        {
            download.get();
        }
    }
    for (const auto &source : sources)
    {
        const auto path = std::filesystem::path(cache_directory) / (source.name + "." + source.format);
        json status;
        status["name"] = source.name;
        status["url"] = source.url;
        status["available"] = std::filesystem::exists(path);
        if (status["available"].get<bool>() && objects[0])
        {
            try
            {
                const size_t changes_before = report["changes"].size();
                auto consume = [&](const Row &row)
                {
                    if (source.name.rfind("SIMBAD", 0) == 0)
                    {
                        context.simbad(row);
                    }
                    else if (source.name == "Mermilliod")
                    {
                        context.mermilliod(row);
                    }
                    else if (source.name == "Skiff")
                    {
                        context.skiff(row);
                    }
                    else if (source.name == "WDS")
                    {
                        context.wds(row);
                    }
                    else if (source.name == "Gaia_DR3")
                    {
                        context.gaia(row);
                    }
                    else if (source.name == "SBX_systems")
                    {
                        context.sbx_systems[cell(row, "sn")] = row;
                    }
                    else if (source.name == "SBX_alias")
                    {
                        context.sbx_alias(row);
                    }
                    else if (source.name == "SBX_orbits")
                    {
                        context.sbx_orbit(row);
                    }
                    else if (source.name == "VSX")
                    {
                        context.vsx(row);
                    }
                    else if (source.name == "DEBCat")
                    {
                        context.debcat(row);
                    }
                    else if (source.name == "ORB6")
                    {
                        context.orb6(row);
                    }
                    else if (source.name == "IAU")
                    {
                        context.iau(row);
                    }
                };
                size_t rows = 0;
                if (source.format == "tsv")
                {
                    rows = read_table(path, source, consume);
                }
                else if (source.format == "debcat")
                {
                    rows = read_debcat(path, consume);
                }
                else if (source.format == "orb6")
                {
                    rows = read_orb6(path, consume);
                }
                else if (source.format == "iau")
                {
                    rows = read_iau(path, consume);
                }
                status["rows"] = rows;
                status["changes"] = report["changes"].size() - changes_before;
                status["valid"] = true;
                std::cout << "Stellar reference " << source.name << ": " << rows << " rows, "
                    << status["changes"] << " field updates" << std::endl;
            }
            catch (const std::exception &error)
            {
                status["valid"] = false;
                status["error"] = error.what();
                std::cerr << error.what() << std::endl;
            }
        }
        report["sources"].push_back(status);
    }
    context.merge_duplicates();
    report["matched_objects"] = context.object_ids.size();
    bool complete = true;
    for (const auto &source : report["sources"])
    {
        complete = complete && source["available"].get<bool>() && source.value("valid", true);
    }
    report["complete"] = complete;
    return report;
}
