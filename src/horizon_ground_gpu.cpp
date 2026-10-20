#include <array>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <utility>
#include "SDL.h"
#include "horizon_ground_gpu.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"

namespace alienorum
{
    bool horizon_ground_gpu_reference = false;
    static HorizonGroundGpuStats stats;
    static SDL_GLContext context = nullptr;
    static GLuint program = 0, vao = 0, vbo = 0, depth_texture = 0, horizon_texture = 0;
    static bool attempted = false;
    static GLint projection_location = -1, terrain_location = -1;
    static int texture_width = 0, texture_height = 0;
    static bool terrain_mask = false;
    typedef void (APIENTRYP DrawArraysProc)(GLenum, GLint, GLsizei);
    static DrawArraysProc draw_arrays = nullptr;
    static constexpr GLenum red_format = 0x1903, rg_format = 0x8227;
    static constexpr GLint r32f_format = 0x822E, rg32f_format = 0x8230;

    struct Vertex
    {
        float x, y;
        ImU32 color;
    };

    struct Batch
    {
        std::vector<Vertex> vertices;
        std::array<float, 16> projection;
        bool terrain = false;
    };

    static std::array<Batch, 4> batches;
    static unsigned int batch_index = 0;

    static const char *vertex_source = R"GLSL(
#version 130
in vec2 aPos;
in vec4 aColor;
out vec2 pixel;
out vec4 color;
uniform mat4 uProjection;
void main()
{
    pixel = aPos;
    color = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)GLSL";

