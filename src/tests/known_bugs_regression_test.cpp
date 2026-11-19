#include <algorithm>
#include <cstdio>
#include <cstring>
#include <queue>
#include <set>
#include <unordered_map>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "../classes/neighborhoodfilter.h"
#include "../classes/patch.h"
#include "../classes/serial.h"
#include "../loaders.h"

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
