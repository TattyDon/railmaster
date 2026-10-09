# Rival companies

How several railroads share one map: who owns what, what running on a
rival's track costs, how the stock market covers every company, and how the
AI tycoons decide. Spec references are to [rt3-clone-spec.md](rt3-clone-spec.md).
Tags: [D] documented, [C] community, [M] from the manual, [I] ours.

## Players and companies

- Each **player** (the human, always player 0, or an AI tycoon) has a
  private account and a portfolio of shares in any company (§12.1 [D]).
- Each player may chair one **company**. The human's company is company 0.
  A rival tycoon founds a company on the default founding terms (see
  m3-finance-model.md), as the player does unless they choose otherwise
  in the founding dialog.
- A player with no company may found a new one (`FoundCompany`; market
  screen, N) with whatever money they have.
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
- Not yet: brokerage (§12.5 [I]).

## Short selling (§12.5 [D concept, C limits])

- Selling more shares than you hold sells the rest short: borrowed shares
  sold into the market, leaving a negative holding. Buying covers a short
  first, and is always allowed while it only covers.
- You cannot short the company you chair [D]. All your shorts together may
  not exceed 50% of your net worth when you open one [C].
- Net worth counts a short at today's price. Purchasing power holds 150% of
  its value against it [I], so a rising price can trigger a margin call.
  A margin call unwinds the biggest position first, long or short, a
  block at a time.
- Short sellers pay the dividend on the shares they borrowed.
- Borrowed shares sold are in public hands, so the public float grows.

## Takeovers (§12.6)

`AttemptTakeover{target}` is personal: any player can ask a company's
shareholders to make them its chairman [D].

- **Vote [I]:** the bidder's shares vote yes and the chairman's no. A share
  of the public float backs the bidder: 25%, plus 25% if the company trades
  below book value per share, plus 25% if its last full year was a loss.
  Other players vote with that majority (yes if it is 50% or more). The bid
  passes on more than half the shares. More than 50% always wins, and a
  chairman who holds more than 50% cannot be removed [C].
- **Result [I]:** the bidder takes the chair. If they ran another company,
  the ousted chairman takes that one, so the two swap seats. Someone with
  no company to offer cannot take the player's company, so the player
  always runs one.
- A failed attempt cannot be repeated on the same company for a year [C].

## Mergers (§12.6)

`AttemptMerger{target, offer_per_share}` is made by a chairman for their
company [D].

- **Cost:** the offer × every share the bidder does not hold [I, the
  spec's formula]. The company must have the cash (unless the player is in
  sandbox).
