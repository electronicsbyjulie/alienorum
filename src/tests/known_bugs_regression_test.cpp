#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <gtest/gtest.h>
#include "../classes/alienorumid.h"
#include "../classes/objectsearch.h"
#include "../classes/browncatalog.h"
#include "../classes/cat.h"
#include "../classes/neighborhoodfilter.h"
#include "../classes/patch.h"
#include "../classes/serial.h"
#include "../loaders.h"
#include "../housekeeping.h"
#include "../dialogs.h"

using namespace alienorum;

namespace
{
    // Public reference contract, separate from all production calculations.
    // https://en.wikipedia.org/w/index.php?title=TRAPPIST-1&oldid=1384648318#Data_table
    // The table's temperatures assume zero Bond albedo and full redistribution.
    // The Habitable_zone section lists e, f and g; h is outside the conservative zone.
    // Changes to this reference or its assertions follow AGENTS.md's approval rule.
    struct PublishedPlanet
    {
        char letter;
        double flux, flux_error, equilibrium_k, temperature_error;
        bool habitable_zone;
    };
    constexpr PublishedPlanet published_planets[] =
    {
        {'b', 4.153, .160, 397.6, 3.8, false},
        {'c', 2.214, .085, 339.7, 3.3, false},
        {'d', 1.115, .040, 286.2, 2.8, false},
        {'e', .646, .025, 249.7, 2.4, true},
        {'f', .373, .014, 217.7, 2.1, true},
        {'g', .252, .010, 197.3, 1.9, true},
        {'h', .144, .006, 171.7, 1.7, false}
    };

    class OfflineCatalogReader : public CatalogReader
    {
        public:
        using CatalogReader::exorow_read_line;
        using CatalogReader::resolve_or_create_exostar;
        using CatalogReader::add_exoplanet_from_row;
    };

    struct ZeroBondAlbedo
    {
        std::vector<LocalPatchPoint> saved = dataset_bond_albedines;
        ZeroBondAlbedo()
        {
            dataset_bond_albedines =
            {
                {0, 0, 0, 0, 0, 0, 0},
                {1, 0, 0, 0, 0, 0, 0},
                {0, 1, 0, 0, 0, 0, 0},
                {1, 1, 0, 0, 0, 0, 0}
            };
        }
        ~ZeroBondAlbedo() { dataset_bond_albedines = std::move(saved); }
    };

    std::string identity_key(const std::string& alias)
    {
        std::string key = stellar_alias_key(alias);
        if (key.compare(0, 6, "GLIESE") == 0) key = "GJ" + key.substr(6);
        else if (key.size() > 2 && key.compare(0, 2, "GL") == 0 && std::isdigit(static_cast<unsigned char>(key[2])))
            key = "GJ" + key.substr(2);
        return key;
    }

    class KnownCatalogRegression : public ::testing::Test
    {
        public:
        static void SetUpTestSuite()
        {
            cels = new CelestialObject*[MAX_CELOBJS]();
            Star::load_main_seq_dat();
            read_cons_lines();
            OfflineCatalogReader reader;
            ASSERT_GT(reader.read_condensed_star_cat(), 100000);
            ExactStarSearchIndex lookup;
            std::map<int, std::vector<int>> planet_ids;
            unsigned int planets = 0;
            bool loaded_stars = false;
            for (const char* path : {"catalogs/exoplanets_stars.dat", "catalogs/exoplanets_planets.dat"})
            {
                FILE* input = fopen(path, "r");
                ASSERT_NE(input, nullptr) << path;
                ExoRow row;
                while (reader.exorow_read_line(input, row))
                {
                    bool created = false;
                    Star* host = reader.resolve_or_create_exostar(row, loaded_stars, &created, &lookup);
                    if (loaded_stars && host)
                        reader.add_exoplanet_from_row(row, host, planet_ids, planets, &lookup);
                }
                fclose(input);
                loaded_stars = true;
            }
            ASSERT_GT(planets, 7000u);
        }

        static void TearDownTestSuite()
        {
            wait_for_texture_tasks();
            for (int index = 0; cels[index]; ++index) delete cels[index];
            delete[] cels;
            cels = nullptr;
            delete[] hdcache;
            delete[] hipcache;
            hdcache = hipcache = nullptr;
            dmcache.clear();
            first_letter_index.clear();
            constellation_index.clear();
            ncelobjs = 0;
        }
    };
}

TEST(CoolDwarfRadiationRegression, HeatingUsesRadiusAndTemperatureRatherThanVisualProxy)
{
    for (double temperature : {2400.0, 2800.0, 3200.0, 3600.0, 3900.0})
    {
        Star star;
        strcpy(star.spectral_type, "M5V");
        star.temperature = temperature;
        star.mass = .15 * solar_mass;
        star.volumetric_mean_radius = .18 * solar_radius;
        star.absolute_magnitude = 10;
        Planet planet;
        planet.orbit = new Orbit();
        planet.orbit->center = &star;
        planet.orbit->semimajor_axis = .08 * AU;
        const double expected = std::pow(.18, 2) * std::pow(temperature / sun_temp, 4) / std::pow(.08, 2);
        EXPECT_NEAR(planet.est_bolometric_flux(), expected, expected * 1e-12);
        star.absolute_magnitude = 20;
        EXPECT_NEAR(planet.est_bolometric_flux(), expected, expected * 1e-12);
        star.volumetric_mean_radius *= 2;
        EXPECT_NEAR(planet.est_bolometric_flux(), 4 * expected, expected * 1e-12);
        star.spectral_type[0] = 0;
        EXPECT_NEAR(planet.est_bolometric_flux(), 4 * expected, expected * 1e-12);
    }
}

