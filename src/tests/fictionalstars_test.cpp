#include <gtest/gtest.h>
#include <SDL_image.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include "../classes/fictionalstars.h"
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
            EXPECT_LE(star->absolute_magnitude + 5*log10(distance*light_year/(10*parsec)), 6.501) << star->name;
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
}
