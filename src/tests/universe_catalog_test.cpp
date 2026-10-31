#include <filesystem>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "universe_fixture.h"
#include "../classes/fictionalstars.h"

using namespace alienorum;
namespace fs = std::filesystem;

TEST(UniverseCatalogNameTest, RemovesSpacesAndDiacritics)
{
    EXPECT_EQ(Serialization::filename_part("NGC 1097"), "NGC1097");
    EXPECT_EQ(Serialization::filename_part(u8"Cú Chulainn"), "CuChulainn");
    EXPECT_EQ(Serialization::filename_part(u8"Cu\u0301 Chulainn"), "CuChulainn");
    EXPECT_EQ(Serialization::filename_part(u8"Ægir / Žižka"), "AEgirZizka");
    EXPECT_EQ(Serialization::filename_part("../:"), "Unnamed");
}

class UniverseCatalogTest : public UniverseFixture
{
    protected:
    fs::path directory;
    const std::string stem = "NGC1097.FIC.CuChulainn";
    Point origin{2, 3, 80};
    json batch;

    void SetUp() override
    {
        UniverseFixture::SetUp();
        directory = fs::temp_directory_path() / ("alienorum_catalog_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        make_star("Sun");
        batch = {{"id", "population:test"}, {"catalog_prefix", "FIC"}, {"galaxy", "PGC10588"}};
    }

    void TearDown() override
    {
        Serialization::finish_file_saves();
        delete_the_universe();
        fs::remove_all(directory);
        UniverseFixture::TearDown();
    }

    json read_json(const std::string& filename)
    {
        std::ifstream input(directory / filename);
        json document;
        input >> document;
        return document;
    }

    std::string read_bytes(const std::string& filename)
    {
        std::ifstream input(directory / filename, std::ios::binary);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    Star* find(const std::string& name)
    {
        for (int i = 0; cels[i]; ++i)
            if (cels[i]->typeclass() == class_star && name == cels[i]->name) return static_cast<Star*>(cels[i]);
        return nullptr;
    }

    Star* added(const char* name, int system, Point position, bool anchor = false)
    {
        Star* star = make_star(name);
        star->user_added = star->user_edited = true;
        star->origname = "FIC-" + std::to_string(100000 + system);
        star->epoch = J2000;
        star->location.galactic_center = origin;
        star->location.system_center = position;
        star->location.local_system_plane.v = Point(0, 1, 0);
        star->location.local_system_plane.a = .25;
        star->location.orbital_plane = star->location.equatorial_plane = star->location.local_system_plane;
        star->lock_system_plane = star->lock_equatorial_plane = true;
        star->stellar_system = {{"system", "population:test:" + std::to_string(system)}, {"slot", int('A')}, {"label", name}};
        star->fictional_origin = {{"id", "population:test:" + std::to_string(system) + ":A"},
            {"batch", batch}, {"anchor", anchor}, {"system_index", system}, {"designation", star->origname}};
        star->sidereal_rotational_period = 23.123456789 * oneday;
        star->set_fictional_motion(Point(1234 + system, -2345, 3456));
        return star;
    }

    Star* populate()
    {
        Galaxy* galaxy = new Galaxy();
        strcpy(galaxy->name, "NGC 1097");
        galaxy->PGC = 10588;
        galaxy->location.galactic_center = origin;
        append_cel(galaxy);
        Star* anchor = added(u8"Cú Chulainn", 0, Point(3.123456789e20, 1.234567891e20, -1.345678912e20), true);
        Star* host = added("FIC-100001", 1, anchor->fictional_epoch_position + Point(3.456789 * light_year, 2 * light_year, -1.21 * light_year));
        Planet* planet = make_planet(host, "Test world");
        planet->user_added = true;
        Star* edited = make_star("Edited catalog star");
        edited->user_edited = true;
        edited->temperature = 4321;
        return host;
    }

    void save(bool background = false)
    {
        ASSERT_TRUE(Serialization::save_region_files((directory / "universe.json").string(), cels, true, background));
        ASSERT_TRUE(Serialization::finish_file_saves());
    }
};

TEST_F(UniverseCatalogTest, SplitsAddedStarsAndKeepsEditedCatalogStarsInJson)
{
    populate();
    save();
    const auto index = read_json("universe.json");
    ASSERT_EQ(index.at("$universe").at("parts").size(), 2u);
    const auto worlds = read_json(stem + ".json");
    EXPECT_TRUE(worlds.contains("FIC-100001.Test world"));
    for (const auto& item : worlds.items())
    {
        if (item.key().front() != '$')
        {
            EXPECT_NE(item.value().at("cls"), class_star);
        }
    }
    const auto edits = read_json("MilkyWay.Edited.Sun.json");
    ASSERT_TRUE(edits.contains("Edited catalog star"));
    EXPECT_FALSE(edits.at("Edited catalog star").at("user_added"));
    const std::string bytes = read_bytes(stem + ".dat");
    EXPECT_EQ(bytes.find("# USER_STARS1 "), 0u);
    EXPECT_NE(bytes.find(" FIC1 "), std::string::npos);
}

TEST_F(UniverseCatalogTest, RelativeCoordinatesAndExactMotionRoundTrip)
{
    Star* host = populate();
    const Point original = host->fictional_epoch_position;
    const Point velocity = host->fictional_space_velocity;
    const double rotation = host->sidereal_rotational_period;
    save(true);
    std::istringstream catalog(read_bytes(stem + ".dat"));
    std::string line, comment, version, payload;
    ASSERT_TRUE(static_cast<bool>(std::getline(catalog, line)));
    std::istringstream header_line(line);
    header_line >> comment >> version >> std::quoted(payload);
    const json header = json::parse(payload);
    EXPECT_TRUE(header.at("relative"));
    EXPECT_EQ(header.at("galaxy").at("name"), "NGC 1097");
    int rows = 0;
    while (std::getline(catalog, line))
    {
        ASSERT_GE(line.size(), 655u);
        EXPECT_EQ(line.substr(468, 3), "SC2");
        const double distance_ly = std::stod(line.substr(225, 11));
        EXPECT_LT(distance_ly, 100000);
        if (rows == 1)
        {
            EXPECT_NEAR(distance_ly, std::sqrt(3.456789 * 3.456789 + 4 + 1.21 * 1.21), .0001);
        }
        ++rows;
    }
    EXPECT_EQ(rows, 2);
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    host = find("FIC-100001");
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(host->fictional_epoch_position.to_json(), original.to_json());
    EXPECT_EQ(host->fictional_space_velocity.to_json(), velocity.to_json());
    EXPECT_DOUBLE_EQ(host->sidereal_rotational_period, rotation);
    EXPECT_EQ(host->location.galactic_center.to_json(), origin.to_json());
    ASSERT_NE(find(u8"Cú Chulainn"), nullptr);
    EXPECT_NE(find("Edited catalog star"), nullptr);
    CelestialObject* world = nullptr;
    for (int i = 0; cels[i]; ++i) if (!strcmp(cels[i]->name, "Test world")) world = cels[i];
    ASSERT_NE(world, nullptr);
    ASSERT_NE(world->orbit, nullptr);
    EXPECT_EQ(world->orbit->center, host);
    const int count = ncelobjs;
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    EXPECT_EQ(ncelobjs, count);
    EXPECT_EQ(find("FIC-100001"), host);
}

TEST_F(UniverseCatalogTest, CompanionOrbitsAndNamedPairImport)
{
    Star* host = populate();
    Star* companion = added("FIC-100001 B", 1, host->fictional_epoch_position);
    companion->origname += " B";
    companion->fictional_origin["designation"] = companion->origname;
    companion->fictional_origin["id"] = "population:test:1:B";
    companion->stellar_system["slot"] = int('B');
    companion->orbit = new Orbit();
    companion->orbit->center = host;
    companion->orbit->center_name = host->name;
    companion->orbit->semimajor_axis = 10 * AU;
    companion->orbit->compute_period(companion->mass);
    const double period = companion->orbit->period;
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / (stem + ".dat")).string(), cels, MAX_CELOBJS));
    companion = find("FIC-100001 B");
    ASSERT_NE(companion, nullptr);
    ASSERT_NE(companion->orbit, nullptr);
    EXPECT_EQ(companion->orbit->center, find("FIC-100001"));
    EXPECT_DOUBLE_EQ(companion->orbit->period, period);
    EXPECT_EQ(find("Edited catalog star"), nullptr);
}

