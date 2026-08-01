
#include <unordered_map>
#include <vector>
#include "gputex.h"

using namespace alienorum;

namespace alienorum
{
    struct GpuTexEntry
    {
        GLuint tex = 0;
        unsigned int gen = 0;
    };

    // Only ever touched from the thread owning the GL context (the main/render thread), same
    // as every other GL call in the app -- see LoadTextureFromMemory() in alienorum.cpp for
    // the existing precedent of raw GL texture upload outside of ImGui.
    static std::unordered_map<Map*, GpuTexEntry> gputex_cache;

    GLuint gputex_for(Map* map)
    {
        if (!map || !map->has_rgb_data()) return 0;

        GpuTexEntry &entry = gputex_cache[map];
        if (entry.tex && entry.gen == map->gen) return entry.tex;

        unsigned long w = map->get_width(), h = map->get_height();
        std::vector<unsigned char> rgba(w * h * 4);
        map->export_rgba(rgba.data());

        if (!entry.tex) glGenTextures(1, &entry.tex);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);          // longitude wraps
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   // latitude does not
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        entry.gen = map->gen;
        return entry.tex;
    }

    void gputex_clear_cache()
    {
        for (auto &[map, entry] : gputex_cache)
            if (entry.tex) glDeleteTextures(1, &entry.tex);
        gputex_cache.clear();
    }
}
