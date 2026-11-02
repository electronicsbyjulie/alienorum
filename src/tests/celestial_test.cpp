#include <filesystem>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/celestial.h"
#include "../classes/planet.h"
#include "../classes/cat.h"
#include "../classes/surfacebiomes.h"
#include "../classes/surfacenavigation.h"
#include "../classes/watercoverage.h"
#include "universe_fixture.h"
#include "../loaders.h"
#include <functional>

using namespace alienorum;
using json = nlohmann::json;

class ShoreTestMap : public Map
{
    public:
    explicit ShoreTestMap(const std::function<bool(double, double)> &water, bool materials = true)
    {
        image_width = 256;
        image_height = 129;
        allocated = image_width * image_height;
        lon_scale = image_width / (2 * _pi);
        lat_scale = (image_height - 1) / _pi;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        if (materials) surface_materials.resize(allocated);
        for (unsigned long y = 0; y < image_height; ++y)
        {
            for (unsigned long x = 0; x < image_width; ++x)
            {
                const auto bounds = surface_pixel_bounds(x, y);
                const bool wet = water((bounds[0] + bounds[1]) * .5, (bounds[2] + bounds[3]) * .5);
                const unsigned long index = y * image_width + x;
                red_data[index] = wet || materials ? 20 : 160;
                green_data[index] = wet || materials ? 100 : 120;
                blue_data[index] = wet || materials ? 160 : 80;
                if (materials) surface_materials[index] = wet ? SurfaceMaterial::water : SurfaceMaterial::ground;
            }
        }
        touch_gen();
    }
};

class ShoreNavigationTest : public UniverseFixture
{
    protected:
    Planet *world(const std::function<bool(double, double)> &water, bool materials = true)
    {
        Planet *planet = make_planet(make_star(), "Shore world");
        planet->volumetric_mean_radius = 1e6;
        planet->temperature = 280;
        planet->has_water = 1;
        planet->ensure_atmosphere()->surface_pressure = oneatm;
        planet->surf_map = new ShoreTestMap(water, materials);
        return planet;
    }

    void check_boundary(SurfaceSampler &surface, const ShoreLocation &shore)
    {
        ASSERT_TRUE(shore.found);
        EXPECT_FALSE(surface.water_at(shore.latitude, shore.longitude));
        EXPECT_TRUE(surface.water_at(shore.water_latitude, shore.water_longitude));
        const double lat = sin((shore.latitude - shore.water_latitude) * .5);
        const double lon = sin((shore.longitude - shore.water_longitude) * .5);
        const double haversine = lat * lat + cos(shore.latitude) * cos(shore.water_latitude) * lon * lon;
        EXPECT_LT(2 * surface.world_radius() * asin(sqrt(haversine)), .06);
    }

    void TearDown() override
    {
        delete_the_universe();
        UniverseFixture::TearDown();
    }
};

TEST_F(ShoreNavigationTest, DetailedCoastMatchesRenderedMaterialsFromEitherSide)
{
    Planet *planet = world([](double lat, double) { return lat < 0; });
    SurfaceBiomes field(42);
    SurfaceSampler surface(planet, planet->surf_map, field, 1e6, 8);
    for (double latitude : {-.004, .004})
    {
        const auto shore = find_nearest_shore(surface, latitude, .2, latitude < 0 ? 0 : _pi);
        check_boundary(surface, shore);
        EXPECT_LT(shore.distance, 5500);
        EXPECT_GT(shore.distance, 2500);
        EXPECT_GT(fabs(shore.latitude) * 1e6, .5);
    }
}

TEST_F(ShoreNavigationTest, FacingBiasCanPreferASlightlyFartherShoreAhead)
{
    Planet *planet = world([](double lat, double) { return fabs(lat) > .03; });
    SurfaceBiomes field(42);
    SurfaceSampler surface(planet, planet->surf_map, field, 1e6, 0);
    const auto north = find_nearest_shore(surface, .006, .2, 0);
    const auto south = find_nearest_shore(surface, .006, .2, _pi);
    check_boundary(surface, north);
    check_boundary(surface, south);
    EXPECT_GT(north.latitude, .006);
    EXPECT_LT(south.latitude, .006);
    EXPECT_GT(south.distance, north.distance);
    EXPECT_LT(south.distance, 2 * north.distance);
}

TEST_F(ShoreNavigationTest, DateLineAndPolarCoastsStayFinite)
{
    Planet *planet = world([](double, double lon) { return lon > 0; });
    SurfaceBiomes field(42);
    SurfaceSampler surface(planet, planet->surf_map, field, 1e6, 8);
    const auto seam = find_nearest_shore(surface, .3, _pi - .004, half_pi);
    check_boundary(surface, seam);
    EXPECT_LT(seam.distance, 5500);
    delete planet->surf_map;
    planet->surf_map = new ShoreTestMap([](double lat, double) { return lat > 1.50; });
    SurfaceSampler polar(planet, planet->surf_map, field, 1e6, 8);
    const auto pole = find_nearest_shore(polar, half_pi, 0, 0);
    check_boundary(polar, pole);
    EXPECT_TRUE(std::isfinite(pole.longitude));
    EXPECT_LE(fabs(pole.latitude), half_pi);
    EXPECT_LT(pole.distance, 1e5);
}

TEST_F(ShoreNavigationTest, UniformWaterOrLandHasNoShore)
{
    Planet *planet = world([](double, double) { return true; });
    SurfaceBiomes field(42);
    SurfaceSampler surface(planet, planet->surf_map, field, 1e6, 8);
    EXPECT_FALSE(find_nearest_shore(surface, 0, 0, 0).found);
    delete planet->surf_map;
    planet->surf_map = new ShoreTestMap([](double, double) { return false; });
    SurfaceSampler land(planet, planet->surf_map, field, 1e6, 8);
    EXPECT_FALSE(find_nearest_shore(land, 0, 0, 0).found);
}

TEST_F(ShoreNavigationTest, ImportedColorsUseTheSameWaterRule)
{
    Planet *planet = world([](double lat, double) { return lat < 0; }, false);
    SurfaceBiomes field(42);
    SurfaceSampler surface(planet, planet->surf_map, field, 1e6, 8);
    check_boundary(surface, find_nearest_shore(surface, .004, .2, _pi));
}

class BiomePixelMap : public Map
{
    public:
    BiomePixelMap()
    {
        image_width = 128;
        image_height = 64;
        allocated = image_width * image_height;
        lon_scale = image_width / (2 * _pi);
        lat_scale = image_height / _pi;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        for (unsigned long i = 0; i < allocated; ++i)
        {
            red_data[i] = i % 256;
            green_data[i] = i / 256;
            blue_data[i] = 137;
        }
        touch_gen();
    }
};

