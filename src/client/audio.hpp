#pragma once

#include <vector>

namespace railmaster::client::audio {

// Sound, made in code (no recordings): a steam whistle's chord, the chuff
// of a working engine, a station bell, a cash register for income and a
// click for the interface. Mixed in SDL's audio callback; where there is
// no audio device the game carries on silent.

enum class Sound { Whistle, Chuff, Bell, Coin, Click };

// After SDL_Init: open the default output and make the sounds. Returns
// whether sound is on.
bool init();
void shutdown();

// Play a sound once: `volume` 0 to 1, `pan` -1 (left) to 1 (right),
// `pitch` 1 as made (a voice is dropped if too many are playing).
void play(Sound s, float volume = 1.0f, float pan = 0.0f, float pitch = 1.0f);

// The engine nearest the view: chuffs a second (0 for none) and loudness.
void set_engine(float chuffs_per_second, float volume, float pan);

// Master volume, 0 to 1.
void set_master(float v);

// The music's volume, 0 (off) to 1: three original tunes, synthesised,
// played in turn.
void set_music_volume(float v);
// A tune's samples (mono, 44.1 kHz), for tests and tools.
std::vector<float> music(int tune);

// The samples of a sound (mono, 44.1 kHz), as made, for tests and tools.
std::vector<float> samples(Sound s);
constexpr int kRate = 44100;

} // namespace railmaster::client::audio
