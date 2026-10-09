# Gap analysis against the RT3 clone spec

[rt3-clone-spec.md](rt3-clone-spec.md) is the primary reference. This page
records, section by section, how the implementation compares with it.
Spec tags: **[D]** documented (implement exactly), **[C]** community
(default, configurable), **[I]** inferred (tunable placeholder).

Last reviewed against spec v1.0 (9 October 2026).

## Brought into line with the spec

| Spec | Rule | Was | Now |
|---|---|---|---|
| §12.4 [D] | Bond interest paid quarterly | monthly | quarterly, with dividends |
| §12.4 [C] | At most 20 bonds; ~2% early-repayment penalty | no cap, repaid at par | both; 30-year maturity [I] repaid at par |
| §12.4 [C] | Each bond lowers the rating, yet 20 are possible | −1 notch per bond (max ~6) | −1 per bond while unproven, −1 per 4 once proven |
| §12.2 [C] | Salary ~$50K | $24K | $50K |
| §10.3 [D] | Equal priority: more valuable cargo wins the meet | older train | cargo value, then older train |
| §10.1 [D] | Wood bridges slow trains a lot, steel a little | no effect | wood 50%, others 90% [I] |
| §11.2 [D] | Wood and stone bridges until 1865, steel from 1865, suspension from 1895 (auto for long water spans) | wood/stone always, steel from 1870, no suspension | as documented; spans ≥ 2 km get suspension [I] |
| §11.2 [D] | Double track costs more than single but less than 2× | 2× | 1.7× [I] |
| §11.2 [I] | Structure cost multiples: wood 3×, steel 5×, stone 6×, suspension 10×, tunnel 15× | 4×, 6.4×, 8×, —, 10× | as the spec |
| §9.4 [D] | Crashes: a rare event that destroys the train; off in sandbox | not modelled | spec's [I] rate; wreck written off the books |
| §8.4 [D] | Difficulty revenue: Easy +20%, Medium 0, Hard −10%, Expert −20%; Easy cuts costs | none | as documented; Easy −15% running costs [I] |
| §8.4 [D] | Station age: +15% new, 0% at 4 years, −10% at 20+; counted from the town's first station; open country half | none | as documented, linear between points [I] |
| §8.2 [I] | Timeliness exp(−0.0023 × S × days); freight expires below 10% | linear 0.5%/day × S | the spec's curve (deterministic integer table) |
| §6.3 [C] | Refinery: oil → diesel; rubber plantation (1900) | missing (rubber had no source) | added |
| §6.3 [C] | Houses take milk only until ~1890 | always | until 1890 |
| §2 [D] | Every game starts paused | started running | starts paused |
| §5.1, §5.3 [D] | Middlemen cheap along water and coasts, slower over mountains; prices flat along coasts, steep across ranges | uniform | per-cell conductance from terrain weights price coupling and drift cost and speed [I numbers] |
| §5.5 [C] | Five economic states, checked twice a year; production, costs, prime rate, share prices | none | as the spec's table; transitions as its [I] matrix |
| §8.5 [D] | Any company may run over a rival's track and use its stations; the owner gets the share of revenue matching the distance on its track; the runner pays all fuel | one company | as documented, measured per leg [I] ([rivals-model.md](rivals-model.md)) |
| §12.1, §12.5 [D] | Net worth and purchasing power across every company; trading any company | one company | portfolios across companies; margin call sells the most valuable holding first [I] |
| §13 [D/C/I] | Historical tycoons as AI rivals with personalities, building and running lines | none | seven tycoons; monthly route planner, train manager, finance and speculation |
| §12.5 [D/C] | Short selling: not your own company, capped at ~50% of net worth | none | as documented; shorts held at 150% against purchasing power, pay dividends, can be margin-called [I] |
| §12.6 [D/C/I] | Takeovers and mergers by shareholder vote; >50% forces it; retry after a year; everything incl. bonds transfers | none | as documented; vote models [I] in [rivals-model.md](rivals-model.md) |
| §12.2 [D/C/I] | Investor sentiment; warning after 2 bad years, ouster vote after 3; >50% cannot be removed; resign keeps shares; salary from 5-year weighted return | none | as the spec's defaults; successor rules ours |
| §12.3 [C/I] | Splits: > $120 for 3 months → 2:1, > $200 → 3:1 | none | as the spec |
| §12.4 [D/C/I] | Voluntary bankruptcy: debt halved, bondholders paid in shares, rating ruined; after 2 loss years or unpaid bills; no repeat in 10 years | none | as the spec |
| §12.2, §12.5 [I] | Founding dialog: player and outside investment, shares at $10; brokerage ~1% | fixed opening; no fees | as the spec |
| §6.1–6.2 [D/C/I] | Industry ownership: buy at ~10× profit or a floor, build processors at ~150%, upgrade to double capacity, closures | none | as the spec; accounts at base prices [I] (see m2-economy-model.md) |
| §3.2, §6.4 [D/C/I] | Towns grow with service, rail hubs faster; new houses near houses and stations; 1–5 stars | fixed towns | the spec's proposed formula, monthly; see m2-economy-model.md |
| §6.3 [C] | Ports import/export at the coast or edge; receive, supply or exchange; unowned receivers upgrade on demand | none | as described |
| §6.3 [D/C] | Warehouses: inland ports that cut spoilage and earn the loss they save | none | as described in m2-economy-model.md; spoilage rate and radius [I] |
| §6.1 [C] | Industries appear from their start year, and new ones as the economy develops | fixed at map creation | yearly: up to the usual count, then by chance |
| §6.1 [C] | Industries slow or stop when their output sells cheaply nearby | constant output | supply price falls with unsold stock; output paced by it [I] |
| §7.2 [D/C] | Post office, hotel, restaurant, tavern: fixed per-station market split by distance; post office and hotel slow waiting loss | none | as the spec; costs and rates [I] |
| §6.1 [C] | Production about 2.2 loads a year for raw producers, about 3 for processors | 24 and 36 | as the spec; saturation, express and town growth recalibrated to match ([calibration.md](calibration.md)) |
| §12.3 [C/I] | Share price: 0.8 × book, EPS trend × P/E by economy, dividend weighted by an unbroken record, 0.1 × revenue; ⅛ smoothing; trade pressure ∝ √size that decays | 0.6 × book + 8 × EPS + 10 × dividend, ¼ smoothing, permanent linear impact | as the spec; EPS trend read as a 3:2:1 weighting ([m3-finance-model.md](m3-finance-model.md)) |
| §12.4 [C/I] | Ten grades A+ to D from a score of asset cover, interest cover, profit trend, bonds and bankruptcy; rate = prime + spread | eight grades from debt ÷ assets, bond notches, a rate table | as the spec; weights and thresholds ours |
| §9.4, §11.2 [I] | Maintenance × (1 + 4% per year of age), × 1.5 with no oil; breakdowns × (1 + age/15); track upkeep 2% of cost a year | linear to 3× at 20 years, ×2 on low oil; no breakdown ageing; 6% a year | as the spec; building upkeep stays 6% (no figure in the spec) |
| §2, §15.7 [D/C/I] | Six speeds (Paused to Very Fast) on `+`, `−` and Pause; ~4 minutes a game year at Normal, ×2 per step | three speeds on 1–3; `+`/`−` traded shares | as the spec; timings in `balance.json` (`time`); shares trade on A/S; Space also pauses |
| §3.1, §5.2, §7.1 [I] | 0.5-mile cells; Small/Medium/Large maps of 256 × 256, 384 × 512, 768 × 1024; economy nodes every few cells (~15,000 [D]); catchment radius 2/3/4 cells | 1 km cells, one node per cell, 128 × 128 maps, radii 1/2/3 | as the spec; 2 × 2-cell nodes on Small, 4 × 4 on Medium, 7 × 7 on Large; map counts scale with area |
| §5.3 [I] | Equilibrium price base × ((D+ε)/(S+ε))^0.5 within 0.3–3×; relaxation over months; middleman flow ∝ gap × stock × conductance; conductance-weighted coupling | screened-Poisson field pinned at sites (consumers 150%, producers 50%, neutral 50%); stock to the single best neighbour | as the spec, except site nodes are not smoothed (it would flatten every market; see m2-economy-model.md); calibrated to the spec's [C] middleman speeds and reshaping time |
| §0 rule | Every [C] and [I] number in a data file | compiled `provisional` constants | `data/balance.json`, validated on load ([balance.md](balance.md)) |

