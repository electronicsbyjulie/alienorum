#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <curl/curl.h>
#include "cat.h"
#include "numeric.h"
#include "../globals.h"

using namespace alienorum;

namespace
{
    const std::string nasa = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync";
    const std::string vizier = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync";
    const std::string hypatia = "https://hypatiacatalog.com/hypatia/api/v2/";
    const std::vector<std::string> elements = {"Fe", "Mg", "Si", "C", "O"};

    double number(const json& row, const std::string& key)
    {
        auto it = row.find(key);
        return it != row.end() && it->is_number() ? it->get<double>() : NAN;
    }

    std::string text(const json& row, const std::string& key)
    {
        auto it = row.find(key);
        return it != row.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    std::string normalized(const std::string& value)
    {
        std::string result;
        for (unsigned char c : value) if (!std::isspace(c) && c != '-') result += std::tolower(c);
        return result;
    }

    std::string escaped(const std::string& value)
    {
        static const char hex[] = "0123456789ABCDEF";
        std::string result;
        for (unsigned char c : value)
        {
            if (std::isalnum(c) || c == '_' || c == '-' || c == '.') result += c;
            else
            {
                result += '%';
                result += hex[c >> 4];
                result += hex[c & 15];
            }
        }
        return result;
    }

    uint64_t identifier(const std::string& name, const std::string& prefix)
    {
        const std::string key = normalized(name);
        if (key.rfind(prefix, 0) != 0 || key.size() == prefix.size()) return 0;
        const std::string digits = numeric_text(key.substr(prefix.size()));
        if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) return 0;
        try { return std::stoull(digits); } catch (...) { return 0; }
    }

    bool read_snapshot(const std::string& path, json& table)
    {
        std::ifstream input(path);
        if (!input) return false;
        table = json::parse(input, nullptr, false);
        normalize_uncertain_numbers(table);
        return table.is_object() && table.contains("rows") && table["rows"].is_array();
    }

    bool save_snapshot(const std::string& path, const json& table)
    {
        if (abort_load) return false;
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
        const std::string temporary = path + ".tmp";
        std::ofstream output(temporary, std::ios::binary);
        output << table.dump() << '\n';
        output.close();
        if (output && !abort_load)
        {
            std::filesystem::rename(temporary, path, error);
            if (!error) return true;
        }
        std::filesystem::remove(temporary, error);
        return false;
    }

    bool tap_rows(const json& response, json& rows)
    {
        if (response.is_array()) { rows = response; return true; }
        if (!response.is_object() || !response.contains("data") || !response["data"].is_array()) return false;
        const json columns = response.value("metadata", response.value("columns", json::array()));
        if (!columns.is_array() || columns.empty()) return false;
        rows = json::array();
        for (const auto& values : response["data"])
        {
            if (!values.is_array() || values.size() != columns.size()) return false;
            json row = json::object();
            for (size_t i = 0; i < columns.size(); ++i)
            {
                const std::string name = text(columns[i], "name");
                if (name.empty()) return false;
                row[name] = values[i];
            }
            rows.push_back(std::move(row));
        }
        return true;
    }

    json measurement(double value, double error, const std::string& ratio, const std::string& kind)
    {
        return
        {
            {"dex", value}, {"error", std::isfinite(error) && error >= 0 ? json(error) : json(nullptr)},
            {"ratio", ratio}, {"quality", "accepted"}, {"error_kind", kind}
        };
    }
}

bool CatalogReader::fetch_stellar_catalog_json(const std::string& url, const std::string& form, json& result)
{
    if (abort_load || radio_silence) return false;
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (!form.empty()) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* bytes, size_t size, size_t count, void* context) -> size_t
    {
        const size_t length = size * count;
        auto* output = static_cast<std::string*>(context);
        if (length > 128 * 1024 * 1024 || output->size() > 128 * 1024 * 1024 - length) return 0;
        output->append(bytes, length);
        return length;
    });
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int
    {
        return abort_load ? 1 : 0;
    });
    const CURLcode status = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (status != CURLE_OK || abort_load)
    {
        if (!abort_load) std::cerr << "Stellar catalog transfer failed: " << url.substr(0, url.find('?'))
            << " (" << curl_easy_strerror(status) << ")" << std::endl;
        return false;
    }
    result = json::parse(response, nullptr, false);
    normalize_uncertain_numbers(result);
    return !result.is_discarded();
}