TEST(MapTest, BiomeCapRejectsBoundariesPolesAndIncompleteMaps)
{
    BiomePixelMap map;
    RGB3 color;
    EXPECT_TRUE(map.color_within_cap(0.3, 0.4, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(0, 0.4, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(0.3, 0, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(half_pi, 0.4, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(-half_pi, 0.4, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(0.3, _pi, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(0.3, -_pi, 0.0001, color));
    EXPECT_FALSE(map.color_within_cap(0.3, 0.4, SurfaceBiomes::angular_bound(1), color));
    Map empty;
    EXPECT_FALSE(empty.color_within_cap(0.3, 0.4, 0.0001, color));
}

class UniformBiomePixelMap : public BiomePixelMap
{
    public:
    UniformBiomePixelMap()
    {
        for (unsigned long i = 0; i < allocated; ++i)
        {
            red_data[i] = 20;
            green_data[i] = 100;
            blue_data[i] = 160;
        }
    }

    void change_color_at(double latitude, double longitude)
    {
        red_data[idx_of(latitude, longitude)] = 200;
        touch_gen();
    }

    void set_material_at(double latitude, double longitude, SurfaceMaterial material)
    {
        if (surface_materials.empty()) surface_materials.assign(allocated, SurfaceMaterial::water);
        surface_materials[idx_of(latitude, longitude)] = material;
        touch_gen();
    }

    size_t cached_regions() const { return uniform_regions.size(); }
};

TEST(MapTest, UniformRegionCacheReusesTilesAndClipsMixedNeighbors)
{
    UniformBiomePixelMap map;
    RGB3 color;
    ASSERT_TRUE(map.color_within_cap(.3, .4, .1, color, true));
    const size_t count = map.cached_regions();
    EXPECT_GT(count, 0u);
    EXPECT_TRUE(map.color_within_cap(.3, .4, .1, color, true));
    EXPECT_EQ(map.cached_regions(), count);
    // A changed pixel in the same coarse tile but outside the cap is harmless.
    map.change_color_at(.9, 1.2);
    EXPECT_TRUE(map.color_within_cap(.3, .4, .1, color, true));
    map.set_material_at(.3, .4, SurfaceMaterial::vegetation);
    EXPECT_FALSE(map.color_within_cap(.3, .4, .1, color, true));
}

TEST(MapTest, IdenticalBluePixelsRetainDistinctMaterialsAcrossPngExports)
{
    UniformBiomePixelMap map;
    map.procedurally_generated = true;
    map.set_material_at(0.3, 0.4, SurfaceMaterial::vegetation);
    EXPECT_EQ(map.material_at(0.3, 0.4), SurfaceMaterial::vegetation);
    EXPECT_EQ(map.material_at(-0.3, -0.4), SurfaceMaterial::water);
    RGB3 color;
    EXPECT_FALSE(map.color_within_cap(0.3, 0.4, 0.1, color, true));
    const auto path = std::filesystem::temp_directory_path() / "alienorum_biome_export_test.png";
    ASSERT_TRUE(map.save_to_png(path.string()));
    Map loaded;
    ASSERT_TRUE(loaded.load_from_png(path.string()));
    EXPECT_TRUE(loaded.procedurally_generated);
    EXPECT_EQ(loaded.material_at(0.3, 0.4), SurfaceMaterial::vegetation);
    EXPECT_EQ(loaded.material_at(-0.3, -0.4), SurfaceMaterial::water);
    EXPECT_FALSE(loaded.color_within_cap(0.3, 0.4, 0.1, color, true));
    std::filesystem::remove(path);
    BiomePixelMap imported;
    EXPECT_EQ(imported.material_at(0.3, 0.4), SurfaceMaterial::unknown);
    Map empty;
    EXPECT_EQ(empty.material_at(0, 0), SurfaceMaterial::unknown);
}

TEST(MapTest, BiomeCapCertifiesAllPixelsAndRejectsAnInteriorColorChange)
{
    UniformBiomePixelMap map;
    RGB3 color;
    EXPECT_FALSE(map.color_within_cap(0.3, 0.4, 0.1, color));
    ASSERT_TRUE(map.color_within_cap(0.3, 0.4, 0.1, color, true));
    EXPECT_EQ(color.r, 20);
    EXPECT_EQ(color.g, 100);
    EXPECT_EQ(color.b, 160);
    map.change_color_at(0.3, 0.4);
    EXPECT_FALSE(map.color_within_cap(0.3, 0.4, 0.1, color, true));
}

TEST(MapTest, BiomeCapMatchesDisplacedColorsAcrossWorldsAndScales)
{
    BiomePixelMap map;
    int shortcuts = 0;
    for (unsigned int seed : {0u, 1u, 42u, 123456u})
    {
        SurfaceBiomes field(seed);
        for (double radius : {1000.0, 100000.0, 6371000.0})
        {
            for (double strength : {-3.0, 0.0, 1.0, 10.0})
            {
                const double angle = SurfaceBiomes::angular_bound(radius, strength);
                for (int i = 0; i < 5000; ++i)
                {
                    const double lat = -half_pi + _pi * (i + 0.5) / 5000;
                    const double lon = -3 * _pi + 6 * _pi * ((i * 997) % 5000) / 5000;
                    RGB3 color;
                    if (!map.color_within_cap(lat, lon, angle, color)) continue;
                    ++shortcuts;
                    const auto displaced = field.coordinates(radius, lat, lon, strength);
                    const RGB3 reference = map.color_at(displaced.first, displaced.second);
                    ASSERT_EQ(color.r, reference.r);
                    ASSERT_EQ(color.g, reference.g);
                    ASSERT_EQ(color.b, reference.b);
                }
            }
        }
    }
    EXPECT_GT(shortcuts, 100000);
}

class SkylineElevationMap : public Map
{
    public:
    SkylineElevationMap()
    {
        image_width = 4;
        image_height = 2;
        allocated = 8;
        lon_scale = image_width / (2.0 * _pi);
        lat_scale = image_height / _pi;
        bump_data = new double[allocated];
        for (unsigned long pixel = 0; pixel < allocated; ++pixel)
        {
            bump_data[pixel] = pixel * 100.0;
        }
    }
};

TEST(MapTest, SkylineElevationInterpolatesBetweenPixels)
{
    SkylineElevationMap map;
    EXPECT_NEAR(map.elevation_at_bilinear(_pi / 4.0, -3.0 * _pi / 4.0), 250.0, 1.0e-9);
    EXPECT_NEAR(map.elevation_at_bilinear(half_pi, 3.0 * _pi / 4.0), 150.0, 1.0e-9);
    const double delta = 1.0e-8;
    EXPECT_NEAR(map.elevation_at_bilinear(0.0, -half_pi - delta),
        map.elevation_at_bilinear(0.0, -half_pi + delta), 1.0e-5);
}

TEST(MapTest, SkylineElevationWrapsAndClamps)
{
    SkylineElevationMap map;
    const double delta = 1.0e-8;
    EXPECT_NEAR(map.elevation_at_bilinear(0.0, _pi - delta),
        map.elevation_at_bilinear(0.0, -_pi + delta), 1.0e-5);
    EXPECT_DOUBLE_EQ(map.elevation_at_bilinear(half_pi + 1.0, -_pi), 0.0);
    EXPECT_DOUBLE_EQ(map.elevation_at_bilinear(-half_pi - 1.0, -_pi), 400.0);
    Map empty;
    EXPECT_DOUBLE_EQ(empty.elevation_at_bilinear(0.0, 0.0), 0.0);
}

// =====================================================================
// Global / Helper Tests
// =====================================================================

TEST(CelestialHelperTest, UsesRockyMapEvaluation)
{
    EXPECT_TRUE(uses_rocky_map(rocky));
    EXPECT_TRUE(uses_rocky_map(icy));
    EXPECT_TRUE(uses_rocky_map(waterworld));
    EXPECT_TRUE(uses_rocky_map(lavaworld));

    // Counter-cases
    EXPECT_FALSE(uses_rocky_map(gas_giant));
    EXPECT_FALSE(uses_rocky_map(star));
    EXPECT_FALSE(uses_rocky_map(galaxy));
}

TEST(CelestialHelperTest, LimbAngleRemainsFiniteAtAndInsideTheSurface)
{
    Planet planet;
    planet.volumetric_mean_radius = 1000;
    for (double distance : {0.0, 999.0, 1000.0, 1000.001, 1200.0})
    {
        planet.tmprel = Point(0, 0, distance);
        EXPECT_TRUE(std::isfinite(planet.get_horizon_angle()));
        EXPECT_GE(planet.get_horizon_angle(), 0);
    }
}

// =====================================================================
// Atmosphere & AtmosphereComposition Tests
// =====================================================================

TEST(AtmosphereTest, CompositionEnforcesIntegrity)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Set some ridiculous values
    comp.O2_portion = 1.5;
    comp.N2_portion = -0.5;

    // Assuming enforce_integrity() clamps values between 0.0 and 1.0
    // and normalizes the sum to 1.0
    comp.enforce_integrity();

    EXPECT_GE(comp.O2_portion, 0.0);
    EXPECT_GE(comp.N2_portion, 0.0);
    EXPECT_LE(comp.O2_portion, 1.0);

    // The total of all portions should equal 1.0
    double total = comp.H2_portion + comp.He_portion + comp.N2_portion +
                   comp.O2_portion + comp.CO2_portion; // ... include all
    EXPECT_NEAR(total, 1.0, 1e-5);
}

TEST(AtmosphereTest, ManagesCompositionPointer)
{
    CelestialObject dummy;
    Atmosphere atm(&dummy);

    EXPECT_EQ(atm.comp, nullptr); // Should start null

    AtmosphereComposition* ensured = atm.ensure_composition();
    ASSERT_NE(ensured, nullptr);
    EXPECT_EQ(atm.comp, ensured);
    EXPECT_EQ(ensured->cel, &dummy);

    // Ensure calling it again returns the same pointer and doesn't leak
    AtmosphereComposition* ensured_again = atm.ensure_composition();
    EXPECT_EQ(ensured, ensured_again);
}

// =====================================================================
// Orbit Tests
// =====================================================================

TEST(OrbitTest, OpenVersusClosedOrbits)
{
    Orbit closed_orbit;
    closed_orbit.eccentricity = 0.016; // Earth-like
    EXPECT_FALSE(closed_orbit.is_open());

    Orbit parabolic_orbit;
    parabolic_orbit.eccentricity = 1.0;
    EXPECT_TRUE(parabolic_orbit.is_open());

    Orbit hyperbolic_orbit;
    hyperbolic_orbit.eccentricity = 1.5; // Oumuamua style
    EXPECT_TRUE(hyperbolic_orbit.is_open());
}

// =====================================================================
// Map Tests
// =====================================================================

TEST(MapTest, GenerationCounterTracksUpdates)
{
    Map m;
    EXPECT_EQ(m.gen, 0); // Reserved for "untouched"

    m.touch_gen();
    unsigned int first_gen = m.gen;
    EXPECT_NE(first_gen, 0);

    m.touch_gen();
    EXPECT_NE(m.gen, first_gen); // Must increment/change
}

TEST(MapTest, DataPresenceFlags)
{
    Map m;
    // Before loading anything
    EXPECT_FALSE(m.has_bump_data());
    EXPECT_FALSE(m.has_rgb_data());
    EXPECT_EQ(m.get_width(), 0);
    EXPECT_EQ(m.get_height(), 0);
}

// =====================================================================
// CelestialObject Tests
// =====================================================================

TEST(CelestialObjectTest, DefaultInitialization)
{
    CelestialObject cel;

    EXPECT_EQ(cel.seqno, -1);
    EXPECT_FALSE(cel.deleted);
    EXPECT_EQ(cel.type, star);
    EXPECT_EQ(cel.typeclass(), class_unknown);

    // Pointers should be null
    EXPECT_EQ(cel.surf_map, nullptr);
    EXPECT_EQ(cel.cloud_map, nullptr);
    EXPECT_EQ(cel.orbit, nullptr);
    EXPECT_EQ(cel.cenobj, nullptr);
    EXPECT_FALSE(cel.transparent_clouds);
}

TEST(CelestialObjectTest, TidalLockingEvaluation)
{
    CelestialObject star;
    CelestialObject planet;
    Orbit planet_orbit;

    // Link them
    planet.orbit = &planet_orbit;
    planet_orbit.center = &star;

    // Case 1: Perfectly tidally locked
    planet_orbit.period = 3600.0;
    planet.sidereal_rotational_period = 3600.0;
    EXPECT_TRUE(planet.is_tidal_locked());

    // Case 2: Within 1% tolerance (from the inline header logic)
    planet.sidereal_rotational_period = 3630.0; // ~0.8% diff
    EXPECT_TRUE(planet.is_tidal_locked());

    // Case 3: Not tidally locked (Earth-like, fast rotation relative to orbit)
    planet.sidereal_rotational_period = 100.0;
    EXPECT_FALSE(planet.is_tidal_locked());

    // Case 4: No orbit (Rogue planet or Star)
    planet.orbit = nullptr;
    EXPECT_FALSE(planet.is_tidal_locked());
}

TEST(CelestialObjectTest, LocalSolarTimeReturnsNegativeWithoutLightCenter)
{
    Star a_star;
    EXPECT_LT(a_star.local_solar_time(0.0), 0.0);

    Planet rogue_planet;
    rogue_planet.type = rocky;
    EXPECT_LT(rogue_planet.local_solar_time(0.0), 0.0);
}

TEST(CelestialObjectTest, LocalSolarTimeCalculatesCorrectHours)
{
    Star host_star;
    host_star.location.local_position = Point(0, 0, 0);

    Planet planet;
    planet.type = rocky;
    planet.location.local_position = Point(AU, 0, 0);
    planet.location.equatorial_plane.a = 0;
    planet.sidereal_rotational_period = oneday;

    planet.orbit = new Orbit();
    planet.orbit->center = &host_star;

    Point star_rel = Point(0, 0, 0) - Point(AU, 0, 0);
    Point star_body = rotate3D(star_rel, center, yaxis, planet.timeofday());
    double subsolar_lon = find_angle(star_body.z, -star_body.x);

    double noon_time = planet.local_solar_time(subsolar_lon);
    EXPECT_NEAR(noon_time, _pi, 1e-6);

    double midnight_time = planet.local_solar_time(subsolar_lon + _pi);
    EXPECT_NEAR(midnight_time, 0.0, 1e-6);

    double sunset_time = planet.local_solar_time(subsolar_lon + half_pi);
    EXPECT_NEAR(sunset_time, 1.5 * _pi, 1e-6);

    double sunrise_time = planet.local_solar_time(subsolar_lon - half_pi);
    EXPECT_NEAR(sunrise_time, 0.5 * _pi, 1e-6);
}

TEST(CelestialObjectTest, DensityCalculation)
{
    CelestialObject cel;
    // Set to roughly Earth mass and radius (in grams and meters per your header)
    cel.mass = 5.972e27;
    cel.volumetric_mean_radius = 6.371e6;

    // Density should be roughly 5514 kg/m^3
    // Note: You will have to check if your density() function returns kg/m^3 or g/cm^3
    double calculated_density = cel.density();
    EXPECT_GT(calculated_density, 0.0);
}

TEST(CelestialObjectTest, JsonSerializationRoundTrip)
{
    CelestialObject original;
    original.mass = 1.989e33; // Solar mass in grams
    original.temperature = 5778;
    original.type = gas_giant;
    original.user_added = true;
    original.transparent_clouds = true;

    json j = original.to_json();

    CelestialObject restored;
    bool success = restored.from_json(j);

    EXPECT_TRUE(success);
    EXPECT_DOUBLE_EQ(restored.mass, 1.989e33);
    EXPECT_EQ(restored.type, gas_giant);
    EXPECT_TRUE(restored.user_added);
    EXPECT_TRUE(restored.transparent_clouds);

    EXPECT_DOUBLE_EQ(restored.temperature, 5778);

    // Zero is not a temperature but the absence of one -- it is what tells
    // Star::estimate_temperature() and Planet::equilibrium_temperature() to work one out instead
    // of returning what they are holding -- so a body that has none writes no key at all rather
    // than writing a zero, and reading a file without one leaves the derivations to it.
    CelestialObject no_temperature;
    no_temperature.mass = 1.989e33;
    json jnt = no_temperature.to_json();
    EXPECT_FALSE(jnt.contains("temperature"));

    CelestialObject from_older_file;
    from_older_file.temperature = 1234;             // whatever it happened to be holding
    EXPECT_TRUE(from_older_file.from_json(jnt));
    EXPECT_DOUBLE_EQ(from_older_file.temperature, 1234);
}

TEST(CelestialObjectTest, TransparentCloudsCatalogParsing)
{
    std::ifstream ifs("catalogs/planets.json");
    ASSERT_TRUE(ifs.is_open());
    json j;
    ifs >> j;

    bool found_earth = false;
    bool found_mars = false;
    bool found_venus = false;

    for (const auto& item : j)
    {
        if (item.contains("BODYNAME"))
        {
            std::string name = item["BODYNAME"];
            if (name == "Earth")
            {
                found_earth = true;
                EXPECT_TRUE(item.contains("TransparentClouds"));
                EXPECT_TRUE(item["TransparentClouds"].get<bool>());
            }
            else if (name == "Mars")
            {
                found_mars = true;
                EXPECT_TRUE(item.contains("TransparentClouds"));
                EXPECT_TRUE(item["TransparentClouds"].get<bool>());
            }
            else if (name == "Venus")
            {
                found_venus = true;
                EXPECT_FALSE(item.contains("TransparentClouds") && item["TransparentClouds"].get<bool>());
            }
        }
    }

    EXPECT_TRUE(found_earth);
    EXPECT_TRUE(found_mars);
    EXPECT_TRUE(found_venus);
}

TEST(CelestialHelperTest, EarthCloudsRateLimiting)
{
    std::string test_ts = "maps/Earth_clouds.timestamp";
    std::string test_file = "maps/Earth_clouds.jpg";

    if (file_exists(test_file.c_str()))
    {
        std::ofstream ofs(test_ts);
        ofs << std::time(nullptr) << std::endl;
        ofs.close();

        std::string cloud_url;
        std::ifstream ifs("catalogs/planets.json");
        if (ifs.is_open())
        {
            json j;
            ifs >> j;
            for (const auto& item : j)
            {
                if (item.value("BODYNAME", "") == "Earth" && item.contains("CloudMap"))
                {
                    cloud_url = item["CloudMap"].get<std::string>();
                    break;
                }
            }
        }
        if (cloud_url.empty())
            cloud_url = "https://clouds.matteason.co.uk/images/1024x512/clouds.jpg";

        bool attempted = check_and_download_earth_clouds(cloud_url, test_file);
        EXPECT_FALSE(attempted);
    }
}

TEST(CelestialHelperTest, CloudsRateLimitingGeneralization)
{
    std::string test_ts = "maps/Mars_clouds.timestamp";
    std::string test_file = "maps/Mars_clouds.png";

    std::ofstream ofs_file(test_file);
    ofs_file << "dummy" << std::endl;
    ofs_file.close();

    std::ofstream ofs_ts(test_ts);
    ofs_ts << std::time(nullptr) << std::endl;
    ofs_ts.close();

    bool attempted = check_and_download_clouds("http://example.com/mars_clouds.png", test_file);
    EXPECT_FALSE(attempted);

    std::remove(test_file.c_str());
    std::remove(test_ts.c_str());
}

TEST(CelestialHelperTest, CloudCooldownSurvivesMissingImageAndFutureClock)
{
    const std::string path = "maps/cloud_cooldown_test.jpg";
    const std::string stamp = "maps/cloud_cooldown_test.timestamp";
    std::remove(path.c_str());
    for (const auto offset : {0, 3 * 3600 - 60, -3600})
    {
        std::ofstream output(stamp);
        output << std::time(nullptr) - offset << std::endl;
        output.close();
        EXPECT_FALSE(check_and_download_clouds("invalid://cloud-cooldown-test", path));
        EXPECT_FALSE(file_exists(path.c_str()));
        EXPECT_FALSE(file_exists("maps/cloud_cooldown_test.attempt"));
    }
    std::remove(stamp.c_str());
}

TEST(CelestialHelperTest, CloudAttemptCooldownSurvivesMissingImage)
{
    const std::string path = "maps/cloud_attempt_test.jpg";
    const std::string stamp = "maps/cloud_attempt_test.attempt";
    std::remove(path.c_str());
    std::ofstream output(stamp);
    output << std::time(nullptr) << std::endl;
    output.close();
    const auto modified = std::filesystem::last_write_time(stamp);
    EXPECT_FALSE(check_and_download_clouds("invalid://cloud-attempt-test", path));
    EXPECT_EQ(std::filesystem::last_write_time(stamp), modified);
    std::remove(stamp.c_str());
}

TEST(CelestialObjectTest, MergedMapFallbackAndSelection)
{
    EXPECT_TRUE(show_clouds);

    CelestialObject cel;
    cel.type = rocky;
    cel.transparent_clouds = false;

    cel.surf_map = (Map*)0x1234;
    cel.cloud_map = (Map*)0x5678;
    EXPECT_EQ(cel.get_day_map(), (Map*)0x5678);

    // When clouds are hidden, surface map is preferred over cloud map
    show_clouds = false;
    EXPECT_EQ(cel.get_day_map(), (Map*)0x1234);

    show_clouds = true;
    cel.cloud_map = nullptr;
    EXPECT_EQ(cel.get_day_map(), (Map*)0x1234);

    cel.surf_map = nullptr;
    EXPECT_EQ(cel.get_day_map(), nullptr);

    // Incomplete cloud map fallback: uses surf_map when cloud_map is not complete
    Map surf;
    Map incomplete_clouds;
    cel.surf_map = &surf;
    cel.cloud_map = &incomplete_clouds;
    cel.transparent_clouds = true;
    EXPECT_FALSE(incomplete_clouds.is_complete());
    EXPECT_EQ(cel.get_day_map(), &surf);
    EXPECT_EQ(cel.get_night_map(), nullptr);
    cel.surf_map = nullptr;
    cel.cloud_map = nullptr;
}

// =====================================================================
// Math Implementations
// =====================================================================

TEST(CelestialMathTest, DistanceFromMagnitudes)
{
    // The sun at 10 parsecs has apparent mag == absolute mag
    double dist = CelestialObject::distance_from_magnitudes(4.83, 4.83);
    
    // Result should be exactly 10 parsecs
    // Adjust "parsec * 10" below if your 'parsec' constant is scoped differently
    EXPECT_NEAR(dist, parsec * 10.0, 1.0);

    // Sirius: Apparent -1.46, Absolute 1.43
    // Distance modulus m - M = -2.89. Distance should be ~2.64 parsecs.
    double dist_sirius = CelestialObject::distance_from_magnitudes(-1.46, 1.43);
    EXPECT_NEAR(dist_sirius, parsec * 2.642, parsec * 0.01);
}

TEST(CelestialMathTest, DensityCalculation)
{
    CelestialObject earth;
    
    // Set to Earth's mass in grams and radius in meters
    earth.mass = 5.9722e27;
    earth.volumetric_mean_radius = 6.371e6; 
    
    double actual_density = earth.density();
    
    // Earth's average density is roughly 5.514 g/cm^3
    EXPECT_NEAR(actual_density, 5.514, 0.05);

    // Edge case: zero radius or mass
    earth.volumetric_mean_radius = 0;
    EXPECT_DOUBLE_EQ(earth.density(), 0.0);
}

TEST(CelestialMathTest, HillSphereRadius)
{
    CelestialObject sun;
    sun.mass = 1.989e33; // grams

    CelestialObject earth;
    earth.mass = 5.9722e27; // grams
    
    Orbit earth_orbit;
    earth_orbit.center = &sun;
    earth_orbit.semimajor_axis = 1.496e11; // 1 AU in meters
    earth_orbit.eccentricity = 0.0167;     // Earth eccentricity

    earth.orbit = &earth_orbit;

    double hill_radius = earth.Hill_sphere_radius();
    
    // Earth's Hill sphere is roughly 1.47 million km (1.496e9 m from the semimajor axis), and
    // the formula takes it at periapsis, a(1-e), which trims it to 1.4714e9 -- not to 1.44e9.
    EXPECT_NEAR(hill_radius, 1.4714e9, 1e7);
    
    // Edge case: Unbound orbit
    earth_orbit.eccentricity = 1.5;
    EXPECT_DOUBLE_EQ(earth.Hill_sphere_radius(), 0.0);
}

TEST(CelestialMathTest, RocheLimit)
{
    CelestialObject earth;
    earth.mass = 5.9722e27; // grams
    earth.volumetric_mean_radius = 6.371e6; // meters
    
    // Using a fluid satellite (e.g., a waterworld)
    CelestialObject fluid_moon;
    fluid_moon.mass = 7.342e25; // Moon mass in grams
    fluid_moon.volumetric_mean_radius = 1.737e6; // Moon radius in meters
    fluid_moon.type = waterworld; 
    
    double roche_fluid = earth.Roche_limit(&fluid_moon);
    
    // Fluid Roche limit for Earth-Moon densities is roughly 18,000 km (1.8e7 meters)
    EXPECT_NEAR(roche_fluid, 1.83e7, 1e6);
    
    // Using a rigid satellite (e.g., rocky)
    CelestialObject rigid_moon;
    rigid_moon.mass = 7.342e25; 
    rigid_moon.volumetric_mean_radius = 1.737e6;
    rigid_moon.type = rocky;
    
    double roche_rigid = earth.Roche_limit(&rigid_moon);
    
    // Rigid Roche limit should be significantly smaller (constant is ~1.26 vs ~2.44)
    EXPECT_LT(roche_rigid, roche_fluid);
    
    // Null orbiter defaults to rigid lunar density
    double roche_null = earth.Roche_limit(nullptr);
    EXPECT_GT(roche_null, 0.0);
}

TEST(CelestialMathTest, EstimateSurfaceGravity)
{
    CelestialObject earth;
    // Set to exactly 1 Earth mass and 1 Earth radius
    // Modify earth_mass and earth_radius below to match your actual variables
    earth.mass = earth_mass; 
    earth.volumetric_mean_radius = earth_radius;
    
    double gravity = earth.estimate_surface_gravity();
    
    // Should be exactly 1.0 Gs
    EXPECT_DOUBLE_EQ(gravity, 1.0);
    
    // Planet with 2x mass and same radius should have 2.0 Gs
    CelestialObject heavy_planet;
    heavy_planet.mass = earth_mass * 2.0;
    heavy_planet.volumetric_mean_radius = earth_radius;
    
    EXPECT_DOUBLE_EQ(heavy_planet.estimate_surface_gravity(), 2.0);
}

// =====================================================================
// Global Arrays & Indices Fixture
// =====================================================================

class CelestialGlobalsTest : public ::testing::Test
{
    protected:
    void SetUp() override
    {
        // This runs before EVERY test. main() (alienorum.cpp) is what normally allocates cels,
        // and no test binary has one, so append_cel() would write through a null pointer.
        if (!cels) cels = new CelestialObject*[MAX_CELOBJS];
        memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
        ncelobjs = 0;
        first_sat = -1;

        // Clear the STL containers so tests don't pollute each other. set_center_objects() gives
        // first_letter_index 36 buckets -- ten digits and twenty-six letters, case folded -- and
        // is the only thing that fills it; see AppendCel_UpdatesFirstLetterIndex below.
        first_letter_index.clear();
        first_letter_index.resize(36);

        constellation_index.clear();
    }

    void TearDown() override
    {
        // This runs after EVERY test. append_cel() does not take ownership -- it only records the
        // pointer -- so each test deletes what it allocated, and we drop the dangling entries here
        // so the next test starts on an empty array.
        memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
        ncelobjs = 0;
        first_sat = -1;
    }
};

// =====================================================================
// append_cel() Logic Tests
// =====================================================================

// Note the use of TEST_F instead of TEST to tie it to the fixture
TEST_F(CelestialGlobalsTest, AppendCel_AssignsSeqno)
{
    CelestialObject* obj = new CelestialObject();
    
    // Assuming seqno starts at -1 per the header
    EXPECT_EQ(obj->seqno, -1);
    
    bool success = append_cel(obj);
    
    EXPECT_TRUE(success);
    
    // append_cel should assign the object its index in the cels array
    EXPECT_GE(obj->seqno, 0);
    EXPECT_EQ(cels[obj->seqno], obj);
    
    delete obj;
}

TEST_F(CelestialGlobalsTest, AppendCel_UpdatesFirstLetterIndex)
{
    CelestialObject* obj = new CelestialObject();
    
    // Set a predictable name
    strcpy(obj->name, "Earth");
    obj->namelen = 0;
    
    append_cel(obj);
    
    // The name indices are NOT append_cel()'s doing: set_center_objects() (housekeeping.cpp)
    // rebuilds first_letter_index from scratch over the whole array once loading has settled,
    // because it also has to skip deleted objects and the HD numbers that already have a catalog
    // of their own. append_cel() is only responsible for the array itself and the object's name,
    // which it copies to origname so a later rename can still be traced back.
    EXPECT_TRUE(first_letter_index[(int)'E' - 'A' + 10].empty());
    EXPECT_EQ(obj->origname, std::string("Earth"));
    EXPECT_EQ(cels[obj->seqno], obj);
    
    delete obj;
}

TEST_F(CelestialGlobalsTest, AppendCel_UpdatesConstellationIndex)
{
    // Assuming constellation_index maps standard 3-letter IAU codes
    CelestialObject* star = new CelestialObject();
    
    // Set whatever properties append_cel looks for to 
    // identify the constellation. For example, if it uses a Star object:
    // Star* star = new Star();
    // strcpy(star->constellation, "Ori");
    
    // For a generic CelestialObject, maybe you map by a string property if one exists:
    // This relies heavily on your cpp implementation!
    
    bool success = append_cel(star);
    EXPECT_TRUE(success);
    
    // As above, the constellation map is built by set_center_objects() and only for objects
    // that are Stars carrying a constellation, so a bare CelestialObject leaves it empty.
    EXPECT_TRUE(constellation_index.empty());
    
    delete star;
}

TEST_F(CelestialGlobalsTest, AppendCel_HandlesFullCapacity)
{
    // The limit is MAX_CELOBJS and append_cel() stops one short of it, so that cels[ncelobjs] is
    // always a null terminator for the many loops that walk the array until they find one. Filling
    // half a million slots with real objects would take a while and most of a gigabyte, so the
    // count is moved instead: everything append_cel() checks is in ncelobjs.
    CelestialObject* fits = new CelestialObject();
    ncelobjs = MAX_CELOBJS - 2;
    EXPECT_TRUE(append_cel(fits));
    EXPECT_EQ(fits->seqno, MAX_CELOBJS - 2);
    EXPECT_EQ(cels[MAX_CELOBJS - 2], fits);
    EXPECT_EQ(cels[MAX_CELOBJS - 1], nullptr) << "the terminator has to stay inside the array";

    // And the next one is refused rather than written past the end.
    CelestialObject* one_too_many = new CelestialObject();
    EXPECT_FALSE(append_cel(one_too_many));
    EXPECT_EQ(one_too_many->seqno, -1) << "a refused object should not think it was added";

    delete fits;
    delete one_too_many;
}

// =====================================================================
// Orbit: the two computations that are each other's inverse
// =====================================================================

TEST(OrbitTest, PeriodAndSemimajorAxisAreInverses)
{
    CelestialObject sun;
    sun.mass = solar_mass;
    sun.type = star;

    Orbit orbit;
    orbit.center = &sun;
    orbit.semimajor_axis = AU;
    orbit.compute_period();

    // Kepler's third law, in the units the program keeps: one AU around one solar mass is a year.
    EXPECT_NEAR(orbit.period, oneyear, oneyear * 0.001);

    // And back again.
    double remembered = orbit.semimajor_axis;
    orbit.semimajor_axis = 0;
    orbit.compute_semimajor_axis();
    EXPECT_NEAR(orbit.semimajor_axis, remembered, remembered * 1e-9);

    // Four times the distance is eight times the period.
    orbit.semimajor_axis = 4 * AU;
    orbit.compute_period();
    EXPECT_NEAR(orbit.period, 8 * oneyear, oneyear * 0.01);
}

TEST(OrbitTest, AnOpenOrbitHasNoPeriod)
{
    CelestialObject sun;
    sun.mass = solar_mass;
    sun.type = star;

    Orbit orbit;
    orbit.center = &sun;
    orbit.semimajor_axis = AU;
    orbit.eccentricity = 1.5;                   // hyperbolic
    orbit.period = 12345;
    orbit.compute_period();
    EXPECT_DOUBLE_EQ(orbit.period, 0) << "it never comes back, so there is nothing to time";
}

TEST(OrbitTest, WithoutACenterNothingIsComputed)
{
    Orbit orphan;
    orphan.semimajor_axis = AU;
    orphan.period = 0;
    orphan.compute_period();
    EXPECT_DOUBLE_EQ(orphan.period, 0);         // and no dereference of the null center
}

TEST(OrbitTest, JsonRoundTripKeepsTheAngles)
{
    // The file states angles in degrees and the program keeps them in radians, so every one of
    // these crosses a conversion on the way out and another on the way back.
    Orbit original;
    original.semimajor_axis = 2.5 * AU;
    original.eccentricity = 0.42;
    original.inclination = 0.3;
    original.ascending_node = 1.1;
    original.arg_periapsis = 2.2;
    original.mean_anomaly = 3.3;
    original.period = 4 * oneyear;
    original.epoch = 2451545.0;

    json j = original.to_json();
    Orbit restored;
    EXPECT_TRUE(restored.from_json(j));

    EXPECT_NEAR(restored.semimajor_axis, 2.5 * AU, 1);
    EXPECT_NEAR(restored.eccentricity, 0.42, 1e-12);
    EXPECT_NEAR(restored.inclination, 0.3, 1e-12);
    EXPECT_NEAR(restored.ascending_node, 1.1, 1e-12);
    EXPECT_NEAR(restored.arg_periapsis, 2.2, 1e-12);
    EXPECT_NEAR(restored.mean_anomaly, 3.3, 1e-12);
    EXPECT_NEAR(restored.period, 4 * oneyear, 1);
    EXPECT_DOUBLE_EQ(restored.epoch, 2451545.0);

    // Written in degrees, not radians -- a reader outside this program has to be able to make
    // sense of the file.
    EXPECT_NEAR(j["inclination"].get<double>(), 0.3 * fiftyseven, 1e-9);
}

// =====================================================================
// Right ascension and declination, as text
// =====================================================================

TEST(CoordinateTextTest, RightAscensionRoundTrip)
{
    CelestialObject cel;

    // Vega: 18h 36m 56.3s.
    cel.RA_from_hms("18:36:56.3");
    EXPECT_NEAR(cel.right_ascension, (18 + 36/60.0 + 56.3/3600.0) * 15 * fiftyseventh, 1e-9);
    EXPECT_EQ(cel.RA_as_hms(0), "18:36:56.3");

    // Zero pads to two digits in each field rather than printing "0:0:0.0".
    cel.RA_from_hms("00:00:00.0");
    EXPECT_DOUBLE_EQ(cel.right_ascension, 0);
    EXPECT_EQ(cel.RA_as_hms(0), "00:00:00.0");

    // A single digit in the seconds field keeps its leading zero on the way out.
    cel.RA_from_hms("05:06:07.0");
    EXPECT_EQ(cel.RA_as_hms(0), "05:06:07.0");

    // Seconds that round up to a full minute carry, rather than printing "05:06:60.0".
    cel.right_ascension = (5 + 6/60.0 + 59.97/3600.0) * 15 * fiftyseventh;
    EXPECT_EQ(cel.RA_as_hms(0), "05:07:00.0");
}

TEST(CoordinateTextTest, DeclinationRoundTrip)
{
    CelestialObject cel;

    // Vega: +38d 47' 01". The seconds are rounded on the way out, not truncated -- truncating
    // cost a second every time a coordinate was displayed, and the object editor reads back what
    // it displays, so a body edited a few times walked steadily south.
    cel.Decl_from_degms("+38:47:01");
    EXPECT_NEAR(cel.declination, (38 + 47/60.0 + 1/3600.0) * fiftyseventh, 1e-9);
    EXPECT_EQ(cel.Decl_as_degms(), "+38:47:01");

    // Rounding that fills the seconds field carries into the minutes rather than printing ":60".
    cel.declination = (38 + 47/60.0 + 59.7/3600.0) * fiftyseventh;
    EXPECT_EQ(cel.Decl_as_degms(), "+38:48:00");
    cel.declination = (38 + 59/60.0 + 59.7/3600.0) * fiftyseventh;
    EXPECT_EQ(cel.Decl_as_degms(), "+39:00:00");

    // South of the equator, where the sign is the whole difference.
    cel.Decl_from_degms("-16:42:58");
    EXPECT_LT(cel.declination, 0);
    EXPECT_NEAR(cel.declination, -(16 + 42/60.0 + 58/3600.0) * fiftyseventh, 1e-9);
    EXPECT_EQ(cel.Decl_as_degms(), "-16:42:58");

    // A northern declination is often typed without its plus, and used to be read with an
    // uninitialized sign -- so it came out north or south depending on the stack.
    cel.Decl_from_degms("38:47:01");
    EXPECT_GT(cel.declination, 0);

    // Nothing at all is zero, not garbage.
    cel.Decl_from_degms("");
    EXPECT_DOUBLE_EQ(cel.declination, 0);
    cel.RA_from_hms("");
    EXPECT_DOUBLE_EQ(cel.right_ascension, 0);
}

// =====================================================================
// Where an orbit puts a body
// =====================================================================

TEST(OrbitLocationTest, ACircularOrbitComesBackToWhereItStarted)
{
    // update_orbit_location() is what every moving body's position goes through, and it is
    // exercised here through Planet::update_location(), which is how the program calls it.
    CelestialObject sun;
    sun.mass = solar_mass;
    sun.type = star;
    sun.location.local_position = Point(0, 0, 0);

    Planet world;
    world.mass = earth_mass;
    world.volumetric_mean_radius = earth_radius;
    world.type = rocky;
    world.orbit = new Orbit();
    world.orbit->center = &sun;
    world.orbit->semimajor_axis = AU;
    world.orbit->eccentricity = 0;
    world.orbit->mean_anomaly = 0;
    world.orbit->epoch = J2000;
    world.orbit->compute_period(world.mass);
    double period = world.orbit->period;

    world.update_location(J2000_TIME_T);
    Point at_epoch = world.location.local_position;

    // A circle of one AU: that is how far from the star it is, whenever you look.
    EXPECT_NEAR(at_epoch.magnitude(), AU, AU * 1e-6);

    world.update_location(J2000_TIME_T + period/4);
    Point quarter = world.location.local_position;
    EXPECT_NEAR(quarter.magnitude(), AU, AU * 1e-6);
    EXPECT_NEAR(at_epoch.distance_to(quarter), AU * std::sqrt(2.0), AU * 1e-3)
        << "a quarter of the way round a circle is a right angle";

    world.update_location(J2000_TIME_T + period/2);
    EXPECT_NEAR(at_epoch.distance_to(world.location.local_position), 2 * AU, AU * 1e-3)
        << "half way round is the other side";

    world.update_location(J2000_TIME_T + period);
    EXPECT_NEAR(at_epoch.distance_to(world.location.local_position), 0, AU * 1e-3)
        << "a whole period later it is back where it began";
}

TEST(OrbitLocationTest, AnEccentricOrbitIsNearestAtPeriapsis)
{
    CelestialObject sun;
    sun.mass = solar_mass;
    sun.type = star;

    Planet world;
    world.mass = earth_mass;
    world.type = rocky;
    world.orbit = new Orbit();
    world.orbit->center = &sun;
    world.orbit->semimajor_axis = AU;
    world.orbit->eccentricity = 0.5;
    world.orbit->mean_anomaly = 0;                  // at periapsis at the epoch
    world.orbit->epoch = J2000;
    world.orbit->compute_period(world.mass);

    world.update_location(J2000_TIME_T);
    double at_periapsis = world.location.local_position.magnitude();
    EXPECT_NEAR(at_periapsis, AU * 0.5, AU * 1e-3);         // a(1-e)

    world.update_location(J2000_TIME_T + world.orbit->period/2);
    double at_apoapsis = world.location.local_position.magnitude();
    EXPECT_NEAR(at_apoapsis, AU * 1.5, AU * 1e-3);          // a(1+e)

    EXPECT_GT(at_apoapsis, at_periapsis);
}

TEST(OrbitLocationTest, ABodyWithNoCenterStaysPut)
{
    Planet rogue;
    rogue.mass = earth_mass;
    rogue.type = rocky;
    rogue.location.local_position = Point(1, 2, 3);

    rogue.update_location(J2000_TIME_T);                    // no orbit at all
    EXPECT_DOUBLE_EQ(rogue.location.local_position.x, 1);

    rogue.orbit = new Orbit();                              // an orbit, but around nothing
    rogue.update_location(J2000_TIME_T);
    EXPECT_DOUBLE_EQ(rogue.location.local_position.x, 1);
}

// =====================================================================
// Map
// =====================================================================

TEST(MapTest, AnEmptyMapAnswersSafely)
{
    // Every accessor has to survive being asked before a loader or generator has established the
    // geometry: a Map with no pixels is what a body has until its textures are made, and the
    // drawing code asks anyway.
    Map m;
    EXPECT_EQ(m.get_width(), 0);
    EXPECT_EQ(m.get_height(), 0);
    EXPECT_FALSE(m.has_rgb_data());
    EXPECT_FALSE(m.has_bump_data());

    // No pixel to point at, so nothing is read from beyond the end. White is the deliberate
    // answer for a map that is not ready -- it multiplies through the body's own color and
    // leaves it as it is, where a black would blot the body out while its textures are building.
    EXPECT_DOUBLE_EQ(m.elevation_at(0.5, 1.0), 0);
    RGB3 c = m.color_at(0.5, 1.0);
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.g, 255);
    EXPECT_EQ(c.b, 255);

    // Including at the poles and the date line, where the index arithmetic wraps.
    EXPECT_DOUBLE_EQ(m.elevation_at(_pi/2, 0), 0);
    EXPECT_DOUBLE_EQ(m.elevation_at(-_pi/2, 0), 0);
    EXPECT_DOUBLE_EQ(m.elevation_at(0, _pi), 0);
    EXPECT_DOUBLE_EQ(m.elevation_at(0, -_pi), 0);
}

TEST(MapTest, ExportsNothingFromAnEmptyMap)
{
    // gputex asks for the pixels in bulk; from a map with none, it must get zeros rather than a
    // read past the end of a null array.
    Map m;
    unsigned char rgba[16];
    float bump[4];
    memset(rgba, 0xAB, sizeof(rgba));
    memset(bump, 0xAB, sizeof(bump));

    m.export_rgba(rgba);
    m.export_bump(bump);

    // Nothing was written, because there is nothing to write; the point is that it did not crash
    // and did not read from a null channel array.
    SUCCEED();
}

TEST(MapTest, GenerationStampsAreNeverReusedAndNeverZero)
{
    // gputex.h tells a stale texture from a current one by this number alone, so two maps must
    // never share one and it must never come back to the value that means "never touched".
    Map a, b;
    EXPECT_EQ(a.gen, 0);
    EXPECT_EQ(b.gen, 0);

    a.touch_gen();
    b.touch_gen();
    EXPECT_NE(a.gen, 0);
    EXPECT_NE(b.gen, 0);
    EXPECT_NE(a.gen, b.gen);

    unsigned int previous = b.gen;
    for (int i = 0; i < 100; i++)
    {
        b.touch_gen();
        EXPECT_NE(b.gen, 0);
        EXPECT_NE(b.gen, previous);
        EXPECT_NE(b.gen, a.gen);
        previous = b.gen;
    }
}

TEST(MapTest, GrayscaleBumpMapsApplyTheDatumInsideTheScale)
{
    const std::string path = "maps/Earth_bump.png";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "Earth bump image is absent";
    Map map;
    ASSERT_TRUE(map.load_from_png(path));
    const double lat = 25 * fiftyseventh, lon = 0;
    const RGB3 pixel = map.color_at(lat, lon);
    ASSERT_TRUE(map.load_from_png(path, true, 20000));
    EXPECT_NEAR(map.elevation_at(lat, lon), 20000 * (pixel.r / 255.0 - 0.5), 1e-8);
}

class PngExportMap : public Map
{
    public:
    explicit PngExportMap(bool relief = false)
    {
        image_width = image_height = 2;
        allocated = 4;
        lat_scale = image_height / _pi;
        lon_scale = image_width / (2 * _pi);
        red_data = new unsigned char[4]{10, 10, 10, 10};
        green_data = new unsigned char[4]{20, 20, 20, 20};
        blue_data = new unsigned char[4]{30, 30, 30, 30};
        if (relief) bump_data = new double[4]{7, 7, 7, 7};
    }
};

TEST(MapTest, RepeatedPngExportsRetainPixelsAndReleaseTemporaryStorage)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum_png_export_test.png";
    PngExportMap map;
    for (int i = 0; i < 8; ++i) ASSERT_TRUE(map.save_to_png(path.string()));
    Map loaded;
    ASSERT_TRUE(loaded.load_from_png(path.string()));
    const RGB3 pixel = loaded.color_at(0, 0);
    EXPECT_EQ(pixel.r, 10);
    EXPECT_EQ(pixel.g, 20);
    EXPECT_EQ(pixel.b, 30);
    std::filesystem::remove(path);
}

TEST(MapTest, PngBumpExportRejectsMissingReliefAndHandlesFlatMaps)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum_png_bump_export_test.png";
    PngExportMap colors;
    EXPECT_FALSE(colors.save_to_png(path.string(), true));
    PngExportMap flat(true);
    ASSERT_TRUE(flat.save_to_png(path.string(), true));
    Map loaded;
    ASSERT_TRUE(loaded.load_from_png(path.string()));
    const RGB3 pixel = loaded.color_at(0, 0);
    EXPECT_EQ(pixel.r, 128);
    EXPECT_EQ(pixel.g, 128);
    EXPECT_EQ(pixel.b, 128);
    std::filesystem::remove(path);
}

TEST(MapTest, PngExportReportsBufferedWriteFailures)
{
    if (!std::filesystem::exists("/dev/full")) GTEST_SKIP() << "A failing output device is unavailable";
    PngExportMap map;
    EXPECT_FALSE(map.save_to_png("/dev/full"));
}

TEST(OsculatingElementTest, ShortFilesAndBoundaryEpochs)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum-orbit-samples.txt";
    uint64_t count = 9;
    std::filesystem::remove(path);
    EXPECT_EQ(OsculatingElement::read_from_file(path.string(), &count), nullptr);
    EXPECT_EQ(count, 0u);
    {
        std::ofstream out(path);
        out << "$$SOE\n EC=0.1\n$$EOE\n";
    }
    EXPECT_EQ(OsculatingElement::read_from_file(path.string(), &count), nullptr);
    EXPECT_EQ(count, 0u);
    {
        std::ofstream out(path);
        out << "$$SOE\n2451545\n EC=0.1 A =100 PR=86400 Tp=2451545\n"
            << "2451546\n EC=0.3 A =200 PR=86400 Tp=2451546\n$$EOE\n";
    }
    Orbit orbit;
    orbit.osculating = OsculatingElement::read_from_file(path.string(), &orbit.num_osc);
    ASSERT_NE(orbit.osculating, nullptr);
    ASSERT_EQ(orbit.num_osc, 2u);
    for (double epoch : {2451545.0, 2451545.5, 2451546.0})
    {
        double n, i, w, a, e, m, p, precn, procarg, effe;
        orbit.interpolate_osculating_e(epoch, n, i, w, a, e, m, p, precn, procarg, effe);
        EXPECT_NEAR(a, 100000 + 100000 * (epoch - 2451545), 1e-6);
        EXPECT_NEAR(e, 0.1 + 0.2 * (epoch - 2451545), 1e-12);
        EXPECT_DOUBLE_EQ(p, 86400);
    }
    std::filesystem::remove(path);
}

TEST(OsculatingElementTest, RejectsUnorderedEpochs)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum-orbit-order.txt";
    {
        std::ofstream out(path);
        out << "$$SOE\n2451545\n2451545\n$$EOE\n";
    }
    uint64_t count = 9;
    EXPECT_EQ(OsculatingElement::read_from_file(path.string(), &count), nullptr);
    EXPECT_EQ(count, 0u);
    std::filesystem::remove(path);
}