TEST(NeighborhoodCategoryRegression, CombinedMenuIncludesBothDwarfPopulations)
{
    EXPECT_EQ(std::count(neighborhood_types.begin(), neighborhood_types.end(), NeighborhoodType::red_brown_dwarfs), 1);
    EXPECT_EQ(std::count(neighborhood_types.begin(), neighborhood_types.end(), NeighborhoodType::red_dwarfs), 0);
    EXPECT_EQ(std::count(neighborhood_types.begin(), neighborhood_types.end(), NeighborhoodType::brown_dwarfs), 0);
    EXPECT_NE(std::string(neighborhood_type_labels, sizeof(neighborhood_type_labels)).find("Red+Brown Dwarfs"), std::string::npos);
    Star red, cool, giant;
    strcpy(red.spectral_type, "M5.5V");
    red.mass = .12 * solar_mass;
    strcpy(cool.spectral_type, "M7.4 INT-G");
    cool.mass = .06 * solar_mass;
    EXPECT_FALSE(cool.is_evolved());
    strcpy(giant.spectral_type, "M3III");
    giant.mass = solar_mass;
    EXPECT_TRUE(neighborhood_matches_type(red, NeighborhoodType::red_brown_dwarfs));
    EXPECT_TRUE(neighborhood_matches_type(cool, NeighborhoodType::red_brown_dwarfs));
    EXPECT_FALSE(neighborhood_matches_type(giant, NeighborhoodType::red_brown_dwarfs));
}

TEST_F(KnownCatalogRegression, TrappistFluxAndEquilibriumTemperaturesMatchPinnedPublicTable)
{
    ZeroBondAlbedo zero_albedo;
    for (const auto& reference : published_planets)
    {
        const std::string name = std::string("TRAPPIST-1 ") + reference.letter;
        const int index = find_object(name.c_str());
        ASSERT_GE(index, 0) << name;
        ASSERT_EQ(cels[index]->typeclass(), class_planet);
        Planet* planet = static_cast<Planet*>(cels[index]);
        SCOPED_TRACE(name);
        EXPECT_NEAR(planet->est_bolometric_flux(), reference.flux, 2 * reference.flux_error);
        EXPECT_NEAR(planet->equilibrium_temperature(), reference.equilibrium_k, 2 * reference.temperature_error);
    }
}

TEST_F(KnownCatalogRegression, TrappistConservativeZoneContainsEFGAndExcludesH)
{
    for (const auto& reference : published_planets)
    {
        const std::string name = std::string("TRAPPIST-1 ") + reference.letter;
        const int index = find_object(name.c_str());
        ASSERT_GE(index, 0) << name;
        EXPECT_EQ(static_cast<Planet*>(cels[index])->is_in_con_HZ(), reference.habitable_zone) << name;
    }
}

TEST_F(KnownCatalogRegression, TeegardenAndProximaShareTheVisibleDwarfCategory)
{
    for (const char* name : {"Teegarden's Star", "Proxima Centauri"})
    {
        const int index = find_object(name, true, 9e29, 0);
        ASSERT_GE(index, 0) << name;
        EXPECT_TRUE(neighborhood_matches_type(*static_cast<Star*>(cels[index]), NeighborhoodType::red_brown_dwarfs)) << name;
    }
}

TEST_F(KnownCatalogRegression, SessionChecklistStarsResolveInUserSearch)
{
    for (const char* query : {
        "Proxima Centauri", "Rigil Kentaurus", "Toliman", "TOI-3835", "WD0743-336",
        "WD0512+326", "HIP 85605", "WISE J0855-0714", "Atlas", "3b Tau 2 B",
        "Xi Ursae Majoris", "HD93359 D", "Proxima b", "8r Lep 12", "6w Pup 7",
        "9r Com 7 C", "5o Lib 4 D", "9o Vir 204 B", "WD0204-306", "WD0325+263",
        "WD0858-220", "WD0509+168", "WD2159-754", "HIP 86961", "HIP 86963",
        "Epsilon Ceti", "Kappa Fornacis", "HD 254229", "HD 198477", "GJ 3331 A",
        "GJ 3728 B", "G 123-45", "G 194-47", "VHS 1256-1257", "2MASS J0213+3648"})
    {
        const auto result = search_objects(query);
        EXPECT_TRUE(result.exact && !result.matches.empty()) << query;
    }
}

TEST_F(KnownCatalogRegression, NearbyDistancesUseAuditedAstrometryAndTheVisualOrbit)
{
    const auto find_star = [](const char* name) -> Star*
    {
        const int id = find_object(name, true, 9e29, 1);
        return id < 0 ? nullptr : static_cast<Star*>(cels[id]);
    };
    for (const auto& [name, expected_pc] : std::map<std::string, double>{
        {"TOI-3835", 504.711}, {"WD0743-336", 15.21}, {"WD0512+326", 81.59},
        {"WD0204-306", 29.25}, {"WD0325+263", 75.68}, {"WD0858-220", 73.79},
        {"HIP 85605", 548.85}, {"HIP 86961", 14.15}})
    {
        const Star* object = find_star(name.c_str());
        ASSERT_NE(object, nullptr) << name;
        EXPECT_NEAR(object->distance / parsec, expected_pc, 0.05) << name;
    }
    Star* alpha = find_star("Rigil Kentaurus");
    Star* toliman = find_star("Toliman");
    Star* proxima = find_star("Proxima Centauri");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(toliman, nullptr);
    ASSERT_NE(proxima, nullptr);
    alpha->update_location(J2000_TIME_T);
    toliman->update_location(J2000_TIME_T);
    proxima->update_location(J2000_TIME_T);
    CelestialLocation sun;
    const double proxima_distance = sun.distance_to(proxima->location) / light_year;
    EXPECT_NEAR(proxima_distance, 4.24, 0.015);
    EXPECT_LT(proxima_distance, sun.distance_to(alpha->location) / light_year);
    EXPECT_LT(proxima_distance, sun.distance_to(toliman->location) / light_year);
    EXPECT_NEAR(proxima->location.distance_to(alpha->location) / AU, 12947, 260);
}

