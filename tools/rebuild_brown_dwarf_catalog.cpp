#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_set>
#include "../src/classes/cat.h"
#include "../src/classes/browncatalog.h"
#include "../src/loaders.h"

using namespace alienorum;

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--bake-orbits")
    {
        cels = new CelestialObject*[MAX_CELOBJS]();
        Star::load_main_seq_dat();
        read_cons_lines();
        CatalogReader reader;
        const int count = reader.read_condensed_star_cat();
        if (count <= 0) return 1;
        const int orbits = reader.read_star_orbits_dat(cels);
        if (!reader.bake_condensed_star_orbits()) return 1;
        const int a = find_object("Luhman 16 A", true, 9e29, 0);
        const int b = find_object("Luhman 16 B", true, 9e29, 0);
        if (a < 0 || b < 0 || !cels[b]->orbit || cels[b]->orbit->center != cels[a]) return 1;
        std::cout << "Baked " << orbits << " orbit records for " << count << " catalog objects.\n";
        return 0;
    }
    if (argc != 3)
    {
        std::cerr << "Usage: rebuild_brown_dwarf_catalog INPUT OUTPUT\n";
        return 2;
    }
    if (std::string(argv[1]) == argv[2]) return 2;
    std::unordered_set<std::string> identities;
    for (const char* path : {"catalogs/brown_dwarfs_20pc.json", "catalogs/ultracool_dwarfs.json"})
    {
        std::ifstream source(path);
        if (!source) continue;
        const json supplement = json::parse(source);
        for (const auto& record : supplement.at("records"))
            for (const auto& alias : record.at("aliases")) identities.insert(stellar_alias_key(alias.get<std::string>()));
    }
    cels = new CelestialObject*[MAX_CELOBJS]();
    auto anchor = new Star();
    snprintf(anchor->name, sizeof(anchor->name), "Catalog rebuild anchor");
    append_cel(anchor);
    std::map<size_t, Star*> existing;
    std::map<Star*, std::set<std::string>> original_aliases;
    std::unordered_set<std::string> ids;
    std::ifstream input(argv[1]);
    if (!input) return 2;
    std::string line;
    size_t row = 0;
    const auto field = [](const std::string& text, size_t first, size_t length)
    {
        return first < text.size() ? trim(text.substr(first, length)) : std::string();
    };
    while (std::getline(input, line))
    {
        ++row;
        if (line.empty() || line[0] == '#' || line.size() < 274) continue;
        ids.insert(field(line, 0, 14));
        const std::string name = field(line, 15, 39), gliese = field(line, 153, 15);
        const double mass = atof(field(line, 244, 11).c_str());
        const double temperature = atof(field(line, 268, 6).c_str());
        std::vector<std::string> aliases;
        const auto alias_start = line.rfind(" ALS1 ");
        if (alias_start != std::string::npos)
        {
            std::istringstream values(line.substr(alias_start+6));
            std::string alias;
            while (values >> std::quoted(alias)) aliases.push_back(alias);
        }
        bool candidate = identities.count(stellar_alias_key(name)) || identities.count(stellar_alias_key(gliese))
            || (mass > 0 && mass < .075) || (temperature > 0 && temperature < 2300);
        for (const auto& alias : aliases) candidate |= identities.count(stellar_alias_key(alias)) != 0;
        if (!candidate) continue;
        auto star = std::make_unique<Star>();
        snprintf(star->name, sizeof(star->name), "%s", name.c_str());
        snprintf(star->Gliese, sizeof(star->Gliese), "%s", gliese.c_str());
        snprintf(star->spectral_type, sizeof(star->spectral_type), "%s", field(line, 83, 15).c_str());
        star->alienorumid = field(line, 0, 14);
        star->HD = atoi(field(line, 113, 6).c_str());
        star->HIP = atoi(field(line, 120, 6).c_str());
        star->WD = field(line, 455, 12);
        star->mass = mass*solar_mass;
        star->temperature = temperature;
        star->distance = atof(field(line, 225, 11).c_str())*light_year;
        star->proper_motion_RA = atof(field(line, 177, 11).c_str());
        star->proper_motion_decl = atof(field(line, 189, 11).c_str());
        const double ra = atof(field(line, 55, 2).c_str()) + atof(field(line, 58, 2).c_str())/60
            +atof(field(line, 61, 4).c_str())/3600;
        star->right_ascension = ra*15*fiftyseventh;
        const double dec = atof(field(line, 67, 2).c_str()) + atof(field(line, 70, 2).c_str())/60
            +atof(field(line, 73, 2).c_str())/3600;
        star->declination = (line[66] == '-' ? -dec : dec)*fiftyseventh;
        star->epoch = J2000;
        const auto exact_start = line.find(" AST1 ");
        if (exact_start != std::string::npos)
        {
            std::istringstream values(line.substr(exact_start+6));
            std::string value;
            if (values >> std::quoted(value))
            {
                const auto position = json::parse(value);
                star->right_ascension = position[0]; star->declination = position[1]; star->epoch = position[2];
            }
        }
        for (const auto& alias : aliases) star->add_catalog_alias(alias);
        existing[row] = star.get();
        original_aliases[star.get()] = std::set<std::string>(aliases.begin(), aliases.end());
        append_cel(star.release());
    }
    const int first_new = ncelobjs;
    const unsigned int added = load_brown_dwarf_supplement();
    input.clear(); input.seekg(0);
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) return 2;
    output << "# BROWN20PC1 " << brown_dwarf_catalog_signature() << '\n';
    size_t aliases_added = 0, retained = 0;
    row = 0;
    while (std::getline(input, line))
    {
        ++row;
        if (line.rfind("# BROWN20PC1 ", 0) == 0) continue;
        const auto found = existing.find(row);
        if (found != existing.end())
        {
            const std::string display = found->second->name;
            if (display != field(line, 15, 39)) line.replace(15, 39, display + std::string(39 - display.size(), ' '));
            if (found->second->stellar_parameters.is_object())
            {
                const auto previous = line.find(" PAR1 ");
                if (previous != std::string::npos)
                {
                    std::istringstream stream(line.substr(previous + 6));
                    std::string ignored;
                    stream >> std::quoted(ignored);
                    const auto length = stream.tellg();
                    line.erase(previous, length < 0 ? std::string::npos : 6 + static_cast<size_t>(length));
                }
                std::ostringstream metadata;
                metadata << " PAR1 " << std::quoted(found->second->stellar_parameters.dump());
                const auto aliases = line.find(" ALS1 ");
                line.insert(aliases == std::string::npos ? line.size() : aliases, metadata.str());
            }
            bool has_tag = line.rfind(" ALS1 ") != std::string::npos;
            for (const auto& alias : found->second->catalog_aliases)
            {
                if (original_aliases[found->second].count(alias)) continue;
                if (!has_tag) { line += " ALS1"; has_tag = true; }
                std::ostringstream value;
                value << ' ' << std::quoted(alias);
                line += value.str();
                ++aliases_added;
            }
        }
        output << line << '\n';
        ++retained;
    }
    for (int i = first_new; i < ncelobjs; ++i)
    {
        Star* star = static_cast<Star*>(cels[i]);
        if (!star->alienorumid.empty() && !ids.insert(star->alienorumid).second)
            throw std::runtime_error("Duplicate catalog identifier");
        // Incremental output uses name identities, independent of this tool's compact array.
        const int sequence = star->seqno;
        star->seqno = -1;
        output << CatalogReader::format_condensed_star_line(star);
        star->seqno = sequence;
    }
    output.close();
    if (!output) return 1;
    std::cout << "Preserved " << retained << " existing lines; added " << added
        << " stars and " << aliases_added << " aliases.\n";
    return 0;
}
