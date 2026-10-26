#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <functional>
#include <vector>
#include <iostream>
#include <chrono>
#include <gtest/gtest.h>
#include "../globals.h"
#include "../fictionalstars_ui.h"
#include "../visuals.h"
#include "../inputs.h"
#include "../sphere_impostor.h"
#include "../housekeeping.h"
#include "../classes/clouds_gpu.h"
#include "../classes/surfacebiomes.h"
#include "../classes/surfacenavigation.h"
#include "../classes/scenerydraw.h"
#include "../horizon_ground_gpu.h"
#include "../horizon_terrain_gpu.h"
#include "../horizon_mesh_gpu.h"
#include "../gputex.h"
#include "../sunclock_gpu.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "universe_fixture.h"

using namespace alienorum;


struct CloudTestView
{
    CloudGeometryMode mode = CloudGeometryMode::accelerated;
    double latitude = 0;
    double longitude = 0;
    double azimuth = 0;
    double observer = 1000;
    double shell = 1001;
    double wind = 0;
    double bend = 0;
    bool patterned = false;
    bool overcast = false;
    bool isolated = false;
    bool sample_overhead = false;
    bool dark = false;
    double warmth = 0;
    double cloud_latitude = 0;
    double cloud_longitude = 0;
    double horizon_angle = -0.0001;
};

class SceneryTestMap : public Map
{
    public:
    explicit SceneryTestMap(bool vegetation = false, bool sea = false)
    {
        image_width = image_height = 2;
        allocated = 4;
        lat_scale = 1.0 / _pi;
        lon_scale = 1.0 / _pi;
        red_data = new unsigned char[4];
        green_data = new unsigned char[4];
        blue_data = new unsigned char[4];
        for (int i = 0; i < 4; ++i)
        {
            const bool plant = vegetation && i % 2 == 1;
            red_data[i] = plant ? 80 : (sea ? 20 : 160);
            green_data[i] = plant ? 140 : (sea ? 100 : 120);
            blue_data[i] = plant ? 30 : (sea ? 160 : 80);
        }
        touch_gen();
    }
};

class BumpySceneryTestMap : public SceneryTestMap
{
    public:
    BumpySceneryTestMap() : SceneryTestMap(true)
    {
        bump_data = new double[4]{0, 2500, 1200, 3700};
    }
};

class BlueBiomeTestMap : public SceneryTestMap
{
    public:
    explicit BlueBiomeTestMap(SurfaceMaterial material) : SceneryTestMap(false, true)
    {
        // Identical RGB for sea and foliage makes color classification ambiguous.
        surface_materials.assign(allocated, material);
    }
};

class DistantPlantTestMap : public Map
{
    public:
    explicit DistantPlantTestMap(double radius)
    {
        image_width = 2;
        image_height = 262144;
        allocated = image_width * image_height;
        lat_scale = image_height / _pi;
        lon_scale = image_width / (2 * _pi);
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        surface_materials.assign(allocated, SurfaceMaterial::water);
        for (unsigned long index = 0; index < allocated; index++)
        {
            // Uniform colors isolate geometry from ground painting; only the
            // distant strip is vegetated, with no plants in the old 100 m field.
            red_data[index] = 80;
            green_data[index] = 60;
            blue_data[index] = 40;
            const double north = (half_pi - (index / image_width) / lat_scale) * radius;
            if (north >= 750 && north <= 950) surface_materials[index] = SurfaceMaterial::vegetation;
        }
        touch_gen();
    }
};

class CoastalTestMap : public Map
{
    public:
    CoastalTestMap()
    {
        image_width = 2;
        image_height = 32768;
        allocated = image_width * image_height;
        lat_scale = (image_height - 1.0) / _pi;
        lon_scale = 1.0 / _pi;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        for (unsigned long index = 0; index < allocated; index++)
        {
            const bool sea = index / image_width < image_height / 2;
            red_data[index] = sea ? 20 : 160;
            green_data[index] = sea ? 100 : 120;
            blue_data[index] = sea ? 160 : 80;
        }
        touch_gen();
    }
};

class MountainBandsTestMap : public Map
{
    public:
    MountainBandsTestMap()
    {
        image_width = 2;
        image_height = 32768;
        allocated = image_width * image_height;
        lat_scale = (image_height - 1) / _pi;
        lon_scale = 1.0 / _pi;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        for (unsigned long i = 0; i < allocated; ++i)
        {
            const bool stripe = (i / image_width / 2) % 2 == 0;
            red_data[i] = stripe ? 160 : 110;
            green_data[i] = stripe ? 120 : 100;
            blue_data[i] = 80;
        }
        touch_gen();
    }
};

class RenderingRegressionTest : public UniverseFixture
{
    protected:
    inline static SDL_Window *window = nullptr;
    inline static SDL_GLContext graphics = nullptr;
    std::vector<std::function<void()>> restore;
    GLuint cloud_texture = 0;
    // Real maps carry global generations, even when GL reuses a texture name.
    inline static unsigned int cloud_epoch_sequence = 0;
    unsigned int cloud_epoch = ++cloud_epoch_sequence;
    int render_width = 192, render_height = 108;
    std::vector<double> surface_magnitudes;
    std::vector<double> surface_radii;
    std::vector<double> system_bloom;

    static void SetUpTestSuite()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            return;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        window = SDL_CreateWindow("Rendering regression", 0, 0, 192, 108,
            SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (window)
        {
            graphics = SDL_GL_CreateContext(window);
        }
        if (graphics)
        {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui_ImplOpenGL3_Init("#version 130");
        }
    }

    static void TearDownTestSuite()
    {
        if (graphics)
        {
            clear_horizon_ground_gpu();
            clear_horizon_terrain_gpu();
            clear_horizon_mesh_gpu();
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext();
            SDL_GL_DeleteContext(graphics);
        }
        if (window)
        {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }

    template<class T, class U> void set(T &target, const U &value)
    {
        const T saved = target;
        restore.push_back([&target, saved]()
        {
            target = saved;
        });
        target = value;
    }

    void SetUp() override
    {
        UniverseFixture::SetUp();
        if (!graphics)
        {
            GTEST_SKIP() << "An OpenGL display is unavailable; run with xvfb-run.";
        }
        set(view_mode, vm_spaceship);
        set(whereami, -1);
        set(zoom, 1.0);
        set(dispcx, 96.0);
        set(dispcy, 54.0);
        set(azimuth, 0.0);
        set(azimuth_correction, 0.0);
        set(altitude, 0.0);
        set(here.equatorial_plane.a, 0.0);
        set(dragging, false);
        // Vertex assertions exercise the reference painter; GPU tests compare complete images.
        set(horizon_ground_gpu_reference, true);
        set(horizon_terrain_gpu_reference, true);
        set(horizon_mesh_gpu_reference, true);
        ImGui::GetIO().DisplaySize = ImVec2(192, 108);
        ImGui::GetIO().DeltaTime = 1.0f / 60;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
    }

    void TearDown() override
    {
        if (graphics)
        {
            ImGui::EndFrame();
            if (cloud_texture)
            {
                glDeleteTextures(1, &cloud_texture);
            }
        }
        delete_the_universe();
        for (auto it = restore.rbegin(); it != restore.rend(); ++it)
        {
            (*it)();
        }
        UniverseFixture::TearDown();
    }

    Planet *surface(bool vegetation = false)
    {
        set(dev_dial, 1.0);
        surface_magnitudes.assign(3, 999.0);
        set(vmag_cache, surface_magnitudes.data());
        Star *primary = make_star("55 Cnc A");
        primary->location.local_position = primary->tmprel = Point(1e11, 1e11, 1e11);
        Star *host = make_star("55 Cnc B", 0.2 * solar_mass);
        host->location.local_position = host->tmprel = Point(0, 1e10, 1e10);
        Planet *world = make_planet(host);
        world->cenobj = primary;
        world->looked_for_maps = true;
        world->surf_map = new SceneryTestMap(vegetation);
        world->temperature = 280;
        if (vegetation)
        {
            world->has_water = 1;
            world->vegetation_r = 80;
            world->vegetation_g = 140;
            world->vegetation_b = 30;
            world->ensure_atmosphere()->surface_pressure = oneatm;
        }
        set(view_mode, vm_horizon);
        set(whereami, world->seqno);
        set(mycenobj, static_cast<CelestialObject*>(primary));
        set(viewer_lat, 0.0);
        set(viewer_lon, 0.0);
        set(viewer_eye_height, 1.75);
        set(luminous_flux, 4e10);
        set(show_terrain, true);
        set(show_grid, false);
        set(dittrsa, false);
        set(redlight_mode, false);
        set(here.local_position, Point(0, 0, 0));
        set(altitude, -0.15);
        return world;
    }

    double biome_border(Planet *world)
    {
        SurfaceBiomes field(static_cast<uint32_t>(whereami));
        const double radius = world->get_equatorial_radius();
        double west = -1024.0 / radius, east = 1024.0 / radius;
        for (int step = 0; step < 40; ++step)
        {
            const double middle = (west + east) * 0.5;
            if (field.coordinates(radius, viewer_lat, middle, shore_displacement).second < 0)
            {
                west = middle;
            }
            else
            {
                east = middle;
            }
        }
        const double border = (west + east) * 0.5;
        const double step = 0.25 / radius;
        const double north = field.coordinates(radius, viewer_lat + step, border, shore_displacement).second
            - field.coordinates(radius, viewer_lat - step, border, shore_displacement).second;
        const double eastward = field.coordinates(radius, viewer_lat, border + step, shore_displacement).second
            - field.coordinates(radius, viewer_lat, border - step, shore_displacement).second;
        set(azimuth, atan2(-north, eastward));
        return border;
    }

    std::vector<ImDrawVert> scenery()
    {
        find_horizon();
        draw_horizon();
        const auto &vertices = ImGui::GetBackgroundDrawList()->VtxBuffer;
        std::vector<ImDrawVert> result(vertices.begin(), vertices.end());
        ImGui::EndFrame();
        ImGui::NewFrame();
        return result;
    }

    std::vector<unsigned char> render()
    {
        ImGui::Render();
        glViewport(0, 0, render_width, render_height);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::vector<unsigned char> pixels(render_width * render_height * 4);
        glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        EXPECT_EQ(glGetError(), static_cast<unsigned int>(GL_NO_ERROR));
        return pixels;
    }

    void freeze_surface_time()
    {
        ImGui::GetCurrentContext()->Time = 50.0;
        // This ImGui version keys background-list resets by time, rather than frame count.
        auto *viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
        viewport->BgFgDrawListsLastTimeActive[0] = -1.0f;
        viewport->BgFgDrawListsLastTimeActive[1] = -1.0f;
    }

    int image_difference(const std::vector<unsigned char> &actual, const std::vector<unsigned char> &expected)
    {
        int count = 0;
        for (size_t i = 0; i < actual.size(); i += 4)
        {
            if (std::equal(actual.begin() + i, actual.begin() + i + 4, expected.begin() + i)) continue;
            if (count < 4)
            {
                std::cout << "Pixel " << (i / 4) % render_width << ", " << (i / 4) / render_width << ": actual ";
                for (int c = 0; c < 4; ++c) std::cout << static_cast<int>(actual[i + c]) << ' ';
                std::cout << "expected ";
                for (int c = 0; c < 4; ++c) std::cout << static_cast<int>(expected[i + c]) << ' ';
                std::cout << std::endl;
            }
            ++count;
        }
        return count;
    }

    std::vector<unsigned char> mesh_image(bool reference)
    {
        horizon_mesh_gpu_reference = reference;
        freeze_surface_time();
        find_horizon();
        draw_horizon();
        auto pixels = render();
        ImGui::NewFrame();
        return pixels;
    }

    std::vector<unsigned char> clouds(unsigned char coverage, double elevation, double field_scale,
        const CloudTestView &view = CloudTestView())
    {
        if (!cloud_texture)
        {
            glGenTextures(1, &cloud_texture);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cloud_texture);
        const int map_width = view.isolated ? 4096 : 8;
        const int map_height = view.isolated ? 2048 : 4;
        std::vector<unsigned char> color(map_width * map_height * 3, 0);
        for (int i = 0; i < 8 * 4; ++i)
        {
            const unsigned char value = view.isolated || (view.patterned && i % 3 == 0) ? 0 : coverage;
            color[i * 3] = color[i * 3 + 1] = color[i * 3 + 2] = value;
        }
        if (view.isolated)
        {
            const int x = int((view.cloud_longitude / (2 * _pi) + 0.5) * map_width);
            const int y = int((0.5 - view.cloud_latitude / _pi) * map_height);
            const int pixel = (y * map_width + x) * 3;
            color[pixel] = color[pixel + 1] = color[pixel + 2] = coverage;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, map_width, map_height, 0, GL_RGB, GL_UNSIGNED_BYTE, color.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, view.isolated ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, view.isolated ? GL_NEAREST : GL_LINEAR);
        float refraction[1025];
        for (int i = 0; i <= 1024; ++i)
        {
            const double angle = -half_pi + i * _pi / 1024;
            refraction[i] = angle + view.bend * cos(angle);
        }
        CloudSkyGpuInput input =
        {
        };
        input.camera[1] = elevation;
        input.camera[0] = view.azimuth;
        input.camera[2] = field_scale;
        input.camera[3] = field_scale * render_height / render_width;
        const double clat = cos(view.latitude), slat = sin(view.latitude);
        const double clon = cos(view.longitude), slon = sin(view.longitude);
        input.camera[4] = -slon;
        input.camera[5] = clon;
        input.camera[8] = -slat * clon;
        input.camera[9] = -slat * slon;
        input.camera[10] = clat;
        input.camera[12] = clat * clon;
        input.camera[13] = clat * slon;
        input.camera[14] = slat;
        input.world[0] = view.observer;
        input.world[1] = view.shell;
        input.world[2] = (view.shell - view.observer) * (view.shell + view.observer);
        input.world[3] = sin(view.horizon_angle);
        input.light[0] = view.dark ? 0 : 1;
        input.light[1] = view.dark ? 0 : 0.6;
        input.light[2] = view.overcast ? 1 : 0;
        input.light[5] = view.warmth;
        input.light[8] = view.wind;
        input.texture = cloud_texture;
        input.revision = cloud_epoch * 1024 + coverage + (view.patterned ? 256 : 0) + (view.isolated ? 512 : 0);
        input.refraction = refraction;
        input.geometry_mode = view.mode;
        input.sample_overhead = view.sample_overhead;
        EXPECT_TRUE(queue_cloud_sky_gpu(input));
        auto pixels = render();
        ImGui::NewFrame();
        return pixels;
    }

    void compare_clouds(unsigned char coverage, double elevation, double scale, CloudTestView view)
    {
        view.mode = CloudGeometryMode::reference;
        const auto reference = clouds(coverage, elevation, scale, view);
        view.mode = CloudGeometryMode::accelerated;
        const auto accelerated = clouds(coverage, elevation, scale, view);
        ASSERT_TRUE(cloud_sky_gpu_stats().geometry_cached);
        int differing = 0, maximum = 0;
        for (size_t i = 0; i < reference.size(); ++i)
        {
            const int gap = abs(int(reference[i]) - int(accelerated[i]));
            differing += gap > 2;
            maximum = std::max(maximum, gap);
        }
        EXPECT_EQ(differing, 0) << "maximum channel difference=" << maximum;
        view.mode = CloudGeometryMode::all_clusters;
        const auto uncropped = clouds(coverage, elevation, scale, view);
        EXPECT_TRUE(accelerated == uncropped);
    }
};

TEST_F(RenderingRegressionTest, SunclockWithoutNightMapKeepsSurfaceVisibleThroughCloudGaps)
{
    Planet *world = surface();
    world->sidereal_rotational_period = 0;
    world->lon_J2000_offset = 0;
    world->location.local_position = Point();
    world->location.equatorial_plane.a = 0;
    world->cenobj->location.local_position = Point(0, 0, -AU);
    world->orbit->center->location.local_position = Point(0, 0, -AU);
    class PatchyCloudMap : public SceneryTestMap
    {
        public:
        PatchyCloudMap()
        {
            for (int i = 0; i < 4; ++i)
            {
                red_data[i] = green_data[i] = blue_data[i] = i % 2 ? 32 : 0;
            }
            touch_gen();
        }
    };
    world->cloud_map = new PatchyCloudMap();
    set(show_clouds, false);
    world->transparent_clouds = true;
    set(view_mode, vm_sunclock);
    set(selected_locale, static_cast<Locale*>(nullptr));
    set(redlight_mode, false);

    const auto capture = [&]()
    {
        draw_sunclock();
        auto pixels = render();
        ImGui::NewFrame();
        double sum = 0;
        for (int y = 40; y < 68; ++y)
        {
            for (int x = 72; x < 120; ++x)
            {
                const auto *rgb = &pixels[(y * render_width + x) * 4];
                sum += rgb[0] + rgb[1] + rgb[2];
            }
        }
        return sum;
    };
    const double clear = capture();
    show_clouds = true;
    const double cloudy = capture();
    EXPECT_GT(clear, 0);
    EXPECT_GT(cloudy, clear * 0.8);
}

TEST_F(RenderingRegressionTest, LavaSunclockDayStaysBrighterThanNightUnderStellarTint)
{
    Planet *world = make_planet(make_star(), "Lava sunclock test", 0.02 * AU);
    world->type = lavaworld;
    world->sidereal_rotational_period = world->orbit->period;
    world->rnd_seed = 173;
    world->fictitious_map_height = 64;
    world->ring_status_known = true;
    world->surf_map = new Map(world);
    world->night_map = new Map(world);

    SunClockGpuInput input = {};
    input.sclk_scale = half_pi / dispcx;
    input.zoom = 1.0;
    input.daylight_tint[0] = 1.0;
    input.daylight_tint[1] = 0.65;
    input.daylight_tint[2] = 0.40;
    for (int i = 0; i < 3; ++i) input.body_axes[i] = earth_radius;
    for (int i = 0; i < 4; ++i) input.rot_matrix[i * 5] = 1.0;

    const auto capture = [&](double longitude, bool day)
    {
        freeze_surface_time();
        input.azimuth = longitude;
        input.light_dir[0] = -sin(longitude) * (day ? 1.0 : -1.0);
        input.light_dir[2] = cos(longitude) * (day ? 1.0 : -1.0);
        EXPECT_TRUE(queue_sunclock_gpu(input, dispcx, dispcy));
        auto pixels = render();
        ImGui::NewFrame();
        return pixels;
    };
    const auto brightness = [&](const std::vector<unsigned char> &pixels)
    {
        double sum = 0.0;
        for (int y = render_height / 4; y < 3 * render_height / 4; ++y)
        {
            for (int x = render_width / 4; x < 3 * render_width / 4; ++x)
            {
                const auto *rgb = &pixels[(y * render_width + x) * 4];
                sum += 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
            }
        }
        return sum;
    };

    for (double temperature : {1600.0, 3200.0, 6000.0})
    {
        world->temperature = temperature;
        world->surf_map->generate_lava_map(world);
        world->night_map->generate_lava_map(world, true);
        input.day_map_texture = gputex_for(world->surf_map);
        input.night_map_texture = gputex_for(world->night_map);
        input.bump_map_texture = gputex_bump_for(world->surf_map);
        for (double longitude : {0.0, 1.3, _pi})
        {
            const double day = brightness(capture(longitude, true));
            const double night = brightness(capture(longitude, false));
            EXPECT_GT(night, 0.0);
            EXPECT_GT(day, night * 2.0) << temperature << ", longitude " << longitude;
        }
    }
}

TEST_F(RenderingRegressionTest, UnresolvedLabelsUsePrimaryAndResolvedCompanionsKeepTheirNames)
{
    Star *sun = make_star("Sun");
    Star *primary = make_star("Xi Ursae Majoris");
    primary->set_component('A', primary);
    primary->absolute_magnitude = 5.09;
    Star *companion = make_star("Xi Ursae Majoris B");
    companion->make_companion_of(primary, 'B');
    companion->absolute_magnitude = 4.47;
    primary->cenobj = companion->cenobj = primary;
    primary->drawnx = companion->drawnx = 96;
    primary->drawny = companion->drawny = 54;
    double magnitudes[3] = {5, 5, 5};
    double angular_sizes[3] = {0, 0, 0};
    double bloom[3] = {0, 0, 0};
    bool discs[3] = {false, false, false};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(angular_sizes));
    set(bloomrad_cache, static_cast<double*>(bloom));
    set(discinstead, static_cast<bool*>(discs));
    set(labels2draw, std::vector<DrawLabel>{});
    set(selected, companion->seqno);
    set(shortnames, false);
    set(cbolbls_selected_idx, 0);
    set(show_localsys, true);
    set(global_brightness, 1.0);
    set(mycenobj, static_cast<CelestialObject*>(sun));
    ASSERT_TRUE(draw_single_object(companion->seqno));
    ASSERT_EQ(labels2draw.size(), 1u);
    EXPECT_EQ(labels2draw.back().text, "Xi Ursae Majoris");
    set(mycenobj, static_cast<CelestialObject*>(primary));
    labels2draw.clear();
    ASSERT_TRUE(draw_single_object(companion->seqno));
    ASSERT_EQ(labels2draw.size(), 1u);
    EXPECT_EQ(labels2draw.back().text, "Xi Ursae Majoris");
    companion->drawnx = 101;
    labels2draw.clear();
    ASSERT_TRUE(draw_single_object(companion->seqno));
    ASSERT_EQ(labels2draw.size(), 1u);
    EXPECT_EQ(labels2draw.back().text, "Xi Ursae Majoris B");
    companion->drawnx = 96;
    set(mycenobj, static_cast<CelestialObject*>(sun));
    set(shortnames, true);
    strcpy(primary->Gouldcons, "UMa");
    struct ShortLabel
    {
        const char *bayer, *flamsteed;
        int gould;
        const char *text;
    };
    const ShortLabel formats[] =
    {
        {"Xi  UMa", "53  UMa", 82, "Xi UMa"},
        {"", "53  UMa", 82, "53 UMa"},
        {"", "", 82, "82G UMa"}
    };
    for (const auto &format : formats)
    {
        strcpy(primary->Bayer, format.bayer);
        strcpy(primary->Flamsteed, format.flamsteed);
        primary->GouldNo = format.gould;
        labels2draw.clear();
        ASSERT_TRUE(draw_single_object(companion->seqno));
        ASSERT_EQ(labels2draw.size(), 1u);
        EXPECT_EQ(labels2draw.back().text, format.text);
    }
    companion->drawnx = 101;
    for (int category : {lbltype_brightest, lbltype_Bayer, lbltype_Flamsteed, lbltype_Gould})
    {
        cbolbls_selected_idx = category;
        labels2draw.clear();
        ASSERT_TRUE(draw_single_object(companion->seqno));
        ASSERT_EQ(labels2draw.size(), 1u);
        EXPECT_EQ(labels2draw.back().text, "Xi Ursae Majoris B");
    }
    // Separately numbered catalog designations remain useful for resolved companions.
    strcpy(primary->Bayer, "Zet2Ret");
    strcpy(companion->Bayer, "Zet1Ret");
    primary->BayerGrkno = 152;
    companion->BayerGrkno = 151;
    strcpy(primary->Flamsteed, "53  UMa");
    strcpy(companion->Flamsteed, "54  UMa");
    strcpy(companion->Gouldcons, "UMa");
    primary->GouldNo = 82;
    companion->GouldNo = 83;
    const std::pair<int, std::string> distinct_labels[] =
    {
        {lbltype_Bayer, std::string(1, Greek_symbol_mapping[5]) + "1"},
        {lbltype_Flamsteed, "54"},
        {lbltype_Gould, "83"}
    };
    for (const auto &format : distinct_labels)
    {
        cbolbls_selected_idx = format.first;
        labels2draw.clear();
        ASSERT_TRUE(draw_single_object(companion->seqno));
        ASSERT_EQ(labels2draw.size(), 1u);
        EXPECT_EQ(labels2draw.back().text, format.second);
    }
    cbolbls_selected_idx = lbltype_Bayer;
    selected = primary->seqno;
    labels2draw.clear();
    ASSERT_TRUE(draw_single_object(primary->seqno));
    ASSERT_EQ(labels2draw.size(), 1u);
    EXPECT_EQ(labels2draw.back().text, std::string(1, Greek_symbol_mapping[5]) + "2");
    selected = companion->seqno;
    // An actually shared Bayer designation still preserves the component name.
    strcpy(companion->Bayer, primary->Bayer);
    companion->BayerGrkno = primary->BayerGrkno;
    cbolbls_selected_idx = lbltype_Bayer;
    labels2draw.clear();
    ASSERT_TRUE(draw_single_object(companion->seqno));
    ASSERT_EQ(labels2draw.size(), 1u);
    EXPECT_EQ(labels2draw.back().text, "Xi Ursae Majoris B");
}