TEST_F(KnownCatalogRegression, FamousStarsHaveOnePhysicalIdentityAcrossEveryCatalogAlias)
{
    const std::vector<std::vector<std::string>> families =
    {
        {"Proxima Centauri", "HIP 70890", "GJ 551", "Gliese 551"},
        {"Rigil Kentaurus", "HD 128620", "HIP 71683", "GJ 559 A"},
        {"Toliman", "HD 128621", "HIP 71681", "GJ 559 B"},
        {"Barnard's Star", "HIP 87937", "GJ 699"},
        {"Teegarden's Star", "2MASS J02530084+1652532", "LSPM J0253+1652"},
        {"TRAPPIST-1", "2MASS J23062928-0502285", "EPIC 246199087", "K2-112"},
        {"82 Eridani", "HD 20794", "HIP 15510", "GJ 139"},
        {"Sirius", "Alpha Canis Majoris", "HD 48915", "HIP 32349", "Gliese 244", "GJ 244 A"},
        {"Vega", "Alpha Lyrae", "HD 172167", "HIP 91262", "GJ 721"},
        {"Capella", "Alpha Aurigae", "HD 34029", "HIP 24608", "GJ 194 A"},
        {"Betelgeuse", "Alpha Orionis", "HD 39801", "HIP 27989"},
        {"Deneb", "Alpha Cygni", "HD 197345", "HIP 102098"}
    };
    std::unordered_map<int, int> hd_count, hip_count;
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->typeclass() != class_star) continue;
        const Star& star = *static_cast<Star*>(cels[index]);
        if (star.HD) ++hd_count[star.HD];
        if (star.HIP) ++hip_count[star.HIP];
    }
    std::unordered_map<std::string, std::vector<int>> identities;
    std::unordered_map<int, std::vector<std::string>> aliases;
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->typeclass() != class_star) continue;
        Star& star = *static_cast<Star*>(cels[index]);
        auto& values = aliases[index];
        values = star.catalog_aliases;
        for (const auto& alias : {std::string(star.name), star.local_name, star.alienorumid,
            std::string(star.Bayer), std::string(star.Flamsteed), std::string(star.Gliese), star.WD, star.gaia_wd_name})
            if (!alias.empty()) values.push_back(alias);
        const char component = star.get_component();
        // Shared system numbers are qualified for real companion stars. A shared
        // unresolved system identifier is not evidence that its components are copies.
        if (star.HD) values.push_back("HD " + std::to_string(star.HD)
            + (hd_count[star.HD] > 1 && component > 'A' ? std::string(" ") + component : ""));
        if (star.HIP) values.push_back("HIP " + std::to_string(star.HIP)
            + (hip_count[star.HIP] > 1 && component > 'A' ? std::string(" ") + component : ""));
        for (const auto& alias : values) identities[identity_key(alias)].push_back(index);
    }
    ExactStarSearchIndex lookup;
    std::set<int> distinct;
    for (const auto& family : families)
    {
        SCOPED_TRACE(family.front());
        const int canonical = find_object(family.front().c_str(), true, 9e29, 0, &lookup);
        ASSERT_GE(canonical, 0);
        EXPECT_TRUE(distinct.insert(canonical).second);
        std::queue<std::string> pending;
        std::set<std::string> visited;
        std::set<int> members;
        for (const auto& anchor : family)
        {
            EXPECT_EQ(find_object(anchor.c_str(), true, 9e29, 0, &lookup), canonical) << anchor;
            pending.push(identity_key(anchor));
        }
        while (!pending.empty())
        {
            const std::string key = pending.front();
            pending.pop();
            if (!visited.insert(key).second) continue;
            const auto found = identities.find(key);
            if (found == identities.end()) continue;
            for (int index : found->second)
            {
                if (!members.insert(index).second) continue;
                for (const auto& alias : aliases[index]) pending.push(identity_key(alias));
            }
        }
        EXPECT_EQ(members, std::set<int>{canonical});
        for (int index : members)
            for (const auto& alias : aliases[index])
                EXPECT_EQ(find_object(alias.c_str(), true, 9e29, 0, &lookup), canonical) << alias;
    }
}

TEST_F(KnownCatalogRegression, BayerAliasesReuseAlphaCentauriAndMirfak)
{
    ExactStarSearchIndex lookup;
    const int primary = find_object("Rigil Kentaurus", true, 9e29, 0, &lookup);
    const int secondary = find_object("Toliman", true, 9e29, 0, &lookup);
    const int mirfak = find_object("Mirfak", true, 9e29, 0, &lookup);
    ASSERT_GE(primary, 0);
    ASSERT_GE(secondary, 0);
    ASSERT_GE(mirfak, 0);
    EXPECT_NE(primary, secondary);
    EXPECT_EQ(find_object("Alf Cen A", true, 9e29, 0, &lookup), primary);
    EXPECT_EQ(find_object("Alf Cen B", true, 9e29, 0, &lookup), secondary);
    EXPECT_EQ(find_object("Alf Per", true, 9e29, 0, &lookup), mirfak);
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
        const auto name = stellar_alias_key(cels[index]->name);
        if (name == "ALFCENA" || name == "ALPCENA") { EXPECT_EQ(index, primary); }
        if (name == "ALFCENB" || name == "ALPCENB") { EXPECT_EQ(index, secondary); }
        if (name == "ALFPER" || name == "ALPPER") { EXPECT_EQ(index, mirfak); }
    }
}

