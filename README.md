# Railmaster

An open-source reimplementation of the gameplay of *Railroad Tycoon 3*
(2003) and its *Coast to Coast* expansion, written in C++20 with SDL2 and
OpenGL, and shipping entirely original assets.

Railmaster is not affiliated with Take-Two Interactive, 2K or PopTop
Software. See [docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md) for what the project
may and may not contain.

## Status

Early development. Done so far:
- a deterministic simulation core: calendar, money, RNG, heightfield terrain
- data for 41 cargo types, 35 locomotives, and 41 industry types plus houses
- a cargo economy: towns and industries, production chains, and a map-wide
  price field along which cargo drifts, shown as a cargo price overlay
- freight by rail: stations gather cargo, trains load the most valuable,
  and deliveries earn the price difference
- passengers, mail and troops with destinations, paid by distance
- a company with cash, running costs, yearly accounts, credit rating and
  bonds, shown on a finance screen
- a personal account and a stock market: trade shares on margin, issue or
  buy back stock, set dividends, grow your net worth
- rival companies run by AI tycoons with their own personalities: each
  builds and runs its own lines, may run over yours (paying trackage),
  trades and shorts shares, and bids for control; the market screen lists
  every company
- short selling, takeovers and mergers by shareholder vote
- terrain-aware middlemen and a business cycle of five economic states
- free-angle straight and curved track with automatic bridges and tunnels
- stations, and trains that run looping routes, slow on grades and meet on
  single track
- water, sand and oil, service towers, maintenance facilities and breakdowns
- build tools: lay track (with live route and price preview), branch off
  existing track, place stations and service buildings, buy trains
- a placeholder top-down client with a demo network of three towns

Game rules are written up in `docs/spec/` before they are implemented.

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

Run `./build/src/client/railmaster --empty` to start on a blank map instead
of the demo network. Three AI rivals join by default; `--rivals=N` sets how
many (0 to 7).

### Playing

| Key or action | Does |
|---|---|
| F1 to F6, or click the toolbar | Inspect, Track, Station, Service tower, Maintenance facility, Train |
| Left click | Use the tool. Track: first click starts a line, each further click builds to the cursor and carries on from there |
| Right click / Esc | Stop the current line or route; Esc again returns to Inspect |
| D, C, [ ] (Track) | Double track, curves on/off, tunnel preference |
| [ ] (Station) | Station size |
| L, [ ], Backspace, Enter (Train) | Next engine, car count, remove last stop, buy |
| F7 | Finance screen. Then: B / R issue or repay a bond; + / - buy or sell 1,000 shares (Shift: 5,000); I / Y issue or buy back stock; [ ] dividend |
| F8 | Market screen: every company and player. Up / Down choose a company; + / - buy or sell its shares (Shift: 5 blocks; selling below zero sells short); T bid to take it over; M offer to merge it into your company at 20% over market (Shift: 50%) |
| O / P | Cycle the cargo price map (red cheap, green dear) |
| Arrows, mouse wheel | Pan, zoom |
| Space, 1-3 | Pause, game speed |

The bottom bar always shows what the current tool does and its options. While
laying track, the planned route is drawn coloured by structure (grey bridges,
dark tunnels), with its price next to the cursor.

## Layout

| Path | Contents |
|------|----------|
| `src/sim/` | Headless, deterministic game simulation |
| `src/client/` | SDL2/OpenGL front end |
| `tests/` | Unit tests for the simulation |
| `data/` | Game data in original JSON formats; `balance.json` holds every tunable number ([docs/spec/balance.md](docs/spec/balance.md)) |
| `docs/spec/` | Sourced specification of the original game's rules |
| `docs/ROADMAP.md` | Milestones and research backlog |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project works spec first: write
the behaviour down with a source, then implement it, then test it.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
