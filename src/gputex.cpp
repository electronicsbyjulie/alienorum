
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>
#include "gputex.h"

using namespace alienorum;

namespace alienorum
{
    // glGenerateMipmap is core since GL 3.0 but predates this file's SDL_opengl.h -- the
    // legacy header sphere_impostor.cpp's own comment describes avoiding (imgui's bundled GL
    // loader would clash with SDL_opengl.h's typedefs in the same translation unit). On Windows
    // in particular, opengl32.dll only exports GL 1.1 statically, so this can't be linked
    // directly the way glGenTextures/glTexImage2D etc. below are -- it has to be resolved at
    // runtime like any other post-1.1 entry point.
    typedef void (APIENTRYP PFN_alienorum_glGenerateMipmap)(GLenum target);
    static void gputex_generate_mipmap(GLenum target)
    {
        static PFN_alienorum_glGenerateMipmap fn = (PFN_alienorum_glGenerateMipmap)SDL_GL_GetProcAddress("glGenerateMipmap");
        if (fn) fn(target);
    }

    struct GpuTexEntry
    {
        GLuint tex = 0;
        unsigned int gen = 0;
    };

    // Only ever touched from the main/render thread.
    static std::unordered_map<Map*, GpuTexEntry> gputex_cache;

    GLuint gputex_for(Map* map)
    {
        // gen==0 means touch_gen() hasn't run yet -- i.e. red_data/green_data/blue_data are
        // allocated (has_rgb_data() already true) but the load/generation fill loop that
        // actually writes real pixels into them hasn't finished. Uploading at that point grabs
        // whatever the background thread has gotten to plus uninitialized heap bytes for the
        // rest -- see Map::gen's own comment in celestial.h for the full story (bug: textures
        // showing a small "real" patch surrounded by glitch on whatever was looked at soon
        // after its load/generation started).
        if (!map || !map->gen || !map->has_rgb_data()) return 0;

        GpuTexEntry &entry = gputex_cache[map];
        if (entry.tex && entry.gen == map->gen) return entry.tex;

        unsigned long w = map->get_width(), h = map->get_height();
        std::vector<unsigned char> rgba(w * h * 4);
        map->export_rgba(rgba.data());

        // Downsample if either dimension exceeds the driver's max 2D texture size.
        // glTexImage2D() silently rejects an over-limit size (GL_INVALID_VALUE) and leaves the
        // texture object with no image data -- per the GL spec, sampling an incomplete texture
        // returns solid black, opaque. Not a corrupted image, not a visible error unless you
        // specifically check glGetError(), just a flatly black disc with a perfectly valid
        // nonzero texture name. Bug: a procedurally-generated map large enough to exceed the
        // limit (e.g. a 10000x5000 exoplanet cloud map, well past a common 8192 cap) rendering
        // solid black in the GPU disc path, while the CPU path -- which samples the CPU-side
        // red_data/green_data/blue_data arrays directly, no GPU texture involved at all --
        // showed the same map completely correctly.
        static GLint max_tex_size = 0;
        if (!max_tex_size)
        {
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
            if (max_tex_size <= 0) max_tex_size = 4096;   // conservative fallback if the query itself fails
        }
        if ((long)w > max_tex_size || (long)h > max_tex_size)
        {
            double scale = std::min((double)max_tex_size / w, (double)max_tex_size / h);
            unsigned long nw = std::max(1UL, (unsigned long)(w * scale));
            unsigned long nh = std::max(1UL, (unsigned long)(h * scale));

            std::vector<unsigned char> down(nw * nh * 4);
            for (unsigned long y = 0; y < nh; y++)
            {
                unsigned long sy = std::min(h - 1, (unsigned long)((double)y * h / nh));
                for (unsigned long x = 0; x < nw; x++)
                {
                    unsigned long sx = std::min(w - 1, (unsigned long)((double)x * w / nw));
                    for (int c = 0; c < 4; c++)
                        down[(y * nw + x) * 4 + c] = rgba[(sy * w + sx) * 4 + c];
                }
            }
            rgba.swap(down);
            w = nw; h = nh;
        }

        if (!entry.tex) glGenTextures(1, &entry.tex);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        // Mipmapped + trilinear rather than a flat GL_LINEAR: sampled at a steep grazing angle
        // (a ring viewed nearly edge-on from horizon mode, a planet's own limb) adjacent screen
        // pixels can land on wildly different texels with no mip chain to fall back to, which
        // reads as a shimmering moire crawling over the surface as the view shifts by even a
        // fraction of a pixel. Letting the GPU pick a coarser, pre-averaged level for compressed
        // footprints is the standard fix and costs nothing where minification isn't happening.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);          // longitude wraps
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   // latitude does not
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        gputex_generate_mipmap(GL_TEXTURE_2D);

        entry.gen = map->gen;
        return entry.tex;
    }

    // Separate from gputex_cache -- a Map can have both a color texture and a bump texture
    // cached at once, so they can't share one Map*-keyed table.
    static std::unordered_map<Map*, GpuTexEntry> gpubumptex_cache;

    GLuint gputex_bump_for(Map* map)
    {
        if (!map || !map->gen || !map->has_bump_data()) return 0;

        GpuTexEntry &entry = gpubumptex_cache[map];
        if (entry.tex && entry.gen == map->gen) return entry.tex;

        unsigned long w = map->get_width(), h = map->get_height();
        std::vector<float> bump(w * h);
        map->export_bump(bump.data());

        // Same downsampling reasoning as gputex_for() -- see its own comment -- reusing the
        // same GL_MAX_TEXTURE_SIZE query (a driver property, not a per-texture one).
        static GLint max_tex_size = 0;
        if (!max_tex_size)
        {
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
            if (max_tex_size <= 0) max_tex_size = 4096;
        }
        if ((long)w > max_tex_size || (long)h > max_tex_size)
        {
            double scale = std::min((double)max_tex_size / w, (double)max_tex_size / h);
            unsigned long nw = std::max(1UL, (unsigned long)(w * scale));
            unsigned long nh = std::max(1UL, (unsigned long)(h * scale));

            std::vector<float> down(nw * nh);
            for (unsigned long y = 0; y < nh; y++)
            {
                unsigned long sy = std::min(h - 1, (unsigned long)((double)y * h / nh));
                for (unsigned long x = 0; x < nw; x++)
                {
                    unsigned long sx = std::min(w - 1, (unsigned long)((double)x * w / nw));
                    down[y * nw + x] = bump[sy * w + sx];
                }
            }
            bump.swap(down);
            w = nw; h = nh;
        }

        if (!entry.tex) glGenTextures(1, &entry.tex);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        // See the identical comment in gputex_for(): same grazing-angle minification shimmer
        // applies to elevation data sampled near a planet's own limb.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);          // longitude wraps
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   // latitude does not
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, (GLsizei)w, (GLsizei)h, 0, GL_RED, GL_FLOAT, bump.data());
        gputex_generate_mipmap(GL_TEXTURE_2D);

        entry.gen = map->gen;
        return entry.tex;
    }

    void gputex_clear_cache()
    {
        for (auto &[map, entry] : gputex_cache)
            if (entry.tex) glDeleteTextures(1, &entry.tex);
        gputex_cache.clear();
        for (auto &[map, entry] : gpubumptex_cache)
            if (entry.tex) glDeleteTextures(1, &entry.tex);
        gpubumptex_cache.clear();
    }
}
