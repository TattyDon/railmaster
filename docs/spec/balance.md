# Balance: the tunable numbers

The spec's rule (rt3-clone-spec.md §0): every community [C] and inferred
[I] number lives in a data file, never in code. Ours is
[`data/balance.json`](../../data/balance.json). Documented [D] rules, such
as the bridge eras or "stock may be issued twice a year", stay in code.

## How it works

- `src/sim/include/railmaster/sim/balance.hpp` declares every number, grouped
  by section, with the shipped default. `data/balance.json` mirrors those
  defaults; a test fails if the two drift apart.
- The client loads the file into `GameData::balance` before anything else
  (cargo prices depend on `economy.cargo_price_unit`). The world hands it to
  the economy, the railway, the company and the investor.
- Loading is strict. A key the simulation does not know is an error, so a
  typo cannot be silently ignored. A wrong `version` is an error too. Keys
  that start with `_` are comments and may appear anywhere.
- A key left out keeps its default, so a calibration file can hold only the
  numbers it changes.
- Code that runs without a loaded file (tests, tools) uses
  `default_balance()`.

## Units

Each key's name carries its unit:

| Suffix | Unit |
|---|---|
| `_mm` | millimetres (1 km = 1,000,000) |
| `_cents` | cents |
| `_permille` / `_per_mille_…` | thousandths |
| `_percent` | hundredths |
| `_bp` | basis points (1% = 100) |
| `_ppm_…` / `_ppb_…` | millionths / billionths |
| `_milli` | thousandths of a carload |
| `_ticks`, `_days`, `_months`, `_years` | simulation time |
| none, on a cost or cash | dollars |

## Sections

| Section | Covers | Described in |
|---|---|---|
| `trains` | movement, dwell, grades, bridge speeds | [m1-provisional-models.md](m1-provisional-models.md) |
| `servicing` | water, sand and oil ranges; service buildings | m1 |
| `breakdowns` | breakdown and crash rates, maintenance ageing | m1 |
| `track` | grade limit, structure clearances, costs per km | m1 |
| `stations` | costs, catchment, gathering, town reach | m1, [m2-economy-model.md](m2-economy-model.md) |
| `economy` | price field, drift, saturation, stockpiles, terrain conductance | m2 |
| `map` | towns and industries placed on a new map | m2 |
| `freight` | timeliness curve, expiry | m2 |
| `express` | passengers, mail and troops | m2 |
| `economic_states` | the business cycle: output, costs, prime rate, share prices | m2 |
| `ai` | rivals' line planning, train buying and share trading | [rivals-model.md](rivals-model.md) |
| `corporate` | takeover and merger votes, retry wait | [rivals-model.md](rivals-model.md) |
| `finance` | starting cash, running costs, credit rating, bonds | [m3-finance-model.md](m3-finance-model.md) |
| `stock` | shares, salary, margin, share price model | m3 |

## Changing a number

1. Edit `data/balance.json`.
2. If the shipped default should change too, edit `balance.hpp` to match;
   the "data/balance.json matches the compiled defaults" test checks this.
3. Say in the matching m1/m2/m3 page what the new value is based on.

The simulation is deterministic for a given balance, seed and command
list. Changing any number changes every game played with it.
