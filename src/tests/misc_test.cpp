#include <cmath>
#include <gtest/gtest.h>
#include "../classes/misc.h"
#include "../classes/point.h"
#include "../classes/surfacescatter.h"
#include "../classes/surfacebiomes.h"
#include "../classes/horizonterrain.h"
#include <unordered_map>
#include <unordered_set>
#include <archive.h>
#include <archive_entry.h>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace alienorum;

TEST(ArchiveTest, RawGzipUsesTheCompressedFileName)
{
    struct RestoreDirectory
    {
        std::filesystem::path original, temporary;
        ~RestoreDirectory()
        {
            std::filesystem::current_path(original);
            std::error_code error;
            std::filesystem::remove_all(temporary, error);
        }
    };
    RestoreDirectory restore
    {
        std::filesystem::current_path(),
        std::filesystem::temp_directory_path() / ("alienorum_gzip_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
    };
    std::filesystem::create_directories(restore.temporary);
    std::filesystem::current_path(restore.temporary);
    archive *writer = archive_write_new();
    ASSERT_NE(writer, nullptr);
    ASSERT_EQ(archive_write_add_filter_gzip(writer), ARCHIVE_OK);
    ASSERT_EQ(archive_write_set_format_raw(writer), ARCHIVE_OK);
    ASSERT_EQ(archive_write_open_filename(writer, "catalog.dat.gz"), ARCHIVE_OK);
    archive_entry *entry = archive_entry_new();
    ASSERT_NE(entry, nullptr);
    archive_entry_set_pathname(entry, "data");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    const std::string content = "catalog row\n";
    archive_entry_set_size(entry, content.size());
    ASSERT_EQ(archive_write_header(writer, entry), ARCHIVE_OK);
    EXPECT_EQ(archive_write_data(writer, content.data(), content.size()), content.size());
    archive_entry_free(entry);
    ASSERT_EQ(archive_write_close(writer), ARCHIVE_OK);
    archive_write_free(writer);
    ASSERT_TRUE(extract_archive("catalog.dat.gz"));
    std::ifstream input("catalog.dat");
    std::string line;
    ASSERT_TRUE(std::getline(input, line));
    EXPECT_EQ(line, "catalog row");
    EXPECT_FALSE(std::filesystem::exists("data"));
    input.close();

    const auto stamp = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time("catalog.dat.gz", stamp);
    std::filesystem::last_write_time("catalog.dat", stamp - std::chrono::seconds(60));
    std::ofstream("catalog.dat") << "stale row\n";
    std::filesystem::last_write_time("catalog.dat", stamp - std::chrono::seconds(60));
    ASSERT_TRUE(refresh_gzip_file("catalog.dat"));
    const auto read = []()
    {
        std::ifstream stream("catalog.dat");
        std::string row;
        std::getline(stream, row);
        return row;
    };
    EXPECT_EQ(read(), "catalog row");
    EXPECT_EQ(std::filesystem::last_write_time("catalog.dat"), stamp);

    std::ofstream("catalog.dat") << "local row\n";
    std::filesystem::last_write_time("catalog.dat", stamp + std::chrono::seconds(60));
    ASSERT_TRUE(refresh_gzip_file("catalog.dat"));
    EXPECT_EQ(read(), "local row");
    std::filesystem::last_write_time("catalog.dat", stamp);
    ASSERT_TRUE(refresh_gzip_file("catalog.dat"));
    EXPECT_EQ(read(), "local row");

    std::filesystem::remove("catalog.dat");
    ASSERT_TRUE(refresh_gzip_file("catalog.dat"));
    EXPECT_EQ(read(), "catalog row");
    std::ifstream packed("catalog.dat.gz", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(packed)), std::istreambuf_iterator<char>());
    packed.close();
    ASSERT_GT(bytes.size(), 8u);
    std::ofstream("catalog.dat.gz", std::ios::binary | std::ios::trunc).write(bytes.data(), bytes.size() - 8);
    std::filesystem::last_write_time("catalog.dat.gz", stamp + std::chrono::seconds(120));
    EXPECT_FALSE(refresh_gzip_file("catalog.dat"));
    EXPECT_EQ(read(), "catalog row");
    EXPECT_FALSE(std::filesystem::exists("catalog.dat.refresh.tmp"));
    std::ofstream("catalog.dat.gz", std::ios::binary | std::ios::trunc) << "invalid gzip";
    std::filesystem::last_write_time("catalog.dat.gz", stamp + std::chrono::seconds(120));
    EXPECT_FALSE(refresh_gzip_file("catalog.dat"));
    EXPECT_EQ(read(), "catalog row");
    EXPECT_FALSE(std::filesystem::exists("catalog.dat.refresh.tmp"));
}

TEST(HorizonTerrainTest, PixelDepthVariesAcrossASlopeAndOccludesFartherFaces)
{
    HorizonTerrainRaster raster;
    raster.reset(8, 8);
    ProjectedTerrainVertex a, b, c;
    a.x = 0;
    a.y = 0;
    a.inverse_depth = 0.01;
    a.distance_over_depth = 10;
    b = a;
    b.x = 8;
    c = a;
    c.y = 8;
    c.inverse_depth = 0.005;
    raster.triangle(a, b, c);
    const double upper = raster.distance_at(0.5, 0.5);
    const double lower = raster.distance_at(0.5, 6.5);
    EXPECT_NEAR(upper, 10.0 / (0.01 * 15.0 / 16.0 + 0.005 / 16.0), 0.001);
    EXPECT_GT(lower, upper);
    EXPECT_EQ(raster.top_at(0), 0);
    EXPECT_FALSE(std::isfinite(raster.distance_at(7.5, 7.5)));
    a.inverse_depth *= 0.5;
    b.inverse_depth *= 0.5;
    c.inverse_depth *= 0.5;
    raster.triangle(a, b, c);
    EXPECT_EQ(raster.distance_at(0.5, 0.5), upper);
    a.inverse_depth *= 4;
    b.inverse_depth *= 4;
    c.inverse_depth *= 4;
    raster.triangle(a, b, c);
    EXPECT_NEAR(raster.distance_at(0.5, 0.5), upper * 0.5, 0.001);
}