TEST_F(RenderingRegressionTest, CompanionHoverAndSelectionFollowFivePixelSeparationInEveryView)
{
    Star *sun = make_star("Sun");
    Star *primary = make_star("Xi Ursae Majoris");
    primary->set_component('A', primary);
    Star *companion = make_star("Xi Ursae Majoris B");
    companion->make_companion_of(primary, 'B');
    primary->cenobj = companion->cenobj = primary;
    primary->drawnx = companion->drawnx = 96;
    primary->drawny = companion->drawny = 54;
    double magnitudes[3] = {5, 5, 4};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(trackidx, -1);
    set(selected, -1);
    set(whereami, 0);
    set(view_mode, vm_spaceship);
    set(is_click, true);
    set(dragged, false);
    set(mycenobj, static_cast<CelestialObject*>(sun));
    set(is_an_obj_under_cursor, -1);
    set(is_a_locale_under_cursor, static_cast<Locale*>(nullptr));
    set(obj_magn_under_cursor, 1e9);
    set(circle_size, 8);
    ImGuiIO &io = ImGui::GetIO();
    for (auto mode : {vm_spaceship, vm_horizon, vm_skymap, vm_sunclock, vm_system})
    {
        view_mode = mode;
        for (Star* observer_system : {sun, primary})
        {
            mycenobj = observer_system;
            if (mode == vm_system && observer_system != primary) continue;
            for (double separation : {0.0, 4.99, 5.0, 5.01, 4.99, 0.0})
            {
                companion->drawnx = 96 + separation;
                io.MousePos = ImVec2(companion->drawnx, companion->drawny);
                const int expected = separation >= 5 ? companion->seqno : primary->seqno;
                for (double primary_magnitude : {3.0, 5.0})
                {
                    magnitudes[1] = primary_magnitude;
                    selected = primary->seqno;
                    trackidx = -1;
                    identify_object_under_cursor(io);
                    EXPECT_EQ(is_an_obj_under_cursor, expected) << separation;
                    EXPECT_EQ(selected, expected) << separation;
                }
                trackidx = companion->seqno;
                identify_object_under_cursor(io);
                EXPECT_EQ(is_an_obj_under_cursor, expected) << separation;
            }
        }
    }
    companion->drawnx = 99;
    companion->drawny = 58;
    EXPECT_EQ(stellar_display_target(companion), companion);
    companion->drawny = 57.99;
    EXPECT_EQ(stellar_display_target(companion), primary);
}

TEST_F(RenderingRegressionTest, FictionalRegionDialogRendersWithImageAndAnalyticFallback)
{
    make_star("Sun");
    Galaxy* galaxy = new Galaxy();
    strcpy(galaxy->name, "M31");
    galaxy->PGC = 2557;
    galaxy->right_ascension = 0.186;
    galaxy->declination = 0.720;
    galaxy->distance = 780000*parsec;
    galaxy->volumetric_mean_radius = 70000*light_year;
    galaxy->location.galactic_center = Point(0, 0, 2.54);
    append_cel(galaxy);
    set(selected, galaxy->seqno);
    ImGui::EndFrame();
    SDL_SetWindowSize(window, 1024, 960);
    ImGui::GetIO().DisplaySize = ImVec2(1024, 960);
    render_width = 1024;
    render_height = 960;
    for (bool fallback : {false, true})
    {
        if (fallback) strcpy(galaxy->name, "No image fictional test galaxy");
        open_fictional_region();
        for (int frame = 0; frame < 2; ++frame)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20, 20));
            draw_fictional_region();
            ImGuiWindow* panel = ImGui::FindWindowByName("Populate Region");
            ASSERT_NE(panel, nullptr);
            EXPECT_GT(panel->DrawList->VtxBuffer.Size, 100);
            EXPECT_EQ(ncelobjs, 2);
            if (frame == 1)
            {
                const auto pixels = render();
                if (!fallback)
                {
                    std::vector<unsigned char> flipped(pixels.size());
                    for (int y = 0; y < render_height; ++y)
                        std::copy_n(pixels.data()+(render_height-1-y)*render_width*4, render_width*4,
                            flipped.data()+y*render_width*4);
                    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), render_width,
                        render_height, 32, render_width*4, SDL_PIXELFORMAT_RGBA32);
                    ASSERT_NE(surface, nullptr);
                    EXPECT_EQ(IMG_SavePNG(surface, "/tmp/alienorum-fictional-region-ui.png"), 0);
                    SDL_FreeSurface(surface);
                }
            }
            else ImGui::EndFrame();
        }
        close_fictional_region();
    }
    SDL_SetWindowSize(window, 192, 108);
    ImGui::GetIO().DisplaySize = ImVec2(192, 108);
    ImGui::NewFrame();
}

TEST_F(RenderingRegressionTest, MoonLabelsRequireSeparationAndExistingVisibilityFilters)
{
    Star* sun = make_star("Sun");
    Planet* parent = make_planet(sun, "Parent");
    Moon* moon = new Moon();
    strcpy(moon->name, "Test moon");
    moon->orbit = new Orbit();
    moon->orbit->center = parent;
    moon->cenobj = sun;
    append_cel(moon);
    parent->drawnx = 96;
    parent->drawny = 54;
    double magnitudes[3] = {5, 5, 5}, sizes[3] = {}, bloom[3] = {};
    bool discs[3] = {};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(sizes));
    set(bloomrad_cache, static_cast<double*>(bloom));
    set(discinstead, static_cast<bool*>(discs));
    set(labels2draw, std::vector<DrawLabel>{});
    set(selected, -1);
    set(show_localsys, true);
    set(lbl_localsys, true);
    set(show_labels, true);
    set(global_brightness, 1.0);
    set(mycenobj, static_cast<CelestialObject*>(sun));
    for (const Cartesian2D& offset : {Cartesian2D(4.99, 0), Cartesian2D(5, 0),
        Cartesian2D(5.01, 0), Cartesian2D(3, 4), Cartesian2D(2.99, 4)})
    {
        moon->drawnx = 96 + offset.x;
        moon->drawny = 54 + offset.y;
        const bool separated = offset.x * offset.x + offset.y * offset.y >= 25;
        for (bool is_selected : {false, true})
        {
            selected = is_selected ? moon->seqno : -1;
            labels2draw.clear();
            ASSERT_TRUE(draw_single_object(moon->seqno));
            EXPECT_EQ(labels2draw.size(), separated ? 1u : 0u);
        }
    }
    selected = -1;
    lbl_localsys = false;
    moon->drawnx = 106;
    labels2draw.clear();
    ASSERT_TRUE(draw_single_object(moon->seqno));
    EXPECT_TRUE(labels2draw.empty());
}

TEST_F(RenderingRegressionTest, ResolvedCompanionLabelsRespectVisibilityAndRetainPrimarySuffix)
{
    Star* sun = make_star("Sun");
    Star* primary = make_star("System A");
    primary->set_component('A', primary);
    Star* companion = make_star("System B");
    companion->make_companion_of(primary, 'B');
    primary->cenobj = companion->cenobj = primary;
    primary->drawnx = 96;
    primary->drawny = companion->drawny = 54;
    companion->drawnx = 101;
    double magnitudes[3] = {5, 5, 5}, sizes[3] = {}, bloom[3] = {};
    bool discs[3] = {};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(sizes));
    set(bloomrad_cache, static_cast<double*>(bloom));
    set(discinstead, static_cast<bool*>(discs));
    set(labels2draw, std::vector<DrawLabel>{});
    set(selected, -1);
    set(shortnames, false);
    set(cbolbls_selected_idx, lbltype_brightest);
    set(show_localsys, true);
    set(show_labels, true);
    set(lbl_localsys, false);
    set(label_favestars, false);
    set(appmagn_lblcut, 6.0);
    set(global_brightness, 1.0);
    set(mycenobj, static_cast<CelestialObject*>(sun));
    for (double separation : {4.99, 5.0, 5.01, 4.99})
    {
        companion->drawnx = 96 + separation;
        labels2draw.clear();
        ASSERT_TRUE(draw_single_object(companion->seqno));
        if (separation >= 5)
        {
            ASSERT_EQ(labels2draw.size(), 1u);
            EXPECT_EQ(labels2draw.back().text, "System B");
        }
        else EXPECT_TRUE(labels2draw.empty());
        labels2draw.clear();
        ASSERT_TRUE(draw_single_object(primary->seqno));
        ASSERT_EQ(labels2draw.size(), 1u);
        EXPECT_EQ(labels2draw.back().text, separation >= 5 ? "System A" : "System");
    }
    companion->drawnx = 101;
    show_labels = false;
    labels2draw.clear();
    ASSERT_TRUE(draw_single_object(companion->seqno));
    EXPECT_TRUE(labels2draw.empty());
}

TEST_F(RenderingRegressionTest, InnerPairsAndFilteredPrimariesUseTheirProjectedSeparation)
{
    make_star("Sun");
    Star* primary = make_star("System A");
    primary->set_component('A', primary);
    Star* secondary = make_star("System B");
    secondary->make_companion_of(primary, 'B');
    Star* tertiary = make_star("System C");
    tertiary->make_companion_of(secondary, 'C');
    primary->drawnx = 96;
    secondary->drawnx = 106;
    tertiary->drawnx = 110;
    primary->drawny = secondary->drawny = tertiary->drawny = 54;
    EXPECT_EQ(stellar_display_target(tertiary), secondary);
    tertiary->drawnx = 111;
    EXPECT_EQ(stellar_display_target(tertiary), tertiary);
    primary->drawnx = -1e9;
    primary->tmprel = Point(0, 0, 1000);
    set(here.equatorial_plane, Rotation());
    set(view_mode, vm_spaceship);
    secondary->drawnx = 100;
    EXPECT_EQ(stellar_display_target(secondary), primary);
    secondary->drawnx = 101;
    EXPECT_EQ(stellar_display_target(secondary), secondary);
}

