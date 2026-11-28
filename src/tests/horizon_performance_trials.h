#pragma once

#include "../horizon_performance.h"
#include <ctime>
#include <sstream>

class HorizonTrialTest : public RenderingRegressionTest
{
    protected:
    double cpu_clock_ms()
    {
        #if defined(CLOCK_THREAD_CPUTIME_ID)
        timespec value{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
        return value.tv_sec * 1000.0 + value.tv_nsec * 1e-6;
        #else
        return std::clock() * 1000.0 / CLOCKS_PER_SEC;
        #endif
    }

    void submit_frame()
    {
        ImGui::Render();
        glViewport(0, 0, render_width, render_height);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFinish();
        EXPECT_EQ(glGetError(), static_cast<unsigned int>(GL_NO_ERROR));
    }

    Planet* configure_scene(const std::string& name)
    {
        Planet* world = surface(true);
        world->mass = earth_mass;
        world->volumetric_mean_radius = earth_radius;
        world->type = rocky;
        world->temperature = 288;
        world->has_water = .79;
        world->vegetation_r = 49;
        world->vegetation_g = 65;
        world->vegetation_b = 28;
        world->ensure_atmosphere()->surface_pressure = oneatm;
        world->atm->ensure_composition()->N2_portion = .79;
        world->atm->comp->O2_portion = .21;
        if (name == "earth")
        {
            world->surf_map->load_from_jpeg("maps/Earth_surf.jpg");
            world->surf_map->load_from_png("maps/Earth_bump.png", true, world->m_bump_scale);
        }
        else if (name == "ice")
        {
            world->type = icy;
            world->mass = 6.86 * earth_mass;
            world->volumetric_mean_radius = 1.7 * earth_radius;
            world->temperature = 265;
            world->has_water = 1;
            world->vegetation_r = world->vegetation_g = world->vegetation_b = 0;
            world->atm->surface_pressure = 7.88e5;
            world->atm->comp->N2_portion = .88;
            world->atm->comp->O2_portion = 0;
            world->atm->comp->CO2_portion = .12;
            world->preserve_loaded_parameters = true;
            world->fictitious_map_height = 768;
            world->surf_map->generate_rocky_map(world);
        }
        else if (name == "ocean")
        {
            world->type = waterworld;
            world->temperature = 302;
            world->mass = 7.6 * earth_mass;
            world->volumetric_mean_radius = 1.76 * earth_radius;
            world->atm->surface_pressure = 15.35e5;
            world->has_water = 1;
            world->vegetation_r = world->vegetation_g = world->vegetation_b = 0;
            delete world->surf_map;
            world->surf_map = new BlueBiomeTestMap(SurfaceMaterial::water);
        }
        else if (name == "venus" || name == "airless" || name == "lava")
        {
            world->has_water = 0;
            world->vegetation_r = world->vegetation_g = world->vegetation_b = 0;
            world->temperature = name == "venus" ? 737 : name == "lava" ? 1800 : 100;
            world->atm->surface_pressure = name == "venus" ? 93e5 : name == "lava" ? oneatm : 0;
            if (name == "lava") world->type = lavaworld;
        }
        else if (name != "forest") ADD_FAILURE() << "Unknown trial scene " << name;
        if (name == "forest" && std::getenv("ALIENORUM_HORIZON_TRIAL")
            && std::atoi(std::getenv("ALIENORUM_HORIZON_TRIAL")) == 202) world->home_world = true;
        set(show_clouds, true);
        set(sky_grad, std::map<int, RGB3>{});
        set(horizon_ground_gpu_reference, false);
        set(horizon_terrain_gpu_reference, false);
        set(horizon_mesh_gpu_reference, false);
        set(scenery_gpu_reference, false);
        // All moving heavy scenes use the same sampling quality in both arms.
        set(horizon_ground_motion_budget_ms, 0.0);
        render_width = 1280;
        render_height = 800;
        SDL_SetWindowSize(window, render_width, render_height);
        SDL_PumpEvents();
        // Commit the GLX backbuffer resize before timing or reading pixels.
        SDL_GL_SwapWindow(window);
        std::cout << "HORIZON_RENDERER," << glGetString(GL_RENDERER) << std::endl;
        set(dispcx, render_width * .5);
        set(dispcy, render_height * .5);
        set(altitude, .0);
        ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
        ImGui::EndFrame();
        ImGui::NewFrame();
        return world;
    }
};

TEST_F(HorizonTrialTest, PairedHorizonPaths)
{
    const char* requested = std::getenv("ALIENORUM_HORIZON_TRIAL");
    if (!requested) GTEST_SKIP() << "Explicit horizon performance trial only.";
    const unsigned trial = static_cast<unsigned>(std::atoi(requested));
    const std::string scene = std::getenv("ALIENORUM_HORIZON_SCENE") ? std::getenv("ALIENORUM_HORIZON_SCENE") : "earth";
    const int frames = std::getenv("ALIENORUM_HORIZON_FRAMES") ? std::atoi(std::getenv("ALIENORUM_HORIZON_FRAMES")) : 6;
    const int rounds = std::getenv("ALIENORUM_HORIZON_ROUNDS") ? std::atoi(std::getenv("ALIENORUM_HORIZON_ROUNDS")) : 3;
    const bool inspect_every_pose = std::getenv("ALIENORUM_HORIZON_INSPECT_EVERY_POSE") != nullptr;
    Planet* world = configure_scene(scene);
    const double origin_lat = scene == "earth" ? 21.656 * fiftyseventh : .0001;
    const double origin_lon = scene == "earth" ? 63.281 * fiftyseventh : .4;
    const double radius = world->volumetric_mean_radius;
    const auto milliseconds = [](auto first, auto last)
    {
        return std::chrono::duration<double, std::milli>(last - first).count();
    };
    std::vector<std::string> modes = {"still", "pan", "walk", "fast_walk", "lighting"};
    if (const char* selected_modes = std::getenv("ALIENORUM_HORIZON_MODES"))
    {
        modes.clear();
        std::istringstream parser(selected_modes);
        std::string selected;
        while (std::getline(parser, selected, ',')) modes.push_back(selected);
    }
    for (const std::string& mode : modes)
    {
        for (int round = 0; round < rounds; ++round)
        {
            std::vector<unsigned char> original;
            std::vector<std::vector<unsigned char>> pose_images;
            std::vector<int> original_strides;
            for (int arm = 0; arm < 2; ++arm)
            {
                // Alternate order to reduce temperature and cache-order bias.
                const bool reference = (arm == 0) != (round % 2 != 0);
                horizon_reference_trial = reference ? trial : 0;
                world->surf_map->touch_gen();
                for (int frame = -2; frame < frames + 3; ++frame)
                {
                    // Settle the existing motion/refinement cache at the origin
                    // before replaying identical movement in the two arms.
                    const int pose_frame = std::max(0, frame);
                    const double distance = (mode == "fast_walk" ? 10 : mode == "walk" ? 1 : 0)
                        * pose_frame * 4.0 / 7.0;
                    // Match End/Shift+End's existing radians-per-circumference
                    // update at a fixed seven frames per second in both arms.
                    const double angle = distance / (2 * _pi * radius);
                    viewer_lat = origin_lat + angle * cos(1.3);
                    viewer_lon = origin_lon + angle * sin(1.3) / cos(origin_lat);
                    azimuth = 1.3 + (mode == "pan" ? pose_frame * .02 : 0);
                    luminous_flux = mode == "lighting" ? 4e10 * std::min(1.0, std::max(.01,
                        sqrt(world->mean_instellation()))) * (.45 + pose_frame * .02) : 4e10;
                    freeze_surface_time();
                    ImGui::GetCurrentContext()->Time = 50.0 + pose_frame / 7.0;
                    const auto start = std::chrono::steady_clock::now();
                    const double cpu_start = cpu_clock_ms();
                    find_horizon();
                    draw_horizon();
                    const auto prepared = std::chrono::steady_clock::now();
                    const double cpu = cpu_clock_ms() - cpu_start;
                    submit_frame();
                    const auto finished = std::chrono::steady_clock::now();
                    if (frame >= 3)
                    {
                        if ((trial == 201 || trial == 202) && !reference && scene == "forest") EXPECT_GT(plant_mesh_stats().instances, 0u);
                        if (arm == 0) original_strides.push_back(precipitation_stats.ground_sample_stride);
                        else EXPECT_EQ(precipitation_stats.ground_sample_stride, original_strides[frame - 3]);
                        std::cout << "HORIZON_SAMPLE," << trial << ',' << scene << ',' << mode << ',' << round << ','
                            << (reference ? "before" : "after") << ',' << frame - 3 << ','
                            << milliseconds(start, prepared) << ',' << cpu << ',' << milliseconds(start, finished)
                            << ',' << precipitation_stats.ground_sample_stride << '\n';
                    }
                    if (inspect_every_pose && frame >= 0)
                    {
                        std::vector<unsigned char> pixels(render_width * render_height * 4);
                        glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                        if (arm == 0) pose_images.push_back(std::move(pixels));
                        else if (trial != 201 && trial != 202) EXPECT_EQ(image_difference(pixels, pose_images[frame], 1), 0)
                            << scene << ' ' << mode << ' ' << round << ' ' << frame;
                    }
                    if (frame + 1 == frames + 3)
                    {
                        if ((trial == 201 || trial == 202) && !reference)
                            std::cout << "HORIZON_PLANTS," << scene << ',' << mode << ',' << plant_mesh_stats().instances
                                << ',' << plant_mesh_stats().triangles << ',' << plant_mesh_stats().draws << '\n';
                        std::vector<unsigned char> pixels(render_width * render_height * 4);
                        glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                        if (arm == 0) original = std::move(pixels);
                        else if (trial != 201 && trial != 202) EXPECT_EQ(image_difference(pixels, original, 1), 0) << scene << ' ' << mode << ' ' << round;
                    }
                    ImGui::NewFrame();
                }
                std::cout << std::flush;
            }
        }
    }
    horizon_reference_trial = 0;
    SDL_SetWindowSize(window, 192, 108);
}

TEST_F(HorizonTrialTest, PairedMeasuredWaterCoverage)
{
    const char* requested = std::getenv("ALIENORUM_HORIZON_TRIAL");
    const unsigned trial = requested ? static_cast<unsigned>(std::atoi(requested)) : 0;
    if (trial != 206 && trial != 209) GTEST_SKIP() << "Explicit measured water performance trial only.";
    const char* scene_arg = std::getenv("ALIENORUM_HORIZON_SCENE");
    const std::string scene = scene_arg ? scene_arg : "atlantic";
    ASSERT_TRUE(scene == "atlantic" || scene == "measured-coast");
    const char* rounds_arg = std::getenv("ALIENORUM_HORIZON_ROUNDS");
    const char* frames_arg = std::getenv("ALIENORUM_HORIZON_FRAMES");
    const int rounds = rounds_arg ? std::atoi(rounds_arg) : 8;
    const int frames = frames_arg ? std::atoi(frames_arg) : 6;
    ASSERT_GT(rounds, 0);
    ASSERT_GT(frames, 0);

    Planet* world = configure_scene("forest");
    delete world->surf_map;
    world->surf_map = new Map(world);
    world->has_real_maps = world->bump_has_bathymetry = true;
    world->sidereal_rotational_period = 86164.1;
    world->has_life = true;
    ASSERT_TRUE(world->surf_map->load_from_jpeg("maps/Earth_surf.jpg"));
    ASSERT_TRUE(world->surf_map->load_calibrated_bump("maps/Earth_bump.png", 65535, -.5));
    set(horizon_reference_trial, 0u);
    set(show_clouds, false);
    set(dragging, true);
    viewer_lat = 23.255 * fiftyseventh;
    viewer_lon = (scene == "atlantic" ? -20 : -16.16) * fiftyseventh;
    altitude = -.12;
    struct RestoreQuality
    {
        ~RestoreQuality() { update_horizon_quality(0); }
    } restore_quality;
    const auto milliseconds = [](auto start, auto end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<std::vector<unsigned char>> images;
        for (int arm = 0; arm < 2; ++arm)
        {
            const bool reference = (arm == 0) != (round % 2 != 0);
            horizon_reference_trial = reference ? trial : 0;
            update_horizon_quality(0);
            for (int frame = -3; frame < frames; ++frame)
            {
                update_horizon_quality(.19);
                azimuth = 1.3 + frame * .024;
                freeze_surface_time();
                const auto start = std::chrono::steady_clock::now();
                const double cpu_start = cpu_clock_ms();
                find_horizon();
                draw_horizon();
                const auto prepared = std::chrono::steady_clock::now();
                const double cpu = cpu_clock_ms() - cpu_start;
                submit_frame();
                const auto finished = std::chrono::steady_clock::now();
                if (frame >= 0)
                {
                    std::vector<unsigned char> pixels(render_width * render_height * 4);
                    glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    if (arm == 0) images.push_back(std::move(pixels));
                    else EXPECT_EQ(image_difference(pixels, images[frame]), 0) << scene << ' ' << round << ' ' << frame;
                    std::cout << "HORIZON_SAMPLE," << trial << ',' << scene << ",pan," << round << ','
                        << (reference ? "before" : "after") << ',' << frame << ',' << milliseconds(start, prepared) << ','
                        << cpu << ',' << milliseconds(start, finished) << ',' << precipitation_stats.ground_sample_stride << std::endl;
                    std::cout << "HORIZON_MAPPED_PAINT," << scene << ',' << round << ','
                        << (reference ? "before" : "after") << ',' << frame << ','
                        << precipitation_stats.ground_paint_milliseconds << '\n';
                }
                ImGui::NewFrame();
            }
        }
    }
    SDL_SetWindowSize(window, 192, 108);
}

TEST_F(HorizonTrialTest, PairedTreeDensityDuringTimeChanges)
{
    const char* requested = std::getenv("ALIENORUM_HORIZON_TRIAL");
    if (!requested || std::atoi(requested) != 207) GTEST_SKIP() << "Explicit plant performance trial only.";
    const char* rounds_arg = std::getenv("ALIENORUM_HORIZON_ROUNDS");
    const char* frames_arg = std::getenv("ALIENORUM_HORIZON_FRAMES");
    const int rounds = rounds_arg ? std::atoi(rounds_arg) : 8;
    const int frames = frames_arg ? std::atoi(frames_arg) : 6;
    ASSERT_GT(rounds, 0);
    ASSERT_GT(frames, 0);
    Planet* world = configure_scene("forest");
    delete world->surf_map;
    world->surf_map = new BlueBiomeTestMap(SurfaceMaterial::vegetation);
    world->plant_crown_size = 2;
    world->plant_crown_oblateness = .5;
    world->plant_trunk_width = .25;
    world->plant_trunk_height = 5;
    world->has_life = true;
    world->has_plants = true;
    set(show_clouds, false);
    set(horizon_reference_trial, 0u);
    set(simnow, 0.0);
    viewer_lat = .0001;
    viewer_lon = .4;
    const auto milliseconds = [](auto start, auto end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<std::vector<unsigned char>> images;
        for (int arm = 0; arm < 2; ++arm)
        {
            const bool reference = (arm == 0) != (round % 2 != 0);
            horizon_reference_trial = reference ? 207 : 0;
            bool drew_plants = false;
            for (int frame = -2; frame < frames; ++frame)
            {
                simnow = oneday * 80 + frame * 3600;
                azimuth = 1.3 + frame * .014;
                freeze_surface_time();
                const auto start = std::chrono::steady_clock::now();
                const double cpu_start = cpu_clock_ms();
                find_horizon();
                draw_horizon();
                const auto prepared = std::chrono::steady_clock::now();
                const double cpu = cpu_clock_ms() - cpu_start;
                submit_frame();
                const auto finished = std::chrono::steady_clock::now();
                if (frame >= 0)
                {
                    drew_plants |= plant_mesh_stats().instances > 0;
                    std::vector<unsigned char> pixels(render_width * render_height * 4);
                    glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    if (arm == 0) images.push_back(std::move(pixels));
                    else EXPECT_EQ(image_difference(pixels, images[frame]), 0) << round << ' ' << frame;
                    std::cout << "HORIZON_SAMPLE,207,forest,time," << round << ','
                        << (reference ? "before" : "after") << ',' << frame << ',' << milliseconds(start, prepared) << ','
                        << cpu << ',' << milliseconds(start, finished) << ',' << precipitation_stats.ground_sample_stride << std::endl;
                }
                ImGui::NewFrame();
            }
            EXPECT_TRUE(drew_plants);
        }
    }
    SDL_SetWindowSize(window, 192, 108);
}

TEST_F(HorizonTrialTest, PairedMappedReliefPanning)
{
    const char* requested = std::getenv("ALIENORUM_HORIZON_TRIAL");
    if (!requested) GTEST_SKIP() << "Explicit horizon performance trial only.";
    const unsigned trial = static_cast<unsigned>(std::atoi(requested));
    ASSERT_TRUE(trial == 203 || trial == 204 || trial == 205);
    const char* scene_arg = std::getenv("ALIENORUM_HORIZON_SCENE");
    const std::string scene = scene_arg ? scene_arg : "phoenix";
    const bool forest = scene == "forest";
    ASSERT_TRUE(scene == "phoenix" || scene == "shore" || (forest && trial == 204));
    const char* rounds_arg = std::getenv("ALIENORUM_HORIZON_ROUNDS");
    const int rounds = rounds_arg ? std::atoi(rounds_arg) : 8;
    const char* frames_arg = std::getenv("ALIENORUM_HORIZON_FRAMES");
    const int frames = frames_arg ? std::atoi(frames_arg) : 6;
    ASSERT_GT(rounds, 0);
    ASSERT_GT(frames, 0);

    Planet* world = configure_scene("forest");
    if (trial == 204)
    {
        render_width = 480;
        render_height = 300;
        SDL_SetWindowSize(window, render_width, render_height);
        SDL_PumpEvents();
        SDL_GL_SwapWindow(window);
        dispcx = render_width * .5;
        dispcy = render_height * .5;
        ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
        ImGui::EndFrame();
        ImGui::NewFrame();
    }
    delete world->surf_map;
    if (forest)
    {
        world->surf_map = new BlueBiomeTestMap(SurfaceMaterial::vegetation);
        world->plant_crown_size = 2;
        world->plant_crown_oblateness = .5;
        world->plant_trunk_width = .25;
        world->plant_trunk_height = 5;
    }
    else
    {
        world->surf_map = new Map(world);
        world->has_real_maps = true;
        world->sidereal_rotational_period = 86164.1;
        ASSERT_TRUE(world->surf_map->load_from_jpeg("maps/Earth_surf.jpg"));
        ASSERT_TRUE(world->surf_map->load_from_png("maps/Earth_bump.png", true, world->estimate_bump_scale()));
    }
    set(show_clouds, false);
    set(horizon_reference_trial, 0u);
    set(dragging, true);
    viewer_lat = forest ? .0001 : (scene == "shore" ? 31.782 : 33.448) * fiftyseventh;
    viewer_lon = forest ? .4 : (scene == "shore" ? -113.625 : -112.073) * fiftyseventh;
    altitude = scene == "shore" ? .12 : 0;
    struct RestoreHorizonQuality
    {
        ~RestoreHorizonQuality() { update_horizon_quality(0); }
    } restore_quality;

    const auto milliseconds = [](auto first, auto last)
    {
        return std::chrono::duration<double, std::milli>(last - first).count();
    };
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<std::vector<unsigned char>> first_images;
        for (int arm = 0; arm < 2; ++arm)
        {
            const bool reference = (arm == 0) != (round % 2 != 0);
            horizon_reference_trial = reference ? trial : 0;
            update_horizon_quality(0);
            bool drew_plant_meshes = false;
            for (int frame = -3; frame < frames; ++frame)
            {
                update_horizon_quality(.19);
                if (forest) viewer_lat = .0001 + frame / earth_radius;
                azimuth = (forest ? 1.3 : (scene == "shore" ? 255 : 70) * fiftyseventh) + frame * .024;
                freeze_surface_time();
                const auto start = std::chrono::steady_clock::now();
                const double cpu_start = cpu_clock_ms();
                find_horizon();
                draw_horizon();
                const auto prepared = std::chrono::steady_clock::now();
                const double cpu = cpu_clock_ms() - cpu_start;
                submit_frame();
                const auto finished = std::chrono::steady_clock::now();
                if (frame >= 0)
                {
                    drew_plant_meshes = drew_plant_meshes || plant_mesh_stats().instances > 0;
                    std::vector<unsigned char> pixels(render_width * render_height * 4);
                    glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    if (arm == 0) first_images.push_back(std::move(pixels));
                    else EXPECT_EQ(image_difference(pixels, first_images[frame]), 0) << scene << ' ' << round << ' ' << frame;
                    std::cout << "HORIZON_SAMPLE," << trial << ',' << scene << (forest ? ",walk," : ",pan,") << round << ','
                        << (reference ? "before" : "after") << ',' << frame << ',' << milliseconds(start, prepared) << ','
                        << cpu << ',' << milliseconds(start, finished) << ',' << precipitation_stats.ground_sample_stride << std::endl;
                }
                ImGui::NewFrame();
            }
            if (forest)
            {
                EXPECT_TRUE(drew_plant_meshes);
            }
        }
    }
    SDL_SetWindowSize(window, 192, 108);
}