TEST(SurfaceBiomesTest, WindingBordersAreRepeatableAndVaryAlongTheCoast)
{
    SurfaceBiomes field(42);
    const double radius = 6371000.0;
    double minimum = 1000, maximum = -1000;
    double prior = 0;
    for (int north = -200; north <= 200; north += 5)
    {
        double west = -100, east = 100;
        for (int step = 0; step < 30; ++step)
        {
            const double middle = (west + east) * 0.5;
            const auto coord = field.coordinates(radius, north / radius, middle / radius);
            if (coord.second < 0)
            {
                west = middle;
            }
            else
            {
                east = middle;
            }
        }
        const double crossing = (west + east) * 0.5;
        minimum = std::min(minimum, crossing);
        maximum = std::max(maximum, crossing);
        if (north > -200)
        {
            EXPECT_LT(fabs(crossing - prior), 5.0);
        }
        prior = crossing;
        const auto first = field.coordinates(radius, north / radius, crossing / radius);
        const auto again = field.coordinates(radius, north / radius, crossing / radius);
        EXPECT_EQ(first.first, again.first);
        EXPECT_EQ(first.second, again.second);
    }
    EXPECT_GT(maximum - minimum, 5.0);
}

TEST(SurfaceBiomesTest, DisplacementHasNoDateLineOrPolarSeam)
{
    SurfaceBiomes field(42);
    const double radius = 6371000.0;
    const auto west = field.coordinates(radius, 0.4, -_pi);
    const auto east = field.coordinates(radius, 0.4, _pi);
    EXPECT_NEAR(west.first, east.first, 1e-12);
    EXPECT_NEAR(remainder(west.second - east.second, 2.0 * _pi), 0, 1e-12);
    const auto pole_a = field.coordinates(radius, half_pi, 0);
    const auto pole_b = field.coordinates(radius, half_pi, 2.0);
    EXPECT_NEAR(pole_a.first, pole_b.first, 1e-12);
    EXPECT_NEAR(pole_a.second, pole_b.second, 1e-9);
    const auto before = field.coordinates(radius, 0.4, 0.5);
    field.set_world(43);
    const auto other = field.coordinates(radius, 0.4, 0.5);
    EXPECT_GT(hypot(before.first - other.first, before.second - other.second), 1e-8);
    field.set_world(42);
    const auto restored = field.coordinates(radius, 0.4, 0.5);
    EXPECT_EQ(before, restored);
}

TEST(SurfaceBiomesTest, StrengthScalesGeographicDisplacement)
{
    SurfaceBiomes field(42);
    const double radius = 6371000.0;
    const double latitude = 0.4, longitude = 0.5;
    const auto off = field.coordinates(radius, latitude, longitude, 0.0);
    EXPECT_EQ(off, std::make_pair(latitude, longitude));
    const auto normal = field.coordinates(radius, latitude, longitude);
    EXPECT_EQ(normal, field.coordinates(radius, latitude, longitude, 1.0));
    const auto triple = field.coordinates(radius, latitude, longitude, 3.0);
    const double normal_north = radius * (normal.first - latitude);
    const double normal_east = radius * cos(latitude) * (normal.second - longitude);
    EXPECT_GT(hypot(normal_north, normal_east), 1.0);
    EXPECT_NEAR(radius * (triple.first - latitude), normal_north * 3.0, 0.001);
    EXPECT_NEAR(radius * cos(latitude) * (triple.second - longitude), normal_east * 3.0, 0.001);
}

TEST(SurfaceBiomesTest, CoastalReliefRisesGentlyFromSeaAcrossTheCircularSeam)
{
    std::vector<double> relief(720, 1000.0);
    std::vector<bool> water(720, false);
    water[0] = true;
    const double spacing = 2.0;
    const double incline = 4.0 * fiftyseventh;
    taper_coastal_relief(relief, water, spacing, incline);
    EXPECT_EQ(relief[0], 0.0);
    for (int i = 1; i < 360; ++i)
    {
        const double gradient = (relief[i] - relief[i - 1]) / spacing;
        EXPECT_GE(gradient, tan(2.0 * fiftyseventh));
        EXPECT_LE(gradient, tan(6.0 * fiftyseventh));
        EXPECT_NEAR(relief[i], relief[720 - i], 1e-12);
    }
    std::fill(water.begin(), water.end(), false);
    const auto inland = relief;
    taper_coastal_relief(relief, water, spacing, incline);
    EXPECT_EQ(relief, inland);
    std::fill(water.begin(), water.end(), true);
    taper_coastal_relief(relief, water, spacing, incline);
    for (double height : relief)
    {
        EXPECT_EQ(height, 0.0);
    }
}