TEST_F(RenderingRegressionTest, CloseCompanionsFollowTheCurrentAstrometricOriginWithoutSelection)
{
    Star* sun = make_star("Sun");
    Star* primary = make_star("Moving primary");
    primary->set_component('A', primary);
    primary->distance = 377 * parsec;
    primary->right_ascension = 83.817225 * fiftyseventh;
    primary->declination = -5.3852166667 * fiftyseventh;
    primary->proper_motion_RA = 0.005 * arcsecond / oneyear;
    primary->proper_motion_decl = 0.002 * arcsecond / oneyear;
    primary->epoch = J2000;
    primary->update_location(J2000_TIME_T);
    Star* companion = make_star("Close companion");
    companion->make_companion_of(primary, 'B');
    companion->orbit->semimajor_axis = .12 * AU;
    companion->orbit->period = 6.471 * oneday;
    companion->orbit->epoch = J2000;
    companion->apparent_magnitude = primary->apparent_magnitude = 8;
    Star* inner = make_star("Nested companion");
    inner->make_companion_of(companion, 'C');
    inner->orbit->semimajor_axis = .001 * AU;
    inner->orbit->period = .1 * oneday;
    inner->orbit->epoch = J2000;
    inner->apparent_magnitude = 10;
    primary->cenobj = companion->cenobj = inner->cenobj = primary;
    set(here, CelestialLocation());
    set(mycenobj, static_cast<CelestialObject*>(sun));
    set(whereami, 0);
    set(selected, -1);
    set(trackidx, -1);
    set(editidx, -1);
    set(global_brightness, 125.0);
    set(redo_proper_motions, false);
    set(simnow, static_cast<double>(J2000_TIME_T));
    for (double years : {26.0, 27.0})
    {
        simnow = J2000_TIME_T + years * oneyear;
        const Point expected = Point::from_ra_dec(primary->right_ascension + primary->proper_motion_RA * years * oneyear,
            primary->declination + primary->proper_motion_decl * years * oneyear, primary->distance);
        for (double magnification : {1.0, 9650.211296})
        {
            zoom = magnification;
            // The ancestors can be outside the camera and can occur later in an update pass.
            primary->drawnx = primary->drawny = -1e9;
            companion->drawnx = companion->drawny = -1e9;
            ASSERT_TRUE(compute_object_location(inner));
            EXPECT_LT((primary->location.system_center - expected).magnitude(), 1);
            EXPECT_LT((primary->tmprel - expected).magnitude(), 1);
            EXPECT_LT(companion->location.distance_to(primary->location), .121 * AU);
            EXPECT_LT(inner->location.distance_to(companion->location), .00101 * AU);
            EXPECT_LT(find_3D_angle(Point(primary->location), Point(companion->location), Point(here)), .00033 * arcsecond);
        }
    }
}

TEST_F(RenderingRegressionTest, ObservingObjectNeverEntersSkyRenderingOrRetainsPickingBounds)
{
    std::vector<CelestialObject*> objects =
    {
        new Star(), new Planet(), new Moon(), new Satellite(), new Comet(), new Galaxy()
    };
    for (auto *object : objects)
    {
        object->volumetric_mean_radius = 1000;
        object->looked_for_maps = true;
        append_cel(object);
    }
    for (auto *object : objects)
    {
        set(whereami, object->seqno);
        for (auto mode : {vm_spaceship, vm_horizon, vm_skymap})
        {
            set(view_mode, mode);
            object->tmprel = Point(0, 0, 10000);
            for (int path = 0; path < 3; path++)
            {
                object->onscreen = object->label_shown = true;
                object->drawnx = object->drawny = 50;
                object->drawnxmin = object->drawnymin = 10;
                object->drawnxmax = object->drawnymax = 90;
                const int vertices = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
                const int commands = ImGui::GetBackgroundDrawList()->CmdBuffer.Size;
                if (path == 0) EXPECT_EQ(draw_sphere_gpu(object, 0.1), 0);
                else if (path == 1) EXPECT_EQ(draw_sphere(object, 0.1), 0);
                else EXPECT_FALSE(draw_single_object(object->seqno));
                EXPECT_EQ(ImGui::GetBackgroundDrawList()->VtxBuffer.Size, vertices);
                EXPECT_EQ(ImGui::GetBackgroundDrawList()->CmdBuffer.Size, commands);
                EXPECT_FALSE(object->onscreen);
                EXPECT_FALSE(object->label_shown);
                EXPECT_LT(object->drawnxmax, -1e8);
                EXPECT_LT(object->drawnymax, -1e8);
            }
            if (object->typeclass() == class_galaxy)
            {
                set(show_galaxy_band, true);
                set(inside_galaxy_idx, object->seqno);
                object->onscreen = true;
                draw_galaxy_band();
                EXPECT_FALSE(object->onscreen);
            }
        }
    }
}

TEST_F(RenderingRegressionTest, SystemPreviewCanStillDrawTheObservingPlanet)
{
    Planet* world = surface();
    set(view_mode, vm_system);
    world->drawnx = dispcx;
    world->drawny = dispcy;
    EXPECT_GT(draw_sphere_gpu(world, 20), 0);
    EXPECT_TRUE(world->onscreen);
}

TEST_F(RenderingRegressionTest, CloseExteriorPlanetMatchesItsAngularSilhouetteInBothSkyProjections)
{
    Planet* world = surface();
    set(whereami, 0);
    set(here, CelestialLocation());
    set(azimuth, 0.0);
    set(azimuth_correction, 0.0);
    set(altitude, 0.0);
    set(myeq, 0.0);
    system_bloom.assign(3, 0);
    set(bloomrad_cache, system_bloom.data());
    world->volumetric_mean_radius = 118500600;
    world->oblateness = 0;
    world->location.equatorial_plane = Rotation();
    for (auto mode : {vm_spaceship, vm_skymap})
    {
        set(view_mode, mode);
        set(zoom, mode == vm_spaceship ? 0.1 : 1.0);
        for (double ratio : {1.018827, 1.12, 2.0})
        {
            const double distance = world->volumetric_mean_radius * ratio;
            world->tmprel = world->location.local_position = Point(0, 0, distance);
            const double angle = asin(1 / ratio);
            const double half_extent = mode == vm_skymap ? angle / _pi * dispcx : tan(angle) * zoom * dispcx;
            draw_sphere(world, angle * zoom);
            const auto pixels = render();
            int painted = 0, outside = 0;
            for (int y = 0; y < render_height; y++)
            {
                for (int x = 0; x < render_width; x++)
                {
                    if (pixels[(y * render_width + x) * 4 + 3] == 0) continue;
                    painted++;
                    if (fabs(x + 0.5 - dispcx) > half_extent + 2
                        || fabs(y + 0.5 - dispcy) > half_extent + 2) outside++;
                }
            }
            EXPECT_GT(painted, 10) << mode << ", R/d=" << 1 / ratio;
            EXPECT_EQ(outside, 0) << mode << ", R/d=" << 1 / ratio;
            ImGui::NewFrame();
        }
    }
}

TEST_F(RenderingRegressionTest, ClosePlanetAngularRadiusMatchesTheTangentRay)
{
    Planet* world = surface();
    cels[0]->deleted = cels[1]->deleted = true;
    delete world->orbit;
    world->orbit = nullptr;
    world->volumetric_mean_radius = 1000;
    world->location.system_center = world->location.galactic_center = Point();
    set(whereami, -1);
    set(view_mode, vm_spaceship);
    set(here, CelestialLocation());
    surface_radii.assign(3, 0);
    set(angular_radius, surface_radii.data());
    for (double distance : {1100.0, 2000.0, 10000.0})
    {
        world->location.local_position = Point(0, 0, distance);
        compute_object_draw_coordinates();
        EXPECT_NEAR(sin(surface_radii[world->seqno]) * world->tmprel.magnitude(), 1000, 1e-8);
    }
}

TEST_F(RenderingRegressionTest, TrackingTheObserverCannotMakeItTheSkyHoverTarget)
{
    Planet* world = surface();
    set(trackidx, world->seqno);
    set(is_an_obj_under_cursor, -1);
    ImGui::GetIO().MousePos = ImVec2(96, 54);
    identify_object_under_cursor(ImGui::GetIO());
    EXPECT_EQ(trackidx, -1);
    EXPECT_NE(is_an_obj_under_cursor, world->seqno);
}

TEST_F(RenderingRegressionTest, RingsQueueWhenThePlanetDiscIsBehindTheCamera)
{
    Star *sun = make_star("Sun");
    sun->tmprel = Point(0, 0, 1e11);
    Planet *saturn = make_planet(sun, "Saturn");
    saturn->looked_for_maps = true;
    saturn->volumetric_mean_radius = 58232000;
    saturn->ring_radius = 1.3678e8;
    saturn->tmprel = Point(0, 0, -1e8);
    double bloom[2] =
    {
    };
    set(bloomrad_cache, static_cast<double*>(bloom));
    const double angular_size = asin(saturn->volumetric_mean_radius / saturn->tmprel.magnitude());
    EXPECT_EQ(draw_sphere_gpu(saturn, angular_size), 0);
    draw_sphere(saturn, angular_size);
    int callbacks = 0;
    for (const auto &command : ImGui::GetBackgroundDrawList()->CmdBuffer)
    {
        callbacks += command.UserCallback != nullptr && command.UserCallback != ImDrawCallback_ResetRenderState;
    }
    EXPECT_GT(callbacks, 0);
}

TEST_F(RenderingRegressionTest, CumulusInteriorsRemainOpaqueDuringDistanceBlending)
{
    const auto pixels = clouds(153, 0.25, 1.0);
    int opaque = 0;
    for (int i = 0; i < 192 * 108; ++i)
    {
        opaque += pixels[i * 4 + 3] >= 250;
    }
    EXPECT_GT(opaque, 1000);
}

TEST_F(RenderingRegressionTest, CumulusEdgesPreserveAnOpaqueCloudBank)
{
    const auto pixels = clouds(255, 0.7, 1.0);
    // Every ray is above the horizon and the bank is opaque. Soft puff
    // silhouettes must retain that background throughout their transition.
    for (int i = 0; i < 192 * 108; ++i)
    {
        EXPECT_GE(pixels[i * 4 + 3], 254) << "pixel=" << i;
    }
}

TEST_F(RenderingRegressionTest, CloudTwilightKeepsNeutralLightAtStrongAtmosphericTint)
{
    CloudTestView view;
    view.warmth = 5;
    const auto pixels = clouds(153, 0.25, 1.0, view);
    int visible = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        if (pixels[i] < 20) continue;
        ++visible;
        // A warm tint may soften the white, but must not extrapolate the
        // blend into saturated orange or black out the green channel.
        EXPECT_GE(int(pixels[i + 1]), int(pixels[i] * 0.60) - 2);
    }
    EXPECT_GT(visible, 1000);
}

TEST_F(RenderingRegressionTest, DistantCloudClusterOutsideTheLimitLeavesOnlyTheBank)
{
    const auto pixels = clouds(77, 0.03, 0.125);
    // This strip formerly sampled a partial out-of-range cluster at a latitude
    // cell border. Whole-cluster rejection leaves only the unresolved bank.
    for (int x = 36; x <= 42; ++x)
    {
        EXPECT_LT(pixels[(107 * 192 + x) * 4 + 3], 100) << "x=" << x;
    }
}

TEST_F(RenderingRegressionTest, CloudGeometryMatchesTheReferenceAcrossViewsAndSurfaceMotion)
{
    CloudTestView view;
    compare_clouds(153, 0.25, 1.0, view);
    view.azimuth = 1.7;
    compare_clouds(77, 0.03, 0.125, view);
    view.longitude = 0.0008;
    compare_clouds(204, 1.4, 1.0, view);
    view.latitude = 0.8;
    view.longitude = _pi - 0.0001;
    view.patterned = true;
    compare_clouds(200, 0.3, 1.5, view);
    view.longitude = -_pi + 0.0001;
    compare_clouds(200, 0.3, 1.5, view);
    view.latitude = half_pi - 0.0002;
    view.bend = 0.03;
    compare_clouds(180, 0.05, 2.0, view);
    view.latitude = -half_pi + 0.0002;
    compare_clouds(180, 1.4, 1.0, view);
    view.observer = 1003;
    compare_clouds(153, -0.6, 1.0, view);
}

TEST_F(RenderingRegressionTest, CloudGeometrySurvivesLookingUpFromInsideAClusterBoundary)
{
    CloudTestView view;
    const float step = 2.8f / float(view.shell);
    const float clat = -float(half_pi) + (float(int(float(half_pi) / step)) + 0.5f) * step;
    const float cells = std::round(float(2 * _pi) * float(view.shell) * cosf(clat) / 2.8f);
    const float clon = -float(_pi) + (floorf(cells * 0.5f) + 0.5f) * (float(2 * _pi) / cells);
    view.isolated = true;
    view.cloud_latitude = clat;
    view.cloud_longitude = clon;
    // Place the observer beside the new billowed mass while keeping its
    // center behind the upward-facing camera, as asserted below.
    view.latitude = clat + 0.25 / view.shell;
    view.longitude = clon;
    view.observer = 1000.9;
    const double elevation = 1.1;
    const double north = view.shell * sin(clat - view.latitude);
    const double up = view.shell * cos(clat - view.latitude) - view.observer;
    ASSERT_LT(north * cos(elevation) + up * sin(elevation), 0);
    render_width = 640;
    render_height = 360;
    SDL_SetWindowSize(window, render_width, render_height);
    ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
    ImGui::EndFrame();
    ImGui::NewFrame();
    // The map activates a single cluster whose center is behind the camera.
    // Its puff edge must still reach into the upward-facing view.
    compare_clouds(255, elevation, 3.0, view);
    const auto pixels = clouds(255, elevation, 3.0, view);
    int opaque = 0;
    for (size_t i = 0; i < pixels.size() / 4; ++i)
    {
        opaque += pixels[i * 4 + 3] > 250;
    }
    EXPECT_GT(opaque, 100);
    SDL_SetWindowSize(window, 192, 108);
}

TEST_F(RenderingRegressionTest, CloudDistanceLimitAcceptsOrRejectsTheWholeCluster)
{
    CloudTestView view;
    const float step = 2.8f / float(view.shell);
    const float band = floorf(float(half_pi) / step) + 3;
    const float clat = -float(half_pi) + (band + 0.5f) * step;
    const float cells = roundf(float(2 * _pi) * float(view.shell) * cosf(clat) / 2.8f);
    const float clon = -float(_pi) + (floorf(cells * 0.5f) + 0.5f) * (float(2 * _pi) / cells);
    view.isolated = true;
    view.cloud_latitude = clat;
    view.cloud_longitude = clon;
    view.longitude = clon;
    view.latitude = clat - 9.85 / view.shell;
    compare_clouds(255, 0.1, 0.15, view);
    const auto near_pixels = clouds(255, 0.1, 0.15, view);
    int beyond_shell_limit = 0;
    for (int y = 0; y < render_height; ++y)
    {
        const double v = (2.0 * (y + 0.5) / render_height - 1) * 0.15 * render_height / render_width;
        for (int x = 0; x < render_width; ++x)
        {
            const double u = (2.0 * (x + 0.5) / render_width - 1) * 0.15;
            const double ray_y = (v * cos(0.1) + sin(0.1)) / sqrt(1 + u * u + v * v);
            const double along = view.observer * ray_y;
            const double delta = (view.shell - view.observer) * (view.shell + view.observer);
            const double distance = delta / (sqrt(along * along + delta) + along);
            beyond_shell_limit += distance > 10.0 && near_pixels[(y * render_width + x) * 4 + 3] > 250;
        }
    }
    EXPECT_GT(beyond_shell_limit, 100);
    view.latitude = clat - 10.3 / view.shell;
    compare_clouds(255, 0.1, 0.15, view);
    const auto far_pixels = clouds(255, 0.1, 0.15, view);
    int opaque = 0;
    for (size_t i = 0; i < far_pixels.size() / 4; ++i)
    {
        opaque += far_pixels[i * 4 + 3] > 250;
    }
    // The isolated map texel may still appear in the distant bank, but none
    // of the out-of-range cluster's much larger procedural lobes may remain.
    EXPECT_LT(opaque, 100);
}

TEST_F(RenderingRegressionTest, CloudBankMeetsTheApparentGroundHorizonWithRefraction)
{
    CloudTestView view;
    view.overcast = true;
    const double horizons[] =
    {
        -0.0001, 0.06
    };
    for (double horizon : horizons)
    {
        view.horizon_angle = horizon;
        view.bend = 0;
        const auto straight = clouds(255, horizon, 0.125, view);
        view.bend = 0.03;
        const auto refracted = clouds(255, horizon, 0.125, view);
        EXPECT_TRUE(straight == refracted);
        EXPECT_GE(refracted[(57 * render_width + render_width / 2) * 4 + 3], 254);
        EXPECT_EQ(refracted[(50 * render_width + render_width / 2) * 4 + 3], 0);
    }
}

