#ifndef ALIENORUM_BROWNCATALOG_H
#define ALIENORUM_BROWNCATALOG_H

#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>
#include "serial.h"
#include "../globals.h"
#include "../catalog_lock_trace.h"
#include "browncolor.h"

namespace alienorum
{
    inline bool condensed_brown_dwarfs_loaded = false;

    void reconcile_catalog_companion_photometry(Star& star);

    inline bool brown_catalog_candidate(const Star& star)
    {
        return !star.is_white_dwarf() && ((star.mass > 0 && star.mass < .075 * solar_mass)
            || (star.spectral_type[0] && strchr("LTY", star.spectral_type[0])));
    }

    inline void estimate_catalog_absolute_magnitude(Star& star, bool repair_census_zero = false,
        bool use_orbit_source_mass = false)
    {
        const bool has_apparent = std::isfinite(star.apparent_magnitude) && std::abs(star.apparent_magnitude) < 99;
        const bool census_default = repair_census_zero && !has_apparent && star.absolute_magnitude == 0
            && !star.user_added && !star.user_edited && brown_catalog_candidate(star)
            && ((star.stellar_parameters.is_object() && star.stellar_parameters.value("census_component", false))
                || (star.spectral_type[0] && strchr("LTY", star.spectral_type[0])));
        if (!census_default && std::isfinite(star.absolute_magnitude) && std::abs(star.absolute_magnitude) < 99) return;
        if (census_default) star.absolute_magnitude = 1e29;
        if (has_apparent && star.distance > 0 && std::isfinite(star.distance))
        {
            star.absolute_magnitude = star.apparent_magnitude - 5 * log10(star.distance / (10 * parsec));
            return;
        }

        double temperature = star.temperature;
        double radius = star.volumetric_mean_radius / solar_radius;
        std::string method = "Visual blackbody estimate from temperature and radius";
        if (!(temperature > 0) && star.spectral_type[0] && strchr("LTY", star.spectral_type[0]))
        {
            temperature = brown_temperature(&star);
            method = "Visual blackbody estimate from spectral type and radius";
        }
        if (!(temperature > 0) && brown_catalog_candidate(star) && star.mass > 0 && radius > 0)
        {
            // Burrows et al. (2001), equation 1, with the reference opacity.
            double age = star.stellar_age.is_object() ? star.stellar_age.value("value_gyr", 1.0) : 1.0;
            if (!std::isfinite(age)) age = 1.0;
            const double luminosity = 4e-5 * pow(std::max(.1, age), -1.3) * pow(star.mass / (.05 * solar_mass), 2.64);
            temperature = sun_temp * pow(luminosity / (radius * radius), .25);
            method = "Visual blackbody estimate from brown dwarf cooling at " + std::to_string(age) + " Gyr";
        }
        if (use_orbit_source_mass && (!std::isfinite(radius) || !(radius > 0) || !(temperature > 0)) &&
            star.mass > .075 * solar_mass && !star.is_white_dwarf())
        {
            const double index = Star::get_mseqidx_from_mass(star.mass / solar_mass);
            if (!(temperature > 0)) temperature = Star::interpolate_mseq_temp(index);
            if (!std::isfinite(radius) || !(radius > 0)) radius = Star::interpolate_mseq_rad(index);
            method = "Visual blackbody estimate with a mass-based stellar calibration";
        }
        if (!(radius > 0) || !std::isfinite(radius)) return;
        if (!(temperature > 0) || !std::isfinite(temperature)) return;
        const double flux = blackbody_flux(temperature, V_band) / blackbody_flux(sun_temp, V_band) * radius * radius;
        if (!(flux > 0) || !std::isfinite(flux)) return;
        const double magnitude = 4.83 - log(flux) * invlogmagnbase;
        if (!std::isfinite(magnitude) || std::abs(magnitude) >= 99) return;
        star.absolute_magnitude = magnitude;
        if (!star.stellar_parameters.is_object()) star.stellar_parameters = json::object();
        star.stellar_parameters["absolute_magnitude_estimate"] = method;
    }

    inline bool brown_component_label(const std::string& name)
    {
        const auto suffix = name.find_last_of(' ');
        return suffix != std::string::npos && suffix + 1 < name.size() && name.size() - suffix <= 3
            && name[suffix + 1] >= 'A' && name[suffix + 1] <= 'Z'
            && (name.size() - suffix == 2 || (name.back() >= 'a' && name.back() <= 'z'));
    }

