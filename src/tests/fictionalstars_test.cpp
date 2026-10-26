#include <gtest/gtest.h>
#include <SDL_image.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <set>
#include "../classes/fictionalstars.h"
#include "../classes/cat.h"
#include "../classes/exocons.h"
#include "../housekeeping.h"
#include "universe_fixture.h"

using namespace alienorum;

class FictionalStarsTest : public UniverseFixture
{
    protected:
    FictionalGalaxy model;
    FictionalRegion region;
    std::filesystem::path temporary;
    void SetUp() override
    {
        UniverseFixture::SetUp();
        Star::load_main_seq_dat();
        make_star("Sun");
        favestars.clear();
        model.key = "TEST";
        model.code = "TEST";
        model.name = "Test galaxy";
        model.east = Point(1, 0, 0);
        model.height_axis = Point(0, 1, 0);
        model.south = Point(0, 0, 1);
        model.origin_mly = Point(20, 30, 40);
        model.radius_ly = 50000;
        region.region_id = "RTEST";
        region.star_count = 500;
        region.center_ly = Point(25000, 0, 0);
        region.seed = 7519;
        temporary = std::filesystem::temp_directory_path() / ("alienorum_fictional_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporary);
    }
    void TearDown() override
    {
        for (int i = 0; cels[i]; ++i)
            if (cels[i]->typeclass() == class_star && static_cast<Star*>(cels[i])->multisys)
            {
                auto* system = static_cast<Star*>(cels[i])->multisys;
                system->unlink();
                delete system;
            }
        favestars.clear();
        delete_the_universe();
        std::filesystem::remove_all(temporary);
        UniverseFixture::TearDown();
    }
};

TEST_F(FictionalStarsTest, PreviewIsDeterministicAndDoesNotAlterTheUniverse)
{
    auto first = generate_fictional_stars(model, region);
    auto second = generate_fictional_stars(model, region);
    ASSERT_TRUE(first.error.empty()) << first.error;
    ASSERT_EQ(first.stars.size(), 500u);
    ASSERT_EQ(second.stars.size(), first.stars.size());
    EXPECT_EQ(ncelobjs, 1);
    std::set<std::string> identities;
    for (size_t i = 0; i < first.stars.size(); ++i)
    {
        EXPECT_EQ(first.stars[i]->to_json(), second.stars[i]->to_json()) << i;
        EXPECT_TRUE(identities.insert(first.stars[i]->fictional_origin.at("id")).second);
        EXPECT_EQ(first.stars[i]->HD, 0u);
        EXPECT_EQ(first.stars[i]->HIP, 0u);
        EXPECT_EQ(first.stars[i]->gaia_source_id, 0u);
        EXPECT_TRUE(first.stars[i]->user_added);
        EXPECT_FALSE(first.stars[i]->known_poles);
    }
    EXPECT_EQ(first.stars.front()->orbit, nullptr);
    EXPECT_TRUE(first.stars.front()->is_sunlike());
    EXPECT_GT(first.clusters, 0);
    EXPECT_GT(first.systems, 1);
    EXPECT_LT(first.systems, int(first.stars.size()));
}

TEST_F(FictionalStarsTest, FalloffPreservesNearbyDwarfsAndBrightDistantStars)
{
    region.star_count = 3000;
    auto batch = generate_fictional_stars(model, region);
    ASSERT_TRUE(batch.error.empty());
    EXPECT_GT(batch.nearby, 0.7*region.star_count);
    int far = 0, dwarfs = 0, giants = 0, white_dwarfs = 0, companions = 0;
    for (const auto& star : batch.stars)
    {
        const Point delta = (star->location.system_center - model.world_offset(region.center_ly))*(1/light_year);
        const double distance = delta.magnitude();
        EXPECT_LE(distance, region.radius_ly + 1e-5);
        if (distance > 25.001)
        {
            EXPECT_LE(star->absolute_magnitude + 5*log10(distance*light_year/(10*parsec)), 6.501) << star->name;
        }
        if (distance > 100) ++far;
        if (star->spectral_type[0] == 'M' && star->is_main_sequence()) ++dwarfs;
        if (strstr(star->spectral_type, "III")) ++giants;
        if (star->is_white_dwarf()) ++white_dwarfs;
        if (star->orbit)
        {
            ++companions;
            EXPECT_GT(star->orbit->period, 0);
            EXPECT_LT(star->orbit->eccentricity, 1);
            EXPECT_NE(star->multisys->get_member('A'), nullptr);
        }
        EXPECT_GT(star->mass, 0);
        EXPECT_GT(star->volumetric_mean_radius, 0);
        EXPECT_GT(star->temperature, 0);
        EXPECT_GT(star->sidereal_rotational_period, 0);
        EXPECT_TRUE(std::isfinite(star->absolute_magnitude));
        EXPECT_TRUE(std::isfinite(star->metallicity));
    }
    EXPECT_GT(far, 0);
    EXPECT_GT(dwarfs, 100);
    EXPECT_GT(giants, 0);
    EXPECT_GT(white_dwarfs, 0);
    EXPECT_GT(companions, 100);
}

TEST_F(FictionalStarsTest, RemotePositionsAndIndependentPlanesSurviveUpdates)
{
    auto batch = generate_fictional_stars(model, region);
    double mean_y = 0, mean_y2 = 0;
    int systems = 0;
    for (const auto& star : batch.stars)
    {
        const CelestialLocation before = star->location;
        const Rotation equatorial = star->location.equatorial_plane;
        const Rotation plane = star->location.local_system_plane;
        star->update_location(J2000_TIME_T + 10*oneyear);
        EXPECT_EQ(star->location.galactic_center.to_json(), model.origin_mly.to_json());
        EXPECT_NEAR(star->location.system_center.distance_to(before.system_center), 0, 1e6);
        EXPECT_DOUBLE_EQ(star->location.equatorial_plane.a, equatorial.a);
        EXPECT_DOUBLE_EQ(star->location.local_system_plane.a, plane.a);
        EXPECT_FALSE(star->known_poles);
        if (!star->orbit)
        {
            const Point pole = rotate3D(Point(0, 1, 0), center, plane.v, -plane.a);
            mean_y += pole.y;
            mean_y2 += pole.y*pole.y;
            ++systems;
        }
    }
    EXPECT_NEAR(mean_y/systems, 0, 0.12);
    EXPECT_NEAR(mean_y2/systems, 1.0/3, 0.10);
}

TEST_F(FictionalStarsTest, SaveImportPreservesIdentityAfterRenamingAndAllCompanionLinks)
{
    region.star_count = 120;
    auto batch = generate_fictional_stars(model, region);
    const auto original = batch.stars.front()->location.to_json();
    std::string message;
    ASSERT_TRUE(batch.install(message)) << message;
    EXPECT_EQ(ncelobjs, 121);
    Star* anchor = static_cast<Star*>(cels[1]);
    strcpy(anchor->name, "A renamed destination");
    anchor->user_edited = false;
    const auto file = temporary / "universe.json";
    {
        std::fstream output(file, std::ios::out);
        ASSERT_TRUE(Serialization::save_all(output, cels, true));
    }
    json records;
    std::ifstream(file) >> records;
    ASSERT_EQ(records.size(), 120u);
    set_center_objects();
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        std::fstream input(file, std::ios::in);
        ASSERT_TRUE(Serialization::load_all(input, cels, MAX_CELOBJS));
        EXPECT_EQ(ncelobjs, 121);
    }
    EXPECT_EQ(cels[1], anchor);
    EXPECT_STREQ(anchor->name, "A renamed destination");
    EXPECT_EQ(anchor->location.to_json(), original);
    for (int i = 1; cels[i]; ++i)
    {
        Star* star = static_cast<Star*>(cels[i]);
        EXPECT_FALSE(star->known_poles);
        EXPECT_TRUE(star->fictional_origin.is_object());
        if (star->orbit)
        {
            ASSERT_NE(star->orbit->center, nullptr);
            ASSERT_NE(star->multisys, nullptr);
            EXPECT_EQ(star->multisys->get_member('A'), star->cenobj);
        }
    }
}

