// Developer-only paired benchmarks; ordinary test runs skip this suite.
#include "rendering_test.cpp"
#include "../performance_options.h"
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <ctime>

class PerformanceBenchmarkTest : public RenderingRegressionTest
{
    protected:
    static double cpu_milliseconds()
    {
#ifdef CLOCK_THREAD_CPUTIME_ID
        timespec stamp;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stamp) == 0)
            return stamp.tv_sec * 1000.0 + stamp.tv_nsec / 1e6;
#endif
        return std::clock() * 1000.0 / CLOCKS_PER_SEC;
    }
    void finish_frame()
    {
        ImGui::Render();
        glViewport(0, 0, render_width, render_height);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        using Finish = void (*)();
        static auto finish = reinterpret_cast<Finish>(SDL_GL_GetProcAddress("glFinish"));
        finish();
        ImGui::NewFrame();
    }
    static int setting(const char* name, int fallback)
    {
        const char* value = std::getenv(name);
        return value ? std::max(1, atoi(value)) : fallback;
    }
    static std::vector<int> cases()
    {
        const char* list = std::getenv("ALIENORUM_PERF_CASES");
        if (!list) return {1, 2, 3, 4, 5, 20, 21, 22, 23, 24, 31, 39, 40, 41, 42, 50, 51, 52};
        std::stringstream text(list);
        std::vector<int> ids;
        std::string token;
        while (std::getline(text, token, ',')) ids.push_back(atoi(token.c_str()));
        return ids;
    }
    void viewport()
    {
        ImGui::EndFrame();
        render_width = setting("ALIENORUM_PERF_WIDTH", 1280);
        render_height = setting("ALIENORUM_PERF_HEIGHT", 720);
        SDL_SetWindowSize(window, render_width, render_height);
        ImGui::GetIO().DisplaySize = ImVec2(render_width, render_height);
        dispcx = render_width * .5;
        dispcy = render_height * .5;
        ImGui::NewFrame();
        set(scenery_gpu_reference, false);
        set(horizon_ground_gpu_reference, false);
        set(selected, -1);
        set(trackidx, -1);
        set(editidx, -1);
        set(simnow, double(J2000_TIME_T + 24 * oneyear));
        set(JDnow, J2000 + 24 * 365.25);
        set(redo_proper_motions, true);
    }
};

