# Railmaster

An open-source reimplementation of the gameplay of *Railroad Tycoon 3*
(2003) and its *Coast to Coast* expansion, written in C++20 with SDL2 and
OpenGL, and shipping entirely original assets.

Railmaster is not affiliated with Take-Two Interactive, 2K or PopTop
Software. See [docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md) for what the project
may and may not contain.

## Try it

Railmaster is a desktop game: you build it from source and run it. It takes
a few minutes. The 3D client is in its first stage: lit, textured terrain
with rivers, woods, shadows and distance haze; towns of timber and stone
houses, terraces, shops and churches along streets, and stations to
match; brick mills and works; engines, cars and trees, all from Kenney's
CC0 kits; track, mines, farms and ports drawn in code; smoke from engines and chimneys; and a camera you can turn and tilt
([docs/ROADMAP.md](docs/ROADMAP.md) has what comes next). V switches to
the old flat map.

**1. Get the code.** Until it is merged into `main`, the work is on the
`claude/project-scaffold` branch:

```sh
git clone https://github.com/TattyDon/railmaster.git
cd railmaster
git checkout claude/project-scaffold
```

**2. Install the dependencies and build.**

Linux (Debian/Ubuntu):

```sh
sudo apt install cmake ninja-build g++ libsdl2-dev libgl-dev nlohmann-json3-dev zlib1g-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAILMASTER_BUILD_TESTS=OFF
cmake --build build
```

macOS (with Homebrew and the Xcode command-line tools):

```sh
brew install cmake ninja sdl2 nlohmann-json
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAILMASTER_BUILD_TESTS=OFF
cmake --build build
```

Windows: the easiest way is WSL2 with Ubuntu, following the Linux steps;
on Windows 11 the game window appears on the desktop. A native build with
Visual Studio and vcpkg (`sdl2`, `nlohmann-json`, `zlib`) should also work.

Only the Linux build is tested so far (CI runs gcc and clang on Ubuntu).
If the macOS or Windows steps fail, please open an issue with the error.

**3. Play.** From the `railmaster` folder (the game reads `data/` from
there):

```sh
./build/src/client/railmaster
```

- The game opens with the founding dialog: Enter founds your railroad on
  the usual terms (`--quick` skips the dialog).
- A starter network of three towns, their stations and two trains is
  built for you.
- Every game starts paused: Space starts the clock, + and - change the
  speed.
- F1 to F11 pick the tools (or click the toolbar), and the bottom bar
  always says what the current tool does.
- O and P cycle the map overlays: holdings, profit, grade, traffic and
  cargo prices.
- The bottom-right corner shows which build you are running, as
  `BUILD <commit> <date>`: compare the commit with the latest one on the
  branch (`git log -1 --oneline`, or the branch page on GitHub). A `+`
  after it means the build had local changes. `--version` prints the same.
- Art: any building, engine, car or industry can come from a glTF model
  instead of the shapes drawn in code; see [docs/models.md](docs/models.md)
  for the roles and how to add a pack.
- The game draws with shaders and shadows where the graphics card allows
  (it prints which renderer it uses); `--no-shaders` uses the simpler
  renderer if the picture looks wrong on your machine.
- Mouse wheel zooms, arrow keys pan; Shift+arrows or dragging with the
  right mouse button turn and tilt the view. Ctrl+S saves, Ctrl+L loads.

Other ways to start:
- a scenario with Bronze, Silver and Gold goals:
  `./build/src/client/railmaster --scenario=data/scenarios/lowland_junction.json`
  (F10 shows the goals);
- `--rivals=0` to `--rivals=7` for fewer or more AI companies (3 by
  default);
- `--empty` for a blank map without the starter network.

