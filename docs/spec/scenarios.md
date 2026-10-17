# Scenarios

A scenario is a JSON file in `data/scenarios/`. It sets up a new game:
start date, map, rivals, territories, founding money and named towns. It
also sets the Bronze, Silver and Gold goals with their deadlines
(rt3-clone-spec §14.3). Run one with
`./build/src/client/railmaster --scenario=data/scenarios/lowland_junction.json`.
F10 shows the briefing and progress.

The three shipped scenarios are original. Their places, briefings and goals
are our own, built on the patterns of RT3's goals: connect named towns,
deliver cargo, reach a book value, win the market.

| File | Start | Goals | Events |
|---|---|---|---|
| `lowland_junction.json` | 1850, no rivals, no unconnected track | Connect two towns, then four; book value; revenue | A frozen canal lifts passenger and mail demand; a council subsidy for the first connection; a new briefing once Corwin is joined |
| `coal_country.json` | 1880, two rivals, a track budget of 500 cells + 80 a year | Deliver coal; revenue; be the most valuable railroad | A steel boom raises coal prices; occasional miners' strikes halve mine output; an early heavy freight engine once coal moves |
| `six_provinces.json` | 1900, one rival, six territories, smaller founding money, chair locked, one company only | Stations in 3 and then 5 territories; personal net worth; be the only railroad | Expansion fever cuts building costs for two years; a board bonus for reaching a second province |

## Format

```json
{
  "key": "lowland_junction",
  "name": "Lowland Junction",
  "briefing": "Text shown on the status screen.",
  "start": "1850-01-01",
  "seed": 11,
  "map": "small",
  "rivals": 0,
  "territories": 0,
  "founder_fortune": 3500000,
  "founder_investment": 3000000,
  "outside_investment": 3000000,
  "chairman_can_resign": true,
  "chairman_can_be_fired": true,
  "towns": [{"name": "Ashford", "x": 60, "y": 128, "houses": 30}],
  "medals": {
    "bronze": {"by": "1856-12-31", "goals": [{"kind": "connect_towns", "towns": ["Ashford", "Brennan Falls"]}]},
    "silver": {"by": "1862-12-31", "goals": [{"kind": "book_value", "amount": 8000000}]},
    "gold":   {"by": "1866-12-31", "goals": [{"kind": "revenue", "amount": 1500000}]}
  }
}
```

- `key`, `name`, `start` and `medals` are required. Everything else is
  optional and has the defaults shown.
- `map` is `small`, `medium` or `large` (§3.1).
- `landscape` (optional) names a hand-made map to play on: a heightmap
  image, relative to the scenario's file (`"../maps/my_map_1.png"`), with
  the industries and territories kept beside it in the map's `.json`.
  The scenario's `towns` are then the map's only towns. The map editor
  writes such scenarios (G).
- The founding amounts replace `balance.json`'s `stock` values for every
  founder, rivals included.
- `towns` are placed first, centred on the given map cell (0.5-mile cells;
  the Small map is 256 × 256), with about this many houses. Water under
  them is drained. Random towns then fill the map up to its usual number,
  and industries follow as usual.
- With `territories`, the first scenario town's territory is the free home
  territory.

### Goals

| `kind` | Fields | Met when |
|---|---|---|
| `connect_towns` | `towns` (2 or more, named in `towns`) | Your company has a station serving each town, and each joins the first town's by track |
| `territories` | `count` | Your stations stand in at least this many territories |
| `book_value` | `amount` | Company book value |
| `company_cash` | `amount` | Company cash |
| `revenue` | `amount` | Company revenue over the last 12 months |
| `personal_net_worth` | `amount` | Your net worth |
| `industry_profit` | `amount` | Lifetime profit of the industries your company owns |
| `deliver` | `cargo`, `count` | Loads of the cargo your company has delivered since the start |
| `only_railroad` | none | Every rival company has merged away |
| `highest_value` | none | Your company's market value is above every rival's |
| `average_speed` | `count` (mph) | Your trains' average speed on the line over the last 12 months (distance run over time spent running, servicing, broken down or held; from the start until a year has passed) is at least this (§14.3 `avgLocoSpeed`) |
| `variable` | `name`, `count` | The scenario counter is at least this (§14.3) |

Amounts are whole dollars.

A file is refused, with a reason, if it has any of these faults:
- an unknown goal kind;
- a goal that names a town the file does not place;
- `connect_towns` with fewer than two towns;
- `deliver` without a cargo or count;
- `average_speed` without a count, or `variable` without a name;
- Bronze with no goals;
- a deadline on or before the start;
- a bad date;
- founding investment above the founder's fortune.

### Restrictions

`restrictions` limits what every company may do (rt3-clone-spec §14.3
[C/I]). All its fields are optional:

```json
"restrictions": {"track_budget": {"initial": 800, "per_year": 100, "refund": false},
                 "no_unconnected_track": true, "one_company_only": true,
                 "no_purchase": ["track", "stations", "trains", "industries"]}
```

