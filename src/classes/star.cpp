#include <iostream>
#include <string.h>
#include <algorithm>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <mutex>
#include <set>
#include "cons.h"
#include "planet.h"
#include "fictionalstars.h"
#include "wdcolor.h"
#include "../globals.h"

using namespace alienorum;

double msq_mass[70], msq_rad[70], msq_lum[70], msq_temp[70], msq_BV[70];
Star **hdcache = nullptr, **hipcache = nullptr;
std::map<std::string, Star*> dmcache;
std::vector<Star*> favestars;
bool show_favestars = false, label_favestars = true;

void Star::refresh_planet_statistics()
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    std::vector<int> valid;
    has_hz_planets = 0;
    for (int index : pl_indices)
    {
        if (!cels || index < 0 || index >= ncelobjs || !cels[index] || cels[index]->deleted
            || cels[index]->typeclass() != class_planet || std::find(valid.begin(), valid.end(), index) != valid.end()) continue;
        Planet* planet = static_cast<Planet*>(cels[index]);
        if (!planet->counts_for_host_statistics() || planet->get_light_center() != this) continue;
        valid.push_back(index);
        if (planet->is_in_con_HZ()) ++has_hz_planets;
    }
    pl_indices.swap(valid);
    has_planets = static_cast<int>(pl_indices.size());
    planet_statistics_temperature = temperature;
    planet_statistics_magnitude = absolute_magnitude;
}
static std::map<std::string, std::set<Star*>> catalog_alias_index;
static std::mutex catalog_alias_mutex;

void Star::add_catalog_alias(const std::string &alias)
{
    std::lock_guard<std::mutex> lock(catalog_alias_mutex);
    if (alias.empty()) return;
    if (std::find(catalog_aliases.begin(), catalog_aliases.end(), alias) == catalog_aliases.end())
    {
        catalog_aliases.push_back(alias);
    }
    catalog_alias_index[stellar_alias_key(alias)].insert(this);
}

std::vector<Star*> alienorum::stellar_alias_matches(const std::string &identifier)
{
    std::lock_guard<std::mutex> lock(catalog_alias_mutex);
    const auto found = catalog_alias_index.find(stellar_alias_key(identifier));
    if (found == catalog_alias_index.end()) return {};
    return {found->second.begin(), found->second.end()};
}

// Canonical key for a Durchmusterung designation, e.g. survey="BD", declination=-2, sequential=5958
// -> "BD-02 05958". Returns an empty string when survey is blank/unset, so callers can test .size().
std::string bonn_survey_key(const char* survey, int declination, unsigned int sequential)
{
    if (!survey[0] || survey[0] == ' ' || !survey[1] || survey[1] == ' ') return std::string();
    char buf[32];
    snprintf(buf, sizeof(buf), "%c%c%+03d %05u", survey[0], survey[1], declination, sequential);
    return std::string(buf);
}

char Star::get_component()
{
    if (!multisys) return 0;
    return multisys->is_member(this);
}

void Star::set_component(char comp, Star* compA)
{
    if (!compA) compA = this;
    if (stellar_system.is_object() && compA->stellar_system.is_object() &&
        stellar_system.at("system") != compA->stellar_system.at("system")) return;

    // assert(!multisys || (compA->multisys == multisys));
    if (multisys && compA->multisys && compA->multisys != multisys)
    {
        compA->multisys->merge(multisys);
    }

    if (!compA->multisys) compA->multisys = new StarMulti();
    multisys = compA->multisys;
    multisys->add_member(this, comp);

    if (comp > 'A')
    {
        if (compA->orbit && compA->orbit->center == this)
        {
            if (orbit) delete orbit;
            orbit = compA->orbit;
            compA->orbit = nullptr;
        }
        if (!orbit) orbit = new Orbit();
        orbit->center = compA;
    }
    else
    {
        if (orbit) delete orbit;
        orbit = nullptr;
    }

    if (!user_edited || user_added) origname = name;
    if (orbit && orbit->center) origcenname = orbit->center->name;
}

Star::Star()
{
    _class = class_star;
    apparent_magnitude = 1e29;
    memset(spectral_type, 0, 32*sizeof(char));
    memset(Bayer, 0, 32*sizeof(char));
    memset(Flamsteed, 0, 32*sizeof(char));
    memset(constellation, 0, 4*sizeof(char));
    memset(Gliese, 0, 16*sizeof(char));
    // sidereal_rotational_period = 25 * oneday;            // Sun
}

std::string alienorum::stellar_alias_key(const std::string &identifier)
{
    std::string key;
    for (unsigned char c : identifier)
    {
        if (c > ' ') key += c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
    }
    return key;
}

