
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
// glBindAttribLocation, glUniform4f/3f/1f -- but not glUniform1i or glUniformMatrix4fv, which
// ImGui's own backend does use, for its texture sampler and projection matrix respectively --
// and GL_DYNAMIC_DRAW/GL_STATIC_DRAW). The code below is written to only use what's actually
// present: glDrawElements + a static index buffer instead of glDrawArrays, glGetAttribLocation
// (post-link query) instead of glBindAttribLocation, per-draw values that would otherwise be
// float/vec uniforms (color, sphere center, orientation basis) passed as per-vertex attributes
// instead, GL_STREAM_DRAW throughout, and glUniform1i for the one uniform that is available
// (the day-map sampler's texture unit).
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
        // Object's local +X/+Y axes, in the same camera space (see SphereImpostorInput) -- used
        // to rotate the per-pixel camera-space normal back into the object's own frame.
        float bxx, bxy, bxz, byx, byy, byz;
        float has_tex;   // 0 or 1
        GLuint tex;
        GLuint night_tex;
        float r, g, b, a;   // fallback color, used when has_tex is 0
        float lightx, lighty, lightz;   // camera space, unit length
        float tintr, tintg, tintb;
        // x=self_luminous, y=night_illum, z=has_night_tex, w=redlight_mode -- all 0/1 except y.
        float flagx, flagy, flagz, flagw;
    };

    static GLuint s_program = 0;
    static GLuint s_vao = 0, s_vbo = 0, s_ebo = 0;
    static GLint s_aPosLoc = -1, s_aRayXYLoc = -1, s_aCenterLoc = -1;
    static GLint s_aBasisXLoc = -1, s_aBasisYLoc = -1, s_aHasTexLoc = -1, s_aColorLoc = -1;
    static GLint s_aLightDirLoc = -1, s_aTintLoc = -1, s_aFlagsLoc = -1;

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
        "in vec3 aBasisX;\n"
        "in vec3 aBasisY;\n"
        "in float aHasTex;\n"
        "in vec4 aColor;\n"
        "in vec3 aLightDir;\n"
        "in vec3 aTint;\n"
        "in vec4 aFlags;\n"
        "out vec2 vRayXY;\n"
        "out vec3 vCenter;\n"
        "out vec3 vBasisX;\n"
        "out vec3 vBasisY;\n"
        "out float vHasTex;\n"
        "out vec4 vColor;\n"
        "out vec3 vLightDir;\n"
        "out vec3 vTint;\n"
        "out vec4 vFlags;\n"
        "void main()\n"
        "{\n"
        "    vRayXY = aRayXY;\n"
        "    vCenter = aCenter;\n"
        "    vBasisX = aBasisX;\n"
        "    vBasisY = aBasisY;\n"
        "    vHasTex = aHasTex;\n"
        "    vColor = aColor;\n"
        "    vLightDir = aLightDir;\n"
        "    vTint = aTint;\n"
        "    vFlags = aFlags;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    // Finds the true perspective silhouette by intersecting each fragment's actual
    // camera-space ray against the sphere, not by approximating it as a screen-space circle
    // (that approximation only holds when the sphere is far enough away / small enough on
    // screen that perspective distortion across its silhouette is negligible, which breaks
    // down badly at close range, e.g. a low-orbit satellite looking at a planet). Day-map/
    // night-map textures are sampled by rotating the hit normal back into the object's own
    // frame (via the basis vectors -- see SphereImpostorInput) to recover lat/lon matching
    // Point::from_ra_dec's convention (x=-sin(lon)cos(lat), y=sin(lat)); the fallback color is
    // used unlit when no day map is available. Lighting matches the CPU path's Lambertian
    // day/night blend (visuals.cpp's draw_sphere(), the non-GPU fallback): a cube-root-softened
    // cosine term between the surface normal and the light direction (or, for a self-luminous
    // object like a star, the view direction instead -- a limb-darkening-like falloff), a
    // white-balance daylight tint, an ambient night_illum floor, and a night-map blend on the
    // unlit side where available.
    static const char *kFragmentShaderSrc =
        "#version 130\n"
        "in vec2 vRayXY;\n"
        "in vec3 vCenter;\n"
        "in vec3 vBasisX;\n"
        "in vec3 vBasisY;\n"
        "in float vHasTex;\n"
        "in vec4 vColor;\n"
        "in vec3 vLightDir;\n"
        "in vec3 vTint;\n"
        "in vec4 vFlags;\n"   // x=self_luminous, y=night_illum, z=has_night_tex, w=redlight_mode
        "out vec4 FragColor;\n"
        "uniform sampler2D uDayMap;\n"
        "uniform sampler2D uNightMap;\n"
        "const float PI = 3.14159265358979;\n"
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
        "\n"
        "    float costerm = (vFlags.x > 0.5) ? dot(n, normalize(-hit)) : dot(n, vLightDir);\n"
        "    float isDay = clamp(pow(max(costerm, 0.0), 0.3333) + vFlags.y, 0.0, 1.0);\n"
        "\n"
        "    vec3 basisZ = cross(vBasisX, vBasisY);\n"
        "    vec3 ln = vBasisX*n.x + vBasisY*n.y + basisZ*n.z;\n"
        "    float lat = asin(clamp(ln.y, -1.0, 1.0));\n"
        "    float lon = atan(-ln.x, ln.z);\n"
        "    vec2 uv = vec2(fract((lon + PI) / (2.0*PI)), 0.5 - lat/PI);\n"
        "\n"
        "    vec3 baseColor = ((vHasTex > 0.5) ? texture(uDayMap, uv).rgb : vColor.rgb) * vTint;\n"
        "    vec3 outColor = (vFlags.z > 0.5)\n"
        "        ? isDay*baseColor + (1.0 - isDay)*texture(uNightMap, uv).rgb\n"
        "        : isDay*baseColor;\n"
        "\n"
        "    if (vFlags.w > 0.5)\n"          // redlight_mode -- see rgba_apply_redlight() in color.cpp
        "    {\n"
        "        float r2 = min(1.0, outColor.r + 0.5*outColor.g + 0.3*outColor.b);\n"
        "        outColor = vec3(r2, outColor.g/3.0, outColor.b/3.0);\n"
        "    }\n"
        "    FragColor = vec4(outColor, vColor.a);\n"
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

    // Floats per vertex: pos(2) rayxy(2) center(3) basisX(3) basisY(3) hasTex(1) color(4)
    // lightDir(3) tint(3) flags(4) = 28.
    static const int kFloatsPerVertex = 28;
    static GLint s_uDayMapLoc = -1, s_uNightMapLoc = -1;

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

        s_aPosLoc      = glGetAttribLocation(s_program, "aPos");
        s_aRayXYLoc    = glGetAttribLocation(s_program, "aRayXY");
        s_aCenterLoc   = glGetAttribLocation(s_program, "aCenter");
        s_aBasisXLoc   = glGetAttribLocation(s_program, "aBasisX");
        s_aBasisYLoc   = glGetAttribLocation(s_program, "aBasisY");
        s_aHasTexLoc   = glGetAttribLocation(s_program, "aHasTex");
        s_aColorLoc    = glGetAttribLocation(s_program, "aColor");
        s_aLightDirLoc = glGetAttribLocation(s_program, "aLightDir");
        s_aTintLoc     = glGetAttribLocation(s_program, "aTint");
        s_aFlagsLoc    = glGetAttribLocation(s_program, "aFlags");
        s_uDayMapLoc   = glGetUniformLocation(s_program, "uDayMap");
        s_uNightMapLoc = glGetUniformLocation(s_program, "uNightMap");

        // Texture units 0/1, matching the convention ImGui's own backend uses for its font/UI
        // texture on unit 0 -- safe since our AddCallback runs between ImGui draw commands,
        // and the paired ImDrawCallback_ResetRenderState immediately after re-establishes
        // ImGui's own state (including its own texture bindings) before anything else draws.
        glUseProgram(s_program);
        glUniform1i(s_uDayMapLoc, 0);
        glUniform1i(s_uNightMapLoc, 1);

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
        glEnableVertexAttribArray(s_aBasisXLoc);
        glVertexAttribPointer(s_aBasisXLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 7));
        glEnableVertexAttribArray(s_aBasisYLoc);
        glVertexAttribPointer(s_aBasisYLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 10));
        glEnableVertexAttribArray(s_aHasTexLoc);
        glVertexAttribPointer(s_aHasTexLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 13));
        glEnableVertexAttribArray(s_aColorLoc);
        glVertexAttribPointer(s_aColorLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 14));
        glEnableVertexAttribArray(s_aLightDirLoc);
        glVertexAttribPointer(s_aLightDirLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 18));
        glEnableVertexAttribArray(s_aTintLoc);
        glVertexAttribPointer(s_aTintLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 21));
        glEnableVertexAttribArray(s_aFlagsLoc);
        glVertexAttribPointer(s_aFlagsLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 24));

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
        // Zero-initialized: if a future field ever gets added to SphereImpostorParams without
        // a matching assignment below (exactly what just happened to lightDir/tint/flags after
        // an editor save collision), it reads as a defined 0 rather than whatever happened to
        // be on the stack -- a wrong-but-obviously-wrong result instead of one that can look
        // plausible enough to pass a casual glance.
        float verts[4 * kFloatsPerVertex] = {};
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
            v[7] = p->bxx;
            v[8] = p->bxy;
            v[9] = p->bxz;
            v[10] = p->byx;
            v[11] = p->byy;
            v[12] = p->byz;
            v[13] = p->has_tex;
            v[14] = p->r;
            v[15] = p->g;
            v[16] = p->b;
            v[17] = p->a;
            v[18] = p->lightx;
            v[19] = p->lighty;
            v[20] = p->lightz;
            v[21] = p->tintr;
            v[22] = p->tintg;
            v[23] = p->tintb;
            v[24] = p->flagx;
            v[25] = p->flagy;
            v[26] = p->flagz;
            v[27] = p->flagw;
        }

        glUseProgram(s_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p->tex);
        glActiveTexture(GL_TEXTURE0 + 1);   // GL_TEXTURE1 isn't in this stripped loader's symbol
                                             // set; texture unit enums are guaranteed sequential.
        glBindTexture(GL_TEXTURE_2D, p->night_tex);
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        // ImGui_ImplOpenGL3_SetupRenderState() (run right after this, via the paired
        // ImDrawCallback_ResetRenderState) never calls glActiveTexture itself -- it just binds
        // to whatever unit is already active, trusting it's unit 0. Leaving it on unit 1 here
        // was corrupting every ImGui draw after a sphere, including the font atlas (bug: UI
        // text rendering as boxes).
        glActiveTexture(GL_TEXTURE0);

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
    // offscreen to require this fallback).
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

    bool queue_sphere_impostor(const SphereImpostorInput &in, double zoom,
        double dispcx, double dispcy,
        double *out_xmin, double *out_ymin, double *out_xmax, double *out_ymax)
    {
        double cx = in.cx, cy = in.cy, cz = in.cz, r = in.r;
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

        p->bxx = (float)in.basisX[0]; p->bxy = (float)in.basisX[1]; p->bxz = (float)in.basisX[2];
        p->byx = (float)in.basisY[0]; p->byy = (float)in.basisY[1]; p->byz = (float)in.basisY[2];
        p->has_tex = in.day_map_texture ? 1.0f : 0.0f;
        p->tex = (GLuint)in.day_map_texture;
        p->night_tex = (GLuint)in.night_map_texture;

        ImVec4 col = ImGui::ColorConvertU32ToFloat4(in.fallback_color);
        p->r = col.x; p->g = col.y; p->b = col.z; p->a = col.w;

        p->lightx = (float)in.light_dir[0]; p->lighty = (float)in.light_dir[1]; p->lightz = (float)in.light_dir[2];
        p->tintr = (float)in.daylight_tint[0]; p->tintg = (float)in.daylight_tint[1]; p->tintb = (float)in.daylight_tint[2];
        p->flagx = in.self_luminous ? 1.0f : 0.0f;
        p->flagy = (float)in.night_illum;
        p->flagz = in.night_map_texture ? 1.0f : 0.0f;
        p->flagw = in.redlight_mode ? 1.0f : 0.0f;

        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_sphere_impostor, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return true;
    }
}
