#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>
#include "SDL.h"
#include "clouds_gpu.h"
#include "imgui.h"
#include "imgui/backends/imgui_impl_opengl3_loader.h"

namespace alienorum
{
    static const char *kCloudVertexShaderSrc = R"GLSL(
#version 130
in vec2 aPos;
in vec2 aXY;
out vec2 vXY;
void main()
{
    vXY = aXY;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";
    static const char *kCloudFragmentShaderSrc = R"GLSL(
#version 130
in vec2 vXY;
out vec4 FragColor;
uniform mat4 uCamera;
uniform mat4 uWorld;
uniform mat4 uLight;
uniform sampler2D uCloudMap;
uniform sampler2D uRefraction;
const float PI = 3.141592653589793;
const float HALF_PI = 1.570796326794897;
const float TWO_PI = 6.283185307179586;
float hash(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
float noise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x),
        mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
        mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z) * 2.0 - 1.0;
}
float perlin_gradient(vec3 cell, vec3 offset)
{
    int h = int(floor(hash(cell) * 16.0));
    float u = h < 8 ? offset.x : offset.y;
    float v = h < 4 ? offset.y : ((h == 12 || h == 14) ? offset.x : offset.z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}
float wisp_perlin(vec3 p)
{
    vec3 cell = floor(p);
    vec3 f = fract(p);
    vec3 fade = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(
        mix(mix(perlin_gradient(cell, f),
                perlin_gradient(cell + vec3(1, 0, 0), f - vec3(1, 0, 0)), fade.x),
            mix(perlin_gradient(cell + vec3(0, 1, 0), f - vec3(0, 1, 0)),
                perlin_gradient(cell + vec3(1, 1, 0), f - vec3(1, 1, 0)), fade.x), fade.y),
        mix(mix(perlin_gradient(cell + vec3(0, 0, 1), f - vec3(0, 0, 1)),
                perlin_gradient(cell + vec3(1, 0, 1), f - vec3(1, 0, 1)), fade.x),
            mix(perlin_gradient(cell + vec3(0, 1, 1), f - vec3(0, 1, 1)),
                perlin_gradient(cell + vec3(1, 1, 1), f - vec3(1, 1, 1)), fade.x), fade.y), fade.z);
}
void main()
{
    // All lengths use cloud-scale units, avoiding planet-sized quadratics.
    float az = uCamera[0].x;
    float alt = uCamera[0].y;
    vec2 xy = vXY * uCamera[0].zw;
    float dev_dial = uLight[1].x;                       // Leave this here in case we find more constants to adjust later.
    float forward = cos(alt) - xy.y * sin(alt);
    vec3 ray = normalize(vec3(xy.x * cos(az) + forward * sin(az),
        xy.y * cos(alt) + sin(alt), forward * cos(az) - xy.x * sin(az)));
    float h = atan(ray.y, length(ray.xz));
    // The inverse curve is cached as a linear texture: one fetch per pixel.
    float true_h = texture(uRefraction, vec2((h + HALF_PI) / PI * (1024.0 / 1025.0)
        + 0.5 / 1025.0, 0.5)).r;
    float horizontal = length(ray.xz);
    ray.xz *= cos(true_h) / max(horizontal, 1e-8);
    ray.y = sin(true_h);
    float observer = uWorld[0].x;
    float shell = uWorld[0].y;
    float delta = uWorld[0].z;
    if (delta > 0.0 && ray.y < uWorld[0].w)
    {
        discard;
    }
    float along = observer * ray.y;
    float discriminant = along * along + delta;
    if (discriminant < 0.0 || (delta < 0.0 && along >= 0.0))
    {
        discard;
    }
    float root = sqrt(discriminant);
    float distance;
    if (delta >= 0.0)
    {
        distance = along > 0.0 ? delta / (root + along) : root - along;
    }
    else
    {
        distance = -delta / (root - along);
    }
    if (distance <= 0.0)
    {
        discard;
    }
    vec3 east = uCamera[1].xyz;
    vec3 north = uCamera[2].xyz;
    vec3 up = uCamera[3].xyz;
    vec3 direction = east * ray.x + north * ray.z + up * ray.y;
    vec3 hit = up * observer + distance * direction;
    vec3 normal = normalize(hit);
    float lat = atan(hit.z, length(hit.xy));
    float lon = atan(hit.y, hit.x);
    vec2 uv = vec2(lon / TWO_PI + 0.5, 0.5 - lat / PI);
    vec2 gx = dFdx(uv);
    vec2 gy = dFdy(uv);
    gx.x -= round(gx.x);
    gy.x -= round(gy.x);
    vec3 rgb = textureGrad(uCloudMap, uv + uLight[2].xy, gx, gy).rgb;
    float coverage = clamp(dot(rgb, vec3(0.29, 0.53, 0.18)), 0.0, 1.0);
    float deck = uLight[0].z > 0.5 ? 1.0 : smoothstep(0.70, 0.90, coverage);
    // Derivatives are measured in output pixels, independent of cache resolution.
    float footprint = max(length(dFdx(hit)) * uWorld[1].x, length(dFdy(hit)) * uWorld[1].y);
    float unresolved = smoothstep(0.025, 0.16, footprint);
    float blend = deck + (1.0 - deck) * unresolved;
    float alpha = mix(coverage, 1.0, deck);
    float shade = 1.0;
    float warm_weight = 1.0;
    // Even a fully blended cloud-map pixel may have a nearer cumulus lobe.
    // Overcast is deliberately exempt: it is a continuous opaque deck.
    if (blend < 0.999 || uLight[0].z < 0.5)
    {
        vec3 tangent_east = vec3(-sin(lon), cos(lon), 0.0);
        vec3 tangent_north = cross(normal, tangent_east);
        vec3 r = vec3(dot(direction, tangent_east), dot(direction, tangent_north), dot(direction, normal));
        float step = 2.8 / shell;
        // Search the entire cloud volume along the ray. A neighborhood at the
        // shell crossing alone cuts off lobes that extend across cell borders.
        float outer = shell + 1.35;
        float inner = shell - 1.35;
        float outer_root = sqrt(max(0.0, along * along + (outer - observer) * (outer + observer)));
        float segment_start = max(0.0, -along - outer_root);
        float segment_end = -along + outer_root;
        float inner_disc = along * along + (inner - observer) * (inner + observer);
        if (inner_disc > 0.0)
        {
            float inner_root = sqrt(inner_disc);
            if (observer < inner)
            {
                segment_start = -along + inner_root;
            }
            else if (-along - inner_root > segment_start)
            {
                segment_end = -along - inner_root;
            }
        }

        // Limit procedural cumulus to nearby clouds.
        float max_cumulus_distance = 10;
        segment_end = min(segment_end, max_cumulus_distance);

        float solid = 0.0;
        float nearest = 1e30;
        float underside = 0.0;
        int probes = segment_end > segment_start
            ? max(1, int(ceil((segment_end - segment_start) / 1.4)))
            : -1;
        for (int probe = 0; probe <= probes; ++probe)
        {
            float travel = mix(segment_start, segment_end, float(probe) / float(probes));
            vec3 search = up * observer + travel * direction;
            float search_lat = atan(search.z, length(search.xy));
            float search_lon = atan(search.y, search.x);
            int band = int(floor((search_lat + HALF_PI) / step));
            for (int row = -1; row <= 1; ++row)
            {
                float clat = -HALF_PI + (float(band + row) + 0.5) * step;
                if (abs(clat) >= HALF_PI)
                {
                    continue;
                }
                float cells = max(1.0, round(TWO_PI * shell * cos(clat) / 2.8));
                float lon_step = TWO_PI / cells;
                float column = floor((search_lon + PI) / lon_step);
                for (int col = -1; col <= 1; ++col)
                {
                    float clon = -PI + (mod(column + float(col), cells) + 0.5) * lon_step;
                    vec3 center = shell * vec3(cos(clat) * cos(clon), cos(clat) * sin(clon), sin(clat));
                    // Reject the whole cluster before noise and six lobe tests.
                    vec3 offset = center - hit;
                    float projection = dot(offset, direction);
                    float miss = max(0.0, dot(offset, offset) - projection * projection);
                    if (miss > 1.35 * 1.35)
                    {
                        continue;
                    }
                    // Cluster membership is fixed at its center. Testing the ray's
                    // map coverage here sliced silhouettes along map boundaries.
                    vec2 cluster_uv = vec2(clon / TWO_PI + 0.5, 0.5 - clat / PI);
                    float cluster_coverage = dot(textureLod(uCloudMap, cluster_uv + uLight[2].xy, 0.0).rgb,
                        vec3(0.29, 0.53, 0.18));
                    if (cluster_coverage <= 0.0)
                    {
                        continue;
                    }
                    float mass = clamp(0.5 + noise(center * 0.37), 0.0, 1.0);
                    if (mass > min(1.0, cluster_coverage * 1.8))
                    {
                        continue;
                    }
                    float size = 0.65 + 0.45 * mass;
                    vec3 base = vec3(dot(offset, tangent_east), dot(offset, tangent_north), dot(offset, normal));
                    for (int puff = 0; puff < 6; ++puff)
                    {
                        float angle = float(puff) * 2.39996323 + mass * 6.0;
                        float radius = puff == 0 ? 0.0 : 0.43 * size;
                        vec3 p = base + vec3(radius * cos(angle), radius * sin(angle),
                            puff == 0 ? -0.18 : 0.05 + 0.16 * sin(angle * 1.7));
                        float hh = (puff == 0 ? 0.85 : 0.46) * size;
                        float vv = (puff == 0 ? 0.16 : 0.38) * size;
                        vec3 axes = vec3(hh, hh, vv);
                        vec3 rr = r / axes;
                        vec3 pp = p / axes;
                        float aa = dot(rr, rr);
                        float bb = dot(pp, rr);
                        float cc = dot(pp, pp) - 1.0;
                        float disc = bb * bb - aa * cc;
                        float edge = max(1e-5, footprint * 1.5);
                        if (disc >= 0.0)
                        {
                            float entry = (bb - sqrt(disc)) / aa;
                            if (entry > -distance)
                            {
                                solid = max(solid, smoothstep(0.0, edge, disc / aa));
                                if (entry < nearest)
                                {
                                    nearest = entry;
                                    vec3 surface = normalize((entry * r - p) / (axes * axes));
                                    shade = clamp(0.78 + 0.22 * surface.z, 0.56, 1.0);
                                    underside = smoothstep(0.0, 0.75, -surface.z);
                                }
                            }
                        }
                    }
                }
            }
        }
        // The cloud map describes the layer at the shell intersection (entry=0).
        // A lobe hit with negative entry is in front of that layer and must
        // not take its color or distant-cloud LOD blend. The shell layer
        // remains in front of lobes whose first hit has positive entry.
        bool foreground_cumulus = nearest < 0.0;
        if (foreground_cumulus)
        {
            deck = 0.0;
            blend = 0.0;
        }
        // Longitude follows the wind; latitude runs across it. Using a
        // circular longitude pair keeps the strands continuous at the map seam.
        // A tangent flow avoids the rounded patches produced when the old
        // anisotropic XYZ field pointed vertically at the observer.
        vec2 flow = shell * vec2(sin(lon), cos(lon));
        float bend = 0.35 * noise(hit * 0.65 + vec3(17.0));
        float crosswind = lat * shell + bend;
        // A planet-fixed gradient field at the original cloud-mass frequency.
        // Logarithmic scaling keeps the factor in [5, 50], favoring lower values.
        float wisp_field = clamp(0.5 + 0.5 * wisp_perlin(hit*0.1 + vec3(83.0, 127.0, 191.0)), 0.0, 1.0);
        float wispiness = 0.001 * pow(10.0, wisp_field*3);
        float strand = noise(vec3(flow * (0.26 * wispiness), crosswind * 18.0) + vec3(17.0));
        float fiber = noise(vec3(flow * (0.65 * wispiness), crosswind * 43.0 + strand * 0.65) + vec3(31.0));
        float veil = smoothstep(-0.18, 0.28, noise(hit * 0.15 + vec3(17.0)));
        // Narrow threads fade independently before they become subpixel.
        // Their broad envelopes taper into clear sky rather than solid lobes.
        float strands = smoothstep(0.02, 0.65, strand)
            * (1.0 - smoothstep(0.12, 0.80, footprint * 18.0));
        float fibers = smoothstep(0.10, 0.72, fiber)
            * (1.0 - smoothstep(0.12, 0.80, footprint * 43.0));
        float feather = strands * (0.48 + 0.52 * fibers) + 0.22 * fibers;
        float cirrus = 0.42 * veil * feather
            * smoothstep(0.0, 0.12, coverage) * (1.0 - smoothstep(0.55, 0.80, coverage));
        float cirrus_alpha = cirrus * (1.0 - solid);
        float combined_alpha = solid + cirrus_alpha;
        // Resolved opaque lobes retain their opacity during the distant veil blend.
        alpha = solid + (1.0 - solid) * mix(cirrus, coverage, blend);
        // Cirrus catches the warm light throughout; cumulus only on undersides.
        warm_weight = mix((solid * underside + cirrus_alpha) / max(1e-5, combined_alpha), 1.0, blend);
    }
    vec3 color = rgb * uLight[0].x * (160.0 / 255.0) * deck
        + vec3(uLight[0].y * shade * (1.0 - deck));
    float warmth = 0.85 * uLight[1].y * warm_weight;
    float glow_light = max(0.18, max(color.r, max(color.g, color.b)));
    color = mix(color, vec3(1.0, 0.45, 0.15) * glow_light, warmth);
    if (uLight[0].w > 0.5)
    {
        color = vec3(min(1.0, color.r + 0.5 * color.g + 0.3 * color.b), color.g / 3.0, color.b / 3.0);
    }
    // Premultiplied colors keep transparent borders clean during scaling.
    FragColor = vec4(color * alpha, alpha);
}
)GLSL";
    static const char *kCloudCompositeShaderSrc = R"GLSL(
