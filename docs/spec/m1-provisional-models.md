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

## Train speed (rt3-clone-spec §10.2)

A train's target speed is its engine's top speed times five factors, each
in thousandths:

- **Load:** 1 − 0.5 × (weight over the engine's free weight) ÷ the
  reference weight, at least 20%.
- **Grade**, uphill only: 1 − grade % × the engine's drop per 1% × weight
  ÷ the reference weight, at least 5%. With no sand, the drop is tripled.
  An engine with no cars climbs at full speed, as the engine's own weight
  is built into its figures.
- **Curve:** the turn's radius ÷ 4 km, at least 25%.
- **Bridge:** wood 50%, other bridges 90%.
- **Water:** 25% for a steam engine with an empty tender.

How the factors are worked out:
- **Curve radius.** Curves are chains of short straight pieces, so the
  radius is read from the turn where two pieces meet: about piece length ÷
  turn angle. A turn of 90° or more gets the floor.
- **Weight.** Weight counts only what is pulled: loaded cars, caboose and
  dining car. Empty cars stay behind.
- **Car weights** follow the era, as documented [D]: a loaded freight car
  is 10 t to 1849, 20 t to 1899, 40 t to 1949 and 80 t from 1950. Express
  cars, cabooses and dining cars weigh 2/3 as much. The weight goes by
  today's date, so a train that ran well in 1899 slows in 1900.
- **Reference weight.** It is four freight cars of the era, the speed
  chart's default load [I].

Speed approaches the target as a first-order lag, in both directions. Each
tick closes 1/T of the gap, where T is the engine's time constant. It rises
quickly at first and then levels off [C], reaching about 63% of the gap
after one time constant.

Each engine has five performance figures, all ours [I], in
`data/locomotives.json`. They were set from the spec's role guidance
(§9.2): the Consolidation as the early freight workhorse, the Shay for
hills, Big Boy and Challenger for heavy freight, and fast single-drivers
that falter with load.
- Time constant, `accel_days`: steam 0.4–0.8 days, diesel about 0.3,
  electric 0.12–0.3.
- Drop per 1% grade, `grade_drop`: 0.04 for the Shay up to 0.35 for the
  Planet.
- Free weight, `free_weight_tons`: 15 t up to 480 t.
- Fuel economy, `fuel_economy`.
- Own weight, `weight_tons`, used for fuel.

The Train tool shows the chosen engine's chart: its speed with the chosen
number of freight or express cars on the flat and up 1%, 2% and 3%.

Rivals pick the engine that is fastest with their usual train up 1%,
rather than the one with the highest top speed.

Following (rt3-clone-spec §10.3 [I]): on single track there is no
overtaking. A train looks 2 km ahead along its path for one going the same
way. If it finds one, it holds that train's speed and comes no closer than
0.5 km. A train ghosting through a meet blocks nobody. Without this rule a
queue behind a yielding train could deadlock against the train it yields
to. On double track trains overtake freely [D].

Each engine also has a passenger appeal, from −15% to +30% of passenger
and mail fares [D range]. The values are ours: handsome, streamlined
express engines score high (Mallard +30%, the E-88 +30%, the Zephyr +25%),
and plain freight engines score low (the Shay −15%).

## Provisional

| Item | Stand-in | What would replace it |
|---|---|---|
| Time scale | A train moves as if 40 real seconds pass per simulation tick (16 ticks per game day, so about 10.7 minutes of travel per day). A 60 mph train covers about 17 km a day. | Time a train between two measured points in-game. |
| Map scale | A map cell is 0.5 mile (805 m); maps are Small 256 × 256 cells (the default), Medium 384 × 512 or Large 768 × 1024 (rt3-clone-spec §3.1 [I]). Distances in `balance.json` with `_cells` are in these cells. | Map dimensions from the original maps. |
| Speed model | rt3-clone-spec §10.2 [I], below. | The Add Train speed-vs-grade chart for 5–6 engines (§17 item 4). |
| Station stop | 8 ticks (half a day). | Observation. |
| Grade limit for building | 3%. Steeper ground is cut, tunnelled or bridged. | RT3's actual limit, if it has one. |
| Tunnel and bridge thresholds | Rail more than 12 m below the ground is a tunnel; more than 10 m above it is a bridge. | Observation. |
| Default tunnel preference | 50 (halfway between climbing over and cutting through). | The default of RT3's "Tunnels" setting. |
| Bridge eras | Wood and stone until 1865, steel from 1865, suspension from 1895 [D, rt3-clone-spec §11.2]. Water spans of 2 km or more get suspension bridges. | The span length is ours. |
| Track prices per km of single track | Ground $25K; structures at the spec's multiples [I]: wood bridge 3×, steel 5×, stone 6×, suspension 10×, tunnel 15×. Double track 1.7× (less than 2× [D]). | The in-game build cost readout. |
| Removing track | Bulldozing your own track returns 10% of its book value; the rest is written off [D: "little cash, but a book-value write-off"; I: 10%]. Each piece carries its own cost: its building cost, shared by length when the piece is split, plus any wiring. A run is removed at a time in the client, out to the nearest junction, station, support building or change of owner. Not under a train. Trains whose way ahead ran over removed track find another way at the end of the piece they are on. | Observation. |
| Undo | The last run built can be undone at full refund until the player leaves the Track tool [D], newest first, up to 64 deep. Refused once a station or support building stands on a node the build created, a train is on it, or later track has split it. The demo network cannot be undone. | — |
| Electrification | 75% of the open-ground price per km of track (rt3-clone-spec §11.2 [I]: +75%), × 1.7 on double track, × the economy's cost level. Carried as track. Electric engines need every leg between consecutive stops electrified and route only over electrified track [D]. | In-game readout. |
| Bridge slowdown | Wood bridges 50% of speed, other bridges 90% [D qualitative, I numbers]. | The speed-versus-structure behaviour in-game. |
| Water | A steam tender lasts 150 km. | Observation. |
| Sand | Used only climbing; runs out after 600 m of total climb. | Observation. |
| Oil | Lasts 1,500 km. | Observation. |
| Service threshold | A train stops at a support building when the relevant gauge is below half. | Observation. |
| Service stop | A quarter of a day; at a station it happens during the station stop. | Observation. |
| Manual service stop | A route stop the player marks as a service stop (rt3-clone-spec §9.3 `serviceStop`, §9.4 [D]: "service stops can also be added manually") fills water, sand and oil from the service buildings at that station's node whatever the gauges read, and adds a quarter of a day to the stop. Buildings elsewhere are not visited [I]. | How RT3's route editor offers them. |
| Changing a route | Stops at stations the old route called at keep their consist; new stops take the old first stop's [I]. A train heading for, or standing at, a station still on the route carries on; otherwise it makes for the first stop from the end of the piece it is on (or leaves for it when its stop is done). A route needs two stops and cannot call at one station twice running; an electric engine's route must be wired throughout [D]. | — |
| Out of water | Steam engines drop to 25% of top speed. | "Greatly reduced" [M]. |
| Out of sand | Three times the speed lost on grades (rt3-clone-spec §9.4 [I]). | "Much of its grade performance" [M]. |
| Breakdown rate | One per 6,400 km on average at reliability 100 with full oil (RT2's Near Perfect engines run about 24,000 cells, 19,300 km, between breakdowns [RL]; ours would be reliability 300 [I]); up to 4× with empty oil; inversely proportional to reliability; × (1 + age/15 years) (rt3-clone-spec §9.4 [I]). Every engine is reliability 100 for now. | Per-engine ratings and observed rates. |
| Breakdown | The train stops where it is for 3 to 10 days, drawn evenly, and its owner pays 5% of the engine's price for repairs, booked as train maintenance (rt3-clone-spec §9.4 [I]). | Observation. |
| Crossings | A new line that crosses existing track without joining it meets it at a level crossing (rt3-clone-spec §11.2 [D]: crossing lines stop each other). Crossings are found when the line is planned, including where pieces of both lines end at the same point. Bridges and tunnels already pass at another level, so they make none. At a level crossing a train stops 0.3 km short while a train on the other line is within 0.3 km of it, or has passed it within its last tick's run. When two arrive together the higher-ranked goes first, as at meets. A train ghosting through a meet blocks nobody. With overpasses on (G in the Track tool), each piece that crosses costs 4× plain track [I] and trains on both lines run on. | Observation. |
| Double-track upgrade | Doubling existing single track costs 90% of laying it again as single track (§11.2 [D]: dearer than building double at once, +70% [I]: +90%). Not on wooden bridges [D]. The piece's book value rises by the cost. The upgrade counts against a scenario's track budget once. | In-game readout. |
| Crash | 0.2% a year at reliability 100, scaled by 100/reliability, by (1 + age/20), and ×3 with no oil (rt3-clone-spec §9.4 [I]). The train is destroyed and written off. | Observation. |
| Maintenance growth | +4% of the new cost per year of age, without limit (2× at 25 years, 3× at 50); 1.5× while out of oil (rt3-clone-spec §9.4 [I]). | [EM] gives about 3× at 20 years, which this undershoots. |
| Support building prices | Service tower $30K; maintenance facility $100K ("relatively expensive" [M]). | In-game prices. |
| Piece length | Curves are sampled every 0.5 km or less. This is a modelling choice, not a game rule. | — |