    inline void update_brown_catalog_photometry(Star& star, const json& record)
    {
        const bool legacy = !star.spectral_type[0] && star.apparent_magnitude_estimated
            && star.temperature == 13723 && star.gaia_g_magnitude >= 99 && brown_catalog_candidate(star);
        const double visual = record.value("vmag", 1e29);
        const bool measured_visual = std::isfinite(visual) && visual > -30 && visual < 99;
        const double gaia_g = record.value("gmag", 1e29);
        const bool measured_gaia = std::isfinite(gaia_g) && gaia_g > -30 && gaia_g < 99;
        if (measured_gaia && star.gaia_g_magnitude >= 99) star.gaia_g_magnitude = gaia_g;
        if (!legacy && star.apparent_magnitude < 99 && !(star.apparent_magnitude_estimated && measured_visual)) return;
        if (legacy)
        {
            star.temperature = 0;
            star.BV_color = star.UB_color = 0;
            star.apparent_magnitude = star.absolute_magnitude = 1e29;
        }
        const auto spectrum = record.value("spectrum", "");
        if (!star.spectral_type[0] && !spectrum.empty())
            snprintf(star.spectral_type, sizeof(star.spectral_type), "%s", spectrum.c_str());
        if (!(star.temperature > 0))
        {
            star.temperature = record.value("temperature", 0.0);
            if (!(star.temperature > 0) && star.spectral_type[0] && strchr("LTY", star.spectral_type[0]))
                star.temperature = brown_temperature(&star);
            if (star.temperature > 0 && !star.is_bv_observed) star.estimate_BV(star.temperature);
        }
        if (measured_visual)
        {
            star.apparent_magnitude = visual;
            star.stellar_parameters["magnitude_band"] = "Johnson V";
        }
        else if (measured_gaia)
        {
            star.apparent_magnitude = star.gaia_g_magnitude;
            star.stellar_parameters["magnitude_band"] = "Gaia G";
        }
        else if (star.temperature > 0 && star.distance > 0 && star.volumetric_mean_radius > 0)
        {
            const double relative_radius = star.volumetric_mean_radius / solar_radius;
            const double flux = blackbody_flux(star.temperature, V_band) / blackbody_flux(sun_temp, V_band)
                * relative_radius * relative_radius;
            star.apparent_magnitude = 4.83 - log(std::max(flux, 1e-100)) * invlogmagnbase
                + 5 * log10(star.distance / (10 * parsec));
            star.stellar_parameters["magnitude_band"] = "estimated Johnson V";
        }
        star.apparent_magnitude_estimated = star.apparent_magnitude < 99 && !measured_visual;
        if (star.apparent_magnitude < 99 && star.distance > 0)
            star.absolute_magnitude = star.apparent_magnitude - 5 * log10(star.distance / (10 * parsec));
        estimate_brown_rotation(&star);
    }

    inline void repair_brown_catalog_star(Star& star)
    {
        if (star.user_added || star.user_edited || !brown_catalog_candidate(star)) return;
        static const json empty_record = json::object();
        if (!strcmp(star.name, "Teegarden's Star") && star.mass < .075 * solar_mass)
        {
            // Dreizler et al. 2024, A&A 684 A117, and the NASA Exoplanet Archive visual magnitude.
            star.mass = .097 * solar_mass;
            star.temperature = 3034;
            star.volumetric_mean_radius = .120 * solar_radius;
            snprintf(star.spectral_type, sizeof(star.spectral_type), "%s", "M7V");
            star.gaia_g_magnitude = 12.2631;
            if (star.apparent_magnitude_estimated || star.apparent_magnitude >= 99)
            {
                star.apparent_magnitude = 15.13;
                star.apparent_magnitude_estimated = false;
                if (star.distance > 0)
                    star.absolute_magnitude = star.apparent_magnitude - 5 * log10(star.distance / (10 * parsec));
            }
            if (!star.is_bv_observed) star.estimate_BV(star.temperature);
            star.stellar_parameters["bulk_parameters_source"] = "https://doi.org/10.1051/0004-6361/202348033";
            star.stellar_parameters["mass_solar_estimated"] = true;
            star.stellar_parameters["mass_method"] = "Published empirical mass calibration";
            star.stellar_parameters["magnitude_band"] = "Johnson V";
            return;
        }
        if (star.temperature == 13723 && star.apparent_magnitude_estimated && !star.spectral_type[0])
        {
            if (brown_component_label(star.name)) star.stellar_parameters["census_component"] = true;
            update_brown_catalog_photometry(star, empty_record);
        }
    }

    inline bool brown_catalog_component(const Star& star)
    {
        return star.stellar_parameters.is_object() && (star.stellar_parameters.value("census_component", false)
            || star.stellar_parameters.contains("component"));
    }

    constexpr double alienorum_magnitude_limit = 10.0;

    void assign_purple_catalog_ids();

    inline bool purple_catalog_id(const std::string& id)
    {
        const auto space = id.find(' ');
        if (space == std::string::npos || space <= 1 || id[space - 1] != 'p') return false;
        const auto first = id.begin() + (id.front() == '-' ? 1 : 0);
        const auto last = id.begin() + space - 1;
        return first < last && std::all_of(first, last, [](unsigned char c) { return std::isdigit(c); });
    }

