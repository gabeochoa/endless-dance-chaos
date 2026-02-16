#pragma once

#include "rl.h"

#include <cmath>
#include <cstdlib>
#include <vector>

// Procedural audio manager for festival game
// Generates all sounds at runtime -- no external audio files needed
struct AudioManager {
    raylib::Sound sfx_click;
    raylib::Sound sfx_place;
    raylib::Sound sfx_demolish;
    raylib::Sound sfx_toast;
    raylib::Sound sfx_death;
    raylib::Sound sfx_gameover;
    raylib::Music music_beat;

    float master_volume = 0.6f;
    float sfx_volume = 0.5f;
    float music_volume = 0.3f;
    bool music_playing = false;
    bool initialized = false;

    // Generate a sine wave tone
    static raylib::Wave gen_tone(float freq, float duration, float volume,
                                 int sample_rate = 44100) {
        int count = static_cast<int>(sample_rate * duration);
        auto* data = static_cast<float*>(std::malloc(count * sizeof(float)));
        for (int i = 0; i < count; i++) {
            float t = static_cast<float>(i) / sample_rate;
            float env = 1.0f;
            // Quick fade-out in last 20%
            float fade_start = duration * 0.8f;
            if (t > fade_start)
                env = 1.0f - (t - fade_start) / (duration * 0.2f);
            data[i] = std::sin(2.0f * 3.14159265f * freq * t) * volume * env;
        }
        raylib::Wave w = {0};
        w.frameCount = static_cast<unsigned int>(count);
        w.sampleRate = static_cast<unsigned int>(sample_rate);
        w.sampleSize = 32;
        w.channels = 1;
        w.data = data;
        return w;
    }

    // Generate a two-tone chime (ascending)
    static raylib::Wave gen_chime(float freq1, float freq2, float duration,
                                  float volume) {
        int rate = 44100;
        int count = static_cast<int>(rate * duration);
        auto* data = static_cast<float*>(std::malloc(count * sizeof(float)));
        int half = count / 2;
        for (int i = 0; i < count; i++) {
            float t = static_cast<float>(i) / rate;
            float freq = (i < half) ? freq1 : freq2;
            float env = 1.0f - static_cast<float>(i) / count;
            data[i] = std::sin(2.0f * 3.14159265f * freq * t) * volume * env;
        }
        raylib::Wave w = {0};
        w.frameCount = static_cast<unsigned int>(count);
        w.sampleRate = static_cast<unsigned int>(rate);
        w.sampleSize = 32;
        w.channels = 1;
        w.data = data;
        return w;
    }

