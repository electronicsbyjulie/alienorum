#include "performance_options.h"
#include <array>
#include <chrono>
#include <iostream>
#include "SDL.h"
#include "horizon_mesh_gpu.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"

namespace alienorum
{
    bool horizon_mesh_gpu_reference = false;
    static HorizonMeshGpuStats stats;
    static SDL_GLContext context = nullptr;
    static GLuint program = 0, vao = 0, framebuffer = 0;
    static GLint count_location = -1, width_location = -1;
    static std::array<GLuint, 4> textures = {};
    static bool attempted = false;
    static unsigned int builds = 0;
    static std::vector<int> saved_permutation;
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
#extension GL_ARB_shader_bit_encoding : require
uniform sampler2D uPositions;
uniform sampler2D uOctaves;
uniform sampler2D uPermutation;
uniform int uCount;
uniform int uWidth;
uniform int uPrecisionMask;
out vec2 result;
// A zero uniform bit mask makes each rounding step opaque to reassociation.
float rounded(float value)
{
    return uintBitsToFloat(floatBitsToUint(value) ^ uint(uPrecisionMask));
}
// High/low pairs preserve fractional noise coordinates without shader FP64.
vec2 add_pair(vec2 a, vec2 b)
{
    float sum = rounded(a.x + b.x);
    float v = rounded(sum - a.x);
    float error = (a.x - rounded(sum - v)) + (b.x - v);
    error += a.y + b.y;
    float high = rounded(sum + error);
    return vec2(high, error - rounded(high - sum));
}
vec2 multiply_pair(vec2 a, vec2 b)
{
    float product = rounded(a.x * b.x);
    // Twelve significant bits per half make each high/low product exact.
    float ah = uintBitsToFloat(floatBitsToUint(a.x) & 0xfffff000u);
    float bh = uintBitsToFloat(floatBitsToUint(b.x) & 0xfffff000u);
    float al = rounded(a.x - ah);
    float bl = rounded(b.x - bh);
    float error = ((ah * bh - product) + ah * bl + al * bh) + al * bl;
    error += a.x * b.y + a.y * b.x;
    error += a.y * b.y;
    float high = rounded(product + error);
    return vec2(high, error - rounded(high - product));
}
vec2 fade_pair(vec2 x)
{
    vec2 cubic = multiply_pair(multiply_pair(x, x), x);
    vec2 inner = add_pair(multiply_pair(x, vec2(6.0, 0.0)), vec2(-15.0, 0.0));
    inner = add_pair(multiply_pair(x, inner), vec2(10.0, 0.0));
    return multiply_pair(cubic, inner);
}
vec2 interpolate_pair(vec2 t, vec2 a, vec2 b)
{
    return add_pair(a, multiply_pair(t, add_pair(b, -a)));
}
int permutation(int index)
{
    return int(texelFetch(uPermutation, ivec2(index & 511, 0), 0).r);
}
vec2 gradient(int hash, vec2 x, vec2 y, vec2 z)
{
    int h = hash & 15;
    vec2 u = h < 8 ? x : y;
    vec2 v = h < 4 ? y : ((h == 12 || h == 14) ? x : z);
    return add_pair((h & 1) == 0 ? u : -u, (h & 2) == 0 ? v : -v);
}
vec2 fraction_pair(vec2 coordinate, out int cell)
{
    float base = floor(coordinate.x);
    vec2 fraction = add_pair(coordinate, vec2(-base, 0.0));
    if (fraction.x < 0.0 || (fraction.x == 0.0 && fraction.y < 0.0))
    {
        base -= 1.0;
        fraction = add_pair(fraction, vec2(1.0, 0.0));
    }
    if (fraction.x > 1.0 || (fraction.x == 1.0 && fraction.y >= 0.0))
    {
        base += 1.0;
        fraction = add_pair(fraction, vec2(-1.0, 0.0));
    }
    cell = int(base) & 255;
    return fraction;
}
vec2 noise_pair(vec2 px, vec2 py, vec2 pz)
{
    int X, Y, Z;
    vec2 x = fraction_pair(px, X);
    vec2 y = fraction_pair(py, Y);
    vec2 z = fraction_pair(pz, Z);
    vec2 u = fade_pair(x), v = fade_pair(y), w = fade_pair(z);
    int A = permutation(X) + Y, AA = permutation(A) + Z, AB = permutation(A + 1) + Z;
    int B = permutation(X + 1) + Y, BA = permutation(B) + Z, BB = permutation(B + 1) + Z;
    vec2 xm = add_pair(x, vec2(-1.0, 0.0));
    vec2 ym = add_pair(y, vec2(-1.0, 0.0));
    vec2 zm = add_pair(z, vec2(-1.0, 0.0));
    return interpolate_pair(w,
        interpolate_pair(v,
            interpolate_pair(u, gradient(permutation(AA), x, y, z), gradient(permutation(BA), xm, y, z)),
            interpolate_pair(u, gradient(permutation(AB), x, ym, z), gradient(permutation(BB), xm, ym, z))),
        interpolate_pair(v,
            interpolate_pair(u, gradient(permutation(AA + 1), x, y, zm), gradient(permutation(BA + 1), xm, y, zm)),
            interpolate_pair(u, gradient(permutation(AB + 1), x, ym, zm), gradient(permutation(BB + 1), xm, ym, zm))));
}
vec4 fetch_position(int index)
{
    int width = textureSize(uPositions, 0).x;
    return texelFetch(uPositions, ivec2(index % width, index / width), 0);
}
void main()
{
    int index = int(gl_FragCoord.y) * uWidth + int(gl_FragCoord.x);
    if (index >= uCount)
    {
        result = vec2(0.0);
        return;
    }
    vec3 high = fetch_position(index * 2).xyz;
    vec3 low = fetch_position(index * 2 + 1).xyz;
    vec2 x = vec2(high.x, low.x), y = vec2(high.y, low.y), z = vec2(high.z, low.z);
    vec2 total = vec2(0.0);
    int count = textureSize(uOctaves, 0).x;
    for (int octave = 0; octave < count; ++octave)
    {
        vec4 parameters = texelFetch(uOctaves, ivec2(octave, 0), 0);
        vec2 frequency = parameters.xy;
        vec2 value = noise_pair(multiply_pair(x, frequency), multiply_pair(y, frequency), multiply_pair(z, frequency));
        total = add_pair(total, multiply_pair(value, parameters.zw));
    }
    result = total;
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
            std::cerr << "Horizon mesh shader: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void clear_horizon_mesh_gpu()
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
        count_location = width_location = -1;
        textures = {};
        attempted = false;
        stats = {};
        saved_permutation.clear();
        builds = 0;
    }

