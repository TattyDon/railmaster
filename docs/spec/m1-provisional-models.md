# M1 provisional models (track and trains)

The research has not established these rules; see the gaps listed in
[trains-track-operations.md](trains-track-operations.md) §7. Each is a
deliberately simple stand-in, kept behind one named constant or function so
it can be replaced once someone measures the real game. **None of these is
a claim about how RT3 works.**

Source of each constant: `src/sim/include/railmaster/sim/railway.hpp`
(namespace `provisional`).

## What is confirmed and implemented as researched
- At most 8 cars per train [M].
- Trains pass each other on single track with no signals. When two trains
  meet, the lower-priority one stops while the other passes [WP✓, SC1].
  Ties go to the train that was bought first (lower id). This tie-break is
  our choice.
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

Source of the track-building constants: `src/sim/include/railmaster/sim/track_builder.hpp`.

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
| Map scale | 1 terrain tile = 1 km by default. | Map dimensions from the original maps. |
| Grade penalty | Uphill: speed factor = 1 − (grade in hundredths of a percent × (2 + cars) × 25 ÷ grade_rating) ÷ 1000, floored at 10% of top speed. Downhill and flat: top speed. For example, a 2% grade with 8 cars halves speed at rating 100. | The RT3 grade-performance tiers and the speed-versus-grade curve. |
| Grade rating | Every engine is 100, because no per-engine data exists yet. | Per-engine values from the in-game "grade performance" display. |
| Acceleration | Reaches top speed in 8 ticks (half a day); braking is instant. | Observation. |
| Station stop | 8 ticks (half a day). | Observation. |
| Curves | No speed penalty. | Unknown whether RT3 has one. |
| Grade limit for building | 3%. Steeper ground is cut, tunnelled or bridged. | RT3's actual limit, if it has one. |
| Tunnel and bridge thresholds | Rail more than 12 m below the ground is a tunnel; more than 10 m above it is a bridge. | Observation. |
| Default tunnel preference | 50 (halfway between climbing over and cutting through). | The default of RT3's "Tunnels" setting. |
| Steel bridges | Available from 1870. | The year steel appears in RT3. |
| Track prices per km of single track | Ground $25K; tunnel $250K; wood bridge $100K; steel $160K; stone $200K. Double track costs 2×. Only the bridge ordering (wood < steel < stone) is researched. | The in-game build cost readout. |
| Piece length | Curves are sampled every 0.5 km or less. This is a modelling choice, not a game rule. | — |
