
#include <cmath>
#include <algorithm>
#include <iostream>
#include "sphere_impostor.h"
// Declarations only (does not define IMGL3W_IMPL), so this resolves against the same
// function-pointer table imgui_impl_opengl3.cpp (which does define IMGL3W_IMPL) fills in via
// ImGui_ImplOpenGL3_Init() at startup -- no separate loader init necessary here. See that
// header's own comment for why this is the supported way to get GL 2.0+/3.0+ symbols outside
// of imgui_impl_opengl3.cpp in this codebase.
//
// This loader is also deliberately stripped down to only the symbols imgui_impl_opengl3.cpp
// itself happens to call, which excludes a few otherwise-ordinary GL calls (glDrawArrays,
// glBindAttribLocation, glUniform4f/1f/3f, GL_DYNAMIC_DRAW/GL_STATIC_DRAW). The code below is
// written to only use what's actually present: glDrawElements + a static index buffer instead
// of glDrawArrays, glGetAttribLocation (post-link query) instead of glBindAttribLocation,
// every per-draw value (color, sphere center, ray direction) passed as a per-vertex attribute
// instead of a uniform, and GL_STREAM_DRAW throughout.
#include "imgui/backends/imgui_impl_opengl3_loader.h"

using namespace alienorum;

namespace alienorum
{
    // Per-draw payload for the AddCallback below. Allocated with `new` at queue time (ImGui's
    // draw list only stores the pointer, not the data -- the callback doesn't actually run
    // until end-of-frame rendering, well after queue_sphere_impostor() returns), and freed by
    // the callback itself once consumed. Standard idiom for ImGui AddCallback with per-draw
    // userdata.
    struct SphereImpostorParams
    {
        // Quad corners, NDC.
        float ndc_x0, ndc_y0, ndc_x1, ndc_y1;
        // "ray xy" (X/Z, Y/Z, i.e. zdes/zoom) at each of the 4 corners, in the same order as
        // the NDC corners above -- these interpolate exactly across the quad because they're
        // computed directly from (and proportional to, modulo the fixed dispcx/dispcy affine
        // map) the screen position itself, so no perspective-correction subtlety applies; the
        // fragment shader reconstructs each pixel's true camera-space ray direction from this.
        float rayxy[4][2];
        // Sphere center in camera space, scaled by 1/r (so the shader can treat the sphere as
        // unit-radius -- keeps the ray-sphere quadratic's coefficients O(1) instead of O(planet
        // radius in meters), which matters for float32 precision).
        float ccx, ccy, ccz;
        float r, g, b, a;
    };

    static GLuint s_program = 0;
    static GLuint s_vao = 0, s_vbo = 0, s_ebo = 0;
    static GLint s_aPosLoc = -1, s_aRayXYLoc = -1, s_aCenterLoc = -1, s_aColorLoc = -1;

