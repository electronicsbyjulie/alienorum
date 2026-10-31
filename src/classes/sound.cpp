#include "../performance_options.h"
#include "sound.h"
#include "celestial.h"
#include "misc.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <cstring>

extern bool play_rise_set_sound;
extern std::string rise_sound_path;
extern std::string set_sound_path;

namespace alienorum
{
    void invalidate_horizon_alerts()
    {
#if PERF_P006
        horizon_alert_revision().fetch_add(1, std::memory_order_relaxed);
#endif
    }

    SoundManager::SoundManager()
    {
    }

    SoundManager::~SoundManager()
    {
        close_audio();
    }

    SoundManager& SoundManager::get_instance()
    {
        static SoundManager instance;
        return instance;
    }

    bool SoundManager::init_audio()
    {
        if (audio_initialized && audio_device > 0)
        {
            return true;
        }

        if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
        {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            {
                return false;
            }
        }

        SDL_AudioSpec desired;
        SDL_zero(desired);
        desired.freq = 44100;
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
        desired.samples = 2048;

        audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &device_spec, 0);
        if (audio_device == 0)
        {
            return false;
        }

        SDL_PauseAudioDevice(audio_device, 0);
        audio_initialized = true;
        return true;
    }

    void SoundManager::close_audio()
    {
        if (audio_device > 0)
        {
            SDL_CloseAudioDevice(audio_device);
            audio_device = 0;
        }
        audio_initialized = false;
    }

    bool SoundManager::play_sound_file(const std::string& filepath)
    {
        if (!play_rise_set_sound)
        {
            return false;
        }

        if (!init_audio())
        {
            return false;
        }

        SDL_AudioSpec wav_spec;
        Uint8 *wav_buffer = nullptr;
        Uint32 wav_length = 0;

        if (SDL_LoadWAV(filepath.c_str(), &wav_spec, &wav_buffer, &wav_length) == nullptr)
        {
            return false;
        }

        // Avoid building up huge audio latency if multiple sounds are queued
        if (SDL_GetQueuedAudioSize(audio_device) > (Uint32)(device_spec.freq * device_spec.channels * sizeof(int16_t) * 5))
        {
            SDL_ClearQueuedAudio(audio_device);
        }

        if (wav_spec.format == device_spec.format
            && wav_spec.channels == device_spec.channels
            && wav_spec.freq == device_spec.freq)
        {
            SDL_QueueAudio(audio_device, wav_buffer, wav_length);
        }
        else
        {
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq,
                                  device_spec.format, device_spec.channels, device_spec.freq) >= 0)
            {
                cvt.len = wav_length;
                cvt.buf = (Uint8*)malloc(wav_length * cvt.len_mult);
                if (cvt.buf)
                {
                    std::memcpy(cvt.buf, wav_buffer, wav_length);
                    if (SDL_ConvertAudio(&cvt) >= 0)
                    {
                        SDL_QueueAudio(audio_device, cvt.buf, cvt.len_cvt);
                    }
                    free(cvt.buf);
                }
            }
        }

        SDL_FreeWAV(wav_buffer);
        SDL_PauseAudioDevice(audio_device, 0);
        return true;
    }

    void SoundManager::play_procedural_chime(bool ascending)
    {
        if (!init_audio())
        {
            return;
        }

        std::vector<double> freqs;
        if (ascending)
        {
            freqs = { 523.25, 659.25, 783.99, 1046.50 };
        }
        else
        {
            freqs = { 1046.50, 783.99, 659.25, 523.25 };
        }

        double duration = 3.5;
        int sample_rate = device_spec.freq;
        int num_samples = (int)(sample_rate * duration);
        std::vector<int16_t> samples(num_samples * 2);

        for (int i = 0; i < num_samples; i++)
        {
            double t = (double)i / sample_rate;
            double sample_val = 0.0;
            for (size_t idx = 0; idx < freqs.size(); idx++)
            {
                double note_start = idx * 0.1;
                if (t >= note_start)
                {
                    double t_note = t - note_start;
                    double decay = (idx < 2 ? 1.6 : 1.1);
                    double env = std::exp(-decay * t_note);
                    double tone = std::sin(2.0 * _pi * freqs[idx] * t_note)
                                  + 0.25 * std::sin(4.0 * _pi * freqs[idx] * t_note);
                    sample_val += tone * env;
                }
            }

            if (t > duration - 0.25)
            {
                double fade = (duration - t) / 0.25;
                sample_val *= fade;
            }

            sample_val = std::fmax(-1.0, std::fmin(1.0, sample_val * 0.35));
            int16_t val_int = (int16_t)(sample_val * 32767.0);
            samples[2 * i] = val_int;
            samples[2 * i + 1] = val_int;
        }

        if (SDL_GetQueuedAudioSize(audio_device) > (Uint32)(device_spec.freq * device_spec.channels * sizeof(int16_t) * 5))
        {
            SDL_ClearQueuedAudio(audio_device);
        }

        SDL_QueueAudio(audio_device, samples.data(), samples.size() * sizeof(int16_t));
        SDL_PauseAudioDevice(audio_device, 0);
    }

    void SoundManager::play_rise_sound(CelestialObject* obj)
    {
        if (on_rise_callback)
        {
            on_rise_callback(obj);
        }

        if (!play_rise_set_sound)
        {
            return;
        }

        if (!play_sound_file(rise_sound_path))
        {
            if (rise_sound_path != (default_rise_sound_path) && play_sound_file(default_rise_sound_path))
            {
                return;
            }
            play_procedural_chime(true);
        }
    }

    void SoundManager::play_set_sound(CelestialObject* obj)
    {
        if (on_set_callback)
        {
            on_set_callback(obj);
        }

        if (!play_rise_set_sound)
        {
            return;
        }

        if (!play_sound_file(set_sound_path))
        {
            if (set_sound_path != (default_set_sound_path) && play_sound_file(default_set_sound_path))
            {
                return;
            }
            play_procedural_chime(false);
        }
    }

    const std::vector<CelestialObject*>& SoundManager::armed_targets()
    {
        const auto revision = horizon_alert_revision().load(std::memory_order_relaxed);
        if (revision != horizon_revision || cels != horizon_array || ncelobjs != horizon_count)
        {
            horizon_targets.clear();
            if (cels) for (int i = 0; i < MAX_CELOBJS && cels[i]; ++i)
            {
                if (cels[i]->alert_rise || cels[i]->alert_set) horizon_targets.push_back(cels[i]);
            }
            horizon_revision = revision;
            horizon_array = cels;
            horizon_count = ncelobjs;
            ++horizon_rebuilds;
        }
        return horizon_targets;
    }

    template<class Callback>
    static void visit_alert_targets(const std::vector<CelestialObject*>* targets, Callback&& callback)
    {
        if (targets)
        {
            for (CelestialObject* object : *targets) callback(object);
        }
        else
        {
            for (int i = 0; i < MAX_CELOBJS && cels[i]; ++i) callback(cels[i]);
        }
    }

    void SoundManager::reset_horizon_tracking()
    {
        horizon_tracking_active = false;
        if (!cels)
        {
            return;
        }

        const auto* targets = performance_enabled(6) ? &armed_targets() : nullptr;
        visit_alert_targets(targets, [](CelestialObject* object)
        {
            if (object->alert_rise || object->alert_set) object->has_prev_horizon_alt = false;
        });
    }

    void SoundManager::leave_horizon()
    {
        if (horizon_tracking_active) reset_horizon_tracking();
    }

    void SoundManager::check_rise_set_alerts()
    {
        if (view_mode != vm_horizon || whereami < 0 || !cels)
        {
            leave_horizon();
            return;
        }

        if (!horizon_tracking_active || whereami != last_whereami
            || std::fabs(viewer_lat - last_viewer_lat) > 0.01
            || std::fabs(viewer_lon - last_viewer_lon) > 0.01)
        {
            reset_horizon_tracking();
            last_whereami = whereami;
            last_viewer_lat = viewer_lat;
            last_viewer_lon = viewer_lon;
        }
        horizon_tracking_active = true;

        const auto* targets = performance_enabled(6) ? &armed_targets() : nullptr;
        visit_alert_targets(targets, [&](CelestialObject* object)
        {
            if (object->deleted)
            {
                return;
            }

            if (!object->alert_rise && !object->alert_set)
            {
                return;
            }

            double cur_alt = object->Decl_as_radians_refracted(here);

            if (!object->has_prev_horizon_alt)
            {
                object->prev_horizon_alt = cur_alt;
                object->has_prev_horizon_alt = true;
                return;
            }

            if (object->alert_rise && object->prev_horizon_alt <= 0.0 && cur_alt > 0.0)
            {
                play_rise_sound(object);
                object->alert_rise = false;
            }

            if (object->alert_set && object->prev_horizon_alt >= 0.0 && cur_alt < 0.0)
            {
                play_set_sound(object);
                object->alert_set = false;
            }

            object->prev_horizon_alt = cur_alt;
        });
    }

    void SoundManager::set_rise_callback(std::function<void(CelestialObject*)> cb)
    {
        on_rise_callback = cb;
    }

    void SoundManager::set_set_callback(std::function<void(CelestialObject*)> cb)
    {
        on_set_callback = cb;
    }
}
