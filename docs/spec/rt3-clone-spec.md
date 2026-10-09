# Railroad Tycoon 3 Clone — Functional & Technical Specification

Version 1.0 · 9 October 2026 · Target: faithful recreation of Railroad Tycoon 3 (PopTop, 2003) including the Coast to Coast (C2C) expansion / v1.04–1.05 patch behaviour.

## 0. How to use this document

This spec is written to be handed to a build agent section by section. Every rule, number and formula carries a confidence tag:

| Tag | Meaning | How to treat it |
|---|---|---|
| **[D]** documented | Official manual, official patch notes or developer Q&A | Implement exactly |
| **[C]** community | Strategy guides, fan sites, forum measurements | Implement as default; expose in config |
| **[I]** inferred | No public source; designed to reproduce documented behaviour | Implement as a tunable placeholder; mark in code with `// TUNABLE` |

Rule for the build: **every [C] and [I] number lives in a data/config file, never hard-coded.** That is what lets you calibrate against the real game later (see §14, Calibration plan).

Legal note: game mechanics are fine to copy; RT3's name, art, models, text, music and data files are not. Use your own title and assets, and treat the original data files as a calibration reference only.

### 0.1 Document map

1. Architecture & simulation loop
2. Time
3. World & map
4. Cargo catalogue
5. Economy: price field, supply, demand, middlemen
6. Industries & production chains
7. Stations & station buildings
8. Revenue
9. Locomotives & rolling stock
10. Train movement & physics
11. Track construction & operations
12. Company, player & stock market
13. AI rivals
14. Scenarios, goals, events & editor
15. User interface
16. Data files & save format
17. Open items & calibration plan
18. Sources

---

## 1. Architecture & simulation loop

### 1.1 Recommended architecture [I]

Separate the deterministic simulation from rendering so it can be tested headless, replayed and (later) run in lockstep multiplayer.

```
┌──────────────┐   commands   ┌─────────────────────┐   snapshots   ┌────────────┐
│ UI / Input   │ ───────────▶ │ Simulation (pure,   │ ────────────▶ │ Renderer   │
│ (panels,     │              │ deterministic,      │               │ (3D view,  │
│  build tools)│ ◀─────────── │ fixed tick)         │               │  overlays) │
└──────────────┘   events     └─────────────────────┘               └────────────┘
                                   ▲        │
                         data/*.json│        │ save/load
                                   │        ▼
                              ┌──────────────────┐
                              │ Content & config │
                              └──────────────────┘
```

- **Simulation core**: fixed-timestep, seeded RNG, no wall-clock reads, no floating-point non-determinism across platforms if multiplayer is planned (use fixed-point or a single platform/compiler).
- **Command pattern** for every player action (`BuildTrack`, `BuyTrain`, `IssueBonds`, `BuyShares`…). Commands are validated by the sim, so AI and UI use the same API and multiplayer is a matter of broadcasting commands.
- **Event bus** out of the sim (`TrainArrived`, `CargoDelivered`, `NewspaperEvent`, `MedalAchieved`, `MarginCall`…) drives UI, audio and news.

### 1.2 Tick schedule [D cadence / I granularity]

| Cadence | Work |
|---|---|
| Every sim tick (e.g. 1 game-hour) | Train movement, loading/unloading, consumable use, breakdown rolls, meeting/yield resolution |
| Daily | Cargo decay, station waiting-cargo update, express-unit spawning and expiry |
| Weekly (or every N days) | Price-field diffusion & middleman flow (expensive — can be spread across ticks by region) |
| Monthly | Industry production/consumption step, stock price update, goal & medal checks (**at month end [C]**), margin-call check, investor events, town growth step |
| Quarterly | Bond interest payment **[D]**, dividend payment **[I, RT2 convention]** |
| Semi-annual | Economic-state transition roll (RT2: ~March and September) **[C]** |
| Yearly (fiscal year end) | Ledger/annual report popup **[D]**, salary review, investor-sentiment review, track-tile allowance in budgeted scenarios **[C]**, station-age modifier step |

---

## 2. Time

- Calendar is a real Gregorian calendar with month/day and, for short scenarios, hour of day (The War Effort runs 06:00 11 Dec 1941 → 06:00 15 Dec 1941) **[D/C]**.
- Typical scenario length 25–35 years **[C]**.
- **Game speeds**: Paused, Very Slow, Slow, Normal, Fast, Very Fast (6 states). Keys `+`, `−`, `Pause`. Every game starts **paused** **[D]**.
- Real time per game year at Normal: not documented. Default **~4 minutes per game year at Normal**, each speed step ×2 / ×0.5 **[I]**.
- Day/night cycle with shortened nights, and a weather cycle ("mostly clear with occasional storms") — both cosmetic in the default implementation **[D cosmetic-ness I]**. CtC added a "brighter night" option **[D]**.
- In multiplayer, game speed is shared **[D]**.

---

## 3. World & map

### 3.1 Terrain [C/I]

- Terrain is a regular **heightfield of cells**; objects and track are placed **at any angle**, not snapped to the grid **[D/C]**.
- Map dimensions come from the heightmap; community maps use 3:4 aspects such as 384×512 or 768×1024 cells **[C]**. Supply presets: Small 256×256, Medium 384×512, Large 768×1024 **[I]**.
- Industries and demand blocks have a **4×4 cell** footprint **[C]**.
- Terrain features: elevation, ocean, lakes, rivers, trees (by species — lumber and rubber trees are economic sources), painted ground colour **[C]**.
- Map scale (miles per cell) is undocumented. Choose a constant so a hop between two neighbouring 1–2 star cities (roughly 50–100 real miles) takes 1–3 game weeks with a mid-era locomotive **[I]**. Suggested start: 1 cell ≈ 0.5 mile.

### 3.2 Cities [D/C]

- Cities are rated **1–5 stars** by size. Houses are individual buildings; each is an origin/destination for passengers and mail and a consumer of goods.
- Cities grow on their own; rail-connected hubs grow faster; unconnected towns stagnate **[D/C]**. Towns can grow into each other (Fort Worth becomes a Dallas suburb) **[C]**.
- Town growth formula — see §6.4.

### 3.3 Territories [D mechanic / C numbers]

- A map can be split into named territories (states/countries). Each company holds per-territory **access rights**; without them it cannot build there **[D]**.
- Access cost is per territory, set by the scenario: ~$600k–$2M for minor German states, $10M for Prussia/Bavaria/Hannover **[C]**. Events can discount access (Texas Tea: Mexico drops from $10M to $50k if a condition is met) **[C]**.
- Territories can carry modifiers: credit rating ±grades, station cost %, overhead % (e.g. Hannover +1 credit grade, Bavaria +15% station cost, Prussia +30% overhead) **[C]**.
- Closed borders can also block off-rail middleman flow (§5.3) **[C]**.

```ts
interface Territory {
  id: number; name: string;
  polygon: Vec2[];                  // or cell mask
  accessCost: Money;
  modifiers: { creditGrades?: number; stationCostPct?: number; overheadPct?: number };
  blocksMiddlemen: boolean;
}
```

---

## 4. Cargo catalogue

### 4.1 Cargo table [D]

41 cargo types: 3 express, 38 freight. "Median" is the typical price per carload in thousands of dollars ($30 = ~$30,000/load). "Sens" is delivery-time sensitivity, 1 (insensitive) to 10 (very perishable).

| Cargo | Class | Available | Median ($k/load) | Sens |
|---|---|---|---|---|
| Passengers | Express | 1800 | distance-based | 9 |
| Mail | Express | 1800 | distance-based | 10 |
| Troops | Express | 1848 | distance-based | 7 |
| Alcohol | Freight | 1800 | 100 | 2 |
| Aluminum | Freight | 1910 | 85 | 1 |
| Ammunition | Freight | 1848 | 160 | 2 |
| Automobiles | Freight | 1900 | 200 | 3 |
| Bauxite | Freight | 1910 | 30 | 1 |
| Cheese | Freight | 1880 | 235 | 5 |
| Chemicals | Freight | 1905 | 30 | 1 |
| Clothing | Freight | 1800 | 95 | 3 |
| Coal | Freight | 1800 | 30 | 1 |
| Coffee | Freight | 1800 | 45 | 2 |
| Corn | Freight | 1800 | 25 | 3 |
| Cotton | Freight | 1800 | 30 | 2 |
| Diesel | Freight | 1890 | 100 | 1 |
| Fertilizer | Freight | 1905 | 80 | 2 |
| Furniture | Freight | 1880 | 220 | 1 |
| Goods | Freight | 1800 | 170 | 1 |
| Grain | Freight | 1800 | 30 | 3 |
| Iron | Freight | 1800 | 30 | 1 |
| Livestock | Freight | 1800 | 90 | 8 |
| Logs | Freight | 1800 | 30 | 2 |
| Lumber | Freight | 1800 | 85 | 1 |
| Meat | Freight | 1800 | 195 | 5 |
| Milk | Freight | 1800 | 110 | 10 |
| Oil | Freight | 1860 | 40 | 1 |
| Paper | Freight | 1800 | 85 | 1 |
| Plastic | Freight | 1935 | 85 | 1 |
| Produce | Freight | 1800 | 45 | 8 |
| Pulpwood | Freight | 1800 | 30 | 2 |
| Rice | Freight | 1800 | 30 | 2 |
| Rubber | Freight | 1900 | 30 | 1 |
| Steel | Freight | 1856 | 85 | 1 |
| Sugar | Freight | 1800 | 35 | 2 |
| Tires | Freight | 1900 | 85 | 1 |
| Toys | Freight | 1880 | 175 | 3 |
| Uranium | Freight | 1950 | 200 | 3 |
| Waste (Recyclables) | Freight | 1990 | 40 | 6 |
| Weapons | Freight | 1848 | 235 | 2 |
| Wool | Freight | 1800 | 30 | 1 |