TEST_F(RenderingRegressionTest, CloudGeometryCacheTracksSurfaceTravelAndMapChanges)
{
    CloudTestView view;
    clouds(153, 0.5, 1.0, view);
    const auto initial = cloud_sky_gpu_stats();
    clouds(153, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().image_hits, initial.image_hits + 1);
    view.azimuth = 0.2;
    clouds(153, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().geometry_builds, initial.geometry_builds);
    EXPECT_LT(cloud_sky_gpu_stats().mean_tile_clusters, cloud_sky_gpu_stats().clusters);
    view.longitude = 0.0008;
    clouds(153, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().geometry_builds, initial.geometry_builds);
    view.longitude = 0.01;
    clouds(153, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().geometry_builds, initial.geometry_builds + 1);
    clouds(180, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().geometry_builds, initial.geometry_builds + 2);
    view.wind = 0.001;
    clouds(180, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().geometry_builds, initial.geometry_builds + 3);
    view.shell = 1002;
    clouds(180, 0.5, 1.0, view);
    EXPECT_EQ(cloud_sky_gpu_stats().geometry_builds, initial.geometry_builds + 4);
    if (cloud_sky_gpu_stats().timers_available)
    {
        EXPECT_GT(cloud_sky_gpu_stats().render_ms, 0);
        EXPECT_GT(cloud_sky_gpu_stats().composite_ms, 0);
    }
}

// Run explicitly with --gtest_also_run_disabled_tests for motion measurements.
TEST_F(RenderingRegressionTest, DISABLED_CloudMotionGpuTimings)
{
    render_width = 1280;
    render_height = 720;
    SDL_SetWindowSize(window, render_width, render_height);
    ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
    ImGui::EndFrame();
    ImGui::NewFrame();
    std::cout << "Cloud timing renderer: " << glGetString(GL_RENDERER) << std::endl;
    const CloudGeometryMode modes[] =
    {
        CloudGeometryMode::reference, CloudGeometryMode::accelerated
    };
    const char *labels[] =
    {
        "reference", "cached geometry and tiles"
    };
    const bool motions[] =
    {
        false, true
    };
    for (bool travel : motions)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            CloudTestView view;
            view.mode = modes[mode];
            view.latitude = 0.3;
            double gpu_ms = 0, cpu_ms = 0, wall_ms = 0, candidates = 0;
            constexpr int warmup = 3, samples = 12;
            for (int frame = 0; frame < warmup + samples; ++frame)
            {
                view.longitude = travel ? frame * 0.0001 : 0;
                view.azimuth = travel ? 0 : frame * 0.02;
                const auto start = std::chrono::steady_clock::now();
                clouds(153, 0.5, 1.0, view);
                if (frame >= warmup)
                {
                    const auto &stats = cloud_sky_gpu_stats();
                    gpu_ms += stats.render_ms;
                    cpu_ms += mode == 0 ? 0 : stats.tile_cpu_ms;
                    candidates += mode == 0 ? 0 : stats.mean_tile_clusters;
                    wall_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                }
            }
            std::cout << (travel ? "Surface travel, " : "Panning, ") << labels[mode]
                << ": GPU " << gpu_ms / samples << " ms, tile CPU " << cpu_ms / samples
                << " ms, wall " << wall_ms / samples << " ms, candidates " << candidates / samples << std::endl;
        }
    }
    SDL_SetWindowSize(window, 192, 108);
}

TEST_F(RenderingRegressionTest, DISABLED_HorizonMotionCpuTimings)
{
    surface(true);
    set(dispcx, 960.0);
    set(dispcy, 540.0);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    for (bool travel : {false, true})
    {
        double elapsed = 0;
        uint64_t digest = 1469598103934665603ULL;
        for (int frame = 0; frame < 8; ++frame)
        {
            viewer_lon = 0.4 + (travel ? frame * 0.00001 : 0);
            azimuth = travel ? 0 : frame * 0.02;
            const auto start = std::chrono::steady_clock::now();
            find_horizon();
            draw_horizon();
            elapsed += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const ImDrawList *draw = ImGui::GetBackgroundDrawList();
            const auto hash = [&](const void *data, size_t size)
            {
                const auto *bytes = static_cast<const unsigned char*>(data);
                for (size_t i = 0; i < size; ++i) digest = (digest ^ bytes[i]) * 1099511628211ULL;
            };
            hash(draw->VtxBuffer.Data, draw->VtxBuffer.Size * sizeof(ImDrawVert));
            hash(draw->IdxBuffer.Data, draw->IdxBuffer.Size * sizeof(ImDrawIdx));
            ImGui::EndFrame();
            ImGui::NewFrame();
        }
        std::cout << (travel ? "Travel" : "Pan") << " horizon CPU: " << elapsed / 8
            << " ms; geometry digest: " << digest << std::endl;
    }
}

TEST_F(RenderingRegressionTest, GaseousHorizonIsVisibleAndIndependentOfCompassHeading)
{
    Planet* world = surface();
    world->cloud_map = new SceneryTestMap();
    world->volumetric_mean_radius = 25362000;
    world->mass = 14.536 * earth_mass;
    const auto capture = [&]()
    {
        freeze_surface_time();
        find_horizon();
        draw_horizon();
        auto pixels = render();
        ImGui::NewFrame();
        return pixels;
    };
    for (cel_obj_type type : {ice_giant, gas_giant})
    {
        world->type = type;
        for (double elevation : {-89.0, -60.0, -30.0, -5.0, 0.0, 30.0, 89.0, 90.0})
        {
            altitude = elevation * fiftyseventh;
            azimuth = 0;
            const auto expected = capture();
            if (elevation <= 0)
            {
                // OpenGL readback starts at the bottom row.
                const size_t bottom_center = render_width / 2 * 4;
                EXPECT_GT(expected[bottom_center + 3], 0) << "Elevation " << elevation;
            }
            for (double bearing : {1.0, 45.0, 90.0, 135.0, 180.0, 225.0, 269.9, 270.0, 270.1, 315.0, 359.0})
            {
                azimuth = bearing * fiftyseventh;
                const auto actual = capture();
                EXPECT_EQ(image_difference(actual, expected), 0)
                    << "Type " << static_cast<int>(type) << ", bearing " << bearing << ", elevation " << elevation;
            }
        }
    }
}

TEST_F(RenderingRegressionTest, GaseousHorizonCoversNadirAndLeavesZenithClear)
{
    Planet* world = surface();
    world->type = ice_giant;
    world->volumetric_mean_radius = 25362000;
    world->cloud_map = new SceneryTestMap();
    for (double elevation : {-90.0, -89.9, 89.9, 90.0})
    {
        altitude = elevation * fiftyseventh;
        azimuth = 270 * fiftyseventh;
        find_horizon();
        draw_horizon();
        const auto pixels = render();
        ImGui::NewFrame();
        for (size_t i = 3; i < pixels.size(); i += 4)
            EXPECT_EQ(pixels[i], elevation < 0 ? 255 : 0) << "Elevation " << elevation << ", pixel " << i / 4;
    }
}

TEST_F(RenderingRegressionTest, GpuGroundMatchesReferenceDuringTravelAndPanning)
{
    #if !horizon_gpu_ground_painting
    GTEST_SKIP() << "GPU ground painting is disabled in visuals.h.";
    #endif
    surface(true);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    const auto capture = [&](bool reference)
    {
        horizon_ground_gpu_reference = reference;
        freeze_surface_time();
        find_horizon();
        draw_horizon();
        auto pixels = render();
        ImGui::NewFrame();
        return pixels;
    };
    for (int frame = 0; frame < 8; ++frame)
    {
        set(viewer_lon, 0.4 + frame * 0.00001);
        set(azimuth, frame * 0.2);
        set(redlight_mode, frame % 3 == 1);
        set(dragging, frame % 3 == 2);
        const auto expected = capture(true);
        const auto actual = capture(false);
        EXPECT_EQ(image_difference(actual, expected), 0) << "Frame " << frame;
        EXPECT_GT(horizon_ground_gpu_stats().tiles, 0u);
        EXPECT_GT(horizon_ground_gpu_stats().pixels, 0u);
    }
}

TEST_F(RenderingRegressionTest, GpuGroundPreservesCoastsDenseAirSeamsAndPoles)
{
    #if !horizon_gpu_ground_painting
    GTEST_SKIP() << "GPU ground painting is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new CoastalTestMap();
    world->vegetation_r = world->vegetation_g = world->vegetation_b = 0;
    for (const auto &position : {std::make_pair(-0.0001, 0.4), std::make_pair(0.0001, 0.4),
        std::make_pair(0.3, _pi - 0.0001), std::make_pair(-0.3, -_pi + 0.0001),
        std::make_pair(half_pi - 0.0001, 0.4), std::make_pair(-half_pi + 0.0001, 0.4)})
    {
        set(viewer_lat, position.first);
        set(viewer_lon, position.second);
        for (double pressure : {oneatm, 15 * oneatm})
        {
            world->ensure_atmosphere()->surface_pressure = pressure;
            std::vector<unsigned char> expected;
            for (bool reference : {true, false})
            {
                horizon_ground_gpu_reference = reference;
                freeze_surface_time();
                find_horizon();
                draw_horizon();
                const auto pixels = render();
                if (reference) expected = pixels;
                else EXPECT_EQ(image_difference(pixels, expected), 0) << position.first << ", " << position.second << ", " << pressure;
                ImGui::NewFrame();
            }
        }
    }
}

TEST_F(RenderingRegressionTest, GpuGroundPreservesChangingMapsAndViewsWithoutTerrain)
{
    #if !horizon_gpu_ground_painting
    GTEST_SKIP() << "GPU ground painting is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    for (int frame = 0; frame < 6; ++frame)
    {
        delete world->surf_map;
        world->surf_map = new SceneryTestMap(frame % 2, frame % 3 == 1);
        set(show_terrain, frame % 2 == 0);
        set(altitude, frame < 3 ? -0.15 : -1.4);
        std::vector<unsigned char> expected;
        for (bool reference : {true, false})
        {
            horizon_ground_gpu_reference = reference;
            freeze_surface_time();
            find_horizon();
            draw_horizon();
            const auto pixels = render();
            if (reference) expected = pixels;
            else EXPECT_EQ(image_difference(pixels, expected), 0) << "Frame " << frame;
            ImGui::NewFrame();
        }
    }
}

TEST_F(RenderingRegressionTest, GpuGroundMatchesAtFullResolutionAndAfterResizing)
{
    #if !horizon_gpu_ground_painting
    GTEST_SKIP() << "GPU ground painting is disabled in visuals.h.";
    #endif
    surface(true);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    for (const auto &size : {std::make_pair(1920, 1080), std::make_pair(801, 451),
        std::make_pair(384, 216), std::make_pair(192, 108)})
    {
        render_width = size.first;
        render_height = size.second;
        SDL_SetWindowSize(window, render_width, render_height);
        SDL_GL_SwapWindow(window);
        const float scale = render_width == 384 ? 2.0f : 1.0f;
        set(dispcx, render_width * 0.5 / scale);
        set(dispcy, render_height * 0.5 / scale);
        set(ImGui::GetIO().DisplayFramebufferScale, ImVec2(scale, scale));
        ImGui::GetIO().DisplaySize = ImVec2(render_width / scale, render_height / scale);
        ImGui::EndFrame();
        ImGui::NewFrame();
        std::vector<unsigned char> expected;
        for (bool reference : {true, false})
        {
            horizon_ground_gpu_reference = reference;
            freeze_surface_time();
            find_horizon();
            draw_horizon();
            const auto pixels = render();
            if (reference) expected = pixels;
            else
            {
                EXPECT_EQ(image_difference(pixels, expected), 0) << render_width << " x " << render_height;
                EXPECT_GT(horizon_ground_gpu_stats().tiles, 0u);
            }
            ImGui::NewFrame();
        }
    }
}

TEST_F(RenderingRegressionTest, GpuTerrainDepthMatchesReferenceAtEdgesAndDepthTies)
{
    set(horizon_terrain_gpu_reference, false);
    std::vector<ProjectedTerrainVertex> triangles =
    {
        {0, 0, 0.01, 10}, {32, 0, 0.02, 11}, {0, 32, 0.005, 9},
        {32, 0, 0.02, 11}, {32, 32, 0.015, 12}, {0, 32, 0.005, 9},
        {4.5, 4.5, 0.02, 10}, {28.5, 4.5, 0.02, 10}, {4.5, 28.5, 0.02, 10},
        {4.5, 4.5, 0.02000000001, 10}, {28.5, 4.5, 0.02000000001, 10}, {4.5, 28.5, 0.02000000001, 10},
        {-1000000, 0, 0.001, 100}, {1000000, 0, 0.002, 200}, {0, 32, 0.003, 300},
        {0, 0, 0, 0}, {32, 0, 0.1, 10}, {0, 32, 0.1, 10},
        {0, 0, 0.1, 10}, {1, 1, 0.1, 10}, {2, 2, 0.1, 10}
    };
    HorizonTerrainRaster expected, actual;
    expected.reset(32, 32);
    actual.reset(32, 32);
    for (size_t index = 0; index < triangles.size(); index += 3)
        expected.triangle(triangles[index], triangles[index + 1], triangles[index + 2]);
    ASSERT_TRUE(rasterize_horizon_terrain_gpu(actual, triangles, 32, 32));
    EXPECT_EQ(actual.distances(), expected.distances());
    for (int x = 0; x < 32; ++x) EXPECT_EQ(actual.top_at(x), expected.top_at(x));
    EXPECT_TRUE(horizon_terrain_gpu_stats().accelerated);
    EXPECT_GT(horizon_terrain_gpu_stats().repaired_pixels, 0u);
    HorizonTerrainRaster empty;
    empty.reset(32, 32);
    ASSERT_TRUE(rasterize_horizon_terrain_gpu(empty, {}, 32, 32));
    EXPECT_EQ(empty.top_at(0), 32);
    EXPECT_FALSE(std::isfinite(empty.distance_at(0.5, 0.5)));
}

TEST_F(RenderingRegressionTest, GpuTerrainPreservesImagesAcrossViewsCoastsAndMapChanges)
{
    #if !horizon_gpu_terrain_rasterization
    GTEST_SKIP() << "GPU terrain rasterization is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    for (int scene = 0; scene < 6; ++scene)
    {
        delete world->surf_map;
        if (scene == 1 || scene == 2) world->surf_map = new CoastalTestMap();
        else if (scene == 3) world->surf_map = new MountainBandsTestMap();
        else world->surf_map = new SceneryTestMap(true);
        world->ensure_atmosphere()->surface_pressure = (scene == 2 ? 15 : 1) * oneatm;
        set(viewer_lat, scene == 1 || scene == 2 || scene == 3 ? -0.0001 : 0.3);
        set(viewer_lon, scene == 4 ? _pi - 0.0001 : 0.4);
        set(altitude, scene == 5 ? -1.4 : -0.15);
        set(redlight_mode, scene == 4);
        set(dragging, scene == 5);
        for (bool ground_reference : {true, false})
        {
            horizon_ground_gpu_reference = ground_reference;
            for (int frame = 0; frame < 3; ++frame)
            {
                set(azimuth, frame * 0.4);
                set(viewer_lon, viewer_lon + 0.00001);
                std::vector<unsigned char> expected;
                for (bool reference : {true, false})
                {
                    horizon_terrain_gpu_reference = reference;
                    freeze_surface_time();
                    find_horizon();
                    draw_horizon();
                    const auto pixels = render();
                    if (reference) expected = pixels;
                    else
                    {
                        EXPECT_EQ(image_difference(pixels, expected), 0) << "Scene " << scene << ", frame " << frame;
                        EXPECT_TRUE(horizon_terrain_gpu_stats().accelerated);
                    }
                    ImGui::NewFrame();
                }
            }
        }
    }
}

TEST_F(RenderingRegressionTest, GpuTerrainDepthMatchesDenseOverlappingTriangles)
{
    set(horizon_terrain_gpu_reference, false);
    uint32_t sequence = 987654321u;
    const auto random = [&]()
    {
        sequence = sequence * 1664525u + 1013904223u;
        return static_cast<double>(sequence >> 8) / 16777216.0;
    };
    for (int frame = 0; frame < 4; ++frame)
    {
        std::vector<ProjectedTerrainVertex> triangles;
        for (int vertex = 0; vertex < 300; ++vertex)
        {
            const double x = random() * 192 - 64, y = random() * 144 - 48;
            const double inverse = 0.001 + random() * 0.01;
            triangles.push_back({x, y, inverse, inverse * (1000 + random() * 10000)});
        }
        HorizonTerrainRaster expected, actual;
        expected.reset(64, 48);
        actual.reset(64, 48);
        for (size_t index = 0; index < triangles.size(); index += 3)
        {
            expected.triangle(triangles[index], triangles[index + 1], triangles[index + 2]);
        }
        GLint previous_viewport[4];
        glGetIntegerv(GL_VIEWPORT, previous_viewport);
        glViewport(2, 3, 17, 19);
        ASSERT_TRUE(rasterize_horizon_terrain_gpu(actual, triangles, 64, 48));
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        EXPECT_EQ(viewport[0], 2);
        EXPECT_EQ(viewport[1], 3);
        EXPECT_EQ(viewport[2], 17);
        EXPECT_EQ(viewport[3], 19);
        glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
        EXPECT_EQ(actual.distances(), expected.distances()) << "Frame " << frame;
        for (int x = 0; x < 64; ++x) EXPECT_EQ(actual.top_at(x), expected.top_at(x));
    }
}

