#pragma once

#include "misc.h"

namespace alienorum
{
    class CelestialObject;

    struct TextureGenerationOptions
    {
        bool randomize = true;
        bool tau_calculator = false;
    };

    TextureGenerationOptions texture_generation_options(const CelestialObject* object);
    inline thread_local const TextureGenerationOptions* active_texture_generation_options = nullptr;

    class ScopedTextureGenerationOptions
    {
        TextureGenerationOptions options;
        const TextureGenerationOptions* previous;

        public:
        explicit ScopedTextureGenerationOptions(TextureGenerationOptions selected)
            : options(selected), previous(active_texture_generation_options)
        {
            active_texture_generation_options = &options;
        }
        explicit ScopedTextureGenerationOptions(const CelestialObject* object)
            : ScopedTextureGenerationOptions(active_texture_generation_options
                ? *active_texture_generation_options : texture_generation_options(object))
        {
        }
        ~ScopedTextureGenerationOptions() { active_texture_generation_options = previous; }
        ScopedTextureGenerationOptions(const ScopedTextureGenerationOptions&) = delete;
        ScopedTextureGenerationOptions& operator=(const ScopedTextureGenerationOptions&) = delete;
    };

    inline bool texture_randomization_enabled()
    {
        return active_texture_generation_options ? active_texture_generation_options->randomize : randomize_txgen;
    }

    inline bool texture_tau_calculator_active()
    {
        return active_texture_generation_options ? active_texture_generation_options->tau_calculator : show_taucalc;
    }
}