TEST_F(PerformanceBenchmarkTest, PairedProductionPaths)
{
#ifndef ALIENORUM_PERFORMANCE_BENCHMARK
    GTEST_SKIP() << "Compile the private benchmark target with ALIENORUM_PERFORMANCE_BENCHMARK.";
#endif
    if (!std::getenv("ALIENORUM_RUN_PERF")) GTEST_SKIP() << "Set ALIENORUM_RUN_PERF=1.";
    viewport();
    int drawable_width = 0, drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    ASSERT_EQ(drawable_width, render_width);
    ASSERT_EQ(drawable_height, render_height);
    std::cout << "PERF renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "PERF viewport: " << drawable_width << 'x' << drawable_height << '\n';
    const char* output = std::getenv("ALIENORUM_PERF_OUTPUT");
    ASSERT_NE(output, nullptr);
    std::ofstream csv(output);
    ASSERT_TRUE(csv.good());
    csv << "proposal,workload,round,order,before_ms,after_ms,change_percent,max_channel_error,before_cpu_ms,after_cpu_ms\n";
    csv << std::setprecision(12);
    Planet* world = surface(true);
    world->sidereal_rotational_period = oneday;
    // surface() sets the small correctness-test viewport; restore benchmark dimensions.
    dispcx = render_width * .5;
    dispcy = render_height * .5;
    const int rounds = setting("ALIENORUM_PERF_ROUNDS", 5);
    const int frames = setting("ALIENORUM_PERF_FRAMES", 8);
    const auto ids = cases();
    bool catalog = false;
    for (int id : ids)
    {
        ASSERT_GT(id, 0);
        ASSERT_LT(static_cast<size_t>(id), performance_reference.size());
        catalog |= id <= 5;
    }
    if (catalog)
    {
        const int bodies = setting("ALIENORUM_PERF_POPULATION", 12000);
        for (int i = 0; i < bodies / 100; ++i)
        {
            Star* host = make_star("Benchmark remote host");
            host->right_ascension = .1 + i * .05;
            host->distance = (50 + i) * parsec;
            host->location.local_position = host->tmprel = Point(1e16, 1e16, 1e16 + i * 1e11);
            for (int body = 0; body < 100; ++body) make_planet(host);
        }
        const int stars = setting("ALIENORUM_PERF_STARS", 30000);
        for (int i = 0; i < stars; ++i)
        {
            Star* star = make_star("Benchmark background");
            star->right_ascension = fmod(i * 2.399963229728653, 2 * _pi);
            star->declination = asin(2 * ((i * .618033988749895) - floor(i * .618033988749895)) - 1);
            star->distance = (10 + i % 1000) * parsec;
            star->absolute_magnitude = 4 + i % 9;
            star->BV_color = (i % 100) / 50.0;
        }
    }
    surface_radii.assign(ncelobjs + 32, 0);
    system_bloom.assign(ncelobjs + 32, 0);
    set(angular_radius, surface_radii.data());
    set(bloomrad_cache, system_bloom.data());
    surface_magnitudes.resize(ncelobjs + 32, 999);
    vmag_cache = surface_magnitudes.data();
    surface_magnitudes[0] = -26;
    surface_magnitudes[1] = -20;
    Planet::reconcile_host_statistics();
    if (catalog) compute_object_draw_coordinates();
    const double base_latitude = 0, base_longitude = .3;
    for (int id : ids)
    {
        std::vector<std::string> scenes = id <= 5 ? std::vector<std::string>{"catalog", "catalog-time-jump"}
            : id == 21 ? std::vector<std::string>{"skyline-walk", "skyline-still"}
            : id == 31 ? std::vector<std::string>{"shore-walk", "forest-refresh"}
            : id == 42 ? std::vector<std::string>{"forest-walk", "forest-still"}
            : id == 50 || id == 51 || id == 52 ? std::vector<std::string>{"ocean-still", "shore-walk", "forest-walk"}
            : std::vector<std::string>{"shore-walk", "forest-walk"};
        for (const std::string& scene : scenes)
        {
            delete world->surf_map;
            const bool skyline = scene.find("skyline") != std::string::npos;
            const bool forest = scene.find("forest") != std::string::npos;
            world->surf_map = forest ? static_cast<Map*>(new BlueBiomeTestMap(SurfaceMaterial::vegetation))
                : scene.find("ocean") != std::string::npos ? static_cast<Map*>(new BlueBiomeTestMap(SurfaceMaterial::water))
                : skyline ? static_cast<Map*>(new MountainBandsTestMap()) : static_cast<Map*>(new CoastalTestMap());
            world->has_water = skyline ? 0 : 1;
            world->vegetation_r = skyline ? 0 : 80;
            world->vegetation_g = skyline ? 0 : 140;
            world->vegetation_b = skyline ? 0 : 30;
            world->plant_crown_size = 2.85;
            world->plant_trunk_height = 22.2;
            world->plant_crown_oblateness = .35;
            show_terrain = true;
            altitude = -.15;
            azimuth = .7;
            zoom = 1;
            const auto frame = [&](int frame_index, bool capture)
            {
                viewer_lat = base_latitude;
                viewer_lon = base_longitude;
                if (scene.find("walk") != std::string::npos) viewer_lat += frame_index * .25 / earth_radius;
                if (scene == "forest-refresh") world->surf_map->touch_gen();
                freeze_surface_time();
                ImGui::GetCurrentContext()->Time = 50 + frame_index / 30.0;
                redo_proper_motions = scene == "catalog-time-jump";
                simnow = J2000_TIME_T + 24 * oneyear + (redo_proper_motions ? frame_index * 10.0 : 0);
                JDnow = J2000 + (simnow - J2000_TIME_T) / oneday;
                if (scene.rfind("catalog", 0) != 0)
                {
                    // A stable, visible secondary source exercises ordinary glint colors.
                    const Point ray = rotate3D(Point(0, AU * .5, AU), center,
                        here.equatorial_plane.v, -here.equatorial_plane.a);
                    cels[1]->location = here;
                    cels[1]->location.local_position += ray;
                }
                compute_object_draw_coordinates();
                if (scene.rfind("catalog", 0) == 0)
                {
                    // Exercise all three consumers, including pointer/filter traversal.
                    volatile size_t count = 0;
                    for (const auto* list : {&frame_draw_candidates(), &frame_bright_candidates(), &frame_pick_candidates()})
                    {
                        for (int index : *list)
                        {
                            const auto* object = cels[index];
                            count += !object->deleted && object->drawnx > -1e8 && vmag_cache[index] < 5;
                        }
                    }
                    (void)count;
                }
                else
                {
                    luminous_flux = 4e10;
                    find_horizon();
                    draw_horizon();
                }
                if (capture)
                {
                    auto pixels = render();
                    if (scene.rfind("catalog", 0) == 0)
                    {
                        const auto append = [&](double value)
                        {
                            const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
                            pixels.insert(pixels.end(), bytes, bytes + sizeof(value));
                        };
                        for (int index = 0; index < ncelobjs; ++index)
                        {
                            for (const Point& point : {cels[index]->location.galactic_center,
                                cels[index]->location.system_center, cels[index]->location.local_position,
                                cels[index]->tmprel})
                            {
                                append(point.x); append(point.y); append(point.z);
                            }
                            append(vmag_cache[index]);
                            append(cels[index]->drawnx); append(cels[index]->drawny);
                        }
                    }
                    ImGui::NewFrame();
                    return pixels;
                }
                finish_frame();
                return std::vector<unsigned char>();
            };
            const auto select = [&](bool before)
            {
                performance_reference.fill(false);
                performance_reference[id] = before;
                invalidate_frame_objects();
                frame(-2, false);
                frame(-1, false);
            };
            select(true);
            const auto expected = frame(0, true);
            select(false);
            const auto actual = frame(0, true);
            int maximum = 0;
            for (size_t i = 0; i < actual.size(); ++i)
                maximum = std::max(maximum, abs(int(actual[i]) - int(expected[i])));
            ASSERT_LE(maximum, id == 42 ? 2 : 0) << "P" << id << ' ' << scene;
            for (int round = 0; round < rounds; ++round)
            {
                double milliseconds[2] = {}, cpu_times[2] = {};
                for (int order = 0; order < 2; ++order)
                {
                    const int mode = (round + order) % 2;
                    select(mode == 0);
                    const auto start = std::chrono::steady_clock::now();
                    const double cpu_start = cpu_milliseconds();
                    for (int sample = 0; sample < frames; ++sample) frame(sample, false);
                    cpu_times[mode] = (cpu_milliseconds() - cpu_start) / frames;
                    milliseconds[mode] = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count() / frames;
                }
                csv << id << ',' << scene << ',' << round << ',' << (round % 2 ? "BA" : "AB") << ','
                    << milliseconds[0] << ',' << milliseconds[1] << ','
                    << (milliseconds[1] / milliseconds[0] - 1) * 100 << ',' << maximum << ','
                    << cpu_times[0] << ',' << cpu_times[1] << '\n';
                csv.flush();
                std::cout << "PERF P" << id << ' ' << scene << " pair " << round + 1 << "/" << rounds
                    << ": " << milliseconds[0] << " -> " << milliseconds[1] << " ms\n" << std::flush;
            }
        }
    }
    performance_reference.fill(false);
    invalidate_frame_objects();
    ImGui::EndFrame();
    SDL_SetWindowSize(window, 192, 108);
    ImGui::GetIO().DisplaySize = ImVec2(192, 108);
    ImGui::NewFrame();
}
