#include "audio.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace railmaster::client::audio {

namespace {

constexpr float kPi = 3.14159265358979f;

// A small fixed-seed noise source, so the sounds are the same every run.
struct Noise {
    std::uint32_t state = 0x2545F491u;
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / 2147483648.0f - 1.0f;
    }
};

std::vector<float> make(float seconds) { return std::vector<float>(static_cast<std::size_t>(seconds * kRate), 0.0f); }

// A steam whistle: three pipes sounding a chord, each with its overtones,
// rising a little as the steam comes on, with the breath of the steam.
std::vector<float> whistle() {
    std::vector<float> out = make(1.5f);
    const float notes[] = {440.0f, 554.4f, 659.3f}; // A major: a chime whistle
    Noise noise;
    float breath = 0;
    float phases[3] = {0, 0, 0}; // each pipe's phase, advanced by its pitch now
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float attack = std::min(1.0f, t / 0.07f);
        const float release = std::clamp((1.5f - t) / 0.3f, 0.0f, 1.0f);
        const float env = attack * release;
        const float rise = 0.96f + 0.04f * std::min(1.0f, t / 0.12f);
        const float vibrato = 1.0f + 0.004f * std::sin(2 * kPi * 5.5f * t);
        float v = 0;
        for (int p = 0; p < 3; ++p) {
            phases[p] = std::fmod(phases[p] + 2 * kPi * notes[p] * rise * vibrato / kRate, 2 * kPi);
            const float phase = phases[p];
            v += std::sin(phase) + 0.35f * std::sin(2 * phase) + 0.12f * std::sin(3 * phase);
        }
        breath += (noise.next() - breath) * 0.25f; // lowpassed hiss
        out[i] = env * (v * 0.17f + breath * 0.12f);
    }
    return out;
}

// One chuff: a burst of steam (noise, darkened) on a low thump.
std::vector<float> chuff() {
    std::vector<float> out = make(0.22f);
    Noise noise;
    float low = 0, lower = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float env = std::min(1.0f, t / 0.004f) * std::exp(-t / 0.055f);
        const float n = noise.next();
        low += (n - low) * 0.18f;      // under ~1.4 kHz
        lower += (low - lower) * 0.03f; // under ~200 Hz
        const float steam = low - lower; // a band of hiss
        const float thump = std::sin(2 * kPi * 62.0f * t) * std::exp(-t / 0.04f);
        out[i] = env * steam * 1.1f + thump * 0.35f;
    }
    return out;
}

// A bell: a struck tone with a bell's inharmonic partials, ringing away.
std::vector<float> bell(float f, float seconds) {
    std::vector<float> out = make(seconds);
    const std::array<std::pair<float, float>, 4> partials{{{1.0f, 1.0f}, {2.0f, 0.5f}, {2.76f, 0.35f}, {5.4f, 0.15f}}};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        float v = 0;
        for (const auto& [ratio, amp] : partials) v += amp * std::sin(2 * kPi * f * ratio * t) * std::exp(-t * ratio / (seconds * 0.3f));
        out[i] = v * std::min(1.0f, t / 0.002f) * 0.3f;
    }
    return out;
}

// A cash register: the drawer's clatter, then two bright bells.
std::vector<float> coin() {
    std::vector<float> out = make(0.7f);
    const std::vector<float> a = bell(2093.0f, 0.5f), b = bell(2637.0f, 0.55f);
    const auto at = static_cast<std::size_t>(0.07f * kRate);
    for (std::size_t i = 0; i < a.size() && i < out.size(); ++i) out[i] += a[i] * 0.6f;
    for (std::size_t i = 0; i < b.size() && i + at < out.size(); ++i) out[i + at] += b[i] * 0.7f;
    Noise noise;
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.03f * kRate); ++i) {
        const float t = static_cast<float>(i) / kRate;
        out[i] += noise.next() * 0.25f * std::exp(-t / 0.008f);
    }
    return out;
}

// A soft click for buttons.
std::vector<float> click() {
    std::vector<float> out = make(0.03f);
    Noise noise;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        out[i] = (std::sin(2 * kPi * 1400.0f * t) * 0.5f + noise.next() * 0.3f) * std::exp(-t / 0.005f) * 0.5f;
    }
    return out;
}

struct Voice {
    const std::vector<float>* sound = nullptr;
    double pos = 0;
    float step = 1, left = 0, right = 0;
};

