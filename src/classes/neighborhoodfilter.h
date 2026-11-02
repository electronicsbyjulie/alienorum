#pragma once

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <utility>
#include <string>
#include "star.h"

namespace alienorum
{
    enum class NeighborhoodType
    {
        all, sunlike, orange, red_brown, sirius, subgiants, giants_ob, white_dwarfs
    };
    enum class NeighborhoodOrder { nearest, brightest };
    enum class NeighborhoodLuminosity { unknown, v, iv_v, iv, iii_iv, iii, ii, i, vi };

    struct NeighborhoodSpectrum
    {
        char type = 0;
        double subtype = -1;
        NeighborhoodLuminosity luminosity = NeighborhoodLuminosity::unknown;
    };

    inline NeighborhoodSpectrum neighborhood_spectrum(const char* text)
    {
        NeighborhoodSpectrum result;
        std::string compact;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        {
            if (!std::isspace(*p)) compact += *p;
        }
        NeighborhoodLuminosity prefix = NeighborhoodLuminosity::unknown;
        if (compact.compare(0, 3, "esd") == 0 || compact.compare(0, 3, "usd") == 0)
        {
            compact.erase(0, 3);
            prefix = NeighborhoodLuminosity::vi;
        }
        else if (compact.compare(0, 2, "sd") == 0)
        {
            compact.erase(0, 2);
            prefix = NeighborhoodLuminosity::vi;
        }
        else if (compact.size() > 1 && compact[0] == 'd' && std::isupper(static_cast<unsigned char>(compact[1])))
        {
            compact.erase(0, 1);
            prefix = NeighborhoodLuminosity::v;
        }
        else if (compact.size() > 1 && compact[0] == 'g' && std::isupper(static_cast<unsigned char>(compact[1])))
        {
            compact.erase(0, 1);
            prefix = NeighborhoodLuminosity::iii;
        }
        for (char& ch : compact) ch = std::toupper(static_cast<unsigned char>(ch));
        const size_t companion = compact.find('+');
        if (companion != std::string::npos) compact.resize(companion);
        if (compact.compare(0, 2, "WD") == 0) result.type = 'D';
        size_t suffix = 0;
        for (size_t pos = 0; pos < compact.size() && !result.type; ++pos)
        {
            const char type = compact[pos];
            if (!strchr("OBAFGKMCLTYDSRNW", type)) continue;
            const char next = pos + 1 < compact.size() ? compact[pos + 1] : 0;
            if (type == 'D' || (type == 'W' && next && strchr("NCO", next))
                || (pos == 0 && (type == 'O' || type == 'B'))
                || !next || std::isdigit(static_cast<unsigned char>(next)) || next == 'I' || next == 'V'
                || (type == 'C' && next == '-'))
            {
                result.type = type;
                suffix = pos + 1;
                if (type == 'D' || type == 'W')
                {
                    while (suffix < compact.size() && std::isalpha(static_cast<unsigned char>(compact[suffix]))) ++suffix;
                }
                char* end = nullptr;
                if (suffix < compact.size() && std::isdigit(static_cast<unsigned char>(compact[suffix])))
                {
                    result.subtype = std::strtod(compact.c_str() + suffix, &end);
                    suffix = end - compact.c_str();
                }
            }
        }
        result.luminosity = prefix;
        const std::pair<const char*, NeighborhoodLuminosity> classes[] =
        {
            {"III-IV", NeighborhoodLuminosity::iii_iv}, {"III/IV", NeighborhoodLuminosity::iii_iv},
            {"IV-V", NeighborhoodLuminosity::iv_v}, {"IV/V", NeighborhoodLuminosity::iv_v},
            {"V-IV", NeighborhoodLuminosity::iv_v}, {"V/IV", NeighborhoodLuminosity::iv_v},
            {"III", NeighborhoodLuminosity::iii}, {"II", NeighborhoodLuminosity::ii},
            {"IV", NeighborhoodLuminosity::iv}, {"VI", NeighborhoodLuminosity::vi},
            {"V", NeighborhoodLuminosity::v}, {"I", NeighborhoodLuminosity::i}
        };
        for (size_t pos = suffix; pos < compact.size(); ++pos)
        {
            if (pos > suffix && std::isalpha(static_cast<unsigned char>(compact[pos - 1]))) continue;
            if (compact.compare(pos, 3, "VAR") == 0) continue;
            for (const auto& entry : classes)
            {
                if (compact.compare(pos, strlen(entry.first), entry.first) == 0)
                {
                    result.luminosity = entry.second;
                    return result;
                }
            }
        }
        return result;
    }