TEST_F(UniverseCatalogTest, CorruptCatalogLeavesLiveUniverseIntact)
{
    populate();
    save();
    { std::ofstream output(directory / (stem + ".dat")); output << "broken"; }
    delete_the_universe();
    Star* sun = make_star("Sun");
    EXPECT_FALSE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    EXPECT_EQ(ncelobjs, 1);
    EXPECT_EQ(cels[0], sun);
    EXPECT_EQ(find("FIC-100001"), nullptr);
}

TEST_F(UniverseCatalogTest, PreviousIndexRestoresMatchingBackupPairs)
{
    Star* host = populate();
    const double temperature = host->temperature;
    save();
    host->temperature = 4567;
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json.bak").string(), cels, MAX_CELOBJS));
    ASSERT_NE(find("FIC-100001"), nullptr);
    EXPECT_DOUBLE_EQ(find("FIC-100001")->temperature, std::round(temperature * 1000) / 1000);
}

TEST_F(UniverseCatalogTest, LegacyJsonRemainsLoadable)
{
    Star* star = make_star("Legacy added star");
    star->user_added = true;
    const fs::path path = directory / "legacy.json";
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, false));
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file(path.string(), cels, MAX_CELOBJS));
    EXPECT_NE(find("Legacy added star"), nullptr);
}