TEST_F(FictionalStarsTest, CancellationCapacityAndDuplicateRegionsDoNotPartiallyInsert)
{
    std::atomic<bool> cancelled{true};
    auto cancelled_batch = generate_fictional_stars(model, region, &cancelled);
    EXPECT_TRUE(cancelled_batch.stars.empty());
    EXPECT_FALSE(cancelled_batch.error.empty());
    auto first = generate_fictional_stars(model, region);
    auto duplicate = generate_fictional_stars(model, region);
    std::string message;
    ASSERT_TRUE(first.install(message));
    EXPECT_FALSE(duplicate.install(message));
    EXPECT_EQ(ncelobjs, 501);
    const int original_count = ncelobjs;
    ncelobjs = MAX_CELOBJS-2;
    EXPECT_FALSE(duplicate.install(message));
    ncelobjs = original_count;
    EXPECT_EQ(ncelobjs, 501);
}

TEST_F(FictionalStarsTest, OverlappingRegionsWithDifferentIdentitiesAreAllowed)
{
    auto first = generate_fictional_stars(model, region);
    region.region_id = "ROVERLAP";
    auto other = generate_fictional_stars(model, region);
    std::string message;
    ASSERT_TRUE(first.install(message));
    ASSERT_TRUE(other.install(message));
    EXPECT_EQ(ncelobjs, 1001);
}

