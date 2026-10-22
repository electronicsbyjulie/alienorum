#include "galaxy.h"
#include <string.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

using namespace alienorum;

Galaxy::Galaxy()
{
    _class = class_galaxy;
    type = galaxy;
    memset(morph_type, 0, sizeof(morph_type));
}

json Galaxy::to_json()
{
    json towrite = CelestialObject::to_json();
    towrite["label_radius_fraction"] = label_radius_fraction;
    towrite["reference_galaxy"] = reference_galaxy;
    towrite["internal_texture"] = internal_texture;
    towrite["band_catalog"] = band_catalog;

    if (T_known) towrite["morphological_T"] = morphological_T;
    if (angular_diameter) towrite["angular_diameter"] = angular_diameter;
    if (axis_ratio != 1) towrite["axis_ratio"] = axis_ratio;
    if (position_angle_known) towrite["position_angle"] = position_angle;
    if (inclination) towrite["inclination"] = inclination;
    if (apparent_magnitude) towrite["apparent_magnitude"] = apparent_magnitude;
    if (radial_velocity) towrite["radial_velocity"] = radial_velocity;
    if (PGC) towrite["PGC"] = PGC;
    if (strlen(morph_type)) towrite["morph_type"] = morph_type;

    return towrite;
}

bool Galaxy::from_json(json j)
{
    CelestialObject::from_json(j);
    label_radius_fraction = j.value("label_radius_fraction", 0.6);
    reference_galaxy = j.value("reference_galaxy", false);
    internal_texture = j.value("internal_texture", std::string());
    band_catalog = j.value("band_catalog", std::string());

    try { j.at("morphological_T").get_to(morphological_T); T_known = true; } catch (...) { ; }
    try { j.at("angular_diameter").get_to(angular_diameter); } catch (...) { ; }
    try { j.at("axis_ratio").get_to(axis_ratio); } catch (...) { ; }
    try { j.at("position_angle").get_to(position_angle); position_angle_known = true; } catch (...) { ; }
    try { j.at("inclination").get_to(inclination); } catch (...) { ; }
    try { j.at("apparent_magnitude").get_to(apparent_magnitude); } catch (...) { ; }
    try { j.at("radial_velocity").get_to(radial_velocity); } catch (...) { ; }
    try { j.at("PGC").get_to(PGC); } catch (...) { ; }
    try
    {
        std::string s;
        j.at("morph_type").get_to(s);
        snprintf(morph_type, sizeof(morph_type), "%s", s.c_str());
    } catch (...) { ; }

    apply_catalog_properties(false);
    return true;
}

bool Galaxy::is_spheroidal_or_elliptical() const
{
    if (T_known && morphological_T < 0)
    {
        return true;
    }
    if (morph_type[0] != '\0')
    {
        if (strstr(morph_type, "Sph") != nullptr || strstr(morph_type, "sph") != nullptr ||
            strstr(morph_type, "dE") != nullptr || strstr(morph_type, "cD") != nullptr ||
            (morph_type[0] == 'E' && (morph_type[1] == '\0' || (morph_type[1] >= '0' && morph_type[1] <= '9'))))
        {
            return true;
        }
    }
    if (strstr(name, "dSph") != nullptr || strstr(name, "Sph") != nullptr)
    {
        return true;
    }
    return false;
}

int alienorum::GalaxyBand::load_dat_file(std::string fname)
{
    FILE *fp = fopen(fname.c_str(), "r");
    if (!fp) return 0;

    int num_read = 0;
    char buffer[256], northsouth = 0;
    double ra, decl;
    while (fgets(buffer, 255, fp))
    {
        if (buffer[0] == '#') continue;
        else if (buffer[0] == 'N' || buffer[0] == 'S') northsouth = buffer[0];
        else
        {
            // The real export (boundary_roads.dat) is whitespace-separated; the comma this used
            // to insist on never matched a single line of it, so every coordinate silently fell
            // through and the band stayed permanently empty. Normalize any comma to a space first
            // so a comma-delimited line -- like the ones the unit test still uses -- still works.
            for (char *c = buffer; *c; c++) if (*c == ',') *c = ' ';
            if (sscanf(buffer, "%lf %lf", &ra, &decl) == 2)
            {
                if (northsouth == 'N')
                {
                    road1_gra.push_back(ra);
                    road1_gdecl.push_back(decl);
                    road1_dist.push_back(0);
                    num_read++;
                }
                else if (northsouth == 'S')
                {
                    road2_gra.push_back(ra);
                    road2_gdecl.push_back(decl);
                    road2_dist.push_back(0);
                    num_read++;
                }
            }
        }
    }

    fclose(fp);
    return num_read;
}

int alienorum::GalaxyBand::create_fictitious()
{
    return 0;
}

void Galaxy::apply_catalog_properties(bool geometry)
{
    using Catalog = std::map<std::string, json>;
    static std::map<std::string, Catalog> cache;
    static std::mutex cache_mutex;
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto path = std::filesystem::absolute("catalogs/galaxy_properties.json");
    auto found = cache.find(path.string());
    if (found == cache.end())
    {
        Catalog entries;
        std::ifstream input(path);
        if (input)
        {
            const json records = json::parse(input, nullptr, false);
            if (records.is_array())
            {
                for (const auto& record : records)
                {
                    for (const auto& alias : record.value("aliases", std::vector<std::string>{})) entries[alias] = record;
                }
            }
        }
        found = cache.emplace(path.string(), std::move(entries)).first;
    }
    const auto entry = found->second.find(name);
    if (entry != found->second.end())
    {
        const auto& record = entry->second;
        label_radius_fraction = record.value("label_radius_fraction", label_radius_fraction);
        reference_galaxy = record.value("reference_galaxy", reference_galaxy);
        internal_texture = record.value("internal_texture", internal_texture);
        band_catalog = record.value("band_catalog", band_catalog);
        if (geometry)
        {
            if (record.contains("inclination_degrees")) inclination = record.at("inclination_degrees").get<double>() * fiftyseventh;
            if (record.contains("position_angle_degrees"))
            {
                position_angle = record.at("position_angle_degrees").get<double>() * fiftyseventh;
                position_angle_known = true;
            }
            axis_ratio = record.value("axis_ratio", axis_ratio);
            if (record.contains("distance_mpc"))
            {
                const double mpc = record.at("distance_mpc").get<double>();
                distance = mpc * 1e6 * parsec;
                location.galactic_center = Point::from_ra_dec(right_ascension, declination, mpc * 3.26156);
                location.system_center = center;
                location.local_position = center;
                distance_known = true;
            }
            if (record.contains("radius_kpc") && distance > 0)
            {
                volumetric_mean_radius = record.at("radius_kpc").get<double>() * 1000 * parsec;
                angular_diameter = 2 * volumetric_mean_radius / distance;
            }
        }
    }
    if (!band_catalog.empty()) band.load_dat_file(band_catalog);
}
