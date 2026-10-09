# M2 economy model

How Railmaster's cargo economy works, and which parts are researched facts
and which are our design. Code: `src/sim/include/railmaster/sim/economy.hpp`,
data: `data/industries.json`.

## Researched and implemented

From [economy-cargo.md](economy-cargo.md):

- Carloads drift slowly across the map along the gradient of a scalar
  price field. Supply sites are sources and demand sites are sinks [WP✓].
- So raw materials can reach industries and be processed with no trains
  [WP✓], and moving cargo over land is slow, which is what railroads fix.
- About 15,000 economy nodes are spread evenly across a typical map. Ours
  is one node per terrain tile: 16,384 on a 128 × 128 map.
- Oversupply pushes a consumer's local price down. Industries are much
  harder to oversupply than towns.
- The price map reshapes slowly, over about a year.
- A two-input steel mill produces nothing unless it gets both inputs [M].
- Who produces and who consumes each of the 41 cargo types, including
  year-limited demand (houses stop burning coal and oil after 1910, Tool
  and Die stops taking iron after 1877, Toy Factory stops taking lumber
  after 1956).
- The cargo overview map: red where a cargo is cheap, green where it is dear.

## Our design (the formulas are not published)

Each game day:

1. **Sites act.** Every industry and town cell produces into, or consumes
   from, the stock of its own cell.
   - Raw producers (mines, farms, wells) make their output from nothing.
     Some have boosters (fertilizer for crop farms, corn for dairy farms
     and cattle ranches) which, when supplied, raise output.
   - Processors stockpile inputs up to a limit and convert them, up to
     their capacity. "All" processors need every input; "any" processors
     need at least one.
   - Consumers and houses take what they need each day.
2. **Stock spoils** a little, faster for perishable cargo.
3. **The price field relaxes one step.** Consumers pin their cell's price
   high, falling as unconsumed stock piles up there. Producers pin their
   cell's price low. Every other cell becomes a screened average of its four
   neighbours: (mean of neighbours + λ × neutral) ÷ (1 + λ). A consumer's
   pull therefore fades with distance, so far from any buyer a cargo sits
   at the neutral price. This is a screened Poisson equation, solved one
   Jacobi step per day, which is also why the map reshapes slowly.
4. **Stock drifts** a fixed share per day to the neighbouring cell with the
   highest price, if that price beats this cell's by more than a transport
   cost.

Express cargo (passengers, mail, troops) is not priced by the field; each
load has a destination. That comes in a later slice.

New maps are populated with towns of 10 to 40 houses, about four of each
raw producer type and two of each processor and consumer type per 128 × 128
map. Only industries whose products exist in the start year are placed.
Processors and consumers go near towns. The map then runs a year of
history, so it opens with prices and cargo already in place.

## Provisional constants

All in the `provisional` namespace of `economy.hpp` and `cargo.hpp`.

| Item | Value | Notes |
|---|---|---|
| Cargo price unit | Base prices in `cargo.json` are read as thousands of dollars per carload (coal $30K). | Players talk of $5K gains per hop, which suggests thousands. Unverified. |
| Consumer price | 150% of base when unsatisfied. | |
| Producer price | 50% of base. | |
| Neutral price | 50% of base, far from any consumer. | |
| Screening λ | 0.0025. A consumer's pull fades over roughly 10 cells (10 km). | |
| Drift | 5% of a cell's stock per day, when the next cell is at least 1% of base dearer. | |
| Saturation | Stock equal to 30 days of a town's demand halves its price; for industries, 120 days. | "Industries are hard to oversupply." |
| Spoilage | 0.1% per day × decay sensitivity (1-10). | Sensitivity read as decay rate; unverified. |
| Cell cap | 50 carloads per cargo per cell. | |
| Processor stockpile | 30 days of input. | |
| Booster | +50% output, consuming half a carload per carload made. | |
| Rates | Raw producers 24 carloads/year; processors 36; consumers 24-48; houses 1 per house per good. | RT2 hint: 1 iron + 1 coal → 2 steel. Ours is 1 + 1 → 1. |
| Input rules | Only the steel mill is "all". The rest are "any", including the auto plant and munitions factory. | Only the steel mill's rule is sourced. |
| Bakery | A consumer only: no output is listed for RT3. | Data gap. |
| Rubber | No producer: the research says ports supply it, and ports are not modelled yet. | Data gap. |
| Diesel | Neither produced nor consumed. | Data gap. |
