#ifndef _SOUND_H
#define _SOUND_H

#include <string>
#include <vector>
#include <functional>
#include <SDL2/SDL.h>

namespace alienorum
{
    class CelestialObject;

    class SoundManager
    {
    private:
        SoundManager();
        ~SoundManager();

        SDL_AudioDeviceID audio_device = 0;
        SDL_AudioSpec device_spec;
        bool audio_initialized = false;

        int last_whereami = -1;
        double last_viewer_lat = 0.0;
        double last_viewer_lon = 0.0;

        std::function<void(CelestialObject*)> on_rise_callback;
        std::function<void(CelestialObject*)> on_set_callback;

        void play_procedural_chime(bool ascending);

    public:
        static SoundManager& get_instance();

        SoundManager(const SoundManager&) = delete;
        SoundManager& operator=(const SoundManager&) = delete;

        bool init_audio();
        void close_audio();

        bool play_sound_file(const std::string& filepath);
        void play_rise_sound(CelestialObject* obj = nullptr);
        void play_set_sound(CelestialObject* obj = nullptr);

        void reset_horizon_tracking();
        void check_rise_set_alerts();

        void set_rise_callback(std::function<void(CelestialObject*)> cb);
        void set_set_callback(std::function<void(CelestialObject*)> cb);
    };

    void process_select_rise_sound();
    void process_select_set_sound();
}

#endif
