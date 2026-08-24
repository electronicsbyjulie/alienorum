
#include <cmath>
#include <algorithm>
#include <deque>
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
    // Per-draw payload for the AddCallback below. ImGui's draw list only stores the pointer, not
    // the data -- the callback does not run until end-of-frame rendering, well after
    // queue_sphere_impostor() returns -- so the payload has to outlive the queueing call.
    //
    // This used to be a `new` here and a `delete` in the callback, which is the standard idiom
    // and leaks whenever the draw list is discarded instead of rendered: the callback is the only
    // thing that frees it, and a discarded list never runs its callbacks. It comes from a pool
    // owned by this file instead (see impostor_begin_frame), so the lifetime no longer depends on
    // anything downstream actually happening.
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
        // axis_x/axis_y/axis_z/d, in the same 1/d-scaled space as ccx et al -- see
        // SphereImpostorInput's comment for what these three represent (equal on a plain
        // sphere; different for an oblate planet or a triaxial tidally-locked moon).
        float radx, rady, radz;
        // Object's local +X/+Y axes, in the same camera space (see SphereImpostorInput) -- used
        // to rotate the per-pixel camera-space normal back into the object's own frame.
        float bxx, bxy, bxz, byx, byy, byz;
        float has_tex;   // 0 or 1
        GLuint tex;
        GLuint night_tex;
        float has_bump_tex;   // 0 or 1
        GLuint bump_tex;
        float bump_strength;
        float limba, limbb;
        // Eclipse casters, laid out as the 16 floats of a column-major mat4 uniform: column i
        // (floats 4i..4i+3) is one caster's xyz offset from this object's own center plus its
        // radius, all scaled by 1/d exactly like ccx/radx above. An unused column is all zeros,
        // which the shader recognizes by its w (radius) being 0. Passed as a uniform rather than
        // as per-vertex attributes because the attribute table is full (see aBumpLimb's comment
        // in the vertex shader) -- and mat4 is the one non-scalar uniform type this project's
        // stripped GL loader can still set.
        float casters[16];
        float light_ang;    // light source's angular radius as seen from this object; 0 = no eclipse test
        // The planet's own rings shadowing it, as a second mat4 uniform (same reasoning as
        // casters above): column 0 = ring plane normal xyz + inner radius w, column 1 = outer
        // radius x + "has an opacity texture" y, the rest unused and zero. Both radii scaled by
        // 1/d like everything else. An outer radius of 0 means the body has no rings.
        float ring[16];
        GLuint ringx_tex;
        // Per-caster umbra light (rgb + strength per column, paired with casters above) and this
        // body's own atmosphere (col 0 = high color rgb + relative shell thickness, col 1 = low
        // color rgb). Both laid out as column-major mat4 uniforms, same as casters/ring.
        float caster_atm[16];
        float atm[16];
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

    // Payload pools, one entry per impostor queued this frame, handed out by the queue_*
    // functions and reset by impostor_begin_frame(). std::deque rather than std::vector because
    // the draw list is holding raw pointers into this for the rest of the frame, and a deque
    // never moves the elements it already has when it grows. Both keep whatever high-water mark
    // the busiest frame reached, so steady state does no allocation at all.
    static std::deque<SphereImpostorParams> s_sphere_pool;
    static size_t s_sphere_used = 0;

    static GLuint s_program = 0;
    static GLuint s_vao = 0, s_vbo = 0, s_ebo = 0;
    static GLint s_aPosLoc = -1, s_aRayXYLoc = -1, s_aScreenYLoc = -1, s_aCenterLoc = -1, s_aRadiiLoc = -1;
    static GLint s_aBasisXLoc = -1, s_aBasisYLoc = -1, s_aHasTexLoc = -1, s_aColorLoc = -1;
    static GLint s_aLightDirLoc = -1, s_aTintLoc = -1, s_aFlagsLoc = -1;
    static GLint s_aSkyLoc = -1, s_aApplySkyLoc = -1;
    static GLint s_aHasBumpTexLoc = -1, s_aBumpLimbLoc = -1;
    static GLint s_uCastersLoc = -1, s_uRingLoc = -1, s_uSphRingXMapLoc = -1;
    static GLint s_uCasterAtmLoc = -1, s_uAtmLoc = -1;

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
        "in vec3 aRadii;\n"
        "in vec3 aBasisX;\n"
        "in vec3 aBasisY;\n"
        "in float aHasTex;\n"
        "in vec4 aColor;\n"
        "in vec3 aLightDir;\n"
        "in vec3 aTint;\n"
        "in vec4 aFlags;\n"
        "in vec4 aSky;\n"
        "in float aApplySky;\n"
        "in float aHasBumpTex;\n"
        // x = bump strength; y, z = the quadratic limb-darkening coefficients (a, b), used only
        // when self_luminous; w = the light source's angular radius in radians, used only for
        // the eclipse test (0 = no eclipse on this object this frame). Four unrelated scalars
        // share one attribute on purpose: OpenGL guarantees only GL_MAX_VERTEX_ATTRIBS >= 16,
        // this machine reports exactly 16 (Mesa, Intel HD 2000), and the list above already uses
        // all 16. Declaring a 17th made the program fail to link, which silently killed every
        // disc in the app -- stars and planets alike, since they all come through this one
        // shader. Any future per-object scalar has to ride along in an existing attribute's
        // spare components the same way (widening one from vec3 to vec4, as the w component here
        // did, costs no extra attribute slot at all: a slot is a whole vec4 either way). The
        // name predates the third and fourth passengers.
        "in vec4 aBumpLimb;\n"
        "out vec2 vRayXY;\n"
        "out float vScreenY;\n"
        "out vec3 vCenter;\n"
        "out vec3 vRadii;\n"
        "out vec3 vBasisX;\n"
        "out vec3 vBasisY;\n"
        "out float vHasTex;\n"
        "out vec4 vColor;\n"
        "out vec3 vLightDir;\n"
        "out vec3 vTint;\n"
        "out vec4 vFlags;\n"
        "out vec4 vSky;\n"
        "out float vApplySky;\n"
        "out float vHasBumpTex;\n"
        "out vec4 vBumpLimb;\n"
        "void main()\n"
        "{\n"
        "    vRayXY = aRayXY;\n"
        "    vScreenY = aScreenY;\n"
        "    vCenter = aCenter;\n"
        "    vRadii = aRadii;\n"
        "    vBasisX = aBasisX;\n"
        "    vBasisY = aBasisY;\n"
        "    vHasTex = aHasTex;\n"
        "    vColor = aColor;\n"
        "    vLightDir = aLightDir;\n"
        "    vTint = aTint;\n"
        "    vFlags = aFlags;\n"
        "    vSky = aSky;\n"
        "    vApplySky = aApplySky;\n"
        "    vHasBumpTex = aHasBumpTex;\n"
        "    vBumpLimb = aBumpLimb;\n"
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
        "in vec3 vRadii;\n"
        "in vec3 vBasisX;\n"
        "in vec3 vBasisY;\n"
        "in float vHasTex;\n"
        "in vec4 vColor;\n"
        "in vec3 vLightDir;\n"
        "in vec3 vTint;\n"
        "in vec4 vFlags;\n"   // x=self_luminous, y=night_illum, z=has_night_tex, w=redlight_mode
        "in vec4 vSky;\n"     // rgb=premultiplied sky color at sky_y, a=sky_y (screen pixels)
        "in float vApplySky;\n"
        "in float vHasBumpTex;\n"
        "in vec4 vBumpLimb;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D uDayMap;\n"
        "uniform sampler2D uNightMap;\n"
        "uniform sampler2D uBumpMap;\n"
        // Eclipse casters -- one per column: xyz = the caster's center relative to this object's
        // own center (camera space, 1/d-scaled like vCenter), w = its radius in the same units.
        // A zero w means the column is unused. See SphereImpostorParams::casters for why this is
        // a mat4 uniform and not attributes.
        "uniform mat4 uCasters;\n"
        // The planet's own rings, shadowing it: column 0 = ring plane normal xyz, w = inner
        // radius; column 1 = x outer radius, y whether uSphRingXMap holds real opacity data.
        // Both radii 1/d-scaled like vRadii. An outer radius of 0 means "no rings".
        "uniform mat4 uRing;\n"
        "uniform sampler2D uSphRingXMap;\n"
        // Per-caster umbra light, paired column-for-column with uCasters above: xyz = the color
        // of the light refracted into that caster's shadow by its own atmosphere, w = how bright
        // it is against direct sunlight. All zero for an airless caster, whose umbra is black.
        "uniform mat4 uCasterAtm;\n"
        // This body's own atmosphere: column 0 = high-altitude scattered color rgb, w = the
        // glow's thickness as a fraction of the body's radius; column 1 = the redder low-
        // altitude color rgb. A zero w means no atmosphere.
        "uniform mat4 uAtm;\n"
        "const float PI = 3.14159265358979;\n"
        //
        // ---- Tunables ---------------------------------------------------------------------
        //
        // The three knobs worth reaching for. All are pure look, not geometry: the shapes and
        // positions of every shadow below are physical and stay put whatever these are set to.
        //
        // RING_SHADOW_DENSITY is the exponent applied to a ring's opacity before it darkens the
        // planet. LOWER makes ring shadows darker and broader: 0.4 is the exact curve the ring
        // impostor draws the rings themselves with, so the shadow is as dense as the ring
        // casting it, while 1.0 leaves only the densest rings marking the planet at all. The
        // value below sits deliberately under 0.4, on the reasoning that a ring's shadow reads
        // stronger than the ring does -- the ring is seen against its own scattered light,
        // the shadow against a bright cloud deck.
        //
        // UMBRA_REFRACTION scales the coppery light an atmosphere bends into its own shadow.
        // 0 gives a black umbra even for a world with air; higher makes a total lunar eclipse
        // glow more strongly. Note that the honest photometric value would be far below 1 -- a
        // totally eclipsed Moon is some ten thousand times fainter than a full one -- which on
        // an unexposed screen is simply invisible; this is set for legibility instead, the same
        // choice the ring brightness above already makes.
        //
        // ATM_GLOW scales the band of lit air on a planet's limb. The band's *height* is not a
        // knob: it comes from the world's own pressure scale height (see
        // Planet::estimate_scale_height()), so a hydrogen giant's puffs out and Mars's stays
        // thin, and this only says how brightly the air shines.
        "const float RING_SHADOW_DENSITY = 0.1;\n"
        "const float UMBRA_REFRACTION = 1.0;\n"
        "const float ATM_GLOW = 1.0;\n"
        
        "const float ATM_REDDEN_PHASE = 10.0;\n"
        "const float SPH_GOSSAMER = 0.08;\n"
        
        "float disc_overlap(float R, float r, float d)\n"
        "{\n"
        "    if (d >= R + r) return 0.0;\n"
        "    if (d <= r - R) return 1.0;\n"
        "    if (d <= R - r) return (r*r)/(R*R);\n"
        "    float d2 = d*d, R2 = R*R, r2 = r*r;\n"
        "    float lens = 0.5*sqrt(max(0.0, (R + r - d)*(d + r - R)*(d - r + R)*(d + r + R)));\n"
        "    float a1 = atan(2.0*lens, d2 + r2 - R2);\n"
        "    float a2 = atan(2.0*lens, d2 + R2 - r2);\n"
        "    return clamp((r2*a1 + R2*a2 - lens)/(PI*R2), 0.0, 1.0);\n"
        "}\n"
        
        "vec3 air_tint(float sun_elev, float phase)\n"
        "{\n"
        "    float redden = smoothstep(0.35, 0.02, sun_elev) * smoothstep(0.5, -0.3, phase);\n"
        "    return mix(uAtm[0].xyz, uAtm[1].xyz, pow(redden, ATM_REDDEN_PHASE)) * vTint;\n"
        "}\n"
        "vec3 finish_color(vec3 c)\n"
        "{\n"
        "    if (vApplySky > 0.5)\n"     // sky glow blend -- matches the CPU path's sky_grad lookup
        "    {\n"
        "        float dy = vSky.a - vScreenY;\n"
        "        if (dy >= 0.0)\n"
        "        {\n"
        "            vec3 skyAtY = vec3(vSky.r*pow(0.999, dy), vSky.g*pow(0.9995, dy), vSky.b*pow(0.9999, dy));\n"
        "            float lum = 0.29*skyAtY.r + 0.56*skyAtY.g + 0.15*skyAtY.b;\n"
        "            c = min(vec3(1.0), (1.0 - lum)*c + skyAtY);\n"
        "        }\n"
        "    }\n"
        "    if (vFlags.w > 0.5)\n"          // redlight_mode -- see rgba_apply_redlight() in color.cpp
        "    {\n"
        "        float r2 = min(1.0, c.r + 0.5*c.g + 0.3*c.b);\n"
        "        c = vec3(r2, c.g/3.0, c.b/3.0);\n"
        "    }\n"
        "    return c;\n"
        "}\n"
        "void main()\n"
        "{\n"
        "    vec3 dir = vec3(vRayXY.x, -vRayXY.y, 1.0);\n"
        "\n"
        "    // Ellipsoid ray intersection -- generalizes the plain-sphere geometric method this\n"
        "    // shader used before oblateness/triaxial support existed (vRadii.x==y==z reduces\n"
        "    // this exactly to that case). The precision hazard that method avoided (see the\n"
        "    // long-standing note this replaced: naive 'a*t^2+b*t+c=0' subtracts a tiny r^2 term\n"
        "    // from an O(1) value at real astronomical distances, losing essentially all\n"
        "    // precision -- bug was a static-like mess of near-black facets on e.g. 82 Eridani)\n"
        "    // is sidestepped the same way here, just in a different coordinate space: project\n"
        "    // dir/vCenter into local coordinates and divide by each axis's own radius\n"
        "    // (Dloc/Cloc), which turns the ellipsoid into a *unit* sphere at Cloc's origin --\n"
        "    // then run the identical well-conditioned tca/perp/thc steps against that unit\n"
        "    // sphere, with Dloc/Cloc standing in for dir/vCenter and radius 1 standing in for\n"
        "    // vRho.\n"
        "    //\n"
        "    // vBasisX/vBasisY (and their cross product, basisZ) are the *rows* of the local-to-\n"
        "    // camera rotation, not its columns -- see draw_sphere_gpu()'s comment on\n"
        "    // undo_to_local() for why. That means camera-space-to-local uses them as\n"
        "    // multipliers in a vector sum (v.x*basisX + v.y*basisY + v.z*basisZ), the same\n"
        "    // pattern this shader already used to recover lat/lon before ellipsoid support\n"
        "    // existed -- NOT dot products, which is what local-to-camera (the shading normal\n"
        "    // below) requires instead, precisely because it's the opposite direction. An earlier\n"
        "    // version of this code had the two backwards (dot products for camera-to-local\n"
        "    // here, a vector sum for local-to-camera below) -- bug: the texture's lat/lon\n"
        "    // mapping came out rotated by the mismatch between the true local frame and this\n"
        "    // swapped one, which tracks the *camera's* azimuth/altitude (folded into\n"
        "    // undo_to_local's chain) as well as the object's own orientation -- i.e. the\n"
        "    // terminator/day-night line and map features appeared to rotate as the object's\n"
        "    // disc moved around the viewport, even though nothing about the object itself had\n"
        "    // changed.\n"
        "    vec3 basisZ = cross(vBasisX, vBasisY);\n"
        "    vec3 dirLocal = dir.x*vBasisX + dir.y*vBasisY + dir.z*basisZ;\n"
        "    vec3 centerLocal = vCenter.x*vBasisX + vCenter.y*vBasisY + vCenter.z*basisZ;\n"
        "    vec3 Dloc = dirLocal / vRadii;\n"
        "    vec3 Cloc = centerLocal / vRadii;\n"
        "    float DlocLen = length(Dloc);\n"
        "    vec3 DlocN = Dloc / DlocLen;\n"
        "\n"
        "    float tca = dot(Cloc, DlocN);\n"
        "    vec3 perp = Cloc - DlocN * tca;\n"
        "    float d2 = dot(perp, perp);\n"
        "\n"
        // The atmosphere is a shell just outside the solid body, in this same normalized space
        // where the body itself is the unit sphere -- so it is the sphere of radius 1+atmRel,
        // and `b`, the ray's closest approach to the center, decides everything: past the shell
        // there is nothing to draw, inside the unit sphere the ray strikes ground, and between
        // the two it passes through air and out the other side. Treating the shell as a constant
        // *fraction* of each axis rather than a constant height makes it slightly thicker over
        // an oblate world's equator than its poles, which is both what a spinning atmosphere
        // actually does and a rounding error next to the glow's own exponential falloff.
        "    float b = sqrt(d2);\n"
        "    float atmRel = uAtm[0].w;\n"
        "    float atmR = 1.0 + atmRel;\n"
        "    bool solid = (d2 <= 1.0);\n"
        "    bool inAir = (atmRel > 0.0 && b < atmR && tca > 0.0 && vFlags.x < 0.5);\n"
        "    if (!solid && !inAir) discard;\n"
        "\n"
        // What color the air comes out is decided by which side of it we are standing on, not by
        // altitude. Looking at a world with the sun behind *us*, we see sunlight its air has
        // *scattered* back at us, and that light is blue for exactly the reason the sky is.
        // Looking at one with the sun behind *it*, we instead see the sunlight that got
        // *through* the air on its way past, which is the one thing scattering leaves behind:
        // red. So a gibbous Earth wears a thin blue thread, and a new Earth -- which is what an
        // observer standing on the Moon sees during a lunar eclipse, the Earth having just
        // covered the sun -- wears the copper ring of all its sunrises and sunsets at once. The
        // same crossing-over is why the shadow behind it is copper too.
        //
        // Two earlier versions of this got it wrong in opposite directions, and both are worth
        // recording. The first drove the color off altitude, orange low and blue high: a real
        // effect, but not the one that decides this, and on its own it put an orange hoop around
        // a 95%-lit Earth. The second drove it off the body's phase alone -- which at least
        // pinned the two ends correctly, but made the *whole ring* change color together, so a
        // 37%-lit Earth came out orange all the way round its limb when it should be blue along
        // nearly all of it. Neither is a knob problem; both were the wrong quantity.
        //
        // air_tint() below takes the right one. See its own comment.
        "    float phase = dot(vLightDir, normalize(-vCenter));\n"
        "    float cosView = sqrt(max(0.0, 1.0 - min(d2, 1.0)));\n"
        "    float maxpath = 2.0*sqrt(max(1e-12, atmR*atmR - 1.0));\n"
        "\n"
        // A ray that clears the body entirely but still crosses its air is the bright thread
        // around the limb, and it is finished here: nothing below this point has a surface to
        // work with. Its brightness is the length of air the ray crossed -- longest right at the
        // limb, shrinking to nothing at the shell's outer edge, which is what draws the arc as an
        // arc -- times how dense the air is that far up, thinning exponentially as a real
        // atmosphere does.
        //
        // The air has to be in sunlight to glow. Out here, beyond the silhouette, the patch being
        // looked through sits at the ray's closest approach to the center (-perp, by
        // construction), which is well conditioned precisely because it is out near the limb.
        // The soft, deliberately asymmetric step is the terminator: the thread fades out past it
        // rather than ending at a line, because air stays lit from above long after the ground
        // under it is dark -- twilight, seen from the outside.
        //
        // Emitted as an ordinary translucent fragment (color, with the glow's strength as its
        // alpha) rather than premultiplied, since ImGui's own blending is the plain kind -- the
        // same arrangement the ring impostor relies on.
        "    if (!solid)\n"
        "    {\n"
        "        float alt = b - 1.0;\n"
        "        float hs = max(atmRel*0.25, 1e-9);\n"
        "        float airAmt = clamp((2.0*sqrt(max(0.0, atmR*atmR - d2))/maxpath) * exp(-alt/hs) * ATM_GLOW, 0.0, 1.0);\n"
        "        vec3 airLocal = -perp * vRadii;\n"
        "        vec3 airN = normalize(vec3(dot(vBasisX, airLocal), dot(vBasisY, airLocal), dot(basisZ, airLocal)));\n"
        "        float sunElev = dot(airN, vLightDir);\n"
        "        airAmt *= smoothstep(-0.35, 0.25, sunElev);\n"
        "        FragColor = vec4(finish_color(air_tint(sunElev, phase)), airAmt*vColor.a);\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    float thc = sqrt(1.0 - d2);\n"
        "    float t = (tca - thc) / DlocLen;\n"
        "    if (t < 0.0) discard;\n"
        "    vec3 hit = dir * t;\n"
        "\n"
        "    // Local-frame point on the *unit* sphere -- by construction this already *is* the\n"
        "    // local-space surface position, directly usable for lat/lon with no further\n"
        "    // transform (unlike the old plain-sphere code, which had to recover local\n"
        "    // coordinates from a camera-space normal that was also its position). Re-\n"
        "    // normalized to absorb float error, same spirit as the old normalize(hit-vCenter).\n"
        "    vec3 hitLocal = normalize(t * Dloc - Cloc);\n"
        "\n"
        "    // Shading normal: the *gradient* of the ellipsoid's implicit surface\n"
        "    // x^2/a^2+y^2/b^2+z^2/c^2=1, which is (x/a^2,y/b^2,z/c^2) in local coordinates --\n"
        "    // only equal to the position itself (hitLocal) when a=b=c, i.e. a true sphere. One\n"
        "    // more division by each radius beyond the one already folded into hitLocal, then\n"
        "    // mapped back into camera space -- local-to-camera, so dot products against the\n"
        "    // basis rows (see this block's opening comment), not a vector sum.\n"
        "    vec3 localGrad = hitLocal / vRadii;\n"
        "    vec3 n = normalize(vec3(dot(vBasisX, localGrad), dot(vBasisY, localGrad), dot(basisZ, localGrad)));\n"
        "\n"
        "    float lat = asin(clamp(hitLocal.y, -1.0, 1.0));\n"
        "    float lon = atan(-hitLocal.x, hitLocal.z);\n"
        "    vec2 uv = vec2(fract((lon + PI) / (2.0*PI)), 0.5 - lat/PI);\n"
        "\n"
        "    // Bump mapping: perturbs only the *shading* normal from a height-field gradient,\n"
        "    // sampled as two small finite-difference steps in uv -- the ray/ellipsoid\n"
        "    // intersection above (hence the silhouette and hit point) is entirely unaffected,\n"
        "    // same tradeoff any bump map makes (fakes relief via lighting, doesn't actually\n"
        "    // displace the surface). Real per-vertex displacement, matching what the CPU path\n"
        "    // does with its polygon mesh, isn't available here -- there are no vertices, only\n"
        "    // one quad -- so this is the GPU-impostor equivalent: without it the terminator\n"
        "    // and any grazing-light limb reads as a perfectly smooth curve no real rocky body\n"
        "    // has, since there's no faceted mesh silhouette to (accidentally) read as texture.\n"
        "    // Tangent directions are the analytic d(local position)/d(lon or lat) at hitLocal\n"
        "    // (see Point::from_ra_dec's convention: x=-sin(lon)cos(lat), y=sin(lat),\n"
        "    // z=cos(lon)cos(lat)) on the *unit* sphere parametrization, not the true ellipsoid\n"
        "    // surface tangent -- close enough for a lighting perturbation on the near-spherical\n"
        "    // bodies (moons, rocky planets) that actually carry bump data.\n"
        "    if (vHasBumpTex > 0.5)\n"
        "    {\n"
        "        // duv sized to the texture's own texel spacing (not an arbitrary fixed step)\n"
        "        // -- the earlier version used a fixed duv=0.004 (~4 texels on a 1024-wide map),\n"
        "        // averaging height differences over a multi-texel span before they even reached\n"
        "        // the slope conversion below, on top of that conversion itself being missing\n"
        "        // entirely (raw meters-per-uv-step was used directly as the perturbation, off by\n"
        "        // the several-more-orders-of-magnitude factor derived below) -- bug: a\n"
        "        // perturbation too small by roughly 1e5-1e6x to move the normal at all\n"
        "        // perceptibly, reading as a perfectly smooth terminator regardless of the actual\n"
        "        // bump data.\n"
        "        vec2 texSize = textureSize(uBumpMap, 0);\n"
        "        vec2 duv = 1.0 / texSize;\n"
        "        float h0 = texture(uBumpMap, uv).r;\n"
        "        float hU = texture(uBumpMap, vec2(fract(uv.x + duv.x), uv.y)).r;\n"
        "        float hV = texture(uBumpMap, vec2(uv.x, clamp(uv.y + duv.y, 0.0, 1.0))).r;\n"
        "        float dhdu = hU - h0;\n"
        "        float dhdv = hV - h0;\n"
        "\n"
        "        float cl = sqrt(max(0.0, 1.0 - hitLocal.y*hitLocal.y));\n"
        "        if (cl > 1e-4)\n"
        "        {\n"
        "            vec3 tLonLocal = vec3(-hitLocal.z, 0.0, hitLocal.x);\n"
        "            vec3 tLatLocal = vec3(-hitLocal.x*hitLocal.y/cl, cl, -hitLocal.z*hitLocal.y/cl);\n"
        "            vec3 tLon = normalize(vec3(dot(vBasisX, tLonLocal), dot(vBasisY, tLonLocal), dot(basisZ, tLonLocal)));\n"
        "            vec3 tLat = normalize(vec3(dot(vBasisX, tLatLocal), dot(vBasisY, tLatLocal), dot(basisZ, tLatLocal)));\n"
        "\n"
        "            // dhdu is meters of height change over duv.x of *uv*. u spans a full 2*PI\n"
        "            // of longitude and v spans PI of latitude (see the uv formula above), so\n"
        "            // dividing by (duv*2*PI) or (duv*PI) converts that to meters of height\n"
        "            // change per *radian* of arc -- still not a dimensionless slope on its own\n"
        "            // (arc length is radius*angle, not just angle), but vBumpLimb.x supplies\n"
        "            // the remaining length-scale division on the CPU side (see\n"
        "            // SphereImpostorInput::bump_strength for what it's actually divided by and\n"
        "            // why), so only the angle-per-duv conversion has to happen here.\n"
        "            float slopeU = (dhdu / (duv.x * 2.0*PI)) * vBumpLimb.x;\n"
        "            float slopeV = (dhdv / (duv.y * PI)) * vBumpLimb.x;\n"
        "\n"
        "            // Clamped, not left to grow unbounded -- a steep enough measured slope\n"
        "            // (crater rims, or just a strong vBumpLimb.x) can otherwise push the\n"
        "            // perturbation vector's magnitude well past the unit-length true normal,\n"
        "            // flipping the *shading* normal to face away from the surface entirely --\n"
        "            // most visible right at the grazing limb, where the true normal is already\n"
        "            // near-perpendicular to the view and even a moderate flip reads as a bright\n"
        "            // pixel where the silhouette edge should be sharp and dark.\n"
        "            vec3 perturb = slopeU*tLon + slopeV*tLat;\n"
        "\n"
        "            // Longitude -- and therefore tLonLocal's own direction -- is undefined\n"
        "            // exactly at a pole; tLonLocal's length is cl itself (it's the analytic\n"
        "            // d(position)/d(longitude) on the unit sphere), so normalizing it where cl\n"
        "            // is merely small, not the 1e-4 the guard above catches, still amplifies\n"
        "            // ordinary per-pixel float noise in hitLocal.x/z into a essentially random\n"
        "            // direction. That was invisible with smooth, low-amplitude terrain bump\n"
        "            // data -- a noisy direction times a tiny dhdu/dhdv barely perturbs anything\n"
        "            // -- but craters' steep rims and bowl walls are a much bigger dhdu/dhdv, so\n"
        "            // the same instability now reads as a streaky, \"combed\" smear of shadow\n"
        "            // wherever a crater happens to land near a pole. Fading the perturbation out\n"
        "            // smoothly as cl shrinks -- rather than the hard on/off this guard used to be\n"
        "            // -- kills the artifact without introducing a visible seam at the fade edge.\n"
        "            perturb *= smoothstep(0.0, 0.15, cl);\n"
        "            float perturbLen = length(perturb);\n"
        "            const float kMaxTilt = 1.5;\n"
        "            if (perturbLen > kMaxTilt) perturb *= kMaxTilt / perturbLen;\n"
        "            n = normalize(n - perturb);\n"
        "        }\n"
        "    }\n"
        "\n"
        "    float costerm = (vFlags.x > 0.5) ? dot(n, normalize(-hit)) : dot(n, vLightDir);\n"
        "    float mu = max(costerm, 0.0);\n"
        "\n"
        // Eclipses. Each caster hides some fraction of the light source's disc as seen from
        // *this* surface point specifically -- not from the object's center -- which is the whole
        // reason the shadow lands as a small dark patch that crosses the disc rather than dimming
        // the entire body at once.
        //
        // The surface point is recovered from hitLocal rather than from the `hit` above, even
        // though `hit - vCenter` is the same vector geometrically. Both terms of that difference
        // are ~unit-magnitude (see SphereImpostorParams::ccx on the 1/d scaling), so subtracting
        // them throws away precision in proportion to how small the object is on screen -- while
        // hitLocal*vRadii is already the offset from the center, built from quantities that are
        // *natively* that small, and so keeps its relative precision no matter the distance. That
        // matters here more than anywhere else in this shader: a shadow's angular geometry is
        // measured against the caster's own angular radius, which for a distant moon is itself
        // only a small fraction of the object's angular size. Rotated local-to-camera, so dot
        // products against the basis rows -- same direction (and same reason) as the shading
        // normal just above.
        "    float shadow = 1.0;\n"
        "    float umbraAmt = 0.0;\n"
        "    vec3 umbraTint = vec3(0.0);\n"
        "    if (vFlags.x < 0.5 && mu > 0.0)\n"
        "    {\n"
        "        vec3 surfLocal = hitLocal * vRadii;\n"
        "        vec3 surf = vec3(dot(vBasisX, surfLocal), dot(vBasisY, surfLocal), dot(basisZ, surfLocal));\n"
        "\n"
        // The planet's own rings, shadowing it: leave this surface point towards the light and
        // see whether the trip crosses the ring plane while still inside the annulus. The plane
        // passes through the planet's center, which is the origin of `surf`'s own frame, so the
        // crossing point's distance from the origin *is* its ring radius -- no projection, and
        // s > 0 restricts it to a crossing between the surface and the light, which is
        // what confines the shadow to the hemisphere on the far side of the ring plane from the
        // sun, exactly as it does on the real Saturn.
        //
        // Opacity comes from the same texture and the same curve the ring impostor shades the
        // rings themselves with, so a ring's shadow is always as dense as the ring casting it:
        // the Cassini division lets light through onto the cloud tops as a bright line inside
        // the dark band, and a gossamer outer ring barely marks the planet at all.
        "        if (uRing[1].x > 0.0)\n"
        "        {\n"
        "            vec3 ringN = uRing[0].xyz;\n"
        "            float denom = dot(ringN, vLightDir);\n"
        "            if (abs(denom) > 1e-9)\n"
        "            {\n"
        "                float s = -dot(ringN, surf) / denom;\n"
        "                if (s > 0.0)\n"
        "                {\n"
        "                    float rr = length(surf + vLightDir*s);\n"
        "                    if (rr >= uRing[0].w && rr <= uRing[1].x)\n"
        "                    {\n"
        "                        float u = clamp((rr - uRing[0].w) / (uRing[1].x - uRing[0].w), 0.0, 1.0);\n"
        "                        float opacity = (uRing[1].y > 0.5)\n"
        "                            ? (1.0 - pow(texture(uSphRingXMap, vec2(u, 0.5)).g, SPH_GOSSAMER))\n"
        "                            : 0.5;\n"
        "                        shadow *= 1.0 - pow(opacity, RING_SHADOW_DENSITY);\n"
        "                    }\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "\n"
        // Multiplied against any eclipse shadow below rather than min()'d with it, unlike two
        // eclipse casters against each other: a ring and a moon hide unrelated parts of the
        // star's disc, so their transmissions genuinely compound, where two moons' shadows
        // would overlap on the same part of it.
        //
        // The ring's shadow edge is hard here, while a real one is softened over the star's own
        // angular size the way an eclipse penumbra is -- roughly a thousand kilometers of blur
        // at Saturn, against a shadow band tens of thousands wide. The ring opacity's own radial
        // gradient covers for it nearly everywhere; the sharpness only really shows at a clean
        // ring edge.
        "        float eclipsed = 1.0;\n"
        "        if (vBumpLimb.w > 0.0)\n"
        "        for (int i = 0; i < 4; i++)\n"
        "        {\n"
        "            vec4 caster = uCasters[i];\n"
        "            if (caster.w <= 0.0) continue;\n"
        "            vec3 toCaster = caster.xyz - surf;\n"
        "            float dist = length(toCaster);\n"
        "            if (dist <= caster.w) { eclipsed = 0.0; break; }\n"   // surface point inside the caster
        "            float casterAng = asin(clamp(caster.w / dist, 0.0, 1.0));\n"
        "            float sep = acos(clamp(dot(toCaster / dist, vLightDir), -1.0, 1.0));\n"
        // What a body with an atmosphere lets into its own shadow. Standing in the umbra of an
        // airless caster you would see the star cleanly hidden behind a black disc; standing in
        // the umbra of one with air, that disc is ringed by a thin band of its atmosphere lit
        // from behind -- every sunrise and sunset on that world at once -- and the light bent
        // inwards from that ring is what falls on you. It is red because that is the only part
        // of it that survives a path that long through air, which is why the totally eclipsed
        // Moon turns copper instead of going out, and why an eclipsed moon of an airless world
        // would simply vanish.
        //
        // Scaled by the same coverage the shadow itself uses, so it arrives exactly as the
        // direct light leaves, and it is at its strongest where the star is most completely
        // hidden. Tracked as the deepest single contribution rather than a sum, matching how
        // `eclipsed` itself combines casters just below.
        "            float cover = disc_overlap(vBumpLimb.w, casterAng, sep);\n"
        "            float refracted = cover * uCasterAtm[i].w * UMBRA_REFRACTION;\n"
        "            if (refracted > umbraAmt) { umbraAmt = refracted; umbraTint = uCasterAtm[i].xyz; }\n"
        // min(), not a product: two casters overlapping the same patch of sky hide overlapping
        // parts of the same disc, so multiplying their two fractions would darken the overlap
        // twice over. Taking the deepest of them is exact whenever one caster's silhouette
        // contains the other's and a slight under-estimate otherwise -- and the "otherwise" is
        // two bodies eclipsing the same point of the same third body simultaneously, which is
        // not a thing anyone will be waiting to see.
        "            eclipsed = min(eclipsed, 1.0 - cover);\n"
        "        }\n"
        "        shadow *= eclipsed;\n"
        "    }\n"
        // A self-luminous body gets a real quadratic limb-darkening law, whose coefficients come
        // from the star's own T_eff and log g (Star::limb_darkening_coefficients). The fixed
        // pow(mu, 1/3) it used to share with the lit-by-a-star case reaches ZERO at the limb, so a
        // star's own edge rendered nearly black against the flare halo drawn behind it -- a dark
        // ring at exactly the disc's radius. No star darkens to black: the solar limb is still
        // near 30% of disc center in the visible.
        "    float isDay;\n"
        "    if (vFlags.x > 0.5)\n"
        "    {\n"
        "        float om = 1.0 - mu;\n"
        "        isDay = clamp(1.0 - vBumpLimb.y*om - vBumpLimb.z*om*om, 0.0, 1.0);\n"
        "    }\n"
        // The shadow scales the *direct* light only, leaving vFlags.y (the ambient night floor)
        // alone: an eclipsed patch of ground falls to exactly the brightness the object's own
        // night side has, which is what it physically is -- night, arriving early and leaving in
        // the wrong direction. On a body with a night map that also means its city lights come up
        // inside the umbra, for free, through the same isDay blend the terminator already uses.
        "    else isDay = clamp(pow(mu, 1.0/3.0)*shadow + vFlags.y, 0.0, 1.0);\n"
        "\n"
        // albedo kept separate from baseColor -- the surface's own color, before the light
        // source's white-balance tint is multiplied in. Direct sunlight gets that tint (it *is*
        // that light); the light refracted into an umbra does not, since it arrives already
        // stamped with the color of the air it came through, which is what umbraTint carries.
        "    vec3 albedo = (vHasTex > 0.5) ? texture(uDayMap, uv).rgb : vColor.rgb;\n"
        "    vec3 baseColor = albedo * vTint;\n"
        "    vec3 outColor = (vFlags.z > 0.5)\n"
        "        ? isDay*baseColor + (1.0 - isDay)*texture(uNightMap, uv).rgb\n"
        "        : isDay*baseColor;\n"
        "\n"
        // The copper light lands on the same ground the direct light does, so it carries the
        // same cosine falloff towards the terminator -- and, being an addition rather than a
        // replacement, it sits on top of whatever the night side already shows, city lights
        // included. Exactly what a total lunar eclipse looks like from a distance: the disc does
        // not go out, it changes color.
        "    outColor += umbraAmt * pow(mu, 1.0/3.0) * umbraTint * albedo;\n"
        "\n"
        // The air in front of the ground, added rather than blended: light this world's own
        // atmosphere scatters towards us on its way past, sitting on top of the surface already
        // shining through it. Only the stretch of air *in front of* the surface counts, which is
        // the chord across the shell less the part behind the ground -- a length that runs from
        // barely a scale height looking straight down to the full grazing path at the limb, so
        // the haze is a thin band hugging the edge and nothing at all across the middle of the
        // disc. The extra (1 - cosView) drives the very center to exactly zero rather than
        // merely nearly so.
        //
        // Two things here were wrong in an earlier version and are worth not repeating. The
        // sunlit test used the ray's closest approach to the center, which out at the limb is
        // fine but toward the middle of the disc points at a spot buried inside the planet and
        // normalizes to noise -- the surface normal is what a point on the ground is actually
        // lit by, and using it is what keeps the haze off the night side (bug: a brown wash over
        // the entire dark hemisphere, city lights and all) and stops the degenerate direction
        // near the disc's center from raising a lumpy mound of glow there. And the whole term
        // was colored as though seen at grazing incidence, which put a hard orange rim on a
        // fully lit Earth; it now takes its color from air_tint() like the limb thread above,
        // with the surface normal standing in for the sun's elevation over this patch of ground
        // -- which is exactly what it is.
        "    float groundSun = dot(n, vLightDir);\n"
        "    float hazePath = sqrt(max(0.0, atmR*atmR - d2)) - cosView;\n"
        "    float haze = (atmRel > 0.0 && vFlags.x < 0.5)\n"
        "        ? clamp((hazePath/maxpath) * (1.0 - cosView) * ATM_GLOW, 0.0, 1.0)\n"
        "            * smoothstep(-0.05, 0.35, groundSun)\n"
        "        : 0.0;\n"
        "    outColor += air_tint(groundSun, phase) * haze;\n"
        "\n"
        "    FragColor = vec4(finish_color(outColor), vColor.a);\n"
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

    // Floats per vertex: pos(2) rayxy(2) screenY(1) center(3) radii(3) basisX(3) basisY(3)
    // hasTex(1) color(4) lightDir(3) tint(3) flags(4) sky(4) applySky(1) hasBumpTex(1)
    // bumpLimb(4) = 42. The eclipse casters are *not* here -- they ride in a mat4 uniform
    // instead (see SphereImpostorParams::casters).
    static const int kFloatsPerVertex = 42;
    static GLint s_uDayMapLoc = -1, s_uNightMapLoc = -1, s_uBumpMapLoc = -1;

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
        s_aRadiiLoc    = glGetAttribLocation(s_program, "aRadii");
        s_aBasisXLoc   = glGetAttribLocation(s_program, "aBasisX");
        s_aBasisYLoc   = glGetAttribLocation(s_program, "aBasisY");
        s_aHasTexLoc   = glGetAttribLocation(s_program, "aHasTex");
        s_aColorLoc    = glGetAttribLocation(s_program, "aColor");
        s_aLightDirLoc = glGetAttribLocation(s_program, "aLightDir");
        s_aTintLoc     = glGetAttribLocation(s_program, "aTint");
        s_aFlagsLoc    = glGetAttribLocation(s_program, "aFlags");
        s_aSkyLoc      = glGetAttribLocation(s_program, "aSky");
        s_aApplySkyLoc = glGetAttribLocation(s_program, "aApplySky");
        s_aHasBumpTexLoc     = glGetAttribLocation(s_program, "aHasBumpTex");
        s_aBumpLimbLoc       = glGetAttribLocation(s_program, "aBumpLimb");
        s_uCastersLoc  = glGetUniformLocation(s_program, "uCasters");
        s_uRingLoc     = glGetUniformLocation(s_program, "uRing");
        s_uCasterAtmLoc = glGetUniformLocation(s_program, "uCasterAtm");
        s_uAtmLoc      = glGetUniformLocation(s_program, "uAtm");
        s_uSphRingXMapLoc = glGetUniformLocation(s_program, "uSphRingXMap");
        s_uDayMapLoc   = glGetUniformLocation(s_program, "uDayMap");
        s_uNightMapLoc = glGetUniformLocation(s_program, "uNightMap");
        s_uBumpMapLoc  = glGetUniformLocation(s_program, "uBumpMap");

        // Texture units 0/1/2/3, matching the convention ImGui's own backend uses for its
        // font/UI texture on unit 0 -- safe since our AddCallback runs between ImGui draw
        // commands, and the paired ImDrawCallback_ResetRenderState immediately after
        // re-establishes ImGui's own state (including its own texture bindings) before anything
        // else draws.
        glUseProgram(s_program);
        glUniform1i(s_uDayMapLoc, 0);
        glUniform1i(s_uNightMapLoc, 1);
        glUniform1i(s_uBumpMapLoc, 2);
        glUniform1i(s_uSphRingXMapLoc, 3);

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
        glEnableVertexAttribArray(s_aRadiiLoc);
        glVertexAttribPointer(s_aRadiiLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 8));
        glEnableVertexAttribArray(s_aBasisXLoc);
        glVertexAttribPointer(s_aBasisXLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 11));
        glEnableVertexAttribArray(s_aBasisYLoc);
        glVertexAttribPointer(s_aBasisYLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 14));
        glEnableVertexAttribArray(s_aHasTexLoc);
        glVertexAttribPointer(s_aHasTexLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 17));
        glEnableVertexAttribArray(s_aColorLoc);
        glVertexAttribPointer(s_aColorLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 18));
        glEnableVertexAttribArray(s_aLightDirLoc);
        glVertexAttribPointer(s_aLightDirLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 22));
        glEnableVertexAttribArray(s_aTintLoc);
        glVertexAttribPointer(s_aTintLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 25));
        glEnableVertexAttribArray(s_aFlagsLoc);
        glVertexAttribPointer(s_aFlagsLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 28));
        glEnableVertexAttribArray(s_aSkyLoc);
        glVertexAttribPointer(s_aSkyLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 32));
        glEnableVertexAttribArray(s_aApplySkyLoc);
        glVertexAttribPointer(s_aApplySkyLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 36));
        glEnableVertexAttribArray(s_aHasBumpTexLoc);
        glVertexAttribPointer(s_aHasBumpTexLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 37));
        glEnableVertexAttribArray(s_aBumpLimbLoc);
        glVertexAttribPointer(s_aBumpLimbLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 38));

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
            v[8] = p->radx;
            v[9] = p->rady;
            v[10] = p->radz;
            v[11] = p->bxx;
            v[12] = p->bxy;
            v[13] = p->bxz;
            v[14] = p->byx;
            v[15] = p->byy;
            v[16] = p->byz;
            v[17] = p->has_tex;
            v[18] = p->r;
            v[19] = p->g;
            v[20] = p->b;
            v[21] = p->a;
            v[22] = p->lightx;
            v[23] = p->lighty;
            v[24] = p->lightz;
            v[25] = p->tintr;
            v[26] = p->tintg;
            v[27] = p->tintb;
            v[28] = p->flagx;
            v[29] = p->flagy;
            v[30] = p->flagz;
            v[31] = p->flagw;
            v[32] = p->skyr;
            v[33] = p->skyg;
            v[34] = p->skyb;
            v[35] = p->sky_y;
            v[36] = p->apply_sky;
            v[37] = p->has_bump_tex;
            v[38] = p->bump_strength;
            v[39] = p->limba;
            v[40] = p->limbb;
            v[41] = p->light_ang;
        }

        glUseProgram(s_program);
        // Uniform, so unlike everything above it persists in the program between draws -- it has
        // to be re-set on *every* draw, including the overwhelmingly common no-eclipse one, or a
        // body drawn after an eclipsed one would inherit the previous body's casters.
        // p->light_ang being 0 already stops the shader reading it in that case, but this keeps
        // the program's own state honest rather than relying on that one guard alone.
        glUniformMatrix4fv(s_uCastersLoc, 1, GL_FALSE, p->casters);
        glUniformMatrix4fv(s_uRingLoc, 1, GL_FALSE, p->ring);   // same "re-set every draw" reason
        glUniformMatrix4fv(s_uCasterAtmLoc, 1, GL_FALSE, p->caster_atm);
        glUniformMatrix4fv(s_uAtmLoc, 1, GL_FALSE, p->atm);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p->tex);
        glActiveTexture(GL_TEXTURE0 + 1);   // GL_TEXTURE1/2/3 aren't in this stripped loader's
                                             // symbol set; texture unit enums are guaranteed
                                             // sequential.
        glBindTexture(GL_TEXTURE_2D, p->night_tex);
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, p->bump_tex);
        glActiveTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, p->ringx_tex);
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

        // Not deleted: p belongs to the frame pool (see impostor_begin_frame), which is
        // reset once per frame rather than freed per callback.
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
        // r >= L here means the camera sits inside this axis-pair's shadow of the sphere --
        // not an error (the camera can be outside the sphere in 3D while this 2D slice still
        // engulfs it, e.g. looking somewhat sideways from low orbit, or for a ring, simply
        // being closer to the planet than the ring's own outer radius, which is routine).
        // There's no external tangent line from inside a circle, so the silhouette is
        // genuinely unbounded on this screen axis -- same case as the L<1e-6 branch above,
        // handled the same way. An earlier version instead let alpha clamp to 90 degrees and
        // fell through to the tw<=0 branch below to pick a wide bound's sign from tu -- usually
        // fine, but tu independently approaches 0 in exactly this regime too (both tu and tw
        // are ~0 when the camera sits almost exactly along this slice's u axis, e.g. flying
        // near a ring's own plane), making the sign it picked essentially frame-to-frame noise.
        // Bug: a hard, creeping cutoff edge partway across the screen (the wide bound
        // intermittently flipping to the wrong side instead of just being wide), and the
        // planet's onscreen flag flickering with it -- rings alternating against the wireframe
        // fallback frame to frame. Bailing out directly here avoids the unstable branch
        // entirely rather than trying to patch its sign heuristic.
        if (r >= L)
        {
            *out_min = -kFiniteBound * zoom;
            *out_max = kFiniteBound * zoom;
            return;
        }
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
        if (in.axis_x <= 0 || in.axis_y <= 0 || in.axis_z <= 0) return false;   // shader divides by each
        if (cx*cx + cy*cy + cz*cz <= r*r) return false;   // camera genuinely inside the sphere

        // The quad has to cover the glowing air outside the body as well as the body itself, or
        // the atmosphere branch in the fragment shader would be clipped off at exactly the
        // silhouette it is supposed to reach past -- a hard-edged ring instead of a soft one.
        // Only the *bounds* grow: the camera-inside test just above, the silhouette, and the
        // returned bounding box all still describe the solid body.
        double mean_r = (in.axis_x + in.axis_y + in.axis_z) / 3.0;
        double atm_h = (in.atmosphere_height > 0 && mean_r > 0) ? in.atmosphere_height : 0.0;
        double quad_r = r + atm_h;

        double zdesXmin, zdesXmax, zdesYmin, zdesYmax;
        tangent_bounds(cx, cz, quad_r, zoom, false, &zdesXmin, &zdesXmax);
        tangent_bounds(cy, cz, quad_r, zoom, true,  &zdesYmin, &zdesYmax);   // Cartesian2D negates Y

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

        if (s_sphere_used == s_sphere_pool.size()) s_sphere_pool.emplace_back();
        SphereImpostorParams *p = &s_sphere_pool[s_sphere_used++];
        *p = SphereImpostorParams();        // as value-initialized as the `new` it replaces
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
        p->radx = (float)(in.axis_x / d);
        p->rady = (float)(in.axis_y / d);
        p->radz = (float)(in.axis_z / d);

        p->bxx = (float)in.basisX[0]; p->bxy = (float)in.basisX[1]; p->bxz = (float)in.basisX[2];
        p->byx = (float)in.basisY[0]; p->byy = (float)in.basisY[1]; p->byz = (float)in.basisY[2];
        p->has_tex = in.day_map_texture ? 1.0f : 0.0f;
        p->tex = (GLuint)in.day_map_texture;
        p->night_tex = (GLuint)in.night_map_texture;
        p->has_bump_tex = in.bump_map_texture ? 1.0f : 0.0f;
        p->bump_tex = (GLuint)in.bump_map_texture;
        p->bump_strength = (float)in.bump_strength;
        p->limba = (float)in.limb_a;
        p->limbb = (float)in.limb_b;

        // Eclipse casters, scaled by the same 1/d as the center and radii above so the shader
        // can compare them against a surface point directly. Columns past num_casters stay zero
        // (SphereImpostorParams is value-initialized), which is how the shader knows to skip
        // them. light_ang is left at 0 when there is nothing to cast a shadow, which is what
        // makes the per-pixel eclipse test cost nothing at all on the ordinary draw.
        p->light_ang = 0.0f;
        int ncast = std::max(0, std::min(in.num_casters, max_eclipse_casters));
        for (int i = 0; i < ncast; i++)
        {
            if (in.casters[i].r <= 0) continue;
            p->casters[i*4 + 0] = (float)(in.casters[i].dx / d);
            p->casters[i*4 + 1] = (float)(in.casters[i].dy / d);
            p->casters[i*4 + 2] = (float)(in.casters[i].dz / d);
            p->casters[i*4 + 3] = (float)(in.casters[i].r / d);
            p->light_ang = (float)in.light_angular_radius;
        }

        // The planet's own rings shadowing it -- same 1/d scaling, same "0 means absent"
        // convention. Left entirely zero for the ringless bodies that are nearly all of them,
        // which is what the shader's uRing[1].x test reads.
        if (in.ring_outer_r > in.ring_inner_r && in.ring_inner_r > 0)
        {
            p->ring[0] = (float)in.ring_normal[0];
            p->ring[1] = (float)in.ring_normal[1];
            p->ring[2] = (float)in.ring_normal[2];
            p->ring[3] = (float)(in.ring_inner_r / d);
            p->ring[4] = (float)(in.ring_outer_r / d);
            p->ring[5] = in.ringx_map_texture ? 1.0f : 0.0f;
            p->ringx_tex = (GLuint)in.ringx_map_texture;
        }

        // Per-caster umbra light, column-for-column with the casters above -- a column left at
        // zero (an airless caster, or an unused slot) is a black shadow.
        for (int i = 0; i < ncast; i++)
        {
            if (in.casters[i].r <= 0 || in.casters[i].umbra_light <= 0) continue;
            p->caster_atm[i*4 + 0] = (float)in.casters[i].umbra_tint[0];
            p->caster_atm[i*4 + 1] = (float)in.casters[i].umbra_tint[1];
            p->caster_atm[i*4 + 2] = (float)in.casters[i].umbra_tint[2];
            p->caster_atm[i*4 + 3] = (float)in.casters[i].umbra_light;
        }

        // This body's own air. The shell's thickness is passed as a fraction of the body's own
        // mean radius, since that is the space the fragment shader works in -- the solid body is
        // the unit sphere there, so the atmosphere is simply the sphere of radius 1 + this.
        if (atm_h > 0)
        {
            p->atm[0] = (float)in.atmosphere_color[0];
            p->atm[1] = (float)in.atmosphere_color[1];
            p->atm[2] = (float)in.atmosphere_color[2];
            p->atm[3] = (float)(atm_h / mean_r);
            p->atm[4] = (float)in.atmosphere_low_color[0];
            p->atm[5] = (float)in.atmosphere_low_color[1];
            p->atm[6] = (float)in.atmosphere_low_color[2];
        }

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
    // didn't have to deal with:
    //
    // 1. Occlusion by the planet's own opaque disc. There is no depth buffer anywhere in this
    //    app (confirmed against alienorum.cpp -- only glClear(GL_COLOR_BUFFER_BIT) runs); all
    //    compositing is draw order. The CPU ring code resolves this by simply never emitting
    //    geometry for a ring point it determines is hidden behind the sphere (a distance-from-
    //    center-to-camera-ray test), relying on draw order for everything else -- the visible
    //    near-side arc, drawn after the disc, naturally paints over it, and the far-side arc
    //    that doesn't overlap the disc's screen silhouette never has to. The ring impostor
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

    // See s_sphere_pool: same arrangement, same reason.
    static std::deque<RingImpostorParams> s_ring_pool;
    static size_t s_ring_used = 0;

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
        "    //\n"
        "    // Skipped whenever the camera itself is essentially sitting on that sphere\n"
        "    // (vRhoInner near 1 -- the camera-to-center distance is normalized to 1 by\n"
        "    // construction, so vRhoInner==1 means the sphere's own radius equals that\n"
        "    // distance) -- exactly horizon mode, standing on the ringed body's own\n"
        "    // surface. With the ray origin pinned to the occluder's surface, tSphereNear's\n"
        "    // sign near the local horizon is decided by sub-ULP rounding in sin/cos rather\n"
        "    // than real geometry, flipping at random from one frame (or pixel) to the next.\n"
        "    // Bug: the whole visible ring winking between 'shown' and 'self-occluded' --\n"
        "    // large contiguous regions, not fine texture noise, since the flip is on a\n"
        "    // single shared boundary rather than per-pixel sampling. The test is also\n"
        "    // physically moot here: standing on the surface, nothing above the local\n"
        "    // horizon can be behind this same body, and anything below it is already\n"
        "    // hidden by the ground polygon drawn over this impostor.\n"
        "    float tca = dot(vCenter, dirN);\n"
        "    vec3 perp = vCenter - dirN * tca;\n"
        "    float d2 = dot(perp, perp);\n"
        "    if (vRhoInner < 0.999 && d2 < vRhoInner*vRhoInner)\n"
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

    // Double-precision variant of kRingFragmentShaderSrc above: identical in every respect
    // except that the ray/ring-plane intersection math (the part that divides by a value which
    // is routinely near-zero) runs in double instead of float. See ensure_ring_gl_objects()'s
    // comment for why that matters and why this is attempted first with a fallback, not a
    // replacement. #version bumped to 150 to match GL_ARB_gpu_shader_fp64's own dependency
    // (GLSL 1.50/GL 3.2); the vertex shader stays at 130 and linking the two is fine -- neither
    // stage uses anything version-specific to its own number.
    static const char *kRingFragmentShaderSrcDouble =
        "#version 150\n"
        "#extension GL_ARB_gpu_shader_fp64 : require\n"
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
        "const float GOSSAMER = 0.08;\n"
        "void main()\n"
        "{\n"
        "    dvec3 dirN = normalize(dvec3(double(vRayXY.x), double(-vRayXY.y), 1.0lf));\n"
        "    dvec3 nrm = dvec3(vNormal);\n"
        "    dvec3 cen = dvec3(vCenter);\n"
        "    double rhoInner = double(vRhoInner);\n"
        "    double rhoOuter = double(vRhoOuter);\n"
        "\n"
        "    double denom = dot(nrm, dirN);\n"
        "    if (abs(denom) < 1e-12lf) discard;\n"     // ray parallel to the ring plane
        "    double s = dot(nrm, cen) / denom;\n"
        "    if (s <= 0.0lf) discard;\n"              // ring-plane hit is behind the camera
        "\n"
        "    dvec3 p = dirN * s;\n"
        "    dvec3 rel = p - cen;\n"
        "    double ringDist = length(rel);\n"
        "    if (ringDist < rhoInner || ringDist > rhoOuter) discard;\n"
        "\n"
        "    // See the float shader's identical comment: skipped near vRhoInner==1 (camera\n"
        "    // essentially on the occluder's own surface -- horizon mode) where this test is\n"
        "    // both physically moot and numerically degenerate. Double precision narrows the\n"
        "    // angular range where sin/cos rounding can flip tSphereNear's sign, but doesn't\n"
        "    // remove it -- the ray origin is still pinned to the sphere's surface either way.\n"
        "    double tca = dot(cen, dirN);\n"
        "    dvec3 perp = cen - dirN * tca;\n"
        "    double d2 = dot(perp, perp);\n"
        "    if (rhoInner < 0.999lf && d2 < rhoInner*rhoInner)\n"
        "    {\n"
        "        double thc = sqrt(rhoInner*rhoInner - d2);\n"
        "        double tSphereNear = tca - thc;\n"
        "        if (tSphereNear > 0.0lf && tSphereNear < s) discard;\n"
        "    }\n"
        "\n"
        "    float u = float(clamp((ringDist - rhoInner) / (rhoOuter - rhoInner), 0.0lf, 1.0lf));\n"
        "\n"
        "    vec3 ringColor = (vHasRingTex > 0.5) ? texture(uRingMap, vec2(u, 0.5)).rgb : vColor.rgb;\n"
        "    float opacity = (vHasRingXTex > 0.5)\n"
        "        ? (1.0 - pow(texture(uRingXMap, vec2(u, 0.5)).g, GOSSAMER))\n"
        "        : 0.5;\n"
        "    opacity = pow(opacity, 0.4);\n"
        "\n"
        "    float isDay;\n"
        "    if (vSelfLuminous > 0.5) isDay = 1.0;\n"
        "    else\n"
        "    {\n"
        "        dvec3 toCenter = cen - p;\n"
        "        dvec3 lightDirD = dvec3(vLightDir);\n"
        "        double tl = dot(toCenter, lightDirD);\n"
        "        dvec3 perp2 = toCenter - lightDirD * tl;\n"
        "        double d2shadow = dot(perp2, perp2);\n"
        "        isDay = (tl > 0.0lf && d2shadow < rhoInner*rhoInner) ? 0.0 : (0.4 + 0.6*vAmtLit);\n"
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

    static bool try_build_ring_program(const char *fs_src)
    {
        GLuint vs = compile_shader(GL_VERTEX_SHADER, kRingVertexShaderSrc);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);

        GLint vs_ok = 0, fs_ok = 0;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &vs_ok);
        glGetShaderiv(fs, GL_COMPILE_STATUS, &fs_ok);
        if (!vs_ok || !fs_ok)
        {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!ok)
        {
            glDeleteProgram(prog);
            return false;
        }

        s_ring_program = prog;
        return true;
    }

    static void ensure_ring_gl_objects()
    {
        static bool s_ring_gl_failed = false;
        if (s_ring_program || s_ring_gl_failed) return;

        // Double precision buys a lot here: a view ray nearly parallel to the ring plane -- the
        // routine case in horizon mode, standing on/near the ring plane and looking along it --
        // makes dot(normal, ray) a near-cancellation in the plane-intersection math below, and
        // float32's ~7 decimal digits of precision run out well before the old discard test
        // (abs(denom) < 1e-9) could reject it, so grazing pixels flip between "ring" and
        // "not ring" from one frame -- even one pixel -- to the next as rounding noise
        // dominates. Bug: the ring's edge shimmering/jumping in horizon mode, distinct from the
        // (also real, separately fixed) texture minification moire from gputex.cpp's missing
        // mipmaps. Falls back to the original float shader on hardware without
        // GL_ARB_gpu_shader_fp64 -- older Intel integrated GPUs mainly -- so unsupported
        // hardware sees exactly today's behavior rather than losing rings outright.
        if (!try_build_ring_program(kRingFragmentShaderSrcDouble)
            && !try_build_ring_program(kRingFragmentShaderSrc))
        {
            std::cerr << "Ring impostor: shader build failed (both double and float variants)." << std::endl;
            s_ring_gl_failed = true;
            return;
        }

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
    // rather than touching GL_BLEND itself -- the sphere impostor never had to care since
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

        // Not deleted: p belongs to the frame pool (see impostor_begin_frame), which is
        // reset once per frame rather than freed per callback.
    }

    bool queue_ring_impostor(const RingImpostorInput &in, double zoom, double dispcx, double dispcy)
    {
        double cx = in.cx, cy = in.cy, cz = in.cz, r = in.outer_r;
        if (r <= 0 || zoom <= 0 || in.inner_r <= 0 || in.inner_r >= in.outer_r) return false;
        // NOT "camera inside outer_r" -- that was copied from the sphere impostor's genuine
        // camera-inside-the-solid-sphere bail-out, but a ring is a flat zero-thickness annulus,
        // not a solid volume the camera can be "inside" in any meaningful sense. ring_radius is
        // typically ~2-2.5x the planet's own radius, so that guard fired on any moderately close
        // flyby -- bug: rings vanishing entirely when approaching the planet, well before actually
        // getting near the ring plane. tangent_bounds() already handles "camera within r for this
        // 2D slice" gracefully (see its own comment on the r/L>1 case) -- the only real degenerate
        // case here is the camera sitting essentially exactly at the ring/planet center, which
        // would divide by ~0 a few lines down.
        double d0 = sqrt(cx*cx + cy*cy + cz*cz);
        if (d0 < 1e-6) return false;

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

        if (s_ring_used == s_ring_pool.size()) s_ring_pool.emplace_back();
        RingImpostorParams *p = &s_ring_pool[s_ring_used++];
        *p = RingImpostorParams();
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
        double d = d0;
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

    void impostor_begin_frame()
    {
        // Only the high-water marks are reset; the storage stays for the next frame to reuse.
        s_sphere_used = 0;
        s_ring_used = 0;
    }
}