TEST(SurfaceSceneryTest, WalkingPreservesObjectsAcrossCellAndFaceBoundaries)
{
    const double radius = 6371000.0;
    const double spacing = sqrt(_pi * 10000.0 / 512.0);
    const double longitudes[] =
    {
        0.0, _pi / 4.0, _pi, -_pi / 4.0
    };
    for (double longitude : longitudes)
    {
        const auto before = surface_scenery(radius, 0.0, longitude - 0.5 / radius, 100.0, spacing, 42);
        const auto after = surface_scenery(radius, 0.0, longitude + 0.5 / radius, 100.0, spacing, 42);
        std::unordered_map<std::uint64_t, SurfaceScenery> prior;
        for (const auto &object : before)
        {
            prior.emplace(object.id, object);
        }
        int shared = 0;
        for (const auto &object : after)
        {
            const auto found = prior.find(object.id);
            if (found == prior.end())
            {
                continue;
            }
            ++shared;
            EXPECT_EQ(object.seed, found->second.seed);
            EXPECT_DOUBLE_EQ(object.latitude, found->second.latitude);
            EXPECT_DOUBLE_EQ(object.longitude, found->second.longitude);
            EXPECT_NEAR(object.east, found->second.east - 1.0, 1.0e-6);
            EXPECT_NEAR(object.north, found->second.north, 1.0e-8);
        }
        EXPECT_GT(shared, 300);
        EXPECT_GT(shared, static_cast<int>(before.size() * 0.95));
    }
}

TEST(SurfaceSceneryTest, LatitudeMovementAndPolesStayContinuous)
{
    const double radius = 6371000.0;
    const double spacing = sqrt(_pi * 10000.0 / 512.0);
    const double latitudes[] =
    {
        -half_pi, -0.615479708670387, 0.0, 0.615479708670387, half_pi
    };
    for (double latitude : latitudes)
    {
        const auto before = surface_scenery(radius, latitude - 0.5 / radius, _pi / 4.0, 100.0, spacing, 42);
        const auto after = surface_scenery(radius, latitude + 0.5 / radius, _pi / 4.0, 100.0, spacing, 42);
        std::unordered_map<std::uint64_t, SurfaceScenery> prior;
        for (const auto &object : before)
        {
            prior.emplace(object.id, object);
        }
        int shared = 0;
        for (const auto &object : after)
        {
            const auto found = prior.find(object.id);
            if (found != prior.end())
            {
                ++shared;
                EXPECT_EQ(object.seed, found->second.seed);
            EXPECT_DOUBLE_EQ(object.latitude, found->second.latitude);
            EXPECT_DOUBLE_EQ(object.longitude, found->second.longitude);
                EXPECT_NEAR(object.east, found->second.east, 1.0e-6);
                EXPECT_NEAR(object.north, found->second.north - 1.0, 1.0e-6);
            }
        }
        EXPECT_GT(shared, 300);
        EXPECT_GT(shared, static_cast<int>(before.size() * 0.95));
    }
}

TEST(SurfaceSceneryTest, RepeatableUniqueAndPeriodic)
{
    const double radius = 6371000.0;
    const auto first = surface_scenery(radius, 0.5, -_pi, 100.0, 8.0, 42);
    const auto again = surface_scenery(radius, 0.5, _pi, 100.0, 8.0, 42);
    const auto other_world = surface_scenery(radius, 0.5, _pi, 100.0, 8.0, 43);
    ASSERT_EQ(first.size(), again.size());
    std::unordered_set<std::uint64_t> identities;
    for (size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(first[i].id, again[i].id);
        EXPECT_EQ(first[i].seed, again[i].seed);
        EXPECT_NEAR(first[i].east, again[i].east, 1.0e-6);
        EXPECT_NEAR(first[i].north, again[i].north, 1.0e-6);
        EXPECT_TRUE(identities.insert(first[i].id).second);
        EXPECT_LE(hypot(first[i].east, first[i].north), 100.0);
    }
    for (const auto &object : other_world)
    {
        EXPECT_EQ(identities.count(object.id), 0u);
    }
}

TEST(SurfaceSceneryTest, VariableCrownsKeepClearanceAndStablePlacementDuringTravel)
{
    constexpr double radius = 6371000.0;
    constexpr double range = 100.0;
    constexpr double crown_scale = 10.0;
    constexpr double maximum_size = 1.85;
    const double margin = 2 * maximum_size * crown_scale + 0.1;
    for (double longitude : {0.0, _pi / 4, _pi})
    {
        std::unordered_map<std::uint64_t, bool> previous;
        int shared = 0;
        for (int step = 0; step < 2; step++)
        {
            const auto objects = surface_scenery(radius, 0, longitude + step / radius,
                range + margin, 8, 42);
            std::vector<double> radii;
            for (const auto &object : objects)
            {
                radii.push_back(crown_scale * scenery_size(object.seed, 0.15, maximum_size));
            }
            const auto retained = scenery_clearance(objects, radii, radius);
            int count = 0;
            for (size_t i = 0; i < objects.size(); i++)
            {
                if (hypot(objects[i].east, objects[i].north) >= range - 1) continue;
                if (step == 0) previous.emplace(objects[i].id, retained[i]);
                else if (previous.count(objects[i].id))
                {
                    EXPECT_EQ(retained[i], previous.at(objects[i].id));
                    shared++;
                }
                if (!retained[i]) continue;
                count++;
                for (size_t j = i + 1; j < objects.size(); j++)
                {
                    if (!retained[j]) continue;
                    const double lat = sin((objects[i].latitude - objects[j].latitude) * 0.5);
                    const double lon = sin((objects[i].longitude - objects[j].longitude) * 0.5);
                    const double chord = 2 * radius * sqrt(lat * lat
                        + cos(objects[i].latitude) * cos(objects[j].latitude) * lon * lon);
                    EXPECT_GE(chord + 1e-8, radii[i] + radii[j] + 0.1);
                }
            }
            EXPECT_GT(count, 5);
            EXPECT_LT(count, static_cast<int>(objects.size()));
        }
        EXPECT_GT(shared, 100);
    }
}