TEST_F(RenderingRegressionTest, GpuTerrainPreservesFullResolutionResizeAndDisplayScaling)
{
    #if !horizon_gpu_terrain_rasterization
    GTEST_SKIP() << "GPU terrain rasterization is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new CoastalTestMap();
    world->ensure_atmosphere()->surface_pressure = 15 * oneatm;
    set(viewer_lat, -0.0001);
    set(viewer_lon, 0.4);
    for (const auto &size : {std::make_pair(1920, 1080), std::make_pair(801, 451),
        std::make_pair(384, 216), std::make_pair(192, 108)})
    {
        render_width = size.first;
        render_height = size.second;
        SDL_SetWindowSize(window, render_width, render_height);
        SDL_GL_SwapWindow(window);
        const float scale = render_width == 384 ? 2.0f : 1.0f;
        set(dispcx, render_width * 0.5 / scale);
        set(dispcy, render_height * 0.5 / scale);
        set(ImGui::GetIO().DisplayFramebufferScale, ImVec2(scale, scale));
        ImGui::GetIO().DisplaySize = ImVec2(render_width / scale, render_height / scale);
        ImGui::EndFrame();
        ImGui::NewFrame();
        for (bool ground_reference : {true, false})
        {
            horizon_ground_gpu_reference = ground_reference;
            std::vector<unsigned char> expected;
            for (bool reference : {true, false})
            {
                horizon_terrain_gpu_reference = reference;
                freeze_surface_time();
                find_horizon();
                draw_horizon();
                const auto pixels = render();
                if (reference) expected = pixels;
                else
                {
                    EXPECT_EQ(image_difference(pixels, expected), 0) << render_width << " x " << render_height;
                    EXPECT_TRUE(horizon_terrain_gpu_stats().accelerated);
                }
                ImGui::NewFrame();
            }
        }
    }
}

TEST_F(RenderingRegressionTest, GpuMeshNoiseMatchesCpuAcrossWorldsAndCellBoundaries)
{
    set(horizon_mesh_gpu_reference, false);
    std::vector<std::array<double, 3>> positions =
    {
        {0, 0, 0}, {127.123456789, 0.23456789, -3.987654321},
        {-127.123456789, -0.23456789, 3.987654321},
        {1.0 - 1e-10, 2.0 + 1e-10, -3.0 - 1e-10}, {1.0 + 1e-10, -2.0 - 1e-10, 3.0 + 1e-10}
    };
    for (int index = 0; index < 1024; ++index)
    {
        positions.push_back({127.0 * sin(index * 0.13), 127.0 * cos(index * 0.07), 127.0 * sin(index * 0.11)});
    }
    for (uint32_t seed : {0u, 42u, 123456789u, 0xffffffffu})
    {
        PerlinNoise noise(seed);
        std::vector<double> totals;
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(generate_horizon_mesh_gpu(positions, noise, totals));
        ASSERT_EQ(totals.size(), positions.size());
        double maximum_error = 0;
        for (size_t index = 0; index < positions.size(); ++index)
        {
            const auto &point = positions[index];
            double expected = 0, amplitude = 1, frequency = 1;
            for (int octave = 0; octave < 12; ++octave)
            {
                expected += noise.noise(point[0] * frequency, point[1] * frequency, point[2] * frequency) * amplitude;
                amplitude *= 0.8559;
                frequency *= 1.29;
            }
            maximum_error = fmax(maximum_error, fabs(totals[index] - expected));
        }
        std::cout << "Mesh seed " << seed << ": maximum noise error " << maximum_error
            << "; initialization " << horizon_mesh_gpu_stats().initialization_ms
            << " ms; generation " << horizon_mesh_gpu_stats().generation_ms << " ms; total "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
            << " ms" << std::endl;
        EXPECT_LT(maximum_error, 5e-10);
    }
}

TEST_F(RenderingRegressionTest, GpuMeshReferenceDoesNotInitializeTheShader)
{
    clear_horizon_mesh_gpu();
    set(horizon_mesh_gpu_reference, true);
    PerlinNoise noise(42);
    const std::vector<std::array<double, 3>> positions = {{127.123456789, 0.4, -2.7}};
    std::vector<double> totals;
    EXPECT_FALSE(generate_horizon_mesh_gpu(positions, noise, totals));
    EXPECT_FALSE(horizon_mesh_gpu_stats().accelerated);
    EXPECT_EQ(horizon_mesh_gpu_stats().initialization_ms, 0);
    horizon_mesh_gpu_reference = false;
    ASSERT_TRUE(generate_horizon_mesh_gpu(positions, noise, totals));
    EXPECT_TRUE(horizon_mesh_gpu_stats().cold);
    ASSERT_EQ(totals.size(), 1u);
}

TEST_F(RenderingRegressionTest, GpuMeshPreservesCoastsMountainBandsSeamsAndPoles)
{
    #if !horizon_gpu_mesh_generation
    GTEST_SKIP() << "GPU mesh generation is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    for (int scene = 0; scene < 7; ++scene)
    {
        delete world->surf_map;
        if (scene == 1 || scene == 2) world->surf_map = new CoastalTestMap();
        else if (scene == 3) world->surf_map = new MountainBandsTestMap();
        else if (scene == 4) world->surf_map = new BumpySceneryTestMap();
        else world->surf_map = new SceneryTestMap(true);
        world->ensure_atmosphere()->surface_pressure = (scene == 2 ? 15 : 1) * oneatm;
        set(viewer_lat, scene == 1 || scene == 2 || scene == 3 ? -0.0001 : (scene == 5 ? half_pi - 0.0001 : 0.3));
        set(viewer_lon, scene == 4 ? _pi - 0.0001 : 0.4);
        set(altitude, scene == 6 ? -1.4 : -0.15);
        set(redlight_mode, scene == 4);
        set(dragging, scene == 6);
        for (bool ground_reference : {true, false})
        {
            horizon_ground_gpu_reference = ground_reference;
            for (int frame = 0; frame < 3; ++frame)
            {
                if (scene == 5) set(viewer_lat, frame % 2 ? half_pi - 0.0001 : -half_pi + 0.0001);
                set(azimuth, frame * 0.4);
                set(viewer_lon, viewer_lon + 0.00001);
                const auto expected = mesh_image(true);
                const auto actual = mesh_image(false);
                EXPECT_EQ(image_difference(actual, expected), 0) << "Scene " << scene << ", frame " << frame;
                EXPECT_TRUE(horizon_mesh_gpu_stats().accelerated);
                EXPECT_EQ(horizon_mesh_gpu_stats().samples, 20480u);
            }
        }
    }
}

TEST_F(RenderingRegressionTest, GpuMeshCacheSurvivesPanningAndTracksTravelAndMapChanges)
{
    #if !horizon_gpu_mesh_generation
    GTEST_SKIP() << "GPU mesh generation is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    set(horizon_mesh_gpu_reference, false);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    scenery();
    const unsigned int first = horizon_mesh_gpu_stats().builds;
    ASSERT_TRUE(horizon_mesh_gpu_stats().accelerated);
    set(azimuth, 0.2);
    scenery();
    EXPECT_EQ(horizon_mesh_gpu_stats().builds, first);
    set(viewer_lon, 0.40001);
    scenery();
    EXPECT_EQ(horizon_mesh_gpu_stats().builds, first + 1);
    world->surf_map->touch_gen();
    scenery();
    EXPECT_EQ(horizon_mesh_gpu_stats().builds, first + 2);
    horizon_mesh_gpu_reference = true;
    scenery();
    horizon_mesh_gpu_reference = false;
    scenery();
    EXPECT_EQ(horizon_mesh_gpu_stats().builds, first + 3);
}

TEST_F(RenderingRegressionTest, GpuMeshPreservesFullResolutionResizeAndDisplayScaling)
{
    #if !horizon_gpu_mesh_generation
    GTEST_SKIP() << "GPU mesh generation is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new CoastalTestMap();
    world->ensure_atmosphere()->surface_pressure = 15 * oneatm;
    set(viewer_lat, -0.0001);
    set(viewer_lon, 0.4);
    for (const auto &size : {std::make_pair(1920, 1080), std::make_pair(801, 451),
        std::make_pair(384, 216), std::make_pair(192, 108)})
    {
        render_width = size.first;
        render_height = size.second;
        SDL_SetWindowSize(window, render_width, render_height);
        SDL_GL_SwapWindow(window);
        const float scale = render_width == 384 ? 2.0f : 1.0f;
        set(dispcx, render_width * 0.5 / scale);
        set(dispcy, render_height * 0.5 / scale);
        set(ImGui::GetIO().DisplayFramebufferScale, ImVec2(scale, scale));
        ImGui::GetIO().DisplaySize = ImVec2(render_width / scale, render_height / scale);
        ImGui::EndFrame();
        ImGui::NewFrame();
        for (bool ground_reference : {true, false})
        {
            horizon_ground_gpu_reference = ground_reference;
            const auto expected = mesh_image(true);
            const auto actual = mesh_image(false);
            EXPECT_EQ(image_difference(actual, expected), 0) << render_width << " x " << render_height;
            EXPECT_TRUE(horizon_mesh_gpu_stats().accelerated);
        }
    }
}

TEST_F(RenderingRegressionTest, GpuMeshPreservesReliefAcrossPlanetSizesAndEyeHeights)
{
    #if !horizon_gpu_mesh_generation
    GTEST_SKIP() << "GPU mesh generation is disabled in visuals.h.";
    #endif
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new BumpySceneryTestMap();
    set(horizon_ground_gpu_reference, false);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    for (double radius : {earth_radius * 0.2, earth_radius, earth_radius * 3.0})
    {
        world->volumetric_mean_radius = radius;
        for (double eye : {0.1, 1.75, 100.0})
        {
            set(viewer_eye_height, eye);
            const auto expected = mesh_image(true);
            const auto actual = mesh_image(false);
            EXPECT_EQ(image_difference(actual, expected), 0) << "Radius " << radius << ", eye " << eye;
            EXPECT_TRUE(horizon_mesh_gpu_stats().accelerated);
        }
    }
}

TEST_F(RenderingRegressionTest, DISABLED_HorizonMeshGpuTimings)
{
    surface(true);
    render_width = 1920;
    render_height = 1080;
    SDL_SetWindowSize(window, render_width, render_height);
    SDL_GL_SwapWindow(window);
    set(dispcx, 960.0);
    set(dispcy, 540.0);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    set(horizon_ground_gpu_reference, false);
    set(horizon_mesh_gpu_reference, false);
    ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
    ImGui::EndFrame();
    ImGui::NewFrame();
    clear_horizon_mesh_gpu();
    const auto cold_start = std::chrono::steady_clock::now();
    find_horizon();
    draw_horizon();
    render();
    std::cout << "Cold mesh initialization " << horizon_mesh_gpu_stats().initialization_ms
        << " ms; noise generation " << horizon_mesh_gpu_stats().generation_ms << " ms; complete cold frame "
        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cold_start).count()
        << " ms" << std::endl;
    ImGui::NewFrame();
    for (bool travel : {false, true})
    {
        for (bool reference : {true, false})
        {
            double cpu_ms = 0, frame_ms = 0, noise_ms = 0;
            for (int frame = 0; frame < 10; ++frame)
            {
                horizon_mesh_gpu_reference = reference;
                viewer_lon = 0.4 + (travel ? frame * 0.00001 : 0);
                azimuth = travel ? 0 : frame * 0.02;
                const unsigned int previous = horizon_mesh_gpu_stats().builds;
                const auto start = std::chrono::steady_clock::now();
                find_horizon();
                draw_horizon();
                const auto prepared = std::chrono::steady_clock::now();
                render();
                const auto finished = std::chrono::steady_clock::now();
                if (frame >= 2)
                {
                    cpu_ms += std::chrono::duration<double, std::milli>(prepared - start).count();
                    frame_ms += std::chrono::duration<double, std::milli>(finished - start).count();
                    if (!reference && horizon_mesh_gpu_stats().builds != previous)
                    {
                        noise_ms += horizon_mesh_gpu_stats().generation_ms;
                    }
                }
                ImGui::NewFrame();
            }
            std::cout << (travel ? "Travel" : "Pan") << (reference ? " CPU mesh" : " GPU mesh")
                << ": preparation " << cpu_ms / 8 << " ms; complete frame with readback " << frame_ms / 8
                << " ms; GPU noise generation " << noise_ms / 8 << " ms" << std::endl;
        }
    }
    SDL_SetWindowSize(window, 192, 108);
    SDL_GL_SwapWindow(window);
}

TEST_F(RenderingRegressionTest, DISABLED_HorizonTerrainGpuTimings)
{
    surface(true);
    render_width = 1920;
    render_height = 1080;
    SDL_SetWindowSize(window, render_width, render_height);
    SDL_GL_SwapWindow(window);
    set(dispcx, 960.0);
    set(dispcy, 540.0);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    set(horizon_ground_gpu_reference, false);
    ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
    for (bool travel : {false, true})
    {
        for (bool reference : {true, false})
        {
            double cpu_ms = 0, frame_ms = 0, setup_ms = 0, select_ms = 0, reconstruct_ms = 0;
            unsigned int pixels = 0, repaired = 0, candidates = 0;
            for (int frame = 0; frame < 10; ++frame)
            {
                horizon_terrain_gpu_reference = reference;
                viewer_lon = 0.4 + (travel ? frame * 0.00001 : 0);
                azimuth = travel ? 0 : frame * 0.02;
                const auto start = std::chrono::steady_clock::now();
                find_horizon();
                draw_horizon();
                const auto prepared = std::chrono::steady_clock::now();
                render();
                const auto finished = std::chrono::steady_clock::now();
                if (frame >= 2)
                {
                    cpu_ms += std::chrono::duration<double, std::milli>(prepared - start).count();
                    frame_ms += std::chrono::duration<double, std::milli>(finished - start).count();
                    if (!reference)
                    {
                        const auto &stats = horizon_terrain_gpu_stats();
                        setup_ms += stats.preparation_ms;
                        select_ms += stats.selection_ms;
                        reconstruct_ms += stats.reconstruction_ms;
                        pixels += stats.pixels;
                        repaired += stats.repaired_pixels;
                        candidates = std::max(candidates, stats.max_candidates);
                    }
                }
                ImGui::NewFrame();
            }
            std::cout << (travel ? "Travel" : "Pan") << (reference ? " CPU raster" : " GPU raster")
                << ": preparation " << cpu_ms / 8 << " ms; complete frame with readback " << frame_ms / 8
                << " ms; raster setup/select/reconstruct " << setup_ms / 8 << "/" << select_ms / 8 << "/"
                << reconstruct_ms / 8 << " ms; pixels " << pixels / 8 << "; repaired " << repaired / 8
                << "; max candidates " << candidates << std::endl;
        }
    }
    SDL_SetWindowSize(window, 192, 108);
    SDL_GL_SwapWindow(window);
}

TEST_F(RenderingRegressionTest, DISABLED_HorizonGroundGpuTimings)
{
    surface(true);
    render_width = 1920;
    render_height = 1080;
    SDL_SetWindowSize(window, render_width, render_height);
    SDL_GL_SwapWindow(window);
    set(dispcx, 960.0);
    set(dispcy, 540.0);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.4);
    ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
    for (bool travel : {false, true})
    {
        for (bool reference : {true, false})
        {
            double cpu_ms = 0, frame_ms = 0;
            unsigned int tiles = 0;
            for (int frame = 0; frame < 10; ++frame)
            {
                horizon_ground_gpu_reference = reference;
                viewer_lon = 0.4 + (travel ? frame * 0.00001 : 0);
                azimuth = travel ? 0 : frame * 0.02;
                const auto start = std::chrono::steady_clock::now();
                find_horizon();
                draw_horizon();
                const auto prepared = std::chrono::steady_clock::now();
                render();
                const auto finished = std::chrono::steady_clock::now();
                if (frame >= 2)
                {
                    cpu_ms += std::chrono::duration<double, std::milli>(prepared - start).count();
                    frame_ms += std::chrono::duration<double, std::milli>(finished - start).count();
                    if (!reference) tiles += horizon_ground_gpu_stats().tiles;
                }
                ImGui::NewFrame();
            }
            std::cout << (travel ? "Travel" : "Pan") << (reference ? " CPU" : " GPU ground")
                << ": preparation " << cpu_ms / 8 << " ms; complete frame with readback "
                << frame_ms / 8 << " ms; GPU tiles " << tiles / 8 << std::endl;
        }
    }
    SDL_SetWindowSize(window, 192, 108);
    SDL_GL_SwapWindow(window);
}