void CatalogReader::prepare_stellar_chemistry()
{
    // The condensed file already contains MET1 and CHEM1. Its archive counts too.
    const std::string condensed = get_condensed_starcat_name();
    if (std::filesystem::exists(condensed) || std::filesystem::exists(condensed + ".gz") || abort_load) return;
    const auto tap = [&](const std::string& endpoint, const std::string& query, int maximum, json& rows)
    {
        json response;
        const std::string form = "request=doQuery&lang=ADQL&format=json&maxrec=" + std::to_string(maximum) + "&query=" + escaped(query);
        // Some TAP gateways accept their sync queries only through GET.
        if (!fetch_stellar_catalog_json(endpoint, form, response) &&
            !fetch_stellar_catalog_json(endpoint + "?" + form, "", response)) return false;
        return tap_rows(response, rows) && rows.size() < static_cast<size_t>(maximum);
    };
    const auto snapshot = [&](const std::string& path, const std::string& endpoint, const std::string& query, int maximum, json& table)
    {
        if (std::filesystem::exists(path)) return read_snapshot(path, table);
        json rows;
        if (!tap(endpoint, query, maximum, rows) || rows.empty()) return false;
        table = {{"schema", 1}, {"endpoint", endpoint}, {"query", query}, {"rows", std::move(rows)}};
        return save_snapshot(path, table);
    };
    std::map<std::string, std::set<Star*>> identities;
    for (int i = 0; cels && i < ncelobjs; ++i)
    {
        if (!cels[i] || cels[i]->typeclass() != class_star) continue;
        Star* star = static_cast<Star*>(cels[i]);
        identities[normalized(star->name)].insert(star);
        for (const auto& alias : star->catalog_aliases) identities[normalized(alias)].insert(star);
        if (star->HD) identities["hd" + std::to_string(star->HD)].insert(star);
        if (star->HIP) identities["hip" + std::to_string(star->HIP)].insert(star);
    }
    const auto match = [&](const std::vector<std::string>& names) -> Star*
    {
        std::set<Star*> matches;
        for (const auto& name : names)
        {
            const auto found = identities.find(normalized(name));
            if (found != identities.end()) matches.insert(found->second.begin(), found->second.end());
        }
        return matches.size() == 1 ? *matches.begin() : nullptr;
    };
    json table;
    const auto apply_metals = [&](const json& rows, const std::string& source)
    {
        for (const auto& row : rows)
        {
            Star* host = match({text(row, "hostname"), text(row, "hd_name"), text(row, "hip_name")});
            if (!host || std::isfinite(host->metallicity)) continue;
            const double limit = number(row, "st_metlim");
            if (std::isfinite(limit) && limit != 0) continue;
            ExoRow input;
            input.st_met = number(row, "st_met");
            input.st_meterr1 = number(row, "st_meterr1");
            input.st_meterr2 = number(row, "st_meterr2");
            input.st_metratio = text(row, "st_metratio");
            input.st_met_source = source;
            apply_exoplanet_metallicity(input, host);
        }
    };
    if (snapshot("catalogs/NASA/stellar_metallicity.json", nasa,
        "select distinct hostname,hd_name,hip_name,st_met,st_meterr1,st_meterr2,st_metlim,st_metratio "
        "from pscomppars where st_met is not null and st_metlim=0", 50000, table))
        apply_metals(table["rows"], "NASA pscomppars");
    if (snapshot("catalogs/ExoplanetEU/stellar_metallicity.json", "http://voparis-tap-planeto.obspm.fr/tap/sync",
        "select distinct star_name as hostname,star_metallicity as st_met from exoplanet.epn_core "
        "where star_metallicity is not null and publication_status='Published in a refereed paper'", 50000, table))
    {
        for (auto& row : table["rows"]) row["st_metratio"] = "[Fe/H]";
        apply_metals(table["rows"], "exoplanet.eu");
    }
    if (snapshot("catalogs/XHIP/stellar_metallicity.json", vizier,
        "select HIP,Comp,\"[Fe/H]\",\"e_[Fe/H]\",\"q_[Fe/H]\" from \"V/137D/XHIP\" where \"[Fe/H]\" is not null", 50000, table))
    {
        for (const auto& row : table["rows"])
        {
            const double hip = number(row, "HIP"), value = number(row, "[Fe/H]");
            const std::string quality = text(row, "q_[Fe/H]");
            if (!std::isfinite(hip) || !std::isfinite(value) || (quality != "A" && quality != "B" && quality != "C")) continue;
            Star* host = match({"HIP " + std::to_string(static_cast<uint64_t>(hip))});
            if (!host || std::isfinite(host->metallicity)) continue;
            host->metallicity = value;
            host->metallicity_ratio = "[Fe/H]";
            host->metallicity_source = "XHIP V/137D " + quality;
            const double error = number(row, "e_[Fe/H]");
            if (quality == "A" && std::isfinite(error) && error >= 0 && error < 9.99)
            {
                host->metallicity_error_upper = error;
                host->metallicity_error_lower = -error;
            }
        }
    }

    const std::string hypatia_path = "catalogs/Hypatia/stellar_abundances.json";
    if (!std::filesystem::exists(hypatia_path) && !abort_load)
    {
        std::set<std::string> names;
        bool complete = true;
        for (const auto& axes : {std::make_pair("Mg", "Si"), std::make_pair("C", "O")})
        {
            json response;
            const std::string url = hypatia + "data/?mode=scatter&xaxis1=Fe&yaxis1=" + axes.first + "&zaxis1=" + axes.second + "&solarnorm=asplund09";
            if (!fetch_stellar_catalog_json(url, "", response) || !response.is_object() ||
                !response.contains("values") || !response["values"].is_array()) { complete = false; break; }
            for (const auto& row : response["values"])
            {
                const std::string name = text(row, "name");
                // Canonical SIMBAD names can differ from local display names. Import the
                // source catalog, then match its explicit identifiers from all_names.
                if (!name.empty()) names.insert(name);
            }
        }
        std::vector<std::string> targets(names.begin(), names.end());
        std::map<std::string, json> records;
        for (size_t start = 0; complete && start < targets.size() && !abort_load; start += 48)
        {
            std::vector<std::future<json>> batches;
            for (size_t offset = start; offset < std::min(start + 48, targets.size()); offset += 12)
            {
                std::string url = hypatia + "composition?";
                for (size_t i = offset; i < std::min(offset + 12, targets.size()); ++i)
                    for (const auto& element : elements)
                        url += "name=" + escaped(targets[i]) + "&element=" + normalized(element) + "&solarnorm=asplund09&";
                batches.push_back(std::async(std::launch::async, [&, url]()
                {
                    json response;
                    if (!fetch_stellar_catalog_json(url, "", response) || !response.is_array()) return json(nullptr);
                    return response;
                }));
            }
            for (auto& batch : batches)
            {
                const json response = batch.get();
                if (!response.is_array()) { complete = false; continue; }
                for (const auto& entry : response)
                {
                    const std::string element = text(entry, "element");
                    std::string name = text(entry, "name");
                    if (name.empty()) name = text(entry, "requested_name");
                    const double value = number(entry, "median_value");
                    if (name.empty() || text(entry, "solarnorm") != "asplund09" || !std::isfinite(value) || fabs(value) > 8 ||
                        std::find(elements.begin(), elements.end(), element) == elements.end()) continue;
                    json& record = records[name];
                    if (record.is_null()) record = {{"name", name}, {"elements", json::object()}};
                    json aliases = entry.value("all_names", json::array());
                    if (!aliases.is_array()) aliases = json::array();
                    aliases.push_back(name);
                    for (const auto& kind : {"hip", "hd", "gaiadr3"})
                    {
                        std::set<uint64_t> ids;
                        for (const auto& alias : aliases)
                            if (alias.is_string())
                                if (const uint64_t id = identifier(alias.get<std::string>(), kind)) ids.insert(id);
                        if (ids.size() == 1) record[std::string(kind) == "gaiadr3" ? "gaia" : kind] = *ids.begin();
                    }
                    record["elements"][element] = measurement(value, number(entry, "plusminus"), "[" + element + "/H]",
                        "literature_half_range_or_representative");
                    std::set<std::string> references;
                    const json medians = entry.value("median", json::array());
                    if (medians.is_array())
                    {
                        for (const auto& median : medians)
                        {
                            const std::string reference = text(median.value("catalog", json::object()), "id");
                            if (!reference.empty()) references.insert(reference);
                        }
                    }
                    record["elements"][element]["references"] = references;
                    const json values = entry.value("all_values", json::array());
                    record["elements"][element]["measurement_count"] = values.is_array() ? values.size() : 0;
                }
            }
            std::cout << "Hypatia chemistry: " << std::min(start + 48, targets.size()) << "/" << targets.size() << std::endl;
        }
        if (complete && !records.empty() && !abort_load)
        {
            json rows = json::array();
            for (const auto& record : records) rows.push_back(record.second);
            save_snapshot(hypatia_path,
            {
                {"schema", 1}, {"source", "Hypatia"}, {"solar_scale", "asplund09"},
                {"solar", {{"Fe", 7.50}, {"Mg", 7.60}, {"Si", 7.51}, {"C", 8.43}, {"O", 8.69}}},
                {"reference", "https://www.hypatiacatalog.com/api"}, {"rows", std::move(rows)}
            });
        }
    }

    const std::string galah_path = "catalogs/GALAH/stellar_abundances.json";
    if (!std::filesystem::exists(galah_path) && !abort_load && snapshot("catalogs/GaiaDR3/hipparcos_crossmatch.json",
        "https://gea.esac.esa.int/tap-server/tap/sync",
        "select source_id,original_ext_source_id from gaiadr3.hipparcos2_best_neighbour where number_of_neighbours=1", 200000, table))
    {
        std::map<uint64_t, int> hip_counts, gaia_counts;
        std::vector<std::pair<uint64_t, uint64_t>> pairs;
        for (const auto& row : table["rows"])
        {
            const auto gaia = row.find("source_id"), hip = row.find("original_ext_source_id");
            if (gaia == row.end() || hip == row.end() || !gaia->is_number_integer()) continue;
            const uint64_t hip_id = hip->is_string() ? identifier("HIP " + hip->get<std::string>(), "hip")
                : hip->is_number_integer() ? hip->get<uint64_t>() : 0;
            const uint64_t gaia_id = gaia->get<uint64_t>();
            if (!hip_id || !gaia_id) continue;
            pairs.push_back({gaia_id, hip_id});
            ++hip_counts[hip_id];
            ++gaia_counts[gaia_id];
        }
        std::map<uint64_t, uint64_t> identities;
        for (const auto& pair : pairs)
            if (hip_counts[pair.second] == 1 && gaia_counts[pair.first] == 1 && match({"HIP " + std::to_string(pair.second)}))
                identities[pair.first] = pair.second;
        std::vector<uint64_t> ids;
        for (const auto& pair : identities) ids.push_back(pair.first);
        std::string query = "select \"GaiaDR3\",\"flagSp\",\"flagSpFit\",\"flagRed\",\"SNRpxCCD3\",\"[Fe/H]\",\"e_[Fe/H]\",\"f_[Fe/H]\"";
        for (const auto& element : elements)
        {
            if (element == "Fe") continue;
            const std::string column = "[" + element + "/Fe]";
            query += ",\"" + column + "\",\"e_" + column + "\",\"f_" + column + "\"";
        }
        query += " from \"J/other/PASA/42.51/allstar\" where \"flagSp\"=0 and \"flagSpFit\"=0 and \"flagRed\"=0 and \"f_[Fe/H]\"=0 and \"SNRpxCCD3\">30 and \"GaiaDR3\" in (";
        json accepted = json::array();
        bool complete = true;
        for (size_t start = 0; complete && start < ids.size() && !abort_load; start += 1500)
        {
            std::string batch = query;
            for (size_t i = start; i < std::min(start + 1500, ids.size()); ++i)
                batch += (i == start ? "" : ",") + std::to_string(ids[i]);
            json rows;
            if (!tap(vizier, batch + ")", 10000, rows)) { complete = false; break; }
            for (const auto& row : rows)
            {
                if (number(row, "flagSp") != 0 || number(row, "flagSpFit") != 0 || number(row, "flagRed") != 0 ||
                    number(row, "f_[Fe/H]") != 0 || !(number(row, "SNRpxCCD3") > 30) || !std::isfinite(number(row, "[Fe/H]"))) continue;
                auto gaia = row.find("GaiaDR3");
                if (gaia == row.end() || !gaia->is_number_integer() || !identities.count(gaia->get<uint64_t>())) continue;
                const uint64_t id = gaia->get<uint64_t>();
                json composition;
                composition["Fe"] = measurement(number(row, "[Fe/H]"), number(row, "e_[Fe/H]"), "[Fe/H]", "reported_standard_error");
                for (const auto& element : elements)
                {
                    const std::string column = "[" + element + "/Fe]";
                    if (element == "Fe" || number(row, "f_" + column) != 0 || !std::isfinite(number(row, column))) continue;
                    composition[element] = measurement(number(row, column), number(row, "e_" + column), column, "reported_standard_error");
                }
                accepted.push_back({{"hip", identities[id]}, {"gaia", id}, {"elements", std::move(composition)}});
            }
            std::cout << "GALAH chemistry: " << std::min(start + 1500, ids.size()) << "/" << ids.size() << std::endl;
        }
        if (complete && !accepted.empty() && !abort_load)
            save_snapshot(galah_path,
            {
                {"schema", 1}, {"source", "GALAH DR4"}, {"solar_scale", "grevesse07"},
                {"solar", {{"Fe", 7.45}, {"Mg", 7.53}, {"Si", 7.51}, {"C", 8.39}, {"O", 8.66}}},
                {"reference", "https://www.galah-survey.org/dr4/"}, {"rows", std::move(accepted)}
            });
    }
    abundance_catalogs_loaded = false;
    abundance_catalogs.clear();
    abundance_lookup.clear();
    duplicate_abundance_hips.clear();
    duplicate_abundance_hds.clear();
    duplicate_abundance_gaia.clear();
    for (int i = 0; !abort_load && cels && i < ncelobjs; ++i)
        if (cels[i] && cels[i]->typeclass() == class_star) apply_stellar_abundances(static_cast<Star*>(cels[i]));
}
