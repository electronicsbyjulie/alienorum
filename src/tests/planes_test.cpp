#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "../globals.h"
#include "../dialogs.h"
#include "../loaders.h"
#include "../classes/wdcolor.h"

using namespace alienorum;

namespace
{
    namespace fs = std::filesystem;

    class OfflineRuntime
    {
        public:
        const fs::path original = fs::current_path();
        const fs::path temporary = fs::temp_directory_path() /
            ("alienorum-planes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        ~OfflineRuntime()
        {
            std::error_code error;
            fs::current_path(original, error);
            fs::remove_all(temporary, error);
        }

        void copy_files()
        {
            fs::create_directory(temporary);
            for (const auto* directory : {"assets", "catalogs", "ephemerides", "galaxies", "maps"})
            {
                if (fs::exists(original / directory))
                    fs::copy(original / directory, temporary / directory, fs::copy_options::recursive);
            }
            for (const auto* file : {"starname.dat", "consline.dat", "locales.json", "exoname.dat", "object_name_pool.dat"})
                fs::copy_file(original / file, temporary / file);
            std::ofstream{temporary / "nonet"};
            std::ofstream{temporary / ".startup-complete"};
            fs::current_path(temporary);
        }
    };

    class LoadedCatalog
    {
        public:
        LoadedCatalog()
        {
            cels = new CelestialObject*[MAX_CELOBJS]();
            vmag_cache = new double[MAX_CELOBJS]();
            bloomrad_cache = new double[MAX_CELOBJS]();
            angular_radius = new double[MAX_CELOBJS]();
            discinstead = new bool[MAX_CELOBJS]();
        }

        ~LoadedCatalog()
        {
            wait_for_texture_tasks();
            for (int index = 0; cels[index]; ++index) delete cels[index];
            delete[] cels;
            delete[] vmag_cache;
            delete[] bloomrad_cache;
            delete[] angular_radius;
            delete[] discinstead;
            delete[] hdcache;
            delete[] hipcache;
            cels = nullptr;
            vmag_cache = bloomrad_cache = angular_radius = nullptr;
            discinstead = nullptr;
            hdcache = hipcache = nullptr;
            ncelobjs = 0;
        }
    };

    double angle_between(const Point& a, const Point& b)
    {
        const double dot = (a.x*b.x + a.y*b.y + a.z*b.z) / (a.magnitude()*b.magnitude());
        return std::acos(std::clamp(dot, -1.0, 1.0)) * fiftyseven;
    }

    Point pole(const Rotation& plane)
    {
        return rotate3D(yaxis, center, plane.v, -plane.a);
    }

    Point sightline(const CelestialObject& object)
    {
        return object.location.system_center + object.location.local_position;
    }

    // The conventional sky basis expresses a published observer-relative pole
    // without using the application's inclination/node conversion.
    Point sky_pole(const Point& line, double inclination, double node)
    {
        const double right_ascension = std::atan2(-line.x, line.z);
        const double declination = std::atan2(line.y, std::hypot(line.x, line.z));
        const Point east(-std::cos(right_ascension), 0, -std::sin(right_ascension));
        const Point north(std::sin(right_ascension)*std::sin(declination), std::cos(declination),
            -std::cos(right_ascension)*std::sin(declination));
        return line * (-std::cos(inclination)/line.magnitude())
            + (east*std::cos(node) - north*std::sin(node))*std::sin(inclination);
    }

    void check_sky_plane(const Rotation& plane, const Point& line, double inclination,
        double node, double tolerance, bool undirected_node = false)
    {
        const Point direction = pole(plane);
        if (std::isnan(node))
        {
            EXPECT_NEAR(angle_between(direction, line * -1), inclination, tolerance);
            return;
        }
        const double expected = angle_between(direction, sky_pole(line, inclination*fiftyseventh, node*fiftyseventh));
        const double alternate = undirected_node
            ? angle_between(direction, sky_pole(line, inclination*fiftyseventh, (node+180)*fiftyseventh))
            : expected;
        EXPECT_LE(std::min(expected, alternate), tolerance);
    }

