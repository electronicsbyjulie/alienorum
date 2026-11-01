#ifndef ALIENORUM_NUMERIC_H
#define ALIENORUM_NUMERIC_H

#include <cmath>
#include <cstdlib>
#include <string>
#include "nlohmann/json.hpp"

namespace alienorum
{
    // Apply only to numeric fields; keep names, spectra, and source annotations intact.
    inline std::string numeric_text(std::string text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        while (!text.empty() && text.back() == '?') text.pop_back();
        const auto last = text.find_last_not_of(" \t\r\n");
        if (last == std::string::npos) return {};
        text.resize(last + 1);
        const auto marker = text.find('?');
        if (marker != std::string::npos)
        {
            const std::string unit = text.substr(marker + 1);
            if (unit == "AU" || unit == "d" || unit == "y" || unit == "s") text.erase(marker, 1);
        }
        return text;
    }

    inline bool parse_numeric_value(const std::string& text, double& value, std::string* suffix = nullptr)
    {
        const std::string normalized = numeric_text(text);
        char* end = nullptr;
        const double parsed = std::strtod(normalized.c_str(), &end);
        if (end == normalized.c_str() || !std::isfinite(parsed)) return false;
        const std::string unit = numeric_text(end);
        if (!suffix && !unit.empty()) return false;
        value = parsed;
        if (suffix) *suffix = unit;
        return true;
    }

    inline void normalize_uncertain_numbers(nlohmann::json& value)
    {
        if (value.is_object())
        {
            for (auto& item : value.items())
            {
                const std::string& key = item.key();
                if (key == "!name" || key == "!origname" || key == "!origcenname" || key == "name" ||
                    key == "BODYNAME" || key == "hostname" || key == "pl_name" || key == "center_name" ||
                    key == "label" || key == "id" || key == "system" || key == "source" ||
                    key == "reference" || key == "catalog_aliases" || key == "n" || key == "on" ||
                    key == "oc" || key == "cen_name" || key == "hash" || key == "generation" ||
                    key == "file" || key == "parts" || key == "aliases" || key == "Bayer" ||
                    key == "Flamsteed" || key == "Gliese" || key == "alienorumid" ||
                    key == "constellation" || key == "Gouldcons" || key == "spectral_type" || key == "sptyp") continue;
                normalize_uncertain_numbers(item.value());
            }
        }
        else if (value.is_array())
        {
            for (auto& item : value) normalize_uncertain_numbers(item);
        }
        else if (value.is_string())
        {
            const std::string text = value.get<std::string>();
            if (text.find('?') == std::string::npos) return;
            const auto number = nlohmann::json::parse(numeric_text(text), nullptr, false);
            if (number.is_number() && std::isfinite(number.get<double>())) value = number;
            else
            {
                double parsed = 0;
                if (parse_numeric_value(text, parsed)) value = parsed;
            }
        }
    }
}

#endif