TEST_F(KnownCatalogRegression, CatalogIdentifiersAreUniqueAcrossPhysicalStars)
{
    std::unordered_map<uint64_t, Star*> hd, hip, hr, gaia;
    const auto verify = [](auto& catalog, uint64_t identifier, Star* star, const char* prefix)
    {
        if (!identifier) return;
        const auto found = catalog.emplace(identifier, star);
        if (found.second) return;
        Star* previous = found.first->second;
        const bool distinct_components = star->multisys && star->multisys == previous->multisys
            && star->get_component() && previous->get_component()
            && star->get_component() != previous->get_component();
        EXPECT_TRUE(distinct_components) << prefix << ' ' << identifier << " on "
            << previous->name << " and " << star->name;
    };
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
        auto* star = static_cast<Star*>(cels[index]);
        verify(hd, star->HD, star, "HD");
        verify(hip, star->HIP, star, "HIP");
        verify(hr, star->HR, star, "HR");
        verify(gaia, star->gaia_source_id, star, "Gaia DR3");
    }
}

TEST_F(KnownCatalogRegression, SourceCrossIdentificationsReuseTheirCatalogStars)
{
    ExactStarSearchIndex lookup;
    for (const auto& pair : {std::pair<const char*, const char*>{"HR 2986", "HD 62376"},
        {"HR 8799", "HIP 114189"}, {"LP 933-24", "HIP 113221"},
        {"TOI-500", "HIP 34269"}, {"TOI-5739", "HD 119056"}, {"TIC 272785018", "HD 143253"}})
    {
        const int canonical = find_object(pair.second, true, 9e29, 1, &lookup);
        ASSERT_GE(canonical, 0) << pair.second;
        EXPECT_EQ(find_object(pair.first, true, 9e29, 1, &lookup), canonical) << pair.first;
        for (int index : lookup.candidates(pair.first))
        {
            if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
            if (identity_key(cels[index]->name) == identity_key(pair.first))
            {
                EXPECT_EQ(index, canonical) << pair.first;
            }
        }
    }
}

TEST_F(KnownCatalogRegression, BakedCatalogKeepsOneStarPerVerifiedCrossIdentification)
{
    ExactStarSearchIndex lookup;
    const int dwarf = find_object("WD0208+396", true, 9e29, 1, &lookup);
    ASSERT_GE(dwarf, 0);
    EXPECT_EQ(find_object("GJ 1042", true, 9e29, 1, &lookup), dwarf);
    const int host = find_object("HD 19467", true, 9e29, 1, &lookup);
    ASSERT_GE(host, 0);
    const int companion = find_object("HD 19467 B", true, 9e29, 1, &lookup);
    ASSERT_GE(companion, 0);
    ASSERT_NE(companion, host);
    ASSERT_NE(cels[companion]->orbit, nullptr);
    EXPECT_EQ(cels[companion]->orbit->center, cels[host]);
    const std::string prefix = "# Duplicate star ";
    std::ifstream source("starname.dat");
    ASSERT_TRUE(source);
    std::set<std::string> retired_names;
    for (std::string line; std::getline(source, line);)
    {
        if (line.compare(0, prefix.size(), prefix)) continue;
        std::istringstream fields(line.substr(prefix.size()));
        std::string obsolete, canonical;
        ASSERT_TRUE(fields >> std::quoted(obsolete) >> std::quoted(canonical)) << line;
        retired_names.insert(obsolete);
        const int canonical_index = find_object(canonical.c_str(), true, 9e29, 1, &lookup);
        ASSERT_GE(canonical_index, 0) << canonical;
        EXPECT_EQ(find_object(obsolete.c_str(), true, 9e29, 1, &lookup), canonical_index) << obsolete;
    }
    EXPECT_GE(retired_names.size(), 48u);
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
        EXPECT_FALSE(retired_names.count(cels[index]->name)) << cels[index]->name;
    }
}

TEST_F(KnownCatalogRegression, VisibleAlienorumIdsFollowTheDocumentedFormat)
{
    const std::regex format(R"((V|-[1-9][0-9]?|[0-9])[bcwyorp] [A-Z][A-Za-z]{2}( [1-9][0-9]*)?( [B-Z])?)");
    size_t numbered = 0, blank = 0;
    for (int index = 0; cels[index]; ++index)
    {
        const auto& object = *cels[index];
        const auto shown = shown_alienorum_id(object);
        if (object.typeclass() != class_star)
        {
            EXPECT_TRUE(shown.empty()) << object.name;
            continue;
        }
        const auto& star = static_cast<const Star&>(object);
        EXPECT_EQ(shown, star.alienorumid) << star.name;
        if (index == 0 && is_catalog_sun(star))
        {
            EXPECT_EQ(shown, "-26");
            continue;
        }
        if (shown.empty()) ++blank;
        else
        {
            ++numbered;
            EXPECT_TRUE(std::regex_match(shown.begin(), shown.end(), format)) << star.name << ' ' << shown;
        }
    }
    EXPECT_GT(numbered, 100000u);
    EXPECT_GT(blank, 1000u);

    Star sample;
    for (const char* bad : {"p-deadbeef", "U2eb4935ee7abc", "-26", "10p Vel 1", "4r Qqq", "4r Ori 0"})
    {
        sample.alienorumid = bad;
        EXPECT_TRUE(shown_alienorum_id(sample).empty()) << bad;
    }
    sample.alienorumid = "6c And 17 B";
    EXPECT_EQ(shown_alienorum_id(sample), sample.alienorumid);
    EXPECT_TRUE(shown_alienorum_id(Planet()).empty());
}

