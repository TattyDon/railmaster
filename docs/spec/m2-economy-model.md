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

## Our design (rt3-clone-spec §5.3 [I], adapted)

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
3. **Prices move toward equilibrium** (rt3-clone-spec §5.3). Each node's
   equilibrium price is

   `base × ((D + ε) ÷ (S + ε)) ^ α`, held between 30% and 300% of base,

   where D is what the node's consumers want over a year (two years for
   industries, which are harder to oversupply [C]), S its stock plus what
   its producers make in a year, ε one carload and α 0.5. With nothing
   there it is the base price; a starved town that wants 3 loads a year
   pays twice base; a mine making 3 a year sells at half.
   - **Nodes with industries or houses** move 1/180 of the way there each
     day (τ about six months [I]). A new factory's price builds over
     months, and repeated deliveries erode it the same way [C].
   - **Every other node** moves 1/540 of the way to its own equilibrium
     (base, lowered by any stock lying there), while 0.12 of each
     conductance-weighted difference with its neighbours smooths it toward
     them (the spec's Laplacian coupling). A site's pull reaches about 8
     nodes (about 13 km), and the map reshapes over a year or two [C].

   Why sites are not smoothed too: the spec applies relaxation and
   coupling to every node, but with coupling strong enough to carry a
   price 15 cells and τ of months, a lone consumer's node would rise only
   1–2% of the way to its equilibrium, as coupling drains it far faster
   than relaxation feeds it. Holding site nodes out of the smoothing keeps
   each market its own price, as RT3's overlay shows.
4. **Middlemen move stock** to every dearer neighbour: each day a share of
   0.11 × (price gap − 1% of base) ÷ base × conductance goes to each (the
   spec's flow rule). On flat land the leading edge of a mine's coal travels
   about 7 cells a year toward a buyer, about 3 in mountains [C: ~8 and
   ~4]; with no buyer, 90% of it stays within about 8 cells [C: "a few
   cells"]. Stock is kept in millionths of a carload so these small daily
   shares do not round away.

### Terrain [D behaviour, I numbers]

Middlemen are "slow and costly over land, slower over mountains, cheap
along rivers and coasts" (rt3-clone-spec §5.1 [D]). Each cell gets a
**conductance**, in thousandths of flat land: water 2.0, coast (land next
to water) 1.5, flat 1.0, hills 0.75, mountains 0.5. A cell is hilly when the
relief across it is at least 4% of its width, mountainous at 7%. Where two
cells meet, the edge conducts as their mean. Conductance does three things:

- **Price coupling.** In step 3 each neighbour difference is weighted by
  the edge's conductance. A consumer's pull therefore reaches further along
  water and dies off faster across mountains, so prices are flat along
  coasts and steep over ranges [D].
- **Middleman speed.** The share of stock moved per day is multiplied by
  it. The middleman's cost is the same on every edge, as in the spec; an
  earlier version also divided it by conductance, which left mountains at
  a quarter of the flat speed rather than half.

On flat land with no water every conductance is 1.0 and the model is
exactly the one above; a test holds it to that.

The stand-in map generator makes gentle hills (at most about 8% relief per
cell), so mountains are rare; the thresholds will want revisiting with real
maps. Rivers are not generated yet. Closed territory borders have
conductance 0 (see Territories below).

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

### Consists (rt3-clone-spec §9.3 [D])

Each stop on a train's route has a rule for what it takes on there:

- **Auto** (the default): the most profitable loads for the stops ahead,
  filtered to Any Cargo, Any Freight or Any Express, up to the rule's
  maximum (default 4).
- **Custom:** exactly the listed cars, one per entry (a cargo listed twice
  fills two cars), whatever else is waiting.
- **Minimum:** the train waits at the stop until it has at least that many
  loaded cars, trying again each dwell ("wait for a full load"). Default 0.
- **Slots:** cargo cars, a caboose and a dining car share 8. A caboose
  halves the breakdown chance per distance run; a dining car makes
  passengers on the train pay 20% more.
- **Hauled cars:** only loaded cars, the caboose and the dining car are
  pulled; empty cars stay behind. Speed on grades and fuel per km count
  hauled cars, so a train running light is faster and cheaper. The
  train's car slots are resized to the rule at each stop, keeping any
  through loads.
- A rule set on one stop changes only that stop; set without a stop it
  applies to all (RT3's "apply this consist to all stations").
- Copying a train buys the same engine type, route, rules and special
  cars; retiring one takes it out of service and writes off its engine.

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

- **Price:** unsold stock counts as supply, so a producer's node price
  falls as it piles up (step 3), as a consumer's does with unconsumed stock.
- **Pace:** a producer or processing plant compares its own equilibrium
  price, counting its unsold stock, with what it would be with none. It
  runs at full pace while that is at least 70%, slowing to a stop at 40%:
  at the spec's rates, about 1.5 and 7 years of output piled up. A plant
  that slows uses fewer inputs. The comparison is with the producer's own
  stock-free price, not with base, so a big producer is not slowed for
  being big.
- **In practice:** middlemen carry output off, so a producer normally
  holds little and runs at full pace; it slows only when nobody hauls and
  nobody nearby buys, and recovers as soon as a railway takes the stock.
- Set `output_full_percent` at or below `output_stop_percent` in
  `data/balance.json` to switch it off.

## Territories (rt3-clone-spec §3.3)

Researched: a map can be split into named territories, and a company
needs access rights to build in one [D]. Access costs from about $600K to
$2M for minor states and $10M for the large ones [C]. Territories may shift
the credit rating by grades, and raise station costs and overhead (Hannover
+1 grade, Bavaria +15% stations, Prussia +30% overhead) [C]. Closed borders
stop middlemen [C].

Our design:

- **Data:** each territory has a name, an access price, a credit-grade
  shift, a station-cost percentage, an overhead percentage and a
  closed-border flag; every map cell belongs to one territory. A territory
  with no price is open to all.
- **Building** track (every point of the run), stations, support and
  station buildings, and plants needs access to where it stands. Trains run
  anywhere, rivals' track included.
- **Access** is bought once per company (BuyTerritoryAccess) and posted as
  "Territory fees", the spec's income-statement line. A merger brings the
  target's rights.
- **Modifiers:** stations built in a territory cost its percentage. Track
  upkeep is weighted by where the company's track lies (by length) and
  building upkeep by where its stations are; each territory's overhead
  percentage applies to its share [I]. Each territory a company holds
  access to shifts its credit rating by its grades.
- **Closed borders:** between economy nodes in different territories,
  either of them closed, nothing conducts, so neither middlemen nor price
  signals cross.
- **Generated maps** (`WorldConfig::territories`, the client's
  `--territories=N`; none by default, like a sandbox): the map is split into
  the cells nearest N random seeds. The first town's territory is free; the
  others cost $600K–$2M, or $10M with a 15% chance, with stations up to 20%
  and overhead up to 30% dearer, a 20% chance of a credit grade either way
  and a 10% chance of closed borders (all in `map.territory_*`). Scenario
  maps will set their own (World::set_territories).

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
| Equilibrium price | base × ((D + 1 load) ÷ (S + 1 load))^0.5, 30%–300% of base; D over 365 days (towns) or 730 (industries), S stock plus 365 days' output. | rt3-clone-spec §5.3 [I]; horizons and ε ours. |
| Neutral price | Base, far from any site (the spec's ε ÷ ε). | |
| Relaxation | Site nodes 1/180 a day; other nodes 1/540 a day plus coupling 0.12 × conductance-weighted neighbour differences. | Spec τ 6–12 months; calibrated so the map reshapes in about 1.2 years. |
| Middlemen | 0.11 × (gap − 1% of base) ÷ base × conductance of the stock a day, to each dearer neighbour. | Calibrated to ~7 cells a year flat, ~3 in mountains. |
| Conductance | Water 2.0, coast 1.5, flat 1.0, hills 0.75 (≥ 4% relief), mountains 0.5 (≥ 7%). | Spec §5.3 gives flat 1.0, hills 0.75, mountains 0.5; water's figure and the thresholds are ours. |
| Economic states | See the table above; checked twice a year. | Section `economic_states`. |
| Saturation | Stock equal to a town's yearly demand brings it from about twice base back to base; industries count two years of demand. | "Industries are hard to oversupply." |
| Spoilage | 0.1% per day × decay sensitivity (1-10). | Sensitivity read as decay rate; unverified. |
| Cell cap | 50 carloads per cargo per cell. | |
| Processor stockpile | 30 days of input. | |
| Booster | +50% output, consuming half a carload per carload made. | |
| Rates | Raw producers 2.2 carloads/year (uranium 1.1); processors 3; consumers 2.2, electric plants 4.4; ports 6; warehouses 3; houses 0.1 per house per good. | Spec §6.1 [C]; calibrated in [calibration.md](calibration.md). RT2 hint: 1 iron + 1 coal → 2 steel. Ours is 1 + 1 → 1. |
| Input rules | Only the steel mill is "all". The rest are "any", including the auto plant and munitions factory. | Only the steel mill's rule is sourced. |
| Bakery | A consumer only: no output is listed for RT3. | Data gap. |
| Rubber | Rubber plantations from 1900 (rt3-clone-spec §6.3); ports, which also supply it, are not modelled yet. | |
| Diesel | Made by refineries; nothing consumes it yet. | Data gap. |