- C2C added **no new cargo types** as far as any source shows **[I, moderate confidence]**.
- The original engine had a hard cap of **52 cargo slots** (`.cty` files); the community v1.06 patch adds hidden freight: Ceramics, Concrete, Crystals, Rock, Dye, Electronics, Ingots, Machinery, Ore, Medicine **[C]**. Make the cargo table data-driven with no hard cap; ship the 41 as default.

```ts
interface CargoType {
  id: string; displayName: string;
  isExpress: boolean;               // passengers, mail, troops
  availableYear: number;
  medianPrice: Money;               // per carload; freight only
  sensitivity: 1|2|3|4|5|6|7|8|9|10;
  carModel: string;                 // visual only
}
```

### 4.2 Car weights [D]

| Era | Freight car (loaded) | Express car / caboose / diner |
|---|---|---|
| 1830–1849 | 10 t | 2/3 → 6.7 t |
| 1850–1899 | 20 t | 13.3 t |
| 1900–1949 | 40 t | 26.7 t |
| 1950+ | 80 t | 53.3 t |

Pay is per carload, so weight affects only physics. Note the step change: a consist that is fine in 1899 struggles in 1900 **[D]**. Weight is evaluated at the current date, not the date the train was bought.

---

## 5. Economy: the price field

This is the heart of RT3 and the thing that makes it differ from RT2. Get this right first.

### 5.1 Model summary [D]

- The map holds roughly **15,000 economy nodes**, spaced evenly across the whole map, not just in cities **[D, developer Q&A]**.
- Each node stores, for every freight cargo, a **price** and an **inventory** **[D]**.
- Producers add inventory and depress local price; consumers remove inventory and raise local price.
- Neighbouring node prices are tightly coupled. Off-rail **middlemen** (wagons, barges, coastal ships) automatically move freight from low-price to high-price nodes: slow and costly over land, slower over mountains, cheap along rivers and coasts **[D]**.
- Freight revenue ≈ destination price − origin price per carload (§8.1) **[D]**.
- Express cargo (passengers, mail, troops) is *not* part of the price field; it is unit-based with explicit destinations (§8.3) **[D]**.

### 5.2 Node grid [I]

- Node spacing = sqrt(mapCells / 15000). For a 768×1024 map that is ~7 cells; use a fixed spacing per map (e.g. every 4 cells, matching the 4×4 building footprint) and accept a different node count.
- Each node: `price[c]`, `inventory[c]`, `terrainConductance` (derived from slope and water), `territoryId`.
- Store as structure-of-arrays (`Float32Array` per cargo) for cache-friendly diffusion. 15k nodes × 41 cargos × 2 floats ≈ 5 MB — fine.

```ts
interface EconomyGrid {
  width: number; height: number;           // nodes
  price: Float32Array[];                   // [cargo][node]
  inventory: Float32Array[];               // [cargo][node]
  conductance: Float32Array;               // per edge or per node
  basePrice: Float32Array;                 // per cargo: medianPrice
}
```

### 5.3 Per-step update [I — designed to reproduce D/C behaviour]

Each economy step (weekly suggested), for each freight cargo `c`:

1. **Production**: every producer adds its output to the node(s) under its footprint (§6).
2. **Consumption**: every consumer removes up to its demand from nodes within its pull radius.
3. **Equilibrium price** per node:
   `Peq = basePrice[c] × clamp( (demand + ε) / (supply + ε) ) ^ α`, clamped to **0.3×–3×** median. α ≈ 0.5.
4. **Price relaxation**: `P += (Peq − P) × dt / τ`, with **τ = 6–12 game months** **[I]** — matches the community observation that a new factory reshapes the price map over **1–2 years** **[C]**.
5. **Middleman flow** on each edge (a→b):
   `flow = max(0, P_b − P_a − edgeCost) × I_a × k_flow × conductance(a,b)`
   - Conductance: water/river/coast high; flat land 1.0; hills 0.75; mountains 0.5; closed border 0.
   - Calibrate so off-rail freight moves **~8 cells/year on flat or water, ~4 cells/year in mountains** **[C]**.
   - Freight drifts toward demand **within ~30 cells**; with no demand it spreads only a few cells from source **[C]**.
6. **Price coupling**: after flow, apply a small Laplacian smoothing of price between neighbours (`P_i += β Σ(P_j − P_i)`), weighted by conductance. Prices are therefore flat along rivers and coasts and steep across mountains **[D]**.

Effects this must reproduce:
- Repeated hauling on one route narrows the price gap and earns less per trip **[C]**. (Example: meat at a packing plant fell from $255k to $205k per load as supply grew.)
- Delivering to a small town beside a big city lets middlemen carry the goods onward; serving the big city directly keeps more profit for the railroad **[D]**.

### 5.4 Overlays fed by the field [D]

- **Cargo Supply overlay (F1)**: per-cargo heatmap — red = low price, yellow = medium, green = high; black boxcar icons show carloads waiting **[D]**.
- **Profitability overlay**: bright green profitable, dull/yellow marginal, red loss **[D/C]**.

### 5.5 Economic state [C]

Five states: **Depression, Recession, Normal, Prosperity, Boom**. Random walk, not player-controlled, checked about twice a year **[C]**. Announced in the news ticker.

| State | Production & demand | Construction / fuel / labour costs | Prime rate [I] | Stock index |
|---|---|---|---|---|
| Boom | ×1.25 | higher (×1.15 [I]) | 4% | ×1.25 |
| Prosperity | ×1.20 | ×1.08 [I] | 5% | ×1.12 |
| Normal | ×1.00 | ×1.00 | 6% | ×1.00 |
| Recession | ×0.85 [I] | ×0.92 [I] | 7% | ×0.90 |
| Depression | ×0.75 [I] | ×0.85 [I] | 8% | ×0.80 |

Observed bond rates: ~5–6% in boom, 7–8% in recession, 10–12% early game **[C]**. Automobile demand drops sharply in recessions **[C]**.

Transition matrix (semi-annual) **[I]**: stay 50%, move one step toward Normal 30%, move one step away 20%; clamp at ends.

Inflation: no secular inflation is documented. Optional slow cost index (0.5–1%/year on construction and running costs) behind a setting, default off **[I]**.

---

## 6. Industries & production chains

### 6.1 General rules

- 60+ industry types and 180+ building types in the original **[D, marketing]**.
- Industry types are enabled per scenario and per region, and appear only from their start year **[C]**.
- **Raw producers** (farms, ranches, mines, logging camps, plantations, wells): base output **~2.2 carloads/year** **[C]**. Usually cannot be built by the player, only bought; they appear randomly **[C]**.
- **Boosted producers**: crop farms get extra output from steady **fertilizer**; livestock farms (cattle, dairy, sheep) from steady **corn**. Max bonus **+25% at ≥1.5 loads/year delivered** **[C, low confidence]** (RT2 was +50%).
- **Processors**: base capacity **~3 loads/year** **[C]**. **Upgrading doubles capacity** at much less than new-build cost but raises overhead; an under-supplied upgraded plant loses money **[D]**.
- **Recipes** run about **1:1 per carload per input type** **[C]**. Multi-input plants (steel mill) need all inputs; the modder observation is 1 iron + 1 coal → 2 steel, i.e. each input yields one output.
- **Price-responsive output**: a factory buys inputs and sells outputs at local node prices, and slows or stops when its output price nearby is red **[C]**.
- **Lag**: finished goods appear ~1–2 years after steady supply begins (largely a consequence of the price field) **[C]**.
- **Costs**: labour scales with output but never reaches zero; overhead (land tax, power, management) is mostly fixed — example brewery $30k/year **[C]**.
- **Closure**: industries that lose money for several years can close; warehouses and ports have been seen to disappear **[C]**. Default: close after 5 consecutive loss years, 20% chance per year after that **[I]**.

