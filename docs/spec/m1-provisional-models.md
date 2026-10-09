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
