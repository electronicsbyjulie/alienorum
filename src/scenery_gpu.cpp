#include "scenery_gpu.h"
#include "SDL.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

namespace alienorum
{
    bool scenery_gpu_reference = false;
    static SDL_GLContext context = nullptr;
    static GLuint program = 0, vao = 0, instances = 0;
    static bool attempted = false;
    static GLint projection_location = -1, fringe_location = -1, aa_location = -1;
    using DrawInstanced = void (APIENTRYP)(GLenum, GLint, GLsizei, GLsizei);
    using Divisor = void (APIENTRYP)(GLuint, GLuint);
    using BindAttribute = void (APIENTRYP)(GLuint, GLuint, const char*);
    using UniformFloat = void (APIENTRYP)(GLint, GLfloat);
    static DrawInstanced draw_instanced = nullptr;
    static Divisor divisor = nullptr;
    static BindAttribute bind_attribute = nullptr;
    static UniformFloat uniform_float = nullptr;
    static SceneryGpuStats stats;
    struct Run
    {
        size_t first = 0, count = 0;
        int key = 0;
    };
    struct Batch
    {
        std::vector<SceneryPlantGpu> plants;
        std::vector<Run> runs;
        float fringe = 1;
        bool aa = true;
        std::array<float, 16> projection;
    };
    static GLuint contour_texture = 0;
    static std::deque<Batch> batches;
    static size_t batch_index = 0;
    static Batch& current_batch()
    {
        if (batch_index == batches.size()) batches.emplace_back();
        return batches[batch_index];
    }

    static const char* vertex_source = R"GLSL(
#version 130
in vec4 aCrown;
in vec4 aTrunk01;
in vec4 aTrunk23;
in vec4 aCrownColor;
in vec4 aTrunkColor;
in vec2 aVisible;
in vec3 aShape;
uniform mat4 uProjection;
uniform float uFringe;
uniform bool uAA;
uniform sampler2D uContours;
out vec4 color;
vec2 point(int i, bool crown, int count)
{
    if (crown) return texelFetch(uContours, ivec2(i, int(aShape.z + 0.5)), 0).rg * aCrown.zw + aCrown.xy;
    if (i == 0) return aTrunk01.xy;
    if (i == 1) return aTrunk01.zw;
    if (i == 2) return aTrunk23.xy;
    return aTrunk23.zw;
}
vec2 normal(vec2 edge)
{
    float length2 = dot(edge, edge);
    if (length2 > 0.0) edge *= 1.0 / sqrt(length2);
    return vec2(edge.y, -edge.x);
}
void main()
{
    int segments = int(aShape.x + 0.5);
    int crown_count = (segments - 2) * 3 + (uAA ? segments * 6 : 0);
    int trunk_count = uAA ? 30 : 6;
    int vertex = gl_VertexID;
    bool crown;
    if (aShape.y > 0.5)
    {
        crown = vertex < crown_count;
        if (!crown) vertex -= crown_count;
    }
    else
    {
        crown = vertex >= trunk_count;
        if (crown) vertex -= trunk_count;
    }
    color = crown ? aCrownColor : aTrunkColor;
    if (vertex >= (crown ? crown_count : trunk_count) || (crown ? aVisible.x : aVisible.y) < 0.5)
    {
        gl_Position = vec4(-1000000.0, -1000000.0, 0.0, 1.0);
        return;
    }
    int count = crown ? segments : 4;
    int triangle = vertex / 3, corner = vertex % 3;
    int index;
    bool outer = false;
    if (triangle < count - 2) index = corner == 0 ? 0 : triangle + corner;
    else
    {
        int fringe = triangle - (count - 2);
        int current = fringe / 2;
        int previous = (current + count - 1) % count;
        if (fringe % 2 == 0)
        {
            index = corner == 0 ? current : previous;
            outer = corner == 2;
        }
        else
        {
            index = corner == 0 ? previous : current;
            outer = corner != 2;
        }
    }
    vec2 p = point(index, crown, count);
    if (uAA)
    {
        vec2 n = (normal(p - point((index + count - 1) % count, crown, count))
            + normal(point((index + 1) % count, crown, count) - p)) * 0.5;
        float length2 = dot(n, n);
        if (length2 > 0.000001) n *= min(1.0 / length2, 100.0);
        p += n * uFringe * 0.5 * (outer ? 1.0 : -1.0);
    }
    if (outer) color.a = 0.0;
    gl_Position = uProjection * vec4(p, 0.0, 1.0);
}
)GLSL";
    static const char* fragment_source = R"GLSL(