    const Constellation* constellation_at(const Point& direction)
    {
        const double ra = std::fmod(std::atan2(-direction.x, direction.z) + 2*_pi, 2*_pi);
        return identify_cons_from_coords(ra, std::atan2(direction.y, std::hypot(direction.x, direction.z)));
    }

    void check_info_panel(CelestialObject& object)
    {
        view_mode = vm_spaceship;
        here = CelestialLocation{};
        whereami = trackidx = -1;
        azimuth_correction = 0;
        is_an_obj_under_cursor = object.seqno;
        objinfwnd = true;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1120, 780));
        ImGui::LogToBuffer();
        draw_objinf_window(ImGui::GetIO());
        const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::EndFrame();
        SCOPED_TRACE(text);
        const auto* rotation = constellation_at(pole(object.location.equatorial_plane));
        ASSERT_NE(rotation, nullptr);
        EXPECT_NE(text.find("Rotation pole constellation: " + rotation->name), std::string::npos);
        if (object.orbit)
        {
            const auto* orbital = constellation_at(pole(object.location.orbital_plane));
            ASSERT_NE(orbital, nullptr);
            EXPECT_NE(text.find("Orbital pole constellation: " + orbital->name), std::string::npos);
        }
    }

    void update(CelestialObject& object, double time)
    {
        if (object.typeclass() == class_star) static_cast<Star&>(object).update_location(time);
        else static_cast<Planet&>(object).update_location(time);
    }

    void check_motion_pole(CelestialObject& object)
    {
        if (!object.orbit || !object.orbit->center || !(object.orbit->period > 0)) return;
        const Point before = object.location.local_position - object.orbit->center->location.local_position;
        const Rotation orbital_plane = object.location.orbital_plane;
        update(object, J2000_TIME_T + object.orbit->period * 1e-4);
        const Point after = object.location.local_position - object.orbit->center->location.local_position;
        update(object, J2000_TIME_T);
        // The simulation's Cartesian coordinates reflect the usual celestial basis.
        const Point north = compute_normal(center, before, after) * -1;
        ASSERT_GT(north.magnitude(), 0);
        EXPECT_LE(angle_between(north, pole(orbital_plane)), 0.15);
    }

    CelestialObject* find_target(const char* name)
    {
        const int index = find_object(name);
        return index >= 0 ? cels[index] : nullptr;
    }

    enum class Plane { Equator, Orbit };

    struct Observation
    {
        const char* name;
        Plane plane;
        double inclination, node, tolerance;
        bool undirected_node = false;
    };

    // Fixed values from the cited measurements. An absent node leaves the
    // sky-plane direction free. A disk image often gives a line modulo 180°.
    const Observation observations[] =
    {
        // Beichman & Sanghi et al. 2025, table 4, and Dumusque 2014, 1409.3593.
        {"Rigil Kentaurus", Plane::Orbit, 79.243, 205.073, 0.1},
        {"Toliman", Plane::Orbit, 79.243, 205.073, 0.1},
        {"Toliman", Plane::Equator, 45, NAN, 9},
        // Kervella et al. 2017, relative ICRS state vectors, and Klein et al. 2021.
        {"Proxima Centauri", Plane::Orbit, 69.604, 14.449, 0.15},
        {"Proxima Centauri", Plane::Equator, 47, NAN, 7},
        // Benedict & McArthur 2020, provisional combined fit.
        {"Proxima Centauri c", Plane::Orbit, 133, 331, 1.2},
        // Resolved disks. The adopted planetary alignment is checked separately.
        {"Tau Ceti", Plane::Orbit, 35, 105, 6, true},                 // Lawler et al., stu1641.
        {"82 Eridani", Plane::Orbit, 50, 4, 8, true},               // Kennedy et al., stv511.
        {"Fomalhaut", Plane::Orbit, 65.6, 337.9, 0.8, true},         // MacGregor et al. 2017.
        {"Fomalhaut", Plane::Equator, 65.6, 335, 4, true},           // Le Bouquin et al. 2009, PA 65°.
        {"Beta Pictoris", Plane::Orbit, 86.6, 30, 1, true},         // Resolved disk, 2501.09058.
        {"Beta Pictoris b", Plane::Orbit, 88.94, 211.95, 0.2},      // Brandt et al. 2021.
        {"Beta Pictoris c", Plane::Orbit, 89.1, 211, 0.9},
        // Relative visual binary orbits.
        {"Gliese 86 B", Plane::Orbit, 126.44, 234.2, 2},             // Peretti et al. 2019.
        {"Sirius B", Plane::Orbit, 136.336, 45.400, 0.2},           // Bond et al. 2017.
        {"Polaris", Plane::Orbit, 127.57, 201.28, 2},                // Evans et al. 2024, inner binary.
        {"Alp Eri B", Plane::Orbit, 30.32, 310.91, 1},              // Kervella et al. 2022.
        {"Achernar", Plane::Equator, 60.6, 126.9, 5},               // Domiciano de Souza et al. 2014, PA 216.9°.
        {"Atlas", Plane::Orbit, 107.863, 334.202, 0.5},             // Torres et al. 2025.
        {"Algol C", Plane::Orbit, 83.66, 132.66, 0.5},              // Baron et al. 2012.
        {"61 Cygni", Plane::Orbit, 52.99, 355.73, 1},
        {"Eps Ind C", Plane::Orbit, 77.082, 147.959, 1},
        {"GJ 661 B", Plane::Orbit, 149.14, 160.0, 1},
        {"GJ 4010 B", Plane::Orbit, 63.0, 133.4, 1},
        {"HD 106515 B", Plane::Orbit, 164.45, 92.06, 1},
        {"HD 196885 B", Plane::Orbit, 120.427, 79.15, 1},
        {"AC Her B", Plane::Orbit, 142.9, 155.1, 2},
        {"AD Phe B", Plane::Orbit, 76.92, NAN, 1},
        {"HR Carinae B", Plane::Orbit, 119.2, NAN, 2},
        // Interferometric or activity-based stellar spin inclinations.
        {"Altair", Plane::Equator, 50.7, 211.1, 1.5},              // Bouchaud et al. 2020, PA 301.1°.
        {"Vega", Plane::Equator, 6.2, NAN, 2},
        {"HD 3651", Plane::Equator, 83, NAN, 5},
        {"Alpha Mensae", Plane::Equator, 21, NAN, 8},
        {"HAT-P-7", Plane::Equator, 27, NAN, 18},                   // Masuda 2015, asteroseismology.
        {"WASP-33", Plane::Equator, 58.3, NAN, 5},                   // Watanabe et al. 2022, nodal precession.
        {"HD 3167", Plane::Equator, 111.6, NAN, 4},                 // Bourrier et al. 2021.
        // Resolved belts from the disk library (2501.09058) and Epsilon Eridani (2303.13584).
        {"HD 105", Plane::Orbit, 49, 14, 3, true},
        {"HD 9672", Plane::Orbit, 79.1, 107.4, 3, true},
        {"HD 10638", Plane::Orbit, 60, 10, 3, true},
        {"HD 10647", Plane::Orbit, 77.2, 56.8, 3, true},
        {"HD 14055", Plane::Orbit, 81.1, 163.3, 3, true},
        {"HD 15115", Plane::Orbit, 88, 98.5, 3, true},
        {"HD 16743", Plane::Orbit, 80, 168.4, 3, true},
        {"Epsilon Eridani", Plane::Orbit, 33.7, 178.9, 4, true},
        // Transit and direct astrometry. No absolute node is available for these transits.
        {"Janssen", Plane::Orbit, 83.59, NAN, 1},
        {"TRAPPIST-1 b", Plane::Orbit, 89.73, NAN, 0.4},           // Agol et al. 2021.
        {"TRAPPIST-1 e", Plane::Orbit, 89.79, NAN, 0.4},
        {"TRAPPIST-1 h", Plane::Orbit, 89.81, NAN, 0.4},
        {"HD 3167 b", Plane::Orbit, 87.8, NAN, 2},
        {"HD 3167 c", Plane::Orbit, 89.43, NAN, 1.5},
        {"HAT-P-7 Ab", Plane::Orbit, 83.11, NAN, 2},
        {"WASP-33 b", Plane::Orbit, 86.6, NAN, 4},
        {"Saffar", Plane::Orbit, 14.27, NAN, 5},
        {"Samh", Plane::Orbit, 7.87, 236.85, 2},
        {"Majriti", Plane::Orbit, 23.76, 4.07, 2}
    };

    struct SolarPole
    {
        const char* name;
        double ra, declination, orbit_inclination;
        const char* constellation;
    };
    // IAU rotational elements at J2000 and JPL mean orbital inclinations.
    const SolarPole solar_poles[] =
    {
        {"Sun", 286.13, 63.87, NAN, "Draco"},
        {"Mercury", 281.01, 61.41, 7.005, "Draco"},
        {"Venus", 272.76, 67.16, 3.395, "Draco"},
        {"Earth", 0, 90, 0, "Ursa Minor"},
        {"Jupiter", 268.06, 64.50, 1.305, "Draco"},
        {"Neptune", 299.36, 43.46, 1.770, "Cygnus"}
    };
}