TEST_F(RenderingRegressionTest, RockFacesStayOpaqueAtEveryVisibleDistance)
{
    surface();
    cels[1]->location.local_position = cels[1]->tmprel = Point(0, -1e10, 1e10);
    const auto vertices = scenery();
    ASSERT_GT(vertices.size(), 100u);
    for (const auto &vertex : vertices)
    {
        const unsigned int alpha = vertex.col >> IM_COL32_A_SHIFT;
        EXPECT_TRUE(alpha == 0 || alpha == 255);
    }
}

TEST_F(RenderingRegressionTest, SceneryShadowsFollowTheHostInsteadOfTheSystemPrimary)
{
    surface();
    const auto before = scenery();
    cels[0]->location.local_position = cels[0]->tmprel = Point(-1e11, -1e11, 1e11);
    const auto after = scenery();
    ASSERT_EQ(before.size(), after.size());
    ASSERT_GT(before.size(), 100u);
    for (size_t index = 0; index < before.size(); ++index)
    {
        EXPECT_EQ(before[index].pos.x, after[index].pos.x);
        EXPECT_EQ(before[index].pos.y, after[index].pos.y);
        EXPECT_EQ(before[index].col, after[index].col);
    }
}

TEST_F(RenderingRegressionTest, WalkingAcrossVegetationBordersKeepsBothGroundTypesVisible)
{
    Planet *world = surface(true);
    set(altitude, -0.4);
    const double radius = world->get_equatorial_radius();
    const double border = biome_border(world);
    for (int side = -1; side <= 1; side += 2)
    {
        viewer_lon = border + side * 0.05 / radius;
        find_horizon();
        draw_horizon();
        const auto pixels = render();
        int vegetated = 0, bare = 0;
        for (size_t index = 0; index < pixels.size(); index += 4)
        {
            if (pixels[index + 3] != 255)
            {
                continue;
            }
            vegetated += pixels[index] == 80 && pixels[index + 1] == 140 && pixels[index + 2] == 30;
            bare += pixels[index] == 160 && pixels[index + 1] == 120 && pixels[index + 2] == 80;
        }
        EXPECT_GT(vegetated, 0) << "Viewer offset: " << side * 0.05 << " m";
        EXPECT_GT(bare, 0) << "Viewer offset: " << side * 0.05 << " m";
        ImGui::NewFrame();
    }
}

TEST_F(RenderingRegressionTest, ShorelineShowsLandAndShimmeringSeaFromBothSides)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new SceneryTestMap(true, true);
    set(show_terrain, false);
    set(altitude, -0.4);
    const double radius = world->get_equatorial_radius();
    const double coast = biome_border(world);
    for (double offset : {-0.05, 0.05})
    {
        viewer_lon = coast + offset / radius;
        find_horizon();
        draw_horizon();
        int crests = 0;
        for (const auto &vertex : ImGui::GetBackgroundDrawList()->VtxBuffer)
        {
            const int alpha = vertex.col >> IM_COL32_A_SHIFT;
            if (alpha > 0 && alpha < 255)
            {
                ++crests;
            }
        }
        EXPECT_GT(crests, 20);
        const auto pixels = render();
        int sea = 0, land = 0;
        for (int y = 0; y < render_height / 3; ++y)
        {
            for (int x = 0; x < render_width; ++x)
            {
                const int index = (y * render_width + x) * 4;
                if (x < render_width / 3 && pixels[index] == 20
                    && pixels[index + 1] == 100 && pixels[index + 2] == 160)
                {
                    ++sea;
                }
                if (x > render_width * 2 / 3 && pixels[index] == 80
                    && pixels[index + 1] == 140 && pixels[index + 2] == 30)
                {
                    ++land;
                }
            }
        }
        EXPECT_GT(sea, 100);
        EXPECT_GT(land, 100);
        ImGui::NewFrame();
    }
}

TEST_F(RenderingRegressionTest, WaterHasNoRocksOrPlants)
{
    Planet *world = surface(true);
    cels[0]->location.local_position = cels[0]->tmprel = Point(0, -1e11, 1e11);
    delete world->surf_map;
    world->surf_map = new SceneryTestMap(false, true);
    const auto vertices = scenery();
    ASSERT_GT(vertices.size(), 100u);
    int scenery_vertices = 0;
    for (const auto &vertex : vertices)
    {
        const unsigned int alpha = vertex.col >> IM_COL32_A_SHIFT;
        if (alpha == 255)
        {
            scenery_vertices += vertex.col != IM_COL32(20, 100, 160, 255);
        }
    }
    EXPECT_EQ(scenery_vertices, 0);
}

TEST_F(RenderingRegressionTest, BlueVegetationDoesNotTurnIdenticalOceanColorsIntoPlants)
{
    Planet *world = surface(true);
    world->vegetation_r = 20;
    world->vegetation_g = 100;
    world->vegetation_b = 160;
    cels[0]->location.local_position = cels[0]->tmprel = Point(0, -1e11, 1e11);
    for (SurfaceMaterial material : {SurfaceMaterial::water, SurfaceMaterial::vegetation})
    {
        delete world->surf_map;
        world->surf_map = new BlueBiomeTestMap(material);
        const auto vertices = scenery();
        ASSERT_GT(vertices.size(), 100u);
        int scenery_vertices = 0;
        for (const auto &vertex : vertices)
        {
            const unsigned int alpha = vertex.col >> IM_COL32_A_SHIFT;
            if (alpha == 255 && vertex.col != IM_COL32(20, 100, 160, 255)) ++scenery_vertices;
        }
        if (material == SurfaceMaterial::water) EXPECT_EQ(scenery_vertices, 0);
        else EXPECT_GT(scenery_vertices, 0);
    }
}

TEST_F(RenderingRegressionTest, ConstellationLabelsIgnoreOffscreenPerimeterVertices)
{
    Star *observer = make_star("Observer");
    make_star("Second star");
    set(mycenobj, static_cast<CelestialObject*>(observer));
    set(here, CelestialLocation());
    set(whereami, -1);
    set(show_labels, true);
    set(show_consln, false);
    set(show_axes, false);
    set(shortnames, false);
    set(labels2draw, std::vector<DrawLabel>());
    set(constellations, std::vector<Constellation>(1));
    Constellation &group = constellations[0];
    group.name = "Test Region";
    group.abbrev = "Tst";
    group.vantage_resolved = true;
    group.lines.emplace_back();
    for (const Point &point : {Point(-2, -.1, 1), Point(2, .1, 1), Point(-.1, -2, 1), Point(.1, 2, 1)})
    {
        ConsBoundary boundary;
        boundary.RA = atan2(-point.x, point.z);
        boundary.decl = atan2(point.y, hypot(point.x, point.z));
        group.bounds.push_back(boundary);
    }
    for (int order = 0; order < 4; ++order)
    {
        for (double yaw : {-.1, 0.0, .1})
        {
            azimuth = yaw;
            labels2draw.clear();
            draw_cons_lines();
            ASSERT_EQ(labels2draw.size(), 1u);
            EXPECT_EQ(labels2draw[0].text, group.name);
            EXPECT_GT(labels2draw[0].pos.x, 0);
            EXPECT_GT(labels2draw[0].pos.y, 0);
        }
        std::rotate(group.bounds.begin(), group.bounds.begin() + 1, group.bounds.end());
    }
    group.lines.clear();
    labels2draw.clear();
    draw_cons_lines();
    EXPECT_EQ(labels2draw.size(), 1u);
}

TEST_F(RenderingRegressionTest, ConstellationLabelRectanglesRetainPartialEdgesAndRejectDistantAnchors)
{
    Star *observer = make_star("Observer");
    make_star("Second star");
    set(mycenobj, static_cast<CelestialObject*>(observer));
    set(here, CelestialLocation());
    set(whereami, -1);
    set(show_labels, true);
    set(show_consln, false);
    set(show_axes, false);
    set(shortnames, false);
    set(labels2draw, std::vector<DrawLabel>());
    set(constellations, std::vector<Constellation>(1));
    auto &group = constellations[0];
    group.name = "Test Region";
    group.vantage_resolved = true;
    for (const ImVec2 center : {ImVec2(1, 54), ImVec2(96, 1), ImVec2(191, 54), ImVec2(96, 107),
        ImVec2(-10000, 54), ImVec2(10000, 54), ImVec2(96, -10000), ImVec2(96, 10000)})
    {
        const Point point((center.x - dispcx) / dispcx, (dispcy - center.y) / dispcx, 1);
        ConsBoundary boundary;
        boundary.RA = atan2(-point.x, point.z);
        boundary.decl = atan2(point.y, hypot(point.x, point.z));
        group.bounds = {boundary};
        labels2draw.clear();
        draw_cons_lines();
        const bool visible = center.x > 0 && center.x < 192 && center.y > 0 && center.y < 108;
        EXPECT_EQ(labels2draw.size(), visible ? 1u : 0u);
    }
    group.bounds[0].RA = _pi;
    group.bounds[0].decl = 0;
    labels2draw.clear();
    draw_cons_lines();
    EXPECT_TRUE(labels2draw.empty());
}

TEST_F(RenderingRegressionTest, CachedLabelsClipTheirFullTextBounds)
{
    set(labels2draw, std::vector<DrawLabel>());
    for (const ImVec2 position : {ImVec2(-10, 40), ImVec2(80, -3), ImVec2(80, 40),
        ImVec2(-1e20f, 40), ImVec2(1e20f, 40), ImVec2(80, -1e20f), ImVec2(80, 1e20f), ImVec2(NAN, 40)})
    {
        DrawLabel label;
        label.font = nullptr;
        label.fontsz = ImGui::GetFontSize();
        label.pos = position;
        label.color = IM_COL32_WHITE;
        label.text = "Example label";
        labels2draw.push_back(label);
        const int before = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
        draw_cached_labels();
        const bool visible = std::isfinite(position.x) && position.x > -100 && position.x < 192
            && position.y > -10 && position.y < 108;
        EXPECT_EQ(ImGui::GetBackgroundDrawList()->VtxBuffer.Size > before, visible);
        EXPECT_TRUE(labels2draw.empty());
    }
}

TEST_F(RenderingRegressionTest, ConstellationLinesRetainCrossingsWithFarOffscreenEndpoints)
{
    Star *observer = make_star("Observer");
    Star *first = make_star("First endpoint");
    Star *second = make_star("Second endpoint");
    set(mycenobj, static_cast<CelestialObject*>(observer));
    set(here, CelestialLocation());
    set(whereami, -1);
    set(show_labels, false);
    set(show_consln, true);
    set(show_axes, false);
    set(constellations, std::vector<Constellation>(1));
    auto &group = constellations[0];
    group.vantage_resolved = true;
    ConsLine line;
    line.a = first;
    line.b = second;
    group.lines.push_back(line);
    first->drawnx = -1e12;
    second->drawnx = 1e12;
    first->drawny = second->drawny = 54;
    auto *draw = ImGui::GetBackgroundDrawList();
    draw_cons_lines();
    ASSERT_GT(draw->VtxBuffer.Size, 0);
    const int count = draw->VtxBuffer.Size;
    first->drawny = second->drawny = -1e12;
    draw_cons_lines();
    EXPECT_EQ(draw->VtxBuffer.Size, count);
    first->drawnx = first->drawny = -1e9;
    second->drawnx = 100;
    second->drawny = 54;
    draw_cons_lines();
    EXPECT_EQ(draw->VtxBuffer.Size, count);
}

TEST_F(RenderingRegressionTest, SupportingLinesClipCrossingsAndRejectOffscreenGeometry)
{
    ImGuiIO &io = ImGui::GetIO();
    for (const auto &pair : {std::pair<ImVec2, ImVec2>{{-1000, 54}, {1000, 54}},
        {{-1e29f, 54}, {1e29f, 54}}, {{96, -1e29f}, {96, 1e29f}}})
    {
        auto *draw = ImGui::GetBackgroundDrawList();
        const int before = draw->VtxBuffer.Size;
        wrapped_line(pair.first, pair.second, IM_COL32_WHITE, io);
        ASSERT_GT(draw->VtxBuffer.Size, before);
        for (int i = before; i < draw->VtxBuffer.Size; ++i)
        {
            EXPECT_GE(draw->VtxBuffer[i].pos.x, -4);
            EXPECT_LE(draw->VtxBuffer[i].pos.x, 196);
            EXPECT_GE(draw->VtxBuffer[i].pos.y, -4);
            EXPECT_LE(draw->VtxBuffer[i].pos.y, 112);
        }
    }
    const int before = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
    wrapped_line(ImVec2(-1000, -1000), ImVec2(1000, -900), IM_COL32_WHITE, io);
    wrapped_line(ImVec2(1000, -1000), ImVec2(1000, 1000), IM_COL32_WHITE, io);
    wrapped_line(ImVec2(NAN, 40), ImVec2(50, 40), IM_COL32_WHITE, io);
    EXPECT_EQ(ImGui::GetBackgroundDrawList()->VtxBuffer.Size, before);
    set(view_mode, vm_skymap);
    wrapped_line(ImVec2(190, 54), ImVec2(2, 54), IM_COL32_WHITE, io);
    const auto &vertices = ImGui::GetBackgroundDrawList()->VtxBuffer;
    EXPECT_GT(vertices.Size, before);
    for (int i = before; i < vertices.Size; ++i)
    {
        EXPECT_TRUE(vertices[i].pos.x < 8 || vertices[i].pos.x > 184);
    }
}

TEST_F(RenderingRegressionTest, ReturnToPresentRefreshesMovingAndCulledStars)
{
    double magnitudes[4] = {}, radii[4] = {}, bloom[4] = {};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(radii));
    set(bloomrad_cache, static_cast<double*>(bloom));
    set(here, here);
    set(luminous_flux, luminous_flux);
    set(lsyscache, std::vector<CelestialObject*>());
    set(inside_galaxy_idx, inside_galaxy_idx);
    Star *observer = make_star("Observer");
    Star *moving = make_star("Moving field star");
    Star *faint = make_star("Culled moving primary");
    Star *companion = make_star("Culled companion");
    observer->cenobj = observer;
    for (Star *star : {moving, faint})
    {
        star->cenobj = star;
        star->right_ascension = .2;
        star->declination = .3;
        star->distance = (star == moving ? 20 : 100) * parsec;
        star->proper_motion_RA = .5 * arcsecond / oneyear;
        star->proper_motion_decl = .2 * arcsecond / oneyear;
        star->radial_velocity = 40000;
        star->apparent_magnitude = star == moving ? 6 : 30;
    }
    companion->make_companion_of(faint, 'B');
    companion->cenobj = faint;
    companion->orbit->semimajor_axis = .2 * AU;
    companion->orbit->period = 5 * oneday;
    companion->apparent_magnitude = 30;
    set(whereami, observer->seqno);
    set(mycenobj, static_cast<CelestialObject*>(observer));
    set(view_mode, vm_spaceship);
    set(explorer, false);
    set(selected, -1);
    set(trackidx, -1);
    set(editidx, -1);
    set(local_tmstep, false);
    set(redo_proper_motions, false);
    set(global_brightness, 1.0);
    set(JDnow, JDnow);
    set(simnow, simnow);
    ImGui::GetIO().KeyCtrl = false;
    for (double years : {-10000000.0, 10000000.0})
    {
        simnow = J2000_TIME_T + years * oneyear;
        JDnow = J2000 + years * oneyear / oneday;
        observer->update_location(simnow);
        here = observer->location;
        moving->update_location(simnow);
        faint->update_location(simnow);
        companion->update_location(simnow);
        ASSERT_FALSE(moving->is_really_truly_in_visible_box(Point(here)));
        faint->is_really_truly_in_visible_box(Point(here));
        companion->is_really_truly_in_visible_box(Point(here));
        redo_proper_motions = false;
        const double before = std::time(nullptr);
        process_key_cmd_char('@');
        EXPECT_GE(simnow, before);
        EXPECT_LE(simnow, std::time(nullptr));
        for (Star *star : {moving, faint})
        {
            const double elapsed = simnow - J2000_TIME_T + (J2000 - star->epoch) * oneday;
            const Point expected = Point::from_ra_dec(star->right_ascension + star->proper_motion_RA * elapsed,
                star->declination + star->proper_motion_decl * elapsed, star->distance + star->radial_velocity * elapsed);
            EXPECT_LT((star->location.system_center - expected).magnitude(), 1);
        }
        EXPECT_TRUE(moving->is_in_visible_box(Point(here)));
        EXPECT_FALSE(faint->is_in_visible_box(Point(here)));
        EXPECT_LT((companion->location.system_center - faint->location.system_center).magnitude(), 1);
        EXPECT_LT(companion->location.distance_to(faint->location), .201 * AU);
        EXPECT_FALSE(redo_proper_motions);
    }
}