    inline bool neighborhood_dwarf_magnitude(const Star& star, const NeighborhoodSpectrum& spectrum)
    {
        if (spectrum.subtype < 0 || spectrum.subtype >= 10 || !spectrum.type || !strchr("OBAFGKM", spectrum.type)
            || !std::isfinite(star.absolute_magnitude) || std::abs(star.absolute_magnitude) >= 1e28) return false;
        const std::string canonical = std::string(1, spectrum.type) + std::to_string(spectrum.subtype) + "V";
        const double index = Star::get_mseqidx_from_sptyp(canonical.c_str());
        const double luminosity = Star::interpolate_mseq_lum(index), temperature = Star::interpolate_mseq_temp(index);
        if (!(luminosity > 0) || !(temperature > 0)) return false;
        // Allow mild evolution and catalog scatter around the existing main-sequence calibration.
        const double magnitude = 4.74 - std::log(luminosity) / std::log(magnbase) - Star::bolometric_correction(temperature);
        return std::abs(star.absolute_magnitude - magnitude) <= 1.5;
    }

    inline bool neighborhood_matches_type(const Star& star, NeighborhoodType filter)
    {
        if (filter == NeighborhoodType::all) return true;
        const auto spectrum = neighborhood_spectrum(star.spectral_type);
        const auto lum = spectrum.luminosity;
        const bool white = spectrum.type == 'D' || lum == NeighborhoodLuminosity::vi || star.is_white_dwarf();
        if (filter == NeighborhoodType::white_dwarfs) return white;
        const bool unknown = lum == NeighborhoodLuminosity::unknown;
        const bool dwarf = lum == NeighborhoodLuminosity::v;
        switch (filter)
        {
            case NeighborhoodType::sunlike:
                return ((spectrum.type == 'F' && spectrum.subtype >= 8 && spectrum.subtype < 10)
                    || spectrum.type == 'G' || (spectrum.type == 'K' && spectrum.subtype >= 0 && spectrum.subtype <= 2))
                    && (dwarf || (unknown && star.absolute_magnitude >= 4.12 && star.absolute_magnitude <= 5.93));
            case NeighborhoodType::orange:
                return spectrum.type == 'K' && spectrum.subtype >= 3 && spectrum.subtype <= 9
                    && (dwarf || (unknown && neighborhood_dwarf_magnitude(star, spectrum)));
            case NeighborhoodType::red_brown:
                return spectrum.type && strchr("MCLTY", spectrum.type)
                    && (dwarf || (unknown && (strchr("LTY", spectrum.type) || neighborhood_dwarf_magnitude(star, spectrum))));
            case NeighborhoodType::sirius:
                return ((spectrum.type == 'A' && spectrum.subtype >= 0 && spectrum.subtype < 10)
                    || (spectrum.type == 'F' && spectrum.subtype >= 0 && spectrum.subtype <= 7))
                    && (dwarf || lum == NeighborhoodLuminosity::iv_v || neighborhood_dwarf_magnitude(star, spectrum));
            case NeighborhoodType::subgiants:
                return lum == NeighborhoodLuminosity::iv || lum == NeighborhoodLuminosity::iv_v;
            case NeighborhoodType::giants_ob:
                return spectrum.type == 'O' || spectrum.type == 'B' || lum == NeighborhoodLuminosity::i
                    || lum == NeighborhoodLuminosity::ii || lum == NeighborhoodLuminosity::iii || lum == NeighborhoodLuminosity::iii_iv;
            default: return false;
        }
    }
}