TEST(SurfaceSceneryTest, KilometerPlantFieldRetainsDistantCandidatesAndLocalClearance)
{
    constexpr double radius = 6371000;
    const auto objects = surface_scenery(radius, 0, 0, 1011, 11, 42);
    std::vector<double> radii;
    for (const auto &object : objects) radii.push_back(2.85 * scenery_size(object.seed, 0.06, 1.85));
    const auto retained = scenery_clearance(objects, radii, radius);
    std::vector<size_t> sorted;
    int distant = 0;
    for (size_t i = 0; i < objects.size(); i++)
    {
        if (!retained[i]) continue;
        sorted.push_back(i);
        const double distance = hypot(objects[i].east, objects[i].north);
        distant += distance >= 750 && distance < 1000;
    }
    EXPECT_GT(distant, 1000);
    std::sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) { return objects[a].east < objects[b].east; });
    for (size_t a = 0; a < sorted.size(); a++)
    {
        const size_t i = sorted[a];
        for (size_t b = a + 1; b < sorted.size(); b++)
        {
            const size_t j = sorted[b];
            if (objects[j].east - objects[i].east >= 2 * 2.85 * 1.85 + 0.1) break;
            EXPECT_GE(hypot(objects[i].east - objects[j].east, objects[i].north - objects[j].north) + 1e-5,
                radii[i] + radii[j] + 0.1);
        }
    }
}

// Nothing in this file touches the network. download_file() is the one function in misc.h that
// does, and it is deliberately left alone: CelesTrak's terms ask for one fetch per file per two
// hours, and a test suite is not a reason to spend one.

// =====================================================================
// Kepler's equation, in its three conic forms
// =====================================================================

// The solvers are checked by substitution rather than against a table: put the anomaly they
// return back into the equation they claim to have solved and see whether the mean anomaly comes
// out again. That is the whole contract, it holds for every eccentricity, and it cannot be
// satisfied by a solver that merely looks plausible.

TEST(KeplerTest, EllipticSolutionSatisfiesTheEquation)
{
    double e, M;
    for (e = 0.0; e < 0.999; e += 0.05)
    {
        for (M = -6.0; M <= 6.0; M += 0.25)
        {
            double E = solve_Kepler(M, e);
            EXPECT_NEAR(E - e * std::sin(E), M, 1e-8)
                << " for e = " << e << ", M = " << M;
        }
    }
}

TEST(KeplerTest, EllipticEdgeCases)
{
    // A circle: the eccentric anomaly is the mean anomaly.
    EXPECT_NEAR(solve_Kepler(0.0, 0.0), 0.0, 1e-12);
    EXPECT_NEAR(solve_Kepler(1.0, 0.0), 1.0, 1e-12);
    EXPECT_NEAR(solve_Kepler(_pi, 0.0), _pi, 1e-12);

    // Periapsis and apoapsis are fixed points at any eccentricity.
    double e;
    for (e = 0.0; e < 0.99; e += 0.11)
    {
        EXPECT_NEAR(solve_Kepler(0.0, e), 0.0, 1e-9) << " at e = " << e;
        EXPECT_NEAR(solve_Kepler(_pi, e), _pi, 1e-7) << " at e = " << e;
    }

    // Nearly parabolic, where the iteration is at its worst behaved.
    double E = solve_Kepler(0.01, 0.99);
    EXPECT_NEAR(E - 0.99 * std::sin(E), 0.01, 1e-8);
    EXPECT_TRUE(std::isfinite(E));
}

TEST(KeplerTest, HyperbolicSolutionSatisfiesTheEquation)
{
    double e, M;
    for (e = 1.05; e < 4.0; e += 0.35)
    {
        for (M = -5.0; M <= 5.0; M += 0.5)
        {
            double H = solve_Kepler_hyperbolic(M, e);
            EXPECT_NEAR(e * std::sinh(H) - H, M, 1e-6)
                << " for e = " << e << ", M = " << M;
        }
    }
}

TEST(KeplerTest, HyperbolicIsAntisymmetricAboutPerihelion)
{
    // Inbound and outbound are mirror images: the same time either side of perihelion puts the
    // body the same angle either side of it.
    EXPECT_NEAR(solve_Kepler_hyperbolic(0.0, 1.5), 0.0, 1e-9);
    EXPECT_NEAR(solve_Kepler_hyperbolic(2.0, 1.5), -solve_Kepler_hyperbolic(-2.0, 1.5), 1e-9);
}

TEST(KeplerTest, BarkerSolvesTheParabolicCase)
{
    // Barker's equation, Mp = D + D^3/3, solved for D = tan(nu/2).
    double Mp;
    for (Mp = -8.0; Mp <= 8.0; Mp += 0.5)
    {
        double D = solve_Barker(Mp);
        EXPECT_NEAR(D + D*D*D/3.0, Mp, 1e-8) << " for Mp = " << Mp;
    }

    EXPECT_NEAR(solve_Barker(0.0), 0.0, 1e-12);
    EXPECT_GT(solve_Barker(1.0), 0.0);
    EXPECT_LT(solve_Barker(-1.0), 0.0);
}

// =====================================================================
// String handling
// =====================================================================