## Matches already

- §1 [I] architecture: deterministic integer simulation, seeded RNG,
  command pattern for player actions.
- §4.1 [D] cargo table (41 types, prices in $K, sensitivity 1–10).
- §5.1 [D] price field on ~15,000 nodes with price and inventory per freight
  cargo; express kept out of the field; revenue = destination − origin price.
- §5.4 [D] cargo supply overlay, red cheap to green dear.
- §7.1 [C] station prices $50K/$100K/$200K; size changes only cost and radius [D].
- §7.1 [D] freight won't board for a stop where it sells for less.
- §7.2 [D] service tower (water, sand) and maintenance facility (oil), with automatic stops when low.
- §9.3 [D] 8 car slots; automatic consist picks the most profitable cars.
- §9.4 [D] consumables and their effects; sandbox switches breakdowns and crashes off.
- §10.3 [D] no signals or collisions; lower priority yields; double track passes freely.
- §12.1 [D] two ledgers; purchasing power = cash + 0.5 × holdings; net worth.
- §12.3 [D/C] issue at most twice a year; buybacks; dividends paid quarterly
  and cut if unaffordable; one 1,000-share lot on a $50 stock with 100K
  shares moves the price about $1 [C], which our square-root impact
  reproduces.
- §12.5 [D/C] 1,000-share lots (5,000 with a modifier); a margin call
  force-sells whole lots, in one trade, until purchasing power is positive.

