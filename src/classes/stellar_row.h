#ifndef ALIENORUM_STELLAR_ROW_H
#define ALIENORUM_STELLAR_ROW_H

#include "serial.h"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <tuple>
#include <cstdint>

namespace alienorum::stellar_row
{
    inline std::string text(const std::string& line, size_t first, size_t last)
    {
        if (line.size() < last) throw std::runtime_error("Truncated stellar columns");
        return trim(line.substr(first - 1, last - first + 1));
    }

    inline json columns(const std::string& line)
    {
        json record = {{"cls", class_star}, {"typ", star}, {"user_added", true}};
        struct Number
        {
            const char* path;
            size_t first, last;
            double scale;
            bool integer = false;
        };
        const Number numbers[] =
        {
            {"/appmagn", 77, 82, 1}, {"/BV_color", 100, 105, 1}, {"/UB_color", 107, 112, 1},
            {"/HD", 114, 119, 1, true}, {"/HIP", 121, 126, 1, true}, {"/HR", 128, 133, 1, true},
            {"/SB9", 135, 140, 1, true}, {"/FlamsteedNo", 142, 144, 1, true}, {"/GouldNo", 170, 172, 1, true},
            {"/pmRA", 178, 188, fiftyseven * oneyear}, {"/pmdec", 190, 200, fiftyseven * oneyear},
            {"/radvel", 202, 212, 1}, {"/plx", 214, 224, fiftyseven * 3600000}, {"/dist", 226, 236, 1},
            {"/absmagn", 238, 243, 1}, {"/mass", 245, 255, solar_mass}, {"/vol_mean_rad", 257, 267, solar_radius},
            {"/temp", 269, 274, 1}, {"/sider_rot_per", 276, 285, 1},
            {"/gaia_source_id", 514, 533, 1, true}, {"/gaia_g_magnitude", 535, 552, 1},
            {"/wd_color_rev", 636, 655, 1, true}
        };
        const auto read_number = [&](const Number& field)
        {
            const std::string value = text(line, field.first, field.last);
            const json::json_pointer path(field.path);
            if (value.empty()) { record[path] = field.integer ? json(uint64_t(0)) : json(0); return; }
            if (field.integer)
            {
                size_t used = 0;
                const uint64_t number = std::stoull(value, &used);
                if (used != value.size()) throw std::runtime_error("Invalid stellar integer");
                record[path] = number;
                return;
            }
            std::istringstream input(value);
            input.imbue(std::locale::classic());
            double number = 0;
            input >> number;
            if (!input || !std::isfinite(number)) return;
            input >> std::ws;
            if (!input.eof()) throw std::runtime_error("Invalid stellar number");
            record[path] = number * field.scale;
        };
        for (const auto& field : numbers) read_number(field);
        struct String { const char* key; size_t first, last; };
        for (const auto& field : {String{"!name", 16, 54}, String{"sptyp", 84, 98}, String{"Bayer", 146, 152},
            String{"Gliese", 154, 168}, String{"Gouldcons", 174, 176}, String{"Bonn_survey", 439, 440},
            String{"WD", 456, 467}, String{"gaia_wd_name", 473, 512}, String{"wd_color_src", 570, 601},
            String{"wd_temp_src", 603, 634}})
            record[field.key] = text(line, field.first, field.last);
        const auto numeric = [&](size_t first, size_t last)
        {
            const std::string value = text(line, first, last);
            return value.empty() ? 0.0 : std::stod(value);
        };
        record["RA"] = 15 * (numeric(56, 57) + numeric(59, 60) / 60 + numeric(62, 65) / 3600);
        record["decl"] = (text(line, 67, 67) == "-" ? -1 : 1)
            * (numeric(68, 69) + numeric(71, 72) / 60 + numeric(74, 75) / 3600);
        record["appmagn_est"] = text(line, 554, 554) == "1";
        record["wd_color_valid"] = text(line, 556, 556) == "1";
        record["wd_color_rgb"] = json::array({int(numeric(558, 560)), int(numeric(562, 564)), int(numeric(566, 568))});
        if (!text(line, 439, 440).empty())
        {
            const int declination = int(numeric(442, 445));
            record["Bonn_survey_sign"] = declination < 0 ? "-" : "+";
            record["Bonn_survey_declination"] = declination;
            record["Bonn_survey_sequential"] = int(numeric(447, 452));
        }
        if (!text(line, 287, 390).empty())
        {
            for (const auto& field : {Number{"/orb/per", 302, 313, 1}, Number{"/orb/semimaj_axis", 315, 326, AU},
                Number{"/orb/ecc", 328, 339, 1}, Number{"/orb/arg_peri", 341, 348, 1},
                Number{"/orb/mean_anom", 350, 357, 1}, Number{"/orb/epch", 359, 372, 1},
                Number{"/orb/heliocen_incl", 374, 381, 1}, Number{"/orb/heliocen_node", 383, 390, 1}})
                read_number(field);
            if (text(line, 359, 372).empty()) record["orb"]["epch"] = J2000;
        }
        return record;
    }