TEST_F(UniverseCatalogTest, FailedStagingPreservesExistingIndex)
{
    populate();
    save();
    const std::string index = read_bytes("universe.json");
    fs::create_directory(directory / (stem + ".json.bak"));
    EXPECT_FALSE(Serialization::save_region_files((directory / "universe.json").string(), cels, true, false));
    EXPECT_EQ(read_bytes("universe.json"), index);
    for (const auto& file : fs::directory_iterator(directory)) EXPECT_NE(file.path().extension(), ".tmp");
}

TEST_F(UniverseCatalogTest, AddedMilkyWayStarsKeepHeliocentricCoordinates)
{
    Star* star = make_star("Custom added star");
    star->user_added = true;
    star->origname = star->name;
    star->right_ascension = .75;
    star->declination = -.25;
    const double distance = star->distance;
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    star = find("Custom added star");
    ASSERT_NE(star, nullptr);
    EXPECT_DOUBLE_EQ(star->distance, distance);
    EXPECT_NEAR(star->right_ascension, .75, 1e-14);
    EXPECT_NEAR(star->declination, -.25, 1e-14);
    EXPECT_FALSE(star->fictional_origin.is_object());
    EXPECT_TRUE(star->user_added);
}

TEST_F(UniverseCatalogTest, AddedExtragalacticStarWithoutGenerationMetadataKeepsItsFrame)
{
    populate();
    Star* star = added("Custom remote star", 2, Point(1.23e20, -4.56e20, 7.89e20));
    const Point position = star->fictional_epoch_position;
    star->fictional_origin = nullptr;
    star->stellar_system = nullptr;
    star->has_fictional_motion = false;
    star->location.system_center = position;
    star->radial_velocity = star->proper_motion_RA = star->proper_motion_decl = 0;
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    star = find("Custom remote star");
    ASSERT_NE(star, nullptr);
    EXPECT_FALSE(star->fictional_origin.is_object());
    EXPECT_TRUE(star->has_fictional_motion);
    EXPECT_EQ(star->location.galactic_center.to_json(), origin.to_json());
    EXPECT_EQ(star->location.system_center.to_json(), position.to_json());
}

TEST_F(UniverseCatalogTest, MultipleGalaxiesRestoreThroughOneIndex)
{
    populate();
    Galaxy* galaxy = new Galaxy();
    strcpy(galaxy->name, "NGC 1300");
    galaxy->PGC = 12412;
    const Point other_origin(-1, 2, 3);
    galaxy->location.galactic_center = other_origin;
    append_cel(galaxy);
    batch = {{"id", "population:other"}, {"catalog_prefix", "FIC"}, {"galaxy", "PGC12412"}};
    Star* other = added("Other Center", 0, Point(5e19, 6e19, 7e19), true);
    other->location.galactic_center = other_origin;
    other->fictional_origin["id"] = "population:other:0:A";
    other->stellar_system["system"] = "population:other:0";
    other->set_fictional_motion(Point(1, 2, 3));
    save();
    EXPECT_EQ(read_json("universe.json").at("$universe").at("parts").size(), 3u);
    EXPECT_TRUE(fs::exists(directory / "NGC1300.FIC.OtherCenter.dat"));
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    ASSERT_NE(find("Other Center"), nullptr);
    EXPECT_EQ(find("Other Center")->location.galactic_center.to_json(), other_origin.to_json());
    EXPECT_NE(find(u8"Cú Chulainn"), nullptr);
}