TEST(CelestialRandomTest, MissingAndExtremeInputsRemainRepeatable)
{
    CelestialObject body;
    Orbit orbit;
    body.orbit = &orbit;
    for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), 1e300})
    {
        body.mass = body.volumetric_mean_radius = body.orbit->semimajor_axis = value;
        body.orbit->arg_periapsis = value;
        body.rnd_seed = 0;
        body.randomize();
        int first = body.cel_rand();
        body.randomize();
        EXPECT_EQ(body.cel_rand(), first);
    }
    body.rnd_seed = 12345;
    body.randomize();
    int first = body.cel_rand();
    body.randomize();
    EXPECT_EQ(body.rnd_seed, 12345u);
    EXPECT_EQ(body.cel_rand(), first);
}

TEST(CelestialMetadataTest, RoleAndMapOrientationSurviveSerialization)
{
    CelestialObject source;
    strcpy(source.name, "Synthetic home");
    source.home_world = true;
    source.map_inverted = true;
    source.orbit_center_codes = {"AB", "CD"};
    CelestialObject restored;
    ASSERT_TRUE(restored.from_json(source.to_json()));
    EXPECT_TRUE(restored.home_world);
    EXPECT_TRUE(restored.map_inverted);
    EXPECT_EQ(restored.orbit_center_codes, source.orbit_center_codes);
}

