# M1 provisional models (track and trains)

The research has not established these rules; see the gaps listed in
[trains-track-operations.md](trains-track-operations.md) §7. Each is a
deliberately simple stand-in, kept behind one named number or function so
it can be replaced once someone measures the real game. **None of these is
a claim about how RT3 works.**

The numbers live in [`data/balance.json`](../../data/balance.json)
(sections `trains`, `servicing`, `breakdowns`, `track`, `stations`); see
[balance.md](balance.md).

## What is confirmed and implemented as researched
- At most 8 cars per train [M].
- Trains pass each other on single track with no signals. When two trains
  meet, the lower-priority one stops while the other passes [WP✓, SC1].
  At equal priority the train with more valuable cargo goes first [D,
  rt3-clone-spec §10.3]; then, our choice, the train bought first.
- Double track removes the wait [SC1].
- Routes are ordered stop lists that loop back to the start.
- Station sizes and prices: small $50K, medium $100K, large $200K [SW].
- Track has no grid and runs at any angle [WP✓]. Curves are modelled as
  chains of short straight pieces.
- Bridges come in wood, stone and steel. Wood is cheapest and single track
  only; stone and steel carry double track; steel arrives later and costs
  slightly less than stone [SW].
- Tunnels exist, and a build setting controls how readily the router digs
  them instead of following the surface [SW]. Ours is `tunnel_preference`.

- Two support buildings sit on the track: the service tower (water and
  sand) and the maintenance facility (oil) [M]. A train passing one stops
  by itself if it is low, and stands still while serviced [M, RG].
- A steam engine out of water slows greatly; any engine out of sand loses
  much of its climbing ability; low oil makes breakdowns much more likely [M].
- Reliability is a locomotive's relative chance of breaking down [M].
  Sandbox games have a breakdown switch, off by default [M]; ours is
  `WorldConfig::sandbox` / `OperatingRules::breakdowns`.
- Maintenance is a yearly cost that rises with age and when oil is low
  [EM, possibly RT2]. An earlier community page gave about 3× by year 20;
  we now use the spec's proposal (rt3-clone-spec §9.4 [I]), below.

Crashes destroy the train [D, rt3-clone-spec §9.4]. Scheduled service stops
in a route are still to do.

Track-building numbers are in the `track` section of `data/balance.json`.
The bridge eras (1865, 1895) are documented rules and stay in
`track_builder.hpp`.

### How a run of track is planned
1. The player's drag becomes a series of points: a straight line, or a
   curve (quadratic Bezier) that carries on in the direction the existing
   track was heading.
2. Two rail-height lines are computed, both no steeper than the grade limit:
   *cut*, the highest line that never rises above the ground, and *fill*,
   the lowest that never drops below it. Water constrains neither, so lakes
   are spanned from bank to bank.
3. The rail height is a blend of the two set by `tunnel_preference` (0 =
   always climb over, 100 = always cut through). The ends are fixed to the
   heights of the nodes they join.
4. Each piece is a bridge if it is over water or well above the ground, a
   tunnel if it is well below, and otherwise ordinary track in a cutting or
   on an embankment.
5. The plan reports its cost before anything is built.

This procedure is our design. RT3's own routing algorithm is unknown.

## Provisional

| Item | Stand-in | What would replace it |
|---|---|---|
| Time scale | A train moves as if 40 real seconds pass per simulation tick (16 ticks per game day, so about 10.7 minutes of travel per day). A 60 mph train covers about 17 km a day. | Time a train between two measured points in-game. |
| Map scale | A map cell is 0.5 mile (805 m); maps are Small 256 × 256 cells (the default), Medium 384 × 512 or Large 768 × 1024 (rt3-clone-spec §3.1 [I]). Distances in `balance.json` with `_cells` are in these cells. | Map dimensions from the original maps. |
| Grade penalty | Uphill: speed factor = 1 − (grade in hundredths of a percent × (2 + cars) × 25 ÷ grade_rating) ÷ 1000, floored at 10% of top speed. Downhill and flat: top speed. For example, a 2% grade with 8 cars halves speed at rating 100. | The RT3 grade-performance tiers and the speed-versus-grade curve. |
| Grade rating | Every engine is 100, because no per-engine data exists yet. | Per-engine values from the in-game "grade performance" display. |
| Acceleration | Reaches top speed in 8 ticks (half a day); braking is instant. | Observation. |
| Station stop | 8 ticks (half a day). | Observation. |
| Curves | No speed penalty. | Unknown whether RT3 has one. |
| Grade limit for building | 3%. Steeper ground is cut, tunnelled or bridged. | RT3's actual limit, if it has one. |
| Tunnel and bridge thresholds | Rail more than 12 m below the ground is a tunnel; more than 10 m above it is a bridge. | Observation. |
| Default tunnel preference | 50 (halfway between climbing over and cutting through). | The default of RT3's "Tunnels" setting. |
| Bridge eras | Wood and stone until 1865, steel from 1865, suspension from 1895 [D, rt3-clone-spec §11.2]. Water spans of 2 km or more get suspension bridges. | The span length is ours. |
| Track prices per km of single track | Ground $25K; structures at the spec's multiples [I]: wood bridge 3×, steel 5×, stone 6×, suspension 10×, tunnel 15×. Double track 1.7× (less than 2× [D]). | The in-game build cost readout. |
| Electrification | 75% of the open-ground price per km of track (rt3-clone-spec §11.2 [I]: +75%), × 1.7 on double track, × the economy's cost level. Carried as track. Electric engines need every leg between consecutive stops electrified and route only over electrified track [D]. | In-game readout. |
| Bridge slowdown | Wood bridges 50% of speed, other bridges 90% [D qualitative, I numbers]. | The speed-versus-structure behaviour in-game. |
| Water | A steam tender lasts 150 km. | Observation. |
| Sand | Used only climbing; runs out after 600 m of total climb. | Observation. |
| Oil | Lasts 1,500 km. | Observation. |
| Service threshold | A train stops at a support building when the relevant gauge is below half. | Observation. |
| Service stop | A quarter of a day; at a station it happens during the station stop. | Observation. |
| Out of water | Steam engines drop to 25% of top speed. | "Greatly reduced" [M]. |
| Out of sand | Climbing ability drops to 40%. | "Much of its grade performance" [M]. |
| Breakdown rate | One per 2,000 km on average at reliability 100 with full oil; up to 4× with empty oil; inversely proportional to reliability; × (1 + age/15 years) (rt3-clone-spec §9.4 [I]). Every engine is reliability 100 for now. | Per-engine ratings and observed rates. |
| Breakdown | The train stops where it is for two days. | Observation; the spec suggests 3–10 days and a repair cost [I]. |
| Crash | 0.2% a year at reliability 100, scaled by 100/reliability, by (1 + age/20), and ×3 with no oil (rt3-clone-spec §9.4 [I]). The train is destroyed and written off. | Observation. |
| Maintenance growth | +4% of the new cost per year of age, without limit (2× at 25 years, 3× at 50); 1.5× while out of oil (rt3-clone-spec §9.4 [I]). | [EM] gives about 3× at 20 years, which this undershoots. |
| Support building prices | Service tower $30K; maintenance facility $100K ("relatively expensive" [M]). | In-game prices. |
| Piece length | Curves are sampled every 0.5 km or less. This is a modelling choice, not a game rule. | — |
