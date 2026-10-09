# M3 finance model

How Railmaster's company finances work: which parts are researched facts and
which are our design. Code: `src/sim/include/railmaster/sim/company.hpp`;
running costs are in `World::charge_running_costs`.

## Researched and implemented

From [overview-finance-scenarios.md §4](overview-finance-scenarios.md):

- The player runs a company, whose accounts are separate from the player's
  own. Personal money comes in a later slice.
- The finance screens show yearly revenue, expenses, interest and profit,
  and a balance sheet with book value.
- Bonds are $500,000 each, need a credit rating of at least B, and cost 2%
  of face value to underwrite.
- Each extra bond lowers the credit rating.
- New companies start with a marginal rating: higher interest and room for
  only one or two bonds.
- The interest rate depends on the rating and on the economy: a new bond
  pays the prime rate (Normal 6%, 4% in a boom, 8% in a depression) plus
  its grade's spread (rt3-clone-spec §12.4 [C/I]). Margin debt moves the same way
  (10% at Normal, i.e. prime + 4% as the spec suggests). Share price targets
  are scaled by the state, 80% to 125%. See m2-economy-model.md for the
  economic states.
- Sandbox mode switches financial limits off.
- Locomotive maintenance rises with age and when out of oil: 4% of the new
  cost per year of age, and half again with no oil (rt3-clone-spec §9.4
  [I]; see m1-provisional-models.md).

## Our design

- **Cash.** Every build or purchase command is checked against company cash
  and refused, with the shortfall, if it cannot be paid (not in sandbox).
  Spending moves cash into assets carried at cost: track, buildings, trains.
  There is no depreciation yet.
- **Revenue.** Delivery income is posted to freight, passenger, mail or
  troop revenue as it is earned.
- **Running costs, charged on the 1st of each month for the month before.**
  - Train maintenance: the M1 yearly figure ÷ 12.
  - Fuel: per km run.
  - Track upkeep: 2% of track cost a year, charged monthly
    (rt3-clone-spec §11.2 [I]).
  - Building upkeep: 6% a year on stations and support buildings, RT2's
    rate; the spec gives no figure.
  - Bond interest.
- **Credit rating** (rt3-clone-spec §12.4 [I]). Ten grades, A+ to D; B or
  better may issue bonds. A score in points:
  - +1,000 per doubling of assets over debt, up to ±5 doublings; no debt
    counts as the cap (+5,000);
  - +500 per times operating profit (the trailing 12 months, before
    interest) covers a year's interest, up to 5 times; nothing before three
    months of accounts;
  - +500 for each profitable year of the last three closed, −500 for each
    loss year;
  - −100 per bond outstanding;
  - −2,000 within ten years of a bankruptcy (and D outright for five).

  Grades: A+ from 7,000, A 6,000, A- 5,000, B+ 4,000, B 3,000, B- 2,000,
  C+ 1,000, C 0, C- −1,000, else D. A new $5M company with no record
  scores 5,000 (A-); its first bond leaves it at B and its second at B-, so
  it has room for two until it proves itself. A proven $30M railroad with a
  $3M profit can carry all 20 bonds at B.
- **Integer logarithm.** The asset-cover term uses an integer base-2
  logarithm, so ratings are the same on every machine.
- **Bonds:** at most 20; interest paid quarterly [D]; repaying early costs
  2% extra [C]; they mature after 30 years [I] and are repaid at face
  value. The interest rate is fixed when issued.
- **Book value** = cash + assets at cost − bonds.
- **Negative cash** is allowed: running costs are always charged. A
  company that cannot pay may declare bankruptcy (below).
- **Bankruptcy** (rt3-clone-spec §12.4 [D]): a voluntary last resort.
  - **Allowed** when the company has bonds and either its last two closed
    years were losses or its cash is below zero (bills it cannot pay)
    [C/I]. Not again within 10 years [I].
  - **Effect:** every bond's principal is halved [D]. The bondholders take
    new shares, at today's price, for what they lost, and the price falls
    in proportion to the dilution [D].
  - **Afterwards:** the credit rating is held at D for 5 years [I], so no
    new bonds. The amount written off shows as "debt forgiven" in that
    year's accounts. There is no personal bankruptcy [C].
  - AI companies declare it when out of cash with no bond left to issue.

## Provisional constants

In the `finance` and `stock` sections of
[`data/balance.json`](../../data/balance.json); see [balance.md](balance.md).

| Item | Value |
|---|---|
| Founding (default terms) | Fortune $3.5M; you invest $3M; outside investors $3M; shares at $10 |
| Fuel per km | Steam $20, diesel $15, electric $10, plus $2 per car |
| Track upkeep | 2% of cost a year |
| Building upkeep | 6% of cost a year |
| Rating score | See above: weights and thresholds in `finance.rating_*` |
| Spread over prime | A+ 0, A 0.5%, A- 1%, B+ 2%, B 3%, B- 4% [spec]; C+ 5%, C 6%, C- 7%, D 8% (ours, for display) |