- **Vote [I]:** the bidder's shares vote yes. Other players accept from a
  premium of 10% over the market price, the target's chairman from 25%.
  The public's support is 50% at a 10% premium and moves 3 points per point
  of premium either way (a linearised version of the spec's sigmoid): none
  at 7% below market, all at 27% above. Over 50% of the shares always wins
  [C].
- **Result:**
  - Every other holder is paid the offer in cash, and short sellers are
    closed out at the offer price.
  - The bidder's own stake becomes new shares of their company, at market
    value [I].
  - All of the target's track, stations, buildings, trains, cash and bonds
    move to the buyer [C]. The target remains as an empty, defunct record
    so ids never change.
  - Its chairman is left without a company but can still trade.
  - The cost appears as "spent on mergers" in the buyer's accounts.
- A failed attempt waits a year [C]. A rival cannot merge the player's own
  company away [I, for now].

## Investors and the chair (§12.2)

- **Shareholders' return:** each year closes with the return investors
  made: the change in share price plus the dividends paid per share, as a
  share of the opening price. Splits adjust the stored prices so returns
  stay comparable.
- **Bad year [I]:** a loss, or a year when profit and share price both
  fell. Investors want rising earnings and share price [D].
- **Five-year weighted return [I]:** the last five closed years' returns,
  weighted 5 (latest), 4, 3, 2, 1.
- **Sentiment:**

  | Mood | When |
  |---|---|
  | Hostile | 3 bad years in a row |
  | Grumbling | 2 bad years in a row [D: "2-3 bad years"] |
  | Happy | five-year weighted return of 10% or more |
  | Content | otherwise |

  It shows on the finance and market screens.
- **Firing [D/C/I]:** at each year end, two bad years in a row bring a
  warning in the news; three bring a vote to remove the chairman. The other
  shareholders vote against them, so only a chairman holding over half the
  shares survives [C]. A founder holding exactly half can be voted out.
  `WorldConfig::chairman_can_be_fired = false` locks the chair, as some
  scenarios do.
- **Resigning [D]:** a chairman may step down and keeps their shares
  (`Resign`; market screen, Q twice). `chairman_can_resign = false` forbids
  it.
- **Successor [I]:** the company's biggest shareholder who runs nothing
  else. If there is none, a tycoon not yet in the game arrives to run it as
  a new AI player. If none is left, the company has no chairman.
- **The player without a company:** a fired or resigned player keeps
  trading. They can win a chair back by takeover, or be appointed when a
  company they hold the most of needs a chairman. Until then the client
  shows the company they last ran, and company commands are refused.
- **Salary [I]:** the board pays $50,000 a year × (1 + 2 × the five-year
  weighted return), held between half and double.

## Stock splits (§12.3 [C/I])

At each month end, a price above $120 for three month ends in a row splits
the stock 2 for 1, or 3 for 1 if it is above $200. Shares outstanding and
every holding (shorts included) multiply by the ratio. The price, dividend
per share and per-share history divide by it, so nobody's net worth moves.
The news reports each split.

## The AI [I]

Tycoons are listed in [`data/tycoons.json`](../../data/tycoons.json): the
historical figures RT3 used as opponents (§13.1 [D/C]) plus two more famous
railroad builders. Each has an original one-line bio and personality
values from 0 to 100 (§13.2 [I]):

| Trait | Effect |
|---|---|
| expansion | How often it builds a new line (every 24 months at 0, every 3 at 100) and how far it will reach (up to 50% further) |
| leverage | How many bonds it will take on to build (up to leverage ÷ 10), and how flush it must be before repaying |
| dividend | Share of trailing profit paid out as a dividend |
| speculation | From 40 up: trades rivals' shares against the price the market is heading for (the monthly target price), buying below 80% of it and selling above 120%. From 70 up it also sells short above 125% and buys back once the price has fallen to it |
| industry | From 40 up: buys profitable industries its stations serve, at no more than 12 years' profit, and doubles plants running at 90% or more of capacity when a year's profit covers the cost |
| recession_caution | Holding back in bad times: caution × 1 in a recession, × 2 in a depression. From 80 points it builds no new lines; per 100 points it keeps twice its usual cash reserve and pays half the dividend and takes half the bonds it otherwise would. Morgan (80) and Kodama (85) stop building in any recession; Stanford (15) and Rhodes (20) build on even in a depression |
| takeovers | From 50 up: builds a stake in the rival trading furthest below book value, up to a majority. With over 35% of a company trading below book it bids for control: always if it has no company of its own, otherwise only from 80 (when its own company passes to the ousted chairman). From 70 up it merges companies where it holds a majority, offering a 25% premium, if its company can pay |

Each month, after the market, each rival in turn:

1. **Finance.** Issues a bond if its cash is negative; sets its dividend;
   repays a bond when it holds several bonds' worth of spare cash.
2. **Expansion**, when its build interval has passed:
   - Lists candidate lines, city pairs and industry–consumer pairs alike
     (rt3-clone-spec §13 [I]), each valued in dollars a year the way the
     game will pay them:
     - **Town pairs:** for each express cargo, each town's yearly rate of
       houses (and barracks) making it × the cargo's generation, sent to the
       other town in proportion to its draw A ÷ (A + 2 loads), both ways, ×
       the fare per km × distance. This is the express code's own rule.
     - **Freight:** from a working producer to a town or industry that pays
       more, the gap between what a medium station at each end would buy and
       sell for (the best price in its catchment) × the producer's output.

     Only lines between 8 and 40 km (more for keen builders) are listed. A
     place another station already serves at both ends is skipped.

     An earlier estimate valued town pairs at fares × distance × the smaller
     town's houses. That counted each house as a load a year, ten times its
     calibrated rate, so freight never won and rivals carried none. In the
     calibration game about a quarter of rival revenue is now freight.
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
   **Engines:** rivals buy the fastest steam or diesel engine they can
   afford; they do not electrify their lines, so they buy no electric ones.
   **Old engines:** re-engines its oldest train whose engine has reached
   25 years (§13.2 [I]) with the fastest engine it can afford above its
   reserve, one a month.
4. **Speculation**, as above. A tycoon whose company was merged away
   still trades and can bid for control of another.
5. **Control:** stake building, takeover bids and mergers, as above.

The AI makes no use of randomness: its choices follow from the map and its
personality. Which tycoons appear is chosen at random from the world seed.
Numbers are in the `ai` section of `data/balance.json`.

Measured on the default map with three rivals: every rival is profitable by
its second year and carries both freight and passengers. Two often build
into the same station.

With six rivals over eight years: AI companies trade well above book value
(their earnings are high), so the cheap company is usually the player's.
Takeover-minded tycoons buy up its whole public float. They cannot win a
vote while the player keeps their founding 50%, but selling any of it
invites a takeover. Founders hold half of every AI company, so AI-on-AI
takeovers are rare unless a founder is forced to sell on margin.

### Not yet

- Replacing old engines, more than two stops per route, and double track.
- Recession caution.
- Recovering a failing line rather than just borrowing; a rival goes
  bankrupt only when its credit is exhausted.