TEST_F(KnownCatalogRegression, CetusVariablesUseRightAscensionAndLeaveNumericRanksContiguous)
{
    const int first = find_object("36 G. Cet", true, 9e29, 1);
    const int second = find_object("Mira", true, 9e29, 1);
    ASSERT_GE(first, 0);
    ASSERT_GE(second, 0);
    EXPECT_LT(static_cast<Star*>(cels[first])->right_ascension, static_cast<Star*>(cels[second])->right_ascension);
    EXPECT_EQ(shown_alienorum_id(*cels[first]), "Vr Cet 1");
    EXPECT_EQ(shown_alienorum_id(*cels[second]), "Vr Cet 2");
    EXPECT_EQ(find_object("Vr Cet 1", true, 9e29, 1), first);
    EXPECT_EQ(find_object("Vr Cet 2", true, 9e29, 1), second);
    ASSERT_GE(find_object("6r Cet 3", true, 9e29, 1), 0);

    for (const std::string prefix : {"Vr Cet", "6r Cet"})
    {
        std::vector<int> ranks;
        for (int index = 0; cels[index]; ++index)
        {
            const auto* object = cels[index];
            if (object->deleted || object->typeclass() != class_star) continue;
            const auto id = shown_alienorum_id(*object);
            if (id.compare(0, prefix.size() + 1, prefix + " ") != 0) continue;
            const auto rank = id.substr(prefix.size() + 1);
            if (rank.find(' ') == std::string_view::npos && rank.front() >= '1' && rank.front() <= '9')
                ranks.push_back(std::stoi(std::string(rank)));
        }
        std::sort(ranks.begin(), ranks.end());
        ASSERT_GE(ranks.size(), 2u);
        for (size_t index = 0; index < ranks.size(); ++index) EXPECT_EQ(ranks[index], index + 1) << prefix;
    }
}

TEST_F(KnownCatalogRegression, SearchBoxFindsPublishedGouldNames)
{
    const int target = find_object("Vr Cet 1", true, 9e29, 1);
    ASSERT_GE(target, 0);
    for (const char* query : {"36 G. Ceti", "36 G. Cet", "36 G Cet", "36g cet", "36 cet"})
    {
        const auto result = search_objects(query);
        EXPECT_TRUE(result.exact) << query;
        EXPECT_TRUE(std::any_of(result.matches.begin(), result.matches.end(), [target](const auto& match)
        {
            return match.index == target;
        })) << query;
    }
}

TEST_F(KnownCatalogRegression, AbsolutePhotometryRepairsAuditedMissingValues)
{
    ExactStarSearchIndex lookup;
    for (const char* name : {"LP 443-17 B", "HR 5110 B", "Zet Cap B", "Gam Cas C", "Zet Cyg B", "VW LMi C",
        "GW Ori B", "HD 136138 B", "G 240-23 B", "2MASS J04173459-4834391 B", "AB Dor Ca", "SCR J0723-8015 B",
        "LP 848-50 B", "WT 1827 B", "LP 909-18 B", "eta Boo Ab", "L 403-31 B", "BD+16 2708 Bb", "G 139-29 B",
        "WISEA J191239.34-501617.9 B", "LP 754-5 B", "LEHPM 2-1265 B", "LP 575-35 B",
        "[MFL2000] J210104.18+030705.1 B", "WISE J0855-0714"})
    {
        const int index = find_object(name, true, 9e29, 1, &lookup);
        ASSERT_GE(index, 0) << name;
        EXPECT_TRUE(std::isfinite(cels[index]->absolute_magnitude)) << name;
        EXPECT_NE(cels[index]->absolute_magnitude, 0) << name;
    }
    const int lp = find_object("LP 443-17 B", true, 9e29, 1, &lookup);
    ASSERT_GE(lp, 0);
    EXPECT_GT(cels[lp]->absolute_magnitude, 12);
    EXPECT_LT(cels[lp]->absolute_magnitude, 60);
}

TEST_F(KnownCatalogRegression, ABDorResolvedCompanionReusesTheCombinedDesignation)
{
    const int ca = find_object("AB Dor Ca", true, 9e29, 1);
    const int a = find_object("HD 36705", true, 9e29, 1);
    const int cb = find_object("AB Dor Cb", false, 9e29, 1);
    ASSERT_GE(a, 0);
    ASSERT_GE(ca, 0);
    ASSERT_GE(cb, 0);
    EXPECT_EQ(find_object("AB Dor C", true, 9e29, 1), ca);
    ASSERT_NE(cels[ca]->orbit, nullptr);
    ASSERT_NE(cels[cb]->orbit, nullptr);
    EXPECT_EQ(cels[ca]->orbit->center, cels[a]);
    EXPECT_EQ(cels[cb]->orbit->center, cels[ca]);
    EXPECT_EQ(cels[cb]->typeclass(), class_planet);
    EXPECT_EQ(find_object("AB Dor Cb", true, 9e29, 1), -1);
    const auto* primary = static_cast<const Star*>(cels[a]);
    ASSERT_NE(primary->multisys, nullptr);
    for (char slot = 'A'; slot <= 'Z'; ++slot)
    {
        const auto* member = primary->multisys->get_member(slot);
        if (member) EXPECT_FALSE(member->deleted) << slot << ' ' << member->name;
    }
}

TEST_F(KnownCatalogRegression, Live82EridaniContainsExactlyTheConfirmedCatalogOrbits)
{
    set_center_objects();
    const int host_index = find_object("HD 20794", true, 9e29, 0);
    ASSERT_GE(host_index, 0);
    const auto* host = static_cast<Star*>(cels[host_index]);
    ASSERT_EQ(host->pl_indices.size(), 3u);
    std::vector<double> periods;
    for (int index = 0; cels[index]; ++index)
    {
        const auto* object = cels[index];
        if (!object->deleted && object->typeclass() == class_planet && object->orbit && object->orbit->center == host)
        {
            periods.push_back(object->orbit->period / oneday);
            EXPECT_FALSE(static_cast<const Planet*>(object)->catalog_candidate);
        }
    }
    std::sort(periods.begin(), periods.end());
    ASSERT_EQ(periods.size(), 3u);
    EXPECT_NEAR(periods[0], 18.314, .1);
    EXPECT_NEAR(periods[1], 89.68, .2);
    EXPECT_NEAR(periods[2], 647.6, 4);
    for (const char* extra : {"82 Eri e", "82 Eri f", "82 Eri g", "82 Eri h"})
        EXPECT_LT(find_object(extra, false, 9e29, 0), 0) << extra;
}

