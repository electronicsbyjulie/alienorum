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
#include "../classes/browncatalog.h"
#include "../classes/cat.h"
#include "../classes/neighborhoodfilter.h"
#include "../classes/patch.h"
#include "../classes/serial.h"
#include "../loaders.h"
#include "../housekeeping.h"

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
