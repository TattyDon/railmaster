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

- The game opens on the title screen, over a demo railway running on its
  own: **New railroad** (choose the map size, rival railroads, territories,
  difficulty, the landscape (generated, or one of the hand-made maps in
  `data/maps`) and the seed for towns and industries, then Start),
  **Tutorial** (a short guided first railroad: moving the view, track
  between two towns, a station at each end, a train, the clock and the
  books; each step passes when you have done it, Shift+Tab skips one), **Scenarios** (each with its briefing),
  **Load a saved game**, or **Quit**. Up/Down and Enter, or click.
- A new railroad or scenario opens with the founding dialog: Enter founds
  your railroad on the usual terms.
- `--quick` skips the title screen and the dialog and starts on the demo
  network: three towns, their stations and two trains, built for you.
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
- Busy stations look it: people wait on the platforms (one for every quarter load of passengers), with mail sacks and freight crates stacked at the ends.
- The game draws with shaders, shadows and ambient occlusion (woods with dark floors, buildings grounded in soft shade) where the graphics card allows
  (it prints which renderer it uses); `--no-shaders` uses the simpler
  renderer if the picture looks wrong on your machine.
- Zoom with the mouse wheel, a two-finger scroll or pinch on a trackpad,
  or Option (Alt) + Up/Down; arrow keys pan; Shift+arrows or dragging with the
  right mouse button turn and tilt the view. Ctrl+S saves, Ctrl+L loads.

