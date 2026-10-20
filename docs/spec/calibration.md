# Calibration

`railmaster_calibrate` (built from `tools/calibrate/`) runs a few headless
games and scores twenty metrics against the spec's targets. It exits 0
when every metric is in range and 1 otherwise, so it can gate a balance
change. A run takes about five minutes.

```sh
cmake --build build
./build/tools/calibrate/railmaster_calibrate          # uses data/
./build/tools/calibrate/railmaster_calibrate my-data  # another data directory
```

## Scenarios

| Scenario | Setup | Measures |
|---|---|---|
| Economy alone | Seed 1, two years, no railway | Producer output, processor capacity, producer prices |
| Price field | A strip of 48 × 9 economy nodes (2 × 2 cells each), flat or mountainous; a coal mine and a power plant 30 cells apart, a mine alone, or a new plant | Middleman speed, spread with no demand, how fast the price map reshapes |
| Demo network | The client's opening network (`sim::build_demo_network`), seeds 1 to 6, ten years | Return on capital, revenue, town growth, share price |
| Later eras | The demo network started in 1900 and in 1950, seeds 1 to 6, four years | Return on capital; that it can be built on every map |
| Rivals | Seeds 1 to 6, three AI tycoons each, five years | Share of rivals in profit, rival revenue, station-building income |

## Targets

| Metric | Target | Source |
|---|---|---|
| Raw producer output | 1.5–3.5 loads a year | §6.1 [C]: about 2.2 |
| Processor capacity | 2–5 loads a year | §6.1 [C]: about 3 |
| Median producer price | $240K–$1M | §6.2 [C]: farms sell for $240K–$350K |
| Middlemen, flat land | 4–16 cells a year | §5.3 [C]: about 8 |
| Middlemen, mountains | 2–8 cells a year | §5.3 [C]: about 4 |
| Spread with no demand, 3 years (90% within) | 0–8 cells | §5.3 [C]: "a few cells" |
| Price map reshapes (63% of the change, 10 cells from a new plant) | 0.5–2.5 years | §5.3 [C]: 1–2 years |
| Demo return on capital, years 2–4 | 4–20% a year | [I], below |
| Demo revenue, years 2–4 | $300K–$2M a year | [I], below |
| Growth of served towns, ten years | 25–200% | §6.4 [C]: visible growth over 10–15 years |
| Demo share price after ten years | $15–120 | §12.3 [C]: typically $50–100 |
| Demo price ÷ book value per share | 0.8–2.5 | §12.3 [C]: book value per share is the main anchor |
| Demo company's credit grade, worst seed | A+ to B | §12.4 [C]: a profitable railroad can borrow |
| Demo return from 1900, years 2–4 | 4–20% a year | [I], as from 1830 |
| Demo return from 1950, years 2–4 | 4–20% a year | [I], as from 1830 |
| Maps the demo network can be built on, all eras | 100% | it must not silently skip a map |
| Rivals in profit in year 5 | 60–100% | §13 [I]: a competent AI |
| Rival revenue, year 5 | $200K–$5M a year | [I], as the demo network |
| Rival revenue from freight, year 5 | 10–80% | §13 [I]: the planner weighs city pairs and industry–consumer pairs alike |
| Station-building income | $0–$20K a year each | §7.2 [C]: about $1K |

The [I] targets are derived from scenario goals: the spec's scenarios ask
for $10–40M of net worth over 25–30 years from a $1–3M start. That is
compound growth of about 4% ($3M to $10M in 30 years) to 16% ($1M to $40M
in 25) a year, so the return target is 4–20% a year, with some headroom.
On a starter network of a few million dollars, revenue should be in the
hundreds of thousands to low millions. (The first version of this page
read the goals as 10–40% a year, which was too generous.)

## Before and after

The first run, with rates of 24 loads a year for raw producers, found 6 of
the 10 metrics out of range. Volumes were about eleven times the spec's.

| Metric | Before | After |
|---|---|---|
| Raw producer output | 28.4 loads/yr | 2.6 |
| Median producer price | $6.5M | $313K |
| Demo return on capital | 126%/yr | 17% |
| Demo revenue | $6.3M/yr | $1.13M |
| Rival revenue | $7.7M/yr | $1.59M |
| Served town growth | — | 48% |
| Demo share price | — | $26 |
| Station-building income | — | $441/yr |

With the spec's share-price and credit-rating models (the following
change), the demo share price rose to $31, at 1.2 × book value, and the
demo company rated A+. All 12 metrics are in range.

With the spec's maintenance ageing and 2% track upkeep (was 6%), the demo
network's return rose to 20.6% a year and its share price to $42.50.

With the spec's map scale (0.5-mile cells, the Small 256 × 256 map, nodes
of 2 × 2 cells, catchment 2/3/4 cells), the demo's return fell to 8.4% a
year over six maps. Most of the fall comes from the bigger map: it has
about 20 towns instead of 8, at the same density, so the demo's nearest
towns are closer and its hops shorter. One node per cell instead of 2 × 2
gave about 9%. Two maps were too few to tell this from luck (one seed alone
ranged from −4% to 17%), so the demo now uses six. Served towns grow 29% in
ten years, near the 25% floor.

With the spec's price field (equilibrium prices from demand and supply,
months-long relaxation, proportional middlemen), the middleman and
reshaping metrics were added. Middlemen first delivered nothing: their
small daily shares rounded away in thousandths of a carload, which is why
stock is now kept in millionths. Dividing the middleman's cost by
conductance as well as scaling the flow left mountains at a quarter of the
flat speed, so the cost is now the same everywhere, as in the spec. Site
prices relaxing over 30 days reshaped the map in 0.7 years; the spec's six
months gives 1.2. Results: middlemen 7.2 cells a year on flat land and 2.8
in mountains, 90% of an unsold mine's coal within 8 cells after three
years (the top of its band), the map reshaped in 1.2 years. The demo and
rivals barely moved (return 8.3%), as their revenue is mostly passengers;
the calibration game's three rivals carry no freight at all.

