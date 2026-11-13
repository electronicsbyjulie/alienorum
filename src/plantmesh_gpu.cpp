#include "plantmesh_gpu.h"
#include "scenery_gpu.h"
#include "horizon_performance.h"
#include "classes/plantmesh.h"
#include "SDL.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <iostream>

namespace alienorum
{
    namespace
    {
        using DrawInstanced = void (APIENTRYP)(GLenum, GLint, GLsizei, GLsizei);
        using Divisor = void (APIENTRYP)(GLuint, GLuint);
        using BindAttribute = void (APIENTRYP)(GLuint, GLuint, const char*);
        using Uniform4 = void (APIENTRYP)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
        using GetBoolean = void (APIENTRYP)(GLenum, GLboolean*);
        using GetDouble = void (APIENTRYP)(GLenum, GLdouble*);
        using EnumState = void (APIENTRYP)(GLenum);
        using BoolState = void (APIENTRYP)(GLboolean);
        using DoubleState = void (APIENTRYP)(GLdouble);
        GetBoolean get_boolean = nullptr;
        GetDouble get_double = nullptr;
        EnumState depth_func = nullptr, cull_mode = nullptr, front_mode = nullptr;
        BoolState depth_write = nullptr;
        DoubleState clear_depth_value = nullptr;
        constexpr GLenum depth_bits_key = 0x0D56, static_draw = 0x88E4, depth_function_key = 0x0B74;
        constexpr GLenum depth_mask_key = 0x0B72, front_face_key = 0x0B46, cull_face_key = 0x0B45;
        constexpr GLenum clear_depth_key = 0x0B73, less_equal = 0x0203, clockwise = 0x0900;
        constexpr GLbitfield depth_buffer_bit = 0x00000100;
        DrawInstanced draw_instanced = nullptr;
        Divisor divisor = nullptr;
        Uniform4 uniform4 = nullptr;
        SDL_GLContext context = nullptr;
        GLuint program = 0, vao = 0, mesh = 0, instances = 0, terrain_texture = 0;
        bool attempted = false, first_draw = true;
        std::array<GLint, 6> locations{};
        std::array<int, 3> mesh_first{}, mesh_count{};
        struct Run { size_t first, count; int level; };
        struct Batch
        {
            std::vector<PlantMeshInstance> plants;
            std::vector<Run> runs;
            PlantMeshView view;
            size_t upload_offset = 0;
        };
        std::deque<Batch> batches;
        std::vector<PlantMeshInstance> upload;
        size_t batch_index = 0;
        PlantMeshView current_view;
        std::vector<float> saved_terrain;
        int terrain_width = 0, terrain_height = 0;
        bool terrain_active = false;
        PlantMeshStats stats;

