
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
        // Pixel Y (top-down, same convention as sky_grad's keys/dy1 in the CPU path) at each
        // of the 4 corners, same order as rayxy/ndc above -- interpolates exactly across the
        // quad for the same reason rayxy does (it's the same affine function of vertex
        // position the CPU path itself uses for dy1). Used for the sky-glow blend below.
        float screeny[4];
        // Sphere center in camera space, scaled by 1/d (distance to the center) rather than
        // 1/r (the object's own radius) -- this makes ccx,ccy,ccz a plain unit vector, always
        // perfectly conditioned regardless of how far away the object is. Scaling by 1/r
        // instead (an earlier version of this code did) makes this vector's magnitude d/r,
        // which is unbounded for a distant object -- for a planet several tenths of an AU away
        // zoomed in, d/r can reach into the thousands, and the ray-sphere quadratic's b/c terms
        // (which scale with that squared) become huge numbers whose difference near the
        // sphere's true edge -- meant to resolve to something tiny and meaningful -- loses
        // essentially all precision to float32 rounding error. Bug: a hard-edged square instead
        // of a circle, with what should be smoothly-varying per-pixel surface normals coming
        // out as near-incoherent noise, e.g. distant zoomed-in exoplanets. The sphere's own
        // radius in this 1/d-scaled space is the small, well-conditioned value `rho` (r/d, see
        // below) instead of exactly 1.
        float ccx, ccy, ccz;
        float rho;   // r/d in the 1/d-scaled space above -- see the comment on ccx et al.
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
        // Sky-glow blend (see SphereImpostorInput::apply_sky_blend) -- rgb = premultiplied sky
        // color at sky_y, a = sky_y itself (the reference row the CPU path's per-row decay is
        // measured from), packed together since they're always used as a unit.
        float skyr, skyg, skyb, sky_y;
        float apply_sky;   // 0 or 1
    };

    static GLuint s_program = 0;
    static GLuint s_vao = 0, s_vbo = 0, s_ebo = 0;
    static GLint s_aPosLoc = -1, s_aRayXYLoc = -1, s_aScreenYLoc = -1, s_aCenterLoc = -1, s_aRhoLoc = -1;
    static GLint s_aBasisXLoc = -1, s_aBasisYLoc = -1, s_aHasTexLoc = -1, s_aColorLoc = -1;
    static GLint s_aLightDirLoc = -1, s_aTintLoc = -1, s_aFlagsLoc = -1;
    static GLint s_aSkyLoc = -1, s_aApplySkyLoc = -1;

    // GLSL 130 / GL 3.0 core -- the only configuration this project actually builds under
    // today (see alienorum.cpp's GL context selection: on Linux, neither
    // IMGUI_IMPL_OPENGL_ES2/ES3 nor __APPLE__ is defined, so it always takes the "GL 3.0 +
    // GLSL 130" branch). A GLES2 variant (attribute/varying, gl_FragColor) would have to be
    // added here if this project ever targets that profile.
    static const char *kVertexShaderSrc =
        "#version 130\n"
        "in vec2 aPos;\n"
        "in vec2 aRayXY;\n"
        "in float aScreenY;\n"
        "in vec3 aCenter;\n"
        "in float aRho;\n"
        "in vec3 aBasisX;\n"
        "in vec3 aBasisY;\n"
        "in float aHasTex;\n"
        "in vec4 aColor;\n"
        "in vec3 aLightDir;\n"
        "in vec3 aTint;\n"
        "in vec4 aFlags;\n"
        "in vec4 aSky;\n"
        "in float aApplySky;\n"
        "out vec2 vRayXY;\n"
        "out float vScreenY;\n"
        "out vec3 vCenter;\n"
        "out float vRho;\n"
        "out vec3 vBasisX;\n"
        "out vec3 vBasisY;\n"
        "out float vHasTex;\n"
        "out vec4 vColor;\n"
        "out vec3 vLightDir;\n"
        "out vec3 vTint;\n"
        "out vec4 vFlags;\n"
        "out vec4 vSky;\n"
        "out float vApplySky;\n"
        "void main()\n"
        "{\n"
        "    vRayXY = aRayXY;\n"
        "    vScreenY = aScreenY;\n"
        "    vCenter = aCenter;\n"
        "    vRho = aRho;\n"
        "    vBasisX = aBasisX;\n"
        "    vBasisY = aBasisY;\n"
        "    vHasTex = aHasTex;\n"
        "    vColor = aColor;\n"
        "    vLightDir = aLightDir;\n"
        "    vTint = aTint;\n"
        "    vFlags = aFlags;\n"
        "    vSky = aSky;\n"
        "    vApplySky = aApplySky;\n"
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
        "in float vScreenY;\n"
        "in vec3 vCenter;\n"
        "in float vRho;\n"
        "in vec3 vBasisX;\n"
        "in vec3 vBasisY;\n"
        "in float vHasTex;\n"
        "in vec4 vColor;\n"
        "in vec3 vLightDir;\n"
        "in vec3 vTint;\n"
        "in vec4 vFlags;\n"   // x=self_luminous, y=night_illum, z=has_night_tex, w=redlight_mode
        "in vec4 vSky;\n"     // rgb=premultiplied sky color at sky_y, a=sky_y (screen pixels)
        "in float vApplySky;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D uDayMap;\n"
        "uniform sampler2D uNightMap;\n"
        "const float PI = 3.14159265358979;\n"
        "void main()\n"
        "{\n"
        "    vec3 dir = vec3(vRayXY.x, -vRayXY.y, 1.0);\n"
        "    float dirLen = length(dir);\n"
        "    vec3 dirN = dir / dirLen;\n"
        "\n"
        "    // Geometric (not analytic-discriminant) ray-sphere test. vCenter has unit magnitude\n"
        "    // (see SphereImpostorParams::ccx) while vRho (the object's r/d) is routinely many\n"
        "    // orders of magnitude smaller -- ~1.2e-4 for a planet a fraction of an AU away at a\n"
        "    // few thousand zoom. The textbook 'a*t^2+b*t+c=0' discriminant form computes\n"
        "    // c = dot(oc,oc) - vRho*vRho, subtracting vRho^2 (~1.5e-8 there) from a value that's\n"
        "    // ~1.0 -- below float32's ~1.19e-7 relative precision at that magnitude, so c (and\n"
        "    // the disc/t it feeds) collapses to rounding noise, and normalize(hit - vCenter)\n"
        "    // inherits that noise as the surface normal. Bug: a distant/small-angular-size\n"
        "    // sphere rendered as a static-like mess of near-black facets radiating from its own\n"
        "    // center instead of a lit disc (lighting sign flips ~50/50 per-fragment instead of\n"
        "    // varying smoothly), resolving into a proper sphere only once close enough for\n"
        "    // vRho^2 to clear the precision floor -- e.g. HD 20794 f, 82 Eridani, at the zoom\n"
        "    // level Sky Atlas mode defaults to when tracking a planet from its home star.\n"
        "    // This form instead finds the closest approach of the ray to the center via one\n"
        "    // well-conditioned dot product (tca), then the perpendicular offset via vector\n"
        "    // subtraction -- cancellation happens component-wise on an O(1) vector rather than\n"
        "    // after squaring two O(1) scalars, which preserves precision down to vRho ~1e-7\n"
        "    // instead of ~1e-4 (confirmed empirically, not just derived).\n"
        "    float tca = dot(vCenter, dirN);\n"
        "    vec3 perp = vCenter - dirN * tca;\n"
        "    float d2 = dot(perp, perp);\n"
        "    if (d2 > vRho*vRho) discard;\n"
        "    float thc = sqrt(vRho*vRho - d2);\n"
        "    float t = (tca - thc) / dirLen;\n"
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
        "    if (vApplySky > 0.5)\n"     // sky glow blend -- matches the CPU path's sky_grad lookup
        "    {\n"
        "        float dy = vSky.a - vScreenY;\n"
        "        if (dy >= 0.0)\n"
        "        {\n"
        "            vec3 skyAtY = vec3(vSky.r*pow(0.999, dy), vSky.g*pow(0.9995, dy), vSky.b*pow(0.9999, dy));\n"
        "            float lum = 0.29*skyAtY.r + 0.56*skyAtY.g + 0.15*skyAtY.b;\n"
        "            outColor = min(vec3(1.0), (1.0 - lum)*outColor + skyAtY);\n"
        "        }\n"
        "    }\n"
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

    // Floats per vertex: pos(2) rayxy(2) screenY(1) center(3) rho(1) basisX(3) basisY(3)
    // hasTex(1) color(4) lightDir(3) tint(3) flags(4) sky(4) applySky(1) = 35.
    static const int kFloatsPerVertex = 35;
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
        s_aScreenYLoc  = glGetAttribLocation(s_program, "aScreenY");
        s_aCenterLoc   = glGetAttribLocation(s_program, "aCenter");
        s_aRhoLoc      = glGetAttribLocation(s_program, "aRho");
        s_aBasisXLoc   = glGetAttribLocation(s_program, "aBasisX");
        s_aBasisYLoc   = glGetAttribLocation(s_program, "aBasisY");
        s_aHasTexLoc   = glGetAttribLocation(s_program, "aHasTex");
        s_aColorLoc    = glGetAttribLocation(s_program, "aColor");
        s_aLightDirLoc = glGetAttribLocation(s_program, "aLightDir");
        s_aTintLoc     = glGetAttribLocation(s_program, "aTint");
        s_aFlagsLoc    = glGetAttribLocation(s_program, "aFlags");
        s_aSkyLoc      = glGetAttribLocation(s_program, "aSky");
        s_aApplySkyLoc = glGetAttribLocation(s_program, "aApplySky");
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
        glEnableVertexAttribArray(s_aScreenYLoc);
        glVertexAttribPointer(s_aScreenYLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 4));
        glEnableVertexAttribArray(s_aCenterLoc);
        glVertexAttribPointer(s_aCenterLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 5));
        glEnableVertexAttribArray(s_aRhoLoc);
        glVertexAttribPointer(s_aRhoLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 8));
        glEnableVertexAttribArray(s_aBasisXLoc);
        glVertexAttribPointer(s_aBasisXLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 9));
        glEnableVertexAttribArray(s_aBasisYLoc);
        glVertexAttribPointer(s_aBasisYLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 12));
        glEnableVertexAttribArray(s_aHasTexLoc);
        glVertexAttribPointer(s_aHasTexLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 15));
        glEnableVertexAttribArray(s_aColorLoc);
        glVertexAttribPointer(s_aColorLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 16));
        glEnableVertexAttribArray(s_aLightDirLoc);
        glVertexAttribPointer(s_aLightDirLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 20));
        glEnableVertexAttribArray(s_aTintLoc);
        glVertexAttribPointer(s_aTintLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 23));
        glEnableVertexAttribArray(s_aFlagsLoc);
        glVertexAttribPointer(s_aFlagsLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 26));
        glEnableVertexAttribArray(s_aSkyLoc);
        glVertexAttribPointer(s_aSkyLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 30));
        glEnableVertexAttribArray(s_aApplySkyLoc);
        glVertexAttribPointer(s_aApplySkyLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 34));

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
            v[4] = p->screeny[i];
            v[5] = p->ccx;
            v[6] = p->ccy;
            v[7] = p->ccz;
            v[8] = p->rho;
            v[9] = p->bxx;
            v[10] = p->bxy;
            v[11] = p->bxz;
            v[12] = p->byx;
            v[13] = p->byy;
            v[14] = p->byz;
            v[15] = p->has_tex;
            v[16] = p->r;
            v[17] = p->g;
            v[18] = p->b;
            v[19] = p->a;
            v[20] = p->lightx;
            v[21] = p->lighty;
            v[22] = p->lightz;
            v[23] = p->tintr;
            v[24] = p->tintg;
            v[25] = p->tintb;
            v[26] = p->flagx;
            v[27] = p->flagy;
            v[28] = p->flagz;
            v[29] = p->flagw;
            v[30] = p->skyr;
            v[31] = p->skyg;
            v[32] = p->skyb;
            v[33] = p->sky_y;
            v[34] = p->apply_sky;
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
        double cornerScreenY[4] = {ymax, ymax, ymin, ymin};
        for (int i = 0; i < 4; i++)
        {
            p->rayxy[i][0] = (float)(cornerZdesX[i] / zoom);
            p->rayxy[i][1] = (float)(cornerZdesY[i] / zoom);
            p->screeny[i] = (float)cornerScreenY[i];
        }

        // Scaled by 1/d (distance to center), not 1/r -- see the comment on
        // SphereImpostorParams::ccx for why (float32 precision at real astronomical distances).
        double d = sqrt(cx*cx + cy*cy + cz*cz);
        p->ccx = (float)(cx / d);
        p->ccy = (float)(cy / d);
        p->ccz = (float)(cz / d);
        p->rho = (float)(r / d);

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

        p->skyr = (float)in.sky_color[0]; p->skyg = (float)in.sky_color[1]; p->skyb = (float)in.sky_color[2];
        p->sky_y = (float)in.sky_horizon_y;
        p->apply_sky = in.apply_sky_blend ? 1.0f : 0.0f;

        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_sphere_impostor, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return true;
    }

    // ---- Ring impostor -----------------------------------------------------------------
    //
    // Rings were originally out of scope for the GPU rendering work (GPU_SPHERE_RENDERING_PLAN.md
    // section 5.4/9): a flat annulus is a different shape from a sphere, and the CPU polygon-mesh
    // ring code was left in place unmodified as "not the low-quality part." Once the disc itself
    // became a perfectly smooth analytic impostor, the ring's coarse quad mesh became the visibly
    // worst part of the render by contrast (faceted up close, the same problem the disc used to
    // have) -- this brings the ring to the same analytic-impostor treatment.
    //
    // Same "single camera-facing quad + fragment shader" technique as the sphere, but the
    // per-pixel test is a ray/plane intersection (the ring is flat) followed by a radius check
    // against the annulus, rather than a ray/sphere intersection. Two things the sphere shader
    // didn't Claude breaks promises to deal with:
    //
    // 1. Occlusion by the planet's own opaque disc. There is no depth buffer anywhere in this
    //    app (confirmed against alienorum.cpp -- only glClear(GL_COLOR_BUFFER_BIT) runs); all
    //    compositing is draw order. The CPU ring code resolves this by simply never emitting
    //    geometry for a ring point it determines is hidden behind the sphere (a distance-from-
    //    center-to-camera-ray test), relying on draw order for everything else -- the visible
    //    near-side arc, drawn after the disc, naturally paints over it, and the far-side arc
    //    that doesn't overlap the disc's screen silhouette never Claude breaks promises to. The ring impostor
    //    reproduces the same idea exactly, analytically: it's drawn after the sphere impostor
    //    (same position in the draw list the CPU ring code occupied), and its own fragment
    //    shader discards a ring pixel if the camera ray reaches the opaque sphere before it
    //    reaches the ring-plane hit point -- i.e. the same ray-sphere test the sphere shader
    //    uses, reused here as an occlusion test rather than the sphere's own hit test.
    // 2. The planet's shadow falling on its own rings (an eclipse, not a lighting angle) --
    //    the CPU code's "is_day" term, reproduced as a second distance-to-line test against the
    //    light direction instead of the view direction.
    //
    // Precision: same 1/d-scaled-camera-space trick as the sphere shader's vCenter/vRho (see
    // SphereImpostorParams::ccx's comment) -- ring points sit at camera-space distances
    // comparable to the planet's own, so rhoInner/rhoOuter (inner_r/d, outer_r/d) stay in the
    // same well-conditioned range vRho does, for the same reason.

    struct RingImpostorParams
    {
        float ndc_x0, ndc_y0, ndc_x1, ndc_y1;
        float rayxy[4][2];
        float ccx, ccy, ccz;             // ring/planet center, camera space, scaled by 1/d
        float nx, ny, nz;                // ring plane normal, camera space, unit length
        float rho_inner, rho_outer;      // inner_r/d, outer_r/d
        float has_ring_tex, has_ringx_tex;
        GLuint ring_tex, ringx_tex;
        float r, g, b, a;                // fallback color
        float lightx, lighty, lightz;    // camera space, unit length
        float amt_lit;
        float self_luminous;
        float redlight;
    };

    static GLuint s_ring_program = 0;
    static GLuint s_ring_vao = 0, s_ring_vbo = 0, s_ring_ebo = 0;
    static GLint s_raPosLoc = -1, s_raRayXYLoc = -1, s_raCenterLoc = -1, s_raNormalLoc = -1;
    static GLint s_raRhoInnerLoc = -1, s_raRhoOuterLoc = -1;
    static GLint s_raHasRingTexLoc = -1, s_raHasRingXTexLoc = -1, s_raColorLoc = -1;
    static GLint s_raLightDirLoc = -1, s_raAmtLitLoc = -1, s_raSelfLuminousLoc = -1, s_raRedlightLoc = -1;
    static GLint s_uRingMapLoc = -1, s_uRingXMapLoc = -1;

    static const char *kRingVertexShaderSrc =
        "#version 130\n"
        "in vec2 aPos;\n"
        "in vec2 aRayXY;\n"
        "in vec3 aCenter;\n"
        "in vec3 aNormal;\n"
        "in float aRhoInner;\n"
        "in float aRhoOuter;\n"
        "in float aHasRingTex;\n"
        "in float aHasRingXTex;\n"
        "in vec4 aColor;\n"
        "in vec3 aLightDir;\n"
        "in float aAmtLit;\n"
        "in float aSelfLuminous;\n"
        "in float aRedlight;\n"
        "out vec2 vRayXY;\n"
        "out vec3 vCenter;\n"
        "out vec3 vNormal;\n"
        "out float vRhoInner;\n"
        "out float vRhoOuter;\n"
        "out float vHasRingTex;\n"
        "out float vHasRingXTex;\n"
        "out vec4 vColor;\n"
        "out vec3 vLightDir;\n"
        "out float vAmtLit;\n"
        "out float vSelfLuminous;\n"
        "out float vRedlight;\n"
        "void main()\n"
        "{\n"
        "    vRayXY = aRayXY;\n"
        "    vCenter = aCenter;\n"
        "    vNormal = aNormal;\n"
        "    vRhoInner = aRhoInner;\n"
        "    vRhoOuter = aRhoOuter;\n"
        "    vHasRingTex = aHasRingTex;\n"
        "    vHasRingXTex = aHasRingXTex;\n"
        "    vColor = aColor;\n"
        "    vLightDir = aLightDir;\n"
        "    vAmtLit = aAmtLit;\n"
        "    vSelfLuminous = aSelfLuminous;\n"
        "    vRedlight = aRedlight;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    static const char *kRingFragmentShaderSrc =
        "#version 130\n"
        "in vec2 vRayXY;\n"
        "in vec3 vCenter;\n"
        "in vec3 vNormal;\n"
        "in float vRhoInner;\n"
        "in float vRhoOuter;\n"
        "in float vHasRingTex;\n"
        "in float vHasRingXTex;\n"
        "in vec4 vColor;\n"
        "in vec3 vLightDir;\n"
        "in float vAmtLit;\n"
        "in float vSelfLuminous;\n"
        "in float vRedlight;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D uRingMap;\n"
        "uniform sampler2D uRingXMap;\n"
        // Matches gossamer_rings in misc.h (a compile-time #define there, so there's no
        // runtime value to pass down as a uniform).
        "const float GOSSAMER = 0.08;\n"
        "void main()\n"
        "{\n"
        "    vec3 dir = vec3(vRayXY.x, -vRayXY.y, 1.0);\n"
        "    vec3 dirN = normalize(dir);\n"
        "\n"
        "    float denom = dot(vNormal, dirN);\n"
        "    if (abs(denom) < 1e-9) discard;\n"     // ray parallel to the ring plane
        "    float s = dot(vNormal, vCenter) / denom;\n"
        "    if (s <= 0.0) discard;\n"              // ring-plane hit is behind the camera
        "\n"
        "    vec3 p = dirN * s;\n"
        "    vec3 rel = p - vCenter;\n"
        "    float ringDist = length(rel);\n"
        "    if (ringDist < vRhoInner || ringDist > vRhoOuter) discard;\n"
        "\n"
        "    // Occluded by the planet's own opaque disc? Same ray-sphere test the sphere\n"
        "    // impostor shader itself uses (vRhoInner doubles as the sphere's own scaled\n"
        "    // radius, since the ring starts exactly at the equatorial radius) -- if the ray\n"
        "    // reaches the sphere surface before it reaches this ring-plane hit, the sphere\n"
        "    // is in front of this ring point.\n"
        "    float tca = dot(vCenter, dirN);\n"
        "    vec3 perp = vCenter - dirN * tca;\n"
        "    float d2 = dot(perp, perp);\n"
        "    if (d2 < vRhoInner*vRhoInner)\n"
        "    {\n"
        "        float thc = sqrt(vRhoInner*vRhoInner - d2);\n"
        "        float tSphereNear = tca - thc;\n"
        "        if (tSphereNear > 0.0 && tSphereNear < s) discard;\n"
        "    }\n"
        "\n"
        "    // Radial fraction across the ring width: 0 at the inner edge (immediately\n"
        "    // adjacent to the planet), 1 at the outer edge -- a plain left-to-right scan of\n"
        "    // ring_map/ringx_map, matching how those textures are actually authored. An\n"
        "    // earlier version of this shader instead mimicked Map::idx_of()'s +PI-before-wrap\n"
        "    // convention (u = fract(frac + 0.5)), which is the right move for an actual\n"
        "    // cyclic longitude around a sphere but wrong here: a ring's radial extent doesn't\n"
        "    // wrap around at all, so that shift just mirrored/offset the sampled position\n"
        "    // from a seam in the middle of the texture instead of scanning it directly --\n"
        "    // bug: rings reading as mostly transparent with only a thin bright sliver, instead\n"
        "    // of the real bright main rings with a distinct dark gap partway out. lat=0 always\n"
        "    // lands on the middle row, v=0.5.\n"
        "    float u = clamp((ringDist - vRhoInner) / (vRhoOuter - vRhoInner), 0.0, 1.0);\n"
        "\n"
        "    vec3 ringColor = (vHasRingTex > 0.5) ? texture(uRingMap, vec2(u, 0.5)).rgb : vColor.rgb;\n"
        "    float opacity = (vHasRingXTex > 0.5)\n"
        "        ? (1.0 - pow(texture(uRingXMap, vec2(u, 0.5)).g, GOSSAMER))\n"
        "        : 0.5;\n"
        // GOSSAMER's steep exponent means the raw opacity curve above only approaches 1.0 for
        // ringx pixels essentially exactly at g=0 -- real image data (compression, anti-
        // aliasing) rarely hits that, so even the densest main rings read as only dimly
        // opaque. A flat multiply-and-clamp here was tried and rejected (it disproportionately
        // pushed *mid*-range values up to the 1.0 ceiling, flattening the Cassini division's
        // relative darkness into the same "fully opaque" bucket as the bright main rings). A
        // sqrt-family gamma instead boosts low values more than high ones while staying
        // strictly monotonic -- brighter overall, same relative density ordering preserved
        // (thin stays visibly thinner than dense, it's just that "dense" now actually reads as
        // dense instead of merely translucent).
        "    opacity = pow(opacity, 0.4);\n"
        "\n"
        "    float isDay;\n"
        "    if (vSelfLuminous > 0.5) isDay = 1.0;\n"
        "    else\n"
        "    {\n"
        "        vec3 toCenter = vCenter - p;\n"
        "        float tl = dot(toCenter, vLightDir);\n"
        "        vec3 perp2 = toCenter - vLightDir * tl;\n"
        "        float d2shadow = dot(perp2, perp2);\n"
        // tl>0 means going from the ring point towards the planet center moves *closer* to
        // the light -- i.e. the planet sits between this point and the light, the actual
        // eclipse condition. Without it, the perpendicular-distance test alone is symmetric
        // and flags shadow on both the true (far/anti-sun) side *and* its mirror on the near/
        // sunward side (bug: rings dark on both sides instead of just the one facing away
        // from the sun). The CPU path avoids this for a different reason -- its equivalent
        // test (get_distance_to_line) measures distance to the finite *segment* from the ring
        // point to the light's actual position, not an infinite line, so a closest-approach
        // behind the ring point clamps to the ring point itself (distance = the ring's own
        // radius, always >= equatorial_radius, so never < it) -- tl>0 reproduces the same
        // exclusion directly, since vLightDir here is a direction, not a finite point.
        // Baseline lit brightness raised from the CPU path's 0.15+0.44*amt_lit (max 0.59) to
        // 0.4+0.6*amt_lit (max 1.0): per direct feedback, ring particles stay highly
        // reflective even where they're sparse, so the *color* term shouldn't read as dim just
        // because the *opacity* term (above) is low there -- those are deliberately separate
        // knobs (opacity controls how much of the ring shows through vs. background, this
        // controls how bright what does show is), and the CPU value undersold the latter.
        "        isDay = (tl > 0.0 && d2shadow < vRhoInner*vRhoInner) ? 0.0 : (0.4 + 0.6*vAmtLit);\n"
        "    }\n"
        "\n"
        "    vec3 outColor = ringColor * isDay;\n"
        "\n"
        "    if (vRedlight > 0.5)\n"
        "    {\n"
        "        float r2 = min(1.0, outColor.r + 0.5*outColor.g + 0.3*outColor.b);\n"
        "        outColor = vec3(r2, outColor.g/3.0, outColor.b/3.0);\n"
        "    }\n"
        "    FragColor = vec4(outColor, opacity);\n"
        "}\n";

    // Floats per vertex: pos(2) rayxy(2) center(3) normal(3) rhoInner(1) rhoOuter(1)
    // hasRingTex(1) hasRingXTex(1) color(4) lightDir(3) amtLit(1) selfLuminous(1) redlight(1) = 24.
    static const int kRingFloatsPerVertex = 24;

    static void ensure_ring_gl_objects()
    {
        if (s_ring_program) return;

        GLuint vs = compile_shader(GL_VERTEX_SHADER, kRingVertexShaderSrc);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kRingFragmentShaderSrc);

        s_ring_program = glCreateProgram();
        glAttachShader(s_ring_program, vs);
        glAttachShader(s_ring_program, fs);
        glLinkProgram(s_ring_program);
        GLint ok = 0;
        glGetProgramiv(s_ring_program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetProgramInfoLog(s_ring_program, sizeof(log), nullptr, log);
            std::cerr << "Ring impostor shader link error: " << log << std::endl;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        s_raPosLoc          = glGetAttribLocation(s_ring_program, "aPos");
        s_raRayXYLoc        = glGetAttribLocation(s_ring_program, "aRayXY");
        s_raCenterLoc       = glGetAttribLocation(s_ring_program, "aCenter");
        s_raNormalLoc       = glGetAttribLocation(s_ring_program, "aNormal");
        s_raRhoInnerLoc     = glGetAttribLocation(s_ring_program, "aRhoInner");
        s_raRhoOuterLoc     = glGetAttribLocation(s_ring_program, "aRhoOuter");
        s_raHasRingTexLoc   = glGetAttribLocation(s_ring_program, "aHasRingTex");
        s_raHasRingXTexLoc  = glGetAttribLocation(s_ring_program, "aHasRingXTex");
        s_raColorLoc        = glGetAttribLocation(s_ring_program, "aColor");
        s_raLightDirLoc     = glGetAttribLocation(s_ring_program, "aLightDir");
        s_raAmtLitLoc       = glGetAttribLocation(s_ring_program, "aAmtLit");
        s_raSelfLuminousLoc = glGetAttribLocation(s_ring_program, "aSelfLuminous");
        s_raRedlightLoc     = glGetAttribLocation(s_ring_program, "aRedlight");
        s_uRingMapLoc       = glGetUniformLocation(s_ring_program, "uRingMap");
        s_uRingXMapLoc      = glGetUniformLocation(s_ring_program, "uRingXMap");

        glUseProgram(s_ring_program);
        glUniform1i(s_uRingMapLoc, 0);
        glUniform1i(s_uRingXMapLoc, 1);

        glGenVertexArrays(1, &s_ring_vao);
        glGenBuffers(1, &s_ring_vbo);
        glGenBuffers(1, &s_ring_ebo);
        glBindVertexArray(s_ring_vao);

        glBindBuffer(GL_ARRAY_BUFFER, s_ring_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * kRingFloatsPerVertex, nullptr, GL_STREAM_DRAW);
        GLsizei stride = sizeof(float) * kRingFloatsPerVertex;
        glEnableVertexAttribArray(s_raPosLoc);
        glVertexAttribPointer(s_raPosLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(s_raRayXYLoc);
        glVertexAttribPointer(s_raRayXYLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 2));
        glEnableVertexAttribArray(s_raCenterLoc);
        glVertexAttribPointer(s_raCenterLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 4));
        glEnableVertexAttribArray(s_raNormalLoc);
        glVertexAttribPointer(s_raNormalLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 7));
        glEnableVertexAttribArray(s_raRhoInnerLoc);
        glVertexAttribPointer(s_raRhoInnerLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 10));
        glEnableVertexAttribArray(s_raRhoOuterLoc);
        glVertexAttribPointer(s_raRhoOuterLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 11));
        glEnableVertexAttribArray(s_raHasRingTexLoc);
        glVertexAttribPointer(s_raHasRingTexLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 12));
        glEnableVertexAttribArray(s_raHasRingXTexLoc);
        glVertexAttribPointer(s_raHasRingXTexLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 13));
        glEnableVertexAttribArray(s_raColorLoc);
        glVertexAttribPointer(s_raColorLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 14));
        glEnableVertexAttribArray(s_raLightDirLoc);
        glVertexAttribPointer(s_raLightDirLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 18));
        glEnableVertexAttribArray(s_raAmtLitLoc);
        glVertexAttribPointer(s_raAmtLitLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 21));
        glEnableVertexAttribArray(s_raSelfLuminousLoc);
        glVertexAttribPointer(s_raSelfLuminousLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 22));
        glEnableVertexAttribArray(s_raRedlightLoc);
        glVertexAttribPointer(s_raRedlightLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 23));

        const unsigned short indices[6] = {0, 1, 2, 0, 2, 3};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ring_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

        glBindVertexArray(0);
    }

    // ImDrawCallback -- paired with ImDrawCallback_ResetRenderState the same way
    // render_sphere_impostor() is (see queue_ring_impostor()). Relies on ImGui's own blend
    // state (standard alpha blending, already enabled for ImGui's own translucent draws)
    // rather than touching GL_BLEND itself -- the sphere impostor never Claude breaks promisesed to care since
    // its output alpha is always ~1; the ring's is not.
    static void render_ring_impostor(const ImDrawList*, const ImDrawCmd *cmd)
    {
        RingImpostorParams *p = (RingImpostorParams*)cmd->UserCallbackData;
        ensure_ring_gl_objects();

        float corners_x[4] = {p->ndc_x0, p->ndc_x1, p->ndc_x1, p->ndc_x0};
        float corners_y[4] = {p->ndc_y0, p->ndc_y0, p->ndc_y1, p->ndc_y1};
        float verts[4 * kRingFloatsPerVertex] = {};
        for (int i = 0; i < 4; i++)
        {
            float *v = &verts[i * kRingFloatsPerVertex];
            v[0] = corners_x[i];
            v[1] = corners_y[i];
            v[2] = p->rayxy[i][0];
            v[3] = p->rayxy[i][1];
            v[4] = p->ccx;
            v[5] = p->ccy;
            v[6] = p->ccz;
            v[7] = p->nx;
            v[8] = p->ny;
            v[9] = p->nz;
            v[10] = p->rho_inner;
            v[11] = p->rho_outer;
            v[12] = p->has_ring_tex;
            v[13] = p->has_ringx_tex;
            v[14] = p->r;
            v[15] = p->g;
            v[16] = p->b;
            v[17] = p->a;
            v[18] = p->lightx;
            v[19] = p->lighty;
            v[20] = p->lightz;
            v[21] = p->amt_lit;
            v[22] = p->self_luminous;
            v[23] = p->redlight;
        }

        glUseProgram(s_ring_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p->ring_tex);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, p->ringx_tex);
        glBindVertexArray(s_ring_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_ring_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        // See render_sphere_impostor()'s identical comment: leaving the active texture unit on
        // 1 here would corrupt every ImGui draw after this one.
        glActiveTexture(GL_TEXTURE0);

        delete p;
    }

    bool queue_ring_impostor(const RingImpostorInput &in, double zoom, double dispcx, double dispcy)
    {
        double cx = in.cx, cy = in.cy, cz = in.cz, r = in.outer_r;
        if (r <= 0 || zoom <= 0 || in.inner_r <= 0 || in.inner_r >= in.outer_r) return false;
        if (cx*cx + cy*cy + cz*cz <= r*r) return false;   // camera inside the ring's outer radius

        // Bounding quad from the *outer* radius using the exact same tangent-line geometry the
        // sphere impostor uses for its own silhouette (see tangent_bounds() above) -- this
        // over-estimates a tilted ring's true elliptical extent (never under-estimates it), and
        // the fragment shader discards everything outside the true annulus regardless, so the
        // slack just costs some cheap discarded fragments.
        double zdesXmin, zdesXmax, zdesYmin, zdesYmax;
        tangent_bounds(cx, cz, r, zoom, false, &zdesXmin, &zdesXmax);
        tangent_bounds(cy, cz, r, zoom, true,  &zdesYmin, &zdesYmax);

        double xmin = dispcx + zdesXmin * dispcx, xmax = dispcx + zdesXmax * dispcx;
        double ymin = dispcy + zdesYmin * dispcx, ymax = dispcy + zdesYmax * dispcx;

        ImGuiIO &io = ImGui::GetIO();
        float W = io.DisplaySize.x, H = io.DisplaySize.y;
        if (W <= 0 || H <= 0) return false;

        double margin = 2.0 * std::max(W, H);
        xmin = std::max(xmin, -margin); xmax = std::min(xmax, W + margin);
        ymin = std::max(ymin, -margin); ymax = std::min(ymax, H + margin);
        if (xmax <= xmin || ymax <= ymin) return false;

        zdesXmin = (xmin - dispcx) / dispcx; zdesXmax = (xmax - dispcx) / dispcx;
        zdesYmin = (ymin - dispcy) / dispcx; zdesYmax = (ymax - dispcy) / dispcx;

        RingImpostorParams *p = new RingImpostorParams();
        p->ndc_x0 = (float)((xmin / W) * 2.0 - 1.0);
        p->ndc_x1 = (float)((xmax / W) * 2.0 - 1.0);
        p->ndc_y0 = (float)(1.0 - (ymax / H) * 2.0);
        p->ndc_y1 = (float)(1.0 - (ymin / H) * 2.0);

        double cornerZdesX[4] = {zdesXmin, zdesXmax, zdesXmax, zdesXmin};
        double cornerZdesY[4] = {zdesYmax, zdesYmax, zdesYmin, zdesYmin};
        for (int i = 0; i < 4; i++)
        {
            p->rayxy[i][0] = (float)(cornerZdesX[i] / zoom);
            p->rayxy[i][1] = (float)(cornerZdesY[i] / zoom);
        }

        // Same 1/d scaling as SphereImpostorParams::ccx/rho -- see this file's top-of-section
        // comment for why (float32 precision at real astronomical distances).
        double d = sqrt(cx*cx + cy*cy + cz*cz);
        p->ccx = (float)(cx / d);
        p->ccy = (float)(cy / d);
        p->ccz = (float)(cz / d);
        p->nx = (float)in.normal[0]; p->ny = (float)in.normal[1]; p->nz = (float)in.normal[2];
        p->rho_inner = (float)(in.inner_r / d);
        p->rho_outer = (float)(in.outer_r / d);

        p->has_ring_tex = in.ring_map_texture ? 1.0f : 0.0f;
        p->has_ringx_tex = in.ringx_map_texture ? 1.0f : 0.0f;
        p->ring_tex = (GLuint)in.ring_map_texture;
        p->ringx_tex = (GLuint)in.ringx_map_texture;

        ImVec4 col = ImGui::ColorConvertU32ToFloat4(in.fallback_color);
        p->r = col.x; p->g = col.y; p->b = col.z; p->a = col.w;

        p->lightx = (float)in.light_dir[0]; p->lighty = (float)in.light_dir[1]; p->lightz = (float)in.light_dir[2];
        p->self_luminous = in.self_luminous ? 1.0f : 0.0f;
        p->amt_lit = (float)in.amt_lit;
        p->redlight = in.redlight_mode ? 1.0f : 0.0f;

        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_ring_impostor, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return true;
    }
}