TEST_F(FictionalStarsTest, ImageSamplingLinearizesPixelsWrapsThePanoramaAndIgnoresDisplay)
{
    const auto file = temporary / "field.png";
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 4, 2, 32, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(surface, nullptr);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
        {
            auto* pixel = static_cast<unsigned char*>(surface->pixels) + y*surface->pitch + x*4;
            pixel[0] = x < 2 ? 128 : 255;
            pixel[1] = 0;
            pixel[2] = x < 2 ? 255 : 0;
            pixel[3] = 255;
        }
    ASSERT_EQ(IMG_SavePNG(surface, file.string().c_str()), 0);
    SDL_FreeSurface(surface);
    FictionalImage image;
    ASSERT_TRUE(image.load(file.string(), true));
    EXPECT_NEAR(image.sample(0.125, 0.5).x, 0.21586, 1e-4);
    EXPECT_EQ(image.sample(0, 0.5).to_json(), image.sample(1, 0.5).to_json());
    EXPECT_EQ(image.sample(-0.01, 0.5).to_json(), image.sample(0.99, 0.5).to_json());
    EXPECT_FALSE(image.hash.empty());
    model.image = image;
    model.image.panoramic = false;
    EXPECT_GT(model.population_color(Point(-25000, 0, 0)).z,
        model.population_color(Point(25000, 0, 0)).z);
}

TEST_F(FictionalStarsTest, FramesSupportMilkyWaySpheroidsIrregularsAndMissingImages)
{
    Galaxy galaxy;
    strcpy(galaxy.name, "A galaxy with no image");
    galaxy.right_ascension = 0.8;
    galaxy.declination = 0.3;
    galaxy.volumetric_mean_radius = 30000*light_year;
    galaxy.PGC = 9999999;
    galaxy.inclination = 1.2;
    galaxy.position_angle = 0.5;
    for (double morphology : {-5.0, 3.0, 10.0})
    {
        galaxy.morphological_T = morphology;
        galaxy.T_known = true;
        const auto frame = FictionalGalaxy::from_galaxy(galaxy);
        const Point input(1234, 56, -789);
        EXPECT_NEAR(frame.disk_offset(frame.world_offset(input)).distance_to(input), 0, 1e-8);
        EXPECT_FALSE(frame.image.available());
        EXPECT_GT(frame.density(input), 0);
        EXPECT_FALSE(frame.inside(Point(1e10, 0, 0)));
    }
    galaxy.reference_galaxy = true;
    auto milky = FictionalGalaxy::from_galaxy(galaxy);
    EXPECT_EQ(milky.key, "MW");
    const Point disk(1234, 567, 890);
    EXPECT_NEAR(milky.disk_offset(milky.world_offset(disk)).distance_to(disk), 0, 1e-8);
    EXPECT_NEAR(milky.from_milky_ray(0, 0, 8200*parsec/light_year).magnitude(), 0, 1e-8);
}