Map editor (title screen, Map editor): start from flat land, generated
hills or any map, then shape it with brushes: 1 Raise, 2 Lower, 3 Smooth,
4 Flatten (to the height where the stroke began), 5 Water, 6 Land, with
`[` `]` for the brush size and left-drag to apply; and 7 Town: click land
to found a town (it gets a name; type to rename it, `[` `]` make it a
village up to a city, Delete removes it, Enter is done), or click a town
to pick it; and 8 Industry: `,` and `.` choose which (coal mine, steel
mill, port, ...), a click on dry land places one, a click on one removes
it (one whose product does not exist yet waits idle until it does); and
9 Territory: `,` and `.` choose territory 1 to 8 (or none, to erase),
left-drag paints it with the brush, Enter renames it, O makes it free or
priced again (the first is free; the rest get a price and modifiers as a
generated map's do). G opens the scenario page, in two tabs (Tab switches). Scenario and
rules: a name and briefing (Enter to type them), the start year, rival
railroads, founding money (lean, usual or rich), a track budget, and
whether new track must join your own, only one company may be run, and
the chairman may neither resign nor be fired. Medals: for Bronze, Silver
and Gold a goal and, if you like, a second one (connect the map's first
towns, book value, revenue, cash, net worth, average speed, the only
railroad, the highest value, industry profit, or stations in so many of
the painted territories), each with its target, and the deadline. Events: up to eight, each from a template (a headline you type, a
cargo's price or an industry's output rising or falling for a while,
building costs changing, a grant to your company, an earthquake near a
town, an engine arriving early) on a month and year; Delete removes one. Save the scenario, or save
it and play it at once; saved scenarios (in a `scenarios/` folder beside
your maps) are listed with the others on the title screen, and a
scenario file names its map with `"landscape": "../maps/NAME.png"`. A map
with towns of its own has those towns and no others, and likewise its
industries and territories (with none placed, the usual random ones and
ports go down); both are kept beside its image as a `.json` file, and a
map from your own folder is saved back over itself; the arrows, wheel and
right-drag move the view as in the game. S saves it as a map in your own
folder (`maps/` beside your saved games), where the title screen finds
it; P starts a new railroad on it at once; Esc leaves (asking first if it
is not saved).

Hand-made maps: any PNG in `data/maps/` (or saved by the editor) appears
under Landscape on the title screen. Brightness is height (black low, white the highest hills),
and blue, or near-black, is water; the image is stretched over the chosen
map size, and towns and industries are placed on its dry land. Draw one in
any image editor, or see `tools/maps/make_maps.py`, which drew the three
that come with the game (Two Rivers, Archipelago, Mountain Pass).
`--landscape=IMAGE` uses one from the command line.

Other ways to start, straight from the command line (any of these skips
the title screen):
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
- industries on yards of 4 × 4 cells, fenced and cleared on the map: a station reaching any part of the yard reaches the industry
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
| F1 to F11 and L, or click the toolbar | Inspect (Look), Track, Station, Service tower, Maintenance facility (Shed), Train, Company, Market, Industry, Status, Lists, Ledger |
| Left click | Use the tool. Track: first click starts a line, each further click builds to the cursor and carries on from there |
| Right click / Esc | Stop the current line or route; Esc again returns to Inspect (and in Inspect opens the game menu) |
| Train tool | Click stations in order (Backspace drops the last); L chooses the engine. Up/Down selects a stop, and the keys then set what the train takes on there: F kind of cargo (Any, Freight, Express), W wait for a full load, [ ] number of cars, K custom cars ([ ] then removes or adds a car of the cargo chosen with < >). S makes the selected stop a service stop (the train fills up there with the station's service buildings even when not low). Enter buys the train |
| Left click (Inspect) | Open the detail panel (rt3-clone-spec §15.4) of the train, station or industry clicked, at the right of the screen; Esc or a click on open ground closes it. A train's shows its status, engine, age, reliability and breakdowns, gauges and a breakdown-risk meter (against a new engine of average reliability), route with each stop's consist, cars, and a cost window: this year's and last year's freight and passenger earnings, maintenance, fuel, repairs and profit (Space makes the view follow it); a station's its size and reach (and the price to enlarge it), town, trains calling, income, and for each cargo what waits there, what its catchment makes and wants a year and the best price within reach, and the buildings by it; an industry's its owner or price, level, output, inputs, local prices and profit |
| Over a train, or with its panel open (Inspect) | E edit its route and per-stop consists in the Train tool (keys as there, Enter saves); H raise its priority (Shift+H lower); F cargo filter (Any, Freight, Express); W wait for full loads; [ ] maximum cars; C caboose; D dining car; Y copy the train; X twice to retire it; R re-engine with the engine chosen in the Train tool (L there). The consist keys (F, W, [ ]) apply to every stop |
| D, C (Track) | Double track, curves on/off |
| T, B, G (Track) | Tunnel, bridge and overpass frequency: Never, Rarely, Average, Common (Shift steps back). Overpasses at Average go over double track; level crossings make trains take turns |
| 1 to 9 | Answer a scenario event's question (the game pauses for it) |
| U (Inspect, a station's panel open) | Enlarge your station a size (Small to Medium to Large) for the difference in their prices, at today's costs and the territory's; the panel shows the price |
| Enter (Inspect, your station's panel open) | Rename the station: type in the panel's title (up to 30 characters, not another station's name), Enter keeps it, Esc cancels |
| T (Inspect) | Buy access rights to the territory under the cursor; the Inspect line shows its price and modifiers |
| Ctrl+Z (Track) | Undo the last run of track, at full refund, until you leave the Track tool |
| U (Track) | Double the run of single track under the cursor, at 90% of laying it again (dearer than building double at once) |
| X twice (Track) | Remove the run of your track under the cursor (out to the next junction, station or support building, shown in red): 10% of its value back, the rest written off. Trains whose route ran over it find another way |
| X twice (Station, Tower, Shed) | Take down your station, service tower or shed under the cursor (ringed in red): 10% of its value back. A station can't go while a train still calls there; change its route first (E) |
| E (Track) | Electrify the piece of your track under the cursor; Shift+E all your track. Electric engines need electrified track between every pair of stops |
| [ ] (Station) | Station size (the circle on the ground shows the area it would serve, the label by the cursor the name it will take and its price, or why it can't go there, and the bottom bar what the houses and industries in it make a year and want). Stations are named after the town they serve (then its East, West, North or South side, then Junction), or out of town after the nearest town's Junction, Crossing or Siding; B cycles to post office, hotel, restaurant and tavern, built by clicking near any station |
| L, [ ], Backspace, Enter (Train) | Next engine, car count, remove last stop, buy. The second line shows the engine's speed chart (speed with that many freight or express cars on the flat and up 1%, 2% and 3%) and its passenger appeal |
| F7 | Company Detail (rt3-clone-spec §15.4), in four tabs (Tab or click to switch): Overview (this year's and last year's income statement, investment, investors, salary, average speed), Bonds (debt, credit rating, prime rate, each bond), Stock (price, shares, market value, dividend, your holding) and Territories (each one's access price and modifiers; Up/Down choose, T buys access). On every tab: B / R issue or repay a bond; A / S buy or sell 1,000 shares (Shift: 5,000); I / Y issue or buy back stock; [ ] dividend; K twice to declare bankruptcy |
| L | Ledger (rt3-clone-spec §12.7), in nine tabs: Overview (revenue, profit, loads delivered, revenue a load, average speed, investors), Income statement (up to ten years side by side), Balance sheets (every company's, side by side), Haulage (loads delivered by cargo), Players (cash, shares, net worth, company run), and lists of your Trains (this year's freight and passenger earnings, costs and profit, best first), Stations (waiting loads, trains calling, income), Industries (level, output, profit, price paid) and the Cargo (since when, base price, waiting at your stations, delivered). In the lists Up/Down choose and Enter opens the train, station or industry on the map, or shows the cargo's price map. It opens by itself, paused, when a year ends |
| F9 | Industry tool: hover an industry for its owner, price and profit; click to buy it, click one of your plants to upgrade it, click open land to build the plant or warehouse chosen with [ ]; M changes what your warehouse or port does (exports, imports or both); C sets a warehouse to convert one cargo into another, at a quarter yield. Ports can be bought too |
| F8 | Market screen: every company and player, and a chart of the chosen company's share price month by month over the last ten years (rt3-clone-spec §15.4). Up / Down choose a company; A / S buy or sell its shares (Shift: 5 blocks; selling below zero sells short); T bid to take it over; M offer to merge it into your company at 20% over market (Shift: 50%); Q twice to resign as chairman; N found a new company if you run none |
| F10 | Status screen: the scenario's briefing, each medal's deadline and goals with progress, and the medal won |
| Esc (Look tool, nothing open) | The game menu (rt3-clone-spec §15.4 File Options), pausing the game: Resume, Save game, Load game, Briefing and goals, Settings (sound volume, scrolling at the window's edge, time of day, frames a second; kept between games), Main menu (back to the title screen) and Quit (both ask first). Saving and loading offer six named games, kept in your own folder (on a Mac `~/Library/Application Support/Railmaster/Railmaster/`), with the quick save; each slot shows the date, company and net worth it holds. Number keys pick a slot. The game also saves itself at every new year, to an Autosave slot listed when loading (and on the title screen) |
| Ctrl+S, Ctrl+L | Quick save to `railmaster.sav` in the working directory, and load it back (`--load=FILE` starts from any save) |
| F11 | Lists of trains, stations and companies (rt3-clone-spec §15.4): Tab switches list, Up/Down and PgUp/PgDn choose, Enter goes to the train or station and opens its detail panel (or the company's market entry) |
| M | Message history (rt3-clone-spec §15.4): every piece of news, newest first, with its month (Up/Down and PgUp/PgDn scroll; M or Esc closes). Not in the Market and Industry tools, where M has its own use |
| Newspaper | Big news makes the front page of the Railroad Gazette, and the game waits while you read it: scenario news, earthquakes and sabotage, bankruptcies, chairmen voted out, rivals founding companies, the economy turning, medals. Esc, Enter, Space or a click closes it (and the game carries on); Shift+N brings the last one back |
| Tab | Hide the interface, leaving only the railway and the town names (Tab again shows it; the stations' signs, with their name, owner's colour and loads of passengers, mail and freight waiting, hide with the rest); questions, the menu and the newspaper still show. In the Company, Ledger and Lists screens Tab turns their pages |
| Ctrl+H | Hide the trees, to see the track and towns (rt3-clone-spec §15.7); again to show them |
| . (with Debug console on in Settings) | The debug console (rt3-clone-spec §15.8): 1 to 4 give $1M or $10M to you or your company; 5 access to every territory; 6 every engine available; 7 twice the production; 8 and 9 switch twice the train speed and no crashes on or off; K and C break down or crash the train whose panel is open; B, S and G win the scenario with that medal, L loses it. Using one marks the game as assisted, for good: F10 and the save slots say so |
| Mouse in lists | In the F11 lists, the Ledger's lists, the Market, the Territories tab and Find, a click chooses a row and a click on the chosen row opens it (as Enter); the wheel over a list scrolls it. The debug console's rows are buttons |
| ? (or the ? Help button) | A help card with the main keys, grouped: building, running the railroad, getting around, time and view. Hovering over a toolbar button says what that tool does |
| Ctrl+G (or the Guide button) | The Guide, in two tabs. Opportunities: the best-paying lines near you, ranked: passengers and mail between towns 5 to 30 miles apart (from both towns' houses, at the fare for the distance; a fair floor: a first line between two towns earned about twice its estimate in testing) and freight links (cargo, where it is made, where it sells for more, miles, extra per load, loads a year and a year's worth; works count only while they are fed, and links you already serve come last); Enter or a second click goes to the source, opens its panel and marks the line on the map in gold; then B (in Look) shows what building it would cost (track, a medium station at each end unless one of yours is already there, a service tower unless one of yours is near, and the fastest engine you can afford with 4 cars) and B again builds it all. Esc lets the plan go. Who wants what: each cargo, what makes it and who pays for it (towns, works, ports) |
| Earnings | When one of your trains is paid at a station, the amount (+$12,345) rises from it in green and fades |
| Trains in trouble | A "!" badge floats over each of your trains with a problem: pulsing red when it has run out of water or has no route, amber when broken down or out of sand or oil. Its panel says what is wrong and what to do |
| Water | Steam engines need water and sand from a service tower (F4) on their line; without one they crawl at a fraction of their speed. The Train tool warns before you buy a steam train whose stations have no tower near, and a message says which train has run dry |
| Ctrl+F | Find a town or station (rt3-clone-spec §15.7 Find city): type part of its name, Up/Down choose, Enter goes there (and opens a station's panel) |
| PgUp / PgDn (Inspect) | Go to your next or previous train |
| O / P | Cycle the map overlays (rt3-clone-spec §15.3): holdings (every company's track, stations and industries in its colour), profitability (trains and stations green, yellow or red over the last 12 months), track grade (green level to red at the limit), traffic (brighter the busier; red where single track held trains at meets, so it needs a second track), then each cargo's price map (red cheap, green dear). Hovering shows the figures |
| Arrows, mouse at the window edge | Pan |
| Mouse wheel, two-finger scroll, pinch, Option (Alt) + Up/Down | Zoom in and out |
| Shift+arrows, right-drag | Turn the view (left/right) and tilt it (up/down); a right click without dragging still stops the current line or route |
| V | Switch between the 3D view and the flat top-down map |
| Sound | Made in code, no recordings: a steam whistle's chord as trains near the view leave a station (diesels and electrics a deeper horn), a bell as they arrive, the engine nearest the middle of the view chuffing at its speed, a cash register when your trains are paid, and clicks. Closer views are louder; the menu's Settings set the volume. Music: three original tunes in the manner of the period (a march-time rag, a waltz and a slow folk air), synthesised as piano and played in turn, quietly; Settings set its volume (Off to 100%) |
| N | Time of day: day, dusk, night, dawn, or a slow round of all four while the game runs. After dark, stars twinkle and the moon rises over the horizon, fixed in the sky as the view turns |
| Clouds and weather | Clouds' shadows drift over the land with the wind; the sky clears and clouds over again every few minutes, and when it is thickest it rains (with its sound) or, in winter, snows, under a duller light. The menu's Settings turn it off |
| Water | Rivers, lakes and the sea ripple in the wind (even while paused), mirror the sky at low angles and glint in the sun; a lake lies at one level inside earth banks, and rivers step down their rapids |
| Seasons | The land follows the calendar: from mid September the broadleaf trees turn gold, orange and red (conifers stay green) and the grass fades; from late November snow lies on open ground, on the trees and on roofs into March, and in midwinter the water ices over. Track, roads, roofs and water keep their colours. The menu's Settings turn it off |
| F12 | Show frames a second and the time a frame takes (bottom right) |
| `--look=X,Y` | Start with the view close over map tile (X, Y) |
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
see [third_party/README.md](third_party/README.md). The interface font is
Source Sans 3 by Adobe, under the SIL Open Font License
(`data/fonts/OFL-SourceSans3.md`).
