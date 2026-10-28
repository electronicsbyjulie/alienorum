#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>
#include <array>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
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
uniform sampler2D uGeometry;
uniform sampler2D uTiles;
uniform int uTileColumns;
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
// Large envelopes only cull rays. The visible cloud is overlapping billows,
// cached alongside those envelopes rather than generated for every sky pixel.
void cloud_parent(int puff, float mass, out vec3 p, out vec3 axes)
{
    float size = 0.38 + 0.72 * mass;
    float angle = float(puff) * 2.39996323 + mass * 6.0;
    float radius = puff == 0 ? 0.0 : 0.43 * size;
    p = vec3(radius * cos(angle), radius * sin(angle),
        (puff == 0 ? -0.08 : 0.16 + 0.13 * sin(angle * 1.7)) * size);
    float hh = (puff == 0 ? 0.65 : 0.43 + 0.05 * sin(angle * 2.3)) * size;
    float vv = (puff == 0 ? 0.38 : 0.38 + 0.06 * cos(angle * 1.9)) * size;
    axes = vec3(hh, hh, vv);
}
float cloud_base(float mass)
{
    float size = 0.38 + 0.72 * mass;
    return -size * mix(0.80, 0.20, smoothstep(0.35, 0.70, mass));
}
void cloud_billow(int puff, int child, float mass, vec3 parent, vec3 axes,
    out vec3 p, out vec3 child_axes)
{
    float height = 1.0 - 2.0 * (float(child) + 0.5) / 8.0;
    float angle = float(child) * 2.39996323 + float(puff) * 1.7 + mass * 6.0;
    float radius = sqrt(max(0.0, 1.0 - height * height));
    vec3 offset = 0.55 * vec3(radius * cos(angle), radius * sin(angle), height);
    float size = 0.52 + 0.035 * sin(angle * 2.7 + float(puff));
    p = parent + axes * offset;
    child_axes = axes * vec3(size, size, size + 0.02 * cos(angle));
    // Compress low billows into oblate lobes with slightly staggered bases.
    // Preserve their tops and horizontal overlap for a solid cloud mass.
    float floor_z = cloud_base(mass) - (0.025 + 0.025 * sin(angle * 1.3)) * (0.38 + 0.72 * mass);
    float top = p.z + child_axes.z;
    float bottom = max(p.z - child_axes.z, min(floor_z, p.z - child_axes.z * 0.30));
    p.z = (top + bottom) * 0.5;
    child_axes.z = (top - bottom) * 0.5;
}
vec4 cloud_detail(int puff, int child, int detail, float mass)
{
    float height = 1.0 - 2.0 * (float(detail) + 0.5) / 4.0;
    float angle = float(detail) * 2.39996323 + float(child) * 1.7 + float(puff) + mass * 6.0;
    float radius = sqrt(max(0.0, 1.0 - height * height));
    return vec4(0.40 * vec3(radius * cos(angle), radius * sin(angle), height),
        0.67 + 0.025 * sin(angle * 1.7));
}
bool cloud_bound(vec3 ray, vec3 p, vec3 axes, float nearest, float solid)
{
    // Every child fits inside this envelope: offset length + largest axis
    // ratio, including the smaller billows, is less than 1.20. Keep a margin
    // for floating-point arithmetic.
    vec3 rr = ray / (axes * 1.20);
    vec3 pp = p / (axes * 1.20);
    float aa = dot(rr, rr);
    float bb = dot(pp, rr);
    float disc = bb * bb - aa * (dot(pp, pp) - 1.0);
    if (disc < 0.0) return false;
    return solid < 0.999 || (bb - sqrt(disc)) / aa <= nearest + 0.75;
}
void cloud_surface(vec3 rr, vec3 pp, float aa, vec3 axes, float radius,
    float footprint, float distance,
    inout float solid, inout float nearest, inout vec3 surface_sum)
{
    // All four details share the child's ellipsoid basis and ray quadratic.
    // Only their normalized centers and radii change, saving divisions and
    // a second texture read for every small billow.
    float bb = dot(pp, rr);
    float disc = bb * bb - aa * (dot(pp, pp) - radius * radius);
    if (disc < 0.0) return;
    float root = sqrt(disc);
    float entry = (bb - root) / aa;
    if (entry <= -distance) return;
    float edge = max(0.0005, footprint * 1.5);
    solid = max(solid, smoothstep(0.0, edge, disc / (aa * radius * radius)));
    if (entry < nearest)
    {
        surface_sum *= exp2(max(-80.0, (entry - nearest) * 24.0));
        nearest = entry;
    }
    float weight = exp2(max(-80.0, (nearest - entry) * 24.0));
    vec3 surface = normalize((entry * rr - pp) / axes);
    surface_sum += weight * surface;
}
void cloud_finish(vec3 ray, vec3 base, float local_solid, float local_nearest,
    vec3 surface_sum, inout float solid, inout float nearest,
    inout float shade, inout float underside)
{
    solid = max(solid, local_solid);
    if (local_nearest < nearest)
    {
        nearest = local_nearest;
        vec3 surface = normalize(surface_sum);
        vec3 broad = normalize((nearest * ray - base) / vec3(0.81, 0.81, 0.36));
        float height = mix(broad.z, surface.z, 0.45);
        shade = clamp(0.92 + 0.33 * height, 0.60, 1.25);
        underside = smoothstep(0.0, 0.75, -broad.z);
    }
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
#ifdef CLOUD_RAIN_PROBE
    ray = vec3(0.0, 1.0, 0.0);
#endif
    float h = atan(ray.y, length(ray.xz));
    float apparent_y = ray.y;
    // The inverse curve is cached as a linear texture: one fetch per pixel.
    float true_h = texture(uRefraction, vec2((h + HALF_PI) / PI * (1024.0 / 1025.0)
        + 0.5 / 1025.0, 0.5)).r;
    float horizontal = length(ray.xz);
    ray.xz *= cos(true_h) / max(horizontal, 1e-8);
    ray.y = sin(true_h);
    float observer = uWorld[0].x;
    float shell = uWorld[0].y;
    float delta = uWorld[0].z;
    // Ground uses an apparent horizon, including its atmospheric bowl lift.
    // Refraction changes the cloud lookup ray, not this screen-space boundary.
    if (delta > 0.0 && apparent_y < uWorld[0].w)
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
    float puff_weight = 0.0;
    float warm_weight = 1.0;
    float rain_cumulus = 0.0;
    // Even a fully blended cloud-map pixel may have a nearer cumulus lobe.
    // Overcast is deliberately exempt: it is a continuous opaque deck.
    if (blend < 0.999 || uLight[0].z < 0.5)
    {
        vec3 tangent_east = vec3(-sin(lon), cos(lon), 0.0);
        vec3 tangent_north = cross(normal, tangent_east);
        vec3 r = vec3(dot(direction, tangent_east), dot(direction, tangent_north), dot(direction, normal));
        float step = 2.8 / shell;
        const float max_cumulus_distance = 10.0;
        float solid = 0.0;
        float nearest = 1e30;
        float underside = 0.0;
#ifdef CLOUD_REFERENCE
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

        // Centers decide eligibility. Search past that radius to include every
        // lobe of an eligible cluster, without slicing its outer silhouette.
        segment_end = min(segment_end, max_cumulus_distance + 1.35);
        int probes = segment_end > segment_start
            ? max(1, int(ceil((segment_end - segment_start) / 1.4)))
            : -1;
#endif
#ifndef CLOUD_REFERENCE
        // Every cluster occurs once per tile and is accepted as a whole.
        ivec2 tile = ivec2(gl_FragCoord.xy - uWorld[1].zw) / 32;
        int tile_row = tile.y * uTileColumns + tile.x;
        int count = int(texelFetch(uTiles, ivec2(0, tile_row), 0).r);
        for (int candidate = 1; candidate <= count; ++candidate)
        {
            int cluster = int(texelFetch(uTiles, ivec2(candidate, tile_row), 0).r);
            vec4 cached_center = texelFetch(uGeometry, ivec2(0, cluster), 0);
            if (cached_center.w < 0.5)
            {
                continue;
            }
            vec3 eye_offset = cached_center.xyz - up * observer;
            if (dot(eye_offset, eye_offset) > max_cumulus_distance * max_cumulus_distance)
            {
                continue;
            }
            vec3 offset = cached_center.xyz - hit;
            float projection = dot(offset, direction);
            float miss = max(0.0, dot(offset, offset) - projection * projection);
            if (miss > 1.35 * 1.35)
            {
                continue;
            }
            // An opaque foreground mass hides every lobe inside this sphere.
            if (solid >= 0.999 && projection - sqrt(1.35 * 1.35 - miss) > nearest) continue;
            vec3 base = vec3(dot(offset, tangent_east), dot(offset, tangent_north), dot(offset, normal));
            float local_solid = 0.0, local_nearest = 1e30;
            vec3 surface_sum = vec3(0.0);
            for (int puff = 0; puff < 6; ++puff)
            {
                vec4 shape = texelFetch(uGeometry, ivec2(2 + puff * 2, cluster), 0);
                float vv = texelFetch(uGeometry, ivec2(3 + puff * 2, cluster), 0).r;
                vec3 parent = base + shape.xyz;
                vec3 parent_axes = vec3(shape.w, shape.w, vv);
                if (!cloud_bound(r, parent, parent_axes, local_nearest, local_solid)) continue;
                for (int child = 0; child < 8; ++child)
                {
                    int field = 14 + (puff * 8 + child) * 2;
                    vec4 billow = texelFetch(uGeometry, ivec2(field, cluster), 0);
                    float height = texelFetch(uGeometry, ivec2(field + 1, cluster), 0).r;
                    vec3 child_center = base + billow.xyz;
                    vec3 child_axes = vec3(billow.w, billow.w, height);
                    if (!cloud_bound(r, child_center, child_axes, local_nearest, local_solid)) continue;
                    vec3 rr = r / child_axes;
                    vec3 pp = child_center / child_axes;
                    float aa = dot(rr, rr);
                    if (max(billow.w, height) <= footprint * 3.0)
                    {
                        cloud_surface(rr, pp, aa, child_axes, 1.0, footprint, distance,
                            local_solid, local_nearest, surface_sum);
                    }
                    else
                    {
                        for (int detail = 0; detail < 4; ++detail)
                        {
                            int detail_field = 110 + (puff * 8 + child) * 4 + detail;
                            vec4 small = texelFetch(uGeometry, ivec2(detail_field, cluster), 0);
                            cloud_surface(rr, pp + small.xyz, aa, child_axes, small.w,
                                footprint, distance, local_solid, local_nearest, surface_sum);
                        }
                    }
                }
            }
            cloud_finish(r, base, local_solid, local_nearest, surface_sum,
                solid, nearest, shade, underside);
        }
#else
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
                    vec3 eye_offset = center - up * observer;
                    if (dot(eye_offset, eye_offset) > max_cumulus_distance * max_cumulus_distance)
                    {
                        continue;
                    }
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
                    vec3 base = vec3(dot(offset, tangent_east), dot(offset, tangent_north), dot(offset, normal));
                    float local_solid = 0.0, local_nearest = 1e30;
                    vec3 surface_sum = vec3(0.0);
                    for (int puff = 0; puff < 6; ++puff)
                    {
                        vec3 parent, parent_axes;
                        cloud_parent(puff, mass, parent, parent_axes);
                        for (int child = 0; child < 8; ++child)
                        {
                            vec3 p, axes;
                            cloud_billow(puff, child, mass, parent, parent_axes, p, axes);
                            vec3 rr = r / axes;
                            vec3 pp = (base + p) / axes;
                            float aa = dot(rr, rr);
                            if (max(axes.x, axes.z) <= footprint * 3.0)
                            {
                                cloud_surface(rr, pp, aa, axes, 1.0, footprint, distance,
                                    local_solid, local_nearest, surface_sum);
                            }
                            else
                            {
                                for (int detail = 0; detail < 4; ++detail)
                                {
                                    vec4 small = cloud_detail(puff, child, detail, mass);
                                    cloud_surface(rr, pp + small.xyz, aa, axes, small.w,
                                        footprint, distance, local_solid, local_nearest, surface_sum);
                                }
                            }
                        }
                    }
                    cloud_finish(r, base, local_solid, local_nearest, surface_sum,
                        solid, nearest, shade, underside);
                }
            }
        }
