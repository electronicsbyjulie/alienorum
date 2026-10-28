#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include "../classes/cons.h"
#include "../classes/cat.h"
#include "../loaders.h"
#include "universe_fixture.h"

using namespace alienorum;
using json = nlohmann::json;

static std::string temp_universe(const char* stem)
{
    return (std::filesystem::temp_directory_path() / (std::string("alienorum_") + stem + ".json")).string();
}

class SerializationTest : public UniverseFixture
{
    protected:
    bool save_and_reload(const char* stem, bool only_edited = false)
    {
        std::string path = temp_universe(stem);
        std::fstream out(path, std::ios::out | std::ios::trunc);
        if (!Serialization::save_all(out, cels, only_edited)) return false;
        out.close();

        delete_the_universe();

        std::fstream in(path, std::ios::in);
        bool ok = Serialization::load_all(in, cels, MAX_CELOBJS);
        in.close();
        return ok;
    }

    json read_file(const char* stem)
    {
        std::fstream in(temp_universe(stem), std::ios::in);
        json j;
        in >> j;
        return j;
    }

    void write_file(const char* stem, json j)
    {
        std::fstream out(temp_universe(stem), std::ios::out | std::ios::trunc);
        out << j.dump(4);
    }

    bool load_file(const char* stem)
    {
        std::fstream in(temp_universe(stem), std::ios::in);
        bool ok = Serialization::load_all(in, cels, MAX_CELOBJS);
        in.close();
        return ok;
    }

    CelestialObject* find_loaded(const char* name)
    {
        for (int i=0; cels[i]; i++) if (!strcmp(cels[i]->name, name)) return cels[i];
        return nullptr;
    }
};

TEST_F(SerializationTest, ReloadRefreshesHostDesignationsWhilePreservingProperNames)
{
    make_star("Sol");
    Star* host = make_star("Preferred host");
    host->add_catalog_alias("HD 998");
    Planet* planet = make_planet(host, "Old host b");
    planet->catalog_designation = "HD 998 b";
    planet->display_designation = "Old host b";
    Planet* named = make_planet(host, "Authored name", 2 * AU);
    named->catalog_designation = "HD 998 c";
    named->display_designation = "Old host c";
    ASSERT_TRUE(save_and_reload("host_designations"));
    Planet* restored = static_cast<Planet*>(find_loaded("Preferred host b"));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->catalog_designation, "HD 998 b");
    EXPECT_EQ(restored->display_designation, "Preferred host b");
    Planet* restored_named = static_cast<Planet*>(find_loaded("Authored name"));
    ASSERT_NE(restored_named, nullptr);
    EXPECT_EQ(restored_named->display_designation, "Preferred host c");
    EXPECT_EQ(find_object("HD 998 b", false), restored->seqno);
    delete_the_universe();
    std::filesystem::remove(temp_universe("host_designations"));
}

TEST_F(SerializationTest, TouchingFictionalMoonIsRepairedWithoutMovingCatalogMoons)
{
    Star* host = make_star("Primary");
    Planet* parent = make_planet(host, "Low density parent");
    parent->mass = 3.833960699906274e30;
    parent->volumetric_mean_radius = 118500600;
    Moon* moon = new Moon();
    strcpy(moon->name, "Touching moon");
    moon->user_added = true;
    moon->mass = 3.013387916361456e26;
    moon->volumetric_mean_radius = 2231156.60685808;
    moon->orbit = new Orbit();
    moon->orbit->center = parent;
    moon->orbit->center_name = parent->name;
    moon->orbit->semimajor_axis = parent->get_equatorial_radius() + moon->get_equatorial_radius();
    moon->orbit->compute_period(moon->mass);
    moon->cenobj = host;
    append_cel(moon);
    const json source = moon->to_json();
    json records = {{"host", host->to_json()}, {"parent", parent->to_json()}, {"moon", source}};
    for (bool fictional : {true, false})
    {
        records["moon"]["user_added"] = fictional;
        write_file("moon_clearance", records);
        delete_the_universe();
        ASSERT_TRUE(load_file("moon_clearance"));
        Moon* loaded = static_cast<Moon*>(find_loaded("Touching moon"));
        ASSERT_NE(loaded, nullptr);
        const double contact = loaded->orbit->center->get_equatorial_radius() + loaded->get_equatorial_radius();
        const double pericenter = loaded->orbit->semimajor_axis * (1 - loaded->orbit->eccentricity);
        if (fictional) EXPECT_GT(pericenter, 1.05 * contact);
        else EXPECT_NEAR(pericenter, contact, 1e-6);
    }
    delete_the_universe();
    std::filesystem::remove(temp_universe("moon_clearance"));
}

TEST_F(SerializationTest, QuijoteBugUniverseHasPositiveClearanceAfterLoading)
{
    std::ifstream input("Quijote_bug.json");
    if (!input) GTEST_SKIP() << "Local Quijote reproduction universe is absent";
    json original;
    input >> original;
    json records = json::object();
    for (const auto& entry : original.items())
    {
        const std::string name = entry.value().value("!name", "");
        if (name == "Mu Arae" || name == "Quijote" || name == "Quijote I") records[entry.key()] = entry.value();
    }
    ASSERT_EQ(records.size(), 3u);
    make_star("Sol");
    write_file("quijote_clearance", records);
    ASSERT_TRUE(load_file("quijote_clearance"));
    Moon* moon = static_cast<Moon*>(find_loaded("Quijote I"));
    ASSERT_NE(moon, nullptr);
    ASSERT_NE(moon->orbit, nullptr);
    ASSERT_STREQ(moon->orbit->center->name, "Quijote");
    const double pericenter = moon->orbit->semimajor_axis * (1 - moon->orbit->eccentricity);
    const double closest_surface = pericenter - moon->get_equatorial_radius();
    EXPECT_GT(closest_surface, moon->orbit->center->get_equatorial_radius());
    std::cout << "Quijote I repaired surface clearance: "
        << (closest_surface - moon->orbit->center->get_equatorial_radius()) / 1000 << " km" << std::endl;
    delete_the_universe();
    std::filesystem::remove(temp_universe("quijote_clearance"));
}

TEST_F(SerializationTest, LinksForwardReferencesAndFindsTheIlluminationStar)
{
    Star* host = make_star("Zulu Primary");
    Planet* world = make_planet(host, "Alpha Planet");
    json records = {{"a", world->to_json()}, {"z", host->to_json()}};
    write_file("forward_links", records);
    delete_the_universe();
    ASSERT_TRUE(load_file("forward_links"));
    CelestialObject* loaded = find_loaded("Alpha Planet");
    ASSERT_NE(loaded, nullptr);
    ASSERT_NE(loaded->orbit, nullptr);
    EXPECT_EQ(loaded->orbit->center, find_loaded("Zulu Primary"));
    EXPECT_EQ(loaded->get_light_center(), find_loaded("Zulu Primary"));
    EXPECT_EQ(loaded->cenobj, find_loaded("Zulu Primary"));
    delete_the_universe();
}

TEST_F(SerializationTest, ResolvesAParentThroughItsCatalogAlias)
{
    Star* host = make_star("Catalog Display Name");
    host->add_catalog_alias("Saved Host Identifier");
    Planet* world = make_planet(host, "Saved Planet");
    world->ensure_atmosphere()->ensure_composition()->CO2_portion = 0.9;
    json record = world->to_json();
    record["orbit"]["center_name"] = "Saved Host Identifier";
    write_file("alias_parent", {{"planet", record}});
    ASSERT_TRUE(load_file("alias_parent"));
    ASSERT_TRUE(load_file("alias_parent"));
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(world->orbit->center, host);
    EXPECT_EQ(world->get_light_center(), host);
    ASSERT_NE(world->atm, nullptr);
    ASSERT_NE(world->atm->comp, nullptr);
    EXPECT_DOUBLE_EQ(world->atm->comp->CO2_portion, 0.9);
    delete_the_universe();
}

TEST_F(SerializationTest, SavedPlanetAndMoonDistinguishStellarBFromPlanetb)
{
    for (const char* parent_name : {"Alpha Mensae", "Alpha Mensae A"})
    {
        SCOPED_TRACE(parent_name);
        Star* host = make_star("Alpha Mensae");
        host->set_component('A', host);
        Star* companion = make_star("Alp Men B");
        companion->make_companion_of(host, 'B');
        Planet* source = make_planet(host, "Alp Men b");
        json planet = source->to_json();
        planet["orbit"]["center_name"] = parent_name;
        planet["!origcenname"] = parent_name;
        Moon moon;
        strcpy(moon.name, "Alp Men b I");
        moon.orbit = new Orbit();
        moon.orbit->center_name = "Alp Men b";
        moon.orbit->semimajor_axis = 1e9;
        moon.orbit->period = 4 * oneday;
        json satellite = moon.to_json();
        satellite["orbit"]["center_name"] = "Alp Men b";
        write_file("component_case", {{"planet", planet}, {"moon", satellite}});
        cels[--ncelobjs] = nullptr;
        delete source;
        ASSERT_TRUE(load_file("component_case"));
        CelestialObject* loaded = find_loaded("Alp Men b");
        CelestialObject* loaded_moon = find_loaded("Alp Men b I");
        ASSERT_NE(loaded, nullptr);
        ASSERT_NE(loaded_moon, nullptr);
        EXPECT_EQ(loaded->orbit->center, host);
        EXPECT_EQ(loaded_moon->orbit->center, loaded);
        EXPECT_EQ(loaded_moon->get_light_center(), host);
        EXPECT_EQ(companion->orbit->center, host);
        EXPECT_EQ(host->has_planets, 1);
        EXPECT_EQ(host->pl_indices.size(), 1u);
        ASSERT_TRUE(load_file("component_case"));
        EXPECT_EQ(ncelobjs, 4);
        EXPECT_EQ(find_loaded("Alp Men b"), loaded);
        EXPECT_EQ(find_loaded("Alp Men b I"), loaded_moon);
        EXPECT_EQ(host->has_planets, 1);
        EXPECT_EQ(host->pl_indices.size(), 1u);
        delete_the_universe();
    }
    std::filesystem::remove(temp_universe("component_case"));
}

