
#ifndef _AlienorumGpuTex
#define _AlienorumGpuTex

#include "globals.h"

namespace alienorum
{
    // Returns a cached OpenGL texture name holding this map's current RGB pixel data as
    // GL_RGBA8, uploading (or re-uploading, if the map has changed since the last request --
    // see Map::gen) as necessary. Must only be called from the thread owning the GL context.
    // Returns 0 if map is null or has no RGB pixel data yet (e.g. still loading
    // asynchronously -- see draw_sphere()'s load_textures thread).
    GLuint gputex_for(Map* map);

    // Frees every cached texture. Not required in normal operation (stale entries are simply
    // replaced as their Map changes) -- provided for explicit teardown if ever necessary.
    void gputex_clear_cache();
}

#endif
