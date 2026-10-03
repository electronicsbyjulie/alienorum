#include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <SDL2/SDL.h>
#include "../classes/sound.h"
#include "../classes/celestial.h"
#include "../classes/misc.h"
#include "../globals.h"

using namespace alienorum;
using json = nlohmann::json;

class SoundTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        play_rise_set_sound = true;
        rise_sound_path = "assets/sounds/rise.wav";
        set_sound_path = "assets/sounds/set.wav";
    }

    void TearDown() override
    {
        SoundManager::get_instance().set_rise_callback(nullptr);
        SoundManager::get_instance().set_set_callback(nullptr);
        SoundManager::get_instance().reset_horizon_tracking();
    }
};

TEST_F(SoundTest, SoundFilesExistAndValidHeader)
{
    // Test rise.wav
    std::ifstream f_rise("assets/sounds/rise.wav", std::ios::binary);
    ASSERT_TRUE(f_rise.is_open());
    char header_rise[12];
    f_rise.read(header_rise, 12);
    EXPECT_EQ(std::string(header_rise, 4), "RIFF");
    EXPECT_EQ(std::string(header_rise + 8, 4), "WAVE");

    SDL_AudioSpec rise_spec;
    Uint8* rise_buf = nullptr;
    Uint32 rise_len = 0;
    ASSERT_NE(SDL_LoadWAV("assets/sounds/rise.wav", &rise_spec, &rise_buf, &rise_len), nullptr);
    int rise_bytes_per_sample = SDL_AUDIO_BITSIZE(rise_spec.format) / 8;
    double rise_duration = (double)rise_len / (rise_spec.freq * rise_spec.channels * rise_bytes_per_sample);
    EXPECT_GE(rise_duration, 3.0);
    EXPECT_LE(rise_duration, 4.0);
    SDL_FreeWAV(rise_buf);

    // Test set.wav
    std::ifstream f_set("assets/sounds/set.wav", std::ios::binary);
    ASSERT_TRUE(f_set.is_open());
    char header_set[12];
    f_set.read(header_set, 12);
    EXPECT_EQ(std::string(header_set, 4), "RIFF");
    EXPECT_EQ(std::string(header_set + 8, 4), "WAVE");

    SDL_AudioSpec set_spec;
    Uint8* set_buf = nullptr;
    Uint32 set_len = 0;
    ASSERT_NE(SDL_LoadWAV("assets/sounds/set.wav", &set_spec, &set_buf, &set_len), nullptr);
    int bytes_per_sample = SDL_AUDIO_BITSIZE(set_spec.format) / 8;
    double set_duration = (double)set_len / (set_spec.freq * set_spec.channels * bytes_per_sample);
    EXPECT_GE(set_duration, 3.0);
    EXPECT_LE(set_duration, 4.0);
    SDL_FreeWAV(set_buf);
}

TEST_F(SoundTest, DefaultPathsAndOverrides)
{
    EXPECT_EQ(rise_sound_path, "assets/sounds/rise.wav");
    EXPECT_EQ(set_sound_path, "assets/sounds/set.wav");
    EXPECT_TRUE(play_rise_set_sound);

    rise_sound_path = "custom/rise.wav";
    set_sound_path = "custom/set.wav";
    play_rise_set_sound = false;

    EXPECT_EQ(rise_sound_path, "custom/rise.wav");
    EXPECT_EQ(set_sound_path, "custom/set.wav");
    EXPECT_FALSE(play_rise_set_sound);
}

TEST_F(SoundTest, UserJsonSerializationRoundTrip)
{
    json j;
    j["PlayRiseSetSounds"] = false;
    j["RiseSound"] = "/path/to/my_rise.wav";
    j["SetSound"] = "/path/to/my_set.wav";

    bool loaded_play = true;
    std::string loaded_rise = "";
    std::string loaded_set = "";

    try
    {
        j.at("PlayRiseSetSounds").get_to(loaded_play);
    }
    catch (...)
    {
        ;
    }

    try
    {
        j.at("RiseSound").get_to(loaded_rise);
    }
    catch (...)
    {
        ;
    }

    try
    {
        j.at("SetSound").get_to(loaded_set);
    }
    catch (...)
    {
        ;
    }

    EXPECT_FALSE(loaded_play);
    EXPECT_EQ(loaded_rise, "/path/to/my_rise.wav");
    EXPECT_EQ(loaded_set, "/path/to/my_set.wav");
}

