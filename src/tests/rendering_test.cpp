#include <algorithm>
#include <functional>
#include <vector>
#include <iostream>
#include <chrono>
#include <gtest/gtest.h>
#include "../globals.h"
#include "../visuals.h"
#include "../classes/clouds_gpu.h"
#include "imgui/backends/imgui_impl_opengl3.h"
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
        input.light[0] = 1;
        input.light[1] = 0.6;
        input.light[2] = view.overcast ? 1 : 0;
        input.light[8] = view.wind;
        input.texture = cloud_texture;
        input.revision = cloud_epoch * 1024 + coverage + (view.patterned ? 256 : 0) + (view.isolated ? 512 : 0);
        input.refraction = refraction;
        input.geometry_mode = view.mode;
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
    view.latitude = clat + 0.5 / view.shell;
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

TEST_F(RenderingRegressionTest, WalkingAcrossVegetationBordersKeepsAMixedSceneryPopulation)
{
    Planet *world = surface(true);
    zoom = 4.0;
    const auto plants = [](const std::vector<ImDrawVert> &vertices)
    {
        int count = 0;
        for (const auto &vertex : vertices)
        {
            const int red = (vertex.col >> IM_COL32_R_SHIFT) & 255;
            const int green = (vertex.col >> IM_COL32_G_SHIFT) & 255;
            const int blue = (vertex.col >> IM_COL32_B_SHIFT) & 255;
            if ((vertex.col >> IM_COL32_A_SHIFT) == 255
                && green > red * 1.4 && blue < red * 0.8
                && vertex.col != IM_COL32(80, 140, 30, 255))
            {
                ++count;
            }
        }
        return count;
    };
    viewer_lon = -0.05 / world->get_equatorial_radius();
    const int before = plants(scenery());
    viewer_lon = 0.05 / world->get_equatorial_radius();
    const int after = plants(scenery());
    EXPECT_GT(before, 0);
    EXPECT_GT(after, 0);
    EXPECT_NEAR(before, after, std::max(before, after) * 0.2);
}

TEST_F(RenderingRegressionTest, ShorelineShowsLandAndShimmeringSeaFromBothSides)
{
    Planet *world = surface(true);
    delete world->surf_map;
    world->surf_map = new SceneryTestMap(true, true);
    set(show_terrain, false);
    set(altitude, -0.4);
    for (double offset : {-0.05, 0.05})
    {
        viewer_lon = offset / world->get_equatorial_radius();
        find_horizon();
        draw_horizon();
        int crests = 0;
        float crest_right = 0;
        for (const auto &vertex : ImGui::GetBackgroundDrawList()->VtxBuffer)
        {
            const int alpha = vertex.col >> IM_COL32_A_SHIFT;
            if (alpha > 0 && alpha < 255)
            {
                ++crests;
                crest_right = std::max(crest_right, vertex.pos.x);
            }
        }
        EXPECT_GT(crests, 20);
        EXPECT_LT(crest_right, dispcx + 5.0);
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
