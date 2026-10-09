#include <cmath>
#include <algorithm>
#include <iostream>
#include "sunclock_gpu.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"

using namespace alienorum;

namespace alienorum
{
    static const char *kSunclockVertexShaderSrc =
        "#version 130\n"
        "in vec2 aPos;\n"
        "in vec2 aXY;\n"
        "out vec2 vXY;\n"
        "void main()\n"
        "{\n"
        "    vXY = aXY;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    static const char *kSunclockFragmentShaderSrc =
        "#version 130\n"
        "in vec2 vXY;\n"
        "out vec4 FragColor;\n"
        "\n"
        "uniform mat4 uParams1;\n"
        "uniform mat4 uParams2;\n"
        "uniform mat4 uRotMatrix;\n"
        "uniform mat4 uCasters;\n"
        "\n"
        "uniform sampler2D uDayMap;\n"
        "uniform sampler2D uNightMap;\n"
        "uniform sampler2D uBumpMap;\n"
        "\n"
        "const float PI = 3.14159265358979323846;\n"
        "const float TWO_PI = 6.28318530717958647692;\n"
        "const float HALF_PI = 1.57079632679489661923;\n"
        "\n"
        "float disc_overlap(float R, float r, float d)\n"
        "{\n"
        "    if (d >= R + r)\n"
        "    {\n"
        "        return 0.0;\n"
        "    }\n"
        "    if (d <= r - R)\n"
        "    {\n"
        "        return 1.0;\n"
        "    }\n"
        "    if (d <= R - r)\n"
        "    {\n"
        "        return (r * r) / (R * R);\n"
        "    }\n"
        "    float d2 = d * d, R2 = R * R, r2 = r * r;\n"
        "    float lens = 0.5 * sqrt(max(0.0, (R + r - d) * (d + r - R) * (d - r + R) * (d + r + R)));\n"
        "    float a1 = atan(2.0 * lens, d2 + r2 - R2);\n"
        "    float a2 = atan(2.0 * lens, d2 + R2 - r2);\n"
        "    return clamp((r2 * a1 + R2 * a2 - lens) / (PI * R2), 0.0, 1.0);\n"
        "}\n"
        "\n"
        "void main()\n"
        "{\n"
        "    float sclk_scale = uParams1[0].x;\n"
        "    float azimuth    = uParams1[0].y;\n"
        "    float altitude   = uParams1[0].z;\n"
        "\n"
        "    float lat = altitude - sclk_scale * vXY.y;\n"
        "    if (abs(lat) > HALF_PI)\n"
        "    {\n"
        "        discard;\n"
        "    }\n"
        "\n"
        "    float lon = mod(sclk_scale * vXY.x + azimuth, TWO_PI);\n"
        "    if (lon < 0.0)\n"
        "    {\n"
        "        lon += TWO_PI;\n"
        "    }\n"
        "\n"
        "    vec2 uv = vec2(fract(lon / TWO_PI + 0.5), 0.5 - lat / PI);\n"
        "    vec2 dUVdx = dFdx(uv);\n"
        "    vec2 dUVdy = dFdy(uv);\n"
        "    if (dUVdx.x > 0.5)\n"
        "    {\n"
        "        dUVdx.x -= 1.0;\n"
        "    }\n"
        "    else if (dUVdx.x < -0.5)\n"
        "    {\n"
        "        dUVdx.x += 1.0;\n"
        "    }\n"
        "    if (dUVdy.x > 0.5)\n"
        "    {\n"
        "        dUVdy.x -= 1.0;\n"
        "    }\n"
        "    else if (dUVdy.x < -0.5)\n"
        "    {\n"
        "        dUVdy.x += 1.0;\n"
        "    }\n"
        "\n"
        "    vec3 body_axes     = uParams2[0].xyz;\n"
        "    int num_casters    = int(uParams2[0].w + 0.5);\n"
        "    vec3 light_dir     = uParams2[1].xyz;\n"
        "    float light_radius = uParams2[1].w;\n"
        "    vec3 light_pos_rel = uParams2[2].xyz;\n"
        "\n"
        "    float has_day_tex   = uParams1[3].x;\n"
        "    float has_night_tex = uParams1[3].y;\n"
        "    float has_bump_tex  = uParams1[3].z;\n"
        "    float redlight_mode = uParams1[3].w;\n"
        "\n"
        "    vec3 dir = vec3(-sin(lon) * cos(lat), sin(lat), cos(lon) * cos(lat));\n"
        "    vec3 land_unrot = dir * body_axes;\n"
        "    if (has_bump_tex > 0.5)\n"
        "    {\n"
        "        float elev = textureGrad(uBumpMap, uv, dUVdx, dUVdy).r;\n"
        "        if (elev != 0.0)\n"
        "        {\n"
        "            float cur_len = length(land_unrot);\n"
        "            if (cur_len > 0.0)\n"
        "            {\n"
        "                land_unrot *= (cur_len + elev) / cur_len;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "\n"
        "    vec3 land_rot = (uRotMatrix * vec4(land_unrot, 0.0)).xyz;\n"
        "\n"
        "    float self_luminous = uParams1[2].w;\n"
        "    float is_day = 1.0;\n"
        "    float is_night = 0.0;\n"
        "\n"
        "    if (self_luminous > 0.5)\n"
        "    {\n"
        "        is_day = 1.0;\n"
        "        is_night = 0.0;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        float cos_theta = dot(normalize(land_rot), light_dir);\n"
        "        if (cos_theta > 0.0)\n"
        "        {\n"
        "            is_day = min(1.0, pow(cos_theta, 1.0 / 3.0));\n"
        "            is_night = 0.0;\n"
        "\n"
        "            if (num_casters > 0)\n"
        "            {\n"
        "                vec3 to_light = light_pos_rel - land_rot;\n"
        "                float d_light = length(to_light);\n"
        "                if (d_light > 0.0 && light_radius > 0.0)\n"
        "                {\n"
        "                    float light_ang = asin(clamp(light_radius / d_light, 0.0, 1.0));\n"
        "                    vec3 lhat = to_light / d_light;\n"
        "                    float worst = 0.0;\n"
        "                    for (int i = 0; i < 4; i++)\n"
        "                    {\n"
        "                        if (i >= num_casters)\n"
        "                        {\n"
        "                            break;\n"
        "                        }\n"
        "                        vec4 caster = (i == 0) ? uCasters[0] : ((i == 1) ? uCasters[1] : ((i == 2) ? uCasters[2] : uCasters[3]));\n"
        "                        if (caster.w <= 0.0)\n"
        "                        {\n"
        "                            continue;\n"
        "                        }\n"
        "                        vec3 rel = caster.xyz - land_rot;\n"
        "                        float dist = length(rel);\n"
        "                        if (dist <= 0.0)\n"
        "                        {\n"
        "                            continue;\n"
        "                        }\n"
        "                        float ang = asin(clamp(caster.w / dist, 0.0, 1.0));\n"
        "                        float cosine = dot(rel / dist, lhat);\n"
        "                        float sep = acos(clamp(cosine, -1.0, 1.0));\n"
        "                        worst = max(worst, disc_overlap(light_ang, ang, sep));\n"
        "                    }\n"
        "                    if (worst > 0.0)\n"
        "                    {\n"
        "                        is_day *= max(1.0 - worst, 0.12);\n"
        "                    }\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            is_day = 0.0;\n"
        "            is_night = 1.0;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    vec3 fallback_color = uParams1[2].rgb;\n"
        "    vec3 daylight_tint  = uParams1[1].rgb;\n"
        "\n"
        "    vec3 rgb = (has_day_tex > 0.5) ? textureGrad(uDayMap, uv, dUVdx, dUVdy).rgb : fallback_color;\n"
        "    vec3 nrgb = (has_night_tex > 0.5) ? textureGrad(uNightMap, uv, dUVdx, dUVdy).rgb : (rgb * vec3(0.20, 0.25, 0.29));\n"
        "\n"
        "    vec3 col;\n"
        "    if (self_luminous > 0.5)\n"
        "    {\n"
        "        col = rgb * is_day;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        col = rgb * (is_day * daylight_tint);\n"
        "    }\n"
        "\n"
        "    if (is_night > 0.0)\n"
        "    {\n"
        "        col += nrgb * is_night;\n"
        "    }\n"
        "\n"
        "    if (redlight_mode > 0.5)\n"
        "    {\n"
        "        float r2 = min(1.0, col.r + 0.5 * col.g + 0.3 * col.b);\n"
        "        col = vec3(r2, col.g / 3.0, col.b / 3.0);\n"
        "    }\n"
        "\n"
        "    FragColor = vec4(col, 1.0);\n"
        "}\n";