std::string alienorum::wd_identifier_key(const std::string &identifier)
{
    std::string key;
    for (unsigned char c : identifier)
    {
        if (c > ' ') key += (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
    }
    if (key.compare(0, 2, "WD") != 0) return "";

    // WDJ identifiers use J2000 coordinates; legacy WD names use B1950.
    // Preserve their separate namespaces and the precision of full WDJ names.
    if (key.compare(0, 3, "WDJ") == 0)
    {
        const size_t sign = key.find_first_of("+-", 3);
        if (sign == std::string::npos || sign == 3 || sign + 1 == key.size()) return "";
        if (key.substr(3, sign - 3).find_first_not_of("0123456789.") != std::string::npos ||
            key.substr(sign + 1).find_first_not_of("0123456789.") != std::string::npos) return "";
        return key;
    }
    if (key.size() < 10 || (key[6] != '+' && key[6] != '-')) return "";
    if (key.find_first_not_of("0123456789", 2) != 6) return "";
    const size_t suffix = key.find('.', 7);
    const size_t end = suffix == std::string::npos ? key.size() : suffix;
    if (end != 10 && end != 11) return "";
    if (key.substr(7, end - 7).find_first_not_of("0123456789") != std::string::npos) return "";
    if (suffix != std::string::npos &&
        (suffix + 1 == key.size() || key.find_first_not_of("0123456789", suffix + 1) != std::string::npos)) return "";

    // Some sources retain the final declination minute, others truncate it.
    // Use the common 4.3 form for matching, retaining any component suffix.
    if (end == 11) key.erase(10, 1);
    return key;
}

namespace
{
    uint64_t published_wd_revision = 0;
    struct PublishedWdColor
    {
        std::string label, gaia, source;
        std::vector<std::string> aliases;
        uint32_t rgb;
        bool selectable = true;
    };

    std::string wd_alias_key(const std::string &identifier)
    {
        std::string key = wd_identifier_key(identifier);
        if (!key.empty()) return key;
        for (unsigned char c : identifier)
        {
            if (c > ' ') key += (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
        }
        return key;
    }

    const std::vector<PublishedWdColor> &published_wd_colors()
    {
        static const auto entries = []()
        {
            std::vector<PublishedWdColor> result;
            std::ifstream input("catalogs/WD_colors.dat");
            std::string line;
            uint64_t revision = 14695981039346656037ULL;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                for (unsigned char c : line)
                {
                    revision ^= c;
                    revision *= 1099511628211ULL;
                }
                revision ^= '\n';
                revision *= 1099511628211ULL;
                if (line.empty() || line[0] == '#') continue;
                PublishedWdColor entry;
                std::string hex, alias;
                std::istringstream row(line);
                if (!(row >> entry.label >> entry.gaia >> hex >> entry.source)) continue;
                if (hex.size() != 7 || hex[0] != '#') continue;
                char *end = nullptr;
                entry.rgb = std::strtoul(hex.c_str() + 1, &end, 16);
                if (!end || *end) continue;
                while (row >> alias)
                {
                    if (alias == "alternative" || alias.compare(0, 21, "unresolved_component=") == 0)
                    {
                        entry.selectable = false;
                    }
                    else if (alias.find('=') == std::string::npos) entry.aliases.push_back(alias);
                }
                result.push_back(entry);
            }
            published_wd_revision = revision;
            return result;
        }();
        return entries;
    }

    const PublishedWdColor *published_wd_color_for(const std::string &identifier)
    {
        static const auto index = []()
        {
            std::map<std::string, const PublishedWdColor*> result;
            for (const auto &entry : published_wd_colors())
            {
                if (!entry.selectable)
                {
                    continue;
                }
                std::vector<std::string> aliases = entry.aliases;
                aliases.push_back(entry.label);
                aliases.push_back(entry.gaia);
                for (const auto &alias : aliases)
                {
                    const std::string key = wd_alias_key(alias);
                    const auto found = result.find(key);
                    if (found == result.end())
                    {
                        result[key] = &entry;
                    }
                    else if (!found->second || found->second->gaia != entry.gaia)
                    {
                        result[key] = nullptr;
                    }
                }
            }
            return result;
        }();
        const auto found = index.find(wd_alias_key(identifier));
        return found == index.end() ? nullptr : found->second;
    }
}

uint64_t alienorum::wd_color_data_revision()
{
    published_wd_colors();
    return published_wd_revision;
}

std::string alienorum::wd_color_gaia_identifier(const std::string &identifier, bool catalog_alias_only)
{
    if (identifier.empty()) return "";
    if (catalog_alias_only)
    {
        static const auto aliases = []()
        {
            std::map<std::string, std::string> result;
            for (const auto& entry : published_wd_colors())
            {
                if (!entry.selectable) continue;
                for (const auto& alias : entry.aliases)
                {
                    const std::string key = wd_alias_key(alias);
                    const auto found = result.find(key);
                    if (found == result.end()) result[key] = entry.gaia;
                    else if (found->second != entry.gaia) found->second.clear();
                }
            }
            return result;
        }();
        const auto found = aliases.find(wd_alias_key(identifier));
        return found == aliases.end() ? "" : found->second;
    }
    const auto *entry = published_wd_color_for(identifier);
    return entry ? entry->gaia : "";
}

int alienorum::associate_wd_color_identifiers(CelestialObject **objects)
{
    int linked = 0, unmatched = 0, conflicts = 0;
    if (!objects) return linked;
    std::map<std::string, std::vector<Star*>> identities;
    for (const auto &entry : published_wd_colors())
    {
        if (!entry.selectable) continue;
        identities[wd_alias_key(entry.gaia)];
        for (const auto &alias : entry.aliases) identities[wd_alias_key(alias)];
    }
    for (int i = 0; objects[i]; ++i)
    {
        if (objects[i]->typeclass() != class_star) continue;
        Star *s = static_cast<Star*>(objects[i]);
        for (const std::string &id : {s->gaia_wd_name, s->WD, std::string(s->Gliese), std::string(s->name)})
        {
            const auto found = identities.find(wd_alias_key(id));
            if (found != identities.end() &&
                std::find(found->second.begin(), found->second.end(), s) == found->second.end())
            {
                found->second.push_back(s);
            }
        }
    }
    for (const auto &entry : published_wd_colors())
    {
        if (!entry.selectable) continue;
        Star *match = nullptr;
        bool ambiguous = false;
        int best_rank = 0;
        const auto consider = [&](const std::string &alias, bool exact_gaia)
        {
            for (Star *s : identities[wd_alias_key(alias)])
            {
                int rank = exact_gaia ? 3 : 1;
                const std::string key = wd_alias_key(alias);
                const char component = s->get_component();
                if (!exact_gaia && component && !key.empty() && key.back() == component &&
                    (key == wd_alias_key(s->Gliese) || key == wd_alias_key(s->name))) rank = 2;
                if (rank > best_rank)
                {
                    best_rank = rank;
                    match = s;
                    ambiguous = false;
                }
                else if (rank == best_rank && match != s) ambiguous = true;
            }
        };
        // Prefer an exact Gaia identity or explicitly identified binary component over a
        // duplicate isolated catalog entry. Do not merge objects or move their coordinates.
        consider(entry.gaia, true);
        for (const auto &alias : entry.aliases) consider(alias, false);
        if (ambiguous || !match || (!match->gaia_wd_name.empty() && match->gaia_wd_name != entry.gaia))
        {
            if (ambiguous || match)
            {
                ++conflicts;
                std::cout << "WD color identity conflict: " << entry.gaia << std::endl;
            }
            else ++unmatched;
            continue;
        }
        match->gaia_wd_name = entry.gaia;
        match->wd_color_valid = false;
        if (match->apply_published_wd_color()) ++linked;
    }
    std::cout << "WD colors: " << linked << " linked, " << unmatched << " absent, " << conflicts << " conflicts." << std::endl;
    return linked;
}

bool Star::apply_published_wd_color()
{
    if (WD.empty() && gaia_wd_name.empty()) return false;
    const uint64_t revision = wd_color_data_revision();
    if (wd_color_valid && wd_color_revision == revision) return true;
    wd_color_valid = false;
    const auto *entry = published_wd_color_for(!gaia_wd_name.empty() ? gaia_wd_name : WD);
    if (!entry)
    {
        return false;
    }
    wd_color_red = (entry->rgb >> 16) & 0xff;
    wd_color_green = (entry->rgb >> 8) & 0xff;
    wd_color_blue = entry->rgb & 0xff;
    wd_color_source = entry->source;
    wd_color_revision = revision;
    wd_color_valid = true;
    return true;
}

bool Star::is_white_dwarf() const
{
    return !WD.empty() || !gaia_wd_name.empty() || spectral_type[0] == 'D' ||
        !strncasecmp(spectral_type, "WD", 2);
}

bool Star::resolve_wd_color(Color &linear_rgb, std::string *source)
{
    if (source) source->clear();
    if (apply_published_wd_color()
        || (is_white_dwarf() && !fictional_origin.is_object() && wd_color_valid && !wd_color_source.empty()))
    {
        linear_rgb = wd_linear_from_srgb((uint32_t(wd_color_red) << 16) |
            (uint32_t(wd_color_green) << 8) | wd_color_blue);
        if (source) *source = wd_color_source;
        return true;
    }

    if (is_white_dwarf() && fictional_origin.is_object()
        && fictional_origin.contains("white_dwarf_atmosphere"))
    {
        const auto& profile = fictional_origin.at("white_dwarf_atmosphere");
        if (profile.is_object() && profile.contains("srgb") && profile.at("srgb").is_number_unsigned()
            && profile.contains("template_temperature_k") && profile.at("template_temperature_k").is_number()
            && profile.value("template_spectral_type", std::string()) == spectral_type
            && fabs(temperature-profile.at("template_temperature_k").get<double>()) < 1)
        {
            linear_rgb = wd_linear_from_srgb(profile.at("srgb").get<uint32_t>());
            if (source) *source = profile.value("template_source", std::string("Hollands2026"))+"_fictional_template_approx";
            return true;
        }
    }
    WdAtmosphere atmosphere;
    if (spectral_type[0] != 'D') return false;
    if (spectral_type[1] == 'A') atmosphere = WdAtmosphere::Hydrogen;
    else if (spectral_type[1] == 'B') atmosphere = WdAtmosphere::Helium;
    else if (spectral_type[1] == 'C' && fictional_origin.is_object()
        && fictional_origin.contains("white_dwarf_atmosphere"))
    {
        const auto& profile = fictional_origin.at("white_dwarf_atmosphere");
        const std::string composition = profile.is_object() ? profile.value("composition", std::string()) : std::string();
        if (composition == "helium") atmosphere = WdAtmosphere::Helium;
        else if (composition == "hydrogen") atmosphere = WdAtmosphere::Hydrogen;
        else return false;
    }
    else return false;
    // Mixed, magnetic, uncertain and peculiar spectra lack a suitable pure-atmosphere fit.
    // Ordinary DAZ stars may use the hydrogen approximation; broad DZ opacity cannot.
    for (size_t i = 2; spectral_type[i]; i++)
    {
        const char c = spectral_type[i];
        if ((c >= '0' && c <= '9') || c == '.') continue;
        if (c == 'Z' && atmosphere == WdAtmosphere::Hydrogen) continue;
        return false;
    }
    if (!wd_atmosphere_color(temperature, atmosphere, linear_rgb)) return false;
    bool gravity_adjusted = false;
    if (atmosphere == WdAtmosphere::Hydrogen && mass > 0 && volumetric_mean_radius > 0)
    {
        const double logg = log10(100 * G * mass / (volumetric_mean_radius * volumetric_mean_radius));
        gravity_adjusted = wd_hydrogen_gravity_color(temperature, logg, linear_rgb);
    }
    if (source)
    {
        *source = gravity_adjusted ? "HollandsC1_KoesterSVO_gravity_ratio_approx" : atmosphere == WdAtmosphere::Hydrogen
            ? "Hollands2026_C1_H_logg8_approx" : "Hollands2026_C2_He_logg8_approx";
    }
    // Derived colors stay separate from published records. Recompute the small interpolation
    // from the saved temperature and spectral type so edits and cache reloads cannot stale it.
    return true;
}

Star::~Star()
{
    if (hdcache && HD > 0 && HD <= MAX_HD && hdcache[HD] == this) hdcache[HD] = nullptr;
    if (hipcache && HIP > 0 && HIP <= MAX_HIP && hipcache[HIP] == this) hipcache[HIP] = nullptr;
    std::lock_guard<std::mutex> lock(catalog_alias_mutex);
    for (const auto &alias : catalog_aliases)
    {
        const auto entry = catalog_alias_index.find(stellar_alias_key(alias));
        if (entry == catalog_alias_index.end()) continue;
        entry->second.erase(this);
        if (entry->second.empty()) catalog_alias_index.erase(entry);
    }
    if (orbit) delete orbit;
}

StarMulti::~StarMulti()
{
    if (members) delete[] members;
}

void Star::set_fictional_motion(Point space_velocity)
{
    has_fictional_motion = true;
    fictional_epoch_position = location.system_center;
    fictional_space_velocity = space_velocity;
    fictional_origin["motion"] = {{"model", "galactic_velocity_ellipsoid_v1"},
        {"reference", "stationary_galaxy_center"}, {"propagation", "linear_cartesian"},
        {"epoch_position_m", fictional_epoch_position.to_json()},
        {"velocity_m_s", space_velocity.to_json()}};
    // Project against the full sightline, including the remote galaxy's offset.
    const Point sightline = location.system_center + location.galactic_center*(1e6*light_year);
    distance = std::max(1.0, sightline.magnitude());
    right_ascension = atan2(-sightline.x, sightline.z);
    if (right_ascension < 0) right_ascension += 2*_pi;
    declination = atan2(sightline.y, hypot(sightline.x, sightline.z));
    const Point radial = sightline*(1/distance);
    const Point east(-cos(right_ascension), 0, -sin(right_ascension));
    const Point north(sin(right_ascension)*sin(declination), cos(declination),
        -cos(right_ascension)*sin(declination));
    const auto dot = [](Point a, Point b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
    radial_velocity = dot(space_velocity, radial);
    proper_motion_RA = dot(space_velocity, east)/(distance*std::max(1e-12, cos(declination)));
    proper_motion_decl = dot(space_velocity, north)/distance;
}

void Star::update_location(double tmnow)
{
    if (!(volumetric_mean_radius > 0) || !std::isfinite(volumetric_mean_radius))
    {
        if (stellar_regime(this) == regime_degenerate) estimate_radius();
    }
    double elapsed, l_dist=0;
    if (orbit)
    {
        update_orbit_location(tmnow);
    }
    else if (has_fictional_motion)
    {
        elapsed = tmnow - J2000_TIME_T + oneday*(J2000-epoch);
        location.system_center = fictional_epoch_position + fictional_space_velocity*elapsed;
        l_dist = (location.system_center + location.galactic_center*(1e6*light_year)).magnitude();
    }
    else
    {
        // How many seconds since star's epoch
        elapsed = tmnow - J2000_TIME_T + oneday * (J2000 - epoch);

        // Estimate RA and Decl using proper motion
        double l_RA = right_ascension + proper_motion_RA * elapsed;
        double l_Decl = declination + proper_motion_decl * elapsed;

        // Estimate distance using radial velocity
        l_dist = distance + radial_velocity * elapsed;

        // Compute new location
        Point newloc = Point::from_ra_dec(l_RA, l_Decl, l_dist);

        // Set system location
        location.system_center = newloc;

        if (!estimated_poles)
        {
            if (!lock_system_plane) location.local_system_plane = system_plane_from_incl_and_node(known_poles ? obliquity : (_pi/2), equinox,
                Point::from_ra_dec(right_ascension, declination, distance));
            if ((mycenobj == this) && !lock_equatorial_plane)
            {
                location.orbital_plane = location.local_system_plane;

                Point eqaxis(sin(equinox), 0, cos(equinox));
                Point my_eq_pole = rotate3D(yaxis, center, eqaxis, obliquity);
                my_eq_pole = rotate3D(my_eq_pole, center, location.local_system_plane.v, -location.local_system_plane.a);
                location.equatorial_plane = align_points_3d(my_eq_pole, yaxis, center);
            }
        }
    }

    if (variability_period && !is_eclipsing_binary)
    {
        // tmnow already includes the light travel time.
        elapsed = simnow - J2000_TIME_T + oneday * (J2000 - epoch_max_brightness)
            - (tmprel.magnitude() - l_dist) / speed_of_light;
        double phase = elapsed / variability_period;
        double min_lum = pow(magnbase, -maxmag), max_lum = pow(magnbase, -minmag);
        double local_lum = (max_lum + min_lum) * 0.5 + (max_lum - min_lum) * 0.5 * cos(phase * _pi * 2);
        double local_mag = -log(local_lum) * invlogmagnbase;
        apparent_magnitude = local_mag;

        // TODO: make these next two lines their own fuction.
        double intrinsic_brightness = pow(magnbase, -apparent_magnitude) * pow(fmax(AU, distance) / parsec / 10, 2);
        absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        volumetric_mean_radius = estimate_radius(true);
    }
    const auto changed = [](double before, double after)
    {
        return before != after && !(std::isnan(before) && std::isnan(after));
    };
    if (!pl_indices.empty() && (changed(planet_statistics_temperature, temperature)
        || changed(planet_statistics_magnitude, absolute_magnitude)))
    {
        Planet::mark_host_statistics_dirty(this);
        planet_statistics_temperature = temperature;
        planet_statistics_magnitude = absolute_magnitude;
    }
}

void Star::rename_from_Bayer_Flamsteed()
{
    if (!strlen(constellation)) return;
    if (has_custom_name) return;
    if (orbit && orbit->center && orbit->center->typeclass() == class_star
        && BayerGrkno < 0 && !FlamsteedNo && GouldNo < 1
        )
    {
        std::string buildname = lop_component(orbit->center->name);
        buildname += std::string(" B");
        strcpy(name, buildname.c_str());
        return;
    }
    if (BayerGrkno < 0 && !FlamsteedNo && (GouldNo < 1)) return;

    if (!constellations.size())
    {
        std::cerr << "Must read constellation definitions before setting Bayer-Flamsteed names." << std::endl;
        throw 0xbadc0de;
    }

    // Find gentive of constellation.
    int i, j=-1, n = constellations.size();
    for (i=0; i<n; i++)
    {
        if (!strcmp(constellations[i].abbrev.c_str(), constellation))
        {
            j = i;
            break;
        }
    }

    if (j<0)
    {
        // Not a valid constellation.
        constellation[0] = 0;
        return;
    }

    // Values >= 100 aren't a raw Greek-letter index: grkno_from_abbrev() encodes a numbered variant
    // (e.g. Gam1Vel, Gam2Vel) as 100 + 10*index + component_digit, decoded just below. Only an index
    // outside the plain 0-23 range *and* outside that encoding is actually invalid.
    if (BayerGrkno > 23 && BayerGrkno < 100) BayerGrkno = -1;
    if (BayerGrkno >= 0)
    {
        int BayerGrkIdx = BayerGrkno;

        if (BayerGrkIdx >= 100)
        {
            BayerGrkIdx -= 100;
            BayerGrkIdx/= 10;
        }

        if (BayerGrkIdx >= 0 && BayerGrkIdx < 24) 
        {
            int number = (strlen(Bayer) > 3) ? atoi(std::string(Bayer).substr(3, 1).c_str()) : 0;
            if (number)
            {
                strcpy(name, (Greek_letter[BayerGrkIdx] + std::string(" ") + std::to_string(number) + std::string(" ") + constellations[j].genitive).c_str());
            }
            else strcpy(name, (Greek_letter[BayerGrkIdx] + std::string(" ") + constellations[j].genitive).c_str());

        }
    }
    else if (FlamsteedNo > 0)
    {
        strcpy(name, (std::to_string(FlamsteedNo) + std::string(" ") + constellations[j].genitive).c_str());
    }
    else if (GouldNo > 0)
    {
        if (strlen(Gliese))
            strcpy(name, Gliese);
        else strcpy(name, (std::to_string(GouldNo) + std::string(" G. ") + Gouldcons /* constellations[j].genitive*/).c_str());
    }

    if (multisys && multisys->get_member('A') == this)
    {
        Star* companion;
        for (char c = 'B'; (companion = multisys->get_member(c)); c++)
        {
            if (companion->has_custom_name) continue;

            // Only borrow "<primary base> <letter>" when the companion has no identity of its own,
            // or it's identical to the primary's -- never when it already reads differently (Zeta
            // 1/2 Reticuli, each with its own Bayer name).
            companion->assign_identifier_name();
            std::string base = lop_component(name);
            if (!trim(companion->name).size() || !strcmp(trim(companion->name).c_str(), base.c_str()))
                strcpy(companion->name, (base + std::string(" ") + std::string(1, c)).c_str() );
            companion->namelen = strlen(companion->name);
        }
    }

    if (!user_edited || user_added) origname = name;
    if (orbit && orbit->center) origcenname = orbit->center->name;
}

bool alienorum::Star::matches_constellation(const char *match_cons)
{
    return (constellation[0] & 0x5f) == (match_cons[0] & 0x5f)
        && (constellation[1] & 0x5f) == (match_cons[1] & 0x5f)
        && (constellation[2] & 0x5f) == (match_cons[2] & 0x5f);
}

bool Star::is_sunlike()
{
    const char* sptyp = spectral_type;
    int i;

    // Must contain any of the letters OBAFGKM
    for (i=0; sptyp[i] && (sptyp[i] < 'A' || sptyp[i] > 'Z'); i++);
    if (!sptyp[i]) return false;
    char mklett = sptyp[i];

    // Followed by a number
    i++;
    if (sptyp[i] < '0' || sptyp[i] > '9') return false;
    float mklettsub = atof(&sptyp[i]);

    // Number might contain a decimal point and more digits.
    i++;
    while (sptyp[i] && (sptyp[i] == '.' || (sptyp[i] >= '0' && sptyp[i] <= '9'))) i++;
    if (!sptyp[i])
    {
        // For stars that don't have a Roman numeral,
        // but are within the temperature and luminosity limits,
        // let's go ahead and consider them sunlike.
        if (absolute_magnitude >= 4.12 && absolute_magnitude <= 5.93) return true;
        return false;
    }

    // There might be a space between.
    while (sptyp[i] == ' ') i++;
    if (!sptyp[i]) return false;

    // If the next letter is V, and it's not followed by I, then we're in the main sequence.
    bool mainseq = (sptyp[i] == 'V' && sptyp[i+1] != 'I');

    return mainseq && ((mklett == 'F' && mklettsub >= 8) || (mklett == 'G') || (mklett == 'K' && mklettsub <= 2));
}

bool Star::is_in_visible_box(Point seen_from)
{
    if (is_faved) return true;
    if (visible_area_set && frand(0,1) > 0.03) return _is_in_visible_range;
    return is_really_truly_in_visible_box(seen_from);
}

bool Star::is_really_truly_in_visible_box(Point seen_from)
{
    if (_is_always_visible || is_faved) return true;
    double effmag = variability_period ? minmag : apparent_magnitude;
    double cutoff_dist = (pow(100.0, 0.2*(normal_best_mag_limit-effmag)) * distance) * global_brightness;
    if (fictional_origin.is_object())
    {
        // A galaxy-relative reference distance is not a solar catalog distance.
        // Use intrinsic light for a conservative visibility bound in any galaxy.
        cutoff_dist = distance_from_magnitudes(normal_best_mag_limit, absolute_magnitude)
            * sqrt(std::max(1.0, global_brightness)*std::max(1.0, zoom));
    }
    visible_area.corner1 = Point(-cutoff_dist, -cutoff_dist, -cutoff_dist) + (Point)location;
    visible_area.corner2 = Point( cutoff_dist,  cutoff_dist,  cutoff_dist) + (Point)location;
    visible_area_set = true;

    _is_in_visible_range = visible_area.point_in_box(seen_from);
    return _is_in_visible_range;
}

void Star::make_universally_visible()
{
    visible_area.corner1 = Point(-1.37e+9*light_year, -1.37e+9*light_year, -1.37e+9*light_year);
    visible_area.corner2 = Point( 1.37e+9*light_year,  1.37e+9*light_year,  1.37e+9*light_year);
    visible_area_set = true;
    _is_always_visible = true;
}

double Star::estimate_temperature()
{
    if (temperature) return temperature;
    if (!strlen(spectral_type)) return 5800;
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) return interpolate_mseq_temp(msqi);

    return 2000;
}

double Star::estimate_luminosity(double tempK)
{
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax)
    {
        // The main-sequence relation only holds for a star actually on the main sequence. B-V is
        // just a color: a white dwarf can be exactly as blue/white as a normal F or A star of the
        // same temperature (this is how Gliese 86 B, a DQ white dwarf, ended up borrowing a whole
        // main-sequence star's luminosity -- tens of times brighter than its primary). A known
        // radius far smaller than that relation's own prediction for this color is a compact
        // object, not a main-sequence star of that color, so fall through to the physical estimate
        // below instead of trusting the lookup.
        double mseq_rad = interpolate_mseq_rad(msqi);
        if (!(volumetric_mean_radius > 0) || !(mseq_rad > 0) || volumetric_mean_radius/solar_radius > 0.2 * mseq_rad)
            return interpolate_mseq_lum(msqi);
    }

    return std::pow(volumetric_mean_radius/solar_radius, 2) * std::pow(tempK/sun_temp, 4);
}