TEST(TrimTest, RemovesWhitespaceFromEitherEnd)
{
    EXPECT_EQ(trim("  Vega  "), "Vega");
    EXPECT_EQ(ltrim("  Vega  "), "Vega  ");
    EXPECT_EQ(rtrim("  Vega  "), "  Vega");

    EXPECT_EQ(trim("Vega"), "Vega");
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("   "), "");
    EXPECT_EQ(trim("\t\r\n Vega \t\r\n"), "Vega");

    // Interior spacing is not its business.
    EXPECT_EQ(trim("  Alpha Centauri B  "), "Alpha Centauri B");
}

TEST(LopComponentTest, RemovesATrailingComponentLetter)
{
    // The component letter is how a multiple system's members are told apart, and has to come off
    // before the system's own name can be used to build a companion's.
    EXPECT_EQ(lop_component("Alpha Centauri A"), "Alpha Centauri");
    EXPECT_EQ(lop_component("Alpha Centauri B"), "Alpha Centauri");

    // A name that merely ends in a letter is not a component.
    EXPECT_EQ(lop_component("Vega"), "Vega");
    EXPECT_EQ(lop_component("HD12345"), "HD12345");
}

TEST(DamerauLevenshteinTest, CountsTheEditsBetweenTwoStrings)
{
    EXPECT_EQ(Damerau_Levenshtein("Vega", "Vega"), 0);
    EXPECT_EQ(Damerau_Levenshtein("", ""), 0);

    // One of each of the four operations it is named for.
    EXPECT_EQ(Damerau_Levenshtein("Vega", "Vego"), 1);          // substitution
    EXPECT_EQ(Damerau_Levenshtein("Vega", "Vegas"), 1);         // insertion
    EXPECT_EQ(Damerau_Levenshtein("Vegas", "Vega"), 1);         // deletion
    EXPECT_EQ(Damerau_Levenshtein("Vega", "Vgea"), 1);          // transposition -- the Damerau part

    // Distance from nothing is the length of the other one.
    EXPECT_EQ(Damerau_Levenshtein("Vega", ""), 4);
    EXPECT_EQ(Damerau_Levenshtein("", "Vega"), 4);

    // Symmetric, as a distance must be.
    EXPECT_EQ(Damerau_Levenshtein("Betelgeuse", "Betelgeuze"),
              Damerau_Levenshtein("Betelgeuze", "Betelgeuse"));
}

TEST(NumberMatchingTest, DigitsAndDots)
{
    EXPECT_TRUE(is_digit_or_dot('0'));
    EXPECT_TRUE(is_digit_or_dot('9'));
    EXPECT_TRUE(is_digit_or_dot('.'));
    EXPECT_FALSE(is_digit_or_dot('A'));
    EXPECT_FALSE(is_digit_or_dot(' '));

    EXPECT_TRUE(contains_digits_or_dots("HD12345"));
    EXPECT_TRUE(contains_digits_or_dots("Gl 411"));
    EXPECT_FALSE(contains_digits_or_dots("Vega"));
    EXPECT_FALSE(contains_digits_or_dots(""));
}

TEST(NumberMatchingTest, HasSameNumbers)
{
    // What keeps a search for "Gl 411" off "Gl 412": the letters may be fuzzy-matched, the
    // numbers may not.
    EXPECT_TRUE(has_same_numbers("Gl 411", "Gl 411"));
    EXPECT_TRUE(has_same_numbers("Gliese 411", "Gl 411"));
    EXPECT_FALSE(has_same_numbers("Gl 411", "Gl 412"));
    EXPECT_TRUE(has_same_numbers("Vega", "Vega"));              // no numbers in either
}

TEST(RomanTest, ConvertsToRomanNumerals)
{
    EXPECT_EQ(Roman(1), "I");
    EXPECT_EQ(Roman(4), "IV");
    EXPECT_EQ(Roman(9), "IX");
    EXPECT_EQ(Roman(14), "XIV");
    EXPECT_EQ(Roman(40), "XL");
    EXPECT_EQ(Roman(90), "XC");
    EXPECT_EQ(Roman(400), "CD");
    EXPECT_EQ(Roman(1987), "MCMLXXXVII");
    EXPECT_EQ(Roman(2024), "MMXXIV");
}

TEST(GreekTest, LettersAndAbbreviations)
{
    // The Bayer designations are stored as an index into Greek_letter[], and the abbreviation is
    // what the catalogs write. Note the char* parameter: misc.h also declares a std::string
    // overload, but nothing defines one, so calling it is a link error rather than a conversion.
    char alp[] = "Alp", bet[] = "Bet", nonsense[] = "Zzz";
    EXPECT_EQ(grkno_from_abbrev("Alp"), 0);
    EXPECT_EQ(Greek_from_abbrev(alp), Greek_letter[0]);
    EXPECT_EQ(Greek_from_abbrev(bet), Greek_letter[1]);

    // Something that is not a Greek letter at all.
    EXPECT_EQ(Greek_from_abbrev(nonsense), std::string(""));
}

TEST(GreekTest, BlankBayerFieldsHaveNoGreekLetter)
{
    EXPECT_EQ(grkno_from_abbrev(nullptr), -1);
    EXPECT_EQ(grkno_from_abbrev(""), -1);
    EXPECT_EQ(grkno_from_abbrev("    "), -1);
    EXPECT_EQ(grkno_from_abbrev("7   "), -1);
    EXPECT_EQ(grkno_from_abbrev("-   "), -1);
    EXPECT_EQ(grkno_from_abbrev("\t"), -1);
}