TEST(CatalogPlaneRegression, PublishedPlanesMatchMotionAndReadoutInEverySystem)
{
    OfflineRuntime runtime;
    ASSERT_NO_THROW(runtime.copy_files());
    LoadedCatalog catalog;
    radio_silence = nosats = true;
    simnow = J2000_TIME_T;
    JDnow = J2000;
    ASSERT_TRUE(look_for_catalogs());
    load_stuff();
    ASSERT_GT(ncelobjs, 100000);
    const auto imgui = std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)>(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 900);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    std::set<int> selected;
    for (const auto& observation : observations)
    {
        SCOPED_TRACE(observation.name);
        auto* object = find_target(observation.name);
        if (!object)
        {
            ADD_FAILURE() << "Catalog object unavailable";
            continue;
        }
        selected.insert(object->seqno);
        SCOPED_TRACE(object->name);
        update(*object, J2000_TIME_T);
        const bool equatorial = observation.plane == Plane::Equator;
        const auto& plane = equatorial ? object->location.equatorial_plane : object->location.orbital_plane;
        Point line = sightline(*object);
        if (!equatorial && object->orbit && object->orbit->center)
        {
            auto* primary = static_cast<Star*>(object->orbit->center);
            line = object->typeclass() == class_star
                ? Point::from_ra_dec(primary->right_ascension, primary->declination, 1)
                : sightline(*primary);
        }
        check_sky_plane(plane, line, observation.inclination, observation.node,
            observation.tolerance, observation.undirected_node);
    }

    for (const auto& reference : solar_poles)
    {
        SCOPED_TRACE(reference.name);
        auto* object = find_target(reference.name);
        ASSERT_NE(object, nullptr);
        selected.insert(object->seqno);
        update(*object, J2000_TIME_T);
        const Point expected = Point::from_ra_dec(reference.ra*fiftyseventh, reference.declination*fiftyseventh, 1);
        EXPECT_LE(angle_between(pole(object->location.equatorial_plane), expected), 0.7);
        const auto* constellation = constellation_at(expected);
        ASSERT_NE(constellation, nullptr);
        EXPECT_EQ(constellation->name, reference.constellation);
        if (std::isfinite(reference.orbit_inclination))
        {
            const Point ecliptic = Point::from_ra_dec(270*fiftyseventh, 66.5607*fiftyseventh, 1);
            EXPECT_NEAR(angle_between(pole(object->location.orbital_plane), ecliptic), reference.orbit_inclination, 0.08);
        }
    }

    for (const char* name : {"Proxima Centauri b", "Proxima Centauri d", "Tau Ceti f", "82 Eri d",
        "Gliese 86 b", "55 Cnc A", "55 Cnc B", "Brahe", "TRAPPIST-1", "Upsilon Andromedae"})
    {
        SCOPED_TRACE(name);
        auto* object = find_target(name);
        ASSERT_NE(object, nullptr);
        selected.insert(object->seqno);
        if (object->typeclass() == class_planet)
        {
            ASSERT_NE(object->orbit, nullptr);
            ASSERT_NE(object->orbit->center, nullptr);
            EXPECT_EQ(object->orbit->center->typeclass(), class_star);
        }
    }
    auto* proxima = find_target("Proxima Centauri");
    ASSERT_NE(proxima, nullptr);
    for (const char* name : {"Rigil Kentaurus", "Toliman", "Proxima Centauri", "Beta Pictoris", "55 Cnc B"})
    {
        SCOPED_TRACE(name);
        auto* star = find_target(name);
        ASSERT_NE(star, nullptr);
        ASSERT_EQ(star->typeclass(), class_star);
        const auto& planets = static_cast<Star*>(star)->pl_indices;
        ASSERT_FALSE(planets.empty());
        for (int index : planets)
        {
            ASSERT_EQ(cels[index]->typeclass(), class_planet);
            ASSERT_NE(cels[index]->orbit, nullptr);
            EXPECT_EQ(cels[index]->orbit->center, star);
            selected.insert(index);
        }
    }
    for (const char* name : {"Proxima Centauri b", "Proxima Centauri d"})
    {
        auto* planet = find_target(name);
        ASSERT_NE(planet, nullptr);
        EXPECT_EQ(planet->orbit->center, proxima);
        EXPECT_LE(angle_between(pole(planet->location.orbital_plane), pole(proxima->location.equatorial_plane)), 0.12);
    }
    auto* c = find_target("Proxima Centauri c");
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(static_cast<Planet*>(c)->catalog_candidate);
    EXPECT_GT(angle_between(pole(c->location.orbital_plane), pole(proxima->location.equatorial_plane)), 85);

    // Winn et al. 2026 (2609.19277) challenge the reported spin–orbit angle of HD 3167 b.
    for (const auto& [planet_name, angle] : {std::pair{"HD 3167 c", 107.7},
        std::pair{"HAT-P-7 Ab", 97.0}, std::pair{"WASP-33 b", 108.2}})
    {
        SCOPED_TRACE(planet_name);
        auto* object = find_target(planet_name);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(object->orbit, nullptr);
        ASSERT_NE(object->orbit->center, nullptr);
        EXPECT_NEAR(angle_between(pole(object->location.orbital_plane),
            pole(object->orbit->center->location.equatorial_plane)), angle, 13);
        EXPECT_LE(angle_between(pole(object->location.orbital_plane), pole(object->location.equatorial_plane)), 0.1);
    }

    EXPECT_GE(selected.size(), 55u);
    RecordProperty("objects", static_cast<int>(selected.size()));
    for (int index : selected)
    {
        auto& object = *cels[index];
        SCOPED_TRACE(object.name);
        check_motion_pole(object);
        check_info_panel(object);
    }
}