    // Generate a noise burst (for death/explosion)
    static raylib::Wave gen_noise(float duration, float volume) {
        int rate = 44100;
        int count = static_cast<int>(rate * duration);
        auto* data = static_cast<float*>(std::malloc(count * sizeof(float)));
        for (int i = 0; i < count; i++) {
            float env = 1.0f - static_cast<float>(i) / count;
            float noise =
                (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
            data[i] = noise * volume * env * env;
        }
        raylib::Wave w = {0};
        w.frameCount = static_cast<unsigned int>(count);
        w.sampleRate = static_cast<unsigned int>(rate);
        w.sampleSize = 32;
        w.channels = 1;
        w.data = data;
        return w;
    }

    // Generate a looping EDM-style beat pattern (kick + hi-hat)
    static raylib::Wave gen_beat_loop(float bpm, int bars, float volume) {
        int rate = 44100;
        float beat_sec = 60.0f / bpm;
        float bar_sec = beat_sec * 4.0f;
        float total_sec = bar_sec * bars;
        int count = static_cast<int>(rate * total_sec);
        auto* data = static_cast<float*>(std::malloc(count * sizeof(float)));
        std::memset(data, 0, count * sizeof(float));

        for (int i = 0; i < count; i++) {
            float t = static_cast<float>(i) / rate;
            float beat_pos = std::fmod(t, beat_sec);

            // Kick drum on beats 1 and 3 (every beat_sec * 2)
            float bar_t = std::fmod(t, bar_sec);
            bool is_kick =
                (bar_t < beat_sec * 0.01f) ||
                (bar_t >= beat_sec * 2.0f && bar_t < beat_sec * 2.01f);
            if (beat_pos < 0.08f) {
                float kick_env = 1.0f - beat_pos / 0.08f;
                float kick_freq = 60.0f + 120.0f * kick_env;
                float kick =
                    std::sin(2.0f * 3.14159265f * kick_freq * beat_pos) *
                    kick_env * kick_env;
                if (is_kick || bar_t < 0.01f ||
                    (bar_t >= beat_sec * 2.0f && bar_t < beat_sec * 2.01f)) {
                    data[i] += kick * volume * 0.8f;
                }
            }

            // Hi-hat on every 8th note
            float eighth = beat_sec / 2.0f;
            float eighth_pos = std::fmod(t, eighth);
            if (eighth_pos < 0.02f) {
                float hat_env = 1.0f - eighth_pos / 0.02f;
                float hat_noise =
                    (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
                data[i] += hat_noise * hat_env * volume * 0.15f;
            }

            // Sub bass on beat 1
            if (bar_t < beat_sec) {
                float sub_env = 1.0f - bar_t / beat_sec;
                data[i] += std::sin(2.0f * 3.14159265f * 55.0f * t) * sub_env *
                           volume * 0.4f;
            }

            data[i] = std::clamp(data[i], -1.0f, 1.0f);
        }

        raylib::Wave w = {0};
        w.frameCount = static_cast<unsigned int>(count);
        w.sampleRate = static_cast<unsigned int>(rate);
        w.sampleSize = 32;
        w.channels = 1;
        w.data = data;
        return w;
    }

    void init() {
        if (initialized) return;

        // UI click: short high-pitched tick
        auto w_click = gen_tone(800.0f, 0.04f, 0.3f);
        sfx_click = raylib::LoadSoundFromWave(w_click);
        raylib::UnloadWave(w_click);

        // Place building: satisfying thunk
        auto w_place = gen_tone(300.0f, 0.08f, 0.4f);
        sfx_place = raylib::LoadSoundFromWave(w_place);
        raylib::UnloadWave(w_place);

        // Demolish: descending tone
        auto w_demo = gen_tone(500.0f, 0.12f, 0.3f);
        sfx_demolish = raylib::LoadSoundFromWave(w_demo);
        raylib::UnloadWave(w_demo);

        // Toast notification: ascending chime
        auto w_toast = gen_chime(523.0f, 784.0f, 0.25f, 0.25f);
        sfx_toast = raylib::LoadSoundFromWave(w_toast);
        raylib::UnloadWave(w_toast);

        // Death: noise burst
        auto w_death = gen_noise(0.15f, 0.2f);
        sfx_death = raylib::LoadSoundFromWave(w_death);
        raylib::UnloadWave(w_death);

        // Game over: low descending tone
        auto w_go = gen_tone(200.0f, 0.5f, 0.4f);
        sfx_gameover = raylib::LoadSoundFromWave(w_go);
        raylib::UnloadWave(w_go);

        // Beat loop: 128 BPM, 4 bars
        auto w_beat = gen_beat_loop(128.0f, 4, 0.5f);
        // Save to temp file for LoadMusicStream (needs file path)
        raylib::ExportWave(w_beat, "/tmp/edc_beat.wav");
        raylib::UnloadWave(w_beat);
        music_beat = raylib::LoadMusicStream("/tmp/edc_beat.wav");
        music_beat.looping = true;

        initialized = true;
    }

    void shutdown() {
        if (!initialized) return;
        raylib::UnloadSound(sfx_click);
        raylib::UnloadSound(sfx_place);
        raylib::UnloadSound(sfx_demolish);
        raylib::UnloadSound(sfx_toast);
        raylib::UnloadSound(sfx_death);
        raylib::UnloadSound(sfx_gameover);
        raylib::UnloadMusicStream(music_beat);
        initialized = false;
    }

    void play_click() {
        raylib::SetSoundVolume(sfx_click, sfx_volume * master_volume);
        raylib::PlaySound(sfx_click);
    }

    void play_place() {
        raylib::SetSoundVolume(sfx_place, sfx_volume * master_volume);
        raylib::PlaySound(sfx_place);
    }

    void play_demolish() {
        raylib::SetSoundVolume(sfx_demolish, sfx_volume * master_volume);
        raylib::PlaySound(sfx_demolish);
    }

    void play_toast() {
        raylib::SetSoundVolume(sfx_toast, sfx_volume * master_volume);
        raylib::PlaySound(sfx_toast);
    }

    void play_death() {
        raylib::SetSoundVolume(sfx_death, sfx_volume * master_volume);
        raylib::PlaySound(sfx_death);
    }

    void play_gameover() {
        raylib::SetSoundVolume(sfx_gameover, sfx_volume * master_volume);
        raylib::PlaySound(sfx_gameover);
    }

    void start_music() {
        if (!music_playing) {
            raylib::SetMusicVolume(music_beat, music_volume * master_volume);
            raylib::PlayMusicStream(music_beat);
            music_playing = true;
        }
    }

    void stop_music() {
        if (music_playing) {
            raylib::StopMusicStream(music_beat);
            music_playing = false;
        }
    }

    void update(bool stage_performing) {
        if (stage_performing && !music_playing) {
            start_music();
        } else if (!stage_performing && music_playing) {
            stop_music();
        }
        if (music_playing) {
            raylib::SetMusicVolume(music_beat, music_volume * master_volume);
            raylib::UpdateMusicStream(music_beat);
        }
    }
};

// Global audio manager instance
inline AudioManager& get_audio() {
    static AudioManager instance;
    return instance;
}
