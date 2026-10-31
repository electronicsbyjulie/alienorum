#include "serial.h"
#include "cat.h"
#include "stellar_row.h"
#include "../background_file.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <locale>
#include <tuple>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_set>

using namespace alienorum;
namespace fs = std::filesystem;

namespace
{
    using Repairs = std::map<fs::path, fs::path>;
    std::mutex repair_mutex;
    std::map<fs::path, Repairs> recovered_files;
    BackgroundFile region_writer;

    std::string digest(const std::string& bytes)
    {
        uint64_t value = 14695981039346656037ull;
        for (unsigned char byte : bytes) value = (value ^ byte) * 1099511628211ull;
        std::ostringstream result;
        result << std::hex << std::setw(16) << std::setfill('0') << value;
        return result.str();
    }

    std::string object_key(CelestialObject* object)
    {
        std::string key = object->name;
        std::unordered_set<CelestialObject*> visited{object};
        while (object->orbit && object->orbit->center)
        {
            object = object->orbit->center;
            if (!visited.insert(object).second) throw std::runtime_error("Cyclic stellar ancestry");
            key = std::string(object->name) + "." + key;
        }
        return key;
    }

    Point point(const json& value)
    {
        Point result;
        for (const char* axis : {"x", "y", "z"})
            if (!value.contains(axis) || !value.at(axis).is_number() || !std::isfinite(value.at(axis).get<double>()))
                throw std::runtime_error("Invalid catalog position");
        result.from_json(value);
        return result;
    }

    std::string original_name(const Star& star)
    {
        if (star.fictional_origin.is_object())
        {
            const std::string designation = star.fictional_origin.value("designation", std::string());
            if (!designation.empty()) return designation;
        }
        return star.origname.empty() ? star.name : star.origname;
    }

    std::string prefix(const Star& star)
    {
        if (star.catalog_frame.is_object() && star.catalog_frame.contains("prefix"))
            return star.catalog_frame.at("prefix").get<std::string>();
        const auto& origin = star.fictional_origin;
        if (origin.is_object() && origin.contains("batch") && origin["batch"].is_object())
        {
            const std::string value = origin["batch"].value("catalog_prefix", std::string());
            if (!value.empty()) return value;
        }
        const std::string designation = original_name(star);
        const auto dash = designation.find('-');
        return dash != std::string::npos ? designation.substr(0, dash) : "Added";
    }

    std::string population(const Star& star)
    {
        if (star.catalog_frame.is_object() && star.catalog_frame.contains("population"))
            return star.catalog_frame.at("population").get<std::string>();
        const auto& origin = star.fictional_origin;
        if (origin.is_object() && origin.contains("batch") && origin["batch"].is_object())
        {
            const std::string id = origin["batch"].value("id", std::string());
            if (!id.empty()) return id;
        }
        if (star.stellar_system.is_object())
        {
            const std::string system = star.stellar_system.value("system", std::string());
            const auto colon = system.rfind(':');
            if (colon != std::string::npos) return system.substr(0, colon);
        }
        return star.location.galactic_center.to_json().dump() + ":" + prefix(star);
    }

    json galaxy_frame(const Star& star, CelestialObject** objects)
    {
        const Point origin = star.location.galactic_center;
        json frame = {{"name", "MilkyWay"}, {"key", "MW"}, {"heliocentric_mly", origin.to_json()}};
        if (origin == Point(0, 0, 0)) return frame;
        double closest = std::numeric_limits<double>::infinity();
        Galaxy* match = nullptr;
        for (int i = 0; objects[i]; ++i)
        {
            if (objects[i]->deleted || objects[i]->typeclass() != class_galaxy) continue;
            Galaxy* galaxy = static_cast<Galaxy*>(objects[i]);
            Point position = galaxy->location.galactic_center
                + (galaxy->location.system_center + galaxy->location.local_position) * (1 / (1e6 * light_year));
            if (position == Point(0, 0, 0) && galaxy->distance > 0)
                position = Point::from_ra_dec(galaxy->right_ascension, galaxy->declination, galaxy->distance / (1e6 * light_year));
            const double separation = (position - origin).squared_magnitude();
            if (separation < closest)
            {
                closest = separation;
                match = galaxy;
            }
        }
        if (match && closest < 1e-12)
        {
            frame["name"] = match->name;
            frame["key"] = match->PGC ? "PGC" + std::to_string(match->PGC) : std::string(match->name);
        }
        else if (star.catalog_frame.is_object() && star.catalog_frame.contains("galaxy")
            && point(star.catalog_frame.at("galaxy").at("heliocentric_mly")) == origin)
        {
            frame = star.catalog_frame.at("galaxy");
        }
        else
        {
            frame["name"] = "Galaxy" + digest(origin.to_json().dump()).substr(0, 8);
            frame["key"] = frame["name"];
        }
        return frame;
    }