void Star::estimate_BV()
{
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) BV_color = interpolate_mseq_BV(msqi);
    else
    {
        double T = estimate_temperature();
        estimate_BV(T);
    }
}

void Star::estimate_UB()
{
    double T = estimate_temperature();
    estimate_UB(T);
}

void Star::estimate_BV(double T)
{
    BV_color = log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
}

void Star::estimate_UB(double T)
{
    UB_color = log(blackbody_flux(T, B_band) / blackbody_flux(T, U_band)) * invlogmagnbase;
}

// Exact inverse of estimate_BV(double T) above: the same black body relation, solved in
//temperature. B-V y decreases monotonically with T, so a bisection is sufficient and cannot
// take the wrong branch.
double Star::temperature_from_BV(double BV)
{
    auto bv_of = [](double T) -> double
    {
        return log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
    };

    double lo = 1000.0, hi = 200000.0;
    if (BV >= bv_of(lo)) return lo;
    if (BV <= bv_of(hi)) return hi;

    for (int i = 0; i < 50; i++)
    {
        double mid = 0.5 * (lo + hi);
        if (bv_of(mid) > BV) lo = mid;                      // still too cold : B-V too red
        else hi = mid;
    }

    return 0.5 * (lo + hi);
}

// Mass-radius relationship for degenerate matter, in its simple analytical form:
//     R/R(Sun) = 0.0126 * (M/M(Sun))^(-1/3) * sqrt(1 - (M/M_Chandrasekhar)^(4/3))
// Verified against Sirius B (M = 1.02 M(Sun)): 5,290 km calculated vs. 5,850 km measured—a 10%
// discrepancy. More than sufficient for a rendering radius—without which a white dwarf has no
// usable radius and its disk is never resolved—but not for asteroseismology.
double Star::degenerate_radius(double mass_grams)
{
    const double chandrasekhar = 1.44;
    double m = mass_grams / solar_mass;

    if (!(m > 0)) m = 0.6;                                  // masse typique du pic observe, faute de mieux
    if (m > 0.99 * chandrasekhar) m = 0.99 * chandrasekhar;

    double core = 1.0 - pow(m / chandrasekhar, 4.0/3.0);
    if (core < 0) core = 0;

    return 0.0126 * pow(m, -1.0/3.0) * sqrt(core) * solar_radius;
}

