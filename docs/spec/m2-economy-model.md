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
4. **Stock drifts** a share per day to the neighbouring cell that pays
   best once a transport cost is taken off, if that beats this cell's price.

### Terrain [D behaviour, I numbers]

Middlemen are "slow and costly over land, slower over mountains, cheap
along rivers and coasts" (rt3-clone-spec §5.1 [D]). Each cell gets a
**conductance**, in thousandths of flat land: water 2.0, coast (land next
to water) 1.5, flat 1.0, hills 0.75, mountains 0.5. A cell is hilly when the
relief across it is at least 4% of its width, mountainous at 7%. Where two
cells meet, the edge conducts as their mean. Conductance does three things:

- **Price coupling.** In step 3 each neighbour is weighted by the edge's
  conductance, against a fixed screening term. A consumer's pull therefore
  reaches further along water and dies off faster across mountains, so
  prices are flat along coasts and steep over ranges [D].
- **Middleman cost.** The transport cost in step 4 is divided by the edge's
  conductance: half as much over water, double over mountains.
- **Middleman speed.** The share of stock moved per day is multiplied by it.

On flat land with no water every conductance is 1.0 and the model is
exactly the one above; a test holds it to that.

The stand-in map generator makes gentle hills (at most about 8% relief per
cell), so mountains are rare; the thresholds will want revisiting with real
maps. Rivers are not generated yet. Closed territory borders (conductance 0)
come with territories.

### Economic state [C]

Five states: Depression, Recession, Normal, Prosperity, Boom
(rt3-clone-spec §5.5). Each game starts Normal. On 1 January and 1 July the
state may move one step [C: "checked about twice a year"]: it stays with
50% odds, moves toward Normal 30%, away 20% [I]; from Normal, up or down
25% each; at either end a move away becomes a stay.

| State | Production and demand | Construction, fuel, labour | Prime rate | Share prices |
|---|---|---|---|---|
| Boom | 125% | 115% | 4% | 125% |
| Prosperity | 120% | 108% | 5% | 112% |
| Normal | 100% | 100% | 6% | 100% |
| Recession | 85% | 92% | 7% | 90% |
| Depression | 75% | 85% | 8% | 80% |

- **Production and demand** scale every site's daily rate, so output,
  consumption and the saturation of consumer prices all move together.
- **Costs** apply to track, stations and support buildings when built, and
  to fuel, train maintenance and upkeep each month. Locomotive prices do not
  move.
- The **prime rate** moves the interest on new bonds and on personal margin
  debt (see m3-finance-model.md). Bonds already issued keep their rate.
- **Share prices**: the target price is scaled (see m3).

The client shows the state in the top bar and flashes a message when it
changes, a stand-in for the news ticker. `WorldConfig::business_cycle =
false` holds the economy at Normal. Not yet modelled: automobile demand
dropping sharply in recessions [C], and the optional slow cost index [I].

## Freight by rail

Researched rules [economy-cargo.md §1, §7, §9; trains-track-operations.md §2]:

- A station's catchment grows with its size.
- Cargo only loads if some destination on the train's route wants it; a
  waiting train is not itself demand.
- Automatic car setup loads the cars that will earn the most [WP✓].
- If fewer loads are waiting than the train has cars, it leaves with what is there.
- A train unloads at the first stop whose price beats what the cargo cost,
  even by $1.
- Revenue is the delivery price minus the pickup price [WP✓], and cargo
  loses value in transit, perishables faster; worthless cargo disappears.

How we implement them (code: `freight.hpp`):

1. **Gathering (daily).** A station has one price per cargo: the best in
   its catchment, used both for buying and selling there. Using the cheapest
   cell for buying would let trains earn the price spread inside one town.
   For each station, find each cargo's best price at any other stop of any
   train that calls there. If that beats the station's own price, 20% of the
   cargo's stock in every catchment cell moves into the station's waiting
   pool, at the station's price.
2. **Arrival, unloading.** Each loaded car is sold if the dearest cell in this
   station's catchment pays more than its pickup price. It is never sold
   back at the station it was loaded at; without that rule, price
   differences within one catchment would pay out for nothing. Income =
   (price here − pickup price) × value left. The cargo goes into that cell's
   stock, where the consumer uses it and oversupply lowers the price.
   Cargo whose value has fallen to nothing is dumped.
3. **Arrival, loading.** Empty cars take whole carloads from the waiting
   pool, best expected gain first, but only cargo that some other stop on
   the route would pay more for.