TEST_F(UniverseCatalogTest, LegacyGeneratedPopulationRecoversPrefixAndCenter)
{
    populate();
    ASSERT_TRUE(Serialization::save_file((directory / "legacy.json").string(), cels, true, false));
    delete_the_universe();
    make_star("Sun");
    Galaxy* galaxy = new Galaxy();
    strcpy(galaxy->name, "NGC 1097");
    galaxy->location.galactic_center = origin;
    append_cel(galaxy);
    ASSERT_TRUE(Serialization::load_file((directory / "legacy.json").string(), cels, MAX_CELOBJS));
    ASSERT_NE(find(u8"Cú Chulainn"), nullptr);
    EXPECT_FALSE(find(u8"Cú Chulainn")->fictional_origin.contains("batch"));
    save();
    ASSERT_TRUE(fs::exists(directory / (stem + ".dat"))) << read_json("universe.json").dump(2);
    const auto worlds = read_json(stem + ".json");
    EXPECT_TRUE(worlds.contains("FIC-100001.Test world"));
}

TEST_F(UniverseCatalogTest, PositionResidualPreservesSmallCoordinatesAroundDistantAnchor)
{
    populate();
    added("Small galactocentric position", 3, Point(1, 2, 3));
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    ASSERT_NE(find("Small galactocentric position"), nullptr);
    EXPECT_EQ(find("Small galactocentric position")->fictional_epoch_position.to_json(), Point(1, 2, 3).to_json());
}

TEST_F(UniverseCatalogTest, LoadedRegionKeepsItsFilenameWithoutTheGalaxyCatalog)
{
    populate();
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / (stem + ".json")).string(), cels, MAX_CELOBJS));
    save();
    const auto parts = read_json("universe.json").at("$universe").at("parts");
    bool found = false;
    for (const auto& name : parts) if (name == stem + ".json") found = true;
    EXPECT_TRUE(found);
    EXPECT_TRUE(find(u8"Cú Chulainn")->catalog_frame.at("anchor"));
}

TEST_F(UniverseCatalogTest, SavingAfterInterruptedPublicationPreservesCommittedBackup)
{
    Star* host = populate();
    const double initial = host->temperature;
    save();
    const std::string committed_index = read_bytes("universe.json");
    host->temperature = 4567;
    save();
    // An interruption before publishing the index leaves the prior index in place.
    { std::ofstream output(directory / "universe.json"); output << committed_index; }
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    ASSERT_NE(find("FIC-100001"), nullptr);
    EXPECT_DOUBLE_EQ(find("FIC-100001")->temperature, std::round(initial * 1000) / 1000);
    find("FIC-100001")->temperature = 5678;
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json.bak").string(), cels, MAX_CELOBJS));
    EXPECT_DOUBLE_EQ(find("FIC-100001")->temperature, std::round(initial * 1000) / 1000);
}

TEST_F(UniverseCatalogTest, LegacyOneBasedPopulationSelectsItsFirstSystemAsCenter)
{
    populate();
    find(u8"Cú Chulainn")->stellar_system["system"] = "population:test:1";
    find("FIC-100001")->stellar_system["system"] = "population:test:2";
    ASSERT_TRUE(Serialization::save_file((directory / "legacy.json").string(), cels, true, false));
    delete_the_universe();
    make_star("Sun");
    Galaxy* galaxy = new Galaxy();
    strcpy(galaxy->name, "NGC 1097");
    galaxy->location.galactic_center = origin;
    append_cel(galaxy);
    ASSERT_TRUE(Serialization::load_file((directory / "legacy.json").string(), cels, MAX_CELOBJS));
    save();
    EXPECT_TRUE(fs::exists(directory / (stem + ".dat")));
}

TEST_F(UniverseCatalogTest, LongWhiteDwarfProvenanceSurvivesTheFixedColumns)
{
    populate();
    Star* star = added("FIC-100003", 3, Point(3e20, 1e20, -1e20));
    strcpy(star->spectral_type, "DA3");
    star->temperature = 10000;
    star->mass = .6 * solar_mass;
    star->volumetric_mean_radius = .012 * solar_radius;
    star->wd_color_valid = true;
    star->wd_color_red = 33;
    star->wd_color_green = 66;
    star->wd_color_blue = 99;
    star->wd_color_source = "A_model_label_that_exceeds_the_fixed_catalog_column_width";
    star->wd_temperature_source = "A_temperature_label_that_exceeds_the_fixed_catalog_column_width";
    const auto expected = Serialization::output_record(star->to_json());
    save();
    delete_the_universe();
    make_star("Sun");
    ASSERT_TRUE(Serialization::load_file((directory / "universe.json").string(), cels, MAX_CELOBJS));
    star = find("FIC-100003");
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(star->wd_temperature_source, expected.at("wd_temp_src"));
    EXPECT_EQ(star->wd_color_source, expected.at("wd_color_src"));
}