        const char* vertex_source = R"GLSL(
#version 130
in vec4 aPosition;
in vec3 aNormal;
in vec4 aBase;
in vec4 aDimensions;
in vec4 aCrown;
in vec4 aTrunk;
in vec4 aHaze;
uniform vec4 uCamera;
uniform vec4 uPitch;
out vec3 normal;
out vec3 world;
out vec4 material;
out vec4 haze;
out vec2 exposure;
void main()
{
    bool crown = aPosition.w > .5;
    float height = max(0.0, aBase.w - aDimensions.y);
    vec3 local;
    if (crown)
    {
        local = aPosition.xyz * vec3(aDimensions.x, aDimensions.y, aDimensions.x);
        local.y += aBase.w;
        normal = aNormal / max(vec3(aDimensions.x, aDimensions.y, aDimensions.x), vec3(.0001));
        exposure = vec2(.65, 2.5);
    }
    else
    {
        float radius = mix(aDimensions.z, aDimensions.w, aPosition.y);
        local = vec3(aPosition.x * radius, aPosition.y * height, aPosition.z * radius);
        normal = abs(aNormal.y) > .5 ? aNormal
            : vec3(aNormal.x, (aDimensions.z - aDimensions.w) / max(height, .0001), aNormal.z);
        exposure = vec2(.70, 2.3);
    }
    world = aBase.xyz + local;
    vec3 camera = vec3(world.x, world.y * uPitch.y - world.z * uPitch.x,
        world.y * uPitch.x + world.z * uPitch.y);
    gl_Position = vec4(camera.x * uCamera.x / uCamera.y,
        camera.y * uCamera.x / uCamera.z, camera.z - .4, camera.z);
    material = crown ? aCrown : aTrunk;
    haze = aHaze;
}
)GLSL";
        const char* fragment_source = R"GLSL(
#version 130
in vec3 normal;
in vec3 world;
in vec4 material;
in vec4 haze;
in vec2 exposure;
uniform vec4 uLight;
uniform vec4 uAirlight;
uniform vec4 uSettings;
uniform vec4 uPitch;
uniform sampler2D uTerrain;
out vec4 FragColor;
void main()
{
    if (uSettings.y > .5)
    {
        ivec2 pixel = ivec2(gl_FragCoord.x / uPitch.z, uSettings.z - gl_FragCoord.y / uPitch.w);
        pixel = clamp(pixel, ivec2(0), textureSize(uTerrain, 0) - ivec2(1));
        float distance = texelFetch(uTerrain, pixel, 0).r;
        if (distance > 0.0 && length(world.xz) > distance + .3) discard;
    }
    float lighting = uSettings.x * exposure.x + uLight.w * exposure.y
        * max(0.0, dot(normalize(normal), uLight.xyz));
    vec3 color = mix(uAirlight.rgb, clamp(material.rgb * lighting, 0.0, 1.0), haze.rgb);
    if (uSettings.w > .5) color = vec3(min(1.0, color.r + .5 * color.g + .3 * color.b), color.g * .333, color.b * .333);
    FragColor = vec4(color, material.a * haze.a);
}
)GLSL";

        GLuint compile(GLenum type, const char* source)
        {
            const GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint success = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (success) return shader;
            char message[1024]{};
            glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
            std::cerr << "Plant mesh shader: " << message << '\n';
            glDeleteShader(shader);
            return 0;
        }

        bool initialize()
        {
            if (context != SDL_GL_GetCurrentContext()) clear_plant_mesh_gpu();
            context = SDL_GL_GetCurrentContext();
            if (attempted) return program != 0;
            attempted = true;
            int major = 0, minor = 0;
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
            if (!(major > 3 || (major == 3 && minor >= 3))
                && (!SDL_GL_ExtensionSupported("GL_ARB_instanced_arrays")
                    || !SDL_GL_ExtensionSupported("GL_ARB_draw_instanced"))) return false;
            get_boolean = reinterpret_cast<GetBoolean>(SDL_GL_GetProcAddress("glGetBooleanv"));
            get_double = reinterpret_cast<GetDouble>(SDL_GL_GetProcAddress("glGetDoublev"));
            depth_func = reinterpret_cast<EnumState>(SDL_GL_GetProcAddress("glDepthFunc"));
            depth_write = reinterpret_cast<BoolState>(SDL_GL_GetProcAddress("glDepthMask"));
            cull_mode = reinterpret_cast<EnumState>(SDL_GL_GetProcAddress("glCullFace"));
            front_mode = reinterpret_cast<EnumState>(SDL_GL_GetProcAddress("glFrontFace"));
            clear_depth_value = reinterpret_cast<DoubleState>(SDL_GL_GetProcAddress("glClearDepth"));
            if (!get_boolean || !get_double || !depth_func || !depth_write || !cull_mode || !front_mode || !clear_depth_value) return false;
            GLint depth_bits = 0;
            glGetIntegerv(depth_bits_key, &depth_bits);
            if (depth_bits < 16) return false;
            draw_instanced = reinterpret_cast<DrawInstanced>(SDL_GL_GetProcAddress("glDrawArraysInstanced"));
            divisor = reinterpret_cast<Divisor>(SDL_GL_GetProcAddress("glVertexAttribDivisor"));
            uniform4 = reinterpret_cast<Uniform4>(SDL_GL_GetProcAddress("glUniform4f"));
            auto bind_attribute = reinterpret_cast<BindAttribute>(SDL_GL_GetProcAddress("glBindAttribLocation"));
            if (!draw_instanced || !divisor || !uniform4 || !bind_attribute) return false;
            const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source), fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
            if (vertex && fragment)
            {
                program = glCreateProgram();
                glAttachShader(program, vertex); glAttachShader(program, fragment);
                const char* attributes[] = {"aPosition", "aNormal", "aBase", "aDimensions", "aCrown", "aTrunk", "aHaze"};
                for (int i = 0; i < 7; ++i) bind_attribute(program, i, attributes[i]);
                glLinkProgram(program);
                GLint success = 0;
                glGetProgramiv(program, GL_LINK_STATUS, &success);
                if (!success) { glDeleteProgram(program); program = 0; }
            }
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            if (!program) return false;
            const char* uniforms[] = {"uCamera", "uPitch", "uLight", "uAirlight", "uSettings", "uTerrain"};
            for (int i = 0; i < 6; ++i) locations[i] = glGetUniformLocation(program, uniforms[i]);
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &mesh); glGenBuffers(1, &instances);
            std::vector<PlantMeshVertex> vertices;
            for (int level = 0; level < 3; ++level)
            {
                mesh_first[level] = vertices.size();
                const auto shape = build_plant_mesh(8 << level, 4 << level);
                vertices.insert(vertices.end(), shape.begin(), shape.end());
                mesh_count[level] = shape.size();
            }
            GLint previous_buffer;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
            glBindBuffer(GL_ARRAY_BUFFER, mesh);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(PlantMeshVertex), vertices.data(), static_draw);
            glBindBuffer(GL_ARRAY_BUFFER, previous_buffer);
            glGenTextures(1, &terrain_texture);
            GLint previous_texture;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
            glBindTexture(GL_TEXTURE_2D, terrain_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, 0x822E, 1, 1, 0, 0x1903, GL_FLOAT, nullptr);
            glBindTexture(GL_TEXTURE_2D, previous_texture);
            return true;
        }

        void render(const ImDrawList*, const ImDrawCmd* command)
        {
            const auto& batch = *static_cast<const Batch*>(command->UserCallbackData);
            const bool initial = first_draw;
            const bool depth_enabled = glIsEnabled(GL_DEPTH_TEST), cull_enabled = glIsEnabled(GL_CULL_FACE);
            const bool scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
            GLint depth_function, front_face, cull_face;
            GLboolean depth_mask;
            GLdouble clear_depth;
            glGetIntegerv(depth_function_key, &depth_function); get_boolean(depth_mask_key, &depth_mask);
            glGetIntegerv(front_face_key, &front_face); glGetIntegerv(cull_face_key, &cull_face);
            get_double(clear_depth_key, &clear_depth);
            glEnable(GL_DEPTH_TEST); depth_func(less_equal); depth_write(GL_TRUE);
            glEnable(GL_CULL_FACE); cull_mode(GL_BACK); // The camera looks along +Z, reversing projected outward winding.
            front_mode(clockwise);
            if (first_draw)
            {
                glDisable(GL_SCISSOR_TEST); clear_depth_value(1); glClear(depth_buffer_bit);
                if (scissor_enabled) glEnable(GL_SCISSOR_TEST);
                first_draw = false;
            }
            glUseProgram(program);
            glBindVertexArray(vao);
            if (initial)
            {
                const auto& view = batch.view;
                uniform4(locations[0], view.focal, view.center_x, view.center_y, 0);
                const auto scale = ImGui::GetIO().DisplayFramebufferScale;
                uniform4(locations[1], view.sin_pitch, view.cos_pitch, scale.x, scale.y);
                uniform4(locations[2], view.light.x, view.light.y, view.light.z, view.light.w);
                uniform4(locations[3], view.airlight.x, view.airlight.y, view.airlight.z, 0);
                uniform4(locations[4], view.ambient, terrain_active ? 1 : 0, view.center_y * 2, view.redlight ? 1 : 0);
                glUniform1i(locations[5], 0);
                glBindBuffer(GL_ARRAY_BUFFER, mesh);
                for (int i = 0; i < 7; ++i)
                {
                    glEnableVertexAttribArray(i); divisor(i, i < 2 ? 0 : 1);
                    if (i < 2)
                        glVertexAttribPointer(i, i == 0 ? 4 : 3, GL_FLOAT, GL_FALSE, sizeof(PlantMeshVertex),
                            reinterpret_cast<void*>(i == 0 ? 0 : offsetof(PlantMeshVertex, normal)));
                }
                glBindBuffer(GL_ARRAY_BUFFER, instances);
                glBufferData(GL_ARRAY_BUFFER, upload.size() * sizeof(PlantMeshInstance), upload.data(), GL_STREAM_DRAW);
            }
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, terrain_texture);
            glBindBuffer(GL_ARRAY_BUFFER, instances);
            for (const auto& run : batch.runs)
            {
                const size_t base = (batch.upload_offset + run.first) * sizeof(PlantMeshInstance);
                const size_t offsets[] = {offsetof(PlantMeshInstance, base), offsetof(PlantMeshInstance, dimensions),
                    offsetof(PlantMeshInstance, crown_color), offsetof(PlantMeshInstance, trunk_color), offsetof(PlantMeshInstance, haze)};
                for (int i = 0; i < 5; ++i)
                {
                    const bool color = i == 2 || i == 3;
                    glVertexAttribPointer(i + 2, 4, color ? GL_UNSIGNED_BYTE : GL_FLOAT, color ? GL_TRUE : GL_FALSE,
                        sizeof(PlantMeshInstance), reinterpret_cast<void*>(base + offsets[i]));
                }
                draw_instanced(GL_TRIANGLES, mesh_first[run.level], mesh_count[run.level], run.count);
                ++stats.draws;
            }
            depth_func(depth_function); depth_write(depth_mask); clear_depth_value(clear_depth);
            front_mode(front_face); cull_mode(cull_face);
            if (!depth_enabled) glDisable(GL_DEPTH_TEST);
            if (!cull_enabled) glDisable(GL_CULL_FACE);
        }
    }

    void clear_plant_mesh_gpu()
    {
        if (context && context == SDL_GL_GetCurrentContext())
        {
            if (program) glDeleteProgram(program);
            if (vao) glDeleteVertexArrays(1, &vao);
            if (mesh) glDeleteBuffers(1, &mesh);
            if (instances) glDeleteBuffers(1, &instances);
            if (terrain_texture) glDeleteTextures(1, &terrain_texture);
        }
        program = vao = mesh = instances = terrain_texture = 0;
        context = nullptr; attempted = false;
        batches.clear(); upload.clear(); saved_terrain.clear(); batch_index = 0;
        terrain_width = terrain_height = 0; stats = {};
    }

    bool prepare_plant_mesh_gpu(const PlantMeshView& view, const std::vector<float>& terrain_depth)
    {
        stats = {};
        if (scenery_gpu_reference || !horizon_fast_path(201) || !SDL_GL_GetCurrentContext()) return false;
        if (!initialize()) return false;
        for (auto& batch : batches) { batch.plants.clear(); batch.runs.clear(); }
        batch_index = 0; upload.clear(); current_view = view; first_draw = true;
        const int width = view.center_x * 2, height = view.center_y * 2;
        terrain_active = terrain_depth.size() == static_cast<size_t>(width) * height;
        if (terrain_active && (terrain_width != width || terrain_height != height || saved_terrain != terrain_depth))
        {
            GLint binding, alignment, row;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row);
            glBindTexture(GL_TEXTURE_2D, terrain_texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, 0x822E, width, height, 0, 0x1903, GL_FLOAT, terrain_depth.data());
            glBindTexture(GL_TEXTURE_2D, binding);
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH, row);
            saved_terrain = terrain_depth; terrain_width = width; terrain_height = height;
        }
        return true;
    }

    void add_plant_mesh_gpu(const PlantMeshInstance& plant, float screen_radius)
    {
        if (batch_index == batches.size()) batches.emplace_back();
        auto& batch = batches[batch_index];
        const int level = screen_radius < 24 ? 0 : screen_radius < 90 ? 1 : 2;
        if (batch.runs.empty() || batch.runs.back().count >= 64)
            batch.runs.push_back({batch.plants.size(), 0, level});
        auto& run = batch.runs.back();
        if (level > run.level)
        {
            stats.triangles += run.count * (mesh_count[level] - mesh_count[run.level]) / 3;
            run.level = level;
        }
        ++run.count; batch.plants.push_back(plant);
        ++stats.instances; stats.triangles += mesh_count[run.level] / 3;
    }

    void flush_plant_mesh_gpu()
    {
        if (batch_index == batches.size() || batches[batch_index].plants.empty()) return;
        auto& batch = batches[batch_index]; batch.view = current_view;
        batch.upload_offset = upload.size();
        upload.insert(upload.end(), batch.plants.begin(), batch.plants.end());
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->AddCallback(render, &batch);
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        ++batch_index;
    }

    const PlantMeshStats& plant_mesh_stats() { return stats; }
}