struct Mixer {
    std::array<std::vector<float>, 5> sounds;
    std::array<Voice, 24> voices{};
    std::mutex lock;
    float master = 0.7f;
    // The engine: chuffs fire on its own clock.
    float engine_rate = 0, engine_volume = 0, engine_pan = 0;
    double engine_clock = 0;
    int engine_beat = 0;
    SDL_AudioDeviceID device = 0;
};
Mixer* g_mixer = nullptr;

std::pair<float, float> pan_gains(float pan) {
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1) * kPi / 4; // equal power
    return {std::cos(a), std::sin(a)};
}

void start(Mixer& m, Sound s, float volume, float pan, float pitch) {
    for (Voice& v : m.voices) {
        if (v.sound) continue;
        const auto [l, r] = pan_gains(pan);
        v = Voice{&m.sounds[static_cast<std::size_t>(s)], 0, pitch, volume * l, volume * r};
        return;
    }
}

void callback(void* user, Uint8* stream, int bytes) {
    auto& m = *static_cast<Mixer*>(user);
    auto* out = reinterpret_cast<float*>(stream);
    const int frames = bytes / static_cast<int>(2 * sizeof(float));
    std::fill(out, out + 2 * frames, 0.0f);
    std::lock_guard<std::mutex> guard(m.lock);
    for (int f = 0; f < frames; ++f) {
        // The engine's beat: four chuffs a turn of the wheels, the first loudest.
        if (m.engine_rate > 0.1f && m.engine_volume > 0.001f) {
            m.engine_clock += m.engine_rate / kRate;
            if (m.engine_clock >= 1.0) {
                m.engine_clock -= 1.0;
                const float accent = (m.engine_beat++ % 4 == 0) ? 1.0f : 0.75f;
                start(m, Sound::Chuff, m.engine_volume * accent, m.engine_pan, 0.95f + 0.05f * static_cast<float>(m.engine_beat % 3));
            }
        }
        float l = 0, r = 0;
        for (Voice& v : m.voices) {
            if (!v.sound) continue;
            const auto i = static_cast<std::size_t>(v.pos);
            if (i + 1 >= v.sound->size()) {
                v.sound = nullptr;
                continue;
            }
            const auto frac = static_cast<float>(v.pos - static_cast<double>(i));
            const float s = (*v.sound)[i] * (1 - frac) + (*v.sound)[i + 1] * frac;
            l += s * v.left;
            r += s * v.right;
            v.pos += v.step;
        }
        // A gentle limit, so many sounds at once do not crack.
        out[2 * f] = std::tanh(l * m.master);
        out[2 * f + 1] = std::tanh(r * m.master);
    }
}

} // namespace

std::vector<float> samples(Sound s) {
    switch (s) {
    case Sound::Whistle: return whistle();
    case Sound::Chuff: return chuff();
    case Sound::Bell: return bell(880.0f, 1.2f);
    case Sound::Coin: return coin();
    case Sound::Click: return click();
    }
    return {};
}

bool init() {
    if (g_mixer) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    auto* m = new Mixer;
    for (int s = 0; s < 5; ++s) m->sounds[static_cast<std::size_t>(s)] = samples(static_cast<Sound>(s));
    SDL_AudioSpec want{}, have{};
    want.freq = kRate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    want.userdata = m;
    m->device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m->device == 0) {
        delete m;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    g_mixer = m;
    SDL_PauseAudioDevice(m->device, 0);
    return true;
}

void shutdown() {
    if (!g_mixer) return;
    SDL_CloseAudioDevice(g_mixer->device);
    delete g_mixer;
    g_mixer = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void play(Sound s, float volume, float pan, float pitch) {
    if (!g_mixer || volume <= 0.001f) return;
    std::lock_guard<std::mutex> guard(g_mixer->lock);
    start(*g_mixer, s, volume, pan, pitch);
}

void set_engine(float chuffs_per_second, float volume, float pan) {
    if (!g_mixer) return;
    std::lock_guard<std::mutex> guard(g_mixer->lock);
    g_mixer->engine_rate = chuffs_per_second;
    g_mixer->engine_volume = volume;
    g_mixer->engine_pan = pan;
}

void set_master(float v) {
    if (!g_mixer) return;
    std::lock_guard<std::mutex> guard(g_mixer->lock);
    g_mixer->master = std::clamp(v, 0.0f, 1.0f);
}

} // namespace railmaster::client::audio