Every key is listed under [Playing](#playing) below.

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
- owning industries: buy producers and plants, build and upgrade plants,
  and collect their profit
- post offices, hotels, restaurants and taverns by stations, sharing each
  station's passenger trade
- industries that slow down when their output piles up unsold
- towns that grow with good service (star ratings by each name), ports on
  the coast that import and export, warehouses you build, and new industries
  appearing over the years
- investor sentiment: bad years make shareholders grumble and then vote
  the chairman out (you included, unless you hold a majority); stock splits
- terrain-aware middlemen and a business cycle of five economic states
- free-angle straight and curved track with automatic bridges and tunnels
- stations, and trains that run looping routes and meet on single track,
  slowed by weight, grades, curves and bridges as each engine's figures
  allow, with cars that grow heavier by era
- water, sand and oil, service towers, maintenance facilities and breakdowns
- build tools: lay track (with live route and price preview), branch off
  existing track, place stations and service buildings, buy trains
- scenarios: JSON files that set the map, towns and rivals, with Bronze,
  Silver and Gold goals and deadlines (three original ones in
  `data/scenarios/`)
- saved games that load and play on exactly as the original
- map overlays (holdings, profit, grade, traffic, cargo prices) and lists
  of trains, stations and companies
- compressed saves with a command log that replays a game exactly
- a first 3D client (lit terrain, trees, houses, industries, track and
  trains as simple shapes, chimney smoke; turn, tilt and zoom) with a demo network of
  three towns

Game rules are written up in `docs/spec/` before they are implemented.

## Building

Dependencies: CMake 3.20+, a C++20 compiler, SDL2, OpenGL, nlohmann-json,
zlib (for compressed saves) and doctest (doctest is fetched automatically if
not installed).

On Debian/Ubuntu:

```sh
sudo apt install cmake ninja-build libsdl2-dev libgl-dev nlohmann-json3-dev zlib1g-dev doctest-dev
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/client/railmaster
```

Run `./build/src/client/railmaster --empty` to start on a blank map instead
of the demo network. A new game opens with the founding dialog: choose how
much of your fortune to invest and how much outside money to take (Enter
founds the company; `--quick` skips it with the usual terms). Three AI
rivals join by default; `--rivals=N` sets how many (0 to 7). `--map=small`,
`medium` or `large` picks the map size (256 × 256, 384 × 512 or 768 × 1024
cells of half a mile; Small by default). `--territories=N` splits the map
into N territories: the first town's is free, the others need access rights
before you can build there (T in Inspect buys them).
`--scenario=data/scenarios/lowland_junction.json` plays a scenario: it sets
the map, rivals and goals itself, and F10 shows the briefing and the medal
goals ([docs/spec/scenarios.md](docs/spec/scenarios.md)).

`./build/tools/replay/railmaster_replay railmaster.sav` replays a saved game
from its start using the command log in the save, and checks it ends
exactly as saved ([docs/spec/saves.md](docs/spec/saves.md)).

`./build/tools/calibrate/railmaster_calibrate` plays a few headless games
and checks the economy against the spec's targets
([docs/spec/calibration.md](docs/spec/calibration.md)).

### Playing

| Key or action | Does |
|---|---|
| F1 to F11, or click the toolbar | Inspect (LOOK), Track, Station, Service tower, Maintenance facility (SHED), Train, Finance (BOOKS), Market, Industry, Status, Lists |
| Left click | Use the tool. Track: first click starts a line, each further click builds to the cursor and carries on from there |
| Right click / Esc | Stop the current line or route; Esc again returns to Inspect |
| Train tool | Click stations in order (Backspace drops the last); L chooses the engine. Up/Down selects a stop, and the keys then set what the train takes on there: F kind of cargo (Any, Freight, Express), W wait for a full load, [ ] number of cars, K custom cars ([ ] then removes or adds a car of the cargo chosen with < >). S makes the selected stop a service stop (the train fills up there with the station's service buildings even when not low). Enter buys the train |
| Over a train (Inspect) | E edit its route and per-stop consists in the Train tool (keys as there, Enter saves); H raise its priority (Shift+H lower); F cargo filter (Any, Freight, Express); W wait for full loads; [ ] maximum cars; C caboose; D dining car; Y copy the train; X twice to retire it; R re-engine with the engine chosen in the Train tool (L there). The consist keys (F, W, [ ]) apply to every stop |
| D, C (Track) | Double track, curves on/off |
| T, B, G (Track) | Tunnel, bridge and overpass frequency: Never, Rarely, Average, Common (Shift steps back). Overpasses at Average go over double track; level crossings make trains take turns |
| 1 to 9 | Answer a scenario event's question (the game pauses for it) |
| T (Inspect) | Buy access rights to the territory under the cursor; the Inspect line shows its price and modifiers |
| Ctrl+Z (Track) | Undo the last run of track, at full refund, until you leave the Track tool |
| U (Track) | Double the run of single track under the cursor, at 90% of laying it again (dearer than building double at once) |
| X twice (Track) | Remove the run of your track under the cursor (out to the next junction, station or support building, shown in red): 10% of its value back, the rest written off. Trains whose route ran over it find another way |
| E (Track) | Electrify the piece of your track under the cursor; Shift+E all your track. Electric engines need electrified track between every pair of stops |
| [ ] (Station) | Station size; B cycles to post office, hotel, restaurant and tavern, built by clicking near any station |
| L, [ ], Backspace, Enter (Train) | Next engine, car count, remove last stop, buy. The second line shows the engine's speed chart (speed with that many freight or express cars on the flat and up 1%, 2% and 3%) and its passenger appeal |
| F7 | Finance screen. Then: B / R issue or repay a bond; A / S buy or sell 1,000 shares (Shift: 5,000); I / Y issue or buy back stock; [ ] dividend; K twice to declare bankruptcy |
| F9 | Industry tool: hover an industry for its owner, price and profit; click to buy it, click one of your plants to upgrade it, click open land to build the plant or warehouse chosen with [ ]; M changes what your warehouse or port does (exports, imports or both); C sets a warehouse to convert one cargo into another, at a quarter yield. Ports can be bought too |
| F8 | Market screen: every company and player. Up / Down choose a company; A / S buy or sell its shares (Shift: 5 blocks; selling below zero sells short); T bid to take it over; M offer to merge it into your company at 20% over market (Shift: 50%); Q twice to resign as chairman; N found a new company if you run none |
| F10 | Status screen: the scenario's briefing, each medal's deadline and goals with progress, and the medal won |
| Ctrl+S, Ctrl+L | Save the game to `railmaster.sav`, load it back (`--load=FILE` starts from a save) |
| F11 | Lists of trains, stations and companies (rt3-clone-spec §15.4): Tab switches list, Up/Down and PgUp/PgDn choose, Enter goes to the train or station (or the company's market entry) |
| PgUp / PgDn (Inspect) | Go to your next or previous train |
| O / P | Cycle the map overlays (rt3-clone-spec §15.3): holdings (every company's track, stations and industries in its colour), profitability (trains and stations green, yellow or red over the last 12 months), track grade (green level to red at the limit), traffic (brighter the busier; red where single track held trains at meets, so it needs a second track), then each cargo's price map (red cheap, green dear). Hovering shows the figures |
| Arrows, mouse wheel, mouse at the window edge | Pan, zoom |
| Shift+arrows, right-drag | Turn the view (left/right) and tilt it (up/down); a right click without dragging still stops the current line or route |
| V | Switch between the 3D view and the flat top-down map |
| Shift+0 to 9, 0 to 9 | Store the view as a bookmark; go back to it |
| Radar (bottom left) | The whole map, with towns, stations, track and where you are looking; click it to go there |
| + / -, Pause (or Space) | Game speed: Paused, Very Slow, Slow, Normal, Fast, Very Fast. Every game starts paused; Normal is about four minutes a game year |

The bottom bar always shows what the current tool does and its options. While
laying track, the planned route is drawn coloured by structure (grey bridges,
dark tunnels), with its price next to the cursor.

## Layout

| Path | Contents |
|------|----------|
| `src/sim/` | Headless, deterministic game simulation |
| `src/client/` | SDL2/OpenGL front end |
| `tests/` | Unit tests for the simulation |
| `tools/calibrate/` | Headless balance check against the spec's targets |
| `tools/replay/` | Replays a saved game from its command log and checks it ends as saved |
| `data/` | Game data in original JSON formats; `balance.json` holds every tunable number ([docs/spec/balance.md](docs/spec/balance.md)) |
| `docs/spec/` | Sourced specification of the original game's rules |
| `docs/ROADMAP.md` | Milestones and research backlog |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project works spec first: write
the behaviour down with a source, then implement it, then test it.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

Art in `data/models/` is by Kenney (www.kenney.nl), CC0; see each pack's
`License.txt` and [docs/models.md](docs/models.md). The model and image
loaders in `third_party/` are cgltf (MIT) and stb_image (public domain);
see [third_party/README.md](third_party/README.md).