TEST_F(SerializationTest, AmbiguousFoldedOrbitParentsStillRejectTheSave)
{
    Star* first = make_star("Alias Host");
    Star* second = make_star("ALIAS HOST");
    Planet* planet = make_planet(first, "World");
    json record = planet->to_json();
    record["orbit"]["center_name"] = "alias host";
    write_file("ambiguous_case", {{"planet", record}});
    EXPECT_FALSE(load_file("ambiguous_case"));
    EXPECT_EQ(planet->orbit->center, first);
    EXPECT_STREQ(second->name, "ALIAS HOST");
    delete_the_universe();
    std::filesystem::remove(temp_universe("ambiguous_case"));
}

TEST_F(SerializationTest, LegacyMoonSurfaceTypesProduceCompleteMaps)
{
    struct RestoreRandomization
    {
        bool saved = randomize_txgen;
        ~RestoreRandomization() { randomize_txgen = saved; }
    } restore;
    randomize_txgen = false;
    for (const auto& [legacy, current] : std::vector<std::pair<int, cel_obj_type>>
        {{0x402, icy}, {0x403, rocky}, {0x404, lavaworld}})
    {
        SCOPED_TRACE(legacy);
        Star* host = make_star("Map primary");
        Planet* parent = make_planet(host, "Map parent");
        Moon source;
        strcpy(source.name, "Legacy moon");
        source.mass = .01 * earth_mass;
        source.volumetric_mean_radius = .27 * earth_radius;
        source.orbit = new Orbit();
        source.orbit->center = parent;
        source.orbit->semimajor_axis = 1e9;
        source.orbit->period = 5 * oneday;
        json record = source.to_json();
        record["type"] = legacy;
        record["temperature"] = current == lavaworld ? 1600 : 250;
        write_file("legacy_moon_map", {{"host", host->to_json()}, {"parent", parent->to_json()}, {"moon", record}});
        delete_the_universe();
        ASSERT_TRUE(load_file("legacy_moon_map"));
        Moon* moon = static_cast<Moon*>(find_loaded("Legacy moon"));
        ASSERT_NE(moon, nullptr);
        EXPECT_EQ(moon->type, current);
        EXPECT_TRUE(uses_rocky_map(moon->type));
        moon->fictitious_map_height = 32;
        moon->rnd_seed = 173;
        load_textures(moon);
        ASSERT_NE(moon->surf_map, nullptr);
        EXPECT_TRUE(moon->surf_map->is_complete());
        EXPECT_FALSE(generating_fic_texture);
        EXPECT_EQ(moon->to_json().at("type"), current);
        delete_the_universe();
    }
    std::filesystem::remove(temp_universe("legacy_moon_map"));
}

TEST_F(SerializationTest, UnknownLockedTypesClassifyWithTheirResolvedStellarHost)
{
    for (int unknown : {0x405, 0x777, -1})
    {
        SCOPED_TRACE(unknown);
        Star* host = make_star("Classification host");
        Planet* parent = make_planet(host, "Classification parent");
        Moon source;
        strcpy(source.name, "Classification moon");
        source.mass = .01 * earth_mass;
        source.volumetric_mean_radius = .27 * earth_radius;
        source.orbit = new Orbit();
        source.orbit->center = parent;
        source.orbit->semimajor_axis = 1e9;
        source.orbit->period = 5 * oneday;
        json moon_record = source.to_json(), parent_record = parent->to_json();
        for (json* record : {&moon_record, &parent_record})
        {
            (*record)["type"] = unknown;
            (*record)["lock_type"] = true;
            (*record)["temperature"] = 280;
        }
        write_file("unknown_body_type", {{"host", host->to_json()}, {"parent", parent_record}, {"moon", moon_record}});
        delete_the_universe();
        ASSERT_TRUE(load_file("unknown_body_type"));
        for (const char* name : {"Classification parent", "Classification moon"})
        {
            Planet* body = static_cast<Planet*>(find_loaded(name));
            ASSERT_NE(body, nullptr);
            EXPECT_EQ(body->type, rocky);
            EXPECT_TRUE(body->lock_type);
            EXPECT_EQ(body->classification_evidence.at("host"), "Classification host");
        }
        ASSERT_TRUE(load_file("unknown_body_type"));
        EXPECT_EQ(ncelobjs, 3);
        delete_the_universe();
    }
    std::filesystem::remove(temp_universe("unknown_body_type"));
}

TEST_F(SerializationTest, TextureDispatchClassifiesUnknownTypesOutsideJsonLoading)
{
    Planet* body = make_planet(make_star(), "Unknown texture type");
    body->type = static_cast<cel_obj_type>(0x777);
    body->lock_type = true;
    body->temperature = 280;
    body->fictitious_map_height = 32;
    body->rnd_seed = 173;
    load_textures(body);
    EXPECT_EQ(body->type, rocky);
    ASSERT_NE(body->surf_map, nullptr);
    EXPECT_TRUE(body->surf_map->is_complete());
    delete_the_universe();
    Planet without_measurements;
    without_measurements.type = static_cast<cel_obj_type>(0x777);
    without_measurements.lock_type = true;
    without_measurements.mass = without_measurements.volumetric_mean_radius = 0;
    without_measurements.classify();
    EXPECT_EQ(without_measurements.type, rocky);
    EXPECT_EQ(without_measurements.classification_evidence.at("basis"), "insufficient_planetary_properties");
}

TEST_F(SerializationTest, CurrentPlanetSurfaceTypesKeepTheirMeaning)
{
    for (cel_obj_type current : {gas_giant, ice_giant, hot_jupiter, clearskies, waterworld, hycean, icy, rocky, lavaworld})
    {
        SCOPED_TRACE(current);
        Planet original;
        original.type = current;
        Planet restored;
        ASSERT_TRUE(restored.from_json(original.to_json()));
        EXPECT_EQ(restored.type, current);
    }
}

TEST_F(SerializationTest, ResolvesAliasesDefinedLaterInTheSameSave)
{
    Star* host = make_star("Zulu Primary");
    host->add_catalog_alias("Saved Host Identifier");
    Planet* world = make_planet(host, "Alpha Planet");
    json record = world->to_json();
    record["orbit"]["center_name"] = "Saved Host Identifier";
    write_file("forward_alias", {{"a", record}, {"z", host->to_json()}});
    delete_the_universe();
    ASSERT_TRUE(load_file("forward_alias"));
    EXPECT_EQ(find_loaded("Alpha Planet")->get_light_center(), find_loaded("Zulu Primary"));
    delete_the_universe();
}

TEST_F(SerializationTest, LegacyIdentityDefaultsRemainStableAcrossRepeatedLoads)
{
    Star* host = make_star("Primary");
    Planet* world = make_planet(host, "Planet");
    json star_record = host->to_json(), planet_record = world->to_json();
    for (json* record : {&star_record, &planet_record})
    {
        record->erase("!origname");
        record->erase("!origcenname");
    }
    write_file("legacy_identity", {{"a", planet_record}, {"z", star_record}});
    delete_the_universe();
    ASSERT_TRUE(load_file("legacy_identity"));
    ASSERT_TRUE(load_file("legacy_identity"));
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(find_loaded("Planet")->get_light_center(), find_loaded("Primary"));
    delete_the_universe();
}

TEST_F(SerializationTest, SharedLoaderWorksWithoutTheDefaultSaveOrOrbitCatalog)
{
    const auto root = std::filesystem::temp_directory_path() / "alienorum_shared_json_loader";
    std::filesystem::create_directories(root);
    struct RestoreDirectory
    {
        std::filesystem::path previous = std::filesystem::current_path();
        ~RestoreDirectory() { std::filesystem::current_path(previous); }
    } restore;
    Star* host = make_star("Primary");
    Planet* world = make_planet(host, "Planet");
    write_file("shared_entry", {{"planet", world->to_json()}, {"star", host->to_json()}});
    std::filesystem::current_path(root);
    ASSERT_TRUE(load_universe(temp_universe("shared_entry")));
    ASSERT_TRUE(load_universe(temp_universe("shared_entry")));
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(world->get_light_center(), host);
    delete_the_universe();
    std::filesystem::current_path(restore.previous);
    std::filesystem::remove_all(root);
}