    // GLSL 130 / GL 3.0 core -- the only configuration this project actually builds under
    // today (see alienorum.cpp's GL context selection: on Linux, neither
    // IMGUI_IMPL_OPENGL_ES2/ES3 nor __APPLE__ is defined, so it always takes the "GL 3.0 +
    // GLSL 130" branch). A GLES2 variant (attribute/varying, gl_FragColor) would have to be
    // added here if this project ever targets that profile.
    static const char *kVertexShaderSrc =
        "#version 130\n"
        "in vec2 aPos;\n"
        "in vec2 aRayXY;\n"
        "in vec3 aCenter;\n"
        "in vec4 aColor;\n"
        "out vec2 vRayXY;\n"
        "out vec3 vCenter;\n"
        "out vec4 vColor;\n"
        "void main()\n"
        "{\n"
        "    vRayXY = aRayXY;\n"
        "    vCenter = aCenter;\n"
        "    vColor = aColor;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    // Unlit placeholder for this implementation phase: fills the true perspective silhouette
    // (found by intersecting each fragment's actual camera-space ray against the sphere, not
    // by approximating it as a screen-space circle -- that approximation is only valid when
    // the sphere is far enough away / small enough on screen that perspective distortion
    // across its silhouette is negligible, which breaks down badly at close range, e.g. a
    // low-orbit satellite looking at a planet) with a flat color. Per-pixel normal-based
    // lighting and texturing land in later phases; the normal (hit - center) is already
    // available here (`n`) for that purpose.
    static const char *kFragmentShaderSrc =
        "#version 130\n"
        "in vec2 vRayXY;\n"
        "in vec3 vCenter;\n"
        "in vec4 vColor;\n"
        "out vec4 FragColor;\n"
        "void main()\n"
        "{\n"
        "    vec3 dir = vec3(vRayXY.x, -vRayXY.y, 1.0);\n"
        "    vec3 oc = -vCenter;\n"
        "    float a = dot(dir, dir);\n"
        "    float b = 2.0 * dot(dir, oc);\n"
        "    float c = dot(oc, oc) - 1.0;\n"      // sphere is unit-radius in this (1/r-scaled) space
        "    float disc = b*b - 4.0*a*c;\n"
        "    if (disc < 0.0) discard;\n"
        "    float t = (-b - sqrt(disc)) / (2.0*a);\n"
        "    if (t < 0.0) discard;\n"
        "    vec3 hit = dir * t;\n"
        "    vec3 n = normalize(hit - vCenter);\n"
        "    FragColor = vColor;\n"
        "}\n";

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
            std::cerr << "Sphere impostor shader compile error: " << log << std::endl;
        }
        return sh;
    }

    // Bytes per vertex: pos(2) + rayxy(2) + center(3) + color(4).
    static const int kFloatsPerVertex = 11;

    static void ensure_gl_objects()
    {
        if (s_program) return;

        GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShaderSrc);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);

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
            std::cerr << "Sphere impostor shader link error: " << log << std::endl;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        s_aPosLoc    = glGetAttribLocation(s_program, "aPos");
        s_aRayXYLoc  = glGetAttribLocation(s_program, "aRayXY");
        s_aCenterLoc = glGetAttribLocation(s_program, "aCenter");
        s_aColorLoc  = glGetAttribLocation(s_program, "aColor");

        glGenVertexArrays(1, &s_vao);
        glGenBuffers(1, &s_vbo);
        glGenBuffers(1, &s_ebo);
        glBindVertexArray(s_vao);

        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * kFloatsPerVertex, nullptr, GL_STREAM_DRAW);
        GLsizei stride = sizeof(float) * kFloatsPerVertex;
        glEnableVertexAttribArray(s_aPosLoc);
        glVertexAttribPointer(s_aPosLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(s_aRayXYLoc);
        glVertexAttribPointer(s_aRayXYLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 2));
        glEnableVertexAttribArray(s_aCenterLoc);
        glVertexAttribPointer(s_aCenterLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 4));
        glEnableVertexAttribArray(s_aColorLoc);
        glVertexAttribPointer(s_aColorLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 7));

        // GL_STATIC_DRAW isn't in this stripped loader's symbol set (see the comment at the
        // top of this file); GL_STREAM_DRAW is a harmless usage-hint mismatch for data that
        // never actually changes, not a correctness issue.
        const unsigned short indices[6] = {0, 1, 2, 0, 2, 3};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

        glBindVertexArray(0);
    }

    // ImDrawCallback. Issues the actual GL draw for one impostor. Paired at the call site
    // with an immediately-following AddCallback(ImDrawCallback_ResetRenderState, nullptr), so
    // ImGui re-establishes its own program/VAO/uniforms before drawing anything else -- we
    // don't have to save/restore any of that ourselves.
    static void render_sphere_impostor(const ImDrawList*, const ImDrawCmd *cmd)
    {
        SphereImpostorParams *p = (SphereImpostorParams*)cmd->UserCallbackData;
        ensure_gl_objects();

        float corners_x[4] = {p->ndc_x0, p->ndc_x1, p->ndc_x1, p->ndc_x0};
        float corners_y[4] = {p->ndc_y0, p->ndc_y0, p->ndc_y1, p->ndc_y1};
        float verts[4 * kFloatsPerVertex];
        for (int i = 0; i < 4; i++)
        {
            float *v = &verts[i * kFloatsPerVertex];
            v[0] = corners_x[i];
            v[1] = corners_y[i];
            v[2] = p->rayxy[i][0];
            v[3] = p->rayxy[i][1];
            v[4] = p->ccx;
            v[5] = p->ccy;
            v[6] = p->ccz;
            v[7] = p->r;
            v[8] = p->g;
            v[9] = p->b;
            v[10] = p->a;
        }

        glUseProgram(s_program);
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        delete p;
    }

    // Exact tangent-line bound for one axis pair (u,w) where w is the camera-space Z (forward)
    // component and u is either X or Y. Returns the screen-space "zdes" (pre dispcx/dispcy)
    // bound reached by rotating the (u,w) direction-to-center by +alpha or -alpha (the tangent
    // half-angle). Written with explicit 2D rotation + an explicit forward/behind-camera check,
    // rather than atan2/tan arithmetic, specifically because near-grazing tangent lines (very
    // plausible at close range -- exactly the case this replaces the old circle approximation
    // for) can point behind or perpendicular to the camera, where tan() wraps around through a
    // discontinuity instead of growing towards the correct large value.
    // kFiniteBound is deliberately large only relative to "slope" units (dimensionless X/Z or
    // Y/Z ratios) -- it exists purely to avoid literal inf/NaN reaching the arithmetic below,
    // not to define how far offscreen the resulting quad edge ends up. That's what the
    // screen-relative clamp in queue_sphere_impostor is for; a raw slope this large, run
    // through *zoom and the pixel/NDC conversion, would otherwise land at NDC coordinates in
    // the thousands, which is large enough to trip GPU clipping/rasterization guard-band
    // limits on some implementations and get the whole primitive culled instead of clipped
    // (this was bug: Jupiter disappearing from Adrastea/Metis whenever enough of it was
    // offscreen to need this fallback).
    static void tangent_bounds(double u, double w, double r, double zoom, bool flip_sign,
        double *out_min, double *out_max)
    {
        double L = sqrt(u*u + w*w);
        const double kFiniteBound = 1e4;
        if (L < 1e-6)   // object dead-on along the axis this slice ignores (e.g. straight up)
        {
            *out_min = -kFiniteBound * zoom;
            *out_max = kFiniteBound * zoom;
            return;
        }
        // r/L > 1 here means the camera sits inside this axis-pair's shadow of the sphere --
        // not an error (the camera can be outside the sphere in 3D while this 2D slice still
        // engulfs it, e.g. looking somewhat sideways from low orbit), it just means the
        // silhouette is unbounded on this screen axis. asin clamped to 1 caps alpha at 90
        // degrees, which combined with the tw<=0 fallback below still produces a sane (if
        // maximally wide) bound rather than NaN. This was bug: Earth not appearing at all from
        // low Earth orbit, where this triggered on nearly every non-nadir view.
        double alpha = asin(std::min(1.0, r / L));
        double cu = u / L, cw = w / L;   // unit vector towards center, in this 2D slice

        double vals[2];
        for (int i = 0; i < 2; i++)
        {
            double s = (i == 0) ? sin(alpha) : -sin(alpha);
            double c = cos(alpha);
            double tu = cu*c - cw*s;
            double tw = cu*s + cw*c;
            double v = (tw > 1e-6) ? (tu / tw) * zoom : (tu >= 0 ? kFiniteBound : -kFiniteBound) * zoom;
            if (flip_sign) v = -v;
            vals[i] = v;
        }
        *out_min = std::min(vals[0], vals[1]);
        *out_max = std::max(vals[0], vals[1]);
    }

    bool queue_sphere_impostor(double cx, double cy, double cz, double r, double zoom,
        double dispcx, double dispcy, ImU32 color,
        double *out_xmin, double *out_ymin, double *out_xmax, double *out_ymax)
    {
        if (r <= 0 || zoom <= 0) return false;
        if (cx*cx + cy*cy + cz*cz <= r*r) return false;   // camera genuinely inside the sphere

        double zdesXmin, zdesXmax, zdesYmin, zdesYmax;
        tangent_bounds(cx, cz, r, zoom, false, &zdesXmin, &zdesXmax);
        tangent_bounds(cy, cz, r, zoom, true,  &zdesYmin, &zdesYmax);   // Cartesian2D negates Y

        double xmin = dispcx + zdesXmin * dispcx, xmax = dispcx + zdesXmax * dispcx;
        double ymin = dispcy + zdesYmin * dispcx, ymax = dispcy + zdesYmax * dispcx;

        ImGuiIO &io = ImGui::GetIO();
        float W = io.DisplaySize.x, H = io.DisplaySize.y;
        if (W <= 0 || H <= 0) return false;

        // Clamp the final pixel bounds to a generous but screen-relative margin. Anything past
        // this is invisible regardless of the "true" tangent-line answer, so clamping here
        // loses nothing visually while keeping the NDC coordinates the GPU actually sees in a
        // sane range (see the kFiniteBound comment above for why that matters).
        double margin = 2.0 * std::max(W, H);
        xmin = std::max(xmin, -margin); xmax = std::min(xmax, W + margin);
        ymin = std::max(ymin, -margin); ymax = std::min(ymax, H + margin);

        // Re-derive zdes from the (possibly now clamped) pixel bounds, inverting the same
        // conversion used above, so each vertex's ray-direction attribute stays exactly
        // consistent with its actual screen position -- otherwise the fragment shader's
        // per-pixel interpolation of ray direction across the quad would be wrong wherever
        // clamping actually changed a corner.
        zdesXmin = (xmin - dispcx) / dispcx; zdesXmax = (xmax - dispcx) / dispcx;
        zdesYmin = (ymin - dispcy) / dispcx; zdesYmax = (ymax - dispcy) / dispcx;

        if (out_xmin) *out_xmin = xmin;
        if (out_ymin) *out_ymin = ymin;
        if (out_xmax) *out_xmax = xmax;
        if (out_ymax) *out_ymax = ymax;

        SphereImpostorParams *p = new SphereImpostorParams();
        p->ndc_x0 = (float)((xmin / W) * 2.0 - 1.0);
        p->ndc_x1 = (float)((xmax / W) * 2.0 - 1.0);
        // Screen Y grows downward; NDC Y grows upward -- ymin (smaller pixel Y, higher on
        // screen) maps to the larger NDC Y.
        p->ndc_y0 = (float)(1.0 - (ymax / H) * 2.0);
        p->ndc_y1 = (float)(1.0 - (ymin / H) * 2.0);

        // Corner order matches render_sphere_impostor(): (x0,y0) (x1,y0) (x1,y1) (x0,y1), i.e.
        // (xmin,ymax) (xmax,ymax) (xmax,ymin) (xmin,ymin) in screen pixel terms once the Y flip
        // above is accounted for -- each corner's ray direction is just its own zdes/zoom.
        double cornerZdesX[4] = {zdesXmin, zdesXmax, zdesXmax, zdesXmin};
        double cornerZdesY[4] = {zdesYmax, zdesYmax, zdesYmin, zdesYmin};
        for (int i = 0; i < 4; i++)
        {
            p->rayxy[i][0] = (float)(cornerZdesX[i] / zoom);
            p->rayxy[i][1] = (float)(cornerZdesY[i] / zoom);
        }

        p->ccx = (float)(cx / r);
        p->ccy = (float)(cy / r);
        p->ccz = (float)(cz / r);

        ImVec4 col = ImGui::ColorConvertU32ToFloat4(color);
        p->r = col.x; p->g = col.y; p->b = col.z; p->a = col.w;

        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_sphere_impostor, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return true;
    }
}