- **`track_budget`:** each company may lay so many map cells of track.
  - Double track counts twice.
  - Each company starts with `initial` cells and gains `per_year` more each
    new year.
  - Undo gives the cells back. Bulldozing gives them back only with
    `refund`, as in the spec's Central Pacific.
  - The top bar shows the player's budget left.
- **`no_unconnected_track`:** after a company's first line, new track must
  start or end on its own track, as in the spec's Go West!.
- **`one_company_only`:** the player may not found a second company, for
  example after resigning.
- **`no_purchase`:** bans building track or stations, buying trains or
  engines, or buying or building industries.

`chairman_can_resign` and `chairman_can_be_fired` (above) are restrictions
too. F10 lists a scenario's rules.

### Events

`events` is a list of things that happen during the game (rt3-clone-spec
§14.4 [C]). They are checked at each month end, in file order.

```json
{"id": "steel_boom", "repeat_months": 0,
 "when": {"after": "1884-03-01", "before": "1890-12-31", "goals": [{"kind": "deliver", "cargo": "coal", "count": 10}],
          "variable": {"name": "boom", "at_least": 1}, "chance_percent": 100},
 "effects": [{"kind": "news", "text": "Steel orders flood in"},
             {"kind": "cargo_price", "target": "coal", "percent": 130, "months": 36}]}
```

**When an event fires.** Every part of `when` must hold, and all of them
are optional:
- the date is on or after `after`, and on or before `before`;
- every goal is met (any goal from the table above);
- the counter `variable.name` is at least `variable.at_least`.

Then the event fires with `chance_percent` chance (100 by default).

- **Once:** an event fires once, unless `repeat_months` lets it fire again
  that many months later.
- **Missed window:** a one-shot event whose `before` date passes is never
  checked again.

**Effects.**

| `kind` | Fields | Does |
|---|---|---|
| `news` | `text` | Puts a headline in the news ticker |
| `company_cash` | `amount` | Pays the player's company, booked as Grants (revenue). Negative takes money away |
| `player_cash` | `amount` | Pays the player's own account |
| `cargo_price` | `target` cargo, `percent`, `months` | Scales the cargo's base price; the price map follows over the following months |
| `express_demand` | `target` cargo, `percent`, `months` | Scales how many passengers, mail or troops appear |
| `production` | `target` industry type, `percent`, `months` | Scales that industry type's output |
| `build_cost` | `percent`, `months` | Scales all construction costs |
| `unlock_engine` | `target` engine | Makes the engine available from now, even before its year, for good |
| `territory_price` | `target` territory name, `amount` | Sets a new price for access rights to the territory, for good |
| `set_variable`, `add_variable` | `target` counter, `value` | Sets or adds to a counter |
| `briefing` | `text` | Replaces the briefing on the status screen |
| `choice` | `text`, `options`, `months` (default 1) | Asks the player a question, with up to nine answers, each a `label` and its own `effects`. The game pauses and shows it; the player answers with 1-9. Unanswered after `months` month ends, it lapses and nothing happens. A choice cannot offer another choice |
| `earthquake` | `target` town, `value` radius in cells (default 3), `percent` (default 100) | Each piece of track (anyone's) whose middle lies within the radius of the town's centre is destroyed with this chance, unless a train is on it. Its owner writes it off with nothing back; trains find another way or wait for the line to be rebuilt |
| `sabotage` | `value` trains (default 1) | So many of the player's trains out on the line (picked at random) stop as if broken down, for 3 to 10 days, and the repair bill is paid as for a breakdown |

- **Duration:** `months` of 0 (the default) means for good; otherwise the
  effect lapses that many months later. Several effects on the same thing
  multiply.
- **Checks:** cargo, industry, engine and territory names are checked
  against the game data when the game starts.
- **Ids:** event ids must be unique.
- **Counters** belong to the whole game; the spec's player, company and
  territory scopes are not modelled.

**Offers with deadlines** are choices: an offer to accept or decline,
with `months` to answer. The shipped Lowland Junction's banker offers a
loan this way, and collects it later through a counter.

## Medals

Tiers nest: Silver needs Bronze's goals as well as its own, and Gold needs
all of them (§14.3 [I]). Goals are checked on the 1st of each month, as the
state at the end of the month just ended (§14.3 [C]).

A tier is won if all its goals hold on or before its own deadline. The best
tier won is kept, and the news reports each medal. Each tier has its own
deadline, so a missed Bronze deadline can still give way to Silver if
Silver's is later.

The scenario ends when Gold is won or the last deadline passes. The game
carries on after that; only the result is fixed.

Score (§14.3 [I]) = medal (1, 2, 3) × difficulty (Easy 0.75, Medium 1,
Hard 1.25, Expert 1.5) × (1 + 0.05 × whole years before the medal's
deadline).

## Not yet

- Per-player, per-company and per-territory counters (§14.4).
- Deliveries from or to a particular place (§14.3).
- Other disasters than earthquakes and sabotage (the spec names only these).
- Hand-made maps: the terrain is still generated from the seed.
- A scenario picker in the client; the file is given on the command line.
