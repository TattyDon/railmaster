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

| File | Start | Goals |
|---|---|---|
| `lowland_junction.json` | 1850, no rivals | Connect two towns, then four; book value; revenue |
| `coal_country.json` | 1880, two rivals | Deliver coal; revenue; be the most valuable railroad |
| `six_provinces.json` | 1900, one rival, six territories, smaller founding money, chair locked | Stations in 3 and then 5 territories; personal net worth; be the only railroad |

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

Amounts are whole dollars.

A file is refused, with a reason, if it has any of these faults:
- an unknown goal kind;
- a goal that names a town the file does not place;
- `connect_towns` with fewer than two towns;
- `deliver` without a cargo or count;
- Bronze with no goals;
- a deadline on or before the start;
- a bad date;
- founding investment above the founder's fortune.

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

- Events and triggers (§14.4).
- Restrictions:
  - track budgets;
  - no unconnected track;
  - no purchase of track, stations or trains;
  - one company only (§14.3).
- The goals `avgLocoSpeed` and `variable`, and deliveries from or to a
  particular place (§14.3).
- Hand-made maps: the terrain is still generated from the seed.
- A scenario picker in the client; the file is given on the command line.