    inline const json& defaults(bool orbit)
    {
        static const json basic = []()
        {
            Star object;
            object.seqno = -1;
            return Serialization::output_record(object.to_json());
        }();
        static const json orbiting = []()
        {
            Star object;
            object.seqno = -1;
            object.orbit = new Orbit();
            return Serialization::output_record(object.to_json());
        }();
        return orbit ? orbiting : basic;
    }

    inline void merge(json& target, const json& source)
    {
        for (const auto& item : source.items())
        {
            if (item.value().is_object() && target.contains(item.key()) && target[item.key()].is_object())
                merge(target[item.key()], item.value());
            else target[item.key()] = item.value();
        }
    }

    inline void prune_defaults(json& record, const json& baseline)
    {
        for (auto field = record.begin(); field != record.end();)
        {
            const auto value = baseline.find(field.key());
            if (value != baseline.end() && *field == *value) { field = record.erase(field); continue; }
            if (field->is_object() && value != baseline.end() && value->is_object())
            {
                prune_defaults(*field, *value);
                if (field->empty()) { field = record.erase(field); continue; }
            }
            ++field;
        }
    }

    inline json difference(double base, double target)
    {
        const double first = target - base;
        if (first == 0) return nullptr;
        const double second = target - (base + first);
        return second == 0 ? json(first) : json::array({first, second});
    }

    inline double adjusted(double base, const json& correction)
    {
        if (correction.is_array())
        {
            if (correction.size() != 2) throw std::runtime_error("Invalid precision correction");
            for (const auto& part : correction) base = adjusted(base, part);
            return base;
        }
        if (!correction.is_number() || !std::isfinite(correction.get<double>()))
            throw std::runtime_error("Invalid precision correction");
        return base + correction.get<double>();
    }

    inline void remove_columns(json& record, const json& core, json& numeric, json& suffixes, const std::string& path = "")
    {
        for (auto field = record.begin(); field != record.end();)
        {
            const auto base = core.find(field.key());
            if (base == core.end()) { ++field; continue; }
            const std::string pointer = path + "/" + field.key();
            if (field->is_object() && base->is_object())
            {
                remove_columns(*field, *base, numeric, suffixes, pointer);
                if (field->empty()) { field = record.erase(field); continue; }
            }
            else if (*field == *base) { field = record.erase(field); continue; }
            else if (field->is_number() && base->is_number())
            {
                const json delta = difference(base->get<double>(), field->get<double>());
                if (!delta.is_null()) numeric[pointer] = delta;
                field = record.erase(field);
                continue;
            }
            else if (field->is_string() && base->is_string())
            {
                const auto& value = field->get_ref<const std::string&>();
                const auto& prefix = base->get_ref<const std::string&>();
                if (!prefix.empty() && value.compare(0, prefix.size(), prefix) == 0)
                {
                    suffixes[pointer] = value.substr(prefix.size());
                    field = record.erase(field);
                    continue;
                }
            }
            ++field;
        }
    }