TEST_F(FictionalStarsTest, DefaultBudgetBenchmark)
{
    region.star_count = 10000;
    auto batch = generate_fictional_stars(model, region);
    ASSERT_TRUE(batch.error.empty()) << batch.error;
    EXPECT_EQ(batch.stars.size(), 10000u);
    EXPECT_GT(batch.estimated_save_bytes, 0u);
    std::cout << "Fictional benchmark: " << batch.stars.size() << " stars, " << batch.systems
        << " systems, " << batch.elapsed_seconds << " seconds, estimated JSON "
        << batch.estimated_save_bytes/(1024.0*1024) << " MiB" << std::endl;
    std::string message;
    ASSERT_TRUE(batch.install(message)) << message;
    const auto save_start = std::chrono::steady_clock::now();
    const auto file = temporary / "default_population.json";
    {
        std::fstream output(file, std::ios::out);
        ASSERT_TRUE(Serialization::save_all(output, cels, true));
    }
    const auto save_end = std::chrono::steady_clock::now();
    {
        std::fstream input(file, std::ios::in);
        ASSERT_TRUE(Serialization::load_all(input, cels, MAX_CELOBJS));
    }
    EXPECT_EQ(ncelobjs, 10001);
    std::cout << "Fictional save benchmark: " << std::filesystem::file_size(file)/(1024.0*1024)
        << " MiB, save " << std::chrono::duration<double>(save_end-save_start).count()
        << " seconds, reload " << std::chrono::duration<double>(std::chrono::steady_clock::now()-save_end).count()
        << " seconds" << std::endl;
}

TEST_F(FictionalStarsTest, BluePixelsFavorYoungHotPopulationsAndBrightnessWeightsDensity)
{
    region.star_count = 1500;
    region.clusters = false;
    model.image.width = model.image.height = 1;
    model.image.rgb = {1, 0.1, 0.01};
    auto red = generate_fictional_stars(model, region);
    model.image.rgb = {0.01, 0.1, 1};
    auto blue = generate_fictional_stars(model, region);
    int red_hot = 0, blue_hot = 0;
    double red_age = 0, blue_age = 0;
    for (const auto& star : red.stars)
    {
        if (star->temperature > 10000) ++red_hot;
        red_age += star->fictional_origin.at("age_gyr").get<double>();
    }
    for (const auto& star : blue.stars)
    {
        if (star->temperature > 10000) ++blue_hot;
        blue_age += star->fictional_origin.at("age_gyr").get<double>();
        double metal, uncertainty;
        EXPECT_TRUE(star->calibrated_iron_abundance(metal, uncertainty));
    }
    EXPECT_GT(blue_hot, red_hot + 20);
    EXPECT_LT(blue_age, red_age*0.7);
    model.image.rgb = {0.01, 0.01, 0.01};
    const double dim = model.density(region.center_ly);
    model.image.rgb = {0.8, 0.8, 0.8};
    EXPECT_GT(model.density(region.center_ly), dim*10);
}

TEST_F(FictionalStarsTest, LongGalaxyNamesKeepDistinctCountersAndRejectOversizedIdentifiers)
{
    Galaxy galaxy;
    strcpy(galaxy.name, "A long fictional galaxy designation");
    galaxy.volumetric_mean_radius = 50000*light_year;
    auto frame = FictionalGalaxy::from_galaxy(galaxy);
    region.region_id = fictional_region_id();
    auto batch = generate_fictional_stars(frame, region);
    ASSERT_TRUE(batch.error.empty()) << batch.error;
    std::set<std::string> names;
    for (const auto& star : batch.stars)
    {
        EXPECT_LT(strlen(star->name), size_t(name_max_len));
        EXPECT_TRUE(names.insert(star->name).second);
        EXPECT_EQ(star->namelen, strlen(star->name));
    }
    region.region_id = std::string(80, 'X');
    auto invalid = generate_fictional_stars(frame, region);
    EXPECT_FALSE(invalid.error.empty());
    EXPECT_TRUE(invalid.stars.empty());
}

