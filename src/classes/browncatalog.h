#ifndef ALIENORUM_BROWNCATALOG_H
#define ALIENORUM_BROWNCATALOG_H

#include <fstream>
#include <memory>
#include <vector>
#include "serial.h"
#include "../globals.h"
#include "../catalog_lock_trace.h"
#include "browncolor.h"

namespace alienorum
{
    inline bool condensed_brown_dwarfs_loaded = false;

    inline std::string brown_dwarf_catalog_signature()
    {
        uint64_t hash = 14695981039346656037ULL;
        bool present = false;
        for (const char* path : {"catalogs/brown_dwarfs_20pc.json", "catalogs/ultracool_dwarfs.json"})
        {
            std::ifstream source(path, std::ios::binary);
            if (!source) continue;
            present = true;
            char value;
            while (source.get(value))
            {
                hash ^= static_cast<unsigned char>(value);
                hash *= 1099511628211ULL;
            }
        }
        return present ? std::to_string(hash) : std::string();
    }

    inline unsigned int load_brown_dwarf_supplement(const char* path = nullptr)
    {
        json catalog = {{"records", json::array()}};
        const std::vector<std::string> paths = path ? std::vector<std::string>{path}
            : std::vector<std::string>{"catalogs/brown_dwarfs_20pc.json", "catalogs/ultracool_dwarfs.json"};
        for (const auto& filename : paths)
        {
            std::ifstream input(filename);
            if (!input) continue;
            json supplement = json::parse(input, nullptr, false);
            if (!supplement.is_object() || !supplement.contains("records") || !supplement["records"].is_array()) continue;
            for (auto& record : supplement["records"]) catalog["records"].push_back(std::move(record));
        }
        CatalogLock lock(catalog_mutex);
        ExactStarSearchIndex names;
        std::vector<Star*> brown;
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i]->typeclass() != class_star) continue;
            Star* star = static_cast<Star*>(cels[i]);
            if (star->stellar_system.is_object() && star->stellar_system.contains("nearby_census"))
            {
                star->stellar_system.erase("nearby_census");
                if (star->stellar_system.empty()) star->stellar_system = nullptr;
            }
            if (!star->is_white_dwarf() && ((star->mass > 0 && star->mass < .075*solar_mass)
                || (star->spectral_type[0] && strchr("LTY", star->spectral_type[0])))) brown.push_back(star);
        }
        const auto system_root = [](const json& record)
        {
            const auto hierarchy = record.value("hierarchy", "");
            return hierarchy.substr(0, hierarchy.find('.'));
        };
        std::map<Star*, std::pair<std::string, std::string>> imported;
        unsigned int added = 0;
        for (const auto& record : catalog["records"])
        {
            Star* target = nullptr;
            for (const auto& alias : record.value("aliases", json::array()))
            {
                const int index = find_object(alias.get<std::string>().c_str(), true, 9e29, 0, &names);
                if (index >= 0 && cels[index]->typeclass() == class_star)
                {
                    target = static_cast<Star*>(cels[index]);
                    break;
                }
            }
            const double parallax = record.value("parallax", 0.0);
            const double distance = parallax > 0 ? 1000/parallax : record.value("distance_pc", 0.0);
            const bool positioned = record.contains("ra") && record.contains("dec") && distance > 0;
            const double ra = record.value("ra", 0.0)*fiftyseventh;
            const double dec = record.value("dec", 0.0)*fiftyseventh;
            const double epoch = J2000+(record.value("epoch", 2000.0)-2000)*oneyear/oneday;
            const double pmra = record.value("pmra", 0.0)*fiftyseventh/(3600000*oneyear)
                /std::max(.001, fabs(cos(dec)));
            const double pmdec = record.value("pmdec", 0.0)*fiftyseventh/(3600000*oneyear);
            const double elapsed = (J2000-epoch)*oneday;
            const Point direction = Point::from_ra_dec(ra+pmra*elapsed, dec+pmdec*elapsed, 1);
            if (!target && positioned && !record.value("position_from_system_member", false))
            {
                for (Star* other : brown)
                {
                    const auto previous = imported.find(other);
                    if (previous != imported.end() && previous->second.first == system_root(record)
                        && previous->second.second != record.value("hierarchy", "")) continue;
                    if (!(other->distance > 0) || fabs(other->distance/parsec-distance) > .25*distance) continue;
                    const double old_elapsed = (J2000-other->epoch)*oneday;
                    const Point old = Point::from_ra_dec(other->right_ascension+other->proper_motion_RA*old_elapsed,
                        other->declination+other->proper_motion_decl*old_elapsed, 1);
                    if ((direction-old).magnitude() < 10*fiftyseventh/3600)
                    {
                        target = other;
                        break;
                    }
                }
            }
            if (!target)
            {
                if (!positioned || ncelobjs >= MAX_CELOBJS-1) continue;
                auto star = std::make_unique<Star>();
                snprintf(star->name, sizeof(star->name), "%s", record.value("name", "Brown dwarf").c_str());
                snprintf(star->spectral_type, sizeof(star->spectral_type), "%s", record.value("spectrum", "").c_str());
                star->right_ascension = ra; star->declination = dec; star->epoch = epoch;
                star->proper_motion_RA = pmra; star->proper_motion_decl = pmdec;
                star->distance = distance*parsec;
                star->distance_known = !record.value("distance_estimated", false);
                star->temperature = record.value("temperature", 0.0);
                if (!(star->temperature > 0)) star->temperature = brown_temperature(star.get());
                star->mass = record.value("mass_solar", .04)*solar_mass;
                star->volumetric_mean_radius = record.value("radius_jupiter", 1.0)*jupiter_radius;
                star->radial_velocity = record.value("radial_velocity", 0.0)*1000;
                star->estimate_BV(star->temperature);
                // Gaia G is retained as a labelled visual proxy. Otherwise use a flux estimate.
                const double visual_flux = blackbody_flux(star->temperature, V_band)/blackbody_flux(sun_temp, V_band)
                    *pow(jupiter_radius/solar_radius, 2);
                star->apparent_magnitude = record.value("gmag", 4.83-log(std::max(visual_flux, 1e-100))*invlogmagnbase
                    +5*log10(distance/10));
                star->apparent_magnitude_estimated = true;
                star->absolute_magnitude = star->apparent_magnitude-5*log10(distance/10);
                estimate_brown_rotation(star.get());
                star->update_location(simnow);
                target = star.release();
                append_cel(target);
                brown.push_back(target);
                ++added;
            }
            const std::string preferred = record.value("name", "");
            if (!target->user_added && !target->user_edited && !target->has_custom_name
                && !preferred.empty() && preferred.size() < strlen(target->name))
            {
                target->add_catalog_alias(target->name);
                snprintf(target->name, sizeof(target->name), "%s", preferred.c_str());
                target->namelen = strlen(target->name);
            }
            if (record.contains("parameters")) target->stellar_parameters = record["parameters"];
            for (const auto& alias : record.value("aliases", json::array())) target->add_catalog_alias(alias.get<std::string>());
            imported[target] = {system_root(record), record.value("hierarchy", "")};
            names.add(target->seqno, *target);
        }
        return added;
    }
}
#endif
