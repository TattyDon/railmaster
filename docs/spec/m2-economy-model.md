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
  groups map cells into square nodes, picked per map so there are about
  15,000 (rt3-clone-spec §5.2 [I]): 2 × 2 cells on the Small 256 × 256 map
  (16,384 nodes), 4 × 4 on Medium 384 × 512, 7 × 7 on Large 768 × 1024.
  Sites, towns, prices and stock live on nodes. Each node is water if most
  of its cells are, coast if any are, and takes its relief across all its
  cells.
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
| Catchment | Radius 2, 3 and 4 map cells (1, 1.5 and 2 miles) for small, medium and large (rt3-clone-spec §7.1 [I]): the nodes whose centres lie within that square. On the Small map that is 2 × 2 to 3 × 3 nodes for a small station and 4 × 4 to 5 × 5 for a large one. | RT1's medium station had a 2-tile radius; RT3's are not known. |
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
   yearly rate of catchment sites that output the cargo (houses: 0.1 per
   house) × the cargo's `generation` multiplier. For every station sharing a
   train route with it, add production × A ÷ (A + 2) per year to the
   waiting pool for that destination, where A is the destination's
   *attraction*: the summed rate of its catchment sites that take the cargo.
   Each extra destination adds traffic, and a big town draws more than a
   village but not without limit. Only stations a train links are
   destinations; transfers between trains are not modelled.
2. **Waiting.** Each pool loses 0.1% a day per point of decay sensitivity
   (passengers 0.9%, mail 1%), half the on-train rate [C], and holds at most
   20 loads.
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
| Attraction half-point | 2 (for example 20 houses). | |
| Part loads | An express car leaves at half a load or more. | [C] load fraction 0.5–1.0. |
| Mail cap | Two months of the town's yearly mail demand, per month. | "Each city has a mail demand cap." The size is ours. |

New maps are populated with towns of 10 to 40 houses, about four of each
raw producer type and two of each processor and consumer type per 128 km ×
128 km of map, scaled by area (the Small map is 2.6 times that, so it has
about 20 towns). Maps under 128 km get the full numbers. Towns are at
least 15 cells apart and spread 3 cells either side of their centre;
plants and consumers go within 8 cells of a town. Only industries whose products exist in the start year are placed.
Processors and consumers go near towns. The map then runs a year of
history, so it opens with prices and cargo already in place.

## Price-responsive output (rt3-clone-spec §6.1 [C])

Researched: a factory sells at local prices, and slows or stops when its
output price nearby is red (cheap) [C].

Our price field used to pin a producer's cell at 50% of base however much
unsold output piled up, so it could never turn red. Now supply mirrors
demand [I]:

- **Supply price:** a producer's price falls as unsold stock builds up in
  its cell: 50% of base × S / (S + stock), where S is 180 days' output. A
  consumer's price already falls the same way with unconsumed stock.
- **Pace:** producers and processing plants run at full pace while their
  best product sells for at least 35% of base there. Below that they slow
  in proportion, stopping at 10%. A plant that slows uses fewer inputs.
- **In practice:** middlemen carry output off as it is made, so a producer
  normally holds about a carload and runs at full pace. It slows only when
  stock piles up: nobody hauls it and the nearby buyers are saturated. It
  recovers as soon as a railway takes the stock away. On the default map
  no producer was slowed after three years.
- **Side effect:** pickup prices at busy producers are a little lower than
  before, so freight margins are slightly higher.
- Set `output_full_percent` at or below `output_stop_percent` in
  `data/balance.json` to switch it off.

## Station-area buildings (rt3-clone-spec §7.2)

Researched [D/C]:

- **Post office:** mail waits longer before expiring; earns nothing.
- **Hotel:** passengers wait longer; it earns mainly from passengers
  waiting or transferring.
- **Restaurant:** earns from every passenger passing through.
- **Tavern:** earns only from passengers boarding.

These share a fixed market per station: adding buildings does not grow it,
the nearest takes the largest share, and one building can serve several
stations. Rivals may build by your stations. Income is small: about $1,000
a year per building, or $500,000 across a large network.

Our design:

- **Placement:** any company builds one on dry land within 2 km of any
  station. Costs are not researched [I]: post office $30,000, hotel
  $100,000, restaurant $50,000, tavern $25,000, moved by the economic
  state. They count as company buildings, with building upkeep.
- **Traffic:** each station counts passenger loads boarding, loads
  arriving, and loads waiting summed over the days. Our passengers go
  straight to their destinations, so hotels earn from waiting rather than
  transfers.
