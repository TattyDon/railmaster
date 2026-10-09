# Calibration

`railmaster_calibrate` (built from `tools/calibrate/`) runs a few headless
games and scores twelve metrics against the spec's targets. It exits 0
when every metric is in range and 1 otherwise, so it can gate a balance
change. A run takes about half a minute.

```sh
cmake --build build
./build/tools/calibrate/railmaster_calibrate          # uses data/
./build/tools/calibrate/railmaster_calibrate my-data  # another data directory
```

## Scenarios

| Scenario | Setup | Measures |
|---|---|---|
| Economy alone | Seed 1, two years, no railway | Producer output, processor capacity, producer prices |
| Demo network | The client's opening network (`sim::build_demo_network`), seeds 1 to 6, ten years | Return on capital, revenue, town growth, share price |
| Rivals | Seed 3, three AI tycoons, five years | Share of rivals in profit, rival revenue, station-building income |

## Targets

| Metric | Target | Source |
|---|---|---|
| Raw producer output | 1.5–3.5 loads a year | §6.1 [C]: about 2.2 |
| Processor capacity | 2–5 loads a year | §6.1 [C]: about 3 |
| Median producer price | $240K–$1M | §6.2 [C]: farms sell for $240K–$350K |
| Demo return on capital, years 2–4 | 4–20% a year | [I], below |
| Demo revenue, years 2–4 | $300K–$2M a year | [I], below |
| Growth of served towns, ten years | 25–200% | §6.4 [C]: visible growth over 10–15 years |
| Demo share price after ten years | $15–120 | §12.3 [C]: typically $50–100 |
| Demo price ÷ book value per share | 0.8–2.5 | §12.3 [C]: book value per share is the main anchor |
| Demo company's credit grade, worst seed | A+ to B | §12.4 [C]: a profitable railroad can borrow |
| Rivals in profit in year 5 | 60–100% | §13 [I]: a competent AI |
| Rival revenue, year 5 | $200K–$5M a year | [I], as the demo network |
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