TEST_F(SerializationTest, ImportsOnlyTheReferencedHostFromTheLocalCache)
{
    struct CacheAccess : CatalogReader
    {
        using CatalogReader::exorow_write_line;
    };
    const auto root = std::filesystem::temp_directory_path() / "alienorum_saved_host_catalog";
    std::filesystem::create_directories(root / "catalogs");
    struct RestoreDirectory
    {
        std::filesystem::path previous = std::filesystem::current_path();
        ~RestoreDirectory() { std::filesystem::current_path(previous); }
    } restore;
    ExoRow row;
    row.hostname = "Cached Primary";
    row.pl_name = "Cached Primary b";
    row.ra = 61;
    row.dec = 20;
    row.sy_dist = 108;
    row.sy_vmag = 10.1;
    row.st_teff = 4970;
    row.st_mass = 1.1;
    row.st_rad = 1.3;
    FILE* file = fopen((root / "catalogs" / "exoplanets_stars.dat").string().c_str(), "w");
    ASSERT_NE(file, nullptr);
    CacheAccess::exorow_write_line(file, row);
    row.hostname = "Unrelated Cached Primary";
    CacheAccess::exorow_write_line(file, row);
    fclose(file);
    Planet world;
    strcpy(world.name, "Saved World");
    world.origname = world.name;
    world.mass = earth_mass;
    world.volumetric_mean_radius = earth_radius;
    Star source;
    strcpy(source.name, "Cached Primary");
    world.orbit = new Orbit();
    world.orbit->center = &source;
    world.orbit->center_name = "Cached Primary";
    world.orbit->semimajor_axis = AU;
    world.orbit->period = oneyear;
    write_file("cached_parent", {{"world", world.to_json()}});
    std::filesystem::current_path(root);
    ASSERT_TRUE(load_file("cached_parent"));
    ASSERT_EQ(ncelobjs, 2);
    CelestialObject* loaded = find_loaded("Saved World");
    Star* host = static_cast<Star*>(find_loaded("Cached Primary"));
    ASSERT_NE(loaded, nullptr);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(loaded->get_light_center(), host);
    EXPECT_DOUBLE_EQ(host->mass, 1.1 * solar_mass);
    EXPECT_DOUBLE_EQ(host->distance, 108 * parsec);
    EXPECT_EQ(find_loaded("Unrelated Cached Primary"), nullptr);
    EXPECT_TRUE(std::isfinite(static_cast<Planet*>(loaded)->viewer_reflectance_magnitude(CelestialLocation())));
    delete_the_universe();
    std::filesystem::current_path(restore.previous);
    std::filesystem::remove_all(root);
}

TEST_F(SerializationTest, MissingParentRejectsTheSaveBeforeApplyingEdits)
{
    Star* host = make_star("Existing Primary");
    Planet* world = make_planet(host, "Existing Planet");
    Orbit* original_orbit = world->orbit;
    json record = world->to_json();
    record["!name"] = "Changed Planet";
    record["orbit"]["center_name"] = "Absent Catalog Host";
    write_file("missing_parent", {{"world", record}});
    EXPECT_FALSE(load_file("missing_parent"));
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_STREQ(world->name, "Existing Planet");
    EXPECT_EQ(world->orbit, original_orbit);
    EXPECT_EQ(world->get_light_center(), host);
    delete_the_universe();
}

TEST_F(SerializationTest, InvalidRecordsDoNotPartiallyRenameLiveObjects)
{
    Star* host = make_star("Existing Primary");
    json valid = host->to_json();
    valid["!name"] = "Changed Primary";
    for (const json& invalid : {json(3), json{{"!name", "Bad"}, {"typeclass", 999}}})
    {
        write_file("invalid_record", {{"a", valid}, {"z", invalid}});
        EXPECT_FALSE(load_file("invalid_record"));
        EXPECT_STREQ(host->name, "Existing Primary");
        EXPECT_EQ(ncelobjs, 1);
    }
    write_file("invalid_root", json::array());
    EXPECT_FALSE(load_file("invalid_root"));
    delete_the_universe();
}

TEST_F(SerializationTest, CircularParentsAreRejectedBeforeInstallingLinks)
{
    Star* host = make_star("Existing Primary");
    Planet* first = make_planet(host, "First");
    Planet* second = make_planet(host, "Second");
    json a = first->to_json(), b = second->to_json();
    a["orbit"]["center_name"] = "Second";
    b["orbit"]["center_name"] = "First";
    write_file("circular_parents", {{"a", a}, {"b", b}});
    EXPECT_FALSE(load_file("circular_parents"));
    EXPECT_EQ(first->orbit->center, host);
    EXPECT_EQ(second->orbit->center, host);
    delete_the_universe();
}

TEST_F(SerializationTest, CapacityChecksAlsoCoverOrbitlessObjects)
{
    Star* host = make_star("Existing Primary");
    json a = host->to_json(), b = a;
    a["!name"] = a["!origname"] = "New Primary A";
    b["!name"] = b["!origname"] = "New Primary B";
    write_file("capacity", {{"a", a}, {"b", b}});
    std::fstream input(temp_universe("capacity"), std::ios::in);
    EXPECT_FALSE(Serialization::load_all(input, cels, 3));
    EXPECT_EQ(ncelobjs, 1);
    EXPECT_EQ(cels[1], nullptr);
    delete_the_universe();
}

TEST_F(SerializationTest, LongStellarFieldsAreBoundedDuringImport)
{
    Star* host = make_star("Existing Primary");
    json record = host->to_json();
    for (const char* key : {"spectral_type", "Bayer", "Flamsteed", "Gliese", "Bonn_survey"})
        record[key] = std::string(2048, 'A');
    write_file("long_stellar_fields", {{"star", record}});
    ASSERT_TRUE(load_file("long_stellar_fields"));
    EXPECT_LT(strlen(host->spectral_type), sizeof(host->spectral_type));
    EXPECT_LT(strlen(host->Gliese), sizeof(host->Gliese));
    EXPECT_LT(strlen(host->Bonn_survey), sizeof(host->Bonn_survey));
    delete_the_universe();
}

TEST_F(SerializationTest, BrokenAndCircularIlluminationChainsRemainSafe)
{
    Star* host = make_star("Primary");
    Planet* world = make_planet(host);
    Planet* moon = make_planet(host, "Nested Body");
    moon->orbit->center = world;
    delete world->orbit;
    world->orbit = nullptr;
    EXPECT_EQ(moon->get_light_center(), nullptr);
    EXPECT_DOUBLE_EQ(moon->mean_instellation(), 0);
    EXPECT_TRUE(std::isinf(moon->viewer_reflectance_magnitude(CelestialLocation())));
    world->orbit = new Orbit();
    world->orbit->center = moon;
    EXPECT_EQ(moon->get_light_center(), nullptr);
    world->orbit->center = host;
    EXPECT_EQ(moon->get_light_center(), host);
    Planet* last = moon;
    for (int i = 0; i < 8; ++i)
    {
        Planet* nested = make_planet(host, ("Nested " + std::to_string(i)).c_str());
        nested->orbit->center = last;
        last = nested;
    }
    EXPECT_EQ(last->get_light_center(), host);
    delete_the_universe();
}

// =====================================================================
// Whole-universe round trip
// =====================================================================

TEST_F(SerializationTest, RoundTripPreservesHierarchyAndFields)
{
    Star* sun = make_star("Test Primary");
    sun->HD = 12345;
    sun->has_planets = 2;
    sun->has_hz_planets = 1;

    Planet* world = make_planet(sun, "Test World", AU);
    world->orbit->eccentricity = 0.0167;
    world->orbit->inclination = 0.5;
    world->orbit->ascending_node = 1.25;
    world->orbit->arg_periapsis = 2.5;
    world->temperature = 288;
    world->albedo = 0.306;
    world->asteroid_no = 4001;
    world->lock_type = true;
    world->ensure_atmosphere()->surface_pressure = 101325;

    Moon* luna = new Moon();
    strcpy(luna->name, "Test Moon");
    luna->namelen = 0;
    luna->mass = 7.342e25;
    luna->volumetric_mean_radius = 1.737e6;
    luna->height = 3.4e6;
    luna->type = rocky;
    luna->orbit = new Orbit();
    luna->orbit->center = world;
    luna->orbit->center_name = world->name;
    luna->orbit->semimajor_axis = 3.844e8;
    luna->cenobj = sun;
    append_cel(luna);

    ASSERT_TRUE(save_and_reload("roundtrip"));
    EXPECT_EQ(ncelobjs, 3);

    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->typeclass(), class_star);
    EXPECT_EQ(rsun->HD, 12345);
    EXPECT_DOUBLE_EQ(rsun->mass, solar_mass);
    EXPECT_DOUBLE_EQ(rsun->temperature, sun_temp);
    EXPECT_EQ(rsun->has_planets, 0); // This round-trip fixture marks its only body as an asteroid.
    EXPECT_EQ(rsun->has_hz_planets, 0);
    EXPECT_TRUE(rsun->pl_indices.empty());

    Planet* rworld = (Planet*)find_loaded("Test World");
    ASSERT_NE(rworld, nullptr);
    EXPECT_EQ(rworld->typeclass(), class_planet);
    ASSERT_NE(rworld->orbit, nullptr);
    EXPECT_EQ(rworld->orbit->center, rsun);                 // relinked by name, not by pointer
    EXPECT_DOUBLE_EQ(rworld->orbit->semimajor_axis, AU);
    EXPECT_NEAR(rworld->orbit->eccentricity, 0.0167, 1e-12);
    EXPECT_NEAR(rworld->orbit->inclination, 0.5, 1e-12);    // radians out, degrees in the file
    EXPECT_NEAR(rworld->orbit->ascending_node, 1.25, 1e-12);
    EXPECT_NEAR(rworld->orbit->arg_periapsis, 2.5, 1e-12);
    EXPECT_DOUBLE_EQ(rworld->temperature, 288);
    EXPECT_DOUBLE_EQ(rworld->albedo, 0.306);
    EXPECT_EQ(rworld->asteroid_no, 4001);
    EXPECT_TRUE(rworld->lock_type);
    ASSERT_NE(rworld->atm, nullptr);
    EXPECT_DOUBLE_EQ(rworld->get_surface_pressure(), 101325);

    Moon* rluna = (Moon*)find_loaded("Test Moon");
    ASSERT_NE(rluna, nullptr);
    EXPECT_EQ(rluna->typeclass(), class_moon);
    ASSERT_NE(rluna->orbit, nullptr);
    EXPECT_EQ(rluna->orbit->center, rworld);                // two levels down, still linked
    EXPECT_DOUBLE_EQ(rluna->height, 3.4e6);

    delete_the_universe();
}

