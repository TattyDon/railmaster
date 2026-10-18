#include "audio.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

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

// Music: an original tune in the manner of the period (a march-time rag in
// C), synthesised as piano over an oom-pah bass and rendered once into a
// loop of about 74 seconds.
struct Note {
    float beat, beats; // start and length, in beats
    int midi;          // -1: a rest
};

// One bar of melody: "E5:.5 G5:.5 C6:1 ...", note names with sharps (#)
// and flats (b) and lengths in beats; R is a rest.
void melody_bar(std::vector<Note>& out, float bar_start, const char* text) {
    static const int kSemitone[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    float at = bar_start;
    for (const char* p = text; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        int midi = -1;
        if (*p == 'R') {
            ++p;
        } else {
            int n = kSemitone[*p - 'A'];
            ++p;
            if (*p == '#') ++n, ++p;
            else if (*p == 'b') --n, ++p;
            const int octave = *p++ - '0';
            midi = 12 * (octave + 1) + n;
        }
        ++p; // ':'
        const float beats = std::strtof(p, nullptr);
        while (*p && *p != ' ') ++p;
        if (midi >= 0) out.push_back({at, beats, midi});
        at += beats;
    }
}

struct Chord {
    int root, fifth;
    std::array<int, 3> triad;
};

std::vector<float> render_music() {
    constexpr float kBpm = 104.0f;
    constexpr float kBeat = 60.0f / kBpm;
    // The bars: melody and the chord under it.
    const Chord C{48, 43, {52, 55, 60}}, F{41, 48, {53, 57, 60}}, G7{43, 50, {53, 55, 59}}, D7{50, 45, {54, 57, 60}},
        A7{45, 52, {55, 57, 61}}, Dm{50, 45, {53, 57, 62}};
    struct Bar {
        const char* tune;
        Chord chord;
    };
    const Bar a[6] = {{"E5:.5 G5:.5 C6:1 B5:.5 A5:.5 G5:1", C}, {"E5:1 D5:.5 E5:.5 C5:2", C},
                      {"F5:.5 A5:.5 C6:1 A5:.5 F5:.5 A5:1", F}, {"G5:1.5 E5:.5 C5:1 E5:1", C},
                      {"D5:.5 F5:.5 G5:1 B5:.5 A5:.5 G5:1", G7}, {"E5:.5 G5:.5 C6:1 G5:1 E5:1", C}};
    const Bar half_close[2] = {{"F#5:.5 A5:.5 D6:1 C6:.5 A5:.5 F#5:1", D7}, {"G5:3 R:1", G7}};
    const Bar full_close[2] = {{"D5:.5 F5:.5 B5:1 A5:.5 F5:.5 D5:1", G7}, {"C5:3 R:1", C}};
    const Bar b[8] = {{"A5:1 C6:.5 A5:.5 F5:1 A5:1", F},       {"G5:.5 A5:.5 C6:1 A5:2", F},
                      {"G5:1 E5:.5 G5:.5 C6:2", C},            {"C#6:.5 B5:.5 A5:1 G5:1 E5:1", A7},
                      {"F5:1 A5:.5 D6:.5 C6:1 A5:1", Dm},       {"G5:1 B5:.5 D6:.5 F6:1 D6:1", G7},
                      {"E6:1.5 D6:.5 C6:1 G5:1", C},            {"F5:.5 E5:.5 D5:1 B4:1 G4:1", G7}};
    std::vector<Bar> bars;
    for (const Bar& x : a) bars.push_back(x);
    bars.insert(bars.end(), std::begin(half_close), std::end(half_close));
    for (const Bar& x : a) bars.push_back(x);
    bars.insert(bars.end(), std::begin(full_close), std::end(full_close));
    for (const Bar& x : b) bars.push_back(x);
    for (const Bar& x : a) bars.push_back(x);
    bars.insert(bars.end(), std::begin(full_close), std::end(full_close));

    std::vector<Note> tune, bass, chords;
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const float start = static_cast<float>(i) * 4;
        melody_bar(tune, start, bars[i].tune);
        // Oom-pah: the root, the chord, the fifth, the chord.
        const Chord& c = bars[i].chord;
        bass.push_back({start, 0.9f, c.root});
        bass.push_back({start + 2, 0.9f, c.fifth});
        for (const float beat : {1.0f, 3.0f})
            for (const int m : c.triad) chords.push_back({start + beat, 0.45f, m});
    }
    const float seconds = static_cast<float>(bars.size()) * 4 * kBeat;
    std::vector<float> out = make(seconds);

    // A struck string: overtones decaying, the higher faster, two strings a
    // hair apart in tune, damped when the key is let go.
    const auto strike = [&](const Note& n, float volume) {
        const float f = 440.0f * std::pow(2.0f, static_cast<float>(n.midi - 69) / 12.0f);
        const float held = n.beats * kBeat;
        const auto first = static_cast<std::size_t>(n.beat * kBeat * kRate);
        const auto length = static_cast<std::size_t>((held + 0.35f) * kRate);
        const float brightness = std::pow(261.6f / f, 0.3f);
        for (int k = 1; k <= 6; ++k) {
            const float amp = volume / std::pow(static_cast<float>(k), 1.4f);
            const float decay = 1.4f * brightness / (1.0f + 0.5f * static_cast<float>(k));
            for (const float detune : {0.9993f, 1.0007f}) {
                const float w = 2 * kPi * f * static_cast<float>(k) * detune / kRate;
                // Turning a phasor is much cheaper than calling sin per sample.
                float re = 1, im = 0;
                const float cw = std::cos(w), sw = std::sin(w);
                for (std::size_t i = 0; i < length && first + i < out.size(); ++i) {
                    const float t = static_cast<float>(i) / kRate;
                    float env = std::exp(-t / decay) * std::min(1.0f, t / 0.004f);
                    if (t > held) env *= std::exp(-(t - held) / 0.07f);
                    out[first + i] += amp * 0.5f * im * env;
                    const float nre = re * cw - im * sw;
                    im = re * sw + im * cw;
                    re = nre;
                }
            }
        }
    };
    for (const Note& n : tune) strike(n, 0.2f);
    for (const Note& n : bass) strike(n, 0.17f);
    for (const Note& n : chords) strike(n, 0.05f);

    // A little room: two feedback echoes, mixed in lightly.
    std::vector<float> wet(out.size(), 0.0f);
    for (const auto& [delay, gain] : {std::pair{0.0297f, 0.4f}, std::pair{0.0411f, 0.37f}}) {
        const auto d = static_cast<std::size_t>(delay * kRate);
        std::vector<float> comb(out.size(), 0.0f);
        for (std::size_t i = 0; i < out.size(); ++i) comb[i] = out[i] + (i >= d ? comb[i - d] * gain : 0.0f);
        for (std::size_t i = 0; i < out.size(); ++i) wet[i] += comb[i] * 0.5f;
    }
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = out[i] * 0.8f + wet[i] * 0.2f;
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
    // The music, made on its own thread; played once it is ready.
    std::vector<float> music;
    std::atomic<bool> music_ready{false};
    std::thread music_maker;
    std::size_t music_pos = 0;
    float music_volume = 0.4f;
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
        float tune = 0;
        if (m.music_ready.load(std::memory_order_acquire) && !m.music.empty() && m.music_volume > 0.001f) {
            tune = m.music[m.music_pos] * m.music_volume;
            if (++m.music_pos >= m.music.size()) m.music_pos = 0;
        }
        // A gentle limit, so many sounds at once do not crack.
        out[2 * f] = std::tanh(l * m.master + tune);
        out[2 * f + 1] = std::tanh(r * m.master + tune);
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
    m->music_maker = std::thread([m] {
        m->music = render_music();
        m->music_ready.store(true, std::memory_order_release);
    });
    SDL_PauseAudioDevice(m->device, 0);
    return true;
}

void shutdown() {
    if (!g_mixer) return;
    SDL_CloseAudioDevice(g_mixer->device);
    if (g_mixer->music_maker.joinable()) g_mixer->music_maker.join();
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

void set_music_volume(float v) {
    if (!g_mixer) return;
    std::lock_guard<std::mutex> guard(g_mixer->lock);
    g_mixer->music_volume = std::clamp(v, 0.0f, 1.0f);
}

std::vector<float> music() { return render_music(); }

} // namespace railmaster::client::audio