#endif
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
        // The map is the unresolved bank behind the resolved lobes. Preserve
        // that background at soft lobe edges instead of exposing clear sky.
        // The shell is an approximation of the same cloud mass, so it must
        // not cut a lobe silhouette at its nominal intersection distance.
        float bank_alpha = mix(cirrus, alpha, blend);
        float background_alpha = (1.0 - solid) * bank_alpha;
        alpha = solid + background_alpha;
        rain_cumulus = solid;
        puff_weight = solid / max(1e-5, alpha);
        // Cirrus catches the warm light throughout; cumulus only on undersides.
        warm_weight = (0.55 * solid * underside + background_alpha) / max(1e-5, alpha);
    }
#ifdef CLOUD_RAIN_PROBE
    FragColor = vec4(rain_cumulus, deck, 0.0, 1.0);
    return;
#endif
    vec3 color = rgb * uLight[0].x * (160.0 / 255.0) * deck
        + vec3(uLight[0].y * (1.0 - deck));
    color = mix(color, vec3(uLight[0].y * shade), puff_weight);
    float warmth = 0.65 * clamp(uLight[1].y, 0.0, 1.0) * warm_weight;
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
        CloudGeometryMode geometry_mode;
    };
    static CloudSkyGpuParams s_pool[4];
    static int s_pool_index = 0;
    static GLuint s_program = 0, s_vao = 0, s_vbo = 0, s_ebo = 0, s_refraction = 0;
    static GLuint s_reference_program = 0;
    static GLuint s_rain_program = 0, s_rain_texture = 0, s_rain_framebuffer = 0;
    static CloudRainCover s_rain_cover;
    static bool s_rain_cached = false;
    static CloudSkyGpuParams s_rain_params;
    static bool s_attempted = false;
    static float s_table[1025];
    static GLint s_aPosLoc = -1, s_aXYLoc = -1;
    static GLint s_uCameraLoc = -1, s_uWorldLoc = -1, s_uLightLoc = -1;
    static GLint s_uCloudMapLoc = -1, s_uRefractionLoc = -1;
    static GLint s_uTileColumnsLoc = -1;
    struct CloudProgramLocations
    {
        GLint camera = -1, world = -1, light = -1;
        GLint cloud_map = -1, refraction = -1, position = -1, xy = -1;

        void load(GLuint program)
        {
            camera = glGetUniformLocation(program, "uCamera");
            world = glGetUniformLocation(program, "uWorld");
            light = glGetUniformLocation(program, "uLight");
            cloud_map = glGetUniformLocation(program, "uCloudMap");
            refraction = glGetUniformLocation(program, "uRefraction");
            position = glGetAttribLocation(program, "aPos");
            xy = glGetAttribLocation(program, "aXY");
        }
    };
    static CloudProgramLocations s_reference_locations, s_rain_locations;
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
    static CloudSkyGpuStats s_stats;
    static GLuint s_geometry_program = 0, s_geometry_texture = 0;
    static GLuint s_cell_texture = 0, s_tile_texture = 0, s_geometry_framebuffer = 0;
    static GLint s_geometry_pos = -1, s_geometry_xy = -1;
    static GLint s_geometry_world = -1, s_geometry_light = -1;
    static CloudSkyGpuParams s_geometry_params =
    {
    };
    static bool s_geometry_valid = false;
    static double s_refraction_span = 0;
    using Vec3 = std::array<double, 3>;
    struct CloudCell
    {
        std::array<float, 4> grid;
        Vec3 center;
    };
    static std::vector<CloudCell> s_cells;
    static Vec3 s_geometry_anchor;
    using GenQueriesFn = void (APIENTRY *)(GLsizei, GLuint*);
    using BeginQueryFn = void (APIENTRY *)(GLenum, GLuint);
    using EndQueryFn = void (APIENTRY *)(GLenum);
    using GetQueryivFn = void (APIENTRY *)(GLenum, GLenum, GLint*);
    using GetQueryObjectivFn = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using GetQueryObject64Fn = void (APIENTRY *)(GLuint, GLenum, std::uint64_t*);
    static GenQueriesFn s_gen_queries = nullptr;
    static BeginQueryFn s_begin_query = nullptr;
    static EndQueryFn s_end_query = nullptr;
    static GetQueryivFn s_get_query = nullptr;
    static GetQueryObjectivFn s_query_available = nullptr;
    static GetQueryObject64Fn s_query_result = nullptr;
    struct CloudTimer
    {
        GLuint id = 0;
        bool pending = false;
        int stage = 0;
        std::uint64_t sequence = 0;
    };
    static CloudTimer s_timers[12];
    static std::uint64_t s_timer_sequence = 0;
    static std::uint64_t s_completed_sequence[3] =
    {
    };

    const CloudSkyGpuStats &cloud_sky_gpu_stats()
    {
        return s_stats;
    }

    static CloudTimer *begin_cloud_timer(int stage)
    {
        if (!s_stats.timers_available)
        {
            return nullptr;
        }
        GLint active = 0;
        s_get_query(0x88BF /* GL_TIME_ELAPSED */, 0x8865 /* GL_CURRENT_QUERY */, &active);
        if (active != 0)
        {
            return nullptr;
        }
        CloudTimer *free_timer = nullptr;
        for (auto &timer : s_timers)
        {
            if (timer.pending)
            {
                GLint ready = 0;
                s_query_available(timer.id, 0x8867 /* GL_QUERY_RESULT_AVAILABLE */, &ready);
                if (ready)
                {
                    std::uint64_t ns = 0;
                    s_query_result(timer.id, 0x8866 /* GL_QUERY_RESULT */, &ns);
                    if (timer.sequence > s_completed_sequence[timer.stage])
                    {
                        double &value = timer.stage == 0 ? s_stats.geometry_ms
                            : (timer.stage == 1 ? s_stats.render_ms : s_stats.composite_ms);
                        value = ns * 1e-6;
                        s_completed_sequence[timer.stage] = timer.sequence;
                    }
                    timer.pending = false;
                }
            }
            if (!timer.pending && !free_timer)
            {
                free_timer = &timer;
            }
        }
        // Skip a sample if the GPU is still processing every query slot.
        if (free_timer)
        {
            if (!free_timer->id)
            {
                s_gen_queries(1, &free_timer->id);
            }
            free_timer->stage = stage;
            free_timer->sequence = ++s_timer_sequence;
            s_begin_query(0x88BF /* GL_TIME_ELAPSED */, free_timer->id);
        }
        return free_timer;
    }

    static void end_cloud_timer(CloudTimer *timer)
    {
        if (timer)
        {
            s_end_query(0x88BF /* GL_TIME_ELAPSED */);
            timer->pending = true;
        }
    }

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
        std::string reference_source(kCloudFragmentShaderSrc);
        reference_source.insert(reference_source.find('\n', 1) + 1, "#define CLOUD_REFERENCE 1\n");
        GLuint reference_fs = compile_shader(GL_FRAGMENT_SHADER, reference_source.c_str());
        if (reference_fs)
        {
            s_reference_program = glCreateProgram();
            glAttachShader(s_reference_program, vs);
            glAttachShader(s_reference_program, reference_fs);
            glLinkProgram(s_reference_program);
            GLint reference_ok = 0;
            glGetProgramiv(s_reference_program, GL_LINK_STATUS, &reference_ok);
            if (!reference_ok)
            {
                glDeleteProgram(s_reference_program);
                s_reference_program = 0;
            }
            else
            {
                s_reference_locations.load(s_reference_program);
                glUseProgram(s_reference_program);
                glUniform1i(s_reference_locations.cloud_map, 0);
                glUniform1i(s_reference_locations.refraction, 1);
            }
            glDeleteShader(reference_fs);
        }
        std::string rain_source(reference_source);
        rain_source.insert(rain_source.find('\n', 1) + 1, "#define CLOUD_RAIN_PROBE 1\n");
        GLuint rain_fs = compile_shader(GL_FRAGMENT_SHADER, rain_source.c_str());
        if (rain_fs)
        {
            s_rain_program = glCreateProgram();
            glAttachShader(s_rain_program, vs);
            glAttachShader(s_rain_program, rain_fs);
            glLinkProgram(s_rain_program);
            GLint rain_ok = 0;
            glGetProgramiv(s_rain_program, GL_LINK_STATUS, &rain_ok);
            if (!rain_ok)
            {
                glDeleteProgram(s_rain_program);
                s_rain_program = 0;
            }
            else
            {
                s_rain_locations.load(s_rain_program);
            }
            glDeleteShader(rain_fs);
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
        s_uTileColumnsLoc = glGetUniformLocation(s_program, "uTileColumns");

        glUseProgram(s_program);
        glUniform1i(s_uCloudMapLoc, 0);
        glUniform1i(s_uRefractionLoc, 1);
        glUniform1i(glGetUniformLocation(s_program, "uGeometry"), 2);
        glUniform1i(glGetUniformLocation(s_program, "uTiles"), 3);

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

        const unsigned short indices[6] =
        {
            0, 1, 2, 0, 2, 3
        };
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
        if (s_gen_framebuffers && s_bind_framebuffer && s_framebuffer_texture && s_check_framebuffer)
        {
            // Generate the exact GLSL shapes once per local patch. Keeping the
            // hash and noise on the GPU avoids CPU/GPU rounding changing clouds.
            std::string geometry_source(kCloudFragmentShaderSrc);
            geometry_source.resize(geometry_source.find("\nvoid main()"));
            geometry_source += R"GLSL(
uniform sampler2D uClusterCells;
void main()
{
    int field = int(gl_FragCoord.x);
    vec3 cell = texelFetch(uClusterCells, ivec2(0, int(gl_FragCoord.y)), 0).xyz;
    float step = 2.8 / uWorld[0].y;
    float clat = -HALF_PI + (cell.x + 0.5) * step;
    float clon = -PI + (cell.y + 0.5) * (TWO_PI / cell.z);
    vec3 center = uWorld[0].y * vec3(cos(clat) * cos(clon), cos(clat) * sin(clon), sin(clat));
    vec2 uv = vec2(clon / TWO_PI + 0.5, 0.5 - clat / PI);
    float coverage = dot(textureLod(uCloudMap, uv + uLight[2].xy, 0.0).rgb, vec3(0.29, 0.53, 0.18));
    float mass = clamp(0.5 + noise(center * 0.37), 0.0, 1.0);
    if (field == 0)
    {
        FragColor = vec4(center, coverage > 0.0 && mass <= min(1.0, coverage * 1.8) ? 1.0 : 0.0);
    }
    else if (field == 1)
    {
        FragColor = vec4(cell, cloud_base(mass));
    }
    else if (field >= 110)
    {
        int puff = (field - 110) / 32;
        int child = ((field - 110) / 4) % 8;
        FragColor = cloud_detail(puff, child, (field - 110) % 4, mass);
    }
    else
    {
        int puff = field < 14 ? (field - 2) / 2 : (field - 14) / 16;
        vec3 parent, parent_axes, p, axes;
        cloud_parent(puff, mass, parent, parent_axes);
        if (field < 14)
        {
            p = parent;
            axes = parent_axes;
        }
        else
        {
            cloud_billow(puff, ((field - 14) / 2) % 8, mass, parent, parent_axes, p, axes);
        }
        FragColor = (field & 1) == 0 ? vec4(p, axes.x) : vec4(axes.z, 0.0, 0.0, 0.0);
    }
}
)GLSL";
            GLuint geometry_vs = compile_shader(GL_VERTEX_SHADER, kCloudVertexShaderSrc);
            GLuint geometry_fs = compile_shader(GL_FRAGMENT_SHADER, geometry_source.c_str());
            if (geometry_vs && geometry_fs)
            {
                s_geometry_program = glCreateProgram();
                glAttachShader(s_geometry_program, geometry_vs);
                glAttachShader(s_geometry_program, geometry_fs);
                glLinkProgram(s_geometry_program);
                glGetProgramiv(s_geometry_program, GL_LINK_STATUS, &ok);
                if (!ok)
                {
                    glDeleteProgram(s_geometry_program);
                    s_geometry_program = 0;
                }
                else
                {
                    s_geometry_pos = glGetAttribLocation(s_geometry_program, "aPos");
                    // Geometry generation uses gl_FragCoord rather than vXY.
                    s_geometry_xy = glGetAttribLocation(s_geometry_program, "aXY");
                    s_geometry_world = glGetUniformLocation(s_geometry_program, "uWorld");
                    s_geometry_light = glGetUniformLocation(s_geometry_program, "uLight");
                    glUseProgram(s_geometry_program);
                    glUniform1i(glGetUniformLocation(s_geometry_program, "uCloudMap"), 0);
                    glUniform1i(glGetUniformLocation(s_geometry_program, "uClusterCells"), 2);
                }
            }
            if (geometry_vs)
            {
                glDeleteShader(geometry_vs);
            }
            if (geometry_fs)
            {
                glDeleteShader(geometry_fs);
            }
        }
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major > 3 || (major == 3 && minor >= 3) || SDL_GL_ExtensionSupported("GL_ARB_timer_query"))
        {
            s_gen_queries = (GenQueriesFn)SDL_GL_GetProcAddress("glGenQueries");
            s_begin_query = (BeginQueryFn)SDL_GL_GetProcAddress("glBeginQuery");
            s_end_query = (EndQueryFn)SDL_GL_GetProcAddress("glEndQuery");
            s_get_query = (GetQueryivFn)SDL_GL_GetProcAddress("glGetQueryiv");
            s_query_available = (GetQueryObjectivFn)SDL_GL_GetProcAddress("glGetQueryObjectiv");
            s_query_result = (GetQueryObject64Fn)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
            if (s_gen_queries && s_begin_query && s_end_query && s_get_query && s_query_available && s_query_result)
            {
                GLint bits = 0;
                s_get_query(0x88BF /* GL_TIME_ELAPSED */, 0x8864 /* GL_QUERY_COUNTER_BITS */, &bits);
                s_stats.timers_available = bits > 0;
            }
        }
        glBindVertexArray(0);
    }

    static void draw_quad(const CloudSkyGpuParams &p, GLint pos, GLint xy)
    {
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(p.verts), p.verts);
        glEnableVertexAttribArray(pos);
        glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
        if (xy >= 0)
        {
            glEnableVertexAttribArray(xy);
            glVertexAttribPointer(xy, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
        }
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    static bool cache_matches(const CloudSkyGpuParams &p, int width, int height)
    {
        if (!s_cache_valid || width != s_output_width || height != s_output_height
            || p.texture != s_cached_params.texture || p.revision != s_cached_params.revision
            || p.geometry_mode != s_cached_params.geometry_mode
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

    static double dot3(const Vec3 &a, const Vec3 &b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    static Vec3 difference(const Vec3 &a, const Vec3 &b)
    {
        return
        {
            a[0] - b[0], a[1] - b[1], a[2] - b[2]
        };
    }

    static void float_texture(GLuint &texture, int width, int height, const float *data, bool scalar = false)
    {
        if (!texture)
        {
            glGenTextures(1, &texture);
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, scalar ? 0x822E /* GL_R32F */ : 0x8814 /* GL_RGBA32F */,
            width, height, 0, scalar ? 0x1903 /* GL_RED */ : GL_RGBA, GL_FLOAT, data);
    }

    static bool prepare_geometry(const CloudSkyGpuParams &p)
    {
        if (!s_geometry_program || p.geometry_mode == CloudGeometryMode::reference || p.world[1] < 50
            || p.light[2] > 0.5f)
        {
            return false;
        }
        constexpr double pi = 3.141592653589793;
        const float shell = p.world[1];
        const Vec3 anchor =
        {
            p.camera[12] * shell, p.camera[13] * shell, p.camera[14] * shell
        };
        const Vec3 shift = difference(anchor, s_geometry_anchor);
        if (s_geometry_valid && dot3(shift, shift) <= 4.0
            && shell == s_geometry_params.world[1] && p.texture == s_geometry_params.texture
            && p.revision == s_geometry_params.revision
            && p.light[8] == s_geometry_params.light[8] && p.light[9] == s_geometry_params.light[9])
        {
            return true;
        }
        s_geometry_valid = false;
        s_cells.clear();
        const double latitude = std::atan2(anchor[2], std::hypot(anchor[0], anchor[1]));
        const double longitude = std::atan2(anchor[1], anchor[0]);
        // Eligible centers lie within 10 units of the observer. This patch
        // also covers shell height, two units of travel, and polar neighbors.
        const double angle = 2 * std::asin(std::min(1.0, 22.0 / (2 * shell)));
        const float step = 2.8f / shell;
        const float half_pi = 1.570796326794897f;
        const float two_pi = 6.283185307179586f;
        const float float_pi = 3.141592653589793f;
        const int first = std::max(0, (int)std::floor((latitude - angle + half_pi) / step) - 1);
        const int last = (int)std::floor((std::min(pi * 0.5, latitude + angle) + half_pi) / step) + 1;
        for (int band = first; band <= last; ++band)
        {
            const float clat = -half_pi + (float(band) + 0.5f) * step;
            if (std::fabs(clat) >= half_pi)
            {
                continue;
            }
            const float cells = std::max(1.0f, std::round(two_pi * shell * std::cos(clat) / 2.8f));
            const float lon_step = two_pi / cells;
            const double denom = std::cos(latitude) * std::cos(clat);
            const double bound = (std::cos(angle) - std::sin(latitude) * std::sin(clat)) / std::max(1e-15, denom);
            const double span = std::acos(std::clamp(bound, -1.0, 1.0));
            const int middle = (int)std::floor((longitude + float_pi) / lon_step);
            const int extent = (int)std::ceil(span / lon_step) + 2;
            const int count = std::min((int)cells, extent * 2 + 1);
            for (int i = 0; i < count; ++i)
            {
                const int column = ((middle - extent + i) % (int)cells + (int)cells) % (int)cells;
                const float clon = -float_pi + (float(column) + 0.5f) * lon_step;
                CloudCell cell;
                cell.grid =
                {
                    float(band), float(column), cells, 0.0f
                };
                cell.center =
                {
                    shell * std::cos(clat) * std::cos(clon),
                    shell * std::cos(clat) * std::sin(clon), shell * std::sin(clat)
                };
                s_cells.push_back(cell);
            }
        }
        GLint max_texture = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
        if (s_cells.empty() || s_cells.size() + 1 > (size_t)max_texture)
        {
            return false;
        }
        std::vector<float> grid;
        for (const auto &cell : s_cells)
        {
            grid.insert(grid.end(), cell.grid.begin(), cell.grid.end());
        }
        CloudTimer *timer = begin_cloud_timer(0);
        glActiveTexture(GL_TEXTURE0 + 2);
        float_texture(s_cell_texture, 1, (int)s_cells.size(), grid.data());
        glActiveTexture(GL_TEXTURE0 + 3);
        float_texture(s_geometry_texture, 302, (int)s_cells.size(), nullptr);
        if (!s_geometry_framebuffer)
        {
            s_gen_framebuffers(1, &s_geometry_framebuffer);
        }
        s_bind_framebuffer(kFramebuffer, s_geometry_framebuffer);
        s_framebuffer_texture(kFramebuffer, kColorAttachment, GL_TEXTURE_2D, s_geometry_texture, 0);
        const bool complete = s_check_framebuffer(kFramebuffer) == kFramebufferComplete;
        if (complete)
        {
            glViewport(0, 0, 302, (int)s_cells.size());
            glDisable(GL_BLEND);
            glUseProgram(s_geometry_program);
            glUniformMatrix4fv(s_geometry_world, 1, GL_FALSE, p.world);
            glUniformMatrix4fv(s_geometry_light, 1, GL_FALSE, p.light);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p.texture);
            draw_quad(p, s_geometry_pos, s_geometry_xy);
            s_geometry_anchor = anchor;
            s_geometry_params = p;
            s_geometry_valid = true;
            ++s_stats.geometry_builds;
            s_stats.clusters = (unsigned int)s_cells.size();
        }
        end_cloud_timer(timer);
        return complete;
    }

    static Vec3 camera_ray(const CloudSkyGpuParams &p, double x, double y)
    {
        const double az = p.camera[0], alt = p.camera[1];
        x *= p.camera[2];
        y *= p.camera[3];
        const double forward = std::cos(alt) - y * std::sin(alt);
        Vec3 ray =
        {
            x * std::cos(az) + forward * std::sin(az),
            y * std::cos(alt) + std::sin(alt),
            forward * std::cos(az) - x * std::sin(az)
        };
        const double norm = std::sqrt(dot3(ray, ray));
        for (auto &component : ray)
        {
            component /= norm;
        }
        Vec3 world =
        {
        };
        for (int i = 0; i < 3; ++i)
        {
            world[i] = p.camera[4 + i] * ray[0] + p.camera[8 + i] * ray[2] + p.camera[12 + i] * ray[1];
        }
        return world;
    }

    static bool prepare_tiles(const CloudSkyGpuParams &p, int width, int height)
    {
        const auto start = std::chrono::steady_clock::now();
        const int columns = (width + 31) / 32, rows = (height + 31) / 32;
        const int stride = (int)s_cells.size() + 1;
        GLint max_texture = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
        if (rows * columns > max_texture)
        {
            return false;
        }
        std::vector<float> tiles((size_t)stride * columns * rows, 0.0f);
        const Vec3 eye =
        {
            p.camera[12] * p.world[0], p.camera[13] * p.world[0], p.camera[14] * p.world[0]
        };
        struct ClusterCone
        {
            Vec3 axis;
            double angle, sine, cosine;
        };
        std::vector<ClusterCone> cluster_cones;
        for (const auto &cell : s_cells)
        {
            ClusterCone cone;
            cone.axis = difference(cell.center, eye);
            const double distance = std::sqrt(dot3(cone.axis, cone.axis));
            const double radius = 1.40 + p.world[1] * 2e-6;
            cone.sine = std::min(1.0, radius / std::max(1e-15, distance));
            cone.cosine = std::sqrt(std::max(0.0, 1.0 - cone.sine * cone.sine));
            cone.angle = std::asin(cone.sine);
            for (auto &component : cone.axis)
            {
                component /= std::max(1e-15, distance);
            }
            cluster_cones.push_back(cone);
        }
        unsigned int total = 0, maximum = 0;
        for (int ty = 0; ty < rows; ++ty)
        {
            const double bottom = 2.0 * (ty * 32) / height - 1;
            const double top = 2.0 * std::min(height, (ty + 1) * 32) / height - 1;
            for (int tx = 0; tx < columns; ++tx)
            {
                const double left = 2.0 * (tx * 32) / width - 1;
                const double right = 2.0 * std::min(width, (tx + 1) * 32) / width - 1;
                const Vec3 axis = camera_ray(p, (left + right) * 0.5, (bottom + top) * 0.5);
                double cone = 0;
                const double xs[2] =
                {
                    left, right
                };
                const double ys[2] =
                {
                    bottom, top
                };
                for (double x : xs)
                {
                    for (double y : ys)
                    {
                        cone = std::max(cone, std::acos(std::clamp(dot3(axis, camera_ray(p, x, y)), -1.0, 1.0)));
                    }
                }
                // Every refracted ray lies inside the perspective tile cone
                // plus the largest angular shift in the inverse curve.
                cone += s_refraction_span + 0.0001;
                if (cone >= 1.570796326794897)
                {
                    // A cone wider than a hemisphere is not convex; retain all
                    // clusters rather than relying only on its corner rays.
                    cone = 3.141592653589793;
                }
                const double cosine = std::cos(cone), sine = std::sin(cone);
                const size_t base = (size_t)(ty * columns + tx) * stride;
                unsigned int count = 0;
                for (size_t cluster = 0; cluster < s_cells.size(); ++cluster)
                {
                    // Bounds include the whole sphere, even across the camera
                    // plane or with the observer inside it. No center culling.
                    const auto &cluster_cone = cluster_cones[cluster];
                    if (p.geometry_mode == CloudGeometryMode::all_clusters || cluster_cone.sine >= 1.0
                        || cone + cluster_cone.angle >= 3.141592653589793
                        || dot3(axis, cluster_cone.axis) >= cosine * cluster_cone.cosine - sine * cluster_cone.sine)
                    {
                        tiles[base + (++count)] = (float)cluster;
                    }
                }
                tiles[base] = (float)count;
                total += count;
                maximum = std::max(maximum, count);
            }
        }
        glActiveTexture(GL_TEXTURE0 + 3);
        float_texture(s_tile_texture, stride, rows * columns, tiles.data(), true);
        s_stats.max_tile_clusters = maximum;
        s_stats.mean_tile_clusters = double(total) / (rows * columns);
        s_stats.tile_cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return true;
    }

    static bool prepare_cache(int width, int height)
    {
        if (!s_composite || !s_gen_framebuffers || !s_bind_framebuffer
            || !s_framebuffer_texture || !s_check_framebuffer)
        {
            return false;
        }
        // Render a compact cloud layer, then upscale with linear filtering.
        // This also reduces the tile work during camera motion.
        const double scale = std::min(0.4, std::min(896.0 / width, 504.0 / height));
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
            const int render_width = cached ? s_cache_width : viewport[2];
            const int render_height = cached ? s_cache_height : viewport[3];
            const bool geometry = prepare_geometry(p) && prepare_tiles(p, render_width, render_height);
            s_stats.geometry_cached = geometry;
            ++s_stats.image_builds;
            float world[16];
            std::memcpy(world, p.world, sizeof(world));
            world[4] = cached ? (float)s_cache_width / viewport[2] : 1.0f;
            world[5] = cached ? (float)s_cache_height / viewport[3] : 1.0f;
            world[6] = cached ? 0.0f : (float)viewport[0];
            world[7] = cached ? 0.0f : (float)viewport[1];
            if (cached)
            {
                s_bind_framebuffer(kFramebuffer, s_cache_framebuffer);
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
                glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            }
            const GLuint program = geometry ? s_program : s_reference_program;
            glUseProgram(program);
            if (geometry)
            {
                glUniform1i(s_uTileColumnsLoc, (render_width + 31) / 32);
            }
            glUniformMatrix4fv(geometry ? s_uCameraLoc : s_reference_locations.camera, 1, GL_FALSE, p.camera);
            glUniformMatrix4fv(geometry ? s_uWorldLoc : s_reference_locations.world, 1, GL_FALSE, world);
            glUniformMatrix4fv(geometry ? s_uLightLoc : s_reference_locations.light, 1, GL_FALSE, p.light);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p.texture);
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, s_refraction);
            glActiveTexture(GL_TEXTURE0 + 2);
            glBindTexture(GL_TEXTURE_2D, s_geometry_texture);
            glActiveTexture(GL_TEXTURE0 + 3);
            glBindTexture(GL_TEXTURE_2D, s_tile_texture);
            CloudTimer *timer = begin_cloud_timer(1);
            draw_quad(p, geometry ? s_aPosLoc : s_reference_locations.position,
                geometry ? s_aXYLoc : s_reference_locations.xy);
            end_cloud_timer(timer);
            if (cached)
            {
                s_cached_params = p;
                s_output_width = viewport[2];
                s_output_height = viewport[3];
                s_cache_valid = true;
            }
        }
        else
        {
            ++s_stats.image_hits;
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
            CloudTimer *timer = begin_cloud_timer(2);
            draw_quad(p, s_composite_pos, s_composite_xy);
            end_cloud_timer(timer);
        }
        // Extra texture units belong to this callback; clear them before ImGui
        // restores its own program, unit zero, and vertex state.
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(0);
    }

    const CloudRainCover &cloud_rain_cover()
    {
        return s_rain_cover;
    }

    static void sample_rain_cover(const CloudSkyGpuParams &p)
    {
        using GetFloatvFn = void (APIENTRY *)(GLenum, GLfloat*);
        using GetBooleanvFn = void (APIENTRY *)(GLenum, GLboolean*);
        using ColorMaskFn = void (APIENTRY *)(GLboolean, GLboolean, GLboolean, GLboolean);
        static const auto get_float = (GetFloatvFn)SDL_GL_GetProcAddress("glGetFloatv");
        static const auto get_boolean = (GetBooleanvFn)SDL_GL_GetProcAddress("glGetBooleanv");
        static const auto color_mask_fn = (ColorMaskFn)SDL_GL_GetProcAddress("glColorMask");
        if (!s_rain_program || !s_bind_framebuffer || !s_gen_framebuffers
            || !s_framebuffer_texture || !s_check_framebuffer || !get_float || !get_boolean || !color_mask_fn)
        {
            s_rain_cover = CloudRainCover();
            return;
        }
        if (s_rain_cached && p.texture == s_rain_params.texture && p.revision == s_rain_params.revision
            && std::memcmp(p.camera + 4, s_rain_params.camera + 4, 12 * sizeof(float)) == 0
            && std::memcmp(p.world, s_rain_params.world, 4 * sizeof(float)) == 0
            && p.light[2] == s_rain_params.light[2]
            && p.light[8] == s_rain_params.light[8] && p.light[9] == s_rain_params.light[9])
        {
            return;
        }
        GLint framebuffer, viewport[4], program, vao, buffer, active, textures[2], pack_row;
        GLfloat clear[4];
        GLboolean color_mask[4];
        glGetIntegerv(kFramebufferBinding, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(0x0D02 /* GL_PACK_ROW_LENGTH */, &pack_row);
        get_float(0x0C22 /* GL_COLOR_CLEAR_VALUE */, clear);
        get_boolean(0x0C23 /* GL_COLOR_WRITEMASK */, color_mask);
        const bool blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
        const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
        for (int unit = 0; unit < 2; ++unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[unit]);
        }
        glActiveTexture(GL_TEXTURE0);
        if (!s_rain_texture)
        {
            glGenTextures(1, &s_rain_texture);
            s_gen_framebuffers(1, &s_rain_framebuffer);
            glBindTexture(GL_TEXTURE_2D, s_rain_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, 0x8814 /* GL_RGBA32F */, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
        }
        s_bind_framebuffer(kFramebuffer, s_rain_framebuffer);
        s_framebuffer_texture(kFramebuffer, kColorAttachment, GL_TEXTURE_2D, s_rain_texture, 0);
        s_rain_cover = CloudRainCover();
        if (s_check_framebuffer(kFramebuffer) == kFramebufferComplete)
        {
            glViewport(0, 0, 1, 1);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            color_mask_fn(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(s_rain_program);
            glUniformMatrix4fv(s_rain_locations.camera, 1, GL_FALSE, p.camera);
            glUniformMatrix4fv(s_rain_locations.world, 1, GL_FALSE, p.world);
            glUniformMatrix4fv(s_rain_locations.light, 1, GL_FALSE, p.light);
            glUniform1i(s_rain_locations.cloud_map, 0);
            glUniform1i(s_rain_locations.refraction, 1);
            glBindTexture(GL_TEXTURE_2D, p.texture);
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, s_refraction);
            draw_quad(p, s_rain_locations.position, s_rain_locations.xy);
            float cover[4] = {};
            glPixelStorei(0x0D02 /* GL_PACK_ROW_LENGTH */, 0);
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, cover);
            s_rain_cover.valid = true;
            s_rain_cover.cumulus = cover[0];
            s_rain_cover.heavy_haze = cover[1];
            s_rain_params = p;
            s_rain_cached = true;
        }
        s_bind_framebuffer(kFramebuffer, static_cast<GLuint>(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
        for (int unit = 0; unit < 2; ++unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textures[unit]));
        }
        glActiveTexture(static_cast<GLenum>(active));
        glPixelStorei(0x0D02 /* GL_PACK_ROW_LENGTH */, pack_row);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        color_mask_fn(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        if (blend)
        {
            glEnable(GL_BLEND);
        }
        if (scissor)
        {
            glEnable(GL_SCISSOR_TEST);
        }
        if (depth)
        {
            glEnable(GL_DEPTH_TEST);
        }
        if (cull)
        {
            glEnable(GL_CULL_FACE);
        }
    }

    bool queue_cloud_sky_gpu(const CloudSkyGpuInput &in)
    {
        ensure_gl_objects();
        if (!s_program || !s_reference_program || !in.texture)
        {
            return false;
        }
        if (!s_inverse_valid || std::memcmp(s_table, in.refraction, sizeof(s_table)) != 0)
        {
            std::memcpy(s_table, in.refraction, sizeof(s_table));
            float inverse[1025];
            int index = 0;
            constexpr double pi = 3.141592653589793;
            s_refraction_span = 0;
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
                s_refraction_span = std::max(s_refraction_span, std::fabs(inverse[i] - altitude));
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
        p->geometry_mode = in.geometry_mode;
        if (in.sample_overhead)
        {
            sample_rain_cover(*p);
        }
        else
        {
            s_rain_cover = CloudRainCover();
            s_rain_cached = false;
        }
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddCallback(render_cloud_sky_gpu, p);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return true;
    }
}
