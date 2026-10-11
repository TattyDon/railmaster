# Saved games

`World::save()` writes the whole game to bytes, and `World::load(bytes, data)`
reads it back (`src/sim/src/save.cpp`). In the client, Ctrl+S saves to
`railmaster.sav` in the working directory, Ctrl+L loads that file, and
`--load=FILE` starts from a save.

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
  - The scenario and its result.

Caches are saved too, so a loaded game is bit for bit the game that was
saved. The simulation is deterministic, so it then plays on exactly as the
original would have. A test checks this: it saves a busy game, loads it,
runs both copies 200 more days, and compares the two saves byte for byte.

## Format

The format is binary and little-endian, with fixed-width integers. Each
type lists its members once, in one function, and that one list is used
both to write and to read (`io(Ar&, T&)`, with `Ar` a writer or a reader).
Writing and reading cannot drift apart.

A Small map saves to about 6 MB in about 15 ms, and loads in under 10 ms.

## Compatibility

- **Version:** a save of another format version is refused. Bump
  `kSaveVersion` whenever the saved members change. There is no migration
  yet.
- **Balance:** a save keeps the balance it was played with. A changed
  `data/balance.json` does not change a saved game.
- **Game data:** the cargo, engines, industries and tycoons come from
  `data/` when loading, and must match the save's keys. A save made with
  different data is refused with a reason.
- **Damaged files:** a file that is not a save, or is cut short, or has
  data left over, is refused with a reason. Counts are checked against the
  bytes left, so a damaged file cannot ask for a huge allocation.

## Differences from the spec

The spec (§16 [I]) suggests a zip holding the scenario, the full
simulation state, the random state and a command log for replays. Ours
holds the first three, uncompressed. The command log, and replays with
it, are not done yet.