TEST_F(SerializationTest, RoundTripKeepsComets)
{
    Star* sun = make_star("Test Primary");

    Comet* c = new Comet();
    strcpy(c->name, "Test Comet");
    c->namelen = 0;
    c->designation = "C/2024 T1";
    c->H1 = 5.5; c->R1 = 10.0; c->D1 = 5.0;
    c->orbit = new Orbit();
    c->orbit->center = sun;
    c->orbit->center_name = sun->name;
    c->orbit->eccentricity = 1.2;                           // hyperbolic: a visitor
    c->orbit->periapsis_distance = 0.5 * AU;
    c->cenobj = sun;
    append_cel(c);

    ASSERT_TRUE(save_and_reload("comet"));

    Comet* rc = (Comet*)find_loaded("Test Comet");
    ASSERT_NE(rc, nullptr);
    EXPECT_EQ(rc->typeclass(), class_comet);
    EXPECT_EQ(rc->designation, "C/2024 T1");
    EXPECT_DOUBLE_EQ(rc->H1, 5.5);
    EXPECT_DOUBLE_EQ(rc->R1, 10.0);
    ASSERT_NE(rc->orbit, nullptr);
    EXPECT_TRUE(rc->orbit->is_open());

    delete_the_universe();
}

// =====================================================================
// The load loop's bookkeeping
// =====================================================================

TEST_F(SerializationTest, LoadKeepsAnObjectThatHasNoOrbit)
{
    Star* sun = make_star("Lone Star");
    make_planet(sun, "First Planet", AU);
    make_planet(sun, "Second Planet", 2*AU);
    make_planet(sun, "Third Planet", 3*AU);
    EXPECT_EQ(ncelobjs, 4);

    ASSERT_TRUE(save_and_reload("orbitless"));

    EXPECT_EQ(ncelobjs, 4);
    EXPECT_NE(find_loaded("Lone Star"), nullptr);
    EXPECT_NE(find_loaded("First Planet"), nullptr);
    EXPECT_NE(find_loaded("Second Planet"), nullptr);
    EXPECT_NE(find_loaded("Third Planet"), nullptr);

    delete_the_universe();
}

TEST_F(SerializationTest, LoadUpdatesAMatchingObjectInPlace)
{
    Star* sun = make_star("Test Primary");
    sun->absolute_magnitude = 4.83;
    ASSERT_TRUE(save_and_reload("inplace"));
    ASSERT_EQ(ncelobjs, 1);

    // Load the same file a second time on top of what it just produced.
    ASSERT_TRUE(load_file("inplace"));
    EXPECT_EQ(ncelobjs, 1) << "the same object loaded twice should not appear twice";

    delete_the_universe();
}

TEST_F(SerializationTest, LoadDoesNotWriteStarFieldsThroughSomethingThatIsNotAStar)
{
    Star* sun = make_star("Test Primary");
    Planet* world = make_planet(sun, "Test World", AU);

    Moon* stray = new Moon();
    strcpy(stray->name, "Stray Moon");
    stray->namelen = 0;
    stray->mass = 7.342e25;
    stray->volumetric_mean_radius = 1.737e6;
    // type deliberately left at the default, so it is its own light center.
    stray->orbit = new Orbit();
    stray->orbit->center = world;
    stray->orbit->center_name = world->name;
    stray->orbit->semimajor_axis = 3.844e8;
    stray->cenobj = sun;
    append_cel(stray);

    ASSERT_TRUE(save_and_reload("nonstar"));
    EXPECT_EQ(ncelobjs, 3);

    // Nothing was credited to the moon, and the star kept the count it was counted for.
    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->has_planets, 1) << "the planet, and not the stray moon as well";

    delete_the_universe();
}

// =====================================================================
// The planet tallies
// =====================================================================

TEST_F(SerializationTest, LoadDoesNotDoubleAStatedPlanetTally)
{
    Star* sun = make_star("Test Primary");
    make_planet(sun, "Planet One", AU);
    make_planet(sun, "Planet Two", 2*AU);
    make_planet(sun, "Planet Three", 3*AU);
    sun->has_planets = 3;
    sun->has_hz_planets = 1;

    ASSERT_TRUE(save_and_reload("tally"));

    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->has_planets, 3) << "the file's tally plus a recount would be six";
    EXPECT_EQ(rsun->has_hz_planets, 1);
    EXPECT_EQ(rsun->pl_indices.size(), 3u);

    delete_the_universe();
}

TEST_F(SerializationTest, StaleSavedTalliesAndOrbitChangesRebuildFromTheLoadedPlanets)
{
    Star* sun = make_star("Saved tally host");
    Planet* planet = make_planet(sun, "Saved tally planet", AU);
    json host = sun->to_json(), body = planet->to_json();
    host["has_planets"] = 42;
    host["has_hz_planets"] = 37;
    write_file("stale_tally", {{"host", host}, {"planet", body}});
    ASSERT_TRUE(load_file("stale_tally"));
    EXPECT_EQ(sun->has_planets, 1);
    EXPECT_EQ(sun->has_hz_planets, 1);
    EXPECT_EQ(sun->pl_indices, std::vector<int>{planet->seqno});
    body["orbit"]["semimajor_axis"] = 4 * AU;
    write_file("stale_tally", {{"host", host}, {"planet", body}});
    ASSERT_TRUE(load_file("stale_tally"));
    EXPECT_EQ(sun->has_planets, 1);
    EXPECT_EQ(sun->has_hz_planets, 0);
    EXPECT_EQ(sun->pl_indices.size(), 1u);
    delete_the_universe();
    std::filesystem::remove(temp_universe("stale_tally"));
}

TEST_F(SerializationTest, LoadCountsTheTallyWhenTheFileStatesNone)
{
    Star* sun = make_star("Test Primary");
    make_planet(sun, "Planet One", AU);
    make_planet(sun, "Planet Two", 2*AU);
    sun->has_planets = 2;

    std::string path = temp_universe("oldtally");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();
    delete_the_universe();

    json j = read_file("oldtally");
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        it.value().erase("has_planets");
        it.value().erase("has_hz_planets");
    }
    write_file("oldtally", j);

    ASSERT_TRUE(load_file("oldtally"));
    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_EQ(rsun->has_planets, 2) << "with nothing stated, the planets in the file are counted";

    delete_the_universe();
}

// =====================================================================
// Fields that used to be lost or mangled in the trip
// =====================================================================

TEST_F(SerializationTest, ZeroPrecessionSurvivesAsZeroAndNotInfinity)
{
    Star* sun = make_star("Test Primary");
    EXPECT_DOUBLE_EQ(sun->precession, 0);

    std::string path = temp_universe("precession");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();

    json j = read_file("precession");
    ASSERT_TRUE(j.begin().value().contains("precession"));
    EXPECT_FALSE(j.begin().value()["precession"].is_null()) << "a null here is a divide by zero";
    EXPECT_DOUBLE_EQ(j.begin().value()["precession"].get<double>(), 0);

    delete_the_universe();
    ASSERT_TRUE(load_file("precession"));
    Star* rsun = (Star*)find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_FALSE(std::isinf(rsun->precession));
    EXPECT_FALSE(std::isnan(rsun->precession));
    EXPECT_DOUBLE_EQ(rsun->precession, 0);

    delete_the_universe();
}

TEST_F(SerializationTest, AStatedPrecessionPeriodRoundTrips)
{
    Star* sun = make_star("Test Primary");
    sun->precession = _pi * 2 / (25772 * oneyear);          // Earth's, in radians per second

    std::string path = temp_universe("precession2");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();

    json j = read_file("precession2");
    EXPECT_NEAR(j.begin().value()["precession"].get<double>(), 25772, 1e-6) << "written in years";

    delete_the_universe();
    ASSERT_TRUE(load_file("precession2"));
    EXPECT_NEAR(find_loaded("Test Primary")->precession, _pi * 2 / (25772 * oneyear), 1e-20);

    delete_the_universe();
}

