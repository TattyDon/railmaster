# Rival companies

How several railroads share one map: who owns what, what running on a
rival's track costs, how the stock market covers every company, and how the
AI tycoons decide. Spec references are to [rt3-clone-spec.md](rt3-clone-spec.md).
Tags: [D] documented, [C] community, [M] from the manual, [I] ours.

## Players and companies

- Each **player** (the human, always player 0, or an AI tycoon) has a
  private account and a portfolio of shares in any company (§12.1 [D]).
- Each player may chair one **company**. The human's company is company 0.
  A rival tycoon founds a company with the same starting cash and the same
  founding share split as the player.
- Every command is made by a player (`World::execute(command, player)`).
  Building, buying trains, bonds, stock issues and dividends act for the
  company that player chairs. Share trades are personal. The AI uses
  exactly the same commands, so it pays the same costs and obeys the same
  rules. The one exception: sandbox's "money is no object" applies only to
  the human.

## Ownership

- Every piece of track, station, support building and train belongs to the
  company that built or bought it. A piece of track split by a junction
  keeps its owner.
- A company may join its track to anyone's. Stations and support buildings
  go only on your own track, which includes a node where your track meets
  someone else's.
- **Any company may run trains over a rival's track and use its stations**
  (§8.5 [D]), and its support buildings (ours).
- **Meets:** on someone else's track, a train yields to the owner's trains
  whatever its priority [M, trains-track-operations.md]. Between two
  visitors, or two of the owner's trains, priority then cargo value decide
  as before.

## Trackage rights [D]

The track's owner receives the share of a train's income matching the share
of the route run on its track; the runner still pays all of its fuel
(§8.5). We measure "the route" as the leg since the train's last stop [I]:
at each stop, the income earned there is split by the distance each company's
track carried the train on the way in. The owner books it as **Trackage
income**, the runner as **Trackage paid**.

Example (tested): a rival's coal train runs 25 km on the player's track and
20 km on its own. The player receives 25/45 of each delivery's income.

## The market across companies

- Net worth and purchasing power count holdings in every company (§12.1
  [D]). Margin is 50% of each holding's value.
- Anyone may buy any company's shares in public hands. A buy is checked
  against the purchasing power before the trade.
- Salary goes to each player who chairs a company. Margin interest is
  charged to anyone with negative cash.
- **Margin call:** if purchasing power goes negative, the most valuable
  holding is sold a block at a time until it is not [D: forced sales; which
  holding goes first is ours].
- Dividends are paid to every holder in proportion.
- Not yet: short selling (§12.5), takeovers and mergers (§12.6), brokerage.

## The AI [I]

Tycoons are listed in [`data/tycoons.json`](../../data/tycoons.json): the
historical figures RT3 used as opponents (§13.1 [D/C]) plus two more famous
railroad builders. Each has an original one-line bio and four personality
values from 0 to 100 (§13.2 [I]):

| Trait | Effect |
|---|---|
| expansion | How often it builds a new line (every 24 months at 0, every 3 at 100) and how far it will reach (up to 50% further) |
| leverage | How many bonds it will take on to build (up to leverage ÷ 10), and how flush it must be before repaying |
| dividend | Share of trailing profit paid out as a dividend |
| speculation | From 40 up: buys rivals' shares below 80% of book value, sells above 150% |

Each month, after the market, each rival in turn:

1. **Finance.** Issues a bond if its cash is negative; sets its dividend;
   repays a bond when it holds several bonds' worth of spare cash.
2. **Expansion**, when its build interval has passed:
   - Lists candidate lines. These are town pairs, valued by passenger and
     mail fares × distance × the smaller town's houses. They are also
     freight lines from a working producer to a town or industry that pays
     more for its cargo, valued by price gap × output. Only lines between 8
     and 40 km (more for keen builders) are listed. A place another station
     already serves at both ends is skipped.
   - Ranks them by value per rough cost, then costs the best eight
     exactly with the track planner. It picks the best value per dollar it
     can afford, borrowing if its temperament allows.
   - Builds the track. Where its new line meets existing track it joins it,
     a rival's included. It then builds stations at both ends (or reuses one
     already there, a rival's included), a maintenance facility and a
     service tower, and buys the fastest engine it can afford with four
     cars.
3. **More trains:** adds a train to a line whose stations have more cargo
   waiting than its trains can carry, up to three a line.
4. **Speculation**, as above.

The AI makes no use of randomness: its choices follow from the map and its
personality. Which tycoons appear is chosen at random from the world seed.
Numbers are in the `ai` section of `data/balance.json`.

Measured on the default map with three rivals: every rival is profitable by
its second year and carries both freight and passengers. Two often build
into the same station.

### Not yet

- Replacing old engines, more than two stops per route, and double track.
- Buying industries (§6.2), short selling, takeovers (§12.6), recession
  caution.
- Bankruptcy for anyone: a rival that keeps losing money just borrows.
