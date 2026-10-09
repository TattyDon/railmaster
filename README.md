# Railmaster

An open-source reimplementation of the gameplay of *Railroad Tycoon 3*
(2003) and its *Coast to Coast* expansion, written in C++20 with SDL2 and
OpenGL, and shipping entirely original assets.

Railmaster is not affiliated with Take-Two Interactive, 2K or PopTop
Software. See [docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md) for what the project
may and may not contain.

## Status

Early scaffolding. Done so far: a deterministic simulation core (calendar,
money, RNG, heightfield terrain, cargo registry) and a placeholder client
that shows the terrain and advances game time. Game rules are being written
up in `docs/spec/` before they are implemented.

## Building

Dependencies: CMake 3.20+, a C++20 compiler, SDL2, OpenGL, nlohmann-json and
doctest (doctest is fetched automatically if not installed).

On Debian/Ubuntu:

```sh
sudo apt install cmake ninja-build libsdl2-dev libgl-dev nlohmann-json3-dev doctest-dev
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/client/railmaster
```

Client controls: arrows/WASD pan, mouse wheel zoom, Space pause, 1-3 speed.

## Layout

| Path | Contents |
|------|----------|
| `src/sim/` | Headless, deterministic game simulation |
| `src/client/` | SDL2/OpenGL front end |
| `tests/` | Unit tests for the simulation |
| `data/` | Game data in original JSON formats |
| `docs/spec/` | Sourced specification of the original game's rules |
| `docs/ROADMAP.md` | Milestones and research backlog |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project works spec first: write
the behaviour down with a source, then implement it, then test it.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
