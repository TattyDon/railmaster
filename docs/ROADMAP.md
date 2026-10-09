# Roadmap

Each milestone ends with something playable or testable. Spec pages in
`docs/spec/` must cover a system before it is implemented.

## M0: Foundations (done)
- Deterministic simulation core: calendar, integer money, PCG32 RNG,
  heightfield terrain with exact grades, fixed-step world tick.
- Cargo registry loaded from `data/cargo.json` (41 cargo types).
- Placeholder SDL2/OpenGL client; CI on gcc and clang.

## M1: Track and trains (in progress)
Done so far: track graph; a track planner with straight and curved runs,
grade-limited rail profiles, automatic bridges (wood/stone/steel) and
tunnels, and cost quotes; stations; locomotive data and loader; train
movement with grade and acceleration; looping routes; single-track meets by
priority; service towers and maintenance facilities with water, sand and oil;
breakdowns with a sandbox switch; age-based maintenance cost; a command
layer through which every player action passes; and build tools in the
client (track with preview, branching, stations, support buildings, trains).
Provisional rules are listed in `docs/spec/m1-provisional-models.md`.

Still to do:
- Track removal and upgrades (single to double, electrification), with refunds.
- Charging track costs to a company (needs M3 ledgers).
- Crashes (needs research on what happens), and scheduled service stops in routes.
- Per-stop consists, and automatic consist selection once cargo exists (M2).
- Track rights: trains on another company's track always yield.
- Train management: edit routes, sell trains, per-stop consists.

## M2: The economy
- The economy-node grid and the cargo price field, with off-rail drift of
  cargo along price gradients (`docs/spec/economy-cargo.md` §1).
- Industries and production chains; houses as producers and consumers.
- Delivery revenue from price difference, with time-based decay.
- Express cargo (passengers, mail, troops) with destinations.

## M3: Company and finance
- Company and personal ledgers, income statement, balance sheet.
- Bonds and credit rating, stock issue and buyback, dividends.
- Stock market: trading, margin, short selling, takeovers and mergers.
- Buying, building and upgrading industries; hotels, restaurants and the like.
- Economic cycle states.

## M4: Real 3D client
- Modern OpenGL renderer: terrain, track, trains, buildings, free camera.
- Discrete LOD with up to six variants per model, as the original used
  (e.g. 931 to 55 polygons), so the art spec should ask for LOD chains.
- Game UI: build tools, train manager, station and cargo overlays, finance screens.
- Original art and audio pipeline (`assets/`, credits).

## M5: Opponents and scenarios
- AI chairmen with personality traits.
- Scenario format, event and trigger system, medal conditions.
- Original scenarios built to RT3's design patterns (new maps and text).
- Map editor with heightmap import.

## M6: Multiplayer and polish
- Lockstep multiplayer over the deterministic simulation.
- Save/load, replays, localisation, packaging for Windows/macOS/Linux.

## Research backlog
The values most needed from observing the original game, in rough order:
1. The grade-to-speed formula and locomotive power and weight.
2. The price-field update and diffusion rule; the revenue and decay formula.
3. Station catchment radii; building and track costs.
4. Industry conversion ratios and production rates per upgrade level.
5. Share-price formula, bond-rate table, economic-state transitions.
6. Map dimensions and world scale.