    struct SunClockGpuParams
    {
        float verts[16];
        float params1[16];
        float params2[16];
        float rot_matrix[16];
        float casters[16];

        GLuint day_tex;
        GLuint night_tex;
        GLuint bump_tex;
    };

    static const int kSunclockPoolSize = 4;
    static SunClockGpuParams s_sunclock_pool[kSunclockPoolSize];
    static int s_sunclock_pool_idx = 0;

    static GLuint s_program = 0;
    static GLuint s_vao = 0, s_vbo = 0, s_ebo = 0;
    static GLint s_aPosLoc = -1, s_aXYLoc = -1;
    static GLint s_uParams1Loc = -1, s_uParams2Loc = -1, s_uRotMatrixLoc = -1, s_uCastersLoc = -1;
    static GLint s_uDayMapLoc = -1, s_uNightMapLoc = -1, s_uBumpMapLoc = -1;

    static GLuint compile_shader(GLenum type, const char *src)
    {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
            std::cerr << "Sun clock shader compile error: " << log << std::endl;
        }
        return sh;
    }

    static void ensure_gl_objects()
    {
        if (s_program)
        {
            return;
        }

        GLuint vs = compile_shader(GL_VERTEX_SHADER, kSunclockVertexShaderSrc);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kSunclockFragmentShaderSrc);

        s_program = glCreateProgram();
        glAttachShader(s_program, vs);
        glAttachShader(s_program, fs);
        glLinkProgram(s_program);
        GLint ok = 0;
        glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetProgramInfoLog(s_program, sizeof(log), nullptr, log);
            std::cerr << "Sun clock shader link error: " << log << std::endl;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        s_aPosLoc = glGetAttribLocation(s_program, "aPos");
        s_aXYLoc  = glGetAttribLocation(s_program, "aXY");

        s_uParams1Loc   = glGetUniformLocation(s_program, "uParams1");
        s_uParams2Loc   = glGetUniformLocation(s_program, "uParams2");
        s_uRotMatrixLoc = glGetUniformLocation(s_program, "uRotMatrix");
        s_uCastersLoc   = glGetUniformLocation(s_program, "uCasters");

        s_uDayMapLoc   = glGetUniformLocation(s_program, "uDayMap");
        s_uNightMapLoc = glGetUniformLocation(s_program, "uNightMap");
        s_uBumpMapLoc  = glGetUniformLocation(s_program, "uBumpMap");

        glUseProgram(s_program);
        glUniform1i(s_uDayMapLoc, 0);
        glUniform1i(s_uNightMapLoc, 1);
        glUniform1i(s_uBumpMapLoc, 2);

        glGenVertexArrays(1, &s_vao);
        glGenBuffers(1, &s_vbo);
        glGenBuffers(1, &s_ebo);
        glBindVertexArray(s_vao);

        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_STREAM_DRAW);

        GLsizei stride = sizeof(float) * 4;
        glEnableVertexAttribArray(s_aPosLoc);
        glVertexAttribPointer(s_aPosLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);

        glEnableVertexAttribArray(s_aXYLoc);
        glVertexAttribPointer(s_aXYLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 2));

        const unsigned short indices[6] = { 0, 1, 2, 0, 2, 3 };
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

        glBindVertexArray(0);
    }

    static void render_sunclock_gpu(const ImDrawList*, const ImDrawCmd *cmd)
    {
        SunClockGpuParams *p = (SunClockGpuParams*)cmd->UserCallbackData;
        ensure_gl_objects();
        if (!s_program)
        {
            return;
        }

        glDisable(GL_SCISSOR_TEST);

        glUseProgram(s_program);
        glUniformMatrix4fv(s_uParams1Loc, 1, GL_FALSE, p->params1);
        glUniformMatrix4fv(s_uParams2Loc, 1, GL_FALSE, p->params2);
        glUniformMatrix4fv(s_uRotMatrixLoc, 1, GL_FALSE, p->rot_matrix);
        glUniformMatrix4fv(s_uCastersLoc, 1, GL_FALSE, p->casters);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p->day_tex);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, p->night_tex);
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, p->bump_tex);

        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(p->verts), p->verts);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(0);
    }

    bool queue_sunclock_gpu(const SunClockGpuInput &in, double dispcx, double dispcy)
    {
        ensure_gl_objects();
        if (!s_program)
        {
            return false;
        }

        SunClockGpuParams *p = &s_sunclock_pool[s_sunclock_pool_idx];
        s_sunclock_pool_idx = (s_sunclock_pool_idx + 1) % kSunclockPoolSize;

        // Quad corners in NDC and screen-relative coordinates
        // Vertex order: (bottom-left, bottom-right, top-right, top-left)
        p->verts[0]  = -1.0f; p->verts[1]  = -1.0f; p->verts[2]  = (float)-dispcx; p->verts[3]  = (float) dispcy;
        p->verts[4]  =  1.0f; p->verts[5]  = -1.0f; p->verts[6]  = (float) dispcx; p->verts[7]  = (float) dispcy;
        p->verts[8]  =  1.0f; p->verts[9]  =  1.0f; p->verts[10] = (float) dispcx; p->verts[11] = (float)-dispcy;
        p->verts[12] = -1.0f; p->verts[13] =  1.0f; p->verts[14] = (float)-dispcx; p->verts[15] = (float)-dispcy;

        // Column 0: sclk_scale, azimuth, altitude, zoom
        p->params1[0] = (float)in.sclk_scale;
        p->params1[1] = (float)in.azimuth;
        p->params1[2] = (float)in.altitude;
        p->params1[3] = (float)in.zoom;

        // Column 1: daylight tint RGB, unused
        p->params1[4] = (float)in.daylight_tint[0];
        p->params1[5] = (float)in.daylight_tint[1];
        p->params1[6] = (float)in.daylight_tint[2];
        p->params1[7] = 0.0f;

        // Column 2: fallback RGB, self_luminous
        p->params1[8]  = (float)in.fallback_color[0];
        p->params1[9]  = (float)in.fallback_color[1];
        p->params1[10] = (float)in.fallback_color[2];
        p->params1[11] = in.self_luminous ? 1.0f : 0.0f;

        // Column 3: has_day_tex, has_night_tex, has_bump_tex, redlight_mode
        p->params1[12] = in.day_map_texture ? 1.0f : 0.0f;
        p->params1[13] = in.night_map_texture ? 1.0f : 0.0f;
        p->params1[14] = in.bump_map_texture ? 1.0f : 0.0f;
        p->params1[15] = in.redlight_mode ? 1.0f : 0.0f;

        // Column 0: body axes XYZ, num_casters
        p->params2[0] = (float)in.body_axes[0];
        p->params2[1] = (float)in.body_axes[1];
        p->params2[2] = (float)in.body_axes[2];
        p->params2[3] = (float)in.num_casters;

        // Column 1: light direction XYZ, light radius
        p->params2[4] = (float)in.light_dir[0];
        p->params2[5] = (float)in.light_dir[1];
        p->params2[6] = (float)in.light_dir[2];
        p->params2[7] = (float)in.light_radius;

        // Column 2: light relative position XYZ, unused
        p->params2[8]  = (float)in.light_pos_rel[0];
        p->params2[9]  = (float)in.light_pos_rel[1];
        p->params2[10] = (float)in.light_pos_rel[2];
        p->params2[11] = 0.0f;

        // Column 3: unused
        p->params2[12] = 0.0f;
        p->params2[13] = 0.0f;
        p->params2[14] = 0.0f;
        p->params2[15] = 0.0f;

        // Rotation matrix (already column-major 4x4)
        for (int i = 0; i < 16; i++)
        {
            p->rot_matrix[i] = (float)in.rot_matrix[i];
        }

        // Casters
        for (int i = 0; i < 4; i++)
        {
            if (i < in.num_casters && in.casters[i].radius > 0.0)
            {
                p->casters[i * 4 + 0] = (float)in.casters[i].dx;
                p->casters[i * 4 + 1] = (float)in.casters[i].dy;
                p->casters[i * 4 + 2] = (float)in.casters[i].dz;
                p->casters[i * 4 + 3] = (float)in.casters[i].radius;
            }
            else
            {
                p->casters[i * 4 + 0] = 0.0f;
                p->casters[i * 4 + 1] = 0.0f;
                p->casters[i * 4 + 2] = 0.0f;
                p->casters[i * 4 + 3] = 0.0f;
            }
        }

        p->day_tex = (GLuint)in.day_map_texture;
        p->night_tex = (GLuint)in.night_map_texture;
        p->bump_tex = (GLuint)in.bump_map_texture;

        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_sunclock_gpu, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        return true;
    }
}