| Item | Value | Notes |
|---|---|---|
| Catchment | Small 3 × 3 cells, medium 5 × 5, large 7 × 7. | RT1's medium station had a 2-tile radius; RT3's are not known. |
| Gathering | 20% of eligible catchment stock per day; up to 20 carloads of each cargo waiting. | |
| Transit decay | Value left = exp(−0.0023 × sensitivity × days) [rt3-clone-spec §8.2, I]: sensitivity 10 loses half in 30 days, 1 loses 5%. Below 10% the load expires. | Computed from an integer table so it is identical on every platform. |
| Revenue modifiers | Difficulty (Easy +20%, Medium 0, Hard −10%, Expert −20%) × station age (+15% for a town's first station when new, 0 at 4 years, −10% from 20 years, half in open country) [D, §8.4]. A station belongs to the nearest town centre within 4 cells. | The 4-cell reach is ours. |
| Waiting at a station | No loss while waiting. | The research says decay is slower at a station than on a train. |

Known simplifications: one consist rule (automatic) for every stop; RT3
also allows manual per-stop consists. Delivery income is tallied but not
yet paid to a company, and running costs are not yet charged (M3), so
profits are overstated for now.

## Passengers, mail and troops (express cargo)

Researched [economy-cargo.md §2, §8; trains-track-operations.md §2]:

- Every express load has a destination [WP✓].
- Loads only appear for places your network connects to.
- Connecting more towns raises passenger demand.
- Each town has a cap on the mail it pays for.
- Waiting passengers give up and mail has a limited shelf life.
- Houses make and receive passengers and mail; barracks make and receive troops.
- No base price is listed for express cargo.

How we implement them (code: `freight.hpp`):

1. **Generation (daily).** For each station, *production* is the summed
   yearly rate of catchment sites that output the cargo (houses: 1 per
   house) × the cargo's `generation` multiplier. For every station sharing a
   train route with it, add production × A ÷ (A + 20) per year to the
   waiting pool for that destination, where A is the destination's
   *attraction*: the summed rate of its catchment sites that take the cargo.
   Each extra destination adds traffic, and a big town draws more than a
   village but not without limit. Only stations a train links are
   destinations; transfers between trains are not modelled.
2. **Waiting.** Each pool loses 0.5% a day per point of decay sensitivity
   (passengers 4.5%, mail 5%) and holds at most 20 loads.
3. **Loading.** Express loads compete with freight for empty cars, valued
   at their fare. Only loads whose destination is on this train's route
   are taken.
4. **Delivery.** Express loads leave the train only at their destination.
   Fare = `fare_per_km` × straight-line distance between the stations ×
   value left after the days in transit, so speed pays. Mail delivered to a
   town past two months of its yearly demand in a month earns nothing
   (`demand_cap`). Loads that go stale on board are dropped.

| Item | Value | Notes |
|---|---|---|
| Fares | Passengers $500, mail $700, troops $400 per load per km. | No RT3 data. Distance-based, as in earlier Railroad Tycoon games. |
| Generation | Passengers ×4, mail ×2, troops ×1 a year per unit of site rate. | |
| Attraction half-point | 20 (for example 20 houses). | |
| Mail cap | Two months of the town's yearly mail demand, per month. | "Each city has a mail demand cap." The size is ours. |

New maps are populated with towns of 10 to 40 houses, about four of each
raw producer type and two of each processor and consumer type per 128 × 128
map. Only industries whose products exist in the start year are placed.
Processors and consumers go near towns. The map then runs a year of
history, so it opens with prices and cargo already in place.

## Provisional constants

In the `economy`, `map`, `freight` and `express` sections of
[`data/balance.json`](../../data/balance.json); see [balance.md](balance.md).

| Item | Value | Notes |
|---|---|---|
| Cargo price unit | Base prices in `cargo.json` are read as thousands of dollars per carload (coal $30K). | Players talk of $5K gains per hop, which suggests thousands. Unverified. |
| Consumer price | 150% of base when unsatisfied. | |
| Producer price | 50% of base. | |
| Neutral price | 50% of base, far from any consumer. | |
| Screening λ | 0.0025. A consumer's pull fades over roughly 10 cells (10 km). | |
| Drift | 5% of a cell's stock per day, when the next cell is at least 1% of base dearer; both scaled by terrain. | |
| Conductance | Water 2.0, coast 1.5, flat 1.0, hills 0.75 (≥ 4% relief), mountains 0.5 (≥ 7%). | Spec §5.3 gives flat 1.0, hills 0.75, mountains 0.5; water's figure and the thresholds are ours. |
| Economic states | See the table above; checked twice a year. | Section `economic_states`. |
| Saturation | Stock equal to 30 days of a town's demand halves its price; for industries, 120 days. | "Industries are hard to oversupply." |
| Spoilage | 0.1% per day × decay sensitivity (1-10). | Sensitivity read as decay rate; unverified. |
| Cell cap | 50 carloads per cargo per cell. | |
| Processor stockpile | 30 days of input. | |
| Booster | +50% output, consuming half a carload per carload made. | |
| Rates | Raw producers 24 carloads/year; processors 36; consumers 24-48; houses 1 per house per good. | RT2 hint: 1 iron + 1 coal → 2 steel. Ours is 1 + 1 → 1. |
| Input rules | Only the steel mill is "all". The rest are "any", including the auto plant and munitions factory. | Only the steel mill's rule is sourced. |
| Bakery | A consumer only: no output is listed for RT3. | Data gap. |
| Rubber | Rubber plantations from 1900 (rt3-clone-spec §6.3); ports, which also supply it, are not modelled yet. | |
| Diesel | Made by refineries; nothing consumes it yet. | Data gap. |