    static const char *fragment_source = R"GLSL(
#version 130
in vec2 pixel;
in vec4 color;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform sampler2D uHorizon;
uniform bool uTerrain;
void main()
{
    ivec2 cell = ivec2(floor(pixel));
    vec2 horizon = texelFetch(uHorizon, ivec2(cell.x, 0), 0).rg;
    if (float(cell.y) + 0.5 < horizon.r) discard;
    if (uTerrain && float(cell.y) < horizon.g && texelFetch(uDepth, cell, 0).r <= 0.0) discard;
    FragColor = color;
}
)GLSL";

    static GLuint compile(GLenum type, const char *source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            char log[2048] = {};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Horizon ground shader: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void clear_horizon_ground_gpu()
    {
        if (context && context == SDL_GL_GetCurrentContext())
        {
            if (program) glDeleteProgram(program);
            if (vao) glDeleteVertexArrays(1, &vao);
            if (vbo) glDeleteBuffers(1, &vbo);
            if (depth_texture) glDeleteTextures(1, &depth_texture);
            if (horizon_texture) glDeleteTextures(1, &horizon_texture);
        }
        program = vao = vbo = depth_texture = horizon_texture = 0;
        texture_width = texture_height = 0;
        attempted = false;
        context = nullptr;
        stats = {};
        for (auto &batch : batches) batch.vertices.clear();
    }

    static bool initialize()
    {
        if (!SDL_GL_GetCurrentContext()) return false;
        if (context != SDL_GL_GetCurrentContext()) clear_horizon_ground_gpu();
        context = SDL_GL_GetCurrentContext();
        if (attempted) return program != 0;
        attempted = true;
        draw_arrays = reinterpret_cast<DrawArraysProc>(SDL_GL_GetProcAddress("glDrawArrays"));
        if (!draw_arrays) return false;
        GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
        GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
        if (vertex && fragment)
        {
            program = glCreateProgram();
            glAttachShader(program, vertex);
            glAttachShader(program, fragment);
            glLinkProgram(program);
            GLint success = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &success);
            if (!success)
            {
                char log[2048] = {};
                glGetProgramInfoLog(program, sizeof(log), nullptr, log);
                std::cerr << "Horizon ground program: " << log << std::endl;
                glDeleteProgram(program);
                program = 0;
            }
        }
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!program) return false;
        projection_location = glGetUniformLocation(program, "uProjection");
        terrain_location = glGetUniformLocation(program, "uTerrain");

        GLint previous_program, previous_vao, previous_vbo;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_vbo);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uDepth"), 0);
        glUniform1i(glGetUniformLocation(program, "uHorizon"), 1);
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        const GLint position = glGetAttribLocation(program, "aPos");
        const GLint color = glGetAttribLocation(program, "aColor");
        glEnableVertexAttribArray(position);
        glEnableVertexAttribArray(color);
        glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        glVertexAttribPointer(color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, color)));
        glBindVertexArray(previous_vao);
        glBindBuffer(GL_ARRAY_BUFFER, previous_vbo);
        glUseProgram(previous_program);
        glGenTextures(1, &depth_texture);
        glGenTextures(1, &horizon_texture);
        return true;
    }

    bool prepare_horizon_ground_gpu(int width, int height, const std::vector<float> &distances,
        const std::vector<double> &horizon, const std::vector<int> &base_top, bool terrain)
    {
        stats = {};
        if (horizon_ground_gpu_reference || width <= 0 || height <= 0
            || horizon.size() != static_cast<size_t>(width) || base_top.size() != horizon.size()
            || (terrain && distances.size() != static_cast<size_t>(width) * height)
            || !initialize())
        {
            return false;
        }
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (width > limit || height > limit) return false;

        GLint previous_texture, previous_row_length, previous_alignment;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_row_length);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        const bool resize = texture_width != width || texture_height != height;
        glBindTexture(GL_TEXTURE_2D, depth_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (resize) glTexImage2D(GL_TEXTURE_2D, 0, r32f_format, width, height, 0, red_format, GL_FLOAT, nullptr);
        if (terrain) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, red_format, GL_FLOAT, distances.data());
        std::vector<float> columns(width * 2);
        for (int x = 0; x < width; ++x)
        {
            // Store the first covered pixel, preserving the double-precision CPU comparison.
            columns[x * 2] = static_cast<float>(ceil(horizon[x] - 0.5));
            columns[x * 2 + 1] = static_cast<float>(base_top[x]);
        }
        glBindTexture(GL_TEXTURE_2D, horizon_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (resize) glTexImage2D(GL_TEXTURE_2D, 0, rg32f_format, width, 1, 0, rg_format, GL_FLOAT, nullptr);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, 1, rg_format, GL_FLOAT, columns.data());
        glBindTexture(GL_TEXTURE_2D, previous_texture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
        if (glGetError() != 0) return false;
        texture_width = width;
        texture_height = height;
        terrain_mask = terrain;
        return true;
    }

    static void render(const ImDrawList*, const ImDrawCmd *command)
    {
        const auto &batch = *static_cast<const Batch*>(command->UserCallbackData);
        glUseProgram(program);
        glUniformMatrix4fv(projection_location, 1, GL_FALSE, batch.projection.data());
        glUniform1i(terrain_location, batch.terrain);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depth_texture);
        glActiveTexture(GL_TEXTURE0 + 1);
        GLint previous_texture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
        glBindTexture(GL_TEXTURE_2D, horizon_texture);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, batch.vertices.size() * sizeof(Vertex), batch.vertices.data(), GL_STREAM_DRAW);
        draw_arrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
        glBindTexture(GL_TEXTURE_2D, previous_texture);
        glActiveTexture(GL_TEXTURE0);
    }

    void queue_horizon_ground_gpu(std::vector<HorizonGroundRect> rectangles)
    {
        if (rectangles.empty()) return;
        Batch &batch = batches[batch_index++ % batches.size()];
        batch.vertices.clear();
        batch.vertices.reserve(rectangles.size() * 6);
        batch.terrain = terrain_mask;
        const ImGuiIO &io = ImGui::GetIO();
        batch.projection =
        {
            2.0f / io.DisplaySize.x, 0, 0, 0,
            0, -2.0f / io.DisplaySize.y, 0, 0,
            0, 0, -1, 0,
            -1, 1, 0, 1
        };
        for (const auto &rectangle : rectangles)
        {
            const float left = rectangle.left, top = rectangle.top;
            const float right = rectangle.right, bottom = rectangle.bottom;
            batch.vertices.insert(batch.vertices.end(),
            {
                {left, top, rectangle.color}, {right, top, rectangle.color}, {right, bottom, rectangle.color},
                {left, top, rectangle.color}, {right, bottom, rectangle.color}, {left, bottom, rectangle.color}
            });
            ++stats.tiles;
            stats.pixels += (rectangle.right - rectangle.left) * (rectangle.bottom - rectangle.top);
        }
        ImDrawList *draw = ImGui::GetBackgroundDrawList();
        draw->AddCallback(render, &batch);
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    const HorizonGroundGpuStats &horizon_ground_gpu_stats() { return stats; }
}