TEST(CelestialLocaleTest, FindsClosestNamedLocaleWithinFiveKilometers)
{
    CelestialObject body;
    body.volumetric_mean_radius = earth_radius;
    Locale places[3];
    places[0].name = "Farther town";
    places[0].lat = 0;
    places[0].lon = 0.035;
    places[1].name = "Closer town";
    places[1].lat = 0;
    places[1].lon = 0.018;
    places[2].name = "";
    places[2].lat = places[2].lon = 0;
    body.locales = places;
    body.nlocales = 3;
    EXPECT_EQ(body.nearest_locale(0, 0, 5000), &places[1]);
    EXPECT_EQ(body.nearest_locale(0, 0, 1000), nullptr);
    EXPECT_EQ(body.nearest_locale(0, 1 * fiftyseventh, 5000), nullptr);
    body.locales = nullptr;
}

TEST(CelestialLocaleTest, HandlesLongitudeSeamPolesAndDifferentBodySizes)
{
    CelestialObject body;
    body.volumetric_mean_radius = earth_radius;
    Locale place;
    place.name = "Surface site";
    place.lat = 0;
    place.lon = 179.99;
    body.locales = &place;
    body.nlocales = 1;
    EXPECT_EQ(body.nearest_locale(0, -179.99 * fiftyseventh, 5000), &place);
    place.lat = 89.99;
    place.lon = 180;
    EXPECT_EQ(body.nearest_locale(89.99 * fiftyseventh, 0, 5000), &place);
    place.lat = 0;
    place.lon = 0.1;
    EXPECT_EQ(body.nearest_locale(0, 0, 5000), nullptr);
    body.volumetric_mean_radius = 1e6;
    EXPECT_EQ(body.nearest_locale(0, 0, 5000), &place);
    body.locales = nullptr;
}