TEST(GreekTest, BayerComponentsAndShortNamesAreSafe)
{
    EXPECT_EQ(grkno_from_abbrev("Alp "), 0);
    EXPECT_EQ(grkno_from_abbrev("Gam1"), 121);
    EXPECT_EQ(grkno_from_abbrev("Gam2Vel"), 122);
    EXPECT_EQ(grkno_from_abbrev("Mu"), 11);
    EXPECT_EQ(grkno_from_abbrev("Pi"), 15);
    EXPECT_EQ(grkno_from_abbrev("Xi"), 13);
    EXPECT_EQ(grkno_from_abbrev("Mu1 Cha"), 211);
    EXPECT_EQ(grkno_from_abbrev("Mu 1Cha"), 211);
    EXPECT_EQ(grkno_from_abbrev("Mu2 Cha"), 212);
    EXPECT_EQ(grkno_from_abbrev("Pi4 Ori"), 254);
    EXPECT_EQ(grkno_from_abbrev("Pi 4Ori"), 254);
    EXPECT_EQ(grkno_from_abbrev("Pi5 Ori"), 255);
    EXPECT_EQ(grkno_from_abbrev("Pi 5Ori"), 255);
    EXPECT_EQ(grkno_from_abbrev("Alp Oph"), 0);
}

TEST(ConsFromAlienorumIdTest, ExtractsTheConstellation)
{
    // An alienorumid is a sequence number followed by the IAU constellation abbreviation.
    EXPECT_EQ(cons_from_alienorumid("1 Ori"), "Ori");
    EXPECT_EQ(cons_from_alienorumid("437 Ser"), "Ser");
    EXPECT_EQ(cons_from_alienorumid("12 Ori B"), "Ori");        // a component letter may follow
}

// =====================================================================
// Numbers
// =====================================================================

TEST(SgnTest, SignOfADouble)
{
    EXPECT_EQ(sgn(3.7), 1);
    EXPECT_EQ(sgn(-3.7), -1);
    EXPECT_EQ(sgn(0.0), 0);
}

TEST(SigmoidTest, BoundedAndMonotonic)
{
    EXPECT_NEAR(sigmoid(0.0), 0.5, 1e-12);
    EXPECT_GT(sigmoid(1.0), sigmoid(0.0));
    EXPECT_LT(sigmoid(-1.0), sigmoid(0.0));

    // Saturates rather than running away.
    EXPECT_GT(sigmoid(50.0), 0.99);
    EXPECT_LT(sigmoid(-50.0), 0.01);
    EXPECT_LE(sigmoid(1e6), 1.0);
    EXPECT_GE(sigmoid(-1e6), 0.0);
}

TEST(ProbabilityDensityTest, NormalDistribution)
{
    // Peaks at the mean, symmetric about it, and falls off either side.
    double peak = probability_density_function(0.0, 0.0, 1.0);
    EXPECT_NEAR(peak, 1.0 / std::sqrt(2 * _pi), 1e-9);
    EXPECT_NEAR(probability_density_function(1.0, 0.0, 1.0),
                probability_density_function(-1.0, 0.0, 1.0), 1e-12);
    EXPECT_LT(probability_density_function(2.0, 0.0, 1.0), peak);
}

TEST(TimeDilationTest, RatioIsAtMostOne)
{
    EXPECT_DOUBLE_EQ(compute_time_dilation(0), 1.0);
    EXPECT_LT(compute_time_dilation(0.9 * speed_of_light), 1.0);
    EXPECT_NEAR(compute_time_dilation(0.6 * speed_of_light), 0.8, 1e-12);   // the 3-4-5 triangle
    EXPECT_NEAR(compute_time_dilation(speed_of_light), 0.0, 1e-12);
}

TEST(BlackbodyTest, ObeysWiensLaw)
{
    // The wavelength of peak emission is inversely proportional to temperature: Wien's constant
    // is 2.898e-3 m*K, so the Sun peaks around 500 nm and a red dwarf well into the infrared.
    auto peak_wavelength = [](double T) -> double
    {
        double best = 0, best_flux = -1;
        for (double nu = 1e-7; nu < 3e-6; nu += 1e-9)
        {
            double f = blackbody_flux(T, nu);
            if (f > best_flux) { best_flux = f; best = nu; }
        }
        return best;
    };

    EXPECT_NEAR(peak_wavelength(5778), 2.898e-3 / 5778, 5e-9);
    EXPECT_NEAR(peak_wavelength(3000), 2.898e-3 / 3000, 5e-9);
    EXPECT_LT(peak_wavelength(9000), peak_wavelength(3000));

    // Hotter is brighter at every wavelength.
    EXPECT_GT(blackbody_flux(9000, V_band), blackbody_flux(5778, V_band));
    EXPECT_GT(blackbody_flux(5778, V_band), blackbody_flux(3000, V_band));
}

TEST(AtmosphericTauTest, ThickerAndGreenerMeansMoreOpaque)
{
    // Optical depth in the thermal infrared: nothing to absorb with means nothing absorbed, and
    // adding greenhouse gas or pressure can only raise it.
    double bare = atmospheric_tau(1.0, 0, 0, 0);
    double with_co2 = atmospheric_tau(1.0, 0.01, 0, 0);
    double with_more_co2 = atmospheric_tau(1.0, 0.1, 0, 0);

    EXPECT_GE(bare, 0.0);
    EXPECT_GT(with_co2, bare);
    EXPECT_GT(with_more_co2, with_co2);

    // Same mixture, more of it.
    EXPECT_GT(atmospheric_tau(10.0, 0.01, 0, 0), with_co2);

    // An airless world.
    EXPECT_NEAR(atmospheric_tau(0.0, 0.01, 0, 0), 0.0, 1e-9);
}