TEST(StellarPhotosphereRegression, HotPolesHaveABluerVisibleTexture)
{
    Star star;
    std::strcpy(star.name, "Fast A-star fixture");
    std::strcpy(star.spectral_type, "A7V");
    star.temperature = 7500;
    star.BV_color = 0.24;
    star.mass = 1.8 * solar_mass;
    star.volumetric_mean_radius = 1.8 * solar_radius;
    star.sidereal_rotational_period = 7.75 * 3600;
    star.oblateness = 0.22;
    star.fictitious_map_height = 32;
    ASSERT_GT(star.effective_oblateness(), 0.15);

    Map photosphere(&star);
    photosphere.generate_stellar_map(&star);
    ASSERT_TRUE(photosphere.is_complete());
    const auto width = photosphere.get_width(), height = photosphere.get_height();
    std::vector<unsigned char> rgba(width * height * 4);
    photosphere.export_rgba(rgba.data());
    const auto blue_to_red = [&](unsigned long row)
    {
        unsigned long red = 0, blue = 0;
        for (unsigned long column = 0; column < width; ++column)
        {
            const auto index = 4 * (row * width + column);
            red += rgba[index];
            blue += rgba[index + 2];
        }
        return static_cast<double>(blue) / red;
    };
    EXPECT_GT(blue_to_red(0), blue_to_red(height / 2) + 0.01);
    EXPECT_GT(blue_to_red(height - 1), blue_to_red(height / 2) + 0.01);
}