    inline bool purple_catalog_primary(Star& star)
    {
        if (star.seqno == 0 || star.deleted || star.user_added || star.user_edited || star.orbit
            || !std::isfinite(star.apparent_magnitude) || star.apparent_magnitude <= -30
            || star.apparent_magnitude >= alienorum_magnitude_limit
            || !brown_catalog_candidate(star)) return false;
        if (star.multisys)
        {
            if (star.get_component() != 'A') return false;
        }
        else if (brown_catalog_component(star) || brown_component_label(star.name)) return false;
        const char* spectrum = star.spectral_type;
        const bool late_l = spectrum[0] == 'L' && spectrum[1] >= '7' && spectrum[1] <= '9'
            && star.mass > 0 && star.mass < .065 * solar_mass;
        if (spectrum[0] != 'T' && spectrum[0] != 'Y' && !late_l) return false;
        if (!star.stellar_parameters.is_object()) return !star.apparent_magnitude_estimated;
        if (star.stellar_parameters.value("spectrum_estimated", false)) return false;
        const auto band = star.stellar_parameters.value("magnitude_band", "");
        return band == "Johnson V" || band == "estimated Johnson V"
            || (band.empty() && !star.apparent_magnitude_estimated);
    }

    inline void finalize_brown_catalog()
    {
        std::unordered_map<std::string, Star*> primaries;
        std::vector<Star*> companions;
        for (int index = 1; cels[index]; ++index)
        {
            auto* object = cels[index];
            if (object->deleted || object->typeclass() != class_star) continue;
            auto* star = static_cast<Star*>(object);
            if (star->alienorumid.rfind("p-", 0) == 0 || purple_catalog_id(star->alienorumid))
                star->alienorumid.clear();
            if (star->user_added || star->user_edited || !brown_catalog_candidate(*star)) continue;
            const std::string name = star->name;
            if (brown_catalog_component(*star) && !star->multisys && !star->orbit)
            {
                if (name.size() > 2 && name[name.size() - 2] == ' ' && name.back() >= 'B' && name.back() <= 'Z')
                {
                    const auto root = name.substr(0, name.size() - 2);
                    primaries.try_emplace(stellar_alias_key(root));
                    primaries.try_emplace(stellar_alias_key(root + " A"));
                    companions.push_back(star);
                }
            }
        }
        const auto register_primary = [&](const std::string& name, Star* star)
        {
            const auto found = primaries.find(stellar_alias_key(name));
            if (found == primaries.end()) return;
            auto*& primary = found->second;
            if (!primary) primary = star;
            else if (primary != star) primary = static_cast<Star*>(cels[0]);
        };
        if (!companions.empty())
        {
            for (int index = 1; cels[index]; ++index)
            {
                auto* object = cels[index];
                if (object->deleted || object->typeclass() != class_star) continue;
                auto* star = static_cast<Star*>(object);
                register_primary(star->name, star);
                if (star->Gliese[0]) register_primary(star->Gliese, star);
                for (const auto& alias : star->catalog_aliases) register_primary(alias, star);
            }
        }
        for (auto* star : companions)
        {
            const std::string name = star->name;
            const auto root = name.substr(0, name.size() - 2);
            Star* primary = primaries.at(stellar_alias_key(root + " A"));
            auto* unqualified = primaries.at(stellar_alias_key(root));
            if (!primary) primary = unqualified;
            else if (unqualified && unqualified != primary) continue;
            if (!primary || primary == star || primary == cels[0] || primary->user_added || primary->user_edited
                || (primary->multisys && primary->get_component() != 'A')) continue;
            if (!(star->distance > 0 && primary->distance > 0)
                || fabs(star->distance - primary->distance) > .15 * star->distance) continue;
            const auto component = name.back();
            if (!primary->multisys) (new StarMulti())->add_member(primary, 'A');
            if (primary->multisys->get_member(component)) continue;
            primary->multisys->add_member(star, component);
            star->stellar_parameters["catalog_primary"] = primary->name;
            if (star->alienorumid.empty() && !primary->alienorumid.empty()
                && trim(primary->alienorumid).size() <= 12)
                star->alienorumid = trim(primary->alienorumid) + " " + component;
        }
        assign_purple_catalog_ids();
    }

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
            if (brown_catalog_candidate(*star)) brown.push_back(star);
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
            const auto row_hierarchy = record.value("hierarchy", "");
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
                star->mass = record.value("mass_solar", .04)*solar_mass;
                star->volumetric_mean_radius = record.value("radius_jupiter", 1.0)*jupiter_radius;
                star->radial_velocity = record.value("radial_velocity", 0.0)*1000;
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
            if (record.contains("parameters"))
            {
                if (!target->stellar_parameters.is_object()) target->stellar_parameters = json::object();
                target->stellar_parameters.update(record["parameters"]);
            }
            if (!target->user_added && !target->user_edited)
            {
                if (record.value("position_from_system_member", false)
                    || (row_hierarchy.find('.') != std::string::npos && row_hierarchy.rfind("UCS:", 0) != 0))
                    target->stellar_parameters["census_component"] = true;
                update_brown_catalog_photometry(*target, record);
                repair_brown_catalog_star(*target);
                estimate_catalog_absolute_magnitude(*target, true);
            }
            for (const auto& alias : record.value("aliases", json::array())) target->add_catalog_alias(alias.get<std::string>());
            imported[target] = {system_root(record), row_hierarchy};
            names.add(target->seqno, *target);
        }
        return added;
    }
}
#endif