TEST_F(FictionalStarsTest, FreshUniverseReloadRetainsSyntheticPhysicsWithoutSourceFiles)
{
    region.star_count = 80;
    auto batch = generate_fictional_stars(model, region);
    std::string message;
    ASSERT_TRUE(batch.install(message));
    static_cast<Star*>(cels[1])->is_faved = true;
    std::vector<json> expected;
    for (int i = 1; cels[i]; ++i) expected.push_back(static_cast<Star*>(cels[i])->to_json());
    const auto file = temporary / "fresh.json";
    {
        std::fstream output(file, std::ios::out);
        ASSERT_TRUE(Serialization::save_all(output, cels, true));
    }
    for (int i = 1; cels[i]; ++i)
        if (static_cast<Star*>(cels[i])->multisys)
        {
            auto* system = static_cast<Star*>(cels[i])->multisys;
            system->unlink();
            delete system;
        }
    delete_the_universe();
    make_star("Sun");
    std::fstream input(file, std::ios::in);
    ASSERT_TRUE(Serialization::load_all(input, cels, MAX_CELOBJS));
    EXPECT_EQ(ncelobjs, 81);
    ASSERT_EQ(favestars.size(), 1u);
    EXPECT_TRUE(favestars.front()->fictional_origin.at("anchor"));
    for (const auto& record : expected)
    {
        Star* restored = nullptr;
        for (int i = 1; cels[i]; ++i)
        {
            Star* candidate = static_cast<Star*>(cels[i]);
            if (candidate->fictional_origin.at("id") == record.at("fictional_origin").at("id")) restored = candidate;
        }
        ASSERT_NE(restored, nullptr);
        EXPECT_EQ(restored->location.galactic_center.to_json(), record.at("location").at("galactic_center"));
        EXPECT_NEAR(restored->location.equatorial_plane.a*fiftyseven,
            record.at("location").at("equatorial_plane").at("a").get<double>(), 1e-10);
        EXPECT_DOUBLE_EQ(restored->absolute_magnitude, record.at("absolute_magnitude"));
        EXPECT_DOUBLE_EQ(restored->mass, record.at("mass"));
        const auto system_id = record.at("stellar_system").at("system");
        int members = 0;
        for (const auto& other : expected) if (other.at("stellar_system").at("system") == system_id) ++members;
        if (members == 1)
        {
            EXPECT_EQ(restored->multisys, nullptr);
        }
        EXPECT_FALSE(restored->known_poles);
        EXPECT_TRUE(restored->user_added);
    }
}

TEST_F(FictionalStarsTest, CatalogWriterExcludesSyntheticStarsAndImportsRejectDuplicateIdentity)
{
    region.star_count = 1;
    auto batch = generate_fictional_stars(model, region);
    struct Writer : CatalogReader
    {
        using CatalogReader::write_condensed_star_cat_line;
    } catalog;
    FILE* output = tmpfile();
    ASSERT_NE(output, nullptr);
    catalog.write_condensed_star_cat_line(output, batch.stars.front().get());
    EXPECT_EQ(ftell(output), 0);
    fclose(output);
    json a = batch.stars.front()->to_json();
    json b = a;
    b["!name"] = "Different display name";
    json all = {{"first", a}, {"second", b}};
    const auto file = temporary / "duplicates.json";
    std::ofstream(file) << all;
    std::fstream input(file, std::ios::in);
    EXPECT_FALSE(Serialization::load_all(input, cels, MAX_CELOBJS));
    EXPECT_EQ(ncelobjs, 1);
}

TEST_F(FictionalStarsTest, ExistingConstellationGeneratorAcceptsGeneratedVantageAndProducesLines)
{
    region.star_count = 150;
    auto batch = generate_fictional_stars(model, region);
    std::string message;
    ASSERT_TRUE(batch.install(message));
    std::string vantage;
    EXPECT_TRUE(ExoConsGenerator::check_should_generate_for(static_cast<Star*>(cels[1]), vantage));
    std::vector<Constellation> generated;
    const auto start = std::chrono::steady_clock::now();
    ExoConsGenerator::generate_constellations(static_cast<Star*>(cels[1]), generated);
    int lines = 0;
    for (const auto& constellation : generated)
    {
        lines += constellation.lines.size();
    }
    EXPECT_GT(lines, 0);
    std::cout << "Fictional constellation benchmark: " << lines << " lines in "
        << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << " seconds" << std::endl;
}

TEST_F(FictionalStarsTest, VisibleRemoteStarsSurviveCullingAtTheGeneratedCenter)
{
    const double old_zoom = zoom, old_brightness = global_brightness;
    zoom = global_brightness = 1;
    auto batch = generate_fictional_stars(model, region);
    CelestialLocation vantage = batch.stars.front()->location;
    int visible = 0;
    for (const auto& star : batch.stars)
    {
        if (star.get() == batch.stars.front().get()) continue;
        const double magnitude = star->viewer_magnitude(vantage);
        if (magnitude < normal_best_mag_limit-0.01)
        {
            ++visible;
            EXPECT_TRUE(star->is_really_truly_in_visible_box(Point(vantage)))
                << star->name << " magnitude " << magnitude;
        }
        CelestialLocation far = star->location;
        far.system_center += Point(1000000*light_year, 0, 0);
        EXPECT_FALSE(star->is_really_truly_in_visible_box(Point(far)));
    }
    EXPECT_GT(visible, 30);
    zoom = old_zoom;
    global_brightness = old_brightness;
}