// Bolometric correction BC_V, such that M_bol = M_V + BC_V. Extracted from
// Planet::est_bolometric_flux(), where it lived online -- a purely stellar magnitude housed
// in a planet method, and above all inaccessible to the exoestar charger which must apply
// the reverse conversion.
double Star::bolometric_correction(double t_eff)
{
    if (!(t_eff > 0)) t_eff = sun_temp;

    if (t_eff > 50000.0)
    {
        // Beyond the empirical fit, continue with the Planck V-band/total-flux ratio.
        // Anchoring at the boundary avoids extrapolating the polynomial into hot remnants.
        return bolometric_correction(50000.0)
            + 2.5 * log10(blackbody_flux(t_eff, V_band) / blackbody_flux(50000.0, V_band))
            - 10.0 * log10(t_eff / 50000.0);
    }

    if (t_eff < 3500.0)
    {
        // Linear interpolation for M dwarfs, based on BT-Settl atmosphere models
        // used by Kopparapu: BC_V is approximately -1.75 at 3500 K and -4.33 at 2500 K.
        double t_fraction = (t_eff - 2500.0) / (3500.0 - 2500.0);
        return -4.33 + t_fraction * (-1.75 - (-4.33));
    }

    // Flower (1996), corrected coefficients from Torres (2010), Table 1.
    // https://arxiv.org/abs/1008.3913
    // Evaluate in log10(K); a quadratic in kelvins diverges for hot stars.
    const double x = log10(t_eff);
    if (x < 3.70)
    {
        return ((381.476328422343 * x - 4212.78819301717) * x
            + 15514.4866764412) * x - 19053.7291496456;
    }
    if (x < 3.90)
    {
        return (((-170.623810323864 * x + 2617.24637119416) * x
            - 15065.1486316025) * x + 38567.2629965804) * x - 37051.0203809015;
    }
    return ((((78.8731721804990 * x - 1705.87278406872) * x
        + 14741.2923562646) * x - 63623.3812100225) * x
        + 137145.973583929) * x - 118115.450538963;
}

// Center-edge darkening, quadratic law I(mu)/I(0) = 1 - a(1-mu) - b(1-mu)^2.
//
// The shader previously employed a fixed pow(mu, 1/3) for any self-luminous body. It's a
// acceptable approximation at the center of the disc and false at the limb, where it falls to ZERO: the edge
// of the star was almost black, and as the halo of draw_flare() is traced behind and
// continue beyond, the result was a dark border stuck between a shiny heart and a crown
// brilliant (measurement on Sirius B: hollow at 207 against 277 just beyond, at the exact radius of the disk).
// No star dims to black: the solar limb is still about 30% from the center
// in the visible.
//
// We first model the total loss at the limb, u = 1 - I(limb)/I(center), then we distribute it
// between the two terms. It decreases with temperature -- a hot star is significantly less
// darkened in the visible -- and increases when surface gravity drops, the atmosphere expands
// of a cold giant being the most marked case. Target benchmarks: Sun 0.70 (observed value,
// and the (a, b) which come out of it, 0.49 and 0.21, well frame the 0.47 and 0.23 tabules by Claret),
// hot dwarf 0.50, cold giant ~0.90, white dwarf ~0.36.
void Star::limb_darkening_coefficients(double &a, double &b)
{
    double T = (temperature > 0) ? temperature : temperature_from_BV(BV_color);
    if (!(T > 0) || isnan(T) || isinf(T)) T = sun_temp;

    // Masses in grams and lengths in meters here, and G is defined accordingly: G*M/R^2 comes out 
    // directly in m/s^2. The factor 100 passes to cgs, the unit in which log g is quoted.
    double radius_m = volumetric_mean_radius;
    if (!(radius_m > 0)) radius_m = solar_radius;
    double mass_grams = (mass > 0) ? mass : solar_mass;
    double logg = log10(fmax(1e-6, G * mass_grams / (radius_m * radius_m)) * 100.0);

    double u = 0.72 - 0.40 * log10(T / sun_temp) + 0.03 * (4.4 - logg);
    if (u < 0.25) u = 0.25;
    if (u > 0.92) u = 0.92;

    b = 0.30 * u;                                   // distribution between linear and quadratic term
    a = u - b;
}

// Photometric classification -- see the comment by stellar_regime_t (star.h) for the rationale,
// and STELLAR_TEXTURE_PLAN.md §5.1 for details on the two pitfalls addressed here.
alienorum::stellar_regime_t alienorum::stellar_regime(CelestialObject *cel)
{
    if (!cel || cel->typeclass() != class_star) return regime_none;
    if (static_cast<Star*>(cel)->is_white_dwarf()) return regime_degenerate;

    // Neither estimate_temperature() nor the main-sequence table it relies on: we
    // invert the blackbody relation, or use the catalog temperature when available.
    double T = (cel->temperature > 0) ? cel->temperature : Star::temperature_from_BV(cel->BV_color);
    if (!(T > 0) || isnan(T) || isinf(T)) return regime_stellar;

    // Radius derived from luminosity and temperature using Stefan-Boltzmann. We must strictly
    // avoid using volumetric_mean_radius: it may have been generated by estimate_radius()
    // based on the main-sequence table, and there is no way to distinguish a measured
    // value from an interpolated one. A stored radius of 2 R(Sun) for a white dwarf
    // is not a refutation; it is a symptom.
    double implied_radius = 0;                              // solar radii
    double absmag = cel->absolute_magnitude;
    if (absmag != 0 && !isnan(absmag) && !isinf(absmag))
    {
        double lum = pow(magnbase, -(absmag - 4.83));       // 4.83 = absolute V magnitude of the Sun
        implied_radius = sqrt(lum) * pow(sun_temp / T, 2.0);
    }

    // Radius alone would be misleading for brown dwarfs: an L0 yields ~0.009 R_Sun using this
    // formula—firmly in white dwarf territory—because M_V ignores the infrared, where the
    // object emits the bulk of its flux. Temperature is therefore the deciding factor, as the
    // two populations are distinct in practice (catalogued white dwarfs: 10,000 to 170,000 K).
    if (implied_radius > 0 && implied_radius < 0.05 && T > 4000.0) return regime_degenerate;

    // Frontiere M/L. It is physically fuzzy -- a young, massive brown dwarf affected 
    // 2500-2900 K and looks like a dusty M -- so we don't put it on the sole 
    //temperature. The mass decides when it is known: the fusion of hydrogen is extinguished towards 
    // 0.075 M(sun). Otherwise, the threshold is placed at the bottom of the sequence M rather than at 2700 K, which 
    // placed all M6 to M9 among brown dwarfs -- they then obtained no cards 
    // and appeared as smooth balls, whereas on the contrary they are the most spotted.
    double mass_solar = cel->mass / solar_mass;
    if (mass_solar > 0)
    {
        if (mass_solar < 0.075) return regime_substellar;
    }
    else if (T < 2300.0) return regime_substellar;

    return regime_stellar;
}

void alienorum::Star::load_main_seq_dat()
{
    FILE *fp = fopen("catalogs/mainseq.dat", "rb");
    if (fp)
    {
        char buffer[1024];
        int i = mseqmin;
        while (fgets(buffer, 1022, fp))
        {
            if (strlen(buffer) < 41) continue;
            if (buffer[0] == '#') continue;
            //           1111111111222222222233333333334444444444
            // 01234567890123456789012345678901234567890123456789
            // O3V     59      13.43   660693  45900   −0.330
            buffer[15] = buffer[23] = buffer[31] = buffer[39] = 0;
            msq_mass[i] = atof(&buffer[8]);
            msq_rad[i] = atof(&buffer[16]);
            msq_lum[i] = atof(&buffer[24]);
            msq_temp[i] = atof(&buffer[32]);
            msq_BV[i] = atof(&buffer[40]);
            i++;
            if (i>=mseqmax) break;
        }
        fclose(fp);
    }
}

double alienorum::Star::get_mseqidx_from_sptyp(const char *sptyp)
{
    int offset = 0;
    if (sptyp[offset] >= 'a' && sptyp[offset] <= 'z') offset++;
    double result = -1;
    if (sptyp[offset] == 'M') result = 60;
    if (sptyp[offset] == 'K') result = 50;
    if (sptyp[offset] == 'G') result = 40;
    if (sptyp[offset] == 'F') result = 30;
    if (sptyp[offset] == 'A') result = 20;
    if (sptyp[offset] == 'B') result = 10;
    if (sptyp[offset] == 'O') result =  0;
    if (result < 0) return result;
    result += atof(&sptyp[offset+1]);
    return result;
}