### 6.2 Ownership economics [C]

| Item | Value |
|---|---|
| Purchase price of profitable industry | ~10× annual profit (refinery earning $750k/yr ≈ $7.5M) |
| Purchase price of loss-making industry | floor price (deep discount) |
| New-build cost | ~150% of an equivalent existing facility [low confidence] |
| Minimum prices (examples) | Cattle ranch $350k · Dairy $350k · Grain $260k · Corn $260k · Cotton $250k · Fruit orchard $240k |
| Farm size tiers | 4 / 8 / 12 (small/medium/large; units probably footprint) |

Industry profit goes to the owning company's revenue and book value; scenarios can track lifetime industry profit **[C]**.

### 6.3 Production chains

| Chain | Flow | Confidence |
|---|---|---|
| Wood | Logging camp → Logs → Lumber mill → Lumber → Furniture factory → Furniture | [C] |
| Paper | Logs / Pulpwood → Paper mill → Paper | [C/I] |
| Meat | Cattle ranch → Livestock → Meat packing plant → Meat | [C] |
| Dairy | Dairy farm → Milk → Dairy processor (1880+) → Cheese | [C] |
| Brewing | Grain → Brewery → Alcohol | [C] |
| Distilling | Produce (or Sugar [I]) → Distillery → Alcohol | [C] |
| Textiles | Cotton and/or Wool → Textile mill → Clothing | [D cotton / C wool] |
| Steel | Iron + Coal → Steel mill → Steel | [C] |
| Goods | Steel (or Plastic / Aluminum) → Tool & die → Goods | [C] |
| Fuel | Oil well → Oil → Refinery → Diesel | [C] |
| Plastics | Oil → Plastics factory → Plastic → Toy factory → Toys | [C] |
| Autos | Rubber plantation (1900) → Rubber → Tire factory → Tires; Tires + Steel → Auto plant → Automobiles | [C] |
| Aluminum | Bauxite mine (1910) → Bauxite → Aluminum mill → Aluminum | [I] |
| Agrichem | Chemical plant (1905, source) → Chemicals → Fertilizer factory → Fertilizer → crop farms | [D] |
| Armaments | Steel/Iron (+ Chemicals [I]) → Weapons factory / Ammunition factory | [C/I] |
| Recycling | Houses (1990) → Waste → Recycling plant → Paper/Goods [I] (reported unprofitable) | [C/I] |
| Plantation | Coffee, Rice, Sugar → consumed by houses and ports | [C] |
| Uranium | Uranium mine (1950) → ports / power plants [I] | [I] |

**Warehouses** act as inland commodity ports (import and export) and can be configured to convert delivered cargo (one case: Goods → Rubber at up to 6/year, low observed yield) **[D/C]**. **Ports** import and export at map edge/coast **[C]**.

**Houses** consume: grain, produce, corn, milk (until ~1890), coffee, clothing, goods, furniture, paper, meat, cheese, alcohol, lumber and more; bigger towns demand more **[C]**.

```ts
interface IndustryType {
  id: string; availableYear: number; footprint: [number, number];   // 4x4
  inputs: { cargo: string; perOutput: number }[];  // [] for raw producers
  outputs: { cargo: string; baseRate: number }[];  // loads/year
  boost?: { cargo: string; maxPct: number; loadsPerYearForMax: number };
  baseCapacity: number; upgradeMultiplier: 2;
  overheadPerYear: Money; labourPerLoad: Money;
  playerBuildable: boolean; buildCostFactor: number;
}
```

### 6.4 Town growth [I]

No formula is published. Proposed:
```
growthPerYear = base(0.5–1%) 
              + a × (passengers+mail served per year / population)
              + b × (value of goods delivered to town / population)
              × (connected ? 1 : 0.3)
```
New houses spawn on free cells near existing houses and stations. Star rating = f(house count): 1★ <20, 2★ 20–50, 3★ 50–120, 4★ 120–300, 5★ 300+ **[I]**. A healthy network should visibly grow cities over 10–15 years **[C]**. Editor/sandbox option: disable building/city generation **[D]**.

---

## 7. Stations & station buildings

### 7.1 Stations

| Size | Cost | Catchment radius |
|---|---|---|
| Small | $50,000 [C] | 2 cells [I] |
| Medium | $100,000 [C] | 3 cells [I] |
| Large | $200,000 [C] | 4 cells [I] |

- Size changes **only cost and radius**; architecture style is cosmetic **[D]**.
- Small and medium can be upgraded; adjacent buildings may need bulldozing first **[D]**.
- Placement colour cue: yellow = valid but unconnected, green = snapped to track, red = invalid; rotate with `[` `]` **[D]**.
- **Freight**: cargo inside the radius is immediately available; cargo outside trickles to the nearest railhead via middlemen **[D]**.
- **Express**: a passenger/mail unit uses a station **only if its origin house is inside the radius** **[D]**.
- Stations create no demand themselves; cargo arrives only if prices pull it **[C]**.
- An industry inside the station's highlight square feeds output straight into station inventory **[C]**.
- Freight will **not board** a train headed to a station where its price is lower than at its current station **[D]**.

### 7.2 Station-area buildings

| Building | Effect | Revenue |
|---|---|---|
| Post office | Mail waits longer before expiring [D] | none directly |
| Hotel | Passengers wait longer; earns mainly from **transferring** passengers [D/C] | per waiting/transfer passenger |
| Restaurant | Earns from every passenger passing through [C] | per passenger visit |
| Tavern / saloon | Earns only from passengers **boarding** there [C] | per boarding passenger |
| Service tower | Refills water (steam) and sand (all) [D]; relatively cheap; ~1 per station pair in steam era | cost centre |
| Maintenance facility (roundhouse) | Refills oil, does maintenance, cuts breakdown/crash risk [D]; trains stop automatically when oil is low and the facility is on route | cost centre |

- Hotel/restaurant/tavern share a fixed market per station: **adding buildings does not grow it**; competitors split it with the **nearest** taking the largest share (two ≈ 50/50) **[D/C]**. A building can serve several nearby stations; rivals may build next to your stations **[C]**.
- Typical income is small (≈ $1k/yr to a small loss per building; ≈ $500k/yr across a large network) **[C]**.

Share model **[I]**: `share_i = (1/d_i²) / Σ(1/d_j²)` over same-type buildings within range of the station.

---

## 8. Revenue

### 8.1 Freight [D]

```
revenuePerLoad = (P_dest[c] − P_origin[c])          // node prices at the two stations
               × loadFraction                         // 0.5–1.0 if car left part-full [C]
               × timelinessFactor(c, transitDays)     // §8.2
               × difficultyMult                       // §8.4
               × stationAgeMult(destStation)          // §8.4
               × (1 − foreignTrackShare)              // §8.5
```

- Manual example: coal bought at $30k and sold at $50k ≈ $20k per load **[D]**.
- **Hubs**: a load relayed A→B→C is valued separately on each leg; each delivery pays its own price difference **[C]**.
- After delivery the destination node inventory rises (+1 load) and the origin node inventory falls (−1 load), which drives the narrowing-margin effect.
- A changed destination mid-trip carries a profit penalty (reduced in v1.04) **[D]**. Default: −25% **[I]**.

### 8.2 Timeliness [D qualitative / I numbers]

Value falls with transit time at a rate set by sensitivity (1–10). Cargo decays more slowly while waiting in a station's catchment than while on a train, and keeps decaying while a train is being serviced **[C]**.

```
timelinessFactor = exp(−k × S × transitDays)          // S = sensitivity
k chosen so that S=10 loses ~50% in 30 days, S=1 loses ~5% in 30 days   // k ≈ 0.0023
waitingDecay = 0.5 × onTrainDecay
```
Cargo can vanish entirely after long waits; express especially gives up **[C]**. Default: freight expires below 10% residual value; express per §8.3.

### 8.3 Express: passengers, mail, troops [D]