// Venus, straight out of planets.json: 9.3e6 Pa (91.7817 atm) at 96.5% CO2, with the trace SO2,
// H2O and CO that record also carries. The band terms are logarithmic and saturate long before
// this -- they alone reach tau 4.99, barely more than a single bar of pure CO2 would give -- so
// the CO2-CO2 collision-induced term is what has to carry a dense CO2 world. It is calibrated to
// land on the tau of 61.4 that planets.json records for Venus.
TEST(AtmosphericTauTest, DenseCO2ReachesTheMeasuredVenusOpacity)
{
    const double venus_atm = 91.7817;
    double venus = atmospheric_tau(venus_atm, 0.965, 0.0, 2e-5, 0.0, 0.0, 1.5e-4, 0.0, 1.7e-5);

    EXPECT_NEAR(venus, 61.4, 0.1) << "Venus must reach the optical depth planets.json records";

    // The shape of the fix, stated without reference to the formula: piling on ninety times more
    // CO2 has to buy far more than ninety-log-of-anything. A purely logarithmic model gives 92 bar
    // less than twice the opacity of 1 bar, which is what left the surface hundreds of K too cold.
    double one_bar_co2 = atmospheric_tau(1.0, 1.0, 0.0, 0.0);
    EXPECT_GT(venus, 10 * one_bar_co2)
        << "a saturating band model cannot reach Venus; the collisional term is what gets there";
}

// The point of making the term quadratic rather than logarithmic is that it vanishes on thin
// atmospheres: Earth and Mars must not feel it at all, or fixing Venus would break both.
TEST(AtmosphericTauTest, ThinCO2AtmospheresAreUntouched)
{
    // Earth: 1 atm, 420 ppm CO2 (plus water vapor and methane, which the band terms do handle).
    double earth = atmospheric_tau(1.0, 0.00042, 1.9e-6, 0.0025);
    EXPECT_NEAR(earth, 0.11593685903340019, 1e-6)
        << "the CO2 CIA term must not perturb Earth's opacity";

    // Mars: 0.00636 atm, but 95.3% CO2 -- a high fraction of almost nothing.
    double mars = atmospheric_tau(0.00636, 0.9532, 0.0, 0.0);
    EXPECT_NEAR(mars, 0.23694652780558079, 1e-6)
        << "a high CO2 fraction at negligible pressure must stay negligible";

    // Both sit far below the pressure where collisional absorption starts to matter at all.
    EXPECT_LT(earth, 1.0);
    EXPECT_LT(mars, 1.0);
}

// Collisional absorption scales with the square of the density, so the optical depth it
// contributes has to scale with the square of the partial pressure.
TEST(AtmosphericTauTest, CollisionInducedAbsorptionIsQuadraticInPressure)
{
    // Pure CO2, far above the pressure where the quadratic term swamps the logarithmic ones, so
    // these ratios are the quadratic part on its own.
    double at_500  = atmospheric_tau(500.0,  1.0, 0.0, 0.0);
    double at_1000 = atmospheric_tau(1000.0, 1.0, 0.0, 0.0);
    double at_2000 = atmospheric_tau(2000.0, 1.0, 0.0, 0.0);

    EXPECT_NEAR(at_1000 / at_500,  4.0, 0.05) << "doubling the pressure must quadruple tau";
    EXPECT_NEAR(at_2000 / at_1000, 4.0, 0.05);

    // Nearer Venus's own pressure the band terms still contribute, so the growth is short of
    // quadratic -- but it must stay well clear of the linear-or-slower behaviour of a pure band
    // model, which is the regime the old formula was stuck in.
    double at_50  = atmospheric_tau(50.0,  1.0, 0.0, 0.0);
    double at_100 = atmospheric_tau(100.0, 1.0, 0.0, 0.0);
    EXPECT_GT(at_100 / at_50, 3.0);

    // And it stays monotonic, with no discontinuity across the 0.5 atm threshold that gates the
    // hydrogen CIA term next to it.
    EXPECT_LT(atmospheric_tau(0.49, 1.0, 0.0, 0.0), atmospheric_tau(0.51, 1.0, 0.0, 0.0));
    EXPECT_NEAR(atmospheric_tau(0.499, 1.0, 0.0, 0.0), atmospheric_tau(0.501, 1.0, 0.0, 0.0), 0.01);
}

TEST(SphereVolumeTest, MatchesTheFormula)
{
    EXPECT_NEAR(sphere_volume(1.0), 4.0/3.0 * _pi, 1e-12);
    EXPECT_NEAR(sphere_volume(2.0), 8 * sphere_volume(1.0), 1e-9);      // cubes with the radius
    EXPECT_DOUBLE_EQ(sphere_volume(0.0), 0.0);
}

// =====================================================================
// Time
// =====================================================================

