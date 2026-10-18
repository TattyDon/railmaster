# Saved games

`World::save()` writes the whole game to bytes, and `World::load(bytes, data)`
reads it back (`src/sim/src/save.cpp`). In the client, Ctrl+S saves to
`railmaster.sav` in the working directory, Ctrl+L loads that file, and
`--load=FILE` starts from a save. The game menu (Esc) saves and loads six
named games, `game1.sav` to `game6.sav`, in the player's own folder
(`SDL_GetPrefPath("Railmaster", "Railmaster")`). Beside every save the
client writes a one-line `.txt` (date, company, net worth) for the menu
to show; the game never reads it back.

## What is in a save

- **Header:** a magic string, a format version, the balance the game was
  played with (as `balance.json` text), and a fingerprint of the game data:
  the keys of every cargo, engine, industry type and tycoon.
- **The simulation:** every member of the World and the parts it holds.
  - The clock and random number generators.
  - Terrain.
  - The economy: price and stock maps, sites, towns, caches.
  - Territories.
  - Track, stations, buildings and trains, including each car's load.
  - Companies, with their books, bonds and share prices.
  - Investors and holdings.
  - Rivals' memory.
  - News waiting to be shown.
  - Undo stacks.
  - The scenario, its result, and its events' state: which have fired,
    counters, effects in force.

Caches are saved too, so a loaded game is bit for bit the game that was
saved. The simulation is deterministic, so it then plays on exactly as the
original would have. A test checks this: it saves a busy game, loads it,
runs both copies 200 more days, and compares the two saves byte for byte.

## Format

The format is binary and little-endian, with fixed-width integers. Each
type lists its members once, in one function, and that one list is used
both to write and to read (`io(Ar&, T&)`, with `Ar` a writer or a reader).
Writing and reading cannot drift apart.

The bytes are then compressed with zlib at its fastest level, behind a
short header: the magic string `RMSAVEZ`, the format version and the size
uncompressed. Loading still reads an uncompressed save. A Small map a year
and a half in saves to about 860 KB (6 MB uncompressed) in about 55 ms, and
loads in about 25 ms.

## Command log and replays

A save also keeps the config the game was made with (`WorldConfig`: seed,
map size, rivals, scenario and the rest) and a log of every command given
from outside the simulation, with the tick it was given at (rt3-clone-spec
§16: "command log (for replay/debug)"). Commands the simulation gives
itself, such as rivals' building and trading, are not logged, because a
replay makes them again. Failed commands are not logged either, as they
change nothing.

`World::replay(save, data)` makes a new world from the config, gives the
logged commands at their ticks, and runs to the tick the save was made at.
The simulation is deterministic, so the result should save to exactly the
same bytes. `railmaster_replay GAME.sav [DATA_DIR]` (built from
`tools/replay/`) does this and says whether they match. It exits 0 on a
match and 1 when the replay differs, which means something in the
simulation is not deterministic. It exits 2 when the save cannot be read.
A test replays a busy game with rivals this way.

Games changed outside commands (tests that edit the terrain directly, for
example) do not replay; the client changes the game only through commands.

## Compatibility

Format versions:
- **1:** first version.
- **2:** scenario events and the Grants ledger line.
- **3:** scenario restrictions, track budgets and company founders.
- **4:** breakdowns awaiting a repair bill.
- **5:** level crossings and overpasses.
- **6:** express loads changing trains.
- **7:** luxury demand and the cost index's start year.
- **8:** warehouse conversion.
- **9:** service stops on train routes.
- **10:** scenario questions waiting for an answer, average-speed samples, train running time.
- **11:** compressed; the game's config and command log.
- **12:** train profits, station income and track traffic by month.
- **13:** each train's accounts, this year and last (the train window's cost window).
- **14:** a hand-made landscape in the game's config.
- **15:** a hand-made map's towns in the game's config.
- **16:** a hand-made map's industries in the game's config.
- **17:** a hand-made map's territories in the game's config.
- **18:** a scenario's hand-made map (`landscape`).
- **19:** express made and lost each month at each station, and travellers avoiding it.
- **20:** whether a cheat from the debug console has been used (an assisted game).

- **Version:** a save of another format version is refused. Bump
  `kSaveVersion` whenever the saved members change. There is no migration
  yet.
- **Balance:** a save keeps the balance it was played with. A changed
  `data/balance.json` does not change a saved game.
- **Game data:** the cargo, engines, industries and tycoons come from
  `data/` when loading, and must match the save's keys. Event effects in
  force are applied to them again. A save made with
  different data is refused with a reason.
- **Damaged files:** a file that is not a save, or is cut short, or has
  data left over, is refused with a reason. Counts are checked against the
  bytes left, so a damaged file cannot ask for a huge allocation.

## Differences from the spec

The spec (§16 [I]) suggests a zip holding the scenario, the full
simulation state, the random state and a command log for replays. Ours
holds the first three, uncompressed. The command log, and replays with
it, are not done yet.
