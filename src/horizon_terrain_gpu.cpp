#include <array>
#include <chrono>
#include <iostream>
#include "SDL.h"
#include "horizon_terrain_gpu.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"

namespace alienorum
{
    bool horizon_terrain_gpu_reference = false;
    static HorizonTerrainGpuStats stats;
    static SDL_GLContext context = nullptr;
    static GLuint program = 0, vao = 0, framebuffer = 0;
    static GLint tile_columns_location = -1;
    static std::array<GLuint, 4> textures = {};
    static bool attempted = false;
    static constexpr int tile_width = 8, tile_height = 2;
    static constexpr GLenum framebuffer_target = 0x8D40, color_attachment = 0x8CE0;
    static constexpr GLenum rgba32f = 0x8814, rg32f = 0x8230, r32f = 0x822E;
    static constexpr GLenum rg_format = 0x8227, red_format = 0x1903;
    using GenFramebuffers = void (APIENTRY *)(GLsizei, GLuint*);
    using BindFramebuffer = void (APIENTRY *)(GLenum, GLuint);
    using DeleteFramebuffers = void (APIENTRY *)(GLsizei, const GLuint*);
    using FramebufferTexture = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebuffer = GLenum (APIENTRY *)(GLenum);
    using DrawArrays = void (APIENTRY *)(GLenum, GLint, GLsizei);
    static GenFramebuffers gen_framebuffers = nullptr;
    static BindFramebuffer bind_framebuffer = nullptr;
    static DeleteFramebuffers delete_framebuffers = nullptr;
    static FramebufferTexture framebuffer_texture = nullptr;
    static CheckFramebuffer check_framebuffer = nullptr;
    static DrawArrays draw_arrays = nullptr;

    struct Triangle
    {
        ProjectedTerrainVertex a, b, c;
        double determinant, da_dx, db_dx;
        int left, top, right, bottom;

        bool sample(HorizonTerrainRaster &target, int x, int y) const
        {
            if (x < left || x >= right || y < top || y >= bottom) return false;
            double wa = ((b.y - c.y) * (left + 0.5 - c.x) + (c.x - b.x) * (y + 0.5 - c.y)) / determinant;
            double wb = ((c.y - a.y) * (left + 0.5 - c.x) + (a.x - c.x) * (y + 0.5 - c.y)) / determinant;
            // Match the reference's repeated double additions, including its final float rounding.
            for (int step = left; step < x; ++step, wa += da_dx, wb += db_dx) {}
            const double wc = 1.0 - wa - wb;
            if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9) return false;
            const double depth = wa * a.inverse_depth + wb * b.inverse_depth + wc * c.inverse_depth;
            const double distance = (wa * a.distance_over_depth + wb * b.distance_over_depth
                + wc * c.distance_over_depth) / depth;
            target.store(x, y, depth, distance);
            return true;
        }
    };

    static const char *vertex_source = R"GLSL(
