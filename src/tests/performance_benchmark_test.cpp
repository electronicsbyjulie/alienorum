// Developer-only paired benchmarks; ordinary test runs skip this suite.
#include "rendering_test.cpp"
#include "../performance_options.h"
#include "../classes/sound.h"
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <ctime>
#include <optional>
#include "../catalog_lock_trace.h"

struct CatalogPublicationProbe
{
    std::atomic<bool> active{false}, stop{false};
    std::thread worker;
    CatalogPublicationProbe() : worker([this]()
    {
        while (!stop.load())
        {
            if (active.load())
            {
                alienorum::CatalogLock lock(catalog_mutex);
                for (volatile unsigned int work = 0; work < 10000; ++work) {}
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }) {}
    ~CatalogPublicationProbe()
    {
        stop = true;
        worker.join();
    }
};

class ResidentTerrainProbe
{
    GLuint program = 0, vao = 0, cpu_texture = 0, texture = 0;
    inline static PFNGLCREATESHADERPROC create_shader = nullptr;
    inline static PFNGLSHADERSOURCEPROC shader_source = nullptr;
    inline static PFNGLCOMPILESHADERPROC compile_shader = nullptr;
    inline static PFNGLGETSHADERIVPROC get_shader = nullptr;
    inline static PFNGLUSEPROGRAMPROC use_program = nullptr;
    inline static PFNGLBINDVERTEXARRAYPROC bind_array = nullptr;
    inline static PFNGLCREATEPROGRAMPROC create_program = nullptr;
    inline static PFNGLATTACHSHADERPROC attach_shader = nullptr;
    inline static PFNGLLINKPROGRAMPROC link_program = nullptr;
    inline static PFNGLDELETESHADERPROC delete_shader = nullptr;
    inline static PFNGLGETPROGRAMIVPROC get_program = nullptr;
    inline static PFNGLGENVERTEXARRAYSPROC gen_arrays = nullptr;
    inline static PFNGLDELETEVERTEXARRAYSPROC delete_arrays = nullptr;
    inline static PFNGLDELETEPROGRAMPROC delete_program = nullptr;
    using Draw = void (APIENTRY *)(GLenum, GLint, GLsizei);
    Draw draw = nullptr;
    static GLuint shader(GLenum type, const char* text)
    {
        GLuint result = create_shader(type);
        shader_source(result, 1, &text, nullptr);
        compile_shader(result);
        GLint success = 0;
        get_shader(result, GL_COMPILE_STATUS, &success);
        if (!success) throw std::runtime_error("Resident terrain probe shader compilation failed.");
        return result;
    }
    static void callback(const ImDrawList*, const ImDrawCmd* command)
    {
        auto& probe = *static_cast<ResidentTerrainProbe*>(command->UserCallbackData);
        GLint depth_function;
        GLboolean depth_mask;
        glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glClear(GL_DEPTH_BUFFER_BIT);
        use_program(probe.program);
        bind_array(probe.vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, probe.texture);
        probe.draw(GL_TRIANGLES, 0, 511 * 39 * 6);
        glDepthFunc(depth_function);
        glDepthMask(depth_mask);
        glDisable(GL_DEPTH_TEST);
    }
    public:
    std::vector<std::array<double, 3>> positions;
    PerlinNoise noise{42};
    ResidentTerrainProbe()
    {
        GLint depth_bits = 0;
        glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
        std::cout << "PERF resident depth bits: " << depth_bits << '\n';
        if (!depth_bits) throw std::runtime_error("Resident terrain probe requires a depth attachment.");
        create_shader = reinterpret_cast<PFNGLCREATESHADERPROC>(SDL_GL_GetProcAddress("glCreateShader"));
        if (!create_shader) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        shader_source = reinterpret_cast<PFNGLSHADERSOURCEPROC>(SDL_GL_GetProcAddress("glShaderSource"));
        if (!shader_source) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        compile_shader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(SDL_GL_GetProcAddress("glCompileShader"));
        if (!compile_shader) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        get_shader = reinterpret_cast<PFNGLGETSHADERIVPROC>(SDL_GL_GetProcAddress("glGetShaderiv"));
        if (!get_shader) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        use_program = reinterpret_cast<PFNGLUSEPROGRAMPROC>(SDL_GL_GetProcAddress("glUseProgram"));
        if (!use_program) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        bind_array = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(SDL_GL_GetProcAddress("glBindVertexArray"));
        if (!bind_array) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        create_program = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(SDL_GL_GetProcAddress("glCreateProgram"));
        if (!create_program) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        attach_shader = reinterpret_cast<PFNGLATTACHSHADERPROC>(SDL_GL_GetProcAddress("glAttachShader"));
        if (!attach_shader) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        link_program = reinterpret_cast<PFNGLLINKPROGRAMPROC>(SDL_GL_GetProcAddress("glLinkProgram"));
        if (!link_program) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        delete_shader = reinterpret_cast<PFNGLDELETESHADERPROC>(SDL_GL_GetProcAddress("glDeleteShader"));
        if (!delete_shader) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        get_program = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(SDL_GL_GetProcAddress("glGetProgramiv"));
        if (!get_program) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        gen_arrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(SDL_GL_GetProcAddress("glGenVertexArrays"));
        if (!gen_arrays) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        delete_arrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(SDL_GL_GetProcAddress("glDeleteVertexArrays"));
        if (!delete_arrays) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        delete_program = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(SDL_GL_GetProcAddress("glDeleteProgram"));
        if (!delete_program) throw std::runtime_error("Resident terrain probe GL entry is unavailable.");
        const char* vertex = R"GLSL(#version 130
uniform sampler2D uNoise;
out vec3 color;
void main()
{
    const ivec2 corners[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1),
        ivec2(1, 0), ivec2(1, 1), ivec2(0, 1));
    int cell = gl_VertexID / 6;
    ivec2 node = ivec2(cell % 511, cell / 511) + corners[gl_VertexID % 6];
    vec2 value = texelFetch(uNoise, node, 0).rg;
    float relief = value.r + value.g;
    vec2 position = vec2(float(node.x) / 511.0 * 2.0 - 1.0, float(node.y) / 39.0 * 1.7 - .85);
    gl_Position = vec4(position.x, position.y + relief * .04, relief * .2, 1.0);
    color = vec3(.4 + relief * .1, .5 + relief * .08, .2);
}
)GLSL";
        const char* fragment = R"GLSL(#version 130
in vec3 color;
out vec4 result;
void main()
{
    result = vec4(color, 1.0);
}
)GLSL";
        GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
        program = create_program();
        attach_shader(program, vs);
        attach_shader(program, fs);
        link_program(program);
        delete_shader(vs);
        delete_shader(fs);
        GLint success = 0;
        get_program(program, GL_LINK_STATUS, &success);
        if (!success) throw std::runtime_error("Resident terrain probe linking failed.");
        gen_arrays(1, &vao);
        glGenTextures(1, &cpu_texture);
        draw = reinterpret_cast<Draw>(SDL_GL_GetProcAddress("glDrawArrays"));
        if (!draw) throw std::runtime_error("Resident terrain probe draw entry is unavailable.");
        positions.resize(512 * 40);
    }
    ~ResidentTerrainProbe()
    {
        glDeleteTextures(1, &cpu_texture);
        delete_arrays(1, &vao);
        delete_program(program);
    }
    void prepare(int frame, bool moving)
    {
        for (int row = 0; row < 40; ++row)
        {
            for (int column = 0; column < 512; ++column)
            {
                const double angle = column * 2 * _pi / 512;
                const double latitude = .3 + row * .0001 + (moving ? frame * 1e-7 : 0);
                positions[row * 512 + column] = {127 * cos(latitude) * cos(angle),
                    127 * cos(latitude) * sin(angle), 127 * sin(latitude)};
            }
        }
        if (performance_enabled(28))
        {
            HorizonMeshResident resident;
            if (!generate_horizon_mesh_gpu_resident(positions, noise, resident))
                throw std::runtime_error("Resident terrain generation is unavailable.");
            if (horizon_mesh_gpu_stats().readback_bytes != 0)
                throw std::runtime_error("Resident terrain generation performed a CPU transfer.");
            texture = resident.texture;
        }
        else
        {
            std::vector<float> values;
            values.reserve(positions.size() * 2);
            for (const auto& point : positions)
            {
                double total = 0, amplitude = 1, frequency = 1;
                for (int octave = 0; octave < 12; ++octave)
                {
                    total += noise.noise(point[0] * frequency, point[1] * frequency, point[2] * frequency) * amplitude;
                    amplitude *= .8559;
                    frequency *= 1.29;
                }
                const float high = static_cast<float>(total);
                values.push_back(high);
                values.push_back(static_cast<float>(total - high));
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, cpu_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, 0x8230, 512, 40, 0, 0x8227, GL_FLOAT, values.data());
            texture = cpu_texture;
        }
        ImGui::GetBackgroundDrawList()->AddCallback(callback, this);
        ImGui::GetBackgroundDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
};