#version 130
in vec4 color;
out vec4 FragColor;
void main()
{
    FragColor = color;
}
)GLSL";

    static GLuint compile(GLenum type, const char* source)
    {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success) return shader;
        glDeleteShader(shader);
        return 0;
    }

    void clear_scenery_gpu()
    {
        if (context && context == SDL_GL_GetCurrentContext())
        {
            if (contour_texture) glDeleteTextures(1, &contour_texture);
            if (instances) glDeleteBuffers(1, &instances);
            if (vao) glDeleteVertexArrays(1, &vao);
            if (program) glDeleteProgram(program);
        }
        program = vao = instances = 0;
        context = nullptr;
        attempted = false;
        contour_texture = 0;
        batches.clear();
        batch_index = 0;
        stats = {};
    }

    bool prepare_scenery_gpu()
    {
        // The previous frame's callbacks have completed before this frame starts.
        for (auto& batch : batches)
        {
            batch.plants.clear();
            batch.runs.clear();
        }
        batch_index = 0;
        stats = {};
        if (scenery_gpu_reference || !SDL_GL_GetCurrentContext()) return false;
        if (context != SDL_GL_GetCurrentContext()) clear_scenery_gpu();
        context = SDL_GL_GetCurrentContext();
        if (attempted) return program != 0;
        attempted = true;
        int major = 0, minor = 0;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
        const bool core = major > 3 || (major == 3 && minor >= 3);
        if (!core && (!SDL_GL_ExtensionSupported("GL_ARB_instanced_arrays")
            || !SDL_GL_ExtensionSupported("GL_ARB_draw_instanced"))) return false;
        draw_instanced = reinterpret_cast<DrawInstanced>(SDL_GL_GetProcAddress("glDrawArraysInstanced"));
        divisor = reinterpret_cast<Divisor>(SDL_GL_GetProcAddress("glVertexAttribDivisor"));
        bind_attribute = reinterpret_cast<BindAttribute>(SDL_GL_GetProcAddress("glBindAttribLocation"));
        uniform_float = reinterpret_cast<UniformFloat>(SDL_GL_GetProcAddress("glUniform1f"));
        if (!draw_instanced || !divisor || !bind_attribute || !uniform_float) return false;
        const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
        if (vertex && fragment)
        {
            program = glCreateProgram();
            glAttachShader(program, vertex);
            glAttachShader(program, fragment);
            const char* attributes[] = {"aCrown", "aTrunk01", "aTrunk23", "aCrownColor",
                "aTrunkColor", "aVisible", "aShape"};
            for (int i = 0; i < 7; ++i) bind_attribute(program, i, attributes[i]);
            glLinkProgram(program);
            GLint success = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &success);
            if (!success)
            {
                glDeleteProgram(program);
                program = 0;
            }
        }
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!program) return false;
        projection_location = glGetUniformLocation(program, "uProjection");
        fringe_location = glGetUniformLocation(program, "uFringe");
        aa_location = glGetUniformLocation(program, "uAA");
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &instances);
        std::vector<ImVec2> contours(512 * 526);
        for (int count = 3; count <= 512; ++count)
        {
            const float maximum = 3.14159265358979323846f * 2.0f * (float(count) - 1.0f) / float(count);
            for (int i = 0; i < count; ++i)
            {
                const float angle = (float(i) / float(count - 1)) * maximum;
                contours[count * 512 + i] = ImVec2(cosf(angle), sinf(angle));
            }
        }
        for (int step = 1; step <= 12; ++step)
        {
            ImDrawList contour(ImGui::GetDrawListSharedData());
            contour._PathArcToFastEx(ImVec2(0, 0), 1, 0, 48, step);
            for (int i = 0; i < contour._Path.Size - 1; ++i)
                contours[(513 + step) * 512 + i] = contour._Path[i];
        }
        GLint texture, row, alignment;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glGenTextures(1, &contour_texture);
        glBindTexture(GL_TEXTURE_2D, contour_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, 0x8230, 512, 526, 0, 0x8227, GL_FLOAT, contours.data());
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        return glGetError() == 0;
    }

    static void render(const ImDrawList*, const ImDrawCmd* command)
    {
        const auto& batch = *static_cast<const Batch*>(command->UserCallbackData);
        glUseProgram(program);
        glUniformMatrix4fv(projection_location, 1, GL_FALSE, batch.projection.data());
        uniform_float(fringe_location, batch.fringe);
        glUniform1i(aa_location, batch.aa);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, instances);
        glBufferData(GL_ARRAY_BUFFER, batch.plants.size() * sizeof(SceneryPlantGpu), batch.plants.data(), GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, contour_texture);
        glUniform1i(glGetUniformLocation(program, "uContours"), 0);
        for (const auto& run : batch.runs)
        {
            const size_t base = run.first * sizeof(SceneryPlantGpu);
            const size_t offsets[] = {offsetof(SceneryPlantGpu, crown), offsetof(SceneryPlantGpu, trunk),
                offsetof(SceneryPlantGpu, trunk) + sizeof(ImVec4), offsetof(SceneryPlantGpu, crown_color),
                offsetof(SceneryPlantGpu, trunk_color), offsetof(SceneryPlantGpu, crown_visible),
                offsetof(SceneryPlantGpu, segments)};
            for (int i = 0; i < 7; ++i)
            {
                glEnableVertexAttribArray(i);
                divisor(i, 1);
                const bool color = i == 3 || i == 4;
                glVertexAttribPointer(i, i == 6 ? 3 : (i == 5 ? 2 : 4), color ? GL_UNSIGNED_BYTE : GL_FLOAT,
                    color ? GL_TRUE : GL_FALSE, sizeof(SceneryPlantGpu), reinterpret_cast<void*>(base + offsets[i]));
            }
            const int vertices = (run.key - 2) * 3 + (batch.aa ? run.key * 6 : 0) + (batch.aa ? 30 : 6);
            draw_instanced(GL_TRIANGLES, 0, vertices, run.count);
            ++stats.draws;
        }
    }

    void add_scenery_plant_gpu(const SceneryPlantGpu& plant, bool crown_first, bool circle)
    {
        Batch& pending = current_batch();
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        int segments = draw->_CalcCircleAutoSegmentCount(std::max(plant.crown.z, plant.crown.w));
        int contour_row = segments;
        if (circle)
        {
            const int step = std::clamp(48 / segments, 1, 12);
            const int overstep = 48 % step;
            segments = 48 / step + (overstep ? 1 : 0);
            contour_row = 513 + step;
        }
        if (pending.runs.empty() || pending.runs.back().count >= 64
            || segments > pending.runs.back().key * 4 || pending.runs.back().key > segments * 4)
            pending.runs.push_back({pending.plants.size(), 0, segments});
        pending.runs.back().key = std::max(pending.runs.back().key, segments);
        ++pending.runs.back().count;
        pending.plants.push_back(plant);
        pending.plants.back().segments = segments;
        pending.plants.back().contour_row = contour_row;
        pending.plants.back().crown_first = crown_first ? 1 : 0;
        ++stats.instances;
    }

    void flush_scenery_gpu()
    {
        Batch& pending = current_batch();
        if (pending.plants.empty()) return;
        auto* draw = ImGui::GetBackgroundDrawList();
        pending.fringe = draw->_FringeScale;
        pending.aa = draw->Flags & ImDrawListFlags_AntiAliasedFill;
        const auto& io = ImGui::GetIO();
        pending.projection = {2 / io.DisplaySize.x, 0, 0, 0, 0, -2 / io.DisplaySize.y, 0, 0,
            0, 0, -1, 0, -1, 1, 0, 1};
        draw->AddCallback(render, &pending);
        ++batch_index;
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    const SceneryGpuStats& scenery_gpu_stats() { return stats; }
}