TEST(CelestialLocaleTest, RejectsInvalidPositionsAndMissingLocales)
{
    CelestialObject body;
    body.volumetric_mean_radius = earth_radius;
    EXPECT_EQ(body.nearest_locale(0, 0, 5000), nullptr);
    Locale place;
    place.name = "Surface site";
    place.lat = place.lon = 0;
    body.locales = &place;
    body.nlocales = 1;
    EXPECT_EQ(body.nearest_locale(std::numeric_limits<double>::quiet_NaN(), 0, 5000), nullptr);
    EXPECT_EQ(body.nearest_locale(0, 0, -1), nullptr);
    place.lat = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(body.nearest_locale(0, 0, 5000), nullptr);
    place.lat = 0;
    body.volumetric_mean_radius = 0;
    EXPECT_EQ(body.nearest_locale(0, 0, 5000), nullptr);
    body.locales = nullptr;
}

TEST(CelestialLocaleTest, InvertedMapLocalesRetainDegreeCoordinates)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum-locale-inversion-test.json";
    const json data = {{"Synthetic body", json::array({json::object(), {{"name", "Surface site"},
        {"latitude", 30}, {"longitude", 120}}})}};
    {
        std::ofstream output(path);
        output << data;
    }
    CelestialObject body;
    strcpy(body.name, "Synthetic body");
    body.volumetric_mean_radius = earth_radius;
    body.map_inverted = true;
    EXPECT_EQ(body.read_locales(path.string()), 1);
    std::filesystem::remove(path);
    EXPECT_TRUE(body.locales_loaded);
    ASSERT_EQ(body.nlocales, 2);
    EXPECT_DOUBLE_EQ(body.locales[1].lat, -30);
    EXPECT_DOUBLE_EQ(body.locales[1].lon, -120);
    EXPECT_EQ(body.nearest_locale(-30 * fiftyseventh, -120 * fiftyseventh, 5000), &body.locales[1]);
    delete[] body.locales;
    body.locales = nullptr;
}