TEST_F(KnownCatalogRegression, PurpleIdentifiersPreserveStellarRanksAndSourcePhotometry)
{
    const int teegarden_index = find_object("Teegarden's Star", true, 9e29, 0);
    const int brown_index = find_object("Luhman 16 A", true, 9e29, 0);
    const int companion_index = find_object("Luhman 16 B", true, 9e29, 0);
    const int invalid_index = find_object("AB Dor Ca", true, 9e29, 0);
    ASSERT_GE(teegarden_index, 0);
    ASSERT_GE(brown_index, 0);
    ASSERT_GE(companion_index, 0);
    ASSERT_GE(invalid_index, 0);
    auto* teegarden = static_cast<Star*>(cels[teegarden_index]);
    auto* brown = static_cast<Star*>(cels[brown_index]);
    auto* companion = static_cast<Star*>(cels[companion_index]);
    auto* invalid = static_cast<Star*>(cels[invalid_index]);
    ASSERT_EQ(std::floor(brown->apparent_magnitude), 22.0);
    EXPECT_NEAR(teegarden->mass / solar_mass, .097, 1e-9);
    EXPECT_DOUBLE_EQ(teegarden->apparent_magnitude, 15.13);
    EXPECT_DOUBLE_EQ(invalid->apparent_magnitude, 1e29);
    EXPECT_DOUBLE_EQ(invalid->temperature, 0);
    std::map<Star*, std::string> published;
    for (int index = 0; cels[index]; ++index)
    {
        if (cels[index]->typeclass() != class_star || cels[index]->deleted) continue;
        auto* star = static_cast<Star*>(cels[index]);
        if (!star->alienorumid.empty()) published.emplace(star, star->alienorumid);
    }
    brown->alienorumid = "p-0000000000";
    companion->alienorumid = "p-0000000000 B";
    finalize_brown_catalog();
    EXPECT_TRUE(brown->alienorumid.empty());
    EXPECT_TRUE(companion->alienorumid.empty());
    EXPECT_EQ(find_object("Luhman 16 A", true, 9e29, 0), brown_index);
    EXPECT_EQ(find_object("Luhman 16 B", true, 9e29, 0), companion_index);
    EXPECT_TRUE(teegarden->alienorumid.empty());
    EXPECT_TRUE(invalid->alienorumid.empty());
    finalize_brown_catalog();
    EXPECT_TRUE(brown->alienorumid.empty());
    EXPECT_TRUE(companion->alienorumid.empty());
    for (const auto& [star, previous] : published) EXPECT_EQ(star->alienorumid, previous) << star->name;
}

TEST_F(KnownCatalogRegression, DefaultNeighborhoodAgreesWithPublishedNearestStars)
{
    struct ReferenceStar
    {
        const char* name;
        double light_years, ra_degrees, dec_degrees;
        bool confirmed = true;
    };
    // Distances and J2000 coordinates from the nearest-star table, through 20 light-years.
    // https://en.wikipedia.org/w/index.php?title=List_of_nearest_stars&oldid=1386991042
    constexpr ReferenceStar reference[] =
    {
#include "data/nearest_stars_reference.inc"
    };

    const CelestialLocation saved_viewer = here;
    const int saved_observer = whereami;
    auto* saved_center = mycenobj;
    here = CelestialLocation();
    here.local_position = Point(AU, 0, 0);
    whereami = -1;
    mycenobj = nullptr;
    last_neighb_cen = nullptr;
    for (int i = 0; i < ncelobjs; ++i)
    {
        if (cels[i]->typeclass() != class_star || cels[i]->deleted) continue;
        auto* star = static_cast<Star*>(cels[i]);
        if (i == 0 && std::strcmp(star->name, "Sun") == 0) star->tmprel = Point() - here.local_position;
        else if (star->distance > 0 && star->distance <= 26 * light_year)
        {
            star->update_location(J2000_TIME_T);
            auto relative = star->location - here;
            star->tmprel = Point(relative);
        }
        else star->tmprel = Point(INFINITY, 0, 0);
    }

    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024, 768);
    io.Fonts->Build();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(950, 650));
    draw_stellar_neighborhood(io);
    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    here = saved_viewer;
    whereami = saved_observer;
    mycenobj = saved_center;

    extern std::vector<int> neighb_celids;
    extern std::vector<double> neighb_celr;
    ASSERT_EQ(neighb_celids.size(), neighb_celr.size());
    const double radians = std::acos(-1.0) / 180;
    std::set<size_t> matched_rows;
    std::vector<std::pair<size_t, const ReferenceStar*>> matches;
    const auto matches_identity = [&](const ReferenceStar& entry, size_t row)
    {
        const auto* star = static_cast<const Star*>(cels[neighb_celids[row]]);
        const auto key = identity_key(entry.name);
        if (identity_key(star->name) == key || identity_key(star->Gliese) == key) return true;
        return std::any_of(star->catalog_aliases.begin(), star->catalog_aliases.end(),
            [&](const std::string& alias) { return identity_key(alias) == key; });
    };
    std::map<const ReferenceStar*, size_t> identified;
    for (const auto& entry : reference)
    {
        for (size_t row = 0; row < neighb_celids.size(); ++row)
        {
            if (matched_rows.count(row) || !matches_identity(entry, row)) continue;
            identified.emplace(&entry, row);
            matched_rows.insert(row);
            break;
        }
    }
    for (const auto& entry : reference)
    {
        const auto direction = Point::from_ra_dec(entry.ra_degrees * radians, entry.dec_degrees * radians, 1);
        size_t match = neighb_celids.size();
        double nearest = 0.35 * radians;
        const auto exact = identified.find(&entry);
        if (exact != identified.end()) match = exact->second;
        else if (!entry.confirmed) continue;
        else for (size_t row = 0; row < neighb_celids.size(); ++row)
        {
            if (matched_rows.count(row)) continue;
            const auto* object = cels[neighb_celids[row]];
            const auto catalog_direction = Point::from_ra_dec(object->right_ascension, object->declination, 1);
            const double distance = neighb_celr[row];
            if (!std::isfinite(distance) || distance <= 0) continue;
            const double offset = std::min((direction - catalog_direction).magnitude(),
                (direction - object->tmprel * (1 / distance)).magnitude());
            if (offset < nearest)
            {
                nearest = offset;
                match = row;
            }
        }
        EXPECT_LT(match, neighb_celids.size()) << "Missing " << entry.name;
        if (match == neighb_celids.size()) continue;
        const auto* object = cels[neighb_celids[match]];
        const auto catalog_direction = Point::from_ra_dec(object->right_ascension, object->declination, 1);
        const double distance = neighb_celr[match];
        ASSERT_TRUE(std::isfinite(distance) && distance > 0) << entry.name;
        EXPECT_LT(std::min((direction - catalog_direction).magnitude(),
            (direction - object->tmprel * (1 / distance)).magnitude()), 0.35 * radians) << entry.name;
        matched_rows.insert(match);
        matches.emplace_back(match, &entry);
        EXPECT_NEAR(neighb_celr[match] / light_year, entry.light_years, 0.12)
            << entry.name << " matched to " << cels[neighb_celids[match]]->name;
    }
    std::sort(matches.begin(), matches.end());
    const ReferenceStar* farthest = nullptr;
    for (const auto& [row, entry] : matches)
    {
        if (farthest)
        {
            EXPECT_GE(entry->light_years + 0.12, farthest->light_years)
                << entry->name << " follows " << farthest->name;
        }
        if (!farthest || entry->light_years > farthest->light_years) farthest = entry;
    }
    for (size_t row = 0; row < neighb_celids.size(); ++row)
    {
        if (neighb_celr[row] < 20 * light_year && std::strcmp(cels[neighb_celids[row]]->name, "Sun") != 0)
        {
            EXPECT_TRUE(matched_rows.count(row)) << "Extra " << cels[neighb_celids[row]]->name
                << " at " << neighb_celr[row] / light_year << " light-years";
        }
    }
}

