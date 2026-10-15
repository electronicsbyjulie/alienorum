#include "stellar_sources.h"
#include "cons.h"
#include "../globals.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
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

    std::string encoded(const std::string &value)
    {
        std::ostringstream result;
        for (unsigned char c : value)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                result << c;
            }
            else
            {
                result << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(c);
            }
        }
        return result.str();
    }

    std::string tap_url(const std::string &endpoint, const std::string &query)
    {
        return endpoint + "?REQUEST=doQuery&LANG=ADQL&FORMAT=tsv&MAXREC=2000000&QUERY=" + encoded(query);
    }

    std::string cell(const Row &row, const std::string &column)
    {
        auto found = row.find(column);
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
    return key;
}

std::vector<StellarReferenceSource> alienorum::stellar_reference_sources()
{
    const std::string vizier = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync";
    const std::string simbad = "https://simbad.cds.unistra.fr/simbad/sim-tap/sync";
    const std::string sbx = "https://astro.ulb.ac.be/sbx/tap/sync";
    std::vector<StellarReferenceSource> sources;
    auto add = [&](const std::string &name, const std::string &endpoint, const std::string &query,
        const std::vector<std::string> &columns)
    {
        StellarReferenceSource source;
        source.name = name;
        source.url = tap_url(endpoint, query);
        source.format = "tsv";
        source.columns = columns;
        sources.push_back(source);
    };
    add("SIMBAD", simbad,
        "SELECT b.oid,b.main_id,b.otype,b.ra,b.dec,b.sp_type,b.sp_qual,b.sp_bibcode,"
        "b.plx_value,b.plx_err,b.plx_bibcode,b.pmra,b.pmdec,b.rvz_radvel,b.rvz_bibcode,"
        "i.ids,f.V,f.B,f.U FROM basic b JOIN ids i ON i.oidref=b.oid "
        "LEFT JOIN allfluxes f ON f.oidref=b.oid WHERE f.V<11 OR b.plx_value>10",
        std::vector<std::string>
        {
            "oid", "main_id", "ids", "ra", "dec", "sp_type"
        });
    add("Mermilliod", vizier, "SELECT * FROM \"II/168/ubvmeans\"",
        std::vector<std::string>
        {
            "SimbadName", "m_LID", "Vmag", "B-V", "U-B"
        });
    add("Skiff", vizier, "SELECT * FROM \"B/mk/mktypes\" WHERE Mag<12 OR Mag IS NULL",
        std::vector<std::string>
        {
            "Name", "SpType", "Bibcode", "RAJ2000", "DEJ2000"
        });
    add("WDS", vizier, "SELECT * FROM \"B/wds/wds\"",
        std::vector<std::string>
        {
            "WDS", "Comp", "mag1", "mag2", "sep2", "pa2", "RAJ2000", "DEJ2000"
        });
    add("SBX_systems", sbx, "SELECT * FROM systems",
        std::vector<std::string>
        {
            "sn", "ra", "dec"
        });
    add("SBX_alias", sbx, "SELECT * FROM alias",
        std::vector<std::string>
        {
            "sn", "catalog", "identifier"
        });
    add("SBX_orbits", sbx, "SELECT * FROM orbits",
        std::vector<std::string>
        {
            "sn", "period", "eccentricity", "bibcode"
        });
    add("Gaia_DR3", "https://gea.esac.esa.int/tap-server/tap/sync",
        "SELECT g.source_id,n.original_ext_source_id AS HIP,n.number_of_neighbours,"
        "g.ra,g.dec,g.ref_epoch,g.parallax,g.parallax_error,g.pmra,g.pmdec,g.radial_velocity,g.ruwe "
        "FROM gaiadr3.gaia_source g JOIN gaiadr3.hipparcos2_best_neighbour n ON g.source_id=n.source_id",
        std::vector<std::string>
        {
            "source_id", "hip", "parallax", "parallax_error", "ruwe"
        });
    StellarReferenceSource debcat;
    debcat.name = "DEBCat";
    debcat.url = "https://astro.keele.ac.uk/jkt/debcat/debs.dat";
    debcat.format = "debcat";
    sources.push_back(debcat);
    StellarReferenceSource orb6;
    orb6.name = "ORB6";
    orb6.url = "https://crf.usno.navy.mil/data_products/WDS/orb6/orb6orbits.txt";
    orb6.format = "orb6";
    sources.push_back(orb6);
    StellarReferenceSource iau;
    iau.name = "IAU";
    iau.url = "https://exopla.net/star-names/modern-iau-star-names/";
    iau.format = "iau";
    sources.push_back(iau);
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
    if (!objects)
    {
        return report;
    }
    std::filesystem::create_directories(cache_directory);
    for (const auto &source : stellar_reference_sources())
    {
        const auto path = std::filesystem::path(cache_directory) / (source.name + "." + source.format);
        json status;
        status["name"] = source.name;
        status["url"] = source.url;
        if (!std::filesystem::exists(path) && allow_download)
        {
            std::cout << "Fetching stellar reference " << source.name << std::endl;
            fetch(source.url, path);
        }
        status["available"] = std::filesystem::exists(path);
        report["sources"].push_back(status);
    }
    return report;
}