TEST(IsoStringTest, ParsesTheCatalogEpochFormat)
{
    // The satellite catalogs state their epoch like this, and the value is fed straight into the
    // orbit, so an hour lost here is an orbit drawn in the wrong place. Parsing is in UTC, never
    // the local zone: run the suite in Montreal or in Tokyo and the same string is the same
    // instant. J2000_TIME_T is this program's epoch, and note that it is midnight -- it pairs
    // with #define J2000 2451544.5, the Julian date of the same moment, and not with the
    // astronomers' J2000.0, which is the noon twelve hours later.
    std::time_t t = from_iso_string("2000-01-01T00:00:00", "%Y-%m-%dT%H:%M:%S");
    EXPECT_EQ(t, J2000_TIME_T);
    EXPECT_EQ(from_iso_string("2000-01-01T12:00:00", "%Y-%m-%dT%H:%M:%S"), J2000_TIME_T + 43200);

    // An hour later is an hour later.
    std::time_t t2 = from_iso_string("2000-01-01T01:00:00", "%Y-%m-%dT%H:%M:%S");
    EXPECT_EQ(t2 - t, 3600);

    // A day later is a day later, across a month boundary and a leap year at that.
    std::time_t t3 = from_iso_string("2000-02-29T12:00:00", "%Y-%m-%dT%H:%M:%S");
    std::time_t t4 = from_iso_string("2000-03-01T12:00:00", "%Y-%m-%dT%H:%M:%S");
    EXPECT_EQ(t4 - t3, 86400);

    // The fractional seconds the satellite catalogs carry are not in the format string, and are
    // simply not read: "2026-07-15T12:22:29.846208" is accepted as of that second.
    EXPECT_EQ(from_iso_string("2026-07-15T12:22:29.846208", "%Y-%m-%dT%H:%M:%S"),
              from_iso_string("2026-07-15T12:22:29", "%Y-%m-%dT%H:%M:%S"));
}

TEST(ElapsedTimeTest, FormatsADuration)
{
    EXPECT_EQ(elapsed_time(0, 0), "00:00");
    EXPECT_EQ(elapsed_time(0, 61), "01:01");
    EXPECT_EQ(elapsed_time(100, 100 + 125), "02:05");
}

// =====================================================================
// Files -- read only, and only files that are already there
// =====================================================================

TEST(FileTest, ExistenceAndAge)
{
    // The makefile is in the working directory the tests are run from, and is not going anywhere.
    EXPECT_TRUE(file_exists("makefile"));
    EXPECT_FALSE(file_exists("no_such_file_as_this_one.dat"));

    // Its age is a real number of seconds, not a negative or a nonsense.
    std::time_t age = file_age("makefile");
    EXPECT_GE(age, 0);
}

// =====================================================================
// CSV parsing
// =====================================================================

// The satellite catalogs are CSV, and a field torn in half by a comma inside it shifts every
// column after it -- which is the epoch and the whole set of orbital elements.

TEST(CsvTest, SplitsOnCommas)
{
    std::vector<std::string> r = parse_csv_row("ISS,25544,EA,2023-01-01");
    ASSERT_EQ(r.size(), 4);
    EXPECT_EQ(r[0], "ISS");
    EXPECT_EQ(r[3], "2023-01-01");
}

TEST(CsvTest, KeepsQuotedCommasInsideTheField)
{
    std::vector<std::string> r = parse_csv_row("\"ISS (ZARYA)\",25544,\"EARTH, LOW ORBIT\"");
    ASSERT_EQ(r.size(), 3);
    EXPECT_EQ(r[0], "ISS (ZARYA)");
    EXPECT_EQ(r[1], "25544");
    EXPECT_EQ(r[2], "EARTH, LOW ORBIT");
}

TEST(CsvTest, DoubledQuoteInsideAQuotedFieldIsOneQuote)
{
    std::vector<std::string> r = parse_csv_row("\"He said \"\"no\"\"\",2");
    ASSERT_EQ(r.size(), 2);
    EXPECT_EQ(r[0], "He said \"no\"");
    EXPECT_EQ(r[1], "2");
}

TEST(CsvTest, EmptyFieldsAndEdges)
{
    std::vector<std::string> r = parse_csv_row("A,,B,,C");
    ASSERT_EQ(r.size(), 5);
    EXPECT_EQ(r[1], "");
    EXPECT_EQ(r[3], "");

    r = parse_csv_row("A,B,");
    ASSERT_EQ(r.size(), 3);
    EXPECT_EQ(r[2], "");

    r = parse_csv_row(",A");
    ASSERT_EQ(r.size(), 2);
    EXPECT_EQ(r[0], "");

    EXPECT_EQ(parse_csv_row("").size(), 0);

    r = parse_csv_row("solitary");
    ASSERT_EQ(r.size(), 1);
    EXPECT_EQ(r[0], "solitary");
}

// =====================================================================
// Noise
// =====================================================================

TEST(NoiseTest, FbmIsBoundedAndRepeatable)
{
    // The map generators lean on this for every surface they make, so it has to stay in its banks
    // and give the same answer for the same point -- a world that changed shape between frames
    // would be worse than an ugly one.
    double a = fBm(0.3, 0.4, 0.5, 6, 2.0, 0.5);
    double b = fBm(0.3, 0.4, 0.5, 6, 2.0, 0.5);
    EXPECT_DOUBLE_EQ(a, b);
    EXPECT_TRUE(std::isfinite(a));

    double x, y;
    for (x = -2; x <= 2; x += 0.37) for (y = -2; y <= 2; y += 0.41)
    {
        double v = fBm(x, y, 0.1, 6, 2.0, 0.5);
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_LE(std::fabs(v), 2.0) << " at " << x << ", " << y;
    }
}

TEST(NoiseTest, RidgedFbmIsBoundedAndRepeatable)
{
    double a = ridged_fBm(0.3, 0.4, 0.5, 6, 2.0, 0.5);
    EXPECT_DOUBLE_EQ(a, ridged_fBm(0.3, 0.4, 0.5, 6, 2.0, 0.5));
    EXPECT_TRUE(std::isfinite(a));

    double x;
    for (x = -2; x <= 2; x += 0.37)
    {
        double v = ridged_fBm(x, 0.2, 0.1, 6, 2.0, 0.5);
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_LE(std::fabs(v), 4.0) << " at " << x;
    }
}