double alienorum::Star::get_mseqidx_from_mass(double m)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_mass[i] <= m)
        {
            if (i == mseqmin) return i;
            delta = msq_mass[i-1] - msq_mass[i];
            d = m - msq_mass[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_rad(double rad)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_rad[i] <= rad)
        {
            if (i == mseqmin) return i;
            delta = msq_rad[i-1] - msq_rad[i];
            d = rad - msq_rad[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_lum(double lum)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_lum[i] <= lum)
        {
            if (i == mseqmin) return i;
            delta = msq_lum[i-1] - msq_lum[i];
            d = lum - msq_lum[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_temp(double T)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_temp[i] <= T)
        {
            if (i == mseqmin) return i;
            delta = msq_temp[i-1] - msq_temp[i];
            d = T - msq_temp[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_BV(double BV)
{
    // Unlike msq_mass/msq_rad/msq_lum/msq_temp, msq_BV increases with i (hot to cool),
    // so this scans for the first entry >= BV rather than <= BV.
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_BV[i] >= BV)
        {
            if (i == mseqmin) return i;
            delta = msq_BV[i] - msq_BV[i-1];
            d = BV - msq_BV[i-1];
            coeff = d/delta;
            return (double)(i-1) + coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::interpolate_mseq_mass(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i<0) i=0;
    if (i >= mseqmax-1) return msq_mass[mseqmax-1];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_mass[i] + coeff1 * msq_mass[i+1];
}

double alienorum::Star::interpolate_mseq_rad(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_rad[mseqmax-1];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_rad[i] + coeff1 * msq_rad[i+1];
}

double alienorum::Star::interpolate_mseq_lum(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_lum[mseqmax-1];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_lum[i] + coeff1 * msq_lum[i+1];
}

double alienorum::Star::interpolate_mseq_temp(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_temp[mseqmax-1];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_temp[i] + coeff1 * msq_temp[i+1];
}

double alienorum::Star::interpolate_mseq_BV(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_BV[mseqmax-1];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_BV[i] + coeff1 * msq_BV[i+1];
}

bool Star::is_hot_ob_star() const
{
    if (spectral_type[0] == 'O' || spectral_type[0] == 'B')
    {
        return true;
    }
    if (spectral_type[0] == 'd' && (spectral_type[1] == 'O' || spectral_type[1] == 'B'))
    {
        return true;
    }
    double msqi = get_mseqidx_from_sptyp(spectral_type);
    if (msqi >= 0 && msqi < 20.0)
    {
        return true;
    }
    if (temperature >= 10000.0)
    {
        return true;
    }
    if (BV_color < -0.05 && BV_color != 0.0)
    {
        return true;
    }
    return false;
}

bool Star::is_main_sequence() const
{
    if (is_white_dwarf())
    {
        return false;
    }
    if (!spectral_type[0])
    {
        return false;
    }

    const char* lsptyp = spectral_type;

    while (*lsptyp <= ' ')
    {
        lsptyp++;
        if (!*lsptyp) return false;
    }

    // Check for 'd' prefix (e.g. "dM3", "dK5", "dG2")
    if (lsptyp[0] == 'd' && lsptyp[1] >= 'A' && lsptyp[1] <= 'M')
    {
        return true;
    }

    if (!strchr("OBAFGKM", lsptyp[0])) return false;

    // Look for luminosity class 'V'
    for (int i = 0; lsptyp[i]; i++)
    {
        if (lsptyp[i] == 'V')
        {
            // Avoid "IV" (subgiant) or "VI" (subdwarf)
            bool prev_I = (i > 0 && lsptyp[i - 1] == 'I');
            bool next_I = (lsptyp[i + 1] == 'I');
            if (!prev_I && !next_I)
            {
                return true;
            }
        }
    }
    return false;
}

double Star::expected_main_sequence_absmag() const
{
    if (is_hot_ob_star())
    {
        return 1e29;
    }

    double msqi = -1.0;

    if (spectral_type[0])
    {
        msqi = get_mseqidx_from_sptyp(spectral_type);
    }
    if (msqi < mseqmin || msqi > mseqmax)
    {
        if (temperature > 0)
        {
            msqi = get_mseqidx_from_temp(temperature);
        }
        else if (BV_color != 0)
        {
            msqi = get_mseqidx_from_BV(BV_color);
        }
    }

    if (msqi >= 20.0 && msqi <= mseqmax)
    {
        double lum = interpolate_mseq_lum(msqi);
        if (lum > 0)
        {
            return 4.74 - log(lum) / log(magnbase)
                - bolometric_correction(interpolate_mseq_temp(msqi));
        }
    }
    return 1e29;
}

bool Star::is_mseq_absmag_outlier(double threshold, double* diff) const
{
    if (!is_main_sequence() || is_hot_ob_star())
    {
        return false;
    }

    double exp_mag = expected_main_sequence_absmag();
    if (exp_mag > 1e28 || isnan(exp_mag) || isinf(exp_mag))
    {
        return false;
    }

    if (isnan(absolute_magnitude) || isinf(absolute_magnitude))
    {
        if (diff)
        {
            *diff = 1e29;
        }
        return true;
    }

    double d = absolute_magnitude - exp_mag;
    if (diff)
    {
        *diff = d;
    }
    return (fabs(d) >= threshold);
}

bool Star::correct_main_sequence_absmag(double threshold)
{
    // The explicit system supplement includes young stars outside the dwarf sequence.
    if (stellar_system.is_object()) return false;
    if (!is_main_sequence() || is_hot_ob_star())
    {
        return false;
    }

    double exp_mag = expected_main_sequence_absmag();
    if (exp_mag > 1e28 || isnan(exp_mag) || isinf(exp_mag))
    {
        return false;
    }

    // Check if the current magnitude is an outlier or unphysical
    bool is_invalid = (isnan(absolute_magnitude) || isinf(absolute_magnitude) || absolute_magnitude == 0 || absolute_magnitude > 50);
    double diff = is_invalid ? 1e29 : (absolute_magnitude - exp_mag);
    if (!is_invalid && fabs(diff) < threshold)
    {
        return false;
    }

    // Stellar companions share the system center's distance and astrometric status.
    const Star* distance_source = this;
    if (cenobj && cenobj->typeclass() == class_star)
    {
        distance_source = static_cast<const Star*>(cenobj);
    }
    double observed_distance = distance_source->distance;
    bool observed_distance_known = distance_source->distance_known;

    // Check if distance and apparent magnitude are available
    bool has_appmag = (!isnan(apparent_magnitude) && !isinf(apparent_magnitude) && apparent_magnitude > -30.0 && apparent_magnitude < 60.0 && apparent_magnitude != 0.0);
    bool has_dist = (!isnan(observed_distance) && !isinf(observed_distance) && observed_distance > 0);

    double m_obs = 1e29;
    if (has_appmag && has_dist)
    {
        double intrinsic_brightness = pow(magnbase, -apparent_magnitude) * pow(fmax(AU, observed_distance) / parsec / 10.0, 2);
        if (intrinsic_brightness > 0)
        {
            m_obs = -log(intrinsic_brightness) * invlogmagnbase;
        }
    }

    double old_absmag = absolute_magnitude;
    double old_distance = distance;

    // Case 1: Observed distance modulus matches the main-sequence expectation (within threshold).
    // This is the K2-106 pattern: distance and apparent magnitude are correct and yield the
    // expected main-sequence magnitude, but absolute_magnitude was set to a bogus fallback (e.g. 10).
    if (m_obs < 1e28 && fabs(m_obs - exp_mag) < threshold)
    {
        absolute_magnitude = m_obs;
        if (volumetric_mean_radius <= 0)
        {
            double msqi = get_mseqidx_from_sptyp(spectral_type);
            if (msqi >= mseqmin && msqi <= mseqmax)
            {
                volumetric_mean_radius = interpolate_mseq_rad(msqi) * solar_radius;
            }
            else
            {
                volumetric_mean_radius = estimate_radius(false);
            }
        }
        if (mass <= 0)
        {
            mass = estimate_mass();
        }
        return true;
    }

    // Case 2: Observed distance modulus is also an outlier, strongly brighter than main sequence.
    // This is the HD 7229 pattern: star has trigonometric parallax (known distance) and apparent magnitude,
    // which makes M_obs much brighter than a dwarf (e.g. G1V with M_V = 1.15).
    // The star is NOT main-sequence; it is an evolved star (giant/subgiant).
    // To remain consistent with observed values: keep M_obs and distance, but correct the luminosity class!
    // Never apply to verified dwarfs (sub-solar radius or mass, or M dwarfs).
    bool is_verified_dwarf = (volumetric_mean_radius > 0 && volumetric_mean_radius < 1.2 * solar_radius)
                          || (mass > 0 && mass < 0.6 * solar_mass)
                          || (spectral_type[0] == 'M')
                          || (spectral_type[0] == 'd' && spectral_type[1] == 'M');
    if (!is_verified_dwarf && m_obs < 1e28 && (exp_mag - m_obs >= threshold) && observed_distance_known)
    {
        absolute_magnitude = m_obs;

        // Determine evolved luminosity class based on M_obs
        const char* new_class = (m_obs <= 2.5) ? "III" : "IV";

        // Replace 'V' with the new luminosity class in spectral_type
        for (int i = 0; spectral_type[i]; i++)
        {
            if (spectral_type[i] == 'V')
            {
                bool prev_I = (i > 0 && spectral_type[i - 1] == 'I');
                bool next_I = (spectral_type[i + 1] == 'I');
                if (!prev_I && !next_I)
                {
                    char remainder[32] = {0};
                    strncpy(remainder, &spectral_type[i + 1], sizeof(remainder) - 1);
                    spectral_type[i] = '\0';
                    strncat(spectral_type, new_class, sizeof(spectral_type) - strlen(spectral_type) - 1);
                    strncat(spectral_type, remainder, sizeof(spectral_type) - strlen(spectral_type) - 1);
                    break;
                }
            }
        }

        // Re-estimate radius from Stefan-Boltzmann: L = 10^(-0.4 * (M_V - 4.83))
        double T = estimate_temperature();
        if (T > 0)
        {
            double lum = pow(magnbase, -(absolute_magnitude - 4.83));
            volumetric_mean_radius = sqrt(lum) * pow(sun_temp / T, 2.0) * solar_radius;
        }
        return true;
    }

    // Case 3: Distance was NOT measured trigonometrically (or invalid/unreliable).
    // The spectral classification is trusted, so assign the expected main-sequence magnitude,
    // and compute the photometric distance so that apparent magnitude matches the catalog.
    if (!observed_distance_known || !has_dist)
    {
        absolute_magnitude = exp_mag;
        if (has_appmag)
        {
            distance = distance_from_magnitudes(apparent_magnitude, absolute_magnitude);
            distance_known = true;
            update_location(simnow);
        }
        if (volumetric_mean_radius <= 0)
        {
            double msqi = get_mseqidx_from_sptyp(spectral_type);
            if (msqi >= mseqmin && msqi <= mseqmax)
            {
                volumetric_mean_radius = interpolate_mseq_rad(msqi) * solar_radius;
            }
            else
            {
                volumetric_mean_radius = estimate_radius(false);
            }
        }
        if (mass <= 0)
        {
            mass = estimate_mass();
        }
        return true;
    }

    // If distance IS known from parallax, and absolute_magnitude was corrupted (did not match m_obs),
    // recover m_obs so that apparent magnitude matches catalog:
    if (m_obs < 1e28 && fabs(absolute_magnitude - m_obs) >= threshold)
    {
        absolute_magnitude = m_obs;
        return true;
    }

    // Universal invariant guard:
    // No star should ever end up with a significantly brighter apparent magnitude than what's in the catalogs.
    if (has_appmag && has_dist)
    {
        double effective_appmag = absolute_magnitude + 5.0 * (log10(observed_distance / (parsec * 10.0)));
        if (effective_appmag < apparent_magnitude - 0.25)
        {
            absolute_magnitude = old_absmag;
            distance = old_distance;
            return false;
        }
    }

    return false;
}

int Star::audit_and_correct_main_sequence_stars(CelestialObject** cels, double threshold)
{
    if (!cels)
    {
        return 0;
    }

    int count = 0;
    for (int i = 0; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (s->is_main_sequence() && s->is_mseq_absmag_outlier(threshold))
            {
                if (s->correct_main_sequence_absmag(threshold))
                {
                    count++;
                    if (s->has_planets)
                    {
                        for (int j = 0; cels[j]; j++)
                        {
                            if (cels[j]->typeclass() == class_planet && cels[j]->cenobj == s)
                            {
                                Planet* p = (Planet*)cels[j];
                                p->classify();
                            }
                        }
                    }
                }
            }
        }
    }
    return count;
}

double Star::estimate_radius(bool sms)
{
    if (stellar_regime(this) == regime_degenerate)
    {
        return volumetric_mean_radius = degenerate_radius(mass);
    }
    if (!sms)
    {
        double msqi = -1.0;
        if (spectral_type[0])
        {
            msqi = get_mseqidx_from_sptyp(spectral_type);
        }
        if (msqi < mseqmin || msqi > mseqmax)
        {
            msqi = get_mseqidx_from_BV(BV_color);
        }
        if (msqi >= mseqmin && msqi <= mseqmax)
        {
            return volumetric_mean_radius = interpolate_mseq_rad(msqi) * solar_radius;
        }
    }

    double sun_absmag = (cels && cels[0] ? cels[0]->absolute_magnitude : 4.83);
    double T = estimate_temperature();
    if (!(temperature > 0) && std::isfinite(BV_color) && BV_color != 0)
    {
        T = temperature_from_BV(BV_color);
    }
    if (!(T > 0) || !std::isfinite(T))
    {
        T = 5800.0;
    }
    // 1. Calculate Luminosity relative to the Sun (L/L_sun)
    double logL = (sun_absmag - absolute_magnitude);
    double luminosity = std::pow(magnbase, logL);

    // 2. Calculate radius relative to the Sun (R/R_sun) using Stefan-Boltzmann then scale to meters
    double solar_radii = std::sqrt(luminosity) * std::pow(sun_temp / T, 2.0);
    double radius = solar_radii * solar_radius;
    if (!(radius > 0) || !std::isfinite(radius))
    {
        // Use the existing main-sequence mass table when photometry is unavailable.
        const double index = get_mseqidx_from_mass(mass / solar_mass);
        radius = mass > 0 && std::isfinite(mass) && index >= mseqmin && index <= mseqmax
            ? interpolate_mseq_rad(index) * solar_radius : solar_radius;
    }
    if (!(radius > 0) || !std::isfinite(radius))
    {
        radius = solar_radius;
    }
    return volumetric_mean_radius = radius;
}

// Priority: custom name > Bayer > Flamsteed > GJ > Gould > HD > HIP > other identifiers.
void Star::assign_identifier_name()
{
    if ((BayerGrkno >= 0 || FlamsteedNo > 0 || GouldNo > 0) && strlen(constellation))
        rename_from_Bayer_Flamsteed();

    if (!trim(name).size() && strlen(Gliese)) strcpy(name, Gliese);
    else if (!trim(name).size() && HD) strcpy(name, (std::string("HD")+std::to_string(HD)).c_str() );
    else if (!trim(name).size() && HIP) strcpy(name, (std::string("HIP")+std::to_string(HIP)).c_str() );
    else if (!trim(name).size() && SAO) strcpy(name, (std::string("SAO")+std::to_string(SAO)).c_str() );
    else if (!trim(name).size() && Bonn_survey_sequential)
    {
        name[0] = Bonn_survey[0];
        name[1] = Bonn_survey[1];
        name[2] = Bonn_survey_sign;
        strcpy(&name[3], (std::to_string(abs(Bonn_survey_declination)) + std::string(" ")
            + std::to_string(Bonn_survey_sequential) ).c_str() );
    }
    else if (!trim(name).size() && SB9) strcpy(name, (std::string("SB9-")+std::to_string(SB9)).c_str() );
}

void Star::gotta_be_named_something()
{
    if (!trim(name).size())
    {
        if (multisys && multisys->get_member('A') != this) return;              // update this name later, based on the main star's name

        // Try this star's own identifiers before ever borrowing "center's name + letter": a
        // companion with its own Bayer/Flamsteed/Gould/GJ/HD/HIP identity (Zeta 2 Reticuli, an
        // adjacent HD number) keeps it instead of becoming "<primary> B".
        assign_identifier_name();

        if (!trim(name).size() && orbit && orbit->center && strlen(orbit->center->name))
        {
            int n = strlen(orbit->center->name);
            if (orbit->center->name[n-1] >= 'A' && orbit->center->name[n-2] == ' ')
            {
                strcpy(name, orbit->center->name);
                name[n]++;
            }
            else
            {
                strcpy(name, ( std::string(orbit->center->name) + std::string(" B") ).c_str());
            }
        }

        if (!trim(name).size()) std::cerr << "Failed to name star @ RA: " << RA_as_hms(0) << " decl " << Decl_as_degms() << " magnitude " << apparent_magnitude
            << " distance " << (distance/light_year) << std::endl;
    }

    // Only the primary hands its base name down to companions. Without this guard, calling
    // gotta_be_named_something() directly on a non-A member (BSC does, for every row it loads,
    // including Gam1Vel/HD68243 itself) walks this same loop, finds itself sitting at its own
    // component letter, and -- now that its own name is already computed -- "matches the primary's
    // base name" trivially against itself, self-appending the letter (e.g. "Gamma 1 Velorum B").
    if (multisys && multisys->get_member('A') == this)
    {
        Star* companion;
        for (char c = 'B'; (companion = multisys->get_member(c)); c++)
        {
            if(companion == nullptr)
                break;
            if (companion->has_custom_name) continue;

            // Only fall back to "<primary base> <letter>" when the companion has no name of its
            // own, or its own name is identical to the primary's (54 Psc A/B, sharing one
            // Flamsteed number) -- never when it already reads differently (Zeta 1/2 Reticuli).
            companion->assign_identifier_name();
            std::string base = lop_component(name);
            if (!trim(companion->name).size() || !strcmp(trim(companion->name).c_str(), base.c_str()))
                strcpy(companion->name, (base + std::string(" ") + std::string(1, c)).c_str() );
            companion->namelen = strlen(companion->name);
        }
    }

    if (!user_edited || user_added) origname = name;
    if (orbit && orbit->center) origcenname = orbit->center->name;
}

json Star::to_json()
{
    json towrite = CelestialObject::to_json();
    if (is_white_dwarf())
    {
        Color linear;
        std::string source;
        const bool available = resolve_wd_color(linear, &source);
        towrite["wd_color_profile"] =
        {
            {"available", available}, {"source", source},
            {"temperature_source", wd_temperature_source}, {"model_revision", 2},
            {"method", !available ? "legacy_BV" : source.find("approx") != std::string::npos
                ? "atmosphere_approximation" : source.find("observed_spectrum") != std::string::npos
                ? "published_spectrum" : "published_model"}
        };
        if (available) towrite["wd_color_profile"]["linear_rgb"] = {linear.red, linear.green, linear.blue};
    }

    towrite["proper_motion_RA"] = proper_motion_RA*fiftyseven*oneyear;
    towrite["proper_motion_decl"] = proper_motion_decl*fiftyseven*oneyear;
    towrite["radial_velocity"] = radial_velocity;
    towrite["apparent_magnitude"] = apparent_magnitude;
    towrite["parallax"] = parallax*fiftyseven*3600*1000;
    if (stellar_abundances.is_object() && !stellar_abundances.empty()) towrite["stellar_abundances"] = stellar_abundances;
    if (stellar_rotation.is_object()) towrite["stellar_rotation"] = stellar_rotation;
    if (stellar_system.is_object()) towrite["stellar_system"] = stellar_system;
    if (catalog_frame.is_object()) towrite["catalog_frame"] = catalog_frame;
    if (has_fictional_motion && !fictional_origin.is_object())
    {
        towrite["catalog_motion"] = {{"epoch_position_m", fictional_epoch_position.to_json()},
            {"velocity_m_s", fictional_space_velocity.to_json()}};
    }
    if (fictional_origin.is_object())
    {
        towrite["fictional_origin"] = fictional_origin;
        towrite["is_faved"] = is_faved;
    }
    towrite["known_poles"] = known_poles;
    towrite["lock_equatorial_plane"] = lock_equatorial_plane;
    towrite["lock_system_plane"] = lock_system_plane;
    towrite["rot_axis_known"] = rot_axis_known;
    towrite["rot_heliocen_incl"] = rot_heliocen_incl * fiftyseven;
    towrite["rot_heliocen_node"] = rot_heliocen_node * fiftyseven;
    if (std::isfinite(metallicity))
    {
        towrite["stellar_metallicity"] =
        {
            {"value_dex", metallicity}, {"ratio", metallicity_ratio}, {"source", metallicity_source},
            {"error_upper_dex", std::isfinite(metallicity_error_upper) ? json(metallicity_error_upper) : json(nullptr)},
            {"error_lower_dex", std::isfinite(metallicity_error_lower) ? json(metallicity_error_lower) : json(nullptr)}
        };
    }
    towrite["spectral_type"] = spectral_type;
    towrite["Bayer"] = Bayer;
    towrite["Flamsteed"] = Flamsteed;
    towrite["Gliese"] = Gliese;
    towrite["BayerGrkno"] = BayerGrkno;
    towrite["FlamsteedNo"] = FlamsteedNo;
    towrite["constellation"] = constellation;
    towrite["HR"] = HR;
    towrite["HD"] = HD;
    towrite["HIP"] = HIP;
    towrite["SAO"] = SAO;
    towrite["SB9"] = SB9;
    towrite["CCDM"] = CCDM;
    towrite["WD"] = WD;
    if (!catalog_aliases.empty()) towrite["catalog_aliases"] = catalog_aliases;
    towrite["gaia_wd_name"] = gaia_wd_name;
    towrite["gaia_source_id"] = gaia_source_id;
    towrite["gaia_g_magnitude"] = gaia_g_magnitude;
    towrite["apparent_magnitude_estimated"] = apparent_magnitude_estimated;
    towrite["wd_color_valid"] = wd_color_valid;
    towrite["wd_color_revision"] = wd_color_revision;
    towrite["wd_temperature_source"] = wd_temperature_source;
    if (wd_color_valid)
    {
        towrite["wd_color_rgb"] = {wd_color_red, wd_color_green, wd_color_blue};
        towrite["wd_color_source"] = wd_color_source;
    }
    towrite["Bonn_survey"] = Bonn_survey;
    towrite["Bonn_survey_sign"] = std::string(1, Bonn_survey_sign);
    towrite["Bonn_survey_declination"] = Bonn_survey_declination;
    towrite["Bonn_survey_sequential"] = Bonn_survey_sequential;
    towrite["is_orbit_multiple"] = is_orbit_multiple;

    if (local_name.size()) towrite["local_name"] = local_name;

    // The planet tallies. has_hz_planets goes with has_planets whether or not anything asks for
    // it: the two are counted in the same breath everywhere they are counted, and a file that
    // stated one and left the other to be worked out would come back internally inconsistent --
    // a star with planets and, suddenly, none of them in the habitable zone.
    if (has_planets) towrite["has_planets"] = has_planets;
    if (has_hz_planets) towrite["has_hz_planets"] = has_hz_planets;

    return towrite;
}

bool Star::from_json(const json& j)
{
    CelestialObject::from_json(j);
    stellar_abundances = j.contains("stellar_abundances") && j["stellar_abundances"].is_object()
        ? j["stellar_abundances"] : json(nullptr);
    stellar_rotation = j.contains("stellar_rotation") && j["stellar_rotation"].is_object()
        ? j["stellar_rotation"] : json(nullptr);
    fictional_origin = j.value("fictional_origin", json(nullptr));
    const bool fictional_marker = j.value("fic", 0) == 1;
    if (fictional_marker && !fictional_origin.is_object()) fictional_origin = json::object();
    has_fictional_motion = false;
    if (j.contains("catalog_motion") || (fictional_origin.is_object() && fictional_origin.contains("motion")))
    {
        const auto& motion = j.contains("catalog_motion") ? j.at("catalog_motion") : fictional_origin.at("motion");
        const auto read_point = [&](const char* key, Point& point)
        {
            if (!motion.is_object() || !motion.contains(key) || !motion.at(key).is_object()) return false;
            const auto& value = motion.at(key);
            for (const char* axis : {"x", "y", "z"})
                if (!value.contains(axis) || !value.at(axis).is_number()
                    || !std::isfinite(value.at(axis).get<double>())) return false;
            point.from_json(value);
            return std::isfinite(point.squared_magnitude());
        };
        has_fictional_motion = read_point("epoch_position_m", fictional_epoch_position)
            && read_point("velocity_m_s", fictional_space_velocity);
    }
    if (fictional_origin.is_object()) is_faved = j.value("is_faved", false);
    stellar_system = j.value("stellar_system", json(nullptr));
    catalog_frame = j.value("catalog_frame", json(nullptr));
    if (stellar_system.is_object()) has_custom_name = true;
    if (stellar_system.is_object() && !stellar_system.contains("slot"))
    {
        const std::string designation = j.value("!origname", std::string(name));
        const char component = orbit && !designation.empty() && designation.back() >= 'A' && designation.back() <= 'Z'
            ? designation.back() : 'A';
        stellar_system["slot"] = int(component);
    }
    if (fictional_marker && !fictional_origin.contains("id"))
    {
        const std::string designation = j.value("!origname", std::string(name));
        const std::string id = stellar_system.is_object() && stellar_system.contains("system") && stellar_system.contains("slot")
            ? stellar_system.at("system").get<std::string>() + ":" + char(stellar_system.at("slot").get<int>())
            : "catalog:" + designation;
        fictional_origin["id"] = id;
        fictional_origin["designation"] = designation;
    }
    // Explicit flags keep a saved display fallback from becoming a measured pole.
    if (fictional_marker) known_poles = rot_axis_known = false;
    try { j.at("known_poles").get_to(known_poles); } catch (...) { ; }
    try { j.at("lock_equatorial_plane").get_to(lock_equatorial_plane); } catch (...) { ; }
    try { j.at("lock_system_plane").get_to(lock_system_plane); } catch (...) { ; }
    try { j.at("rot_axis_known").get_to(rot_axis_known); } catch (...) { ; }
    try { rot_heliocen_incl = j.at("rot_heliocen_incl").get<double>() * fiftyseventh; } catch (...) { ; }
    try { rot_heliocen_node = j.at("rot_heliocen_node").get<double>() * fiftyseventh; } catch (...) { ; }
    metallicity = metallicity_error_upper = metallicity_error_lower = NAN;
    metallicity_ratio.clear();
    metallicity_source.clear();
    if (j.contains("stellar_metallicity") && j["stellar_metallicity"].is_object())
    {
        const auto& measurement = j["stellar_metallicity"];
        const auto numeric = [&](const char* key) -> double
        {
            auto it = measurement.find(key);
            if (it == measurement.end() || !it->is_number()) return NAN;
            const double value = it->get<double>();
            return std::isfinite(value) ? value : NAN;
        };
        metallicity = numeric("value_dex");
        if (std::isfinite(metallicity))
        {
            metallicity_error_upper = numeric("error_upper_dex");
            metallicity_error_lower = numeric("error_lower_dex");
            if (measurement.contains("ratio") && measurement["ratio"].is_string())
                metallicity_ratio = measurement["ratio"].get<std::string>();
            if (measurement.contains("source") && measurement["source"].is_string())
                metallicity_source = measurement["source"].get<std::string>();
        }
    }
    if (fictional_origin.is_object()) metallicity_error_upper = metallicity_error_lower = NAN;
    try { j.at("proper_motion_RA").get_to(proper_motion_RA); proper_motion_RA *= fiftyseventh/oneyear; } catch (...) { ; }
    try { j.at("proper_motion_decl").get_to(proper_motion_decl); proper_motion_decl *= fiftyseventh/oneyear; } catch (...) { ; }
    try { j.at("radial_velocity").get_to(radial_velocity); } catch (...) { ; }
    try { j.at("apparent_magnitude").get_to(apparent_magnitude); } catch (...) { ; }
    try { j.at("has_planets").get_to(has_planets); } catch (...) { ; }
    try { j.at("has_hz_planets").get_to(has_hz_planets); } catch (...) { ; }
    try { j.at("local_name").get_to(local_name); } catch (...) { ; }
    try { j.at("parallax").get_to(parallax); parallax *= fiftyseventh / 3600000; } catch (...) { ; }
    try
    {
        std::string str;
        j.at("spectral_type").get_to(str);
        snprintf(spectral_type, sizeof(spectral_type), "%s", str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Bayer").get_to(str);
        snprintf(Bayer, sizeof(Bayer), "%s", str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Flamsteed").get_to(str);
        snprintf(Flamsteed, sizeof(Flamsteed), "%s", str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Gliese").get_to(str);
        snprintf(Gliese, sizeof(Gliese), "%s", str.c_str());
    } catch (...) { ; }
    try { j.at("HR").get_to(HR); } catch (...) { ; }
    try { j.at("HD").get_to(HD); } catch (...) { ; }
    try { j.at("HIP").get_to(HIP); } catch (...) { ; }
    try { j.at("SAO").get_to(SAO); } catch (...) { ; }
    try { j.at("SB9").get_to(SB9); } catch (...) { ; }
    try { j.at("CCDM").get_to(CCDM); } catch (...) { ; }
    try
    {
        j.at("WD").get_to(WD);
    }
    catch (...)
    {
        ;
    }
    if (j.contains("gaia_wd_name") && j["gaia_wd_name"].is_string())
    {
        j["gaia_wd_name"].get_to(gaia_wd_name);
    }
    if (j.contains("catalog_aliases") && j["catalog_aliases"].is_array())
    {
        for (const auto &alias : j.at("catalog_aliases")) add_catalog_alias(alias.get<std::string>());
    }
    if (j.contains("gaia_source_id") && j["gaia_source_id"].is_number_unsigned())
    {
        j["gaia_source_id"].get_to(gaia_source_id);
    }
    if (j.contains("gaia_g_magnitude") && j["gaia_g_magnitude"].is_number())
    {
        j["gaia_g_magnitude"].get_to(gaia_g_magnitude);
    }
    if (j.contains("apparent_magnitude_estimated") && j["apparent_magnitude_estimated"].is_boolean())
    {
        j["apparent_magnitude_estimated"].get_to(apparent_magnitude_estimated);
    }
    if (j.contains("wd_color_valid") && j["wd_color_valid"].is_boolean())
    {
        j["wd_color_valid"].get_to(wd_color_valid);
    }
    wd_color_revision = 0;
    wd_temperature_source = "unspecified";
    if (j.contains("wd_color_revision") && j["wd_color_revision"].is_number_unsigned())
    {
        j["wd_color_revision"].get_to(wd_color_revision);
    }
    if (j.contains("wd_temperature_source") && j["wd_temperature_source"].is_string())
    {
        j["wd_temperature_source"].get_to(wd_temperature_source);
    }
    if (wd_color_valid && j.contains("wd_color_rgb") && j["wd_color_rgb"].is_array() && j["wd_color_rgb"].size() == 3)
    {
        wd_color_red = j["wd_color_rgb"][0].get<unsigned char>();
        wd_color_green = j["wd_color_rgb"][1].get<unsigned char>();
        wd_color_blue = j["wd_color_rgb"][2].get<unsigned char>();
        if (j.contains("wd_color_source") && j["wd_color_source"].is_string())
        {
            j["wd_color_source"].get_to(wd_color_source);
        }
    }
    else
    {
        wd_color_valid = false;
    }
    try
    {
        std::string str;
        j.at("Bonn_survey").get_to(str);
        snprintf(Bonn_survey, sizeof(Bonn_survey), "%s", str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Bonn_survey_sign").get_to(str);
        Bonn_survey_sign = str.c_str()[0];
    } catch (...) { ; }
    try { j.at("Bonn_survey_declination").get_to(Bonn_survey_declination); } catch (...) { ; }
    try { j.at("Bonn_survey_sequential").get_to(Bonn_survey_sequential); } catch (...) { ; }
    try { j.at("is_orbit_multiple").get_to(is_orbit_multiple); } catch (...) { ; }
    try
    {
        std::string str;
        j.at("constellation").get_to(str);
        if (str.size() >= sizeof(constellation)) str = str.substr(0, sizeof(constellation)-1);
        strcpy(constellation, str.c_str());
    } catch (...) { ; }
    try { j.at("BayerGrkno").get_to(BayerGrkno); } catch (...) { ; }
    try { j.at("FlamsteedNo").get_to(FlamsteedNo); } catch (...) { ; }
    if (fictional_marker)
    {
        if (metallicity_source.empty() && std::isfinite(metallicity)) metallicity_source = "fictional population model v1";
        if (stellar_rotation.is_object() && !stellar_rotation.contains("source")) stellar_rotation["source"] = "fictional population model v1";
        if (!has_fictional_motion)
        {
            if (j.contains("space_velocity") && j["space_velocity"].is_object())
            {
                Point velocity;
                velocity.from_json(j["space_velocity"]);
                set_fictional_motion(velocity);
            }
            else
            {
                const Point radial(-sin(right_ascension)*cos(declination), sin(declination), cos(right_ascension)*cos(declination));
                const Point east(-cos(right_ascension), 0, -sin(right_ascension));
                const Point north(sin(right_ascension)*sin(declination), cos(declination), -cos(right_ascension)*sin(declination));
                set_fictional_motion(radial*radial_velocity + east*(proper_motion_RA*distance*cos(declination))
                    + north*(proper_motion_decl*distance));
            }
        }
        if (is_white_dwarf() && wd_color_valid)
        {
            std::string template_source = wd_color_source;
            const std::string suffix = "_fictional_template_approx";
            if (template_source.size() >= suffix.size()
                && template_source.compare(template_source.size()-suffix.size(), suffix.size(), suffix) == 0)
                template_source.resize(template_source.size()-suffix.size());
            fictional_origin["white_dwarf_atmosphere"] =
            {
                {"srgb", (uint32_t(wd_color_red) << 16) | (uint32_t(wd_color_green) << 8) | wd_color_blue},
                {"template_temperature_k", temperature}, {"template_spectral_type", spectral_type},
                {"template_source", template_source}
            };
        }
    }
    restore_fictional_motion(*this);
    return true;
}

void Star::make_companion_of(Star *A, char comp)
{
    if (stellar_system.is_object() && A->stellar_system.is_object() &&
        stellar_system.at("system") != A->stellar_system.at("system")) return;
    cenobj = A->cenobj ? A->cenobj : A;
    right_ascension = A->right_ascension;
    declination = A->declination;
    parallax = A->parallax;
    distance = A->distance;
    location = A->location;
    if (!has_custom_name)
    {
        // Only borrow "<primary base> <letter>" when this star has no identity of its own, or that
        // identity is identical to the primary's -- never when it already reads differently.
        assign_identifier_name();
        std::string base = lop_component(A->name);
        if (!trim(name).size() || !strcmp(trim(name).c_str(), base.c_str()))
            strcpy(name, (base + std::string(" ") + std::string(1, comp)).c_str());
    }
    CCDM = A->CCDM;
    proper_motion_RA = A->proper_motion_RA;
    proper_motion_decl = A->proper_motion_decl;
    visible_area = A->visible_area;
    type = star;

    // A->set_component('A', A);
    set_component(comp, A);
}

double Star::estimate_mass()
{
    double msqi = -1.0;
    if (spectral_type[0])
    {
        msqi = get_mseqidx_from_sptyp(spectral_type);
    }
    if (msqi < mseqmin || msqi > mseqmax)
    {
        msqi = get_mseqidx_from_BV(BV_color);
    }
    if (msqi >= mseqmin && msqi <= mseqmax)
    {
        return mass = interpolate_mseq_mass(msqi) * solar_mass;
    }

    double sun_absmag = (cels[0] ? cels[0]->absolute_magnitude : 4.83);
    double T = estimate_temperature();
    if (T <= 0)
    {
        T = 5800.0;
    }
    double logL = (sun_absmag - absolute_magnitude);
    double luminosity = std::pow(magnbase, logL);
    double radius = (volumetric_mean_radius ? volumetric_mean_radius : estimate_radius()) / solar_radius;

    // Approximate Surface Gravity (log g) based on empirical stellar trends
    // Hotter and more luminous stars have different surface profiles.
    // This polynomial roughly mimics evolutionary paths on the HR diagram.
    double logT = std::log10(T);
    double est_log_G;

    if (luminosity > 10000.0)
    {
        // Supergiants / Bright Giants (Class I & II)
        est_log_G = 5.0 - 1.1 * (logT - 3.5);
    }
    else if (luminosity > 50.0)
    {
        // Regular Giants (Class III)
        est_log_G = 3.2 - 1.5 * (logT - 3.6);
    }
    else
    {
        // Main Sequence / Subgiants (Class V & IV)
        est_log_G = 4.4 - 0.6 * (logT - 3.7);
    }

    // Clamp log g to physically realistic boundaries for safety
    if (est_log_G < 0.5) est_log_G = 0.5;
    if (est_log_G > 5.0) est_log_G = 5.0;

    // Convert log10(g) back to raw gravity value in cgs (cm/s^2)
    double gravity = std::pow(10.0, est_log_G);

    // Calculate Mass via g = GM / R^2
    // Expressed cleanly using solar constants:
    // (g / g_sun) * (R / R_sun)^2 = M / M_sun
    double solargravity = 27400.0; // Sun's surface gravity is ~27,400 cm/s^2
    mass = (gravity / solargravity) * std::pow(radius, 2.0) * solar_mass;

    return mass;
}


void rename_all_from_Bayer_Flamsteed()
{
    int i;
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            s->rename_from_Bayer_Flamsteed();           // has no effect if not a Bayer-Flamsteed star.
        }
    }
}

void Gliese_doubles_fix()
{
    int i, j, m, n;
    char name1[29], name2[29];

    // Fix for members B of multiple systems getting left behind when stellar positions are updated from B1950 to later epochs.
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            if (cels[i]->type != star) continue;
            Star* s1 = (Star*)cels[i];
            if (!strlen(s1->Gliese)) continue;

            strcpy(name1, s1->Gliese);
            n = strlen(name1);
            if (name1[n-1] == 'A' && name1[n-2] == ' ')
                name1[n-2] = 0;
            else continue;

            n = strlen(name1);

            for (j=std::max(0, i-100); cels[j] && j<i+100; j++)
            {
                if (j==i) continue;
                if (cels[j]->type != star) continue;
                Star* s2 = (Star*)cels[j];
                m = strlen(s2->Gliese);
                if (!m) continue;
                if (m < n) continue;
                if (s2->Gliese[n] != ' ') continue;

                strcpy(name2, s2->Gliese);
                name2[n] = 0;
                if (!strcmp(name1, name2) || lop_component(s1->Gliese) == lop_component(s2->Gliese))
                {
                    s2->right_ascension = s1->right_ascension;
                    s2->declination = s1->declination;
                    s2->distance = s1->distance;
                    s2->proper_motion_RA = s1->proper_motion_RA;
                    s2->proper_motion_decl = s1->proper_motion_decl;
                    s2->radial_velocity = s1->radial_velocity;

                    s2->update_location(J2000_TIME_T);
                }
            }
        }
    }
}

void StarMulti::add_member(Star *s, char comp)
{
    comp &= 0x5f;
    if (comp < 'A' || comp > 'Z') throw 0xbadc0de;

    int cidx = comp - 'A';
    if (!members || cidx >= allocated)
    {
        char toalloc = std::max(2, cidx+1);
        Star** new_members = new Star*[toalloc];
        memset(new_members, 0, sizeof(Star*)*toalloc);
        if (members)
        {
            int i;
            for (i=0; i<allocated; i++) new_members[i] = members[i];
            delete[] members;
        }
        members = new_members;
        allocated = toalloc;
    }
    // Relocating a member must preserve any star occupying its new component slot.
    const char previous = is_member(s);
    if (previous && previous - 'A' != cidx)
    {
        members[previous - 'A'] = members[cidx];
    }
    members[cidx] = s;
    s->multisys = this;
}

Star *StarMulti::get_member(char comp)
{
    comp &= 0x5f;
    if (comp < 'A' || comp > 'Z') throw 0xbadc0de;
    int i = comp - 'A';
    if (i >= allocated) return nullptr;
    return members[i];
}

char StarMulti::is_member(Star *s)
{
    if (!allocated) return 0;

    int i;
    for (i=0; i<allocated; i++)
        if (members[i] == s) return 'A' + i;
    return 0;
}

int alienorum::StarMulti::num_members()
{
    int i, result = 0;
    for (i=0; i<allocated; i++) if (members[i]) result++;
    return result;
}

char alienorum::StarMulti::next_available()
{
    int i;
    if (!allocated || !members) return 0;
    for (i=0; i<allocated; i++) if (!members[i]) break;
    return 'A' + i;
}

void StarMulti::unlink()
{
    if (!allocated) return;
    int i;
    for (i=0; i<allocated; i++)
    {
        if (members[i])
        {
            members[i]->multisys = nullptr;
            members[i] = nullptr;
        }
    }
}

#define _debug_merge 0
void alienorum::StarMulti::merge(StarMulti *other)
{
    int toalloc = std::max(2, allocated + other->allocated);
    Star** new_members = new Star*[toalloc];
    memset(new_members, 0, sizeof(Star*)*toalloc);

    #if _debug_merge
    std::cout << "Merge StarMulti objects: " << std::endl;
    #endif
    int i, j=0;
    if (members)
    {
        for (i=0; i<allocated; i++) if (members[i])
        {
            #if _debug_merge
            std::cout << "(1) -> " << members[i]->name << std::endl;
            #endif
            new_members[j++] = members[i];
        }
        delete[] members;
    }
    if (other->members)
    {
        for (i=0; i<other->allocated; i++) if (other->members[i])
        {
            #if _debug_merge
            std::cout << "(2) -> " << other->members[i]->name << std::endl;
            #endif
            new_members[j++] = other->members[i];
        }
        delete[] other->members;
        other->members = nullptr;
    }
    members = new_members;
    allocated = toalloc;
    for (i=0; i<allocated; i++)
        if (members[i])
            members[i]->multisys = this;

    delete other;
    #if _debug_merge
    std::cout << std::endl << std::flush;
    #endif
}