- **Income [I]:** each month, for each station:
  - hotels share $20 per load-day waiting
  - restaurants share $50 per load arriving or boarding
  - taverns share $80 per load boarding
  Buildings in range split it by 1 / distance² (the spec's share model),
  and it posts to each owner's Station buildings line.
- **Waiting [D, I amount]:** a post office within range halves mail's
  waiting loss, and a hotel passengers'.
- **AI:** rivals put a restaurant by each of their stations that has none.
- **Measured:** four rivals put up 22 buildings in three years, earning
  $50 to $17,000 a year each company [C: small].

## Town growth (rt3-clone-spec §3.2, §6.4)

Researched: towns grow on their own, rail-connected hubs faster, and
unconnected towns stagnate [D/C]. Growth is driven by cargo moving in and
out and by connections [C]. No formula is published.

Our design follows the spec's proposal [I]:

- **Each town** keeps the house cells that make it up and the income earned
  at its stations: passenger and mail fares, and freight sold there.
  Months with no data yet are annualised.
- **Monthly growth rate:** a yearly rate in thousandths, made of
  - 0.8% a year as a base
  - +0.1% for each $1,000 a house of passenger and mail income in the last
    12 months
  - +0.1% for each $1,000 a house of freight income
  The total is capped at 15% a year. A town no train stops at grows at 30%
  of that, which is next to nothing.
- **Houses** accumulate in thousandths. Each whole house goes where the town
  is busiest: an existing house cell with room (up to 6 houses a cell), or a
  free dry cell next to one, whichever is nearest one of the town's
  stations (else its centre). Towns never reach beyond 8 cells from their
  centre. More houses mean more passengers, mail and demand.
- **Stars:** 1 to 5 by houses: under 20, 20, 50, 120 and 300 [I, the
  spec's thresholds]. The client shows them by each town's name.
- `WorldConfig::town_growth = false` stops growth (the editor/sandbox
  switch [D]).
- **Measured:** on the default map with four rivals, served towns grow from
  about 30 houses to 55-100 in ten years (2 to 3 stars). The spec asks for
  visible growth over 10-15 years [C].

## Ports (rt3-clone-spec §6.3)

Researched: ports import and export at the coast or map edge [C]. Each can
be set to receive, supply or exchange cargo [C, research §8]. Unowned ports,
warehouses and power plants upgrade on demand [C].

- **A port** is an industry of a new kind. It takes the cargo in its input
  list (exports) and supplies its output list (imports), each at its rate,
  according to its mode:
  - **Receive:** exports only.
  - **Supply:** imports only.
  - **Exchange:** both.
  Receiving ports pull prices up like any consumer; supplying ones push
  them down like any producer.
- **The shipped port** exports grain, cotton, wool, lumber, coffee, rice,
  sugar, diesel and uranium, and imports rubber and goods. Diesel and
  plantation crops at last have a buyer, and rubber another source.
- **New maps** get 3 ports per 128 km × 128 km, on coastal land (land next to
  water), or on the map edge if there is no coast. Each gets a random mode.
- **Upgrade on demand [C/I]:** at each year end, an unowned port or
  consumer (power plant and the like) that received 90% of its capacity
  doubles it, up to 8 times the original.
- **Not yet:** owning ports.

## Warehouses (rt3-clone-spec §6.3, research §8)

Researched: warehouses act as inland commodity ports, importing and
exporting [D/C]. They store cargo longer and avoid spoilage, and "its profit
is the loss it saves" [C]. They can be set to convert delivered cargo [D/C].

Our design:

- **Building:** a company builds a warehouse on any open dry cell, like a
  processing plant ($450,000; doubled capacity on upgrade).
- **Trade:** it trades its cargo lists like a port, in a mode its owner
  sets: receive, supply or exchange. The shipped warehouse handles the
  port's crops, lumber and diesel inbound and rubber and goods outbound, at
  half a port's rate. It trades only cargo that exists that year.
- **Spoilage [I numbers]:** stock within 2 cells spoils at a quarter of the
  usual rate.
- **Accounts:** the owner earns the value of the spoilage saved, at base
  prices, less the usual overhead [C: "its profit is the loss it saves"].
  It posts to the owner's Industry income and costs like any owned
  industry.
- **Not yet:** converting cargo (one observed case: goods to rubber at up
  to 6 a year, low yield).

## New industries over time (research §5)

Researched: new industries appear as the economy develops, and the player
can buy them [C]. Industry types appear only from their start year [C].

Our design [I], at each year end after closures:

- **Below the usual number:** every industry type in use that year (its
  products, or a consumer's inputs, exist) with fewer open sites than a new
  map would have gets one more. This brings in new kinds of industry as
  their cargo arrives (rubber plantations and tire factories from 1900) and
  replaces closed ones.
- **By chance:** a type already at its usual number gets one more with a 10%
  chance a year, times the economy's activity, up to twice its usual number.
- **Placement:** as on a new map. Producers go anywhere on open land;
  processing plants and consumers go near towns.
- The news names each opening and the nearest town.
  `WorldConfig::industries_appear = false` holds the map as it started.
- **Measured:** 1895 to 1905 on the default map, 42 industries opened
  (including 4 rubber plantations and 3 auto plants), 14 idle plants closed,
  and a busy port expanded.

## Owning industries (rt3-clone-spec §6.1-6.2)

Researched: a company can buy existing industries and build processing
plants anywhere, including any available in the current era. Capacity can
be upgraded. An owned industry's profit is output value less input cost and
labour, and goes to the company's revenue and book value
[overview-finance-scenarios.md §4.4; spec §6.2 [C]].

Our design:

- **Accounts.** Every producer and processing plant keeps monthly accounts,
  owned or not:
  - **revenue:** the carloads it made, at the cargo's base price
  - **costs:** the carloads it used at theirs, plus labour at 20% of output
    value [I], plus overhead of $30,000 a year per level of capacity [C: a
    brewery's overhead]
  - **why base prices [I]:** our price field pins a producer's own cell at
    the producer price and a consumer's at the consumer price, so valuing at
    the local price would make every processor lose money. Price-responsive
    output (slowing when the local price is red) is still to do.
  - **history:** the last 12 months are kept. A new map's year of history
    gives every industry a year of accounts from the start.
- **Buying [C]:** an industry nobody owns costs ten years' profit, or a
  floor of $300,000 per level of capacity if that is more (the spec's farms
  sell from $240,000 to $350,000). Producers and processing plants can be
  bought; consumers and houses cannot.
- **Building [C/D]:** processing plants only (producers "can only be
  bought" [C]), on any open dry cell, once their output cargo exists. The
  cost is 150% of an existing plant's floor price [C, low confidence], i.e.
  $450,000, moved by the economic state like all construction.
- **Upgrading [D/I]:** doubles a plant's capacity, and its overhead, for
  half the cost of building another plant as big. An upgraded plant that
  is under-supplied loses money [D].
- **Books:** an owned industry's month posts to its owner as Industry
  income and Industry costs. What was paid for it is carried as the
  balance-sheet line Industries. Mergers move industries to the buyer [C].
- **Closure [I]:** an unowned producer or plant with five loss years in a
  row closes with a 20% chance each year after. Owned ones never close;
  their owner carries the losses.
- **Scale:** at the spec's ~2.2 loads a year a producer earns tens of
  thousands of dollars a year, so most sell near the floor price, as RT3's
  farms do (median about $313K; see [calibration.md](calibration.md)).
- **AI:** tycoons with an industry trait of 40 or more buy profitable
  industries their stations serve, and double plants running near capacity
  that would repay it within a year.

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
| Saturation | Stock equal to a year of a town's demand halves its price; for industries, two years. | "Industries are hard to oversupply." Scaled with the rates; see [calibration.md](calibration.md). |
| Spoilage | 0.1% per day × decay sensitivity (1-10). | Sensitivity read as decay rate; unverified. |
| Cell cap | 50 carloads per cargo per cell. | |
| Processor stockpile | 30 days of input. | |
| Booster | +50% output, consuming half a carload per carload made. | |
| Rates | Raw producers 2.2 carloads/year (uranium 1.1); processors 3; consumers 2.2, electric plants 4.4; ports 6; warehouses 3; houses 0.1 per house per good. | Spec §6.1 [C]; calibrated in [calibration.md](calibration.md). RT2 hint: 1 iron + 1 coal → 2 steel. Ours is 1 + 1 → 1. |
| Input rules | Only the steel mill is "all". The rest are "any", including the auto plant and munitions factory. | Only the steel mill's rule is sourced. |
| Bakery | A consumer only: no output is listed for RT3. | Data gap. |
| Rubber | Rubber plantations from 1900 (rt3-clone-spec §6.3); ports, which also supply it, are not modelled yet. | |
| Diesel | Made by refineries; nothing consumes it yet. | Data gap. |
