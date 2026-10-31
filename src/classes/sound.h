#ifndef _SOUND_H
#define _SOUND_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#if defined(__has_include)
  #if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #else
    #include <SDL.h>
  #endif
#else
  #include <SDL.h>
#endif

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
        bool horizon_tracking_active = false;
        std::vector<CelestialObject*> horizon_targets;
        std::uint64_t horizon_revision = 0, horizon_rebuilds = 0;
        CelestialObject** horizon_array = nullptr;
        int horizon_count = -1;
        const std::vector<CelestialObject*>& armed_targets();

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
        void leave_horizon();
        void check_rise_set_alerts();
        std::size_t horizon_alert_count() const { return horizon_targets.size(); }
        std::uint64_t horizon_alert_rebuilds() const { return horizon_rebuilds; }

        void set_rise_callback(std::function<void(CelestialObject*)> cb);
        void set_set_callback(std::function<void(CelestialObject*)> cb);
    };

    void process_select_rise_sound();
    void process_select_set_sound();
}

#endif
