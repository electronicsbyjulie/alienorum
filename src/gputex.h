
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

    // Same idea, for the map's bump/elevation data (Map::bump_data) instead of its color --
    // a single-channel GL_R32F texture holding real-world elevation values in meters (same
    // units as Map::elevation_at()), for the GPU disc impostor's bump-mapped lighting (see
    // sphere_impostor.cpp). Returns 0 if map is null or has no bump data (has_bump_data()
    // false) -- most maps never load one; this is not the common case gputex_for() is.
    GLuint gputex_bump_for(Map* map);

    // Frees every cached texture. Not required in normal operation (stale entries are simply
    // replaced as their Map changes) -- provided for explicit teardown if ever necessary.
    void gputex_clear_cache();
}

#endif