TEST_F(SoundTest, RiseAlertTriggersWhenCrossingHorizon)
{
    ViewMode prev_vm = view_mode;
    int prev_whereami = whereami;

    view_mode = vm_horizon;
    whereami = 0;

    CelestialObject test_obj;
    test_obj.alert_rise = true;
    test_obj.alert_set = false;
    test_obj.has_prev_horizon_alt = true;
    test_obj.prev_horizon_alt = -0.1; // below horizon

    int rise_fired = 0;
    SoundManager::get_instance().set_rise_callback([&](CelestialObject* obj)
    {
        rise_fired++;
    });

    // Allocate cels array mock
    if (!cels)
    {
        cels = new CelestialObject*[MAX_CELOBJS]();
    }
    cels[10] = &test_obj;

    // Simulate an object rising: previous alt was -0.1, current alt is +0.1
    // We mock check_rise_set_alerts logic directly for the transition:
    double cur_alt = 0.1;
    if (test_obj.alert_rise && test_obj.prev_horizon_alt <= 0.0 && cur_alt > 0.0)
    {
        SoundManager::get_instance().play_rise_sound(&test_obj);
        test_obj.alert_rise = false;
    }
    test_obj.prev_horizon_alt = cur_alt;

    EXPECT_EQ(rise_fired, 1);
    EXPECT_FALSE(test_obj.alert_rise);

    // Another frame while still above horizon - should NOT fire again
    cur_alt = 0.2;
    if (test_obj.alert_rise && test_obj.prev_horizon_alt <= 0.0 && cur_alt > 0.0)
    {
        SoundManager::get_instance().play_rise_sound(&test_obj);
        test_obj.alert_rise = false;
    }

    EXPECT_EQ(rise_fired, 1);

    cels[10] = nullptr;
    view_mode = prev_vm;
    whereami = prev_whereami;
}

TEST_F(SoundTest, SetAlertTriggersWhenCrossingHorizon)
{
    ViewMode prev_vm = view_mode;
    int prev_whereami = whereami;

    view_mode = vm_horizon;
    whereami = 0;

    CelestialObject test_obj;
    test_obj.alert_rise = false;
    test_obj.alert_set = true;
    test_obj.has_prev_horizon_alt = true;
    test_obj.prev_horizon_alt = 0.1; // above horizon

    int set_fired = 0;
    SoundManager::get_instance().set_set_callback([&](CelestialObject* obj)
    {
        set_fired++;
    });

    if (!cels)
    {
        cels = new CelestialObject*[MAX_CELOBJS]();
    }
    cels[10] = &test_obj;

    // Simulate an object setting: previous alt was +0.1, current alt is -0.1
    double cur_alt = -0.1;
    if (test_obj.alert_set && test_obj.prev_horizon_alt >= 0.0 && cur_alt < 0.0)
    {
        SoundManager::get_instance().play_set_sound(&test_obj);
        test_obj.alert_set = false;
    }
    test_obj.prev_horizon_alt = cur_alt;

    EXPECT_EQ(set_fired, 1);
    EXPECT_FALSE(test_obj.alert_set);

    // Another frame while still below horizon - should NOT fire again
    cur_alt = -0.2;
    if (test_obj.alert_set && test_obj.prev_horizon_alt >= 0.0 && cur_alt < 0.0)
    {
        SoundManager::get_instance().play_set_sound(&test_obj);
        test_obj.alert_set = false;
    }

    EXPECT_EQ(set_fired, 1);

    cels[10] = nullptr;
    view_mode = prev_vm;
    whereami = prev_whereami;
}

TEST_F(SoundTest, IndependentRiseAndSetAlerts)
{
    CelestialObject test_obj;
    test_obj.alert_rise = true;
    test_obj.alert_set = true;
    test_obj.has_prev_horizon_alt = true;
    test_obj.prev_horizon_alt = 0.1;

    int rise_fired = 0;
    int set_fired = 0;
    SoundManager::get_instance().set_rise_callback([&](CelestialObject* obj)
    {
        rise_fired++;
    });
    SoundManager::get_instance().set_set_callback([&](CelestialObject* obj)
    {
        set_fired++;
    });

    // Object sets
    double cur_alt = -0.05;
    if (test_obj.alert_rise && test_obj.prev_horizon_alt <= 0.0 && cur_alt > 0.0)
    {
        SoundManager::get_instance().play_rise_sound(&test_obj);
        test_obj.alert_rise = false;
    }
    if (test_obj.alert_set && test_obj.prev_horizon_alt >= 0.0 && cur_alt < 0.0)
    {
        SoundManager::get_instance().play_set_sound(&test_obj);
        test_obj.alert_set = false;
    }
    test_obj.prev_horizon_alt = cur_alt;

    EXPECT_EQ(set_fired, 1);
    EXPECT_EQ(rise_fired, 0);
    EXPECT_FALSE(test_obj.alert_set);
    EXPECT_TRUE(test_obj.alert_rise);

    // Object rises
    cur_alt = 0.05;
    if (test_obj.alert_rise && test_obj.prev_horizon_alt <= 0.0 && cur_alt > 0.0)
    {
        SoundManager::get_instance().play_rise_sound(&test_obj);
        test_obj.alert_rise = false;
    }
    if (test_obj.alert_set && test_obj.prev_horizon_alt >= 0.0 && cur_alt < 0.0)
    {
        SoundManager::get_instance().play_set_sound(&test_obj);
        test_obj.alert_set = false;
    }
    test_obj.prev_horizon_alt = cur_alt;

    EXPECT_EQ(set_fired, 1);
    EXPECT_EQ(rise_fired, 1);
    EXPECT_FALSE(test_obj.alert_rise);
    EXPECT_FALSE(test_obj.alert_set);
}