class CoverageTestMap : public ShoreTestMap
{
    public:
    double scale;
    CoverageTestMap(double bump_scale = 1000) : ShoreTestMap([](double, double) { return false; }), scale(bump_scale)
    {
        bump_data = new double[allocated];
        profile([](double latitude) { return pow((sin(latitude) + 1) * .5, 2); });
    }
    void imported() { surface_materials.clear(); touch_gen(); }
    void profile(const std::function<double(double)> &height)
    {
        for (unsigned long row = 0; row < image_height; ++row)
        {
            const auto bounds = surface_pixel_bounds(0, row);
            const double elevation = scale * (height((bounds[0] + bounds[1]) * .5) - .5);
            for (unsigned long col = 0; col < image_width; ++col) bump_data[row * image_width + col] = elevation;
        }
        touch_gen();
    }
};

TEST_F(ShoreNavigationTest, WaterCoverageConvertsSurfaceAreaInsteadOfScalingWaterLevel)
{
    Planet *planet = world([](double, double) { return false; });
    delete planet->surf_map;
    planet->surf_map = new CoverageTestMap();
    planet->m_bump_scale = 1000;
    planet->temperature = 320;
    planet->sidereal_rotational_period = oneday;
    planet->has_water = .25;
    WaterCoverage coverage;
    coverage.refresh(planet, 0);
    ASSERT_TRUE(coverage.editable());
    EXPECT_NEAR(coverage.percent(.25), 50, 1.6);
    EXPECT_NEAR(coverage.level(50), .25, .02);
    EXPECT_NEAR(coverage.percent(coverage.level(71)), 71, 1.6);
    EXPECT_FLOAT_EQ(planet->has_water, .25);
    EXPECT_FLOAT_EQ(coverage.level(-10), 0);
    EXPECT_FLOAT_EQ(coverage.level(0), 0);
    EXPECT_FLOAT_EQ(coverage.level(100), 1);
    EXPECT_FLOAT_EQ(coverage.level(200), 1);
    EXPECT_FLOAT_EQ(coverage.level(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_DOUBLE_EQ(coverage.percent(0), 0);
    EXPECT_DOUBLE_EQ(coverage.percent(1), 100);
    static_cast<CoverageTestMap*>(planet->surf_map)->profile([](double) { return .9; });
    coverage.refresh(planet, 0);
    EXPECT_DOUBLE_EQ(coverage.percent(.25), 0);
    EXPECT_NEAR(coverage.level(50), .9, .00001);
    planet->temperature = 200;
    coverage.refresh(planet, 0);
    EXPECT_DOUBLE_EQ(coverage.percent(1), 100); // Submerged frozen regions still count toward geographic water coverage.
}

TEST_F(ShoreNavigationTest, WaterCoverageBudgetEstimationPreservesPressureAndAllowsManualOverride)
{
    Planet* planet = world([](double, double) { return false; });
    delete planet->surf_map;
    planet->surf_map = new CoverageTestMap();
    planet->m_bump_scale = 1000;
    planet->mass = earth_mass;
    planet->volumetric_mean_radius = earth_radius;
    planet->user_added = true;
    planet->ensure_atmosphere()->surface_pressure = 5e5;
    planet->atm->pressure_supplied = true;
    WaterCoverageEditor editor;
    editor.refresh(planet, 0);
    ASSERT_TRUE(editor.estimate());
    EXPECT_FALSE(planet->has_water_supplied);
    EXPECT_EQ(planet->surface_water_model["terrain"], "equal_area_samples");
    EXPECT_DOUBLE_EQ(planet->get_surface_pressure(), 5e5);
    const float estimated = planet->has_water;
    planet->surf_map->touch_gen();
    editor.refresh(planet, 0);
    EXPECT_FLOAT_EQ(planet->has_water, estimated);
    EXPECT_FALSE(editor.has_target());
    editor.edit(25);
    EXPECT_TRUE(planet->has_water_supplied);
    EXPECT_NEAR(editor.percent(), 25, 1e-10);
}

TEST_F(ShoreNavigationTest, ImportedWaterCoverageUsesEqualAreaAndAcceptsGenerationTarget)
{
    Planet *planet = world([](double lat, double) { return lat > _pi / 3; }, false);
    WaterCoverage coverage;
    coverage.refresh(planet, 0);
    ASSERT_TRUE(coverage.ready());
    EXPECT_FALSE(coverage.editable());
    EXPECT_NEAR(coverage.percent(planet->has_water), 100 * (1 - sin(_pi / 3)) / 2, 1);
    EXPECT_DOUBLE_EQ(coverage.percent(.1), coverage.percent(.9));
    EXPECT_FLOAT_EQ(planet->has_water, 1);
    WaterCoverageEditor editor;
    editor.refresh(planet, 0);
    ASSERT_TRUE(editor.editable());
    editor.edit(90);
    EXPECT_DOUBLE_EQ(editor.percent(), 90);
    EXPECT_TRUE(editor.has_target());
    EXPECT_FLOAT_EQ(planet->has_water, 1); // No arbitrary level is guessed without elevations.
    editor.edit(20);
    EXPECT_DOUBLE_EQ(editor.percent(), 20);
    editor.edit(-10);
    EXPECT_DOUBLE_EQ(editor.percent(), 0);
    editor.edit(120);
    EXPECT_DOUBLE_EQ(editor.percent(), 100);

}

TEST_F(ShoreNavigationTest, WaterCoverageTargetAppliesAfterFirstElevationGeneration)
{
    Planet *planet = world([](double, double) { return false; }, false);
    planet->temperature = 320;
    planet->sidereal_rotational_period = oneday;
    planet->fictitious_map_height = 129;
    planet->preserve_loaded_parameters = true;
    WaterCoverageEditor editor;
    editor.refresh(planet, 0);
    editor.edit(90);
    editor.prepare_generation(planet);
    delete planet->surf_map;
    planet->m_bump_scale = planet->estimate_bump_scale();
    planet->surf_map = new CoverageTestMap(planet->m_bump_scale);
    const bool old_randomize = randomize_txgen;
    randomize_txgen = false;
    editor.refresh(planet, 0);
    EXPECT_NEAR(planet->has_water, .81, .03);
    wait_for_texture_tasks();
    editor.poll(0);
    EXPECT_FALSE(editor.has_target());
    EXPECT_NEAR(editor.percent(), 90, 2);
    editor.edit(20);
    editor.prepare_generation(planet);
    planet->mark_all_maps_for_regen();
    wait_for_texture_tasks();
    editor.poll(0);
    EXPECT_FALSE(editor.has_target());
    EXPECT_NEAR(editor.percent(), 20, 2);
    randomize_txgen = old_randomize;
}

TEST_F(ShoreNavigationTest, InitialImportedWaterCoverageSurvivesElevationResampling)
{
    const bool old_randomize = randomize_txgen;
    randomize_txgen = false;
    for (double target : {17.0, 85.0})
    {
        SCOPED_TRACE(target);
        Planet *planet = world([](double, double) { return false; }, false);
        planet->temperature = 320;
        planet->sidereal_rotational_period = oneday;
        planet->fictitious_map_height = 64;
        planet->preserve_loaded_parameters = true;
        planet->has_real_maps = true;
        auto *surface = new CoverageTestMap(planet->estimate_bump_scale());
        surface->profile([](double lat) { return .35 + .3 * (sin(lat) + 1) * .5; });
        surface->imported();
        delete planet->surf_map;
        planet->surf_map = surface;
        planet->m_bump_scale = 0;
        WaterCoverageEditor editor;
        editor.refresh(planet, 0);
        editor.edit(target);
        editor.prepare_generation(planet);
        planet->mark_all_maps_for_regen();
        for (int pass = 0; pass < 3 && editor.has_target(); ++pass)
        {
            wait_for_texture_tasks();
            editor.poll(0);
        }
        EXPECT_FALSE(editor.has_target());
        EXPECT_NEAR(editor.percent(), target, 1.5);
        EXPECT_EQ(planet->surf_map->get_height(), 64u);
    }
    randomize_txgen = old_randomize;
}

TEST_F(ShoreNavigationTest, ImportedEarthMapsMeetInitialCoverageTargets)
{
    for (double target : {17.0, 85.0})
    {
        SCOPED_TRACE(target);
        Planet *planet = make_planet(make_star(), "Earth");
        planet->temperature = 288;
        planet->sidereal_rotational_period = oneday;
        planet->fictitious_map_height = 768;
        planet->preserve_loaded_parameters = true;
        planet->has_real_maps = true;
        planet->has_water = .71;
        planet->ensure_atmosphere()->surface_pressure = oneatm;
        planet->surf_map = new Map(planet);
        ASSERT_TRUE(planet->surf_map->load_from_jpeg("maps/Earth_surf.jpg"));
        ASSERT_TRUE(planet->surf_map->load_from_png("maps/Earth_bump.png", true, planet->estimate_bump_scale()));
        ASSERT_EQ(planet->surf_map->get_height(), 640u);
        planet->m_bump_scale = 0;
        WaterCoverageEditor editor;
        editor.refresh(planet, 8);
        editor.edit(target);
        editor.prepare_generation(planet);
        const bool old_randomize = randomize_txgen;
        randomize_txgen = false;
        planet->mark_all_maps_for_regen();
        for (int pass = 0; pass < 3 && editor.has_target(); ++pass)
        {
            wait_for_texture_tasks();
            editor.poll(8);
        }
        randomize_txgen = old_randomize;
        EXPECT_FALSE(editor.has_target());
        EXPECT_DOUBLE_EQ(round(editor.percent() * 10) / 10, target);
        EXPECT_EQ(planet->surf_map->get_height(), 768u);
    }
}

TEST_F(ShoreNavigationTest, GeneratedCloudAttributionTracksProvenanceAcrossPngReloads)
{
    Planet *planet = world([](double, double) { return true; });
    planet->home_world = true;
    planet->fictitious_map_height = 32;
    EXPECT_FALSE(planet->has_eumetsat_clouds());
    planet->generate_water_clouds();
    ASSERT_NE(planet->cloud_map, nullptr);
    EXPECT_TRUE(planet->cloud_map->procedurally_generated);
    EXPECT_FALSE(planet->has_eumetsat_clouds());
    const auto path = std::filesystem::temp_directory_path() / "alienorum_cloud_provenance_test.png";
    ASSERT_TRUE(planet->cloud_map->save_to_png(path.string()));
    delete planet->cloud_map;
    planet->cloud_map = new Map(planet);
    ASSERT_TRUE(planet->cloud_map->load_from_png(path.string()));
    EXPECT_TRUE(planet->cloud_map->procedurally_generated);
    EXPECT_FALSE(planet->has_eumetsat_clouds());
    PngExportMap relief(true);
    ASSERT_TRUE(relief.save_to_png(path.string(), true));
    ASSERT_TRUE(planet->cloud_map->load_from_png(path.string(), true));
    EXPECT_TRUE(planet->cloud_map->procedurally_generated);
    PngExportMap imported;
    ASSERT_TRUE(imported.save_to_png(path.string()));
    ASSERT_TRUE(planet->cloud_map->load_from_png(path.string()));
    EXPECT_FALSE(planet->cloud_map->procedurally_generated);
    EXPECT_TRUE(planet->has_eumetsat_clouds());
    planet->home_world = false;
    EXPECT_FALSE(planet->has_eumetsat_clouds());
    std::filesystem::remove(path);
}

TEST_F(ShoreNavigationTest, StellarTexturesIgnoreViewChangesDuringGeneration)
{
    struct RestoreView
    {
        double brightness = global_brightness, gamma = global_inverse_gamma;
        bool redlight = redlight_mode;
        ~RestoreView()
        {
            global_brightness = brightness;
            global_inverse_gamma = gamma;
            redlight_mode = redlight;
        }
    } restore;
    Star *star = make_star("Texture test", .0975 * solar_mass);
    star->temperature = 1364;
    star->volumetric_mean_radius = 23 * solar_radius;
    star->absolute_magnitude = .34;
    star->BV_color = 1.5;
    star->sidereal_rotational_period = 500 * oneday;
    star->rnd_seed = 123456;
    star->fictitious_map_height = 96;
    global_brightness = default_brightness;
    global_inverse_gamma = 1.0 / default_gamma;
    redlight_mode = false;
    Map reference(star);
    reference.generate_stellar_map(star);
    ASSERT_TRUE(reference.is_complete());
    std::vector<unsigned char> expected(reference.get_width() * reference.get_height() * 4);
    reference.export_rgba(expected.data());
    ASSERT_NE(star->night_map, nullptr);
    std::vector<unsigned char> expected_faculae(expected.size());
    star->night_map->export_rgba(expected_faculae.data());
    delete star->night_map;
    star->night_map = nullptr;
    Map actual(star);
    std::atomic<bool> entered{false}, finished{false};
    std::exception_ptr error;
    std::thread generation([&]()
    {
        entered.store(true);
        try { actual.generate_stellar_map(star); }
        catch (...) { error = std::current_exception(); }
        finished.store(true);
    });
    while (!entered.load()) std::this_thread::yield();
    size_t changes = 0;
    while (!finished.load())
    {
        const bool alternate = (++changes % 2) != 0;
        global_brightness = alternate ? .001 : 1000000;
        global_inverse_gamma = alternate ? .25 : 4;
        redlight_mode = alternate;
        std::this_thread::yield();
    }
    generation.join();
    ASSERT_FALSE(bool(error));
    EXPECT_GT(changes, 0u);
    ASSERT_TRUE(actual.is_complete());
    std::vector<unsigned char> pixels(expected.size());
    actual.export_rgba(pixels.data());
    EXPECT_TRUE(pixels == expected);
    ASSERT_NE(star->night_map, nullptr);
    star->night_map->export_rgba(pixels.data());
    EXPECT_TRUE(pixels == expected_faculae);
}

TEST_F(ShoreNavigationTest, PlanetTexturesIgnoreBrightnessGammaAndRedlight)
{
    struct RestoreView
    {
        double brightness = global_brightness, gamma = global_inverse_gamma;
        bool redlight = redlight_mode, randomize = randomize_txgen;
        ~RestoreView()
        {
            global_brightness = brightness;
            global_inverse_gamma = gamma;
            redlight_mode = redlight;
            randomize_txgen = randomize;
        }
    } restore;
    randomize_txgen = false;
    for (bool gaseous : {false, true})
    {
        std::vector<unsigned char> expected;
        for (bool altered_view : {false, true})
        {
            global_brightness = altered_view ? 1000000 : default_brightness;
            global_inverse_gamma = altered_view ? .25 : 1.0 / default_gamma;
            redlight_mode = altered_view;
            Planet *planet = world([](double, double) { return false; });
            planet->type = gaseous ? gas_giant : rocky;
            planet->temperature = 320;
            planet->BV_color = .8;
            planet->albedo = .3;
            planet->rnd_seed = 123456;
            planet->sidereal_rotational_period = oneday;
            planet->fictitious_map_height = 32;
            planet->preserve_loaded_parameters = true;
            Map *texture;
            if (gaseous)
            {
                texture = planet->cloud_map = new Map(planet);
                texture->generate_gas_giant_map(planet);
            }
            else
            {
                delete planet->surf_map;
                texture = planet->surf_map = new Map(planet);
                texture->generate_rocky_map(planet);
            }
            ASSERT_TRUE(texture->is_complete());
            std::vector<unsigned char> pixels(texture->get_width() * texture->get_height() * 4);
            texture->export_rgba(pixels.data());
            if (!altered_view) expected = pixels;
            else EXPECT_TRUE(pixels == expected) << (gaseous ? "gaseous" : "rocky");
            EXPECT_EQ(redlight_mode, altered_view);
        }
    }
}