## Personal account, shares and the stock market

Code: `src/sim/include/railmaster/sim/stock.hpp`. With rival companies the
market covers every company and every player; see
[rivals-model.md](rivals-model.md). Investor sentiment, firing, resigning,
salary and stock splits are described there too.

Researched [overview-finance-scenarios.md §4.1, §4.3]:

- The player's money is separate from the company's. Personal income is a
  small salary, dividends and trading. Personal net worth is the usual
  scenario score.
- There are no personal loans; borrowing happens only through margin.
  Purchasing power = cash + what can be borrowed against holdings (one
  player estimates 50%). Cash may go negative and is charged interest. If
  purchasing power goes negative, shares are sold automatically, which can
  push the price down further.
- Shares trade in blocks of 1,000, and Ctrl-click trades 5,000 (we use
  Shift). A trade executes at the price after its own impact, so big blocks
  move the price against you.
- The company may issue stock at most twice a year. An issue raises cash
  and pushes the price down; a buyback raises the price but reduces book
  value.
- Dividends are an annual per-share figure set by the chairman, paid
  quarterly at the end of March, June, September and December.
- Prices update at least monthly. Players say they are driven by earnings,
  dividends, book value per share, the economy, issues and buybacks, and
  trading.

Our design:

- **Founding** (rt3-clone-spec §12.2, the founding dialog [I, RT2]): the
  founder chooses how much of their fortune to invest, from $100,000 up to
  all of it, and how much outside money to take, up to the investors'
  offer. Capital is issued as shares at $10. The founder gets the shares
  their money bought and the outside investors hold the rest. The default
  terms (fortune $3.5M, invest $3M, outside $3M) give the old opening:
  600,000 shares, half the founder's, $500,000 left in hand. Investing more
  than the outside money gives a majority, which no shareholder vote can
  remove.
- **Brokerage** [I]: every share trade a player makes, short sales and
  margin calls included, pays 1% of its value to the broker.
- **Price model** (rt3-clone-spec §12.3 [I]). Each month the price closes
  1/8 of the gap [C] to a value of
  - 0.8 × book value per share,
  - + the EPS trend (if positive) × a P/E of 8, 9, 10, 11 or 12 from
    Depression to Boom. The trend weights the trailing 12 months' earnings
    3, last year's 2 and the year before's 1 (our reading of "3-year
    trend");
  - + 6 × the dividend per share × years of unbroken dividends ÷ 5 (at
    most 1). A year counts if all four quarterly dividends were paid;
    a missed or cut quarter starts the count again;
  - + 0.1 × revenue per share (trailing 12 months);

  all × the economic state's share index (80% to 125%).
- **Trade pressure** (§12.3 [C/I]). A trade moves the price by
  0.2 × √(shares traded ÷ shares outstanding) × price: one 1,000-share lot
  on a $50 stock with 100,000 shares moves it $1 [C], and ten lots in one
  trade about $3.16, not $10. Each block pays the price after the trade so
  far. The move is held as pressure on top of the fundamental price and
  decays to half in about six months.
- **Margin calls** unwind the fewest blocks that restore purchasing power,
  in one trade. Selling a block at a time would move the price by each
  block's own square root and could spiral.
- **Buying** must be covered by purchasing power *before* the trade.
  Valuing holdings after it would let a purchase, which lifts the price,
  pay for itself. Tests caught exactly that loop.
- **Issues and buybacks** are 10% of shares outstanding. A buyback can only
  take shares in public hands.
- **Dividends** are cut to zero if the company cannot pay a quarter's
  worth. They are recorded as distributions, not expenses.
- **Short selling** is allowed only on rivals, so it comes with AI companies.

| Item | Value |
|---|---|
| Personal cash | Fortune less investment ($500,000 by default); $500,000 for a tycoon who arrives later |
| Brokerage | 1% a trade |
| Salary | $50,000 a year [C] |
| Margin | Holdings count at 50% |
| Margin interest | 10% a year on negative cash |
| Issue or buyback size | 10% of shares outstanding |
| Price value weights | 0.8 × book value/share, EPS trend × P/E 8–12, 6 × dividend/share after 5 unbroken years, 0.1 × revenue/share |
| Monthly price adjustment | 1/8 of the gap |
| Trade impact | 0.2 × √(share of company traded) × price; pressure keeps 89.1% a month |
| Minimum share price | $0.50 |

## Balance

The early fares and rates made a 20 km line earn about $1.5M in its first
year on $1M invested. Since the production calibration the demo network
returns about 17% a year on capital; see [calibration.md](calibration.md).
Real RT3 fares, track prices and starting capital would still tighten the
targets; they are in the research backlog.