- Each express unit has a specific **origin house** and **destination house** (manual example: a traveller from near Birmingham to an aunt's house outside London) **[D]**.
- A unit boards only a train that moves it usefully toward its destination; it may travel away to a hub to catch a direct train; it won't board if the next stop is rarely served **[D]**.
- If it cannot reach its destination in time it goes home and **avoids rail for a while** **[D]** (default cool-down 6 months for that house **[I]**).
- Pay rises with distance and with speed **[D]**. Volume rises with the number of reachable destinations, so connecting to rival networks helps **[D]**.

Proposed fare **[I]**:
```
idealDays  = distance / referenceSpeed(era)
speedFactor = clamp(idealDays / actualDays, 0.25, 1.5)
fare       = baseRate(cargo, era) × distance^0.9 × speedFactor
             × timelinessFactor(S, actualDays)
             × appealMult(loco)          // +30% … −15% [D]
             × (diningCar ? 1.20 : 1)    // [D] passengers only
             × difficultyMult × stationAgeMult
```
Spawning **[I]**: each house generates passengers and mail per month ∝ (reachable destinations within the network) × era factor; destinations chosen with gravity-model weighting (population / distance²) over reachable houses. Troops spawn only via scenario events and military buildings.

Scenario events can scale passenger volume (e.g. gold rush +50%) **[C]**.

### 8.4 Revenue modifiers [D]

**Difficulty**

| Level | Revenue per load | Other effects |
|---|---|---|
| Easy | +20% | lower maintenance, fuel and track costs; AI financially penalised [D] |
| Medium (Campaign "Normal") | 0% | none |
| Hard | −10% | AI financial bonus (RT2: +10% [C]) |
| Expert | −20% | AI financial bonus (RT2: +20% [C]) |

Easy cost reduction: −15% maintenance, fuel and track **[I]**.

**Station age** — the first railroad into a city earns a premium that turns into a penalty:

| Years since first station in that city | Modifier |
|---|---|
| 0 | +15% |
| 4 | 0% |
| 20+ | −10% |

Piecewise-linear between points **[I interpolation]**. Age counts from the *first* station built in the city, including rebuilt or extra ones. Stations in open country get **half** the effect **[D]**.

### 8.5 Trackage rights [D]

Any company may run over a rival's track and use its stations. The track owner receives a share of the train's revenue **equal to the fraction of the route distance on its track**; the runner still pays **100% of fuel** **[D]**. Manual advice: keep foreign track to 10–20% of a route.

---

## 9. Locomotives & rolling stock

### 9.1 Stat model [D]

The Add Train window shows exactly these stats — horsepower, tractive effort and engine weight are **never shown** **[D]**:

| Stat | Effect |
|---|---|
| Cost | Locomotive purchase price. **Cars are free** and can be added/removed at will. |
| Annual maintenance | Rises with age; higher when out of oil |
| Fuel economy | Fuel cost ∝ fuel economy × distance × load weight |
| Acceleration | Time to reach top speed |
| Reliability | Relative breakdown/crash chance |
| Passenger appeal | Passenger & mail revenue +30% (handsome) to −15% (ugly) |
| Engine type | Steam / Diesel / Electric |
| Top speed | Plus a **speed-vs-grade chart**; freight/express/mixed radio buttons only change the car weights the chart assumes |

Reviews describe the grade rating running from "atrocious" to "mountain king" **[C]**. Top speeds range ~30–300 mph **[C]**.

```ts
interface LocoType {
  id: string; name: string; engine: 'steam'|'diesel'|'electric';
  yearAvailable: number; yearObsolete: number; regions: string[];
  topSpeedMph: number;
  accelTimeDays: number;           // time constant, §10
  gradeDropPer1Pct: number;        // fraction of speed lost per 1% grade at reference load
  freeWeightTons: number;          // load it hauls without speed loss
  reliability: number;             // 0–1, higher = fewer breakdowns
  fuelEconomy: number;             // cost per ton-mile multiplier
  appeal: number;                  // −0.15 … +0.30
  cost: Money; maintenancePerYear: Money;
}
```

### 9.2 Roster [D names for C2C / C names otherwise / numbers missing]

~60 locomotives in the base game, ~70 with C2C **[C]**. Real-world introduction years shown in brackets as starting values for `yearAvailable` **[I]**.

- **Steam**: Planet 2-2-0 [1830], Adler 2-2-2 [1835], Norris 4-2-0 [1836], Firefly 2-2-2 [1840], Baldwin 0-6-0 [~1846 event in Go West!], Beuth 2-2-2 [1843], Crampton 4-2-0 [1846], American 4-4-0 [~1850], Eight Wheeler 4-4-0, Camelback 0-6-0 [~1850s], Consolidation 2-8-0 [1866; ~1860 event in Central Pacific], Fairlie 0-6-6-0 [1869], Stirling 4-2-2 [1870], Shay [1880], Atlantic 4-4-2 [1895], Duke Class 4-4-0 [1895], Class 500 4-6-0 [1900], S3 4-4-0 [1903], Pacific 4-6-2 [~1905], H10 2-8-2 [~1920], Class 01 4-6-2 [1925], Northern 4-8-4 [1927], Challenger 4-6-6-4 [1936], Mallard 4-6-2 [1938], Big Boy 4-8-8-4 [1941], Orca NX462 (hidden; 1941, 104 mph, $200k, $24k/yr maintenance [C]), Kriegslok 2-10-0 [1942], Red Devil 4-8-4.
- **Electric**: Be 5/7 [~1913], Ge 6/6 Crocodile [1919], EP-2 Bipolar [1919], 2-D-2 [~1925], GG1 [1934], E428 [1934], E18 [1935], Class 9100 [~1950], Shinkansen Series 0 [1964], Class EF 66 [1966], VL80T [1967], Class 6E [1969], ET22 [1969], Class 103 [1970], Re 6/6 [1972], E60CP [1974], Trans-Euro (fictional) [1994], Brenner E412 [1997], E-88 (fictional) [2012], Ee 3/3, Class 132.
- **Diesel**: F3 [1945], GP7 [1949], V200 [1953], C55 Deltic [1961], FP45 [1967], DD40AX [1969], HST 125 [1976], USA 103, NA-90D, DD 080-X (fictional).
- **C2C additions [D]**: 242 A1, Class 460, Class A1, Class P8, GP 35, QJ Class, U1, The Zephyr.

Role guidance **[C]**: Consolidation = best early freight workhorse; American = small early lines; Shay = hills; Northern = balanced but expensive; Big Boy/Challenger = heavy freight; GP diesels = cheap reliable all-rounders; Shinkansen/Trans-Euro/E-88 = express.

**The numeric columns (speed, acceleration, grade, cost, maintenance, reliability, appeal) are the biggest gap in this spec** — see §17 for exactly how to fill them.

Era progression guidance **[D]**: diesels and electrics appear in the early 20th century but become clearly superior only around 1950; the manual advises upgrading locomotives every 20–30 years. Locos are gated by date and by region/territory; scenarios can override, and the "Allow locomotives from any time period" option disables gating **[D]**.

### 9.3 Consists [D]

| Rule | Value |
|---|---|
| Max cars | **8 slots**, including caboose/diner |
| Default min/max | **0 / 4** |
| Min cars | Train waits at the stop until it has at least *min* cars — this is "wait for full load" |
| Auto-manage (default) | At each stop picks the most profitable cars for the next stop, filtered by **Any Cargo / Any Freight / Any Express** |
| Custom consist | Specific car types per stop; "Apply this consist to all stations" overwrites every stop |
| Caboose | −50% breakdown chance; uses a slot |
| Dining car | +20% passenger revenue on that train; uses a slot |
| Copy train | Clones loco, route and consist |
| Replace locomotive | Keeps route and consist |
| Retire | Deletes the train |

```ts
interface Train {
  id: number; owner: CompanyId; loco: LocoTypeId; builtDate: Date;
  priority: 1|2|3|4|5;
  route: { stationId: number; consist: ConsistRule; serviceStop?: boolean }[];
  cars: { cargo: CargoId | 'caboose' | 'diner'; load: number; loadedAt: StationId; loadedDate: Date }[];
  consumables: { water: number; sand: number; oil: number };   // 0–1
  position: TrackPosition; speed: number;
}
type ConsistRule = { mode: 'auto'; filter: 'any'|'freight'|'express'; min: number; max: number }
                 | { mode: 'custom'; cars: (CargoId|'caboose'|'diner')[]; min: number };
```

### 9.4 Reliability, ageing, consumables [D]

- **Breakdown chance** = f(reliability, age, oil level); caboose halves it. Running out of oil greatly increases it.
- **Crash**: a separate rare random event that destroys the train (cheat names confirm crashes and breakdowns are distinct). Not a train-to-train collision **[D/I]**.
- Sandbox: breakdowns/crashes **off by default** with a toggle **[D]**.

| Consumable | Used by | Refilled at | Effect when empty |
|---|---|---|---|
| Water | Steam only | Service tower | Greatly reduced speed |
| Sand | All | Service tower | Greatly reduced grade performance |
| Oil | All | Maintenance facility | Breakdown chance greatly increased; maintenance cost higher |

Proposed numbers **[I]**:
```
breakdownProbPerMonth = base(0.02) × (1 − reliability)×2 × (1 + age/15) × (oil<=0 ? 3 : 1) × (caboose ? 0.5 : 1)
breakdown: train stops for 3–10 days, repair cost = 5% of loco cost
crashProbPerYear = 0.002 × (1 − reliability)×2 × (1 + age/20) × (oil<=0 ? 3 : 1)   // destroys train
maintenance(year) = baseMaint × (1 + 0.04 × ageYears) × (oil<=0 ? 1.5 : 1)
water range: 60–120 miles (steam); sand range: 150 miles of >1% grade; oil range: ~12 months running
outOfWater: speed × 0.25 ; outOfSand: gradeDrop × 3
```

Trains whose route passes a maintenance facility stop automatically when oil is low; service stops can also be added manually **[D]**. Community best practice is a spur with tower + shed at hubs, serviced when empty **[C]**.

---

## 10. Train movement & physics

### 10.1 Documented behaviour [D]

- Top speed depends on number of cars, car type and grade.
- Long, heavy trains are slow and can stall on steep grades.
- Steep grades raise fuel use.
- Wooden bridges force a large slowdown; steel bridges a small one.
- Trains slow on curves (minimum radius) **[C]**.
- Speed matters most for express revenue.

### 10.2 Movement model [I]

RT2 data used top speed, acceleration, a climbing modifier and a "free weight" (load tolerance) rather than horsepower; RT3 almost certainly does the same **[C/I]**. Implement:

```
W        = Σ car_weight(era, carType)                             // loco weight is implicit
loadF    = clamp(1 − kLoad × max(0, W − loco.freeWeightTons) / Wref, 0.2, 1)
gradeF   = clamp(1 − grade% × loco.gradeDropPer1Pct × (W / Wref) × (sand ? 1 : 3), 0.05, 1)
curveF   = min(1, curveRadius / Rcomfort)                         // ≥ min radius by construction
bridgeF  = wood 0.5 | stone 0.9 | steel 0.9 | suspension 0.9
waterF   = steam && water<=0 ? 0.25 : 1
vTarget  = loco.topSpeed × loadF × gradeF × curveF × bridgeF × waterF × (cheatGoGoGo ? 2 : 1)
dv/dt    = (vTarget − v) / loco.accelTime                         // first-order lag
fuelCost = loco.fuelEconomy × distance × (W + locoNominalWeight) × fuelPrice(era, engine, economy)
```

Use the exponential approach (fast rise, levelling off), which matches observed RT2 acceleration curves **[C]**. Wref = 4 cars × current-era freight weight, which reproduces the Add Train chart's default 4-car assumption **[I]**.

Electric locomotives require **every segment between consecutive stations to be electrified**; the route editor must reject otherwise **[D]**.

### 10.3 Meetings, priority and passing [D]

- **No signals, no train-to-train collisions** **[D/C]**.
- When two trains meet on single track, the lower-priority train **stops and turns transparent ("ghosts")** while the other passes through; the waiting train then re-accelerates slowly **[D/C]**.
- Tie-break: equal priority → train with **more valuable cargo** wins **[D]**.
- On another company's track, foreign trains **always yield to the owner's trains** **[D]**.
- **Double track**: trains pass at full speed with no stop **[D]**.
- Trains also fade when entering stations **[C]**.
- Traffic overlay shows congestion in red as a cue to double-track **[C]**.

Implementation **[I]**: detect pairs of opposing trains on the same single-track segment within a look-ahead distance; the loser decelerates to 0 at its current position, waits until the winner has passed its position, then resumes. Same-direction trains: follower caps speed to leader's speed when within a minimum gap (no overtaking on single track; overtaking allowed on double).

---

## 11. Track construction & operations

### 11.1 Geometry [D/C]

- **Free-angle track** laid by dragging; no rigid grid **[C]**. Represent as splines (clothoid or circular-arc segments) over the heightfield **[I]**.
- **Minimum turning radius** limits curves **[C]**. Default min radius ≈ 3 cells **[I]**.
- Live colour feedback while dragging: green on flat ground, red on steep; each segment can show its numeric grade **[D/C]**.
- **Grade overlay**: steepest track red, flattest green **[D]**.
- Obstacles block within 1–3 track lengths; a buffer zone either side of track makes city routing hard **[D/C]**.
- When joining existing track, the final approach should be ≥ ~10 segments **[D]**.
- **Max grade** undocumented. Default cap 6%, with automatic bridge/tunnel insertion governed by the frequency settings **[I]**.
- Laying track auto-terraforms (cut/fill), which costs extra **[C]**.
- **Undo is free and fully refunded until you leave the Track Laying panel** (Ctrl+Z) **[D]**.

### 11.2 Options & cost structure [D relationships / I numbers]

| Item | Documented relationship | Default number [I] |
|---|---|---|
| Single track | cost ∝ length × terrain slope × earthworks | $15k per segment (≈1 cell) at 1850 prices |
| Grade multiplier | low-grade routes cost more (more earthworks) | earthworks = volume of cut/fill × $/m³ |
| Double track | more than single, **less than 2×**; building single then upgrading costs more than building double at once [D] | +70% if built together; +90% as upgrade |
| Electrification | drawn over existing track or "Electrify all track" button; substantial [D/C] | +75% of single-track cost |
| Wood bridge | until 1865, cheapest, **single track only**, big slowdown [D] | 3× per segment |
| Stone bridge | until 1865, dearer than wood, double OK [D/C] | 6× |
| Steel bridge | 1865+, between wood and stone, double OK, small slowdown [D] | 5× |
| Suspension bridge | 1895+, very expensive, auto-used for long water spans [D] | 10× |
| Tunnel | needs long drag and flat portals; "serious cash" [D] | 15× per segment |
| Overpass | grade separation so crossing lines don't stop each other [D] | 4× |
| Bulldoze buildings | costs money; industrial buildings especially [D] | building value × 1.0 |
| Remove own track | little cash, but a book-value write-off [D] | 10% refund |
| Track maintenance | charged on owned track [D] | 2% of build cost per year |

Bridge/tunnel/overpass **frequency** settings: Common / Average (default) / Rarely / Never **[D]**. Bridges: drag roughly perpendicular to water and start about 5× the river width back from the bank; water always gets a bridge **[D/C]**. Scenario events can modify costs (Crossing the Alps: cordite cuts tunnel cost 15% from March 1889) **[C]**.

### 11.3 Track budgets [C]

Some scenarios restrict building with a **track-tile budget** (Central Pacific: 800 tiles at start, +100/year; double track uses 2 per tile; no refund on bulldoze) **[C]**. Implement as an optional per-company counter decremented per segment.

```ts
interface TrackSegment {
  id: number; owner: CompanyId;
  curve: SplineDef; length: number; maxGrade: number; minRadius: number;
  double: boolean; electrified: boolean;
  structure?: 'woodBridge'|'stoneBridge'|'steelBridge'|'suspensionBridge'|'tunnel'|'overpass';
  nodes: [JunctionId, JunctionId];
  buildCost: Money; builtDate: Date;
}
```
The track network is a graph of junctions and segments; routing between stations uses Dijkstra/A* over segments, cost = travel time estimate, with a penalty proportional to the share of foreign track.

---

## 12. Company, player & stock market

### 12.1 Two ledgers [D]

- **Player (chairman)**: personal cash (may go negative on margin) and a share portfolio.
- **Company**: company cash, assets, bonds. Track, stations, trains and industries are paid from company cash.
- Personal panel: Cash, Stock (market value), **Total = net worth**, **Purchasing Power** **[D]**.

```
netWorth        = personalCash + Σ shares_i × price_i        // shorts count negative [D; shorts I]
purchasingPower = personalCash + 0.5 × Σ |longs| × price      // [D]
```

Personal income: board salary, dividends, trading gains, scripted events **[D/C]**. Negative personal cash accrues interest that grows quickly; there is no personal bankruptcy **[C]**. Default personal debt rate = prime + 4% **[I]**.

### 12.2 Founding & chairman [D/C]

- Scenario sets starting money (e.g. Go West!: $250k personal, investors offer $2.5M; Germany: $100k personal, $1.1M company) **[C]**.
- Founding dialog: player investment + outside investment sliders **[I, RT2]**. Initial price $10/share, shares = capital / 10 **[I, RT2]**.
- Founder or takeover winner is **chairman**; can **Resign** (keeps shares) **[D]**.
- **No hired managers in RT3** (unlike RT2) **[I, high confidence]**.
- **Salary**: set by the board, ~$50k/year, hard to raise **[C]**. Proposed: `$50k × clamp(1 + k × fiveYearWeightedReturn, 0.5, 2.0)` **[I]**.
- **Investor sentiment**: investors want rising earnings and share price; **2–3 bad years in a row** cause grumbling; unhappy investors can **oust the chairman**, e.g. after a poor 5-year weighted return **[D]**. A holder of >50% cannot be removed **[C]**. Default: warning after 2 bad years, ouster vote after 3 **[I]**.

### 12.3 Company stock

**Stock tab [D]**: Share Price, Shares Outstanding, Market Cap, Annual Dividend per share, Annual Dividend total. Actions: Issue Stock, Buy Back Stock, Change Dividend, Attempt Merger. Graphs: price, book value/share, revenue/share, EPS, DPS, 5-year weighted return.

**Price drivers [D]**: rises with book value, revenue, earnings, healthy dividends, and in boom times. Heavy buying lifts it temporarily (the effect fades); large sales push it down. A **steady dividend over many years** helps.

**Community observations [C]**: book value per share is the main anchor (12-month average BV/share ≈ next year's starting price); train arrivals move it short-term; cutting dividends in a crash deepens the fall; typical prices $50–100; losers can trade far below book.

**Proposed price formula [I]** (monthly):
```
fundamental = BVps × 0.8
            + max(0, EPS_3yrTrend) × PE(economy)            // PE ≈ 8–12
            + DPS × 6 × min(1, yearsDividendUnbroken / 5)
            + RevPS × 0.1
fundamental *= econMod {Depression .80, Recession .90, Normal 1.0, Prosperity 1.12, Boom 1.25}
price_t = price_{t−1} + (fundamental − price_{t−1}) / 8      // RT2-style 1/8 smoothing [C]
        + tradePressure
tradePressure decays 50% per 6 months; per trade: ± c × sqrt(lotShares / sharesOutstanding) × price
```
Calibration target: one 1,000-share lot on a $50 stock with 100k shares outstanding moves price ≈ $1 **[C]**; impact is sub-linear in size **[C]**.

**Issue / buy back**: issue raises cash near current price, dilutes, usually lowers price; **max twice per year** **[D/C]**; block size 10,000 shares **[I, RT2]**. Buyback uses company cash, raises price and every holder's ownership share, reduces book value **[D/C]**.

**Splits [C]**: price too high for several months or dividend low relative to profit → 2:1 or 3:1. Trigger **[I]**: price > $120 for 3 months → 2:1; > $200 → 3:1.

**Dividends**: annual per-share amount paid quarterly **[I, RT2]**; auto-drops to zero if unaffordable, which angers investors **[C]**.

### 12.4 Bonds & credit rating

| Item | Value |
|---|---|
| Bond face value | **$500,000** [C] |
| Issue cost | **2%** ($10,000) [C] |
| Early repayment | allowed, ~2% penalty [C] |
| Minimum rating to issue | **B or better** [C] |
| Max outstanding | **20 bonds ($10M)** [C] |
| Interest paid | **end of every quarter** [D] |
| Rate | prime (from economy) + spread by rating [C/I] |
| Maturity | exists, length undocumented → **30 years** [I] |

Rating drivers **[C]**: asset value, bonds outstanding (each new bond lowers it), annual profit; improves over time; events/territories can grant +1.

Proposed model **[I]**:
```
grades = [A+, A, A-, B+, B, B-, C+, C, C-, D]      // RT2-style; B or better can issue
score  = w1·log(assets/debt) + w2·min(5, operatingProfit/annualInterest)
       + w3·profitTrend3yr − w4·bondsOutstanding − w5·(yearsSinceBankruptcy < 10)
spread = A+ 0% · A 0.5% · A- 1% · B+ 2% · B 3% · B- 4%
```

**Bankruptcy [D]**: voluntary button, last resort. Effects: **all debt halved**, credit rating ruined (no new bonds), **bondholders receive new shares** (dilution, price falls). Gate: only after ≥2 consecutive loss years or unpaid interest **[C/I]**; rating floor D for 5 years and no repeat within 10 years **[I]**.

### 12.5 Player as investor [D unless marked]

- Stock Market panel lists every company: share price, shares owned, share value; Buy / Sell / View.
- Lots: click = 1,000 shares; Shift/Ctrl-click = 5,000 **[C]**.
- **Margin**: buying beyond cash drives personal cash negative; interest accrues **[D/C]**.
- **Margin call**: if purchasing power < 0, the broker **force-sells** shares in 1,000-share lots until PP ≥ 0; the player cannot stop it; forced selling depresses price and can cascade **[D/C]**.
- **Short selling**: borrow and sell; holding goes negative; **cannot short your own company**; cap ≈ 50% of net worth **[D concept / C limits]**.
- Brokerage: ~1% per trade **[I]**.
- Net-worth and medal checks run at **month end**, so a pumped price at month end counts **[C]** — keep this (players expect it) or make it a setting.

### 12.6 Takeovers & mergers

- **Attempt Takeover** (on a rival's Overview): if you control enough stock you replace its chairman **[D]**.
- **Attempt Merger** (Stock tab): acquirer's company cash buys out the target's other shareholders at an offered price; shareholders vote **[D]**.
- **>50%** of target shares forces the result; with less you must offer a premium (40% held → need ~10% more) **[C]**. Failed attempt: retry only after **1 year** **[C]**.
- Target's track, stations, trains, industries, cash **and all bonds** transfer to the acquirer **[C]**.

Vote model **[I]**:
```
yes = acquirerShares + chairmanShares
    + Σ_otherHolders shares × sigmoid((offer/marketPrice − 1.10) × 12)    // AI holders adjust by personality
pass if yes > 50% of outstanding
cost = offer × (outstanding − shares already held by acquirer)
```

### 12.7 Ledger & reports [D headers / I line items]

Ledger opens automatically at fiscal year end and on demand (L). Tabs: **Company Overview** (revenue, profit, load-miles, revenue/load, average train speed, investor sentiment, salary status), **Company List**, **Player List** (holdings, historical bios), **Game Status** (medal progress, link to briefing) **[D]**. Sub-reports: Income Statement (I), Balance Sheet (B), Haulage Report, Stock Report, Train List, Station List, Industry List, Cargo List; 10 years of history **[C]**.

- **Income statement**: Revenue (freight, passengers, mail, troops, station buildings, industry profit, trackage income) − Expenses (train maintenance, fuel, track maintenance, station/building overhead, trackage paid, industry costs, territory fees) − Interest = Net profit; Dividends; Retained earnings **[I]**.
- **Balance sheet**: Cash, Track, Stations & buildings, Rolling stock, Industries, Shares in other companies − Bonds = Book value; BV/share **[D partial / I]**.
- Taxes: **none** in RT3 as far as any source shows **[I]**.

---

## 13. AI rivals

### 13.1 Documented / observed

- Opponents are historical tycoons whose AI "tends to act like" the person, each with a short bio **[D]**. Named: Cecil Rhodes (relentless expansion), Jay Gould (stock speculator) **[D]**; J.P. Morgan, William Wheelwright, General Kodama Gentaro **[C]**.
- Number of AIs is set per scenario/map up to a per-map maximum **[D]**.
- Observed behaviour **[C]**: builds small networks (2 cities + a few industries), rarely expands; sometimes builds "track to nowhere"; borrows to pay bills and often goes bankrupt; uses the player's track; manages trains decently; buys back large blocks (~100k shares); may issue stock or split when the player accumulates it. Widely considered weak.

### 13.2 Design for the clone [I]

Keep personality flavour, but make the AI competent — this is the most common complaint about the original.

```ts
interface AIPersonality {
  expansionAppetite: number;   // Rhodes high
  leverageTolerance: number;   // bonds willingness; Morgan low
  dividendPolicy: number;
  speculation: number;         // trading, margin, shorting rivals; Gould high
  takeoverAggression: number;  // Gould, Morgan high
  industryInvestment: number;
  recessionCaution: number;
}
```

Decision loop (monthly):
1. **Route planner**: score candidate city pairs and industry→consumer pairs by (expected revenue from price field − build cost estimate) / build cost; build the best that fits budget × expansionAppetite.
2. **Train manager**: add trains where station waiting cargo exceeds capacity; replace locos above age 25; add service towers/maintenance where consumables run out.
3. **Finance**: issue bonds when ROI of best project > rate + margin × (1 − leverageTolerance); repay when cash-rich; set dividend per policy.
4. **Market**: trade rival stocks on value (price vs fundamental) scaled by speculation; launch takeovers when owning >35% and target price < BVps.
5. Difficulty applies AI finance multipliers (§8.4).

---

## 14. Scenarios, goals, events & editor

### 14.1 Game modes [D]

Main menu: Tutorial · Single Player · Multiplayer · High Scores · Settings · Extras (Map Editor, Credits, Readme) · Exit.

| Mode | Rules |
|---|---|
| **Campaign** | 16 scenarios in a "railroad museum": 4 rooms (American 5, European 5, World 3, Futuristic 3 [C]); finishing a room's cases opens the next. Difficulty Easy/Normal/Hard chosen once, locked. Bronze or better on all 16 unlocks a bonus [C]. |
| **Scenario** | Setup: Map, Start Date (some maps), Skill (Easy/Medium/Hard/Expert), number of AI players (up to per-map max), time of day, weather, any-era locos. |
| **Sandbox** | Finance off; free build; terraform, plant trees, paint ground. Options: time of day, weather, any-era locos, allow breakdowns/crashes (off by default), disable building/city generation. Changeable mid-game. |
| **Multiplayer** | Manual: up to 8 players over LAN/Internet [D]; PCGamingWiki says 4 [C]. Chat (Space), pings (Ctrl+N), shared game speed. |

Load screens list only saves of their own mode **[D]**.

### 14.2 Campaign scenarios [C unless noted]

| # | Scenario | Years | Region | Bronze | Silver | Gold | Restrictions / key events |
|---|---|---|---|---|---|---|---|
| 1 | Go West! | 1840–1866 | NE USA | Boston–Buffalo by 1866 | …by 1861 | …by 1856 | $250k personal; investors $2.5M; no unconnected track; can't resign. 1848 gold rush +50% passengers; Baldwin ~1846 |
| 2 | Germantown, U.S.A. | 1850–1880 | Midwest | St Louis–Cleveland + $10M BV | + $25M BV + $5M industry profit | + $40M BV + $10M industry profit + only surviving railroad | One company; 4 AIs |
| 3 | Central Pacific | 1850–1874 | CA/NV/UT | Sacramento–Salt Lake City | + $20M BV | + 10 troop loads SF→SLC | 800-tile budget +100/yr; no refund; Civil War raises weapon/ammo prices; Consolidation ~1860 |
| 4 | Texas Tea | 1888–1917 | TX/OK/NM | $15M company cash + 50 oil loads | + 30 coffee loads from Mexico | + $15M personal NW | Mexico access $10M (→$50k offer); Beaumont oil event |
| 5 | The War Effort (puzzle) | 11–15 Dec 1941 | US East Coast | 10 each weapons/ammo/fuel to Norfolk or NYC | 15 each + 5 clothing | 15 each + 10 each clothing/meat/cheese | No buying; 1 starting train; a cargo counts only if all goes to one port |
| 6 | State of Germany | 1848–1877 | Germany | 6 states | 10 states + $8M PNW | 13 states + $15M PNW within 25 yrs | Territories: minor $600k–$2M, major $10M; start-territory modifiers |
| 7 | The Flying Scotsman | 1840–1865 | GB & Ireland | London–Edinburgh by 1865 | + lifetime avg loco speed > 12 mph | + only railroad in business | Irish famine event |
| 8 | Crossing the Alps | 1875–1910 | IT/AT/CH/Bavaria | Milan–Zürich by 1910 | + Munich + Venice | + 30 weapons loads to Munich | Territories; cordite −15% tunnels (1889) |
| 9 | The Third Republic | 1871–1896 | France | not recovered | | | |
| 10 | The Orient Express | 1880–1914 | Balkans | not recovered (avg-speed goal reported) | | | |
| 11 | Argentina | 1880–1909 | Argentina | not recovered | | | |
| 12 | Rhodes Unfinished | 1902–1933 | East Africa | partial ($35M company value mentioned) | | | Zanzibar connection bonus ~$2M before 1910 |
| 13 | Japan Quakes | 1964–1985 | Japan | partial (50 clothing loads) | | | Earthquakes damage track |
| 14 | The Seeder (puzzle) | ~2020 | Greenland | "green" the largest area; 250 build units | | | Research stations |
| 15 | Dutchlantis | ~2021–2051 | Flooded W. Europe | Camelot–Britain tunnel; reach Munich | | | Cheese bonus $300k/yr |
| 16 | A Chip Off the Old Block | 2050–2080 | Post-quake California | $20M PNW reported | | | $1M connection subsidies; Angels City deadline |

**Coast to Coast scenarios [D names]**: Alternate USA, Coast to Coast, East of the Mississippi, Eastern China, Ireland, Louisiana, Mexico, Pacific Coastal, Pacific Northwest, Poland, Russia, Southern Pacific, Spanish Mainline. Start years and medals not recovered.

Since you are building a clone, you'll write your own scenarios anyway — use these as templates for goal mix and pacing rather than content to reproduce.

### 14.3 Goal & restriction model [I, derived from D/C examples]

Medal tiers are nested AND-sets of goals with a deadline; Silver ⊇ Bronze, Gold ⊇ Silver. Checked at month end **[C]**. Progress shown on the Game Status tab **[D]**.

```ts
type Goal =
  | { kind: 'connectCities'; cities: CityId[] }
  | { kind: 'connectTerritories'; count: number }
  | { kind: 'companyBookValue' | 'companyCash' | 'personalNetWorth' | 'lifetimeIndustryProfit' | 'companyRevenue'; atLeast: Money }
  | { kind: 'deliverCargo'; cargo: CargoId; loads: number; from?: Ref; to?: Ref }
  | { kind: 'avgLocoSpeed'; atLeastMph: number }
  | { kind: 'onlySurvivingRailroad' } | { kind: 'highestCompanyValue' }
  | { kind: 'variable'; scope: Scope; name: string; atLeast: number };

interface MedalTier { goals: Goal[]; deadline: Date }
interface Restrictions {
  noUnconnectedTrack?: boolean; oneCompanyOnly?: boolean; cannotResign?: boolean;
  trackTileBudget?: { initial: number; perYear: number; doubleCostsTwo: true; refundOnBulldoze: false };
  noPurchase?: ('track'|'stations'|'trains')[];
}
```
Score **[I]**: `medalBase {B 1, S 2, G 3} × difficultyMult {E .75, M 1, H 1.25, X 1.5} × (1 + yearsEarly × 0.05)`.

### 14.4 Events / triggers [C]

- Event = **conditions + effects**, optional temporary duration (e.g. +1 to a variable for 10 months); triggers evaluated in order **[C]**.
- **Variables** in 4 scopes: game, player, company, territory — mostly used as counters **[C]**.
- Entities referenced by numeric IDs (territories, cities, chairmen, locos, cargos) **[C]**.
- The editor validates events and flags errors **[C]**.
- Effect catalogue seen in scenarios **[C]**: cash grants (player/company), cost modifiers (tunnel −15%), cargo price changes, passenger demand multiplier, loco availability/offers, territory-access offers with conditions and deadlines, track-tile grants, disasters (earthquake damage, sabotage), production changes, newspaper/dialog popups with accept/decline, and **New Briefing Text** (added by C2C **[D]**).

```ts
interface ScenarioEvent {
  id: string; oneShot: boolean; repeatEveryMonths?: number;
  triggers: Condition[];               // date, variable compare, city connected, cargo delivered, cash, territory owned, random chance…
  effects: Effect[];                   // see catalogue above
  durationMonths?: number;             // effects auto-revert
  ui?: { kind: 'newspaper'|'dialog'|'choice'; headline: string; body: string; choices?: { label: string; effects: Effect[] }[] };
}
```

### 14.5 Map editor [C — manual ch.12 not yet read]

- Access: Main Menu → Extras → Map Editor; in-game Shift+E **[D/C]**.
- Create flat map of chosen size or import a **TGA heightmap** with height multiplier, mountain-top modifier and smoothing factor **[C]**.
- Tools: raise/lower, flatten to level, add water, paint rivers, paint ground colour, plant trees by species, place cities (small/medium/large), industries and landmark buildings **[C]**.
- Economy settings: economy seed, economy scale tuned to start year, industry weightings **[C]**.
- Territories: draw, name, set access cost and modifiers **[D/C]**.
- Scenario panel: start/end dates, starting money, AI count and chairmen, enabled industries/locos per region and year, events, medal goals, briefing text **[C/I]**.

---

## 15. User interface

### 15.1 Screen layout [D]

1. **Main window** — 3D view.
2. **Radar** (bottom-left minimap): cities as white dots; click to jump, drag to pan; date box below.
3. **Button panel** — 9 primary buttons + a strip of 8 (game-speed controls, View Ledger, File Options).
4. **Dynamic panel** — context panel for the selected tool or object.
5. **Hover help bar** — tooltip text and the **cost of the pending action**.

### 15.2 Primary buttons [D]

Lay Track · Build Station · Add Train · View Companies · View Stations · View Trains · Company Detail · Stock Market · Overview Mode.

### 15.3 Overlays (Overview mode) [D]

| Key | Overlay |
|---|---|
| F1 | Cargo Supply — per-cargo price heatmap, carload icons; supply ▲ / demand ▼ markers |
| F2 | Holdings by company |
| F3 | Profitability (green/yellow/red per train or station) |
| F4 | Track grade |
| F5 | Traffic density (red = needs double track) |

### 15.4 Panels [D]

- **Lists** (companies/stations/trains): double-click opens detail and moves camera; PgUp/PgDn cycles.
- **Train detail (instrument panel)**: oil/water/sand gauges, breakdown-chance meter, cost window, route list (add/remove stops, change cargo), route status bar with warning icons, priority, copy, route map, age, follow-cam, retire, replace locomotive.
- **Add Train / consist editor**: loco list filtered by date/region with the §9.1 stats and speed-vs-grade chart (freight/express/mixed toggle); auto-manage vs custom; min/max cars; caboose; diner; "apply to all stations".
- **Station detail**: radius, cargo supply/demand/prices, waiting cargo, upgrade, adjacent buildings.
- **Company Detail** — 4 tabs: Overview (revenue/expenses/interest/profit, Resign, Attempt Takeover, Change Dividend), Bonds (debt, rating, prime rate, Issue Bonds, Declare Bankruptcy), Stock (price, shares, market cap, dividend, Issue/Buy Back, Attempt Merger), Territories.
- **Stock Market**: cash, stock value, purchasing power; company list with Buy/Sell/View chart.
- **Ledger** (L): Company Overview, Company List, Player List, Game Status + sub-reports (§12.7).
- **File Options**: Load, Save, Briefing, Settings, Resign/Main Menu, Quit.
- **Newspaper & messages**: N last newspaper; M / Shift+M message history; D last dialog; event dialogs can offer choices.

### 15.5 Build tools [D]

- **Track**: drag to lay; Esc cancel; Ctrl+Z free undo until leaving the panel; bulldoze; single/double; bridge type; bridge/tunnel/overpass frequency; Electrify all track.
- **Station**: S/M/L; rotate `[` `]`; locked mode (place unconnected).
- **Support buildings**: service tower, maintenance facility.
- **Other buildings**: post office, restaurant, hotel, tavern, industries (buy/build/upgrade). Non-station buildings banned on steep slopes (v1.04) **[D]**.

### 15.6 Camera [D/C]

Edge-scroll, arrow keys, middle-drag, or radar to pan; wheel to zoom; right-drag to rotate; Shift+arrows zoom/rotate. Two modes: on-screen zoom/rotate buttons, or "Free – Any Angle". Scroll Lock toggles free/locked. Bookmarks: Shift+0–9 store, 0–9 recall.

### 15.7 Hotkeys [C]

| Key | Action | Key | Action |
|---|---|---|---|
| F1–F5 | Overlays | F6 | Settings |
| F7 / F8 | Save / Load | Shift+F7 / F8 | Quick save / load |
| + / − / Pause | Speed | Ctrl+L / T / S | Lay track / Buy train / Build station |
| Ctrl+Z | Undo track | [ / ] | Rotate station |
| K | Stock market | C / Shift+C | Company list / detail |
| T / Shift+T | Train list / detail | S / Shift+S | Station list / detail |
| PgUp / PgDn | Prev / next entity | L / I / B / A | Ledger / Income / Balance / Game status |
| N | Newspaper | M / Shift+M | Prev / next message |
| D | Last dialog | F | Find city |
| G / Shift+G | Grid / grade overlay | H / Shift+H | Hide trees / trees+stumps |
| Shift+N | City names | Shift+E | Editor panel |
| Tab / Shift+Tab | Hide bottom UI / all UI | W | Whistle |
| 0–9 / Shift+0–9 | Camera recall / store | . (period) | Debug console (cheats) |

### 15.8 Debug console (mirrors original cheats) [C]

Implement as a dev console: +$10M / +$1M player, +$10M / +$1M company, all territories, 2× train speed, no crashes, crash all trains, all locos available, 2× production, force win at tier, force loss, force breakdown/crash of selected train. Gate behind a setting and flag the save as "assisted".

---

## 16. Data files & save format [I]

Original formats for reference **[C]**: cargo `.cty` (Data\Types, max 52), buildings `.bty`/`.bca`, rolling stock `.car`, strings `RT3.lng`, maps/scenarios `.gmp` (in Maps folder), saves in `Saved Games` (extension unconfirmed).

Clone layout:
```
content/
  cargo.json            // §4.1
  industries.json       // §6
  locomotives.json      // §9.2 — fill from calibration
  buildings.json        // stations, towers, hotels…
  economy.json          // §5.5 state table, price-field constants
  difficulty.json       // §8.4
  balance.json          // every [C]/[I] constant in this spec
maps/<name>.map.zip     // heightmap, terrain, cities, industries, territories
scenarios/<name>.json   // map ref, dates, money, AIs, goals, restrictions, events, briefing
saves/<name>.save.zip   // scenario + full sim state + RNG state + command log (for replay/debug)
```
Version every file; the sim must refuse or migrate mismatched versions.

---

## 17. Open items & calibration plan

These are the gaps no public source fills. The fastest way to close them is in-game measurement on a Steam/GOG copy, or the community data packs listed below.

| # | Gap | How to close it |
|---|---|---|
| 1 | **Per-locomotive numbers** (speed, accel, grade, cost, maintenance, reliability, appeal, years) | Fandom page "Specifications of locomotives in Railroad Tycoon 3"; MagnusA's `RT3_Locomotive_List_v1.06.zip` (FED2k forum / Hawk & Badger RT3 Extras). Or screenshot the Add Train panel for each loco. |
| 2 | Industry recipes & rates | Magnus Adielsson's RT3 Industry Chart and Kurt's RT3 Cargo Chart 1.05 (hawkdawg.com, login); or dump `.bca` files with the community Hex Editing Building Planner |
| 3 | Track/bridge/tunnel/electrification prices | Lay 10 segments flat, at 2% and at 4% in a fixed year; read the hover-help cost |
| 4 | Speed-vs-grade | Screenshot the Add Train chart for 5–6 representative locos; fit `gradeDropPer1Pct` and `freeWeightTons` |
| 5 | Station catchment radii | Place each size on flat ground and count highlighted cells |
| 6 | Real seconds per game day | Stopwatch one month at each speed |
| 7 | Map scale | Time a known loco over a measured distance |
| 8 | Stock price, credit rating, bond spreads, salary | Log monthly values over a few game years and regress against BV, EPS, DPS |
| 9 | Campaign medals for scenarios 9–16, C2C start years | StrategyWiki / Fandom scenario pages, or play-throughs |
| 10 | Manual chapters 11–12 (Multiplayer, Map Editor) | Download the Steam manual PDF and read pp.78–93 |

Build order recommendation:
1. Heightfield + free-angle track + stations + one train moving between two stations (§3, §9–11).
2. Price field with 3–4 cargos and middleman flow, plus F1 overlay (§5) — validate the "narrowing margin" and "1–2 year adjustment" behaviours before anything else.
3. Freight revenue, consists, consumables (§8, §9).
4. Express units, passenger revenue (§8.3).
5. Company finance, bonds, stock market (§12).
6. Scenario goals and events (§14).
7. AI (§13), then editor, then polish.

---

## 18. Sources

- Official RT3 manual (Steam): https://store.steampowered.com/manual/7610 → https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/7610/manuals/manual_en.pdf
- v1.04 / Coast to Coast patch notes (ModDB): https://www.moddb.com/games/railroad-tycoon-3/downloads/railroad-tycoon-3-v104-patch-coast-to-coast-exp
- GameSpot developer Q&A: https://www.gamespot.com/articles/railroad-tycoon-3-qanda/1100-6076739/
- Wikipedia: https://en.wikipedia.org/wiki/Railroad_Tycoon_3
- StrategyWiki: https://strategywiki.org/wiki/Railroad_Tycoon_3/Gameplay
- GameFAQs (Zoogz guide): https://gamefaqs.gamespot.com/pc/534361-railroad-tycoon-3/faqs/49084
- Cyberly FAQ mirror: https://www.cyberly.org/game-cheats/pc-walkthroughs/railroad-tycoon-3.html
- Gaming Nexus review: https://www.gamingnexus.com/Article/380/Railroad-Tycoon-3
- Hawk & Badger / Heineken & Pacific strategy: https://hawkdawg.com/hp/strategy.php · https://hawkdawg.com/rrt/rrt3_extras/rrt3_utilities.php
- Kurt's RT Fan Station: https://www.techtourguide.com/kurtdvich/Kurt's_Fan_Station_Reference.htm
- rrtycoon blog (cargo slots, .cty, v1.06): https://rrtycoon.blogspot.com/2024/10/redesigning-cargo-economy-models-for-rr.html
- Steam routing guide: https://steamcommunity.com/sharedfiles/filedetails/?id=284155250
- FED2k forums: https://forum.dune2k.com/topic/22464-stock-market/ · https://forum.dune2k.com/topic/22751-announcement-updated-industry-charts-and-locomotive-lists/
- StopGame walkthrough (RU): https://stopgame.ru/show/3314/railroad_tycoon_3_prohozhdenie/p2
- The Raven's Call retrospective: https://theravenscall.substack.com/p/railroad-tycoon-iii-and-the-dire
- Codex Gamicus scenario list: https://gamicus.fandom.com/wiki/Railroad_Tycoon_3
- PCGamingWiki: https://www.pcgamingwiki.com/wiki/Railroad_Tycoon_3
- RT2 manual (used where RT3 is silent): https://ia601008.us.archive.org/32/items/railroad-tycoon-2-manual/Railroad%20Tycoon%202%20-%20Manual.pdf
- Locomotive specs (not reachable from here, recommended): https://railroad-tycoon.fandom.com/wiki/Specifications_of_locomotives_in_Railroad_Tycoon_3