Rivals then valued town pairs at ten times their calibrated passenger
rate, so they built no freight lines. With the express code's own rates in
the estimate, 27% of rival revenue in year 5 is freight; total rival revenue
is about the same ($947K a year).

With the spec's speed model (era car weights, free weight, grade × load,
curves, first-order acceleration, fuel by weight), the demo's return rose
from 8.6% to 10.1% a year and its revenue stayed about the same ($501K). Its
1830s trains are light, so they burn less fuel than the old flat
per-car rate. Rival freight share went from 18.5% to 22%.

With the spec's express fares (distance^0.9, a speed factor against the
era's reference speed, engine appeal) and following on single track, the
first reference speeds (12 mph in 1830) were below what the 1830s trains
achieve: the average trip earned 1.14 × the plain fare. Raised by 15%,
the average is 0.99 and the demo's revenue is $512K (return 10.5%).

The rival metrics now pool six maps. On seed 3 alone, rivals' freight share
fell to 8% (below its band), while the six maps gave 8% to 56%. That is
too wide for one map's three rivals to measure. Pooled, it is 34%, and
rival revenue is $1.2M a year each.

With breakdowns of 3–10 days that cost 5% of the engine to repair (was 2
days, free), the demo's return fell from 10.5% to 9.9% and its share price
from $28 to $26. Rivals are unchanged at Medium difficulty.

With level crossings, rival revenue rose slightly ($1.25M a year each). Then
with changes of trains, it rose to $2.0M, and its freight share fell to
21%. Rivals run many lines from the same towns, so one change opens many
new destinations. The spec expects this: volume rises with reachable
destinations. The demo network, a single triangle, barely moved (return
10.3%).

With rivers on the map, every generated map changed. The demo's revenue
rose to $614K a year and its return settled at 9.5%. The rivals were
about the same ($2.1M, 33% freight).

With later eras measured, three problems showed. The demo network took the
first engine of the year, so it ran a slow Fairlie in 1900 and an electric
engine (on unelectrified track) in 1950. It now takes the fastest steam or
diesel engine hauling four cars up 1%, as rivals do. It could not pay for
its track on half the maps since rivers and tunnels came in, and the tool
skipped those maps silently. It now chooses, among each town and its two
nearest, the triangle with the most houses whose track and territory
access cost at most 60% of its cash (`demo.track_budget_percent`), and
builds each line by the cheapest of its tunnel and bridge settings. A map it cannot
build on now counts against it. Finally, the returns fell with the era:
about 13% from 1830, 10% from 1900 and 4% from 1950. Faster trains run
more kilometres for the same passengers, so they burn more fuel and broke
down far more often: once per 2,000 km, six times a year for a 1950
express. Breakdowns are now once per 6,400 km at reliability 100, a third
of RT2's Near Perfect figure [RL]. The spec's era factor on passenger and
mail spawning (§8.3), absent until now, rises from 100% in 1830 to 135% in
1950. The demo now returns 12.5% from 1830, 15.4% from 1900 and 14.8% from
1950; its other metrics and the rivals' are as before.

With the bridge, tunnel and overpass frequency settings, the demo network
tries tunnels and bridges each at Never, Rarely and Average and builds the
cheapest; its return is 12.6% from 1830, 15.9% from 1900 and 15.5% from
1950. Rivals build at Average, which plans track exactly as before.

With industry footprints of 4 × 4 cells (stations reach an industry
anywhere on its yard, and yards keep apart), every generated map changed.
All 20 metrics stayed in range: the demo returned 11.6% a year on $530K of
revenue, with served towns growing 27%.

With travellers sent by gravity (each station's new loads shared among
its destinations by pull ÷ distance^e, the total unchanged), the demo
network's revenue fell, as more of them take short, cheap trips:

| Setting | Demo return | Demo revenue | Served towns grow | Out of range |
|---|---|---|---|---|
| none (before) | 11.6% | $530K | 27% | 0 |
| distance², from 8 km (the spec's proposal) | 7.5% | $392K | 22% | 1 (growth) |
| distance², from 20 km | 8.6% | $421K | 22% | 1 (growth) |
| distance¹, from 8 km | 9.6% | $462K | 24% | 1 (growth) |
| distance^1.5, from 12 km (chosen) | 9.0% | $438K | 27% | 0 |

From 1900 and 1950 the demo returns 9.3% and 9.0%; rivals are about the
same ($1.67M a year, 36% freight).

## What changed

- **Rates** in `data/industries.json` follow the spec: raw producers 2.2
  loads a year (uranium 1.1), processors 3, electric plants 4.4, other
  consumers 2.2, ports 6, warehouses 3, houses 0.1 each. Rates may now be
  fractional; they are held in thousandths.
- **Saturation** was measured in days of output, so with a tenth of the
  volume the windows grew to match: consumers 365 days (was 30), industries
  730 (was 120), unsold supply 180 (was 60).
- **Express:** the attraction half-point fell from 20 to 2 (in units of
  house rate, so still about 20 houses). Waiting loads lose 0.1% a day per
  point of decay sensitivity (about 0.9% for passengers), half the on-train
  rate [C]. Express cars leave half full or better (`express.min_load_milli`,
  [C] load fraction 0.5–1.0), as low volumes would otherwise never fill a
  car.
- **Town growth** coefficients rose from 1 to 3 per thousand houses served,
  so served towns grow visibly with the lower passenger volumes.

## Re-running

Change `data/balance.json` or `data/industries.json`, run the tool, and
record the new table here when the defaults change.