    struct StarRow
    {
        std::string key, parent_id;
        json record, origin;
        Point velocity;
        bool moving = false;
        int center_rank = 3;
        int system_order = std::numeric_limits<int>::max();
    };

    struct Region
    {
        std::string identity, prefix, basename;
        json galaxy, batch;
        std::vector<StarRow> stars;
        json objects = json::object();
    };

    struct SaveBatch
    {
        fs::path index;
        std::string generation;
        std::map<std::string, Region> regions;
    };

    SaveBatch capture(const std::string& path, CelestialObject** objects, bool only_edited)
    {
        SaveBatch batch;
        batch.index = fs::absolute(fs::u8path(path));
        static std::atomic<uint64_t> sequence{0};
        batch.generation = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(sequence.fetch_add(1));
        json records = Serialization::snapshot(objects, only_edited);
        std::unordered_map<CelestialObject*, std::string> membership;
        for (int i = 0; objects[i]; ++i)
        {
            CelestialObject* object = objects[i];
            if (object->deleted || object->typeclass() != class_star || !object->user_added) continue;
            Star& star = *static_cast<Star*>(object);
            const std::string key = object_key(object);
            if (!records.contains(key)) continue;
            const std::string identity = population(star);
            const std::string group = identity + "@" + star.location.galactic_center.to_json().dump();
            auto [entry, inserted] = batch.regions.try_emplace(group);
            Region& region = entry->second;
            if (inserted)
            {
                region.identity = identity;
                region.prefix = prefix(star);
                region.galaxy = galaxy_frame(star, objects);
                if (star.fictional_origin.is_object()) region.batch = star.fictional_origin.value("batch", json());
            }
            StarRow row;
            row.key = key;
            if (star.orbit && star.orbit->center && star.orbit->center->typeclass() == class_star)
            {
                const Star* parent = static_cast<Star*>(star.orbit->center);
                row.parent_id = parent->user_added ? "U" + digest(object_key(star.orbit->center)).substr(0, 13) : parent->alienorumid;
            }
            row.record = std::move(records[key]);
            row.record.erase("catalog_frame");
            records.erase(key);
            if (star.catalog_frame.is_object() && star.catalog_frame.value("anchor", false)) row.center_rank = 0;
            if (star.fictional_origin.is_object())
            {
                row.origin = star.fictional_origin;
                row.origin.erase("batch");
                row.origin.erase("motion");
                if (row.origin.value("anchor", false)) row.center_rank = 0;
                else if (row.origin.value("system_index", -1) == 0 && !star.orbit) row.center_rank = 1;
            }
            if (!star.orbit)
            {
                row.center_rank = std::min(row.center_rank, 2);
                if (star.stellar_system.is_object())
                {
                    const std::string system = star.stellar_system.value("system", std::string());
                    const auto colon = system.rfind(':');
                    if (colon != std::string::npos)
                    {
                        try
                        {
                            size_t consumed = 0;
                            const int order = std::stoi(system.substr(colon + 1), &consumed);
                            if (order >= 0 && consumed == system.size() - colon - 1) row.system_order = order;
                        }
                        catch (const std::exception&) { }
                    }
                }
            }
            row.moving = star.has_fictional_motion;
            row.velocity = star.fictional_space_velocity;
            row.record.erase("catalog_motion");
            if (row.moving) row.record["loc"]["sys_cen"] = star.fictional_epoch_position.to_json();
            else if (!(star.location.galactic_center == Point(0, 0, 0)))
            {
                const double ra = star.right_ascension, dec = star.declination;
                const Point radial(-sin(ra) * cos(dec), sin(dec), cos(ra) * cos(dec));
                const Point east(-cos(ra), 0, -sin(ra));
                const Point north(sin(ra) * sin(dec), cos(dec), -cos(ra) * sin(dec));
                row.velocity = radial * star.radial_velocity + east * (star.proper_motion_RA * star.distance * cos(dec))
                    + north * (star.proper_motion_decl * star.distance);
                row.moving = true;
                row.record["epch"] = J2000 + (simnow - J2000_TIME_T) / oneday;
            }
            region.stars.push_back(std::move(row));
            membership[object] = group;
        }
        std::unordered_map<std::string, std::string> world_regions;
        for (int i = 0; objects[i]; ++i)
        {
            CelestialObject* object = objects[i];
            if (object->deleted || object->typeclass() == class_star) continue;
            CelestialObject* ancestor = object;
            std::unordered_set<CelestialObject*> visited;
            while (ancestor && visited.insert(ancestor).second)
            {
                auto found = membership.find(ancestor);
                if (found != membership.end())
                {
                    world_regions[object_key(object)] = found->second;
                    break;
                }
                ancestor = ancestor->orbit ? ancestor->orbit->center : nullptr;
            }
        }
        for (auto& item : records.items())
        {
            const auto found = world_regions.find(item.key());
            const std::string group = found == world_regions.end() ? std::string() : found->second;
            Region& region = batch.regions[group];
            region.objects[item.key()] = std::move(item.value());
        }
        std::set<std::string> filenames;
        for (auto& [identity, region] : batch.regions)
        {
            if (region.stars.empty())
            {
                region.basename = "MilkyWay.Edited." + Serialization::filename_part(objects[0] ? objects[0]->name : "Sun");
            }
            else
            {
                std::sort(region.stars.begin(), region.stars.end(), [](const StarRow& a, const StarRow& b)
                {
                    return std::tie(a.center_rank, a.system_order, a.key) < std::tie(b.center_rank, b.system_order, b.key);
                });
                region.basename = Serialization::filename_part(region.galaxy.at("name").get<std::string>()) + "."
                    + Serialization::filename_part(region.prefix) + "."
                    + Serialization::filename_part(region.stars.front().record.at("!name").get<std::string>());
            }
            std::string folded = region.basename;
            std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!filenames.insert(folded).second) throw std::runtime_error("Two regions have the same output filename");
        }
        return batch;
    }

    void local_coordinates(json& record, Point position)
    {
        const double radius = std::hypot(position.x, position.y, position.z);
        double ra = radius > 0 ? atan2(-position.x, position.z) * fiftyseven : 0;
        if (ra < 0) ra += 360;
        record["RA"] = ra;
        record["decl"] = radius > 0 ? atan2(position.y, std::hypot(position.x, position.z)) * fiftyseven : 0;
        record["dist"] = radius / light_year;
        record["loc"]["sys_cen"] = position.to_json();
    }

    json compact_star_state(const StarRow& row, const std::string& line, const json& header)
    {
        const bool relative = header.at("relative");
        json record = Serialization::output_record(Serialization::expand_output_record(row.record));
        record.erase("catalog_frame");
        json extra = stellar_row::compact(record, line, relative);
        json restored = stellar_row::restore(line, extra);
        const bool anchor_row = stellar_row::text(line, 1, 14) == header.at("center").at("id");
        const Point anchor = point(header.at("center").at("galactocentric_m"));
        Point approximate = stellar_row::sky_position(restored);
        if (relative) approximate = anchor_row ? anchor : approximate + anchor;
        const Point exact = point(record.at("loc").at("sys_cen"));
        if (relative || row.moving)
        {
            const json correction = stellar_row::vector_difference(approximate, exact);
            if (!correction.empty()) extra["position_residual_m"] = correction;
        }
        if (row.moving)
        {
            const Point galaxy = point(header.at("galaxy").at("heliocentric_mly"));
            const Point velocity = stellar_row::velocity(restored, exact + galaxy * (1e6 * light_year));
            const json correction = stellar_row::vector_difference(velocity, row.velocity);
            if (!correction.empty()) extra["velocity_residual_m_s"] = correction;
            if (record.value("fic", 0) != 1) extra["linear_motion"] = true;
        }
        if (row.origin.is_object() && row.origin.contains("id") && row.origin["id"].is_string()
            && !row.origin["id"].get_ref<const std::string&>().empty())
        {
            const std::string designation = record.value("!origname", record.at("!name").get<std::string>());
            const char component = record.contains("orb") && !designation.empty() && designation.back() >= 'A' && designation.back() <= 'Z'
                ? designation.back() : 'A';
            const std::string id = record.contains("stsys") && record["stsys"].contains("sys")
                ? record["stsys"]["sys"].get<std::string>() + ":" + component : "catalog:" + designation;
            if (row.origin.at("id") != id) extra["origin_id"] = row.origin.at("id");
        }
        return extra;
    }

    std::string catalog_bytes(const SaveBatch& batch, const Region& region)
    {
        const StarRow& anchor = region.stars.front();
        const Point anchor_position = point(anchor.record.at("loc").at("sys_cen"));
        const bool relative = !(point(region.galaxy.at("heliocentric_mly")) == Point(0, 0, 0));
        json header = {{"version", 2}, {"generation", batch.generation}, {"galaxy", region.galaxy},
            {"population", region.identity}, {"prefix", region.prefix},
            {"center", {{"id", "U" + digest(anchor.key).substr(0, 13)}, {"name", anchor.record.at("!name")},
                {"galactocentric_m", anchor_position.to_json()}}},
            {"relative", relative}, {"axes", "ICRF"}, {"distance_unit", "ly"}, {"position_unit", "m"}, {"count", region.stars.size()}};
        if (region.batch.is_object()) header["batch"] = region.batch;
        std::set<std::string> ids;
        for (const StarRow& row : region.stars) ids.insert("U" + digest(row.key).substr(0, 13));
        for (const StarRow& row : region.stars)
        {
            if (!row.parent_id.empty() && !ids.count(row.parent_id))
                header["external_parents"][row.parent_id] = row.record.at("orb").at("cen_name");
        }
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << "# USER_STARS1 " << std::quoted(header.dump()) << '\n';
        for (const StarRow& row : region.stars)
        {
            json record = row.record;
            const Point absolute = point(record.at("loc").at("sys_cen"));
            const Point offset = relative && row.key != anchor.key ? absolute - anchor_position : absolute;
            if (relative) local_coordinates(record, offset);
            record["loc"].erase("gal_cen");
            struct CatalogColumns : Star
            {
                ~CatalogColumns() { HD = HIP = 0; }
            } formatted;
            formatted.seqno = -1;
            json columns = Serialization::expand_output_record(record);
            columns.erase("catalog_aliases");
            if (!formatted.from_json(columns)) throw std::runtime_error("Invalid added star");
            formatted.right_ascension = record.at("RA").get<double>() * fiftyseventh;
            formatted.declination = record.at("decl").get<double>() * fiftyseventh;
            formatted.distance = record.at("dist").get<double>() * light_year;
            formatted.alienorumid = "U" + digest(row.key).substr(0, 13);
            Star parent_columns;
            parent_columns.seqno = -1;
            parent_columns.alienorumid = row.parent_id;
            if (formatted.orbit && !row.parent_id.empty()) formatted.orbit->center = &parent_columns;
            const std::string line = CatalogReader::format_condensed_star_line(&formatted, false);
            output << line << " FIC2 " << std::quoted(compact_star_state(row, line, header).dump()) << '\n';
        }
        return output.str();
    }

    struct StagedFile
    {
        fs::path destination, temporary, backup;
        bool existed = false, committed = false;
    };

    void replace_file(const fs::path& temporary, const fs::path& destination)
    {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish universe output");
#else
        fs::rename(temporary, destination);
#endif
    }

    bool publish_batch(SaveBatch batch)
    {
        std::vector<StagedFile> files;
        std::set<fs::path> destinations;
        const auto stage = [&](const fs::path& destination, const std::function<void(std::ostream&)>& write)
        {
            if (!destinations.insert(destination).second) throw std::runtime_error("Universe index collides with a region filename");
            StagedFile file;
            file.destination = destination;
            file.temporary = destination.string() + "." + batch.generation + ".tmp";
            file.backup = destination.string() + ".bak";
            files.push_back(file);
            std::ofstream output(file.temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot stage universe output");
            write(output);
            output.flush();
            output.close();
            if (!output) throw std::runtime_error("Cannot finish universe output");
        };
        try
        {
            // Restore the last committed files before rotating their backups again.
            {
                std::lock_guard<std::mutex> lock(repair_mutex);
                for (const auto& [destination, source] : recovered_files[batch.index.parent_path()])
                {
                    if (!publish_file(destination, [&](std::ostream& output)
                    {
                        std::ifstream input(source, std::ios::binary);
                        if (!input) return false;
                        output << input.rdbuf();
                        return !input.bad() && output.good();
                    })) throw std::runtime_error("Cannot restore a recovered universe companion");
                }
                recovered_files.erase(batch.index.parent_path());
            }
            const fs::path directory = batch.index.parent_path();
            json index = {{"$universe", {{"version", 1}, {"generation", batch.generation}, {"parts", json::array()}}}};
            for (auto& [identity, region] : batch.regions)
            {
                region.objects["$region"] = {{"version", 1}, {"generation", batch.generation}};
                if (!region.stars.empty())
                {
                    const std::string bytes = catalog_bytes(batch, region);
                    const std::string name = region.basename + ".dat";
                    stage(directory / name, [&](std::ostream& output) { output << bytes; });
                    region.objects["$stellar_catalog"] = {{"version", 1}, {"generation", batch.generation},
                        {"file", name}, {"hash", digest(bytes)}, {"count", region.stars.size()}};
                }
                const std::string name = region.basename + ".json";
                stage(directory / name, [&](std::ostream& output) { output << std::setw(4) << region.objects; });
                index["$universe"]["parts"].push_back(name);
            }
            stage(batch.index, [&](std::ostream& output) { output << std::setw(4) << index; });
            // Stage the entire batch before replacing any file; publish the index last.
            for (auto& file : files)
            {
                file.existed = fs::exists(file.destination);
                if (file.existed) fs::copy_file(file.destination, file.backup, fs::copy_options::overwrite_existing);
            }
            for (auto& file : files)
            {
                replace_file(file.temporary, file.destination);
                file.committed = true;
            }
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Universe save failed: " << error.what() << std::endl;
            for (auto it = files.rbegin(); it != files.rend(); ++it)
            {
                std::error_code ignored;
                if (it->committed)
                {
                    if (it->existed) fs::copy_file(it->backup, it->destination, fs::copy_options::overwrite_existing, ignored);
                    else fs::remove(it->destination, ignored);
                }
                fs::remove(it->temporary, ignored);
            }
            return false;
        }
    }

    std::string read_bytes(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open universe companion: " + path.string());
        std::ostringstream output;
        output << input.rdbuf();
        if (input.bad()) throw std::runtime_error("Cannot read universe companion");
        return output.str();
    }

    fs::path companion(const fs::path& directory, const std::string& filename)
    {
        const fs::path name = fs::u8path(filename);
        if (name.empty() || name.filename() != name || filename == "." || filename == "..")
            throw std::runtime_error("Invalid universe companion filename");
        return directory / name;
    }

    void merge_records(json& target, json source)
    {
        if (!source.is_object()) throw std::runtime_error("Invalid universe records");
        for (auto& item : source.items())
        {
            if (target.contains(item.key())) throw std::runtime_error("Duplicate universe record: " + item.key());
            target[item.key()] = std::move(item.value());
        }
    }

    json read_catalog(const std::string& bytes, const json& descriptor)
    {
        if (digest(bytes) != descriptor.at("hash")) throw std::runtime_error("Stellar catalog checksum mismatch");
        std::istringstream input(bytes);
        std::string line, comment, format, payload;
        if (!std::getline(input, line)) throw std::runtime_error("Empty stellar catalog");
        std::istringstream first(line);
        first >> comment >> format >> std::quoted(payload);
        if (!first || comment != "#" || format != "USER_STARS1") throw std::runtime_error("Unknown stellar catalog format");
        const json header = json::parse(payload);
        const int version = header.at("version");
        if ((version != 1 && version != 2) || header.at("generation") != descriptor.at("generation"))
            throw std::runtime_error("Stellar catalog generation mismatch");
        const Point galaxy = point(header.at("galaxy").at("heliocentric_mly"));
        const Point anchor = point(header.at("center").at("galactocentric_m"));
        json records = json::object();
        std::map<std::string, std::string> names, parents;
        std::set<std::string> original_parents;
        const std::string anchor_key = version == 1 ? header.at("center").at("key").get<std::string>()
            : "@" + header.at("center").at("id").get<std::string>();
        while (std::getline(input, line))
        {
            if (line.size() < 655 || line.size() > 1024 * 1024 || line.substr(468, 3) != "SC2")
                throw std::runtime_error("Invalid stellar catalog row");
            std::istringstream extras(line.substr(655));
            extras >> format >> std::quoted(payload);
            if (!extras || format != (version == 1 ? "FIC1" : "FIC2")) throw std::runtime_error("Missing added-star state");
            const json extra = json::parse(payload);
            const std::string id = stellar_row::text(line, 1, 14);
            const std::string key = version == 1 ? extra.at("key").get<std::string>() : "@" + id;
            json record = version == 1 ? extra.at("record") : stellar_row::restore(line, extra);
            if (record.at("cls") != class_star || !record.value("user_added", false) || records.contains(key))
                throw std::runtime_error("Invalid added-star identity");
            Point position = version == 1 ? point(record.at("loc").at("sys_cen")) : stellar_row::sky_position(record);
            if (header.at("relative").get<bool>())
            {
                if (version == 2 && key == anchor_key) position = anchor;
                else if (key != anchor_key) position += anchor;
            }
            if (extra.contains("position_residual_m"))
            {
                if (version == 1) position += point(extra.at("position_residual_m"));
                else position = stellar_row::adjusted_vector(position, extra.at("position_residual_m"));
            }
            if (header.at("relative").get<bool>()) local_coordinates(record, position + galaxy * (1e6 * light_year));
            record["loc"]["sys_cen"] = position.to_json();
            record["loc"]["gal_cen"] = galaxy.to_json();
            record["catalog_frame"] = {{"population", header.at("population")}, {"prefix", header.at("prefix")},
                {"galaxy", header.at("galaxy")}, {"anchor", key == anchor_key}};
            if (version == 1 && extra.contains("origin")) record["fictional_origin"] = extra.at("origin");
            if (version == 2 && record.value("fic", 0) == 1) record["fictional_origin"] = json::object();
            if (extra.contains("origin_id")) record["fictional_origin"]["id"] = extra.at("origin_id");
            if (record.contains("fictional_origin") && header.contains("batch")) record["fictional_origin"]["batch"] = header.at("batch");
            const bool moving = version == 1 ? extra.contains("velocity_m_s")
                : record.value("fic", 0) == 1 || extra.value("linear_motion", false);
            if (moving)
            {
                Point velocity = version == 1 ? point(extra.at("velocity_m_s"))
                    : stellar_row::velocity(record, position + galaxy * (1e6 * light_year));
                if (version == 2 && extra.contains("velocity_residual_m_s"))
                    velocity = stellar_row::adjusted_vector(velocity, extra.at("velocity_residual_m_s"));
                const json motion = {{"model", "galactic_velocity_ellipsoid_v1"},
                    {"reference", "stationary_galaxy_center"}, {"propagation", "linear_cartesian"},
                    {"epoch_position_m", position.to_json()}, {"velocity_m_s", velocity.to_json()}};
                if (record.contains("fictional_origin")) record["fictional_origin"]["motion"] = motion;
                else record["catalog_motion"] = motion;
            }
            if (version == 2)
            {
                if (id.empty() || !names.emplace(id, record.at("!name")).second) throw std::runtime_error("Duplicate stellar column identifier");
                const std::string parent = stellar_row::text(line, 287, 300);
                if (!parent.empty()) parents[key] = parent;
                if (extra.value("original_parent", false)) original_parents.insert(key);
            }
            records[key] = std::move(record);
        }
        for (const auto& [key, parent] : parents)
        {
            const auto name = names.find(parent);
            if (name != names.end()) records[key]["orb"]["cen_name"] = name->second;
            else if (header.contains("external_parents") && header["external_parents"].contains(parent))
                records[key]["orb"]["cen_name"] = header["external_parents"][parent];
            else throw std::runtime_error("Unknown stellar column parent: " + parent);
        }
        for (const std::string& key : original_parents) records[key]["!origcenname"] = records[key].at("orb").at("cen_name");
        if (!records.contains(anchor_key)) throw std::runtime_error("Stellar catalog has no center star");
        if (records.size() != descriptor.at("count").get<size_t>() || records.size() != header.at("count").get<size_t>())
            throw std::runtime_error("Stellar catalog count mismatch");
        return records;
    }

    json read_part(const fs::path& path, const std::string& generation, Repairs& repairs)
    {
        std::string failure;
        for (const std::string suffix : {std::string(), std::string(".bak")})
        {
            try
            {
                Repairs selected;
                json document = json::parse(read_bytes(path.string() + suffix));
                if (!generation.empty() && document.at("$region").at("generation") != generation)
                    throw std::runtime_error("Universe region generation mismatch");
                const std::string region_generation = document.contains("$region")
                    ? document.at("$region").at("generation").get<std::string>() : std::string();
                if (document.contains("$region"))
                {
                    if (document.at("$region").at("version") != 1) throw std::runtime_error("Unknown region format");
                    document.erase("$region");
                }
                if (document.contains("$stellar_catalog"))
                {
                    const json descriptor = document.at("$stellar_catalog");
                    if (descriptor.at("version") != 1) throw std::runtime_error("Unknown catalog descriptor");
                    if (descriptor.at("generation") != region_generation) throw std::runtime_error("Region and catalog generations differ");
                    const fs::path catalog = companion(path.parent_path(), descriptor.at("file"));
                    json stars;
                    bool found = false;
                    for (const std::string catalog_suffix : {std::string(), std::string(".bak")})
                    {
                        try
                        {
                            stars = read_catalog(read_bytes(catalog.string() + catalog_suffix), descriptor);
                            if (!catalog_suffix.empty()) selected[catalog] = catalog.string() + catalog_suffix;
                            found = true;
                            break;
                        }
                        catch (const std::exception&) { }
                    }
                    if (!found) throw std::runtime_error("No matching stellar catalog: " + catalog.string());
                    document.erase("$stellar_catalog");
                    merge_records(document, std::move(stars));
                }
                if (!suffix.empty())
                {
                    std::cerr << "Recovered universe region from backup: " << path.string() << std::endl;
                    selected[path] = path.string() + suffix;
                }
                for (const auto& [destination, source] : selected) repairs[destination] = source;
                return document;
            }
            catch (const std::exception& error) { if (failure.empty()) failure = error.what(); }
        }
        throw std::runtime_error(failure);
    }
}