TEST_F(FictionalStarsTest, InvalidStagedPhysicsCannotPartiallyEnterTheUniverse)
{
    auto batch = generate_fictional_stars(model, region);
    ASSERT_FALSE(batch.stars.empty());
    batch.stars.back()->temperature = NAN;
    std::string message;
    EXPECT_FALSE(batch.install(message));
    EXPECT_EQ(ncelobjs, 1);
    EXPECT_FALSE(message.empty());
}

TEST_F(FictionalStarsTest, MidGenerationCancellationLeavesNoLiveObjects)
{
    region.star_count = 100000;
    std::atomic<bool> cancel{false};
    std::atomic<int> progress{0};
    auto future = std::async(std::launch::async, [&]()
    {
        return generate_fictional_stars(model, region, &cancel, &progress);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (progress.load() < 100 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    cancel.store(true);
    auto batch = future.get();
    EXPECT_TRUE(batch.stars.empty());
    EXPECT_FALSE(batch.error.empty());
    EXPECT_EQ(ncelobjs, 1);
}

TEST_F(FictionalStarsTest, SystemsRespectMinimumSpacingIncludingTheCenterAndClusters)
{
    for (double spacing : {1.0, 2.0, 5.0})
    {
        region.minimum_spacing_ly = spacing;
        auto batch = generate_fictional_stars(model, region);
        ASSERT_TRUE(batch.error.empty()) << spacing << ": " << batch.error;
        std::vector<Point> systems;
        for (const auto& star : batch.stars)
        {
            if (star->orbit) continue;
            Point p = star->location.system_center*(1/light_year);
            for (Point other : systems) EXPECT_GE(p.distance_to(other), spacing-1e-7);
            systems.push_back(p);
        }
        EXPECT_GT(systems.size(), 100u);
    }
}

TEST_F(FictionalStarsTest, ImpossibleSpacingReturnsAnErrorInsteadOfOverlappingSystems)
{
    region.star_count = 2;
    region.radius_ly = 1;
    region.minimum_spacing_ly = 5;
    auto batch = generate_fictional_stars(model, region);
    EXPECT_FALSE(batch.error.empty());
    EXPECT_TRUE(batch.stars.empty());
    EXPECT_EQ(ncelobjs, 1);
}

TEST_F(FictionalStarsTest, SectorCodesAreShortRestrictedAndSeparateFromPersistentIdentity)
{
    for (int i = 0; i < 1000; ++i)
    {
        const std::string code = fictional_region_id();
        ASSERT_EQ(code.size(), 4u);
        for (int digit = 0; digit < 3; ++digit)
            EXPECT_NE(std::string("2357").find(code[digit]), std::string::npos);
        EXPECT_NE(std::string("BCDJLMNPQRVW").find(code[3]), std::string::npos);
    }
    region.star_count = 1;
    region.region_id = fictional_region_id();
    region.identity = fictional_population_id();
    auto a = generate_fictional_stars(model, region);
    const std::string identity = a.stars.front()->fictional_origin.at("id");
    region.identity = fictional_population_id();
    auto b = generate_fictional_stars(model, region);
    EXPECT_NE(identity, b.stars.front()->fictional_origin.at("id"));
    std::string message;
    ASSERT_TRUE(a.install(message));
    EXPECT_NE(fictional_region_id(), region.region_id);
    const auto collision_file = temporary / "sector_collision.json";
    json collision = {{b.stars.front()->name, b.stars.front()->to_json()}};
    std::ofstream(collision_file) << collision;
    std::fstream input(collision_file, std::ios::in);
    EXPECT_FALSE(Serialization::load_all(input, cels, MAX_CELOBJS));
    EXPECT_EQ(ncelobjs, 2);
    EXPECT_EQ(static_cast<Star*>(cels[1])->fictional_origin.at("id"), identity);
}