    static bool initialize()
    {
        if (!SDL_GL_GetCurrentContext()) return false;
        if (context != SDL_GL_GetCurrentContext()) clear_horizon_mesh_gpu();
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
                std::cerr << "Horizon mesh program: " << log << std::endl;
                glDeleteProgram(program);
                program = 0;
            }
        }
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!program) return false;
        count_location = glGetUniformLocation(program, "uCount");
        width_location = glGetUniformLocation(program, "uWidth");
        GLint previous_program;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uPositions"), 0);
        glUniform1i(glGetUniformLocation(program, "uOctaves"), 1);
        glUniform1i(glGetUniformLocation(program, "uPermutation"), 2);
        glUniform1i(glGetUniformLocation(program, "uPrecisionMask"), 0);
        glUseProgram(previous_program);
        glGenVertexArrays(1, &vao);
        glGenTextures(textures.size(), textures.data());
        gen_framebuffers(1, &framebuffer);
        return true;
    }

    struct MeshGlState
    {
        GLint program, vao, active_texture, read_framebuffer, draw_framebuffer;
        GLint viewport[4], texture[3], unpack_alignment, unpack_row, pack_alignment, pack_row;
        std::array<GLenum, 5> flags = {GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, 0x0B90};
        std::array<bool, 5> enabled;

        MeshGlState()
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

        ~MeshGlState()
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

    static bool generate_mesh(const std::vector<std::array<double, 3>>& positions,
        const PerlinNoise& noise, std::vector<double>* totals, HorizonMeshResident* resident)
    {
        stats = {};
        if (totals) totals->clear();
        if (resident) *resident = {};
        if (horizon_mesh_gpu_reference || positions.empty() || noise.permutation().size() != 512) return false;
        const auto start = std::chrono::steady_clock::now();
        const bool cold = !attempted || context != SDL_GL_GetCurrentContext();
        if (!initialize()) return false;
        stats.cold = cold;
        if (cold) stats.initialization_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto generation_start = std::chrono::steady_clock::now();
        const int width = std::min(512, static_cast<int>(positions.size()));
        const int height = (positions.size() + width - 1) / width;
        GLint limit;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (width > limit || height > limit) return false;
        std::vector<float> coordinates;
        coordinates.reserve(positions.size() * 8);
        for (const auto &point : positions)
        {
            for (double value : point)
            {
                if (!std::isfinite(value) || fabs(value) > 10000000) return false;
                coordinates.push_back(static_cast<float>(value));
            }
            coordinates.push_back(0);
            for (double value : point)
            {
                coordinates.push_back(static_cast<float>(value - static_cast<float>(value)));
            }
            coordinates.push_back(0);
        }
        std::vector<float> octaves;
        double frequency = 1, amplitude = 1;
        for (int octave = 0; octave < 12; ++octave)
        {
            octaves.insert(octaves.end(), {static_cast<float>(frequency), static_cast<float>(frequency - static_cast<float>(frequency)),
                static_cast<float>(amplitude), static_cast<float>(amplitude - static_cast<float>(amplitude))});
            frequency *= 1.29;
            amplitude *= 0.8559;
        }
        std::vector<float> output;
        if (totals) output.resize(static_cast<size_t>(width) * height * 2);
        {
            MeshGlState state;
            glActiveTexture(GL_TEXTURE0);
            if (!upload(textures[0], std::move(coordinates), 4, limit)) return false;
            glActiveTexture(GL_TEXTURE0 + 1);
            if (!upload(textures[1], std::move(octaves), 4, limit)) return false;
            glActiveTexture(GL_TEXTURE0 + 2);
            if (saved_permutation != noise.permutation())
            {
                std::vector<float> permutation(noise.permutation().begin(), noise.permutation().end());
                if (!upload(textures[2], std::move(permutation), 1, limit)) return false;
            }
            glBindTexture(GL_TEXTURE_2D, textures[3]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, rg32f, width, height, 0, rg_format, GL_FLOAT, nullptr);
            glBindTexture(GL_TEXTURE_2D, textures[2]);
            bind_framebuffer(framebuffer_target, framebuffer);
            framebuffer_texture(framebuffer_target, color_attachment, GL_TEXTURE_2D, textures[3], 0);
            if (check_framebuffer(framebuffer_target) != 0x8CD5) return false;
            glViewport(0, 0, width, height);
            glUseProgram(program);
            glUniform1i(count_location, positions.size());
            glUniform1i(width_location, width);
            glBindVertexArray(vao);
            draw_arrays(GL_TRIANGLES, 0, 3);
            if (totals)
            {
                glReadPixels(0, 0, width, height, rg_format, GL_FLOAT, output.data());
                stats.readback_bytes = output.size() * sizeof(float);
            }
            if (glGetError() != 0) return false;
        }
        if (totals)
        {
            for (size_t index = 0; index < positions.size(); ++index)
            {
                const double total = static_cast<double>(output[index * 2]) + output[index * 2 + 1];
                if (!std::isfinite(total)) return false;
                totals->push_back(total);
            }
        }
        saved_permutation = noise.permutation();
        stats.samples = positions.size();
        stats.builds = ++builds;
        stats.generation_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - generation_start).count();
        stats.accelerated = true;
        if (resident) *resident = {textures[3], width, height, static_cast<int>(positions.size()), builds, context};
        return true;
    }

    bool generate_horizon_mesh_gpu(const std::vector<std::array<double, 3>>& positions,
        const PerlinNoise& noise, std::vector<double>& totals)
    {
        return generate_mesh(positions, noise, &totals, nullptr);
    }

    bool generate_horizon_mesh_gpu_resident(const std::vector<std::array<double, 3>>& positions,
        const PerlinNoise& noise, HorizonMeshResident& result)
    {
        result = {};
        if (!performance_enabled(28))
        {
            stats = {};
            return false;
        }
        return generate_mesh(positions, noise, nullptr, &result);
    }

    const HorizonMeshGpuStats &horizon_mesh_gpu_stats() { return stats; }
}