#version 130
in vec2 vXY;
out vec4 FragColor;
uniform sampler2D uLayer;
void main()
{
    FragColor = texture(uLayer, vXY * 0.5 + 0.5);
}
)GLSL";
    struct CloudSkyGpuParams
    {
        float verts[16];
        float camera[16], world[16], light[16];
        GLuint texture;
        unsigned int revision;
    };
    static CloudSkyGpuParams s_pool[4];
    static int s_pool_index = 0;
    static GLuint s_program = 0, s_vao = 0, s_vbo = 0, s_ebo = 0, s_refraction = 0;
    static bool s_attempted = false;
    static float s_table[1025];
    static GLint s_aPosLoc = -1, s_aXYLoc = -1;
    static GLint s_uCameraLoc = -1, s_uWorldLoc = -1, s_uLightLoc = -1;
    static GLint s_uCloudMapLoc = -1, s_uRefractionLoc = -1;
    // The bundled loader has no framebuffer API; resolve these core GL 3.0
    // functions through SDL, matching the application's other extension calls.
    using GenFramebuffersFn = void (APIENTRY *)(GLsizei, GLuint*);
    using BindFramebufferFn = void (APIENTRY *)(GLenum, GLuint);
    using FramebufferTextureFn = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferFn = GLenum (APIENTRY *)(GLenum);
    static GenFramebuffersFn s_gen_framebuffers = nullptr;
    static BindFramebufferFn s_bind_framebuffer = nullptr;
    static FramebufferTextureFn s_framebuffer_texture = nullptr;
    static CheckFramebufferFn s_check_framebuffer = nullptr;
    static constexpr GLenum kFramebuffer = 0x8D40;
    static constexpr GLenum kFramebufferBinding = 0x8CA6;
    static constexpr GLenum kColorAttachment = 0x8CE0;
    static constexpr GLenum kFramebufferComplete = 0x8CD5;
    static GLuint s_composite = 0, s_cache_texture = 0, s_cache_framebuffer = 0;
    static GLint s_composite_pos = -1, s_composite_xy = -1;
    static int s_cache_width = 0, s_cache_height = 0;
    static int s_output_width = 0, s_output_height = 0;
    static bool s_cache_valid = false;
    static bool s_inverse_valid = false;
    static CloudSkyGpuParams s_cached_params = {};
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
            std::cerr << "Cloud sky shader compile error: " << log << std::endl;
        }
        if (!ok)
        {
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    }

    static void ensure_gl_objects()
    {
        if (s_attempted)
        {
            return;
        }

        s_attempted = true;
        GLuint vs = compile_shader(GL_VERTEX_SHADER, kCloudVertexShaderSrc);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kCloudFragmentShaderSrc);

        if (!vs || !fs)
        {
            if (vs)
            {
                glDeleteShader(vs);
            }
            if (fs)
            {
                glDeleteShader(fs);
            }
            return;
        }
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
            std::cerr << "Cloud sky shader link error: " << log << std::endl;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        if (!ok)
        {
            glDeleteProgram(s_program);
            s_program = 0;
            return;
        }
        s_aPosLoc = glGetAttribLocation(s_program, "aPos");
        s_aXYLoc  = glGetAttribLocation(s_program, "aXY");

        s_uCameraLoc   = glGetUniformLocation(s_program, "uCamera");
        s_uWorldLoc   = glGetUniformLocation(s_program, "uWorld");
        s_uLightLoc = glGetUniformLocation(s_program, "uLight");

        s_uCloudMapLoc   = glGetUniformLocation(s_program, "uCloudMap");
        s_uRefractionLoc = glGetUniformLocation(s_program, "uRefraction");

        glUseProgram(s_program);
        glUniform1i(s_uCloudMapLoc, 0);
        glUniform1i(s_uRefractionLoc, 1);

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

        // A separate cheap shader displays the layer between cache updates.
        GLuint composite_fs = compile_shader(GL_FRAGMENT_SHADER, kCloudCompositeShaderSrc);
        GLuint composite_vs = compile_shader(GL_VERTEX_SHADER, kCloudVertexShaderSrc);
        if (composite_fs && composite_vs)
        {
            s_composite = glCreateProgram();
            glAttachShader(s_composite, composite_vs);
            glAttachShader(s_composite, composite_fs);
            glLinkProgram(s_composite);
            glGetProgramiv(s_composite, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                glDeleteProgram(s_composite);
                s_composite = 0;
            }
            else
            {
                s_composite_pos = glGetAttribLocation(s_composite, "aPos");
                s_composite_xy = glGetAttribLocation(s_composite, "aXY");
                glUseProgram(s_composite);
                glUniform1i(glGetUniformLocation(s_composite, "uLayer"), 0);
            }
        }
        if (composite_fs)
        {
            glDeleteShader(composite_fs);
        }
        if (composite_vs)
        {
            glDeleteShader(composite_vs);
        }
        s_gen_framebuffers = (GenFramebuffersFn)SDL_GL_GetProcAddress("glGenFramebuffers");
        s_bind_framebuffer = (BindFramebufferFn)SDL_GL_GetProcAddress("glBindFramebuffer");
        s_framebuffer_texture = (FramebufferTextureFn)SDL_GL_GetProcAddress("glFramebufferTexture2D");
        s_check_framebuffer = (CheckFramebufferFn)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
        glBindVertexArray(0);
    }

    static void draw_quad(const CloudSkyGpuParams &p, GLint pos, GLint xy)
    {
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(p.verts), p.verts);
        glEnableVertexAttribArray(pos);
        glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
        glEnableVertexAttribArray(xy);
        glVertexAttribPointer(xy, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    static bool cache_matches(const CloudSkyGpuParams &p, int width, int height)
    {
        if (!s_cache_valid || width != s_output_width || height != s_output_height
            || p.texture != s_cached_params.texture || p.revision != s_cached_params.revision
            || std::memcmp(p.camera, s_cached_params.camera, sizeof(p.camera)) != 0
            || std::memcmp(p.world, s_cached_params.world, sizeof(p.world)) != 0)
        {
            return false;
        }
        for (int i = 0; i < 16; ++i)
        {
            // Sub-byte illumination changes can reuse the displayed layer.
            const float tolerance = (i < 2 || i == 5) ? 1.0f / 512.0f : 0.0f;
            if (std::fabs(p.light[i] - s_cached_params.light[i]) > tolerance)
            {
                return false;
            }
        }
        return true;
    }

    static bool prepare_cache(int width, int height)
    {
        if (!s_composite || !s_gen_framebuffers || !s_bind_framebuffer
            || !s_framebuffer_texture || !s_check_framebuffer)
        {
            return false;
        }
        // Half resolution removes three quarters of the expensive fragments.
        // Cap large displays so cost cannot grow without bound.
        const double scale = std::min(0.5, std::min(1024.0 / width, 576.0 / height));
        const int cw = std::max(1, (int)std::ceil(width * scale));
        const int ch = std::max(1, (int)std::ceil(height * scale));
        if (!s_cache_texture)
        {
            glGenTextures(1, &s_cache_texture);
            s_gen_framebuffers(1, &s_cache_framebuffer);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_cache_texture);
        if (cw != s_cache_width || ch != s_cache_height)
        {
            s_cache_valid = false;
            s_cache_width = cw;
            s_cache_height = ch;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        s_bind_framebuffer(kFramebuffer, s_cache_framebuffer);
        s_framebuffer_texture(kFramebuffer, kColorAttachment, GL_TEXTURE_2D, s_cache_texture, 0);
        return s_check_framebuffer(kFramebuffer) == kFramebufferComplete;
    }

    static void render_cloud_sky_gpu(const ImDrawList*, const ImDrawCmd *cmd)
    {
        const CloudSkyGpuParams &p = *(const CloudSkyGpuParams*)cmd->UserCallbackData;
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] <= 0 || viewport[3] <= 0)
        {
            return;
        }
        GLint framebuffer = 0;
        if (s_bind_framebuffer)
        {
            glGetIntegerv(kFramebufferBinding, &framebuffer);
        }
        glDisable(GL_SCISSOR_TEST);
        const bool cached = prepare_cache(viewport[2], viewport[3]);
        if (!cached || !cache_matches(p, viewport[2], viewport[3]))
        {
            float world[16];
            std::memcpy(world, p.world, sizeof(world));
            world[4] = cached ? (float)s_cache_width / viewport[2] : 1.0f;
            world[5] = cached ? (float)s_cache_height / viewport[3] : 1.0f;
            if (cached)
            {
                glViewport(0, 0, s_cache_width, s_cache_height);
                glDisable(GL_BLEND);
                // Discarded ground pixels must replace any previous cloud layer.
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            else
            {
                if (s_bind_framebuffer)
                {
                    s_bind_framebuffer(kFramebuffer, (GLuint)framebuffer);
                }
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
            glUseProgram(s_program);
            glUniformMatrix4fv(s_uCameraLoc, 1, GL_FALSE, p.camera);
            glUniformMatrix4fv(s_uWorldLoc, 1, GL_FALSE, world);
            glUniformMatrix4fv(s_uLightLoc, 1, GL_FALSE, p.light);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p.texture);
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, s_refraction);
            draw_quad(p, s_aPosLoc, s_aXYLoc);
            if (cached)
            {
                s_cached_params = p;
                s_output_width = viewport[2];
                s_output_height = viewport[3];
                s_cache_valid = true;
            }
        }
        if (cached)
        {
            s_bind_framebuffer(kFramebuffer, (GLuint)framebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(s_composite);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_cache_texture);
            draw_quad(p, s_composite_pos, s_composite_xy);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(0);
    }

    bool queue_cloud_sky_gpu(const CloudSkyGpuInput &in)
    {
        ensure_gl_objects();
        if (!s_program || !in.texture)
        {
            return false;
        }
        if (!s_inverse_valid || std::memcmp(s_table, in.refraction, sizeof(s_table)) != 0)
        {
            std::memcpy(s_table, in.refraction, sizeof(s_table));
            float inverse[1025];
            int index = 0;
            constexpr double pi = 3.141592653589793;
            for (int i = 0; i <= 1024; ++i)
            {
                const double altitude = -pi * 0.5 + i * pi / 1024.0;
                while (index < 1023 && in.refraction[index + 1] < altitude)
                {
                    ++index;
                }
                const double interval = in.refraction[index + 1] - in.refraction[index];
                const double fraction = std::clamp((altitude - in.refraction[index]) / std::max(1e-7, interval), 0.0, 1.0);
                inverse[i] = (float)(-pi * 0.5 + (index + fraction) * pi / 1024.0);
            }
            s_inverse_valid = true;
            s_cache_valid = false;
            if (!s_refraction)
            {
                glGenTextures(1, &s_refraction);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_refraction);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, 0x822E /* GL_R32F */, 1025, 1, 0, 0x1903 /* GL_RED */, GL_FLOAT, inverse);
        }
        CloudSkyGpuParams *p = &s_pool[s_pool_index];
        s_pool_index = (s_pool_index + 1) % 4;
        const float verts[16] =
        {
            -1, -1, -1, -1,
             1, -1,  1, -1,
             1,  1,  1,  1,
            -1,  1, -1,  1
        };
        std::memcpy(p->verts, verts, sizeof(verts));
        std::memcpy(p->camera, in.camera, sizeof(p->camera));
        std::memcpy(p->world, in.world, sizeof(p->world));
        std::memcpy(p->light, in.light, sizeof(p->light));
        p->texture = in.texture;
        p->revision = in.revision;
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_cloud_sky_gpu, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return true;
    }
}