class PerformanceBenchmarkTest : public RenderingRegressionTest
{
    protected:
    std::unique_ptr<bool[]> orbit_discs;
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
#ifdef ALIENORUM_PERFORMANCE_FIXED
    std::cout << "PERF comparison: fixed\n";
#endif
    std::cout << "PERF renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "PERF viewport: " << drawable_width << 'x' << drawable_height << '\n';
    const char* output = std::getenv("ALIENORUM_PERF_OUTPUT");
    ASSERT_NE(output, nullptr);
    std::ofstream csv(output);
    ASSERT_TRUE(csv.good());
    csv << "proposal,workload,round,order,before_ms,after_ms,change_percent,max_channel_error,before_cpu_ms,after_cpu_ms,before_max_ms,after_max_ms\n";
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
        catalog |= id <= 18 || id == 68 || id == 73 || id == 77;
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
    if (std::find(ids.begin(), ids.end(), 7) != ids.end())
    {
        for (int i = 0; i < 24; ++i)
        {
            Planet* body = make_planet(static_cast<Star*>(cels[0]), "Active eclipse body", (2 + i * .5) * AU);
            body->orbit->inclination = i * .01;
        }
    }
    if (std::find(ids.begin(), ids.end(), 68) != ids.end())
    {
        for (int i = 0; i < 8; ++i)
        {
            Planet* body = make_planet(static_cast<Star*>(cels[0]), "Orbit benchmark body", (1.5 + i * .7) * AU);
            body->orbit->inclination = i * .03;
        }
        orbit_discs.reset(new bool[ncelobjs + 32]{});
        set(discinstead, orbit_discs.get());
        set(show_orbits, true);
        set(show_localsys, true);
        set(show_labels, false);
        set(show_sats, false);
        for (int i = 0; i < ncelobjs; ++i) cels[i]->looked_for_maps = true;
    }
    if (std::find(ids.begin(), ids.end(), 77) != ids.end())
    {
        orbit_discs.reset(new bool[ncelobjs + 64]{});
        set(discinstead, orbit_discs.get());
        set(show_orbits, false);
        set(show_localsys, true);
        set(show_labels, false);
        for (int i = 0; i < ncelobjs; ++i) cels[i]->looked_for_maps = true;
    }
    int benchmark_galaxy_index = -1;
    if (std::find(ids.begin(), ids.end(), 67) != ids.end() || std::find(ids.begin(), ids.end(), 77) != ids.end())
    {
        Galaxy* galaxy = new Galaxy();
        strcpy(galaxy->name, "M31");
        galaxy->volumetric_mean_radius = 50000 * light_year;
        galaxy->tmprel = Point(10 * light_year, 0, 0);
        galaxy->location.local_system_plane.v = xaxis;
        galaxy->location.local_system_plane.a = .6;
        append_cel(galaxy);
        benchmark_galaxy_index = galaxy->seqno;
        set(inside_galaxy_idx, galaxy->seqno);
        set(show_galaxy_band, true);
        set(sky_mag_shift, 0.0);
        ASSERT_NE(gputex_galaxy_internal(galaxy->name), 0u);
    }
    if (std::find(ids.begin(), ids.end(), 73) != ids.end())
    {
        set(is_click, false);
        set(dragged, false);
    }
    set(play_rise_set_sound, false);
    auto& alerts = SoundManager::get_instance();
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
    std::unique_ptr<ResidentTerrainProbe> resident_probe;
    if (std::find(ids.begin(), ids.end(), 28) != ids.end())
    {
        set(horizon_mesh_gpu_reference, false);
        resident_probe = std::make_unique<ResidentTerrainProbe>();
    }
    std::unique_ptr<CatalogPublicationProbe> publication;
    const double base_latitude = 0, base_longitude = .3;
    for (int id : ids)
    {
        std::vector<std::string> scenes = id == 6 ? std::vector<std::string>{"catalog-alerts-none", "catalog-alerts-armed"}
            : id == 7 ? std::vector<std::string>{"catalog-eclipse", "catalog-eclipse-time-jump"}
            : id == 12 ? std::vector<std::string>{"catalog-observer-still", "catalog-observer-walk"}
            : id == 18 ? std::vector<std::string>{"catalog-lock-idle", "catalog-lock-publication"}
            : id == 13 ? std::vector<std::string>{"catalog", "catalog-walk", "catalog-time-jump"}
            : id <= 5 || id == 8 ? std::vector<std::string>{"catalog", "catalog-time-jump"}
            : id == 77 ? std::vector<std::string>{"forest-walk", "shore-walk", "galaxy-shore-still", "skyline-walk", "catalog-scratch"}
            : id == 73 ? std::vector<std::string>{"catalog-pick-empty", "catalog-pick-moving", "catalog-pick-target"}
            : id == 68 ? std::vector<std::string>{"catalog-orbits", "catalog-orbits-walk", "catalog-orbits-time-jump"}
            : id == 67 ? std::vector<std::string>{"galaxy-shore-walk", "galaxy-shore-still", "galaxy-shore-up", "galaxy-shore-down", "galaxy-shore-drag"}
            : id == 49 ? std::vector<std::string>{"forest-still", "forest-walk", "forest-turn", "shore-still"}
            : id == 43 ? std::vector<std::string>{"forest-walk", "forest-still", "forest-turn"}
            : id == 37 ? std::vector<std::string>{"shore-walk", "forest-walk", "shore-still", "shore-pole", "shore-seam"}
            : id == 36 ? std::vector<std::string>{"shore-walk", "forest-walk"}
            : id == 30 ? std::vector<std::string>{"shore-walk", "forest-walk", "ocean-still"}
            : id == 29 ? std::vector<std::string>{"shore-up", "shore-walk", "shore-down"}
            : id == 28 ? std::vector<std::string>{"resident-mesh-still", "resident-mesh-walk"}
            : id == 25 ? std::vector<std::string>{"shore-still", "forest-still", "shore-walk", "forest-walk"}
            : id == 21 ? std::vector<std::string>{"skyline-walk", "skyline-still"}
            : id == 31 ? std::vector<std::string>{"shore-walk", "forest-refresh"}
            : id == 42 ? std::vector<std::string>{"forest-walk", "forest-still"}
            : id == 50 || id == 51 || id == 52 ? std::vector<std::string>{"ocean-still", "shore-walk", "forest-walk"}
            : std::vector<std::string>{"shore-walk", "forest-walk"};
        for (const std::string& scene : scenes)
        {
            if (publication) publication->active = scene == "catalog-lock-publication";
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
            altitude = scene.find("shore-up") != std::string::npos ? .9 : scene.find("shore-down") != std::string::npos ? -.9 : -.15;
            dragging = scene == "galaxy-shore-drag";
            azimuth = .7;
            zoom = 1;
            [[maybe_unused]] float captured_background_clip = dispcy * 2;
            const auto frame = [&](int frame_index, bool capture)
            {
                std::optional<CatalogLock> frame_lock;
                if (id == 18) frame_lock.emplace(catalog_mutex, CatalogLockSite::frame);
                viewer_lat = scene == "shore-pole" ? half_pi - 1e-6 : base_latitude;
                viewer_lon = scene == "shore-seam" ? _pi - 1e-6 : base_longitude;
                if (scene.find("walk") != std::string::npos) viewer_lat += frame_index * .25 / earth_radius;
                if (scene == "forest-refresh") world->surf_map->touch_gen();
                if (scene == "forest-turn") azimuth = .7 + frame_index * .13;
                freeze_surface_time();
                ImGui::GetCurrentContext()->Time = 50 + frame_index / 30.0;
                redo_proper_motions = scene.find("time-jump") != std::string::npos;
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
                if (id == 67 || id == 77)
                {
                    inside_galaxy_idx = benchmark_galaxy_index;
                    cels[benchmark_galaxy_index]->tmprel = Point(10 * light_year, 0, 0);
                    sky_mag_shift = 0;
                }
                if (scene.rfind("catalog", 0) == 0 || id == 28)
                {
                    // Exercise all three consumers, including pointer/filter traversal.
                    volatile size_t count = 0;
                    if (id != 73) for (const auto* list : {&frame_draw_candidates(), &frame_bright_candidates(), &frame_pick_candidates()})
                    {
                        for (int index : *list)
                        {
                            const auto* object = cels[index];
                            count += !object->deleted && object->drawnx > -1e8 && vmag_cache[index] < 5;
                        }
                    }
                    (void)count;
                    if (id == 73)
                    {
                        auto& io = ImGui::GetIO();
                        io.MousePos = scene == "catalog-pick-empty" ? ImVec2(-20, -20)
                            : scene == "catalog-pick-target" ? ImVec2(cels[0]->drawnx, cels[0]->drawny)
                            : ImVec2((frame_index * 37 + 640) % render_width, (frame_index * 29 + 360) % render_height);
                        identify_object_under_cursor(io);
                    }
                    if (id == 6) alerts.check_rise_set_alerts();
                    if (id == 68 || id == 77)
                    {
                        labels2draw.clear();
                        draw_objects();
                    }
                }
                else
                {
                    luminous_flux = 4e10;
                    find_horizon();
                    if (id == 67 || id == 77)
                    {
#ifdef PERF_P067
                        if (capture) captured_background_clip = horizon_background_clip_y();
#endif
                        const int band_start = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
                        draw_galaxy_band();
                        if (capture) std::cout << "PERF galaxy-band vertices: "
                            << ImGui::GetBackgroundDrawList()->VtxBuffer.Size - band_start << '\n';
                    }
                    draw_horizon();
                }
                if (id == 28) resident_probe->prepare(frame_index, scene.find("walk") != std::string::npos);
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
                        if (id == 73)
                        {
                            append(is_an_obj_under_cursor); append(selected); append(obj_magn_under_cursor);
                        }
                        if (id == 12)
                        {
                            for (const Point& point : {here.galactic_center, here.system_center,
                                here.local_position, here.equatorial_plane.v})
                            {
                                append(point.x); append(point.y); append(point.z);
                            }
                            append(here.equatorial_plane.a);
                            append(npaz); append(azimuth_correction);
                        }
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
                if (id == 36)
                {
                    const auto idle_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
                    if (performance_enabled(36)) prefetch_surface_tiles(SURFACE_PREFETCH_BUDGET_MS);
                    std::this_thread::sleep_until(idle_end);
                }
                return std::vector<unsigned char>();
            };
            const auto select = [&](bool before)
            {
                if (id == 18) publication.reset();
                performance_reference.fill(false);
                performance_reference[id] = before;
                invalidate_frame_objects();
                if (id == 18)
                {
                    publication = std::make_unique<CatalogPublicationProbe>();
                    publication->active = scene == "catalog-lock-publication";
                }
                if (id == 6)
                {
                    const bool armed = scene == "catalog-alerts-armed";
                    cels[0]->alert_rise = armed;
                    cels[1]->alert_set = armed;
                    alerts.reset_horizon_tracking();
                }
                frame(-2, false);
                frame(-1, false);
            };
            select(true);
            const auto expected = frame(0, true);
            select(false);
            const auto actual = frame(0, true);
#ifdef PERF_P067
            if (id == 67) std::cout << "PERF background clip row: " << captured_background_clip << '\n';
#endif
#ifdef PERF_P068
            if (id == 68) std::cout << "PERF orbit candidates: " << frame_orbit_candidates().size() << '\n';
#endif
#ifdef PERF_P049
            if (id == 49) std::cout << "PERF scenery projection cache hits: " << scenery_projection_cache_hits() << '\n';
#endif
#ifdef PERF_P037
            if (id == 37) std::cout << "PERF local tile nodes: " << local_surface_tile_sample_count() << '\n';
#endif
            ASSERT_EQ(actual.size(), expected.size());
            std::uint64_t image_hash = 14695981039346656037ULL;
            for (unsigned char byte : actual)
            {
                image_hash ^= byte;
                image_hash *= 1099511628211ULL;
            }
            std::cout << "PERF IMAGE P" << id << ' ' << scene << ' ' << image_hash << '\n';
            if (id == 42)
            {
                if (const char* prefix = std::getenv("ALIENORUM_PERF_CAPTURE_PREFIX"))
                {
                    std::ofstream capture(std::string(prefix) + ".P" + std::to_string(id) + "." + scene + ".bin", std::ios::binary);
                    capture.write(reinterpret_cast<const char*>(actual.data()), actual.size());
                    ASSERT_TRUE(capture.good());
                }
            }
            int maximum = 0;
            for (size_t i = 0; i < actual.size(); ++i)
                maximum = std::max(maximum, abs(int(actual[i]) - int(expected[i])));
            if (id == 26)
            {
                const auto& stats = horizon_terrain_lod_stats();
                std::cout << "PERF P26 quality max_channel_error=" << maximum
                    << " patches=" << stats.patches << " accepted=" << stats.accepted_patches
                    << " triangles_removed=" << stats.triangles_removed
                    << " horizon_segments_removed=" << stats.horizon_segments_removed << '\n';
            }
            if (!(id == 26 && std::getenv("ALIENORUM_PERF_MEASURE_REJECTED_LOD")))
            {
                ASSERT_LE(maximum, id == 42 ? 2 : 0) << "P" << id << ' ' << scene;
            }
            for (int round = 0; round < rounds; ++round)
            {
                double milliseconds[2] = {}, cpu_times[2] = {}, maximum_times[2] = {};
                for (int order = 0; order < 2; ++order)
                {
                    const int mode = (round + order) % 2;
                    select(mode == 0);
                    const auto start = std::chrono::steady_clock::now();
                    const double cpu_start = cpu_milliseconds();
                    for (int sample = 0; sample < frames; ++sample)
                    {
                        const auto frame_start = (id == 36 || id == 37) ? std::chrono::steady_clock::now() : start;
                        frame(sample, false);
                        if (id == 36 || id == 37) maximum_times[mode] = std::max(maximum_times[mode],
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count());
                    }
                    cpu_times[mode] = (cpu_milliseconds() - cpu_start) / frames;
                    milliseconds[mode] = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count() / frames;
                }
                csv << id << ',' << scene << ',' << round << ',' << (round % 2 ? "BA" : "AB") << ','
                    << milliseconds[0] << ',' << milliseconds[1] << ','
                    << (milliseconds[1] / milliseconds[0] - 1) * 100 << ',' << maximum << ','
                    << cpu_times[0] << ',' << cpu_times[1];
                if (id == 36 || id == 37) csv << ',' << maximum_times[0] << ',' << maximum_times[1];
                else csv << ",,";
                csv << '\n';
                csv.flush();
                std::cout << "PERF P" << id << ' ' << scene << " pair " << round + 1 << "/" << rounds
                    << ": " << milliseconds[0] << " -> " << milliseconds[1] << " ms\n" << std::flush;
            }
        }
    }
    publication.reset();
    print_catalog_lock_trace(std::cout);
    alerts.leave_horizon();
    performance_reference.fill(false);
    invalidate_frame_objects();
    ImGui::EndFrame();
    SDL_SetWindowSize(window, 192, 108);
    ImGui::GetIO().DisplaySize = ImVec2(192, 108);
    ImGui::NewFrame();
}