#version 130
void main()
{
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

    static const char *fragment_source = R"GLSL(
#version 130
uniform sampler2D uTriangles;
uniform sampler2D uTiles;
uniform sampler2D uIndices;
uniform int uTileColumns;
out vec2 result;
vec4 fetch(sampler2D source, int index)
{
    int width = textureSize(source, 0).x;
    return texelFetch(source, ivec2(index % width, index / width), 0);
}
void main()
{
    ivec2 tile = ivec2(gl_FragCoord.xy) / ivec2(8, 2);
    vec2 span = fetch(uTiles, tile.y * uTileColumns + tile.x).rg;
    int first = int(span.x);
    int count = int(span.y);
    float best = 0.0;
    float best_error = 0.0;
    int winner = -1;
    bool uncertain = false;
    for (int item = 0; item < count; ++item)
    {
        int index = int(fetch(uIndices, first + item).r);
        vec4 p = fetch(uTriangles, index * 3);
        vec4 q = fetch(uTriangles, index * 3 + 1);
        vec4 r = fetch(uTriangles, index * 3 + 2);
        float wa = p.x + gl_FragCoord.x * p.z + gl_FragCoord.y * q.x;
        float wb = p.y + gl_FragCoord.x * p.w + gl_FragCoord.y * q.y;
        float wc = 1.0 - wa - wb;
        float edge = min(wa, min(wb, wc));
        if (edge < -r.y) continue;
        if (edge <= r.y) uncertain = true;
        if (edge < -1e-9) continue;
        float depth = r.x + gl_FragCoord.x * q.z + gl_FragCoord.y * q.w;
        if (winner >= 0 && abs(depth - best) <= r.z + best_error) uncertain = true;
        if (depth > best)
        {
            best = depth;
            best_error = r.z;
            winner = index;
        }
    }
    result = vec2(float(winner + 1), uncertain ? 1.0 : 0.0);
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
            std::cerr << "Horizon terrain shader: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void clear_horizon_terrain_gpu()
    {
        if (context && context == SDL_GL_GetCurrentContext())
        {
            if (program) glDeleteProgram(program);
            if (vao) glDeleteVertexArrays(1, &vao);
            if (framebuffer) delete_framebuffers(1, &framebuffer);
            glDeleteTextures(textures.size(), textures.data());
        }
        context = nullptr;
        program = vao = framebuffer = 0;
        tile_columns_location = -1;
        textures = {};
        attempted = false;
        stats = {};
    }

    static bool initialize()
    {
        if (!SDL_GL_GetCurrentContext()) return false;
        if (context != SDL_GL_GetCurrentContext()) clear_horizon_terrain_gpu();
        context = SDL_GL_GetCurrentContext();
        if (attempted) return program != 0;
        attempted = true;
        gen_framebuffers = reinterpret_cast<GenFramebuffers>(SDL_GL_GetProcAddress("glGenFramebuffers"));
        bind_framebuffer = reinterpret_cast<BindFramebuffer>(SDL_GL_GetProcAddress("glBindFramebuffer"));
        delete_framebuffers = reinterpret_cast<DeleteFramebuffers>(SDL_GL_GetProcAddress("glDeleteFramebuffers"));
        framebuffer_texture = reinterpret_cast<FramebufferTexture>(SDL_GL_GetProcAddress("glFramebufferTexture2D"));
        check_framebuffer = reinterpret_cast<CheckFramebuffer>(SDL_GL_GetProcAddress("glCheckFramebufferStatus"));
        draw_arrays = reinterpret_cast<DrawArrays>(SDL_GL_GetProcAddress("glDrawArrays"));
        if (!gen_framebuffers || !bind_framebuffer || !delete_framebuffers || !framebuffer_texture
            || !check_framebuffer || !draw_arrays)
        {
            return false;
        }
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
                std::cerr << "Horizon terrain program: " << log << std::endl;
                glDeleteProgram(program);
                program = 0;
            }
        }
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!program) return false;
        tile_columns_location = glGetUniformLocation(program, "uTileColumns");
        GLint previous_program;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uTriangles"), 0);
        glUniform1i(glGetUniformLocation(program, "uTiles"), 1);
        glUniform1i(glGetUniformLocation(program, "uIndices"), 2);
        glUseProgram(previous_program);
        glGenVertexArrays(1, &vao);
        glGenTextures(textures.size(), textures.data());
        gen_framebuffers(1, &framebuffer);
        return true;
    }

    struct State
    {
        GLint program, vao, active_texture, read_framebuffer, draw_framebuffer;
        GLint viewport[4], texture[3], unpack_alignment, unpack_row, pack_alignment, pack_row;
        std::array<GLenum, 5> flags = {GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, 0x0B90};
        std::array<bool, 5> enabled;

        State()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
            glGetIntegerv(0x8CAA, &read_framebuffer);
            glGetIntegerv(0x8CA6, &draw_framebuffer);
            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row);
            glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
            glGetIntegerv(0x0D02, &pack_row);
            for (int unit = 0; unit < 3; ++unit)
            {
                glActiveTexture(GL_TEXTURE0 + unit);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture[unit]);
            }
            for (size_t index = 0; index < flags.size(); ++index)
            {
                enabled[index] = glIsEnabled(flags[index]);
                glDisable(flags[index]);
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glPixelStorei(0x0D02, 0);
        }

        ~State()
        {
            glUseProgram(program);
            glBindVertexArray(vao);
            bind_framebuffer(0x8CA8, read_framebuffer);
            bind_framebuffer(0x8CA9, draw_framebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row);
            glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
            glPixelStorei(0x0D02, pack_row);
            for (int unit = 0; unit < 3; ++unit)
            {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, texture[unit]);
            }
            glActiveTexture(active_texture);
            for (size_t index = 0; index < flags.size(); ++index)
            {
                if (enabled[index]) glEnable(flags[index]);
                else glDisable(flags[index]);
            }
        }
    };

    static bool upload(GLuint texture, std::vector<float> data, int components, GLint limit)
    {
        const int count = static_cast<int>(data.size()) / components;
        const int width = std::min(1024, std::max(1, count));
        const int height = std::max(1, (count + width - 1) / width);
        if (width > limit || height > limit) return false;
        data.resize(static_cast<size_t>(width) * height * components);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, components == 4 ? rgba32f : r32f,
            width, height, 0, components == 4 ? GL_RGBA : red_format, GL_FLOAT, data.data());
        return true;
    }

    bool rasterize_horizon_terrain_gpu(HorizonTerrainRaster &target,
        const std::vector<ProjectedTerrainVertex> &vertices, int width, int height)
    {
        stats = {};
        if (horizon_terrain_gpu_reference || width <= 0 || height <= 0 || vertices.size() % 3
            || !initialize())
        {
            return false;
        }
        const auto start = std::chrono::steady_clock::now();
        GLint limit;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (width > limit || height > limit) return false;
        std::vector<Triangle> triangles;
        triangles.reserve(vertices.size() / 3);
        int crop_left = width, crop_top = height, crop_right = 0, crop_bottom = 0;
        for (size_t index = 0; index < vertices.size(); index += 3)
        {
            const auto &a = vertices[index], &b = vertices[index + 1], &c = vertices[index + 2];
            if (a.inverse_depth <= 0 || b.inverse_depth <= 0 || c.inverse_depth <= 0) continue;
            const double determinant = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            if (!std::isfinite(determinant) || fabs(determinant) < 1e-10) continue;
            const double left = std::max(0.0, std::min(a.x, std::min(b.x, c.x)));
            const double right = std::min(static_cast<double>(width), std::max(a.x, std::max(b.x, c.x)));
            const double top = std::max(0.0, std::min(a.y, std::min(b.y, c.y)));
            const double bottom = std::min(static_cast<double>(height), std::max(a.y, std::max(b.y, c.y)));
            if (left >= right || top >= bottom) continue;
            for (const auto &vertex : {a, b, c})
            {
                if (!(vertex.distance_over_depth > 0) || !std::isfinite(vertex.distance_over_depth)) return false;
            }
            Triangle triangle = {a, b, c, determinant, (b.y - c.y) / determinant, (c.y - a.y) / determinant,
                static_cast<int>(left), static_cast<int>(top), static_cast<int>(ceil(right)), static_cast<int>(ceil(bottom))};
            triangles.push_back(triangle);
            crop_left = std::min(crop_left, triangle.left);
            crop_top = std::min(crop_top, triangle.top);
            crop_right = std::max(crop_right, triangle.right);
            crop_bottom = std::max(crop_bottom, triangle.bottom);
        }
        stats.triangles = triangles.size();
        if (triangles.empty())
        {
            stats.accelerated = true;
            return true;
        }
        const int crop_width = crop_right - crop_left, crop_height = crop_bottom - crop_top;
        const int columns = (crop_width + tile_width - 1) / tile_width;
        const int rows = (crop_height + tile_height - 1) / tile_height;
        std::vector<std::vector<unsigned int>> bins(static_cast<size_t>(columns) * rows);
        std::vector<float> coefficients;
        coefficients.reserve(triangles.size() * 12);
        for (size_t index = 0; index < triangles.size(); ++index)
        {
            const Triangle &t = triangles[index];
            const double wa = ((t.b.y - t.c.y) * (crop_left - t.c.x) + (t.c.x - t.b.x) * (crop_top - t.c.y)) / t.determinant;
            const double wb = ((t.c.y - t.a.y) * (crop_left - t.c.x) + (t.a.x - t.c.x) * (crop_top - t.c.y)) / t.determinant;
            const double da_dy = (t.c.x - t.b.x) / t.determinant;
            const double db_dy = (t.a.x - t.c.x) / t.determinant;
            const double scale = fabs(wa) + fabs(wb) + crop_width * (fabs(t.da_dx) + fabs(t.db_dx))
                + crop_height * (fabs(da_dy) + fabs(db_dy)) + 1;
            const double error = scale * 2e-6 + 1e-7;
            const double inverse_origin = t.c.inverse_depth + wa * (t.a.inverse_depth - t.c.inverse_depth)
                + wb * (t.b.inverse_depth - t.c.inverse_depth);
            const double inverse_dx = t.da_dx * (t.a.inverse_depth - t.c.inverse_depth)
                + t.db_dx * (t.b.inverse_depth - t.c.inverse_depth);
            const double inverse_dy = da_dy * (t.a.inverse_depth - t.c.inverse_depth)
                + db_dy * (t.b.inverse_depth - t.c.inverse_depth);
            const double inverse_error = (fabs(inverse_origin) + crop_width * fabs(inverse_dx)
                + crop_height * fabs(inverse_dy)) * 2e-6 + 1e-12;
            const double values[] = {wa, wb, t.da_dx, t.db_dx, da_dy, db_dy,
                inverse_dx, inverse_dy, inverse_origin, error, inverse_error, 0};
            for (double value : values)
            {
                if (!std::isfinite(static_cast<float>(value))) return false;
                coefficients.push_back(static_cast<float>(value));
            }
            for (int y = (t.top - crop_top) / tile_height; y <= (t.bottom - crop_top - 1) / tile_height; ++y)
            {
                for (int x = (t.left - crop_left) / tile_width; x <= (t.right - crop_left - 1) / tile_width; ++x)
                {
                    bins[y * columns + x].push_back(index);
                }
            }
        }
        std::vector<float> spans, indices;
        spans.reserve(bins.size() * 4);
        for (const auto &bin : bins)
        {
            stats.max_candidates = std::max(stats.max_candidates, static_cast<unsigned int>(bin.size()));
            spans.insert(spans.end(), {static_cast<float>(indices.size()), static_cast<float>(bin.size()), 0, 0});
            for (unsigned int index : bin) indices.push_back(static_cast<float>(index));
        }
        if (indices.size() >= 16777216) return false;
        stats.preparation_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::vector<float> selected(static_cast<size_t>(crop_width) * crop_height * 2);
        const auto selection_start = std::chrono::steady_clock::now();
        {
            State state;
            glActiveTexture(GL_TEXTURE0);
            if (!upload(textures[0], std::move(coefficients), 4, limit)) return false;
            glActiveTexture(GL_TEXTURE0 + 1);
            if (!upload(textures[1], std::move(spans), 4, limit)) return false;
            glActiveTexture(GL_TEXTURE0 + 2);
            if (!upload(textures[2], std::move(indices), 1, limit)) return false;
            glBindTexture(GL_TEXTURE_2D, textures[3]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, rg32f, crop_width, crop_height, 0, rg_format, GL_FLOAT, nullptr);
            glBindTexture(GL_TEXTURE_2D, textures[2]);
            bind_framebuffer(framebuffer_target, framebuffer);
            framebuffer_texture(framebuffer_target, color_attachment, GL_TEXTURE_2D, textures[3], 0);
            if (check_framebuffer(framebuffer_target) != 0x8CD5) return false;
            glViewport(0, 0, crop_width, crop_height);
            glUseProgram(program);
            glUniform1i(tile_columns_location, columns);
            glBindVertexArray(vao);
            draw_arrays(GL_TRIANGLES, 0, 3);
            glReadPixels(0, 0, crop_width, crop_height, rg_format, GL_FLOAT, selected.data());
            if (glGetError() != 0) return false;
        }
        const auto reconstruction_start = std::chrono::steady_clock::now();
        stats.selection_ms = std::chrono::duration<double, std::milli>(reconstruction_start - selection_start).count();
        for (size_t index = 0; index < selected.size(); index += 2)
        {
            if (!std::isfinite(selected[index]) || selected[index] < 0 || selected[index] > triangles.size()
                || !std::isfinite(selected[index + 1]))
            {
                return false;
            }
        }
        for (int y = 0; y < crop_height; ++y)
        {
            for (int x = 0; x < crop_width; ++x)
            {
                const size_t pixel = (static_cast<size_t>(y) * crop_width + x) * 2;
                const int id = static_cast<int>(selected[pixel]) - 1;
                bool repair = selected[pixel + 1] > 0;
                if (!repair && id >= 0) repair = !triangles[id].sample(target, crop_left + x, crop_top + y);
                if (repair)
                {
                    ++stats.repaired_pixels;
                    for (unsigned int candidate : bins[(y / tile_height) * columns + x / tile_width])
                    {
                        triangles[candidate].sample(target, crop_left + x, crop_top + y);
                    }
                }
            }
        }
        stats.pixels = static_cast<unsigned int>(crop_width * crop_height);
        stats.reconstruction_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - reconstruction_start).count();
        stats.accelerated = true;
        return true;
    }

    const HorizonTerrainGpuStats &horizon_terrain_gpu_stats() { return stats; }
}