std::string Serialization::filename_part(const std::string& text)
{
    // Latin letters with accents, including precomposed and combining spellings.
    static const std::vector<std::pair<std::u32string, std::string>> latin =
    {
        {U"ÀÁÂÃÄÅĀĂĄǍ", "A"}, {U"àáâãäåāăąǎ", "a"}, {U"ÇĆĈĊČ", "C"}, {U"çćĉċč", "c"},
        {U"ÐĎĐ", "D"}, {U"ðďđ", "d"}, {U"ÈÉÊËĒĔĖĘĚ", "E"}, {U"èéêëēĕėęě", "e"},
        {U"ĜĞĠĢ", "G"}, {U"ĝğġģ", "g"}, {U"ĤĦ", "H"}, {U"ĥħ", "h"},
        {U"ÌÍÎÏĨĪĬĮİǏ", "I"}, {U"ìíîïĩīĭįıǐ", "i"}, {U"Ĵ", "J"}, {U"ĵ", "j"},
        {U"Ķ", "K"}, {U"ķ", "k"}, {U"ĹĻĽĿŁ", "L"}, {U"ĺļľŀł", "l"},
        {U"ÑŃŅŇŊ", "N"}, {U"ñńņňŋ", "n"}, {U"ÒÓÔÕÖØŌŎŐǑ", "O"}, {U"òóôõöøōŏőǒ", "o"},
        {U"ŔŖŘ", "R"}, {U"ŕŗř", "r"}, {U"ŚŜŞŠȘ", "S"}, {U"śŝşšș", "s"},
        {U"ŢŤŦȚ", "T"}, {U"ţťŧț", "t"}, {U"ÙÚÛÜŨŪŬŮŰŲǓ", "U"}, {U"ùúûüũūŭůűųǔ", "u"},
        {U"Ŵ", "W"}, {U"ŵ", "w"}, {U"ÝŶŸ", "Y"}, {U"ýÿŷ", "y"}, {U"ŹŻŽ", "Z"}, {U"źżž", "z"},
        {U"ÆǼ", "AE"}, {U"æǽ", "ae"}, {U"Œ", "OE"}, {U"œ", "oe"}, {U"ß", "ss"}, {U"Þ", "Th"}, {U"þ", "th"}
    };
    std::string result;
    for (size_t i = 0; i < text.size();)
    {
        const unsigned char first = text[i++];
        char32_t code = first;
        int continuation = 0;
        if (first >= 0xc0 && first < 0xe0) { code = first & 0x1f; continuation = 1; }
        else if (first >= 0xe0 && first < 0xf0) { code = first & 0x0f; continuation = 2; }
        else if (first >= 0xf0 && first < 0xf8) { code = first & 7; continuation = 3; }
        else if (first >= 0x80) throw std::runtime_error("Invalid UTF-8 filename");
        while (continuation--)
        {
            if (i == text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
                throw std::runtime_error("Invalid UTF-8 filename");
            code = (code << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
        }
        if ((code >= U'A' && code <= U'Z') || (code >= U'a' && code <= U'z') || (code >= U'0' && code <= U'9')
            || code == U'-' || code == U'_') result += static_cast<char>(code);
        else if (code >= 0x300 && code <= 0x36f) continue;
        else if (code >= 0x80)
        {
            bool mapped = false;
            for (const auto& [letters, replacement] : latin)
            {
                if (letters.find(code) == std::u32string::npos) continue;
                result += replacement;
                mapped = true;
                break;
            }
            if (!mapped)
            {
                std::ostringstream escaped;
                escaped << 'U' << std::hex << static_cast<uint32_t>(code);
                result += escaped.str();
            }
        }
    }
    return result.empty() ? "Unnamed" : result;
}

bool Serialization::save_region_files(const std::string& path, CelestialObject** objects, bool only_edited, bool background)
{
    try
    {
        if (!objects || !objects[0]) throw std::runtime_error("No universe to save");
        if (!background)
        {
            region_writer.finish();
            return publish_batch(capture(path, objects, only_edited));
        }
        return region_writer.submit([&]()
        {
            SaveBatch batch = capture(path, objects, only_edited);
            return [batch = std::move(batch)]() mutable { return publish_batch(std::move(batch)); };
        });
    }
    catch (const std::exception& error)
    {
        std::cerr << "Universe capture failed: " << error.what() << std::endl;
        return false;
    }
}

bool Serialization::finish_region_saves()
{
    return region_writer.finish();
}

bool Serialization::load_file(const std::string& filename, CelestialObject** objects, unsigned int max, bool as_user_added)
{
    finish_file_saves();
    fs::path path = fs::absolute(fs::u8path(filename));
    if (path.extension() == ".dat") path.replace_extension(".json");
    json records;
    Repairs chosen_repairs;
    std::string failure;
    bool complete = false;
    for (const std::string suffix : {std::string(), std::string(".bak")})
    {
        try
        {
            Repairs selected;
            const json document = json::parse(read_bytes(path.string() + suffix));
            if (document.contains("$universe"))
            {
                const auto& manifest = document.at("$universe");
                if (manifest.at("version") != 1) throw std::runtime_error("Unknown universe index format");
                const std::string generation = manifest.at("generation");
                records = json::object();
                for (const auto& part : manifest.at("parts"))
                    merge_records(records, read_part(companion(path.parent_path(), part.get<std::string>()), generation, selected));
            }
            else if (document.contains("$region") || document.contains("$stellar_catalog")) records = read_part(path, std::string(), selected);
            else records = document;
            if (!suffix.empty())
            {
                std::cerr << "Recovered universe index from backup: " << path.string() << std::endl;
                selected[path] = path.string() + suffix;
            }
            chosen_repairs = std::move(selected);
            complete = true;
            break;
        }
        catch (const std::exception& error) { if (failure.empty()) failure = error.what(); }
    }
    if (!complete)
    {
        std::cerr << "Universe import failed: " << failure << std::endl;
        return false;
    }
    if (!load_document(std::move(records), objects, max, as_user_added)) return false;
    std::lock_guard<std::mutex> lock(repair_mutex);
    if (path.extension() != ".bak")
        for (const auto& [destination, source] : chosen_repairs) recovered_files[path.parent_path()][destination] = source;
    return true;
}