TEST_F(KnownCatalogRegression, CatalogSearchNamesUseKeyboardCharactersOrDocumentedExceptions)
{
    std::set<std::string> source_names;
    for (const auto& [path, first, width] : std::vector<std::tuple<const char*, size_t, size_t>>{
        {"exoname.dat", 40, 23}, {"starname.dat", 0, 25}})
    {
        std::ifstream names(path);
        ASSERT_TRUE(names) << path;
        for (std::string line; std::getline(names, line);)
        {
            if (line.empty() || line.front() == '#' || line.size() <= first) continue;
            source_names.insert(trim(line.substr(first, width).c_str()));
        }
    }
    const std::regex greek_letter(u8"^(α|β|γ|δ|ε|ζ|η|θ|ι|κ|λ|μ|ν|ξ|ο|π|ρ|σ|τ|υ|φ|χ|ψ|ω)[0-9]?[ ]*[A-Za-z][ -~]*$");
    const auto check = [&](const std::string& value)
    {
        if (std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= 32 && ch <= 126; })) return;
        const size_t last_space = value.rfind(' ');
        const bool component = last_space != std::string::npos && last_space + 2 == value.size()
            && value.back() >= 'A' && value.back() <= 'Z';
        if (source_names.count(value) || (component && source_names.count(value.substr(0, last_space))))
        {
            const auto key = object_search_key(value);
            EXPECT_FALSE(key.empty()) << value;
            EXPECT_TRUE(std::all_of(key.begin(), key.end(), [](unsigned char ch) { return ch >= 32 && ch <= 126; })) << value;
        }
        else
        {
            EXPECT_TRUE(std::regex_match(value, greek_letter)) << value;
        }
    };
    for (int i = 0; i < ncelobjs; ++i)
    {
        if (cels[i]->deleted) continue;
        const auto& object = *cels[i];
        check(object.name);
        check(object.origname);
        if (object.typeclass() == class_star)
        {
            const auto& star = static_cast<const Star&>(object);
            for (const auto& value : {star.local_name, std::string(star.Bayer), std::string(star.Flamsteed),
                std::string(star.Gliese), star.alienorumid, star.WD, star.gaia_wd_name, star.CCDM}) check(value);
            for (const auto& value : star.catalog_aliases) check(value);
        }
        else if (object.typeclass() == class_planet || object.typeclass() == class_moon)
        {
            const auto& planet = static_cast<const Planet&>(object);
            check(planet.catalog_designation);
            check(planet.display_designation);
        }
    }
}

TEST_F(KnownCatalogRegression, TrappistInnerWorldsUseTheObservedAirlessScenario)
{
    for (char letter = 'b'; letter <= 'h'; ++letter)
    {
        const auto name = std::string("TRAPPIST-1 ") + letter;
        const int index = find_object(name.c_str(), false, 9e29, 0);
        ASSERT_GE(index, 0) << name;
        ASSERT_EQ(cels[index]->typeclass(), class_planet) << name;
        auto* planet = static_cast<Planet*>(cels[index]);
        EXPECT_TRUE(planet->classification_evidence.contains("bulk_composition")) << name;
        if (letter <= 'd')
        {
            ASSERT_NE(planet->atm, nullptr) << name;
            EXPECT_EQ(planet->type, rocky) << name;
            EXPECT_EQ(planet->get_surface_pressure(), 0) << name;
            EXPECT_FALSE(planet->atm->pressure_supplied) << name;
            EXPECT_EQ(planet->atm->pressure_model.value("basis", ""), "airless_surface_observation_scenario") << name;
        }
        else
        {
            ASSERT_TRUE(planet->classification_evidence.contains("atmosphere_observations")) << name;
            EXPECT_EQ(planet->classification_evidence["atmosphere_observations"].value("composition_status", ""),
                "unconstrained") << name;
        }
    }
}