    inline json compact(json record, const std::string& line, bool relative)
    {
        json extra = json::object();
        if (record.contains("sider_rot_per") && record["sider_rot_per"] == "tidal")
            record["sider_rot_per"] = record.at("orb").at("per");
        if (record.value("!origname", std::string()) == record.at("!name")) record.erase("!origname");
        if (record.contains("stsys") && record["stsys"].value("label", std::string()) == record.at("!name"))
            record["stsys"].erase("label");
        if (record.contains("orb"))
        {
            const auto& parent = record["orb"].at("cen_name");
            if (record.value("!origcenname", std::string()) == parent && parent != "")
            {
                extra["original_parent"] = true;
                record.erase("!origcenname");
            }
            if (!text(line, 287, 300).empty()) record["orb"].erase("cen_name");
        }
        record["loc"].erase("sys_cen");
        record["loc"].erase("gal_cen");
        if (relative)
            for (const char* key : {"RA", "decl", "dist"}) record.erase(key);
        json numeric = json::object(), suffixes = json::object();
        remove_columns(record, columns(line), numeric, suffixes);
        prune_defaults(record, defaults(record.contains("orb") || !text(line, 287, 390).empty()));
        if (!record.empty()) extra["record"] = std::move(record);
        if (!numeric.empty()) extra["numeric_residuals"] = std::move(numeric);
        if (!suffixes.empty()) extra["text_suffixes"] = std::move(suffixes);
        return extra;
    }

    inline json restore(const std::string& line, const json& extra)
    {
        const json core = columns(line);
        const json record = extra.value("record", json::object());
        json result = defaults(core.contains("orb") || record.contains("orb"));
        merge(result, core);
        merge(result, record);
        if (!record.contains("!origname")) result["!origname"] = result.at("!name");
        if (result.contains("stsys") && !result["stsys"].contains("label")) result["stsys"]["label"] = result.at("!name");
        if (extra.contains("numeric_residuals"))
            for (const auto& field : extra["numeric_residuals"].items())
            {
                const json::json_pointer path(field.key());
                result[path] = adjusted(result.at(path).get<double>(), field.value());
            }
        if (extra.contains("text_suffixes"))
            for (const auto& field : extra["text_suffixes"].items())
            {
                const json::json_pointer path(field.key());
                result[path] = result.at(path).get<std::string>() + field.value().get<std::string>();
            }
        return result;
    }

    inline Point sky_position(const json& record)
    {
        return Point::from_ra_dec(record.at("RA").get<double>() * fiftyseventh,
            record.at("decl").get<double>() * fiftyseventh, record.at("dist").get<double>() * light_year);
    }

    inline Point velocity(const json& record, Point position)
    {
        const double radius = std::hypot(position.x, position.y, position.z);
        const double ra = radius > 0 ? atan2(-position.x, position.z) : record.at("RA").get<double>() * fiftyseventh;
        const double dec = radius > 0 ? atan2(position.y, std::hypot(position.x, position.z)) : record.at("decl").get<double>() * fiftyseventh;
        const Point radial(-sin(ra) * cos(dec), sin(dec), cos(ra) * cos(dec));
        const Point east(-cos(ra), 0, -sin(ra));
        const Point north(sin(ra) * sin(dec), cos(dec), -cos(ra) * sin(dec));
        return radial * record.value("radvel", 0.0)
            + east * (record.value("pmRA", 0.0) / (fiftyseven * oneyear) * radius * cos(dec))
            + north * (record.value("pmdec", 0.0) / (fiftyseven * oneyear) * radius);
    }

    inline json vector_difference(Point base, Point target)
    {
        json result = json::object();
        for (const auto& axis : {std::make_tuple("x", base.x, target.x), std::make_tuple("y", base.y, target.y),
            std::make_tuple("z", base.z, target.z)})
        {
            const json delta = difference(std::get<1>(axis), std::get<2>(axis));
            if (!delta.is_null()) result[std::get<0>(axis)] = delta;
        }
        return result;
    }

    inline Point adjusted_vector(Point base, const json& correction)
    {
        if (correction.contains("x")) base.x = adjusted(base.x, correction.at("x"));
        if (correction.contains("y")) base.y = adjusted(base.y, correction.at("y"));
        if (correction.contains("z")) base.z = adjusted(base.z, correction.at("z"));
        return base;
    }
}
#endif