TEST_F(SerializationTest, OldFileStatingZeroPrecessionLoadsAsZero)
{
    // 37 of the 38 objects in Koora.json state exactly this, from before the guard existed.
    make_star("Test Primary");
    std::string path = temp_universe("oldprecession");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, false));
    out.close();
    delete_the_universe();

    json j = read_file("oldprecession");
    for (auto it = j.begin(); it != j.end(); ++it) it.value()["precession"] = 0.0;
    write_file("oldprecession", j);

    ASSERT_TRUE(load_file("oldprecession"));
    CelestialObject* rsun = find_loaded("Test Primary");
    ASSERT_NE(rsun, nullptr);
    EXPECT_FALSE(std::isinf(rsun->precession));
    EXPECT_DOUBLE_EQ(rsun->precession, 0);

    delete_the_universe();
}

// =====================================================================
// Saving only what was edited
// =====================================================================

TEST_F(SerializationTest, StreamedSaveMatchesFormattedDocumentBytes)
{
    Star* host = make_star("Streaming primary");
    host->user_edited = true;
    Planet* planet = make_planet(host, "Streaming planet");
    planet->user_added = true;
    host->stellar_system["format_probe"] = {{"unicode", "é Ω"}, {"escape", "line\n\t\""}, {"null", nullptr}};
    for (bool edited : {false, true})
    {
        const std::string path = temp_universe("stream_bytes");
        std::fstream output(path, std::ios::out | std::ios::trunc);
        json reference;
        ASSERT_TRUE(Serialization::save_one(output, host, reference));
        ASSERT_TRUE(Serialization::save_one(output, planet, reference));
        ASSERT_TRUE(Serialization::save_all(output, cels, edited));
        output.close();
        std::ifstream input(path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        EXPECT_EQ(bytes, reference.dump(4));
        std::remove(path.c_str());
    }
    delete_the_universe();
    const std::string path = temp_universe("stream_empty");
    std::fstream output(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(output, cels, true));
    output.close();
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(bytes, "null");
    std::remove(path.c_str());
}

TEST_F(SerializationTest, OnlyEditedWritesOnlyTheEditedObjects)
{
    // This is the mode the program actually saves in (loaders.cpp), so that a universe file holds
    // the user's own work and not a copy of every catalog it happened to have open.
    Star* kept = make_star("Edited Star");
    kept->user_edited = true;
    Star* skipped = make_star("Catalog Star");
    skipped->user_edited = false;

    std::string path = temp_universe("onlyedited");
    std::fstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(Serialization::save_all(out, cels, true));
    out.close();

    json j = read_file("onlyedited");
    EXPECT_EQ(j.size(), 1);
    EXPECT_TRUE(j.contains("Edited Star"));
    EXPECT_FALSE(j.contains("Catalog Star"));

    delete_the_universe();
}

// =====================================================================
// find_object()
// =====================================================================

class FindObjectTest : public UniverseFixture {};

TEST_F(FindObjectTest, PublishedWdAliasesAttachToExistingCatalogStars)
{
    Star *eri_primary = make_star("GJ 166 A");
    strcpy(eri_primary->Gliese, "GJ 166 A");
    Star *sirius = make_star("Sirius");
    strcpy(sirius->Gliese, "GJ 244 A");
    const char *aliases[] = {"WD0848+159", "WD1328+648", "WD2354-211", "WD0413-077", ""};
    const char *queries[] = {"WD0851+1543", "WD1330+6435", "WD2356-2054", "WD0415-0739", "WD0645-1643"};
    const char *gaia[] = {"WDJ085100.24+154302.04", "WDJ133001.17+643523.69", "WDJ235645.10-205449.94",
        "WDJ041521.80-073929.20", "WDJ064509.30-164300.72"};
    const unsigned int colors[] = {0xf1fff8, 0xddffb8, 0x70b4ff, 0xaecaff, 0xa4c1ff};
    std::vector<Star*> targets;
    for (int i = 0; i < 5; ++i)
    {
        Star *s = make_star(("Catalog WD " + std::to_string(i)).c_str());
        s->WD = aliases[i];
        s->apparent_magnitude = 11 + i;
        s->right_ascension = 0.5 + i;
        targets.push_back(s);
    }
    strcpy(targets[3]->Gliese, "GJ 166 B");
    strcpy(targets[4]->Gliese, "GJ 244 B");
    eri_primary->set_component('A', eri_primary);
    targets[3]->set_component('B', eri_primary);
    sirius->set_component('A', sirius);
    targets[4]->set_component('B', sirius);
    Orbit *orbit = targets[4]->orbit;
    ASSERT_EQ(associate_wd_color_identifiers(cels), 5);
    EXPECT_EQ(associate_wd_color_identifiers(cels), 5);
    for (int i = 0; i < 5; ++i)
    {
        Star *s = targets[i];
        EXPECT_EQ(find_object(queries[i], true), s->seqno);
        EXPECT_EQ(find_object(gaia[i], true), s->seqno);
        if (*aliases[i]) EXPECT_EQ(find_object(aliases[i], true), s->seqno);
        EXPECT_TRUE(s->wd_color_valid);
        EXPECT_EQ((unsigned(s->wd_color_red) << 16) | (unsigned(s->wd_color_green) << 8) | s->wd_color_blue, colors[i]);
        EXPECT_DOUBLE_EQ(s->apparent_magnitude, 11 + i);
        EXPECT_DOUBLE_EQ(s->right_ascension, 0.5 + i);
        Star copy;
        ASSERT_TRUE(copy.from_json(s->to_json()));
        EXPECT_EQ(copy.gaia_wd_name, gaia[i]);
        EXPECT_TRUE(copy.wd_color_valid);
    }
    EXPECT_EQ(targets[4]->orbit, orbit);
    EXPECT_EQ(find_object("WD1330+643", true), targets[1]->seqno);
    EXPECT_EQ(targets[4]->orbit->center, sirius);
    EXPECT_FALSE(sirius->wd_color_valid);
    EXPECT_TRUE(sirius->gaia_wd_name.empty());
    EXPECT_FALSE(eri_primary->wd_color_valid);
    EXPECT_EQ(find_object("WD0039+3035", true), -1);
    delete_the_universe();
}

TEST_F(FindObjectTest, PublishedWdAliasesResolveBeforeColorAssociation)
{
    Star* legacy = make_star("WD1328+648");
    const std::string gaia = wd_color_gaia_identifier(legacy->name);
    ASSERT_FALSE(gaia.empty());
    ExactStarSearchIndex lookup;
    EXPECT_EQ(find_object("WD 1328+648", true), legacy->seqno);
    EXPECT_EQ(find_object("WD 1328+648", true, 9e29, 1, &lookup), legacy->seqno);
    EXPECT_EQ(find_object("WD1330+643", true), legacy->seqno);
    EXPECT_EQ(find_object("WD 1330+6435", true, 9e29, 1, &lookup), legacy->seqno);
    EXPECT_EQ(find_object(gaia.c_str(), true, 9e29, 1, &lookup), legacy->seqno);
    Star* explicit_record = make_star("Explicit Gaia record");
    explicit_record->gaia_wd_name = gaia;
    EXPECT_EQ(find_object("WD 1328+648", true), explicit_record->seqno);
    EXPECT_EQ(find_object("WD1330+643", true), explicit_record->seqno);
    explicit_record->gaia_wd_name = "WDJ100000.00+100000.00";
    legacy->gaia_wd_name = "WDJ200000.00+200000.00";
    EXPECT_EQ(find_object("WD 1328+648", true), -1);
    legacy->gaia_wd_name.clear();
    explicit_record->gaia_wd_name.clear();
    legacy->WD = legacy->name;
    explicit_record->WD = legacy->name;
    EXPECT_EQ(find_object("WD 1328+648", true), -1);
    delete_the_universe();
}

TEST_F(FindObjectTest, LegacyCoordinatesOutrankACollidingPublishedCoordinateLabel)
{
    Star* legacy = make_star("Legacy coordinate identity");
    legacy->WD = "WD1330+643";
    const std::string gaia = wd_color_gaia_identifier("WD1328+648");
    ASSERT_FALSE(gaia.empty());
    EXPECT_EQ(find_object("WD1330+643", true), legacy->seqno);
    EXPECT_EQ(find_object(gaia.c_str(), true), -1);
    Star* published = make_star("Published coordinate identity");
    published->WD = "WD1328+648";
    published->gaia_wd_name = gaia;
    ExactStarSearchIndex lookup;
    EXPECT_EQ(find_object("WD1330+643", true, 9e29, 1, &lookup), legacy->seqno);
    EXPECT_EQ(find_object(gaia.c_str(), true, 9e29, 1, &lookup), published->seqno);
    delete_the_universe();
}

TEST_F(FindObjectTest, PublishedWdAssociationsRejectConflictingComponents)
{
    Star *first = make_star("First component");
    strcpy(first->Gliese, "GJ 166 B");
    Star *second = make_star("Other component");
    second->WD = "WD0413-077";
    EXPECT_EQ(associate_wd_color_identifiers(cels), 0);
    EXPECT_TRUE(first->gaia_wd_name.empty());
    EXPECT_TRUE(second->gaia_wd_name.empty());
    delete_the_universe();
}

TEST_F(FindObjectTest, PublishedWdAssociationsPreferExactGaiaOverAnIsolatedDuplicate)
{
    Star *gaia = make_star("Gaia WD");
    gaia->WD = "WD2359-434";
    gaia->gaia_wd_name = "WDJ000210.72-430955.39";
    Star *duplicate = make_star("GJ 915");
    strcpy(duplicate->Gliese, "GJ 915");
    EXPECT_EQ(associate_wd_color_identifiers(cels), 1);
    EXPECT_EQ(find_object("WD0002-4309", true), gaia->seqno);
    EXPECT_TRUE(gaia->wd_color_valid);
    EXPECT_FALSE(duplicate->wd_color_valid);
    EXPECT_TRUE(duplicate->gaia_wd_name.empty());
    delete_the_universe();
}

TEST_F(FindObjectTest, PublishedWdAssociationsPreserveAnExplicitBinaryComponent)
{
    Star *primary = make_star("GJ 288 A");
    Star *component = make_star("GJ 288 B");
    strcpy(component->Gliese, "GJ 288 B");
    primary->set_component('A', primary);
    component->set_component('B', primary);
    Star *duplicate = make_star("WD0743-336");
    duplicate->WD = "WD0743-336";
    Orbit *orbit = component->orbit;
    EXPECT_EQ(associate_wd_color_identifiers(cels), 1);
    EXPECT_EQ(find_object("WD0745-3355", true), component->seqno);
    EXPECT_TRUE(component->wd_color_valid);
    EXPECT_EQ(component->orbit, orbit);
    EXPECT_EQ(component->orbit->center, primary);
    EXPECT_TRUE(duplicate->gaia_wd_name.empty());
    delete_the_universe();
}

TEST_F(FindObjectTest, WdSearchAcceptsBothDeclinationPrecisions)
{
    Star *short_id = make_star("Truncated WD");
    short_id->WD = "WD1234+567";
    Star *full_id = make_star("Full WD");
    full_id->WD = "WD2345-0567";
    Star *component = make_star("WD component");
    component->WD = "WD1234+567.1";
    EXPECT_EQ(find_object("WD1234+5678", true), short_id->seqno);
    EXPECT_EQ(find_object("wd 1234+567", true), short_id->seqno);
    EXPECT_EQ(find_object("WD2345-056", true), full_id->seqno);
    EXPECT_EQ(find_object("WD2345-0567", true), full_id->seqno);
    EXPECT_EQ(find_object("WD1234+5678.1", true), component->seqno);
    EXPECT_EQ(find_object("WD1234+5678.2", true), -1);
    EXPECT_EQ(find_object("WD1234-5678", true), -1);
    delete_the_universe();
}

TEST_F(FindObjectTest, WdSearchRejectsAmbiguousTruncatedIdentifiers)
{
    Star *first = make_star("First WD");
    first->WD = "WD1234+5678";
    Star *second = make_star("Second WD");
    second->WD = "WD1234+5679";
    EXPECT_EQ(find_object("WD1234+567", true), -1);
    delete_the_universe();
}

TEST_F(FindObjectTest, ShortAndNumberedBayerNamesUseGreekIdentity)
{
    auto saved_constellations = std::move(constellations);
    constellations.clear();
    Constellation tau;
    tau.name = "Taurus";
    tau.abbrev = "Tau";
    const char *queries[] =
    {
        "Mu Tau", "Nu  Tau", "Xi Tau", "Pi  Tau", "Gam2Tau"
    };
    const int greek_indices[] =
    {
        11, 12, 13, 15, 122
    };
    for (int i = 0; i < 5; i++)
    {
        Star *s = make_star(("Identifier " + std::to_string(i)).c_str());
        strcpy(s->constellation, "Tau");
        s->BayerGrkno = greek_indices[i];
        tau.Bayer_stars[greek_indices[i]] = s;
    }
    constellations.push_back(tau);
    for (int i = 0; i < 5; i++)
    {
        EXPECT_EQ(find_object(queries[i], true), i) << queries[i];
    }
    constellations = std::move(saved_constellations);
    delete_the_universe();
}

TEST_F(FindObjectTest, NuAndNUOrionisRemainDistinctForConstellationLines)
{
    auto saved_constellations = std::move(constellations);
    constellations.clear();
    Constellation ori;
    ori.name = "Orion";
    ori.abbrev = "Ori";
    ori.vantage_name = "Sun";
    Star *variable = make_star("NU Orionis");
    strcpy(variable->constellation, "Ori");
    variable->declination = -5.26 * fiftyseventh;
    Star *greek = make_star("Nu Orionis");
    strcpy(greek->constellation, "Ori");
    strcpy(greek->Bayer, "Nu  Ori");
    greek->BayerGrkno = 12;
    greek->declination = 14.77 * fiftyseventh;
    ori.Bayer_stars[12] = greek;
    ConsLine line;
    line.starnamea = "Nu  Ori";
    line.starnameb = "NU Orionis";
    ori.lines.push_back(line);
    constellations.push_back(ori);
    EXPECT_EQ(find_object("Nu Ori", true), greek->seqno);
    EXPECT_EQ(find_object("NU Orionis", true), variable->seqno);
    EXPECT_EQ(find_object("NU Ori", true), -1);
    cache_cons_lines();
    EXPECT_EQ(constellations[0].lines[0].a, greek);
    EXPECT_EQ(constellations[0].lines[0].b, variable);
    constellation_index["Ori"] = {variable, greek};
    cache_cons_lines();
    EXPECT_EQ(constellations[0].lines[0].a, greek);
    EXPECT_EQ(constellations[0].lines[0].b, variable);
    constellations = std::move(saved_constellations);
    delete_the_universe();
}

TEST_F(FindObjectTest, MissingBayerIdentityDoesNotSelectAnotherGreekLetter)
{
    auto saved_constellations = std::move(constellations);
    constellations.clear();
    Constellation tau;
    tau.name = "Taurus";
    tau.abbrev = "Tau";
    Star *pi = make_star("Pi Tauri");
    pi->apparent_magnitude = 4.69;
    strcpy(pi->constellation, "Tau");
    strcpy(pi->Bayer, "Pi  Tau");
    pi->BayerGrkno = 15;
    tau.Bayer_stars[15] = pi;
    constellations.push_back(tau);
    EXPECT_EQ(find_object("Xi  Tau", true), -1);
    EXPECT_EQ(find_object("Xi Tau", true), -1);
    constellations = std::move(saved_constellations);
    delete_the_universe();
}

TEST_F(FindObjectTest, NumberedBayerNamesAcceptCatalogAndCompactSpacing)
{
    auto saved_constellations = std::move(constellations);
    constellations.clear();
    Constellation tau;
    tau.name = "Taurus";
    tau.abbrev = "Tau";
    for (int letter = 0; letter < 24; letter++)
    {
        for (int number = 1; number <= 9; number++)
        {
            Star *s = make_star(("Identifier " + std::to_string(letter) + " " + std::to_string(number)).c_str());
            strcpy(s->constellation, "Tau");
            s->BayerGrkno = 100 + 10 * letter + number;
            tau.Bayer_stars[s->BayerGrkno] = s;
        }
    }
    constellations.push_back(tau);
    for (int pass = 0; pass < 2; pass++)
    {
        for (int letter = 0; letter < 24; letter++)
        {
            const std::string prefix = Greek_letter[letter].substr(0, 3);
            for (int number = 1; number <= 9; number++)
            {
                const std::string digit = std::to_string(number);
                const int expected = letter * 9 + number - 1;
                EXPECT_EQ(find_object((prefix + digit + " Tau").c_str(), true), expected);
                EXPECT_EQ(find_object((prefix + " " + digit + "Tau").c_str(), true), expected);
                EXPECT_EQ(find_object((prefix + " " + digit + " Tau").c_str(), true), expected);
            }
        }
        // Reloaded constellation definitions have no populated lookup maps.
        constellations[0].Bayer_stars.clear();
    }
    constellations = std::move(saved_constellations);
    delete_the_universe();
}

TEST_F(FindObjectTest, ExplicitConstellationEndpointsResolveBelowTheDisplayLimit)
{
    auto saved_constellations = std::move(constellations);
    constellations.clear();
    Constellation cha;
    cha.name = "Chamaeleon";
    cha.abbrev = "Cha";
    cha.vantage_name = "Sun";
    Star *mu1 = make_star("Mu 1 Chamaeleontis");
    strcpy(mu1->constellation, "Cha");
    strcpy(mu1->Bayer, "Mu 1Cha");
    mu1->BayerGrkno = 211;
    mu1->apparent_magnitude = 5.53;
    Star *mu2 = make_star("Mu 2 Chamaeleontis");
    strcpy(mu2->constellation, "Cha");
    strcpy(mu2->Bayer, "Mu 2Cha");
    mu2->BayerGrkno = 212;
    mu2->apparent_magnitude = 6.60;
    Star *faint = make_star("Faint endpoint");
    strcpy(faint->constellation, "Cha");
    strcpy(faint->Flamsteed, "100 Cha");
    faint->FlamsteedNo = 100;
    faint->apparent_magnitude = 8;
    ConsLine first;
    first.starnamea = "Mu1 Cha";
    first.starnameb = "Mu2 Cha";
    cha.lines.push_back(first);
    ConsLine second;
    second.starnamea = "Mu2 Cha";
    second.starnameb = "100 Cha";
    cha.lines.push_back(second);
    constellations.push_back(cha);

    // Rebuilt definitions have empty Greek-letter lookup maps.
    EXPECT_EQ(find_object("Mu2 Cha", true, 6.5, 1), -1);
    cache_cons_lines();
    EXPECT_EQ(constellations[0].lines[0].a, mu1);
    EXPECT_EQ(constellations[0].lines[0].b, mu2);
    EXPECT_EQ(constellations[0].lines[1].a, mu2);
    EXPECT_EQ(constellations[0].lines[1].b, faint);

    // Exercise the local constellation index as well as the general lookup fallback.
    constellation_index["Cha"].push_back(mu1);
    constellation_index["Cha"].push_back(mu2);
    constellation_index["Cha"].push_back(faint);
    cache_cons_lines();
    EXPECT_EQ(constellations[0].lines[0].b, mu2);
    EXPECT_EQ(constellations[0].lines[1].b, faint);
    constellations = std::move(saved_constellations);
    delete_the_universe();
}

TEST_F(FindObjectTest, CachedHDCompanionsResolveBeforeMembershipIsRebuilt)
{
    Star **saved_cache = hdcache;
    std::vector<Star*> cache(MAX_HD + 1, nullptr);
    hdcache = cache.data();
    Star *primary = make_star("Xi Ursae Majoris");
    primary->HD = 98231;
    hdcache[98231] = primary;
    Star *companion = make_star("HD98231 C");
    companion->orbit = new Orbit();
    companion->orbit->center = primary;
    EXPECT_EQ(primary->multisys, nullptr);
    EXPECT_EQ(find_object("HD98231 C", true), companion->seqno);
    EXPECT_EQ(find_object("HD98231 D", true), -1);
    hdcache = saved_cache;
    delete_the_universe();
}

TEST_F(FindObjectTest, ExactNameWins)
{
    Star* a = make_star("Alpha Centauri");
    Star* b = make_star("Beta Centauri");
    a->apparent_magnitude = 0;
    b->apparent_magnitude = 0.6;

    EXPECT_EQ(find_object("Alpha Centauri"), a->seqno);
    EXPECT_EQ(find_object("Beta Centauri"), b->seqno);

    delete_the_universe();
}

TEST_F(FindObjectTest, WhiteDwarfAliasesSurviveJsonAndRejectAmbiguity)
{
    Star *target = make_star("WD1008+290");
    target->WD = "WD1008+290";
    target->add_catalog_alias("WD1011+2845");
    target->add_catalog_alias("LHS2229");
    EXPECT_EQ(find_object("LHS 2229", true), target->seqno);
    EXPECT_EQ(find_object("WD1011+2845", true), target->seqno);
    const auto data = target->to_json();
    Star restored;
    restored.from_json(data);
    EXPECT_EQ(restored.catalog_aliases, target->catalog_aliases);
    Star *other = make_star("Other WD");
    other->add_catalog_alias("LHS2229");
    EXPECT_EQ(find_object("LHS 2229", true), -1);
    Star *canonical = make_star("LHS2229");
    EXPECT_EQ(find_object("LHS2229", true), canonical->seqno);
    delete_the_universe();
}

TEST_F(FindObjectTest, ToleratesAMisspelling)
{
    Star* a = make_star("Betelgeuse");
    a->apparent_magnitude = 0.5;

    // Damerau-Levenshtein: one transposition and one substitution, inside the default requirement.
    EXPECT_EQ(find_object("Betelgeuze"), a->seqno);

    delete_the_universe();
}

TEST_F(FindObjectTest, ExactAliasSearchPreservesCaseFoldingAndRejectsTypos)
{
    Star *target = make_star("Stored Display");
    target->local_name = "Orbit Alias";
    strcpy(target->Bayer, "Alias Bayer");
    strcpy(target->Flamsteed, "Alias Number");
    EXPECT_EQ(find_object("oRbIt aLiAs", true, 9e29, 1), target->seqno);
    EXPECT_EQ(find_object("alias bayer", true, 9e29, 1), target->seqno);
    EXPECT_EQ(find_object("alias number", true, 9e29, 1), target->seqno);
    EXPECT_EQ(find_object("stored display", true, 9e29, 1), target->seqno);
    EXPECT_EQ(find_object("Orbit Aliaz", true, 9e29, 1), -1);
    EXPECT_EQ(find_object("Orbit Aliaz", true), target->seqno);
    delete_the_universe();
}

TEST_F(FindObjectTest, IndexedExactSearchMatchesReferenceAndTracksAddedAndRenamedStars)
{
    Star *first = make_star("Stored Display Name");
    first->local_name = "Orbit Alias";
    strcpy(first->Gliese, "GJ 234.5");
    strcpy(first->Bayer, "Alias Bayer");
    first->alienorumid = "SA123456";
    first->FlamsteedNo = 42;
    first->GouldNo = 57;
    strcpy(first->constellation, "Ori");
    strcpy(first->Gouldcons, "Ori");
    Star *second = make_star("Other Display");
    second->local_name = "Orbit Alias";
    second->deleted = true;
    make_star(" W D1234+567");
    Star *gaia = make_star("Gaia White Dwarf");
    gaia->gaia_wd_name = "WDJ123456.78+123456.7";
    ExactStarSearchIndex index;
    for (const char *query : {"Stored Display Name", "stored display", "Orbit Alias", "orbit alias",
        "Orbit Aliaz", "GJ234.5", "Gliese 234.5", "Alias Bayer", "SA123456", "42 Ori", "57 G. Ori", "Missing Star",
        "WD1234+567", "WDJ123456.78+123456.7", "WDJ1234+1234"})
    {
        SCOPED_TRACE(query);
        EXPECT_EQ(find_object(query, true, 9e29, 1, &index), find_object(query, true, 9e29, 1));
    }
    Star *added = make_star("New Orbit Member");
    index.add(added->seqno, *added);
    EXPECT_EQ(find_object("New Orbit Member", true, 9e29, 1, &index), added->seqno);
    strcpy(added->name, "Renamed Orbit Member");
    added->namelen = strlen(added->name);
    index.add(added->seqno, *added);
    EXPECT_EQ(find_object("Renamed Orbit Member", true, 9e29, 1, &index), added->seqno);
    EXPECT_EQ(find_object("New Orbit Member", true, 9e29, 1, &index), -1);
    // Fuzzy and planet searches ignore the star-only snapshot.
    EXPECT_EQ(find_object("Orbit Aliaz", true, 9e29, 3, &index), find_object("Orbit Aliaz", true));
    Planet *planet = make_planet(first, "Test World");
    EXPECT_EQ(find_object("Test World", false, 9e29, 1, &index), planet->seqno);
    delete_the_universe();
}

TEST_F(FindObjectTest, CatalogIdentitiesResolveBeforeFuzzyDisplayNames)
{
    make_star("Imported Primary Extended");
    Star* host = make_star("Custom Primary");
    host->HD = 123456;
    strcpy(host->Gliese, "GJ 345.6");
    strcpy(host->Bayer, "alp Ori");
    strcpy(host->Flamsteed, "42 Ori");
    strcpy(host->constellation, "Ori");
    strcpy(host->Gouldcons, "Ori");
    host->FlamsteedNo = 42;
    host->GouldNo = 57;
    host->add_catalog_alias("Imported Primary");
    Planet* planet = make_planet(host, "GJ 345.6 b");
    planet->origname = "GJ 345.6 b";
    make_planet(static_cast<Star*>(cels[0]), "Custom Primarz b");
    ExactStarSearchIndex index;
    for (const char* alias : {"Custom Primary", "HD 123456", "GJ 345.6", "alp Ori", "42 Ori", "57 G. Ori", "Imported Primary"})
    {
        SCOPED_TRACE(alias);
        for (int distance : {1, 3})
        {
            EXPECT_EQ(find_object(alias, true, 9e29, distance), host->seqno);
            EXPECT_EQ(find_object(alias, true, 9e29, distance, &index), host->seqno);
            EXPECT_EQ(find_object(alias, false, 9e29, distance), host->seqno);
            const std::string query = std::string(alias) + " b";
            EXPECT_EQ(find_object(query.c_str(), false, 9e29, distance), planet->seqno);
        }
    }
    EXPECT_EQ(find_object("Custom Primary c", false), -1);
    EXPECT_EQ(find_object("Imported Primarz", true), host->seqno);
    delete_the_universe();
}

TEST_F(FindObjectTest, DeletedStarsReleaseNumericIdentifiers)
{
    std::vector<Star*> hd(MAX_HD + 1, nullptr), hip(MAX_HIP + 1, nullptr);
    struct RestoreCaches
    {
        Star** hd;
        Star** hip;
        ~RestoreCaches()
        {
            hdcache = hd;
            hipcache = hip;
        }
    } restore{hdcache, hipcache};
    hdcache = hd.data();
    hipcache = hip.data();
    Star* host = make_star("Temporary Primary");
    host->HD = 123456;
    host->HIP = 45678;
    hd[123456] = host;
    hip[45678] = host;
    delete_the_universe();
    EXPECT_EQ(hd[123456], nullptr);
    EXPECT_EQ(hip[45678], nullptr);
    EXPECT_EQ(find_object("HD 123456", true), -1);
    EXPECT_EQ(find_object("HIP 45678", true), -1);
}

TEST_F(FindObjectTest, RefusesSomethingNothingLikeAnyOfThem)
{
    make_star("Betelgeuse");
    EXPECT_EQ(find_object("Nonexistent Wombat"), -1);

    delete_the_universe();
}

TEST_F(FindObjectTest, ZeroDistanceLimitPreservesLiteralNamesAndRejectsFuzzyAliases)
{
    Star *sun = make_star("Test Primary");
    sun->local_name = "Solar Alias";
    Planet *planet = make_planet(sun, "Test World");

    EXPECT_EQ(find_object("Test Primary", false, 9e29, 0), sun->seqno);
    EXPECT_EQ(find_object("Test World", false, 9e29, 0), planet->seqno);
    EXPECT_EQ(find_object("Solar Alias", false, 9e29, 0), sun->seqno);
    EXPECT_EQ(find_object("Test Worl", false, 9e29, 0), -1);
    EXPECT_EQ(find_object("Absent Moon Parent", false, 9e29, 0), -1);

    delete_the_universe();
}

TEST_F(FindObjectTest, OnlyStarsSkipsThePlanets)
{
    Star* sun = make_star("Test Primary");
    Planet* p = make_planet(sun, "Wobbler", AU);

    EXPECT_EQ(find_object("Wobbler"), p->seqno);
    EXPECT_NE(find_object("Wobbler", true), p->seqno);      // only_stars: the planet is not eligible

    delete_the_universe();
}

TEST_F(FindObjectTest, ExactNameIgnoresTheMagnitudeLimit)
{
    Star* faint = make_star("Faint Thing");
    faint->apparent_magnitude = 14;

    EXPECT_EQ(find_object("Faint Thing", false, 6.0), faint->seqno);

    delete_the_universe();
}

// =====================================================================
// Idempotence: everything written is read back
// =====================================================================

template <class T> static void expect_json_idempotent(T& original, const char* what)
{
    json first = original.to_json();

    T restored;
    ASSERT_TRUE(restored.from_json(first)) << what;
    json second = restored.to_json();

    for (auto it = first.begin(); it != first.end(); ++it)
    {
        ASSERT_TRUE(second.contains(it.key())) << what << ": " << it.key() << " was not written back";
        const json& a = it.value();
        const json& b = second[it.key()];
        if (a.is_number() && b.is_number())
        {
            double x = a.get<double>(), y = b.get<double>();
            EXPECT_NEAR(x, y, std::fabs(x) * 1e-12 + 1e-12)
                << what << ": " << it.key() << " did not survive the round trip";
        }
        else EXPECT_EQ(a.dump(), b.dump())
            << what << ": " << it.key() << " did not survive the round trip";
    }
    for (auto it = second.begin(); it != second.end(); ++it)
        EXPECT_TRUE(first.contains(it.key())) << what << ": " << it.key() << " appeared out of nowhere";
}

class JsonIdempotenceTest : public UniverseFixture
{
    protected:
    // The fields every celestial object has, filled with values that are distinguishable from
    // each other and from any default.
    void fill_common(CelestialObject& cel, const char* name)
    {
        strcpy(cel.name, name);
        cel.origname = name;
        cel.origcenname = "Something Else";
        cel.mass = 1.234e27;
        cel.volumetric_mean_radius = 5.678e6;
        cel.oblateness = 0.00335;
        cel.temperature = 288;
        cel.sidereal_rotational_period = 86164.1;
        cel.right_ascension = 1.1;
        cel.declination = -0.4;
        cel.obliquity = 0.409;
        cel.equinox = 0.7;
        cel.lon_J2000_offset = 0.25;
        cel.precession = _pi * 2 / (25772 * oneyear);
        cel.distance = 12 * light_year;
        cel.distance_known = true;
        cel.epoch = 2451545.0;
        cel.absolute_magnitude = 4.83;
        cel.UB_color = 0.17;
        cel.BV_color = 0.65;
        cel.VR_color = 0.36;
        cel.RI_color = 0.32;
        cel.user_added = true;
        cel.location.galactic_center = Point(1, 2, 3);
        cel.location.system_center = Point(4e11, 5e11, 6e11);
        cel.location.local_position = Point(7, 8, 9);
    }
};

TEST_F(JsonIdempotenceTest, CelestialObject)
{
    CelestialObject cel;
    fill_common(cel, "Test Object");
    cel.type = rocky;
    expect_json_idempotent(cel, "CelestialObject");
}

TEST_F(JsonIdempotenceTest, Orbit)
{
    Orbit orbit;
    orbit.semimajor_axis = 2.5 * AU;
    orbit.eccentricity = 0.42;
    orbit.inclination = 0.3;
    orbit.ascending_node = 1.1;
    orbit.arg_periapsis = 2.2;
    orbit.mean_anomaly = 3.3;
    orbit.period = 4 * oneyear;
    orbit.epoch = 2451545.0;
    orbit.prec_node = 1e-9;
    orbit.proc_argperi = 2e-9;
    expect_json_idempotent(orbit, "Orbit");
}

TEST_F(JsonIdempotenceTest, Star)
{
    Star s;
    fill_common(s, "Test Star");
    s.type = star;
    strcpy(s.spectral_type, "G2V");
    strcpy(s.constellation, "Ori");
    strcpy(s.Bayer, "Alp Ori");
    strcpy(s.Flamsteed, "58 Ori");
    strcpy(s.Gliese, "Gl 999");
    strcpy(s.Bonn_survey, "BD");
    s.Bonn_survey_sign = '-';
    s.Bonn_survey_declination = 12;
    s.Bonn_survey_sequential = 3456;
    s.proper_motion_RA = 1e-12;
    s.proper_motion_decl = -2e-12;
    s.radial_velocity = 12345;
    s.apparent_magnitude = 4.5;
    s.parallax = 1e-7;
    s.HR = 1; s.HD = 2; s.HIP = 3; s.SAO = 4; s.SB9 = 5;
    s.CCDM = "12345+6789";
    s.BayerGrkno = 0;
    s.FlamsteedNo = 58;
    s.has_planets = 3;
    s.has_hz_planets = 1;
    s.is_orbit_multiple = true;
    expect_json_idempotent(s, "Star");
}

TEST_F(JsonIdempotenceTest, Planet)
{
    Planet p;
    fill_common(p, "Test Planet");
    p.type = rocky;
    p.albedo = 0.306;
    p.opposition_surge = 0.35;
    p.J2 = 0.0010826;
    p.asteroid_no = 4001;
    p.lock_type = true;
    Atmosphere* atm = p.ensure_atmosphere();
    atm->surface_pressure = 101325;
    atm->tau = 0.83;
    atm->particulates = 0.2;
    AtmosphereComposition* comp = atm->ensure_composition();
    comp->N2_portion = 0.78;
    comp->O2_portion = 0.21;
    comp->Ar_portion = 0.009;
    comp->CO2_portion = 0.001;
    expect_json_idempotent(p, "Planet");
}

TEST_F(JsonIdempotenceTest, Moon)
{
    Moon m;
    fill_common(m, "Test Moon");
    m.type = rocky;
    m.albedo = 0.12;
    m.height = 3.4e6;
    m.width = 3.5e6;
    m.depth = 3.3e6;
    expect_json_idempotent(m, "Moon");
}

TEST_F(JsonIdempotenceTest, Comet)
{
    Comet c;
    fill_common(c, "Test Comet");
    c.type = icy_tailed;
    c.designation = "C/2024 T1";
    c.H1 = 5.5; c.R1 = 10.0; c.D1 = 5.0;
    c.H2 = 14.0; c.R2 = 5.0; c.D2 = 5.0;
    expect_json_idempotent(c, "Comet");
}

TEST_F(JsonIdempotenceTest, Satellite)
{
    Satellite sat;
    fill_common(sat, "Test Satellite");
    sat.type = artificial;
    sat.bstar = 0.0001234;
    sat.mean_motion = 15.5 * 2 * _pi / oneday;
    expect_json_idempotent(sat, "Satellite");
}

TEST_F(JsonIdempotenceTest, Galaxy)
{
    Galaxy g;
    fill_common(g, "Test Galaxy");
    g.type = galaxy;
    expect_json_idempotent(g, "Galaxy");
}

TEST_F(JsonIdempotenceTest, Atmosphere)
{
    CelestialObject owner;
    Atmosphere atm(&owner);
    atm.surface_pressure = 9200000;             // Venus
    atm.tau = 4.2;
    atm.particulates = 0.9;
    AtmosphereComposition* comp = atm.ensure_composition();
    comp->CO2_portion = 0.965;
    comp->N2_portion = 0.035;

    // Atmosphere::from_json() has no default constructor to restore into, so this one is done by
    // hand rather than through the template.
    json first = atm.to_json();
    Atmosphere restored(&owner);
    ASSERT_TRUE(restored.from_json(first));
    EXPECT_EQ(first.dump(), restored.to_json().dump());
}