TEST_F(KnownCatalogRegression, EveryAttachedOrbitInTheLoadedCatalogEndsAtALiveRootWithoutACycle)
{
    std::unordered_map<const CelestialObject*, int> indices;
    for (int i = 0; i < ncelobjs; ++i)
    {
        ASSERT_NE(cels[i], nullptr) << i;
        ASSERT_EQ(cels[i]->seqno, i) << cels[i]->name;
        ASSERT_TRUE(indices.emplace(cels[i], i).second) << cels[i]->name;
    }
    // A parent can occur after its children. Memoize completed paths so even a
    // deep hierarchy is checked in linear time without recursion.
    std::vector<unsigned char> state(ncelobjs);
    size_t edges = 0;
    for (int start = 0; start < ncelobjs; ++start)
    {
        if (state[start] || cels[start]->deleted) continue;
        int index = start;
        std::vector<int> path;
        while (state[index] == 0)
        {
            state[index] = 1;
            path.push_back(index);
            const auto* orbit = cels[index]->orbit;
            if (!orbit || !orbit->center) break;
            auto parent = indices.find(orbit->center);
            ASSERT_NE(parent, indices.end()) << cels[index]->name;
            ASSERT_FALSE(cels[parent->second]->deleted) << cels[index]->name;
            index = parent->second;
            ++edges;
            ASSERT_NE(state[index], 1) << "Orbital cycle through " << cels[index]->name;
        }
        for (const int slot : path) state[slot] = 2;
    }
    EXPECT_GT(edges, 100u);
}

TEST_F(KnownCatalogRegression, ObservedStellarCompanionsLinkToTheirOwnPrimaryAfterCatalogLoad)
{
    // Independent component identifications from RECONS, Hubble, ESO and the GJ 65 observations.
    // https://recons.org/TOP100.posted.htm
    // https://science.nasa.gov/missions/hubble/hubble-applauds-waltzing-dwarfs/
    // https://www.eso.org/public/images/eso0906d/
    // https://uhra.herts.ac.uk/id/eprint/6627/1/Published_Version.pdf
    struct Pair
    {
        const char* primary;
        const char* companion;
        char component;
    };
    const Pair pairs[] =
    {
        {"SCR J1845-6357 A", "SCR J1845-6357 B", 'B'},  // Late M dwarf with a T dwarf.
        {"Luhman 16 A", "Luhman 16 B", 'B'},            // Two brown dwarfs.
        {"Sirius", "GJ 244 B", 'B'},                    // Bright primary and a white dwarf.
        {"Procyon", "GJ 280 B", 'B'},
        {"61 Cygni", "61 Cygni B", 'B'},                // Wide pair of K dwarfs.
        {"BL Ceti", "UV Ceti", 'B'},                    // Two separately named flare stars.
        {"Rigil Kentaurus", "Toliman", 'B'},            // Close pair and distant tertiary.
        {"Rigil Kentaurus", "Proxima Centauri", 'C'},
        {"Keid", "GJ 166 B", 'B'},                      // 40 Eridani, with a tight BC pair.
        {"Keid", "GJ 166 C", 'C'},
        {"Theta1 Orionis C1", "Theta1 Orionis C2", 'B'}  // Massive binary in the Trapezium.
    };
    set_center_objects();
    const auto find_star = [](const char* name) -> Star*
    {
        const int slot = find_object(name, true, 9e29, 0);
        if (slot < 0 || cels[slot]->deleted || cels[slot]->typeclass() != class_star) return nullptr;
        return static_cast<Star*>(cels[slot]);
    };
    Star* unrelated = find_star("Barnard's Star");
    Star* other_trapezium_system = find_star("Theta1 Orionis A1");
    EXPECT_NE(unrelated, nullptr);
    EXPECT_NE(other_trapezium_system, nullptr);
    const auto check = [&](const Pair& pair)
    {
        SCOPED_TRACE(std::string(pair.primary) + " / " + pair.companion);
        Star* primary = find_star(pair.primary);
        Star* companion = find_star(pair.companion);
        ASSERT_NE(primary, nullptr);
        ASSERT_NE(companion, nullptr);
        ASSERT_NE(companion, primary);
        // Both the unresolved-star display and the component list use this membership.
        EXPECT_NE(primary->multisys, nullptr) << "Primary has no component list";
        EXPECT_NE(companion->multisys, nullptr) << "Companion has no component list";
        EXPECT_EQ(companion->multisys, primary->multisys);
        if (primary->multisys)
        {
            EXPECT_EQ(primary->multisys->get_member('A'), primary);
            EXPECT_EQ(primary->multisys->get_member(pair.component), companion);
            for (auto* outsider : {unrelated, other_trapezium_system})
            {
                if (outsider)
                {
                    EXPECT_EQ(primary->multisys->is_member(outsider), 0);
                }
            }
        }
        // Navigation also follows orbital parents. A tertiary may reach A through B.
        const CelestialObject* current = companion;
        std::set<const CelestialObject*> visited;
        while (current != primary && current && visited.insert(current).second)
        {
            EXPECT_NE(current, unrelated);
            EXPECT_NE(current, other_trapezium_system);
            current = current->orbit ? current->orbit->center : nullptr;
        }
        EXPECT_EQ(current, primary) << "Following the companion's orbit did not reach its primary";
    };
    for (const auto& pair : pairs) check(pair);
}