TEST_F(RenderingRegressionTest, LargeTimeKeysRetainEarthOrLocalSolarTime)
{
    double magnitudes[4] = {}, radii[4] = {}, bloom[4] = {};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(radii));
    set(bloomrad_cache, static_cast<double*>(bloom));
    set(here, here);
    set(luminous_flux, luminous_flux);
    set(inside_galaxy_idx, inside_galaxy_idx);
    set(lsyscache, std::vector<CelestialObject*>());
    Star *sun = make_star("Reference star");
    Planet *earth = make_planet(sun, "Earth");
    Planet *local = make_planet(sun, "Time-step world", 1.5 * AU);
    Moon *moon = new Moon();
    strcpy(moon->name, "Time-step moon");
    moon->orbit = new Orbit();
    moon->orbit->center = local;
    moon->orbit->semimajor_axis = 3e8;
    moon->orbit->period = 20 * oneday;
    moon->orbit->eccentricity = .05;
    moon->sidereal_rotational_period = moon->orbit->period;
    moon->cenobj = sun;
    append_cel(moon);
    earth->sidereal_rotational_period = 86164.1;
    earth->obliquity = 23.44 * fiftyseventh;
    earth->orbit->eccentricity = .0167;
    local->sidereal_rotational_period = .83 * oneday;
    local->obliquity = .8;
    local->orbit->eccentricity = .25;
    for (Planet *world : {earth, local}) world->cenobj = sun;
    set(whereami, local->seqno);
    set(mycenobj, static_cast<CelestialObject*>(sun));
    set(view_mode, vm_system);
    set(JDnow, J2000 + 1234.12345);
    set(simnow, (JDnow - J2000) * oneday + J2000_TIME_T);
    set(local_tmstep, false);
    set(redo_proper_motions, false);
    ImGui::GetIO().KeyCtrl = false;
    const auto update = [&]()
    {
        simnow = (JDnow - J2000) * oneday + J2000_TIME_T;
        sun->update_location(simnow);
        earth->update_location(simnow);
        local->update_location(simnow);
        moon->update_location(simnow);
    };
    const char keys[] = {'d', 'm', 'y', 'z', 'D', 'M', 'Y', 'Z'};
    for (bool local_mode : {false, true})
    {
        local_tmstep = local_mode;
        Planet *reference = local_mode ? local : earth;
        for (char key : keys)
        {
            update();
            const double before = reference->local_solar_time(.4);
            const double start = JDnow;
            process_key_cmd_char(key);
            EXPECT_EQ(JDnow > start, key >= 'a');
            EXPECT_NEAR(simnow, (JDnow - J2000) * oneday + J2000_TIME_T, 1e-6);
            update();
            EXPECT_NEAR(std::remainder(reference->local_solar_time(.4) - before, 2 * _pi), 0, 1e-6);
        }
        for (char key : {'y', 'Y', 'z', 'Z'})
        {
            update();
            const double before = reference->local_solar_time(.4);
            process_key_cmd_ctrl_char(key);
            update();
            EXPECT_NEAR(std::remainder(reference->local_solar_time(.4) - before, 2 * _pi), 0, 1e-6);
        }
    }
    const double start = JDnow;
    process_key_cmd_char('i');
    EXPECT_NEAR(JDnow - start, 1.0 / 1440, 1e-9);
    EXPECT_NEAR(simnow, (JDnow - J2000) * oneday + J2000_TIME_T, 1e-6);
    local_tmstep = true;
    local->obliquity = 2.5;
    local->sidereal_rotational_period = 300 * oneday;
    for (Planet *reference : {local, static_cast<Planet*>(moon)})
    {
        whereami = reference->seqno;
        for (char key : {'d', 'D', 'm', 'M', 'y', 'Y'})
        {
            update();
            const double before = reference->local_solar_time(.4);
            const double previous = JDnow;
            process_key_cmd_char(key);
            EXPECT_EQ(JDnow > previous, key >= 'a');
            update();
            EXPECT_NEAR(std::remainder(reference->local_solar_time(.4) - before, 2 * _pi), 0, 1e-6);
        }
    }
}

TEST_F(RenderingRegressionTest, CachedCrownContoursMatchNativeEllipseVertices)
{
    for (bool antialias : {false, true})
    {
        for (float scale : {.5f, 2.0f, 15.0f, 150.0f, 1000.0f})
        {
            ImDrawList expected(ImGui::GetDrawListSharedData());
            ImDrawList actual(ImGui::GetDrawListSharedData());
            for (ImDrawList *draw : {&expected, &actual})
            {
                draw->_ResetForNewFrame();
                draw->PushClipRectFullScreen();
                if (antialias) draw->Flags |= ImDrawListFlags_AntiAliasedFill;
                else draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            }
            const ImVec2 center(120.125f, 85.75f), radius(scale, scale * .35f);
            const ImU32 color = IM_COL32(25, 130, 80, 255);
            expected.AddEllipseFilled(center, radius, color);
            draw_scenery_crown(&actual, center, radius, color);
            ASSERT_EQ(actual.VtxBuffer.Size, expected.VtxBuffer.Size);
            ASSERT_EQ(actual.IdxBuffer.Size, expected.IdxBuffer.Size);
            for (int i = 0; i < actual.VtxBuffer.Size; ++i)
            {
                EXPECT_EQ(actual.VtxBuffer[i].pos.x, expected.VtxBuffer[i].pos.x);
                EXPECT_EQ(actual.VtxBuffer[i].pos.y, expected.VtxBuffer[i].pos.y);
                EXPECT_EQ(actual.VtxBuffer[i].col, expected.VtxBuffer[i].col);
            }
            for (int i = 0; i < actual.IdxBuffer.Size; ++i)
            {
                EXPECT_EQ(actual.IdxBuffer[i], expected.IdxBuffer[i]);
            }
        }
    }
}

TEST_F(RenderingRegressionTest, CrownOcclusionPreservesPixelsAtGapsAndViewportEdges)
{
    SceneryCrownOcclusion coverage;
    coverage.reset(1027, 131);
    coverage.cover(500, 65, 1000, 120);
    EXPECT_TRUE(coverage.covered(16, 16, 992, 112));
    EXPECT_FALSE(coverage.covered(0, 16, 992, 112));
    EXPECT_FALSE(coverage.covered(16, 16, 1027, 112));
    coverage.reset(render_width, render_height);
    EXPECT_FALSE(coverage.covered(80, 45, 110, 65));
    coverage.cover(96, 54, 80, 40);
    EXPECT_TRUE(coverage.covered(80, 45, 110, 65));
    EXPECT_FALSE(coverage.covered(50, 45, 110, 65));
    coverage.reset(render_width, render_height);
    coverage.cover(56, 54, 80, 40);
    coverage.cover(136, 54, 80, 40);
    EXPECT_FALSE(coverage.covered(90, 45, 102, 65));
    for (float x : {0.0f, 96.0f, 190.0f})
    {
        for (float far_radius : {10.0f, 45.0f})
        {
            const ImVec2 center(x, 54), near_radius(80, 40), radius(far_radius, far_radius * .5f);
            coverage.reset(render_width, render_height);
            coverage.cover(center.x, center.y, near_radius.x, near_radius.y);
            const bool hidden = coverage.covered(x - radius.x - 2, 54 - radius.y - 2,
                x + radius.x + 2, 54 + radius.y + 2);
            auto *draw = ImGui::GetBackgroundDrawList();
            draw_scenery_crown(draw, center, radius, IM_COL32(30, 60, 220, 255));
            draw_scenery_crown(draw, center, near_radius, IM_COL32(30, 180, 60, 255));
            const auto expected = render();
            ImGui::NewFrame();
            draw = ImGui::GetBackgroundDrawList();
            if (!hidden) draw_scenery_crown(draw, center, radius, IM_COL32(30, 60, 220, 255));
            draw_scenery_crown(draw, center, near_radius, IM_COL32(30, 180, 60, 255));
            EXPECT_EQ(render(), expected);
            ImGui::NewFrame();
        }
    }
}

TEST_F(RenderingRegressionTest, DenseForestWalkingProducesFiniteGeometry)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new BlueBiomeTestMap(SurfaceMaterial::vegetation);
    world->vegetation_r = 20;
    world->vegetation_g = 100;
    world->vegetation_b = 160;
    cels[0]->location.local_position = cels[0]->tmprel = Point(0, -1e11, 1e11);
    scenery();
    const auto start = std::chrono::steady_clock::now();
    size_t vertex_count = 0;
    for (int frame = 0; frame < 40; ++frame)
    {
        set(viewer_lat, frame * .25 / world->get_equatorial_radius());
        const auto vertices = scenery();
        ASSERT_GT(vertices.size(), 100u);
        vertex_count += vertices.size();
        for (const auto &vertex : vertices)
        {
            ASSERT_TRUE(std::isfinite(vertex.pos.x));
            ASSERT_TRUE(std::isfinite(vertex.pos.y));
        }
    }
    std::cout << "Dense forest CPU ms/frame: "
        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 40
        << "; vertices/frame: " << vertex_count / 40 << '\n';
}

TEST_F(RenderingRegressionTest, PlantsArePlacedNearTheKilometerCutoff)
{
    Planet* world = surface(true);
    delete world->surf_map;
    world->surf_map = new DistantPlantTestMap(world->get_equatorial_radius());
    world->plant_trunk_height = 2;
    world->plant_crown_size = 2.85;
    set(zoom, 30.0);
    set(altitude, 0.0);
    const auto vertices = scenery();
    int crowns = 0;
    for (const auto &vertex : vertices)
    {
        const int red = (vertex.col >> IM_COL32_R_SHIFT) & 255;
        const int green = (vertex.col >> IM_COL32_G_SHIFT) & 255;
        const int blue = (vertex.col >> IM_COL32_B_SHIFT) & 255;
        if (green > 1.2 * red && green > 2 * blue) crowns++;
    }
    EXPECT_GT(crowns, 0);
}

TEST_F(RenderingRegressionTest, ShorelineGeometryStaysFiniteAtFullResolution)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new SceneryTestMap(true, true);
    set(dispcx, 960.0);
    set(dispcy, 540.0);
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    const auto vertices = scenery();
    int plants = 0;
    ASSERT_GT(vertices.size(), 100u);
    for (const auto &vertex : vertices)
    {
        EXPECT_TRUE(std::isfinite(vertex.pos.x));
        EXPECT_TRUE(std::isfinite(vertex.pos.y));
        const int red = (vertex.col >> IM_COL32_R_SHIFT) & 255;
        const int green = (vertex.col >> IM_COL32_G_SHIFT) & 255;
        const int blue = (vertex.col >> IM_COL32_B_SHIFT) & 255;
        if ((vertex.col >> IM_COL32_A_SHIFT) == 255 && green > red * 1.4
            && blue < red * 0.8 && vertex.col != IM_COL32(80, 140, 30, 255))
        {
            ++plants;
        }
    }
    EXPECT_GT(plants, 0);
}

TEST_F(RenderingRegressionTest, DenseAtmosphereCoastHasSeaBelowTheLiftedHorizon)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new CoastalTestMap();
    world->vegetation_r = world->vegetation_g = world->vegetation_b = 0;
    world->temperature = 311.5;
    world->ensure_atmosphere()->surface_pressure = 15 * oneatm;
    set(viewer_lat, -0.0001);
    set(viewer_lon, 0.0);
    set(azimuth, 0.0);
    set(zoom, 4.0);
    set(altitude, world->atmospheric_horizon_lift());
    find_horizon();
    draw_horizon();
    const auto pixels = render();
    int sea = 0;
    for (int y = 56; y < 60; y++)
    {
        for (int x = 86; x < 106; x++)
        {
            const int index = ((render_height - y - 1) * render_width + x) * 4;
            sea += pixels[index + 2] > pixels[index] * 2;
        }
    }
    EXPECT_GT(sea, 60);
    ImGui::NewFrame();
}

TEST_F(RenderingRegressionTest, HotDryGroundSkipsPerPixelBiomePainting)
{
    Planet *world = surface(true);
    world->temperature = 737;
    world->ensure_atmosphere()->surface_pressure = 93 * oneatm;
    delete world->surf_map;
    world->surf_map = new MountainBandsTestMap();
    set(dispcx, 960.0);
    set(dispcy, 540.0);
    set(show_terrain, false);
    find_horizon();
    const int before = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
    draw_horizon();
    const int vertices = ImGui::GetBackgroundDrawList()->VtxBuffer.Size - before;
    render();
    ImGui::NewFrame();
    delete world->surf_map;
    world->surf_map = new SceneryTestMap();
    const int plain_before = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
    draw_horizon();
    const int plain_vertices = ImGui::GetBackgroundDrawList()->VtxBuffer.Size - plain_before;
    EXPECT_EQ(vertices, plain_vertices);
    render();
    ImGui::NewFrame();
}

TEST_F(RenderingRegressionTest, MountainsSampleDifferentBiomesAlongTheirDepthAboveTheSeaHorizon)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new MountainBandsTestMap();
    world->has_water = 0;
    set(zoom, 20.0);
    set(altitude, 0.005);
    find_horizon();
    draw_horizon();
    const auto pixels = render();
    const double radius = world->get_equatorial_radius();
    const double dip = acos(radius / (radius + viewer_eye_height));
    const int sea_horizon = static_cast<int>(dispcy + dispcx * zoom * tan(altitude + dip));
    int mixed_columns = 0;
    for (int x = 80; x < 112; ++x)
    {
        bool first = false, second = false;
        for (int y = 0; y < std::min(render_height, sea_horizon - 1); ++y)
        {
            const int index = ((render_height - y - 1) * render_width + x) * 4;
            first = first || (pixels[index] == 160 && pixels[index + 1] == 120 && pixels[index + 2] == 80);
            second = second || (pixels[index] == 110 && pixels[index + 1] == 100 && pixels[index + 2] == 80);
        }
        mixed_columns += first && second;
    }
    EXPECT_GT(mixed_columns, 0);
    ImGui::NewFrame();
}

TEST_F(RenderingRegressionTest, RainRequiresOverheadCumulusOrHeavyHaze)
{
    CloudTestView view;
    view.sample_overhead = true;
    clouds(0, half_pi, 0.001, view);
    ASSERT_TRUE(cloud_rain_cover().valid);
    EXPECT_FALSE(cloud_rain_cover().permits_rain());

    clouds(255, half_pi, 0.001, view);
    EXPECT_EQ(cloud_rain_cover().heavy_haze, 1.0f);
    EXPECT_TRUE(cloud_rain_cover().permits_rain());

    view.overcast = true;
    clouds(0, half_pi, 0.001, view);
    EXPECT_EQ(cloud_rain_cover().heavy_haze, 1.0f);
    EXPECT_TRUE(cloud_rain_cover().permits_rain());

    view.observer = view.shell + 2;
    clouds(255, half_pi, 0.001, view);
    EXPECT_FALSE(cloud_rain_cover().permits_rain());

    view.sample_overhead = false;
    clouds(255, half_pi, 0.001, view);
    EXPECT_FALSE(cloud_rain_cover().valid);
}

TEST_F(RenderingRegressionTest, RainProbeMatchesVisibleCumulusAndClearGapsIndependentOfCameraAndLighting)
{
    CloudTestView view;
    view.sample_overhead = true;
    bool found_cumulus = false, found_gap = false;
    double cumulus_latitude = 0;
    for (int step = 0; step < 40 && !(found_cumulus && found_gap); ++step)
    {
        view.latitude = 0.2 + step * 0.003;
        const auto pixels = clouds(128, half_pi, 0.001, view);
        const auto cover = cloud_rain_cover();
        ASSERT_TRUE(cover.valid);
        const int alpha = pixels[((render_height / 2) * render_width + render_width / 2) * 4 + 3];
        if (cover.cumulus > 0.9f)
        {
            EXPECT_GT(alpha, 240);
            EXPECT_TRUE(cover.permits_rain());
            found_cumulus = true;
            cumulus_latitude = view.latitude;
        }
        if (cover.cumulus < 0.01f)
        {
            EXPECT_LT(alpha, 240);
            EXPECT_FALSE(cover.permits_rain());
            found_gap = true;
        }
    }
    ASSERT_TRUE(found_cumulus);
    ASSERT_TRUE(found_gap);
    view.latitude = cumulus_latitude;
    view.azimuth = 1.2;
    clouds(128, 0.0, 0.5, view);
    const auto daytime = cloud_rain_cover();
    EXPECT_TRUE(daytime.permits_rain());
    view.dark = true;
    clouds(128, -0.1, 0.5, view);
    EXPECT_EQ(cloud_rain_cover().cumulus, daytime.cumulus);
    EXPECT_EQ(cloud_rain_cover().heavy_haze, daytime.heavy_haze);
    EXPECT_TRUE(cloud_rain_cover().permits_rain());
}

