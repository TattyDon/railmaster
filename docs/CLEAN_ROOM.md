# Clean-room and asset policy

Railmaster is an independent reimplementation of the gameplay of
*Railroad Tycoon 3* (PopTop Software, 2003) and its *Coast to Coast*
expansion. It is not affiliated with or endorsed by Take-Two Interactive,
2K, PopTop Software or Gathering of Developers. "Railroad Tycoon" is a
trademark of its owner and is used here only to describe what this project
is compatible with.

The project ships **entirely original assets**. It does not need, read or
include any files from the original game. These rules keep it that way.

## What may go in the repository

- Code written by contributors.
- Art, models, textures, sounds, music and fonts made by contributors or
  under a GPL-compatible licence (CC0, CC-BY, CC-BY-SA 4.0, OFL for fonts).
  Record the licence and source of every third-party asset in
  `assets/CREDITS.md`.
- **Facts about how the game behaves**: rules, formulas, numbers, years,
  speeds, prices, win conditions. Facts are not copyrightable. Write them
  in your own words in `docs/spec/` with a source for each one.
- Real-world historical facts: locomotive names, specifications and
  service dates, city names and locations, historical figures' names.

## What must never go in the repository

- Any file from the original game, or anything derived from one by
  conversion: textures, models (`.3dp`), sounds, music, maps (`.gmp`), car
  or engine definition files, `.pk4` archives, executables, DLLs.
- Text copied from the game, its manual or its encyclopedia (descriptions,
  scenario briefings, tooltips). Paraphrase the facts; write new prose.
- Screenshots or ripped art used as textures, or used as tracing
  references for art that ships.
- Decompiled or disassembled code, even if rewritten line by line.
- The original logo, box art or trade dress.

## How to research behaviour

1. Prefer observation: play the original (it is sold on Steam and GOG),
   measure what happens, and record the method so others can reproduce it.
2. Manuals, strategy guides, wikis and community forums are fine sources
   for facts. Cite them in the spec and paraphrase.
3. Community documentation of the game's *data file formats* may be read to
   learn what parameters exist and what values they take. Write values into
   `docs/spec/` as facts. Do not write a converter or loader for original files.
4. Do not reverse-engineer the executable. If a behaviour can only be
   determined that way, open an issue tagged `needs-research` describing the
   observed behaviour instead.

## Naming

Use "Railmaster" in the UI, packaging and store listings. Do not put
"Railroad Tycoon" in the window title, executable name or icon.