## Our design differs from the spec's [C]/[I] proposal: decisions needed

None open. Where we adapted a proposal to make it work, the model pages
say why (for example the price field's site nodes, m2-economy-model.md).

## Missing

Grouped by the spec's own build order (§17).

**Trains and track**
- Era car weights (§4.2 [D]) and the speed model with free weight,
  acceleration and appeal (§9.1 [D], §10.2 [I]). Needs per-locomotive
  numbers (§17 item 1).
- Consist rules (§9.3 [D]): min/max cars (default 0/4) and waiting for a
  full load; Any/Freight/Express filters; custom consists per stop;
  caboose (−50% breakdowns); dining car (+20% passengers); copy, replace and
  retire.
- Electrification, and electric engines needing it (§10.2, §11.2 [D]).
- Free undo while laying track; removing track (10% refund); overpasses
  (§11.1–11.2 [D]).
- Curve slowdown; following trains on single track (§10 [C/I]).
- Breakdown repair cost and duration (§9.4 [I]).

**Economy**
- Rivers in the map generator, and closed borders (conductance 0) (§5.3).
- Automobile demand dropping sharply in recessions; the optional cost index (§5.5).
- Owning ports; warehouse cargo conversion (§6.3 [D/C]).
- Industry and demand-block footprints of 4 × 4 cells (§3.1 [C]); ours sit
  on one economy node (2 × 2 cells on the Small map).

**Express**
- Units with origin and destination houses chosen by gravity weighting;
  transfers via hubs; won't board rarely served stops; travellers who
  give up avoid rail for a while (§8.3 [D/I]).
- Fare speed factor, locomotive appeal and dining car (§8.3 [D]).

**Company and market**
- AI: replacing engines, recession caution, AI
  difficulty multipliers (§13.2, §8.4).

**Scenarios and presentation**
- Territories and access rights (§3.3 [D]).
- Scenario goals, medals, events, track budgets (§11.3, §14 [C/I]).
- Ledger reports, lists, overlays F2–F5, radar, 3D camera (§15 [D]).
- Save/load and content packaging (§16 [I]).