TEST_F(RenderingRegressionTest, SystemViewKeepsAPlanetVisibleWhenItsHostRadiusIsUnknown)
{
    system_bloom.assign(2, 10.0);
    set(bloomrad_cache, system_bloom.data());
    Star *host = make_star("DW Lyn");
    host->mass = 0;
    host->volumetric_mean_radius = 0;
    host->looked_for_maps = true;
    host->cenobj = host;
    strcpy(host->spectral_type, "SdB D");
    Planet *planet = make_planet(host, "DW Lyn b", 1.148 * AU);
    planet->mass = 5.58 * jupiter_mass;
    planet->volumetric_mean_radius = jupiter_radius;
    planet->looked_for_maps = true;
    planet->surf_map = new SceneryTestMap();
    const std::vector<CelestialObject*> local_system =
    {
        host, planet
    };
    set(lsyscache, local_system);
    set(view_mode, vm_system);
    set(selected, -1);
    set(lbl_localsys, false);
    set(show_clouds, false);
    draw_system_view();
    EXPECT_TRUE(std::isfinite(host->drawnx));
    EXPECT_TRUE(std::isfinite(host->drawny));
    EXPECT_TRUE(host->onscreen);
    EXPECT_LT(host->drawnxmin, host->drawnxmax);
    EXPECT_TRUE(std::isfinite(planet->drawnx));
    EXPECT_TRUE(std::isfinite(planet->drawny));
    EXPECT_GT(planet->drawnx, 0);
    EXPECT_LT(planet->drawnx, dispcx * 2);
    EXPECT_GT(planet->drawny, 0);
    EXPECT_LT(planet->drawny, dispcy * 2);
    EXPECT_GT(host->volumetric_mean_radius, 0);
    EXPECT_TRUE(std::isfinite(host->volumetric_mean_radius));
    EXPECT_EQ(host->mass, 0);
    const auto pixels = render();
    int star_pixels = 0, planet_pixels = 0;
    for (int y = 0; y < render_height; ++y)
    {
        for (int x = 0; x < render_width; ++x)
        {
            const int index = (y * render_width + x) * 4;
            if (!(pixels[index] || pixels[index + 1] || pixels[index + 2]))
            {
                continue;
            }
            star_pixels += x < render_width / 4;
            planet_pixels += fabs(x - planet->drawnx) < 10;
        }
    }
    EXPECT_GT(star_pixels, 20);
    EXPECT_GT(planet_pixels, 20);
    ImGui::NewFrame();
}

TEST_F(RenderingRegressionTest, ObjectLabelsFollowTheirObjectDepthAcrossAnOpaqueDisc)
{
    Star *disc = make_star("Foreground Disc");
    disc->volumetric_mean_radius = 350;
    disc->location.local_position = disc->tmprel = Point(0, 0, 1000);
    disc->drawnx = 96;
    disc->drawny = 54;
    disc->cenobj = disc;
    disc->looked_for_maps = true;
    disc->surf_map = new SceneryTestMap();
    Star *label = make_star("Background Label");
    label->volumetric_mean_radius = 0.001;
    label->drawnx = 96;
    label->drawny = 30;
    label->cenobj = label;
    label->tmp_vis_flag = true;
    label->looked_for_maps = true;
    double magnitudes[] = {4, 4}, blooms[] = {1, 1}, radii[] = {0.35, 0.000001};
    bool discs[] = {false, false};
    set(vmag_cache, &magnitudes[0]);
    set(bloomrad_cache, &blooms[0]);
    set(angular_radius, &radii[0]);
    set(discinstead, &discs[0]);
    set(mycenobj, static_cast<CelestialObject*>(disc));
    set(global_font, ImGui::GetFont());
    set(show_localsys, true);
    set(show_orbits, false);
    set(show_labels, false);
    set(lbl_localsys, false);
    set(show_clouds, false);
    set(global_brightness, 1.0);
    set(sky_mag_shift, 0.0);
    set(here, CelestialLocation());
    set(labels2draw, std::vector<DrawLabel>());
    for (double distance : {10000.0, 500.0})
    {
        label->location.local_position = label->tmprel = Point(0, 0.25 * distance, distance);
        std::vector<unsigned char> labeled;
        for (bool show : {true, false})
        {
            set(selected, show ? label->seqno : -1);
            freeze_surface_time();
            draw_objects();
            EXPECT_EQ(label->label_shown, show);
            EXPECT_TRUE(labels2draw.empty());
            draw_cached_labels();
            const auto pixels = render();
            if (show) labeled = pixels;
            else
            {
                int different = 0;
                for (int y = 37; y < 45; ++y)
                {
                    for (int x = 85; x < 106; ++x)
                    {
                        const int offset = ((render_height - 1 - y) * render_width + x) * 4;
                        if (!std::equal(pixels.begin() + offset, pixels.begin() + offset + 4, labeled.begin() + offset)) ++different;
                    }
                }
                if (distance > 1000)
                {
                    EXPECT_EQ(different, 0);
                }
                else
                {
                    EXPECT_GT(different, 0);
                }
            }
            ImGui::NewFrame();
        }
    }
}

TEST_F(RenderingRegressionTest, DirectionalThrustAndPlusStaySublightUntilWarpIsRequested)
{
    make_star("Sol");
    make_star("Navigation target");
    set(velocity, Point());
    set(azimuth, 0.0);
    set(altitude, 0.0);
    set(ImGui::GetIO().KeyCtrl, false);
    set(viewchanged, viewchanged);
    set(spin, spin);
    set(took_off_from, took_off_from);
    set(tookoff_countdown, tookoff_countdown);
    set(viewer_locale, viewer_locale);
    const double light_frame = speed_of_light / target_frame_rate;
    for (double fraction : {0.01, 0.099, 0.1, 0.9, 0.999999, 1.0 - 1e-12, 1.0})
    {
        for (double heading : {0.0, half_pi, _pi})
        {
            for (int command = 0; command < 3; ++command)
            {
                velocity = Point(0, 0, fraction * light_frame);
                azimuth = heading;
                altitude = heading == half_pi ? 0.4 : 0.0;
                for (int step = 0; step < 512; ++step)
                {
                    if (command == 0) process_key_end();
                    else if (command == 1) process_key_home();
                    else process_key_cmd_char('+');
                    ASSERT_TRUE(std::isfinite(velocity.magnitude()));
                    ASSERT_LT(velocity.magnitude() * target_frame_rate, speed_of_light)
                        << "fraction " << fraction << ", heading " << heading << ", command " << command;
                }
            }
        }
    }
    azimuth = altitude = 0;
    const double initial = 0.9 * light_frame;
    velocity = Point(0, 0, initial);
    process_key_end();
    const double end_increment = velocity.magnitude() - initial;
    velocity = Point(0, 0, initial);
    process_key_cmd_char('+');
    EXPECT_NEAR(velocity.magnitude() - initial, 5 * end_increment, 1e-7);
    velocity = Point();
    process_key_cmd_char('w');
    EXPECT_GT(velocity.magnitude() * target_frame_rate, speed_of_light);
    const double warp_speed = velocity.magnitude();
    process_key_end();
    EXPECT_GT(velocity.magnitude(), warp_speed);
}

TEST_F(RenderingRegressionTest, DirectionalThrustKeepsSurfaceWalkingUnchanged)
{
    extern double walk_speed;
    Star *host = make_star("Sol");
    Planet *world = make_planet(host);
    set(view_mode, vm_horizon);
    set(whereami, world->seqno);
    set(viewer_lat, 0.3);
    set(viewer_lon, 0.2);
    set(walk_speed, 4.0 / target_frame_rate);
    set(save_viewer_latlon, true);
    set(velocity, Point(0, 0, 1000));
    const Point previous = velocity;
    const double step = walk_speed / (2 * _pi * world->volumetric_mean_radius);
    process_key_end();
    EXPECT_NEAR(viewer_lat, 0.3 + step, 1e-15);
    EXPECT_DOUBLE_EQ(viewer_lon, 0.2);
    EXPECT_FALSE(save_viewer_latlon);
    process_key_home();
    EXPECT_NEAR(viewer_lat, 0.3, 1e-15);
    EXPECT_DOUBLE_EQ(velocity.x, previous.x);
    EXPECT_DOUBLE_EQ(velocity.y, previous.y);
    EXPECT_DOUBLE_EQ(velocity.z, previous.z);
}

TEST_F(RenderingRegressionTest, ExtremeZoomKeepsProjectedCentersIndependentOfGpuBounds)
{
    Star* sun = make_star("Sun");
    sun->distance = 0;
    Star* star = make_star("Vega projection regression");
    star->right_ascension = 0.7;
    star->declination = 0.4;
    star->distance = 25 * light_year;
    star->looked_for_maps = true;
    double magnitudes[2] = {}, radii[2] = {}, blooms[2] = {};
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(radii));
    set(bloomrad_cache, static_cast<double*>(blooms));
    set(mycenobj, static_cast<CelestialObject*>(sun));
    set(selected, star->seqno);
    set(trackidx, -1);
    set(zoom, 2500000.0);
    set(here, CelestialLocation());
    here.equatorial_plane.v = Point(1, 0, 1);
    here.equatorial_plane.a = 1e-8;
    compute_object_draw_coordinates();
    const Cartesian2D projection(to_viewer_plane(star->tmprel), azimuth + azimuth_correction, altitude, zoom);
    EXPECT_NEAR(star->drawnx, static_cast<float>(dispcx + projection.x * dispcx), 0.01);
    EXPECT_NEAR(star->drawny, static_cast<float>(dispcy + projection.y * dispcx), 0.01);
    const double x = star->drawnx, y = star->drawny;
    EXPECT_EQ(draw_sphere_gpu(star, radii[star->seqno] * zoom), 0);
    EXPECT_DOUBLE_EQ(star->drawnx, x);
    EXPECT_DOUBLE_EQ(star->drawny, y);
}

TEST_F(RenderingRegressionTest, ExtremeZoomCullsBackgroundDiscsBeforeDrawingGlare)
{
    Star* sun = make_star("Sun");
    sun->deleted = true;
    Star* vega = make_star("Vega zoom regression");
    vega->looked_for_maps = true;
    const double distance = 25 * light_year;
    vega->location.local_position = vega->tmprel = Point(0, 0, distance);
    vega->drawnx = 96;
    vega->drawny = 54;
    for (int i = 0; i < 3; ++i)
    {
        Star* background = make_star("Offscreen star");
        background->looked_for_maps = true;
        background->location.local_position = background->tmprel = Point((0.03 + 0.02 * i) * distance, 0, distance);
        background->drawnx = 96 + background->tmprel.x / distance * 2500000 * 96;
        background->drawny = 54;
    }
    double magnitudes[5] = {}, radii[5], blooms[5] = {};
    bool discs[5] = {};
    std::fill(radii, radii + 5, 2.36 * solar_radius / distance);
    set(vmag_cache, static_cast<double*>(magnitudes));
    set(angular_radius, static_cast<double*>(radii));
    set(bloomrad_cache, static_cast<double*>(blooms));
    set(discinstead, static_cast<bool*>(discs));
    set(mycenobj, static_cast<CelestialObject*>(sun));
    set(selected, vega->seqno);
    set(zoom, 2500000.0);
    set(here, CelestialLocation());
    set(global_font, ImGui::GetFont());
    set(show_localsys, true);
    set(show_orbits, false);
    set(show_labels, false);
    set(lbl_localsys, false);
    set(global_brightness, 20.0);
    set(sky_mag_shift, 0.0);
    set(labels2draw, std::vector<DrawLabel>());
    draw_objects();
    for (int i = 2; i < 5; ++i) EXPECT_DOUBLE_EQ(blooms[i], 0);
    EXPECT_LT(ImGui::GetBackgroundDrawList()->VtxBuffer.Size, 10000);
}

TEST_F(RenderingRegressionTest, FindShoreShortcutMovesToDetailedEdgeAndPreservesView)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new CoastalTestMap();
    world->sidereal_rotational_period = oneday;
    set(viewer_lat, -.002);
    set(viewer_lon, .2);
    set(viewer_locale, std::string("Old locale"));
    set(selected_locale, selected_locale);
    set(is_a_locale_under_cursor, is_a_locale_under_cursor);
    set(velocity, Point(1, 2, 3));
    set(save_viewer_latlon, true);
    set(viewchanged, false);
    set(here, here);
    set(npdummy.location, npdummy.location);
    set(npaz, npaz);
    const double heading = azimuth, tilt = altitude, magnification = zoom;
    const double eye = viewer_eye_height, epoch = simnow;
    ASSERT_TRUE(can_find_shore());
    process_key_cmd_ctrl_char('f');
    EXPECT_GT(viewer_lat, -.0002);
    EXPECT_EQ(view_mode, vm_horizon);
    EXPECT_EQ(whereami, world->seqno);
    EXPECT_DOUBLE_EQ(azimuth, heading);
    EXPECT_DOUBLE_EQ(altitude, tilt);
    EXPECT_DOUBLE_EQ(zoom, magnification);
    EXPECT_DOUBLE_EQ(viewer_eye_height, eye);
    EXPECT_DOUBLE_EQ(simnow, epoch);
    EXPECT_TRUE(viewer_locale.empty());
    EXPECT_EQ(selected_locale, nullptr);
    EXPECT_EQ(is_a_locale_under_cursor, nullptr);
    EXPECT_DOUBLE_EQ(velocity.magnitude(), 0);
    EXPECT_FALSE(save_viewer_latlon);
    EXPECT_TRUE(viewchanged);
    SurfaceBiomes field(static_cast<uint32_t>(whereami));
    const double radius = world->get_equatorial_radius();
    SurfaceSampler surface_sample(world, world->surf_map, field, radius, shore_displacement);
    EXPECT_FALSE(surface_sample.water_at(viewer_lat, viewer_lon));
    bool water_adjacent = false;
    for (int i = 0; i < 32; ++i)
    {
        const double bearing = 2 * _pi * i / 32;
        water_adjacent |= surface_sample.water_at(viewer_lat + .2 * cos(bearing) / radius,
            viewer_lon + .2 * sin(bearing) / radius);
    }
    EXPECT_TRUE(water_adjacent);
    world->has_water = 0;
    EXPECT_FALSE(can_find_shore());
    const double latitude = viewer_lat, longitude = viewer_lon;
    EXPECT_FALSE(process_find_shore());
    EXPECT_DOUBLE_EQ(viewer_lat, latitude);
    EXPECT_DOUBLE_EQ(viewer_lon, longitude);
    world->has_water = 1;
    view_mode = vm_spaceship;
    EXPECT_FALSE(can_find_shore());
    EXPECT_FALSE(process_find_shore());
}

TEST_F(RenderingRegressionTest, NadirSurfaceCoversTheViewportOnLandAndWater)
{
    Planet *world = surface(true);
    set(show_terrain, false);
    for (bool water : {false, true})
    {
        world->has_water = water ? 1 : 0;
        world->vegetation_r = world->vegetation_g = world->vegetation_b = 0;
        for (double pitch : {-half_pi - .01, -half_pi, -half_pi + .01})
        {
            for (double magnification : {1.0, 4.0})
            {
                altitude = pitch;
                zoom = magnification;
                find_horizon();
                draw_horizon();
                const auto pixels = render();
                for (int y : {2, render_height / 2, render_height - 3})
                {
                    for (int x : {2, render_width / 2, render_width - 3})
                        EXPECT_EQ(pixels[(y * render_width + x) * 4 + 3], 255)
                            << "water=" << water << " pitch=" << pitch << " zoom=" << zoom;
                }
                ImGui::NewFrame();
            }
        }
    }
}

TEST_F(RenderingRegressionTest, SkyGradientFollowsZoomAndViewingAltitude)
{
    surface(true);
    set(mycenobj, static_cast<CelestialObject*>(nullptr));
    set(whtbkgd, false);
    set(sky_grad, std::map<int, RGB3>{});
    set(sky_mag_shift, sky_mag_shift);
    altitude = .5;
    zoom = 1;
    draw_sky_gradient();
    ASSERT_FALSE(sky_grad.empty());
    const RGB3 center = sky_grad.at(int(dispcy));
    const int broad_range = abs(int(sky_grad.begin()->second.g) - int(sky_grad.rbegin()->second.g));
    zoom = 100;
    draw_sky_gradient();
    ASSERT_EQ(sky_grad.size(), size_t(dispcy * 2));
    const int narrow_range = abs(int(sky_grad.begin()->second.g) - int(sky_grad.rbegin()->second.g));
    EXPECT_GT(broad_range, 5);
    EXPECT_LE(narrow_range, 2);
    EXPECT_NEAR(sky_grad.at(int(dispcy)).g, center.g, 2);
    const RGB3 lower_sky = sky_grad.at(int(dispcy));
    altitude = 1.2;
    draw_sky_gradient();
    EXPECT_LT(sky_grad.at(int(dispcy)).g, lower_sky.g);
    altitude = -half_pi;
    draw_sky_gradient();
    EXPECT_TRUE(sky_grad.empty());
}

TEST_F(RenderingRegressionTest, GpuSphereUsesProjectedSkyRows)
{
    set(sky_grad, std::map<int, RGB3>{});
    for (int y = 0; y < render_height; ++y) sky_grad[y] = RGB3(32, 64, 96);
    SphereImpostorInput input{};
    input.cz = 5;
    input.r = input.axis_x = input.axis_y = input.axis_z = 1;
    input.basisX[0] = input.basisY[1] = 1;
    input.fallback_color = IM_COL32(0, 0, 0, 255);
    input.light_dir[2] = -1;
    input.daylight_tint[0] = input.daylight_tint[1] = input.daylight_tint[2] = 1;
    input.apply_sky_blend = true;
    double xmin, ymin, xmax, ymax;
    impostor_begin_frame();
    ASSERT_TRUE(queue_sphere_impostor(input, 1, dispcx, dispcy, dispcx, &xmin, &ymin, &xmax, &ymax));
    const auto pixels = render();
    const size_t center = (render_height / 2 * render_width + render_width / 2) * 4;
    EXPECT_NEAR(pixels[center], 32, 1);
    EXPECT_NEAR(pixels[center + 1], 64, 1);
    EXPECT_NEAR(pixels[center + 2], 96, 1);
    ImGui::NewFrame();
}
