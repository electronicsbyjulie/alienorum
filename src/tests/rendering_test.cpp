#include <algorithm>
#include <functional>
#include <vector>
#include <gtest/gtest.h>
#include "../globals.h"
#include "../visuals.h"
#include "../classes/clouds_gpu.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "universe_fixture.h"

using namespace alienorum;

class SceneryTestMap : public Map
{
    public:
    explicit SceneryTestMap(bool vegetation = false)
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
            red_data[i] = plant ? 80 : 160;
            green_data[i] = plant ? 140 : 120;
            blue_data[i] = plant ? 30 : 80;
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
        glViewport(0, 0, 192, 108);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::vector<unsigned char> pixels(192 * 108 * 4);
        glReadPixels(0, 0, 192, 108, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        EXPECT_EQ(glGetError(), static_cast<unsigned int>(GL_NO_ERROR));
        return pixels;
    }

    std::vector<unsigned char> clouds(unsigned char coverage, double elevation, double field_scale)
    {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        const unsigned char color[3] =
        {
            coverage, coverage, coverage
        };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        float refraction[1025];
        for (int i = 0; i <= 1024; ++i)
        {
            refraction[i] = -half_pi + i * _pi / 1024;
        }
        CloudSkyGpuInput input =
        {
        };
        input.camera[1] = elevation;
        input.camera[2] = field_scale;
        input.camera[3] = field_scale * 108.0 / 192;
        input.camera[5] = 1;
        input.camera[10] = 1;
        input.camera[12] = 1;
        input.world[0] = 1000;
        input.world[1] = 1001;
        input.world[2] = 2001;
        input.world[3] = -0.0001;
        input.light[0] = 1;
        input.light[1] = 0.6;
        input.texture = texture;
        input.refraction = refraction;
        EXPECT_TRUE(queue_cloud_sky_gpu(input));
        auto pixels = render();
        glDeleteTextures(1, &texture);
        return pixels;
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

TEST_F(RenderingRegressionTest, DistantCloudLobesExtendBeyondTheShellSearchCell)
{
    const auto pixels = clouds(77, 0.03, 0.125);
    // This lobe crosses a latitude search-cell border at a shallow viewing angle.
    // The former single-shell neighborhood clips the entire strip to alpha < 100.
    for (int x = 36; x <= 42; ++x)
    {
        EXPECT_GT(pixels[(107 * 192 + x) * 4 + 3], 220) << "x=" << x;
    }
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