TEST(StellarPhotosphereRegression, OpticalChromaticityChangesBeyondTheReferenceColorGrid)
{
    for (bool white_dwarf : {false, true})
    {
        SCOPED_TRACE(white_dwarf ? "DA white dwarf" : "hot O star");
        Star star;
        std::strcpy(star.name, "Optical chromaticity fixture");
        std::strcpy(star.spectral_type, white_dwarf ? "DA5" : "O3V");
        star.temperature = white_dwarf ? 11000 : 65000;
        star.mass = (white_dwarf ? .6 : 30) * solar_mass;
        star.volumetric_mean_radius = (white_dwarf ? .013 : 8) * solar_radius;
        star.fictitious_map_height = 32;
        star.manual_oblateness = true;

        Color atmosphere;
        if (white_dwarf)
        {
            ASSERT_TRUE(star.resolve_wd_color(atmosphere));
        }
        const auto by_latitude = [&](double flattening)
        {
            star.oblateness = flattening;
            Map photosphere(&star);
            photosphere.generate_stellar_map(&star);
            const auto width = photosphere.get_width(), height = photosphere.get_height();
            std::vector<unsigned char> rgb(width * height * 4);
            photosphere.export_rgba(rgb.data());
            const auto chromaticity = [&](unsigned long row)
            {
                const auto index = 4 * row * width;
                Color linear(wd_srgb_decode(rgb[index] / 255.0),
                    wd_srgb_decode(rgb[index+1] / 255.0), wd_srgb_decode(rgb[index+2] / 255.0));
                const double intensity = linear.red + linear.green + linear.blue;
                return Color(linear.red / intensity, linear.green / intensity, linear.blue / intensity);
            };
            return std::array<Color, 2>{chromaticity(0), chromaticity(height / 2)};
        };

        const auto flat = by_latitude(0);
        EXPECT_DOUBLE_EQ(flat[0].red, flat[1].red);
        EXPECT_DOUBLE_EQ(flat[0].blue, flat[1].blue);
        if (white_dwarf)
        {
            const double sum = atmosphere.red + atmosphere.green + atmosphere.blue;
            EXPECT_NEAR(flat[0].red, atmosphere.red / sum, .004);
            EXPECT_NEAR(flat[0].blue, atmosphere.blue / sum, .004);
        }
        // Keep both O-star temperatures above the grid and make their
        // continuum difference resolvable in the 8-bit texture.
        const auto oblate = by_latitude(white_dwarf ? .28 : .44);
        EXPECT_GT(oblate[0].blue, oblate[1].blue + .004);
        EXPECT_GT(oblate[1].red, oblate[0].red + .004);
    }
}
