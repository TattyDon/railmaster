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
- The interest rate depends on the rating; the economy's effect comes with
  economic cycles.
- Sandbox mode switches financial limits off.
- Locomotive maintenance rises with age, to about 3× by year 20, and with
  low oil (already modelled in M1).

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
  - Track upkeep: 0.5% of track cost a month, RT2's rate (6% a year).
  - Building upkeep: the same rate on stations and support buildings.
  - Bond interest.
- **Credit rating.** The grade is set by debt as a share of total assets.
  A company that has not yet finished a profitable year is held to BB at
  best, and every bond outstanding costs one more notch. A new company can
  therefore issue two bonds (BB, then B) and no more until it proves itself.
- **Bonds:** at most 20; interest paid quarterly [D]; repaying early costs
  2% extra [C]; they mature after 30 years [I] and are repaid at face
  value. The interest rate is fixed when issued.
- **Rating penalty for bonds:** a notch per bond while the company is
  unproven, a notch per four bonds once it has had a profitable year, so
  the 20-bond maximum is reachable.
- **Book value** = cash + assets at cost − bonds.
- **Negative cash** is currently allowed: running costs are always charged.
  Bankruptcy comes later.

## Provisional constants

In the `finance` and `stock` sections of
[`data/balance.json`](../../data/balance.json); see [balance.md](balance.md).

| Item | Value |
|---|---|
| Starting cash | $6,000,000 |
| Fuel per km | Steam $20, diesel $15, electric $10, plus $2 per car |
| Track and building upkeep | 0.5% of cost a month |
| Rating by debt ÷ assets | AAA < 5%, AA < 15%, A < 25%, BBB < 35%, BB < 45%, B < 55%, C < 70%, else D |
| Interest by rating | AAA 4%, AA 4.5%, A 5%, BBB 6%, BB 7%, B 8%, C 10%, D 12% |

## Personal account, shares and the stock market

Code: `src/sim/include/railmaster/sim/stock.hpp`.

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

- **Founding.** The company starts with 600,000 shares priced at its
  starting cash per share ($10). The player holds half and has $500,000 in
  personal cash.
- **Price impact.** Each 1,000-share block moves the price by
  2 × (block ÷ shares outstanding), so trading 1% of the company moves the
  price 2%.
- **Price model.** Each month the price closes a quarter of the gap to a
  target of 0.6 × book value per share + 8 × earnings per share + 10 ×
  dividend per share. Earnings are the last 12 months' profit, annualised
  once three months exist. The economy's effect comes with economic cycles.
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
| Personal starting cash | $500,000 |
| Salary | $50,000 a year [C] |
| Margin | Holdings count at 50% |
| Margin interest | 10% a year on negative cash |
| Issue or buyback size | 10% of shares outstanding |
| Price target weights | 0.6 × book value/share, 8 × EPS, 10 × dividend/share |
| Monthly price adjustment | 25% of the gap |
| Minimum share price | $0.50 |

## Balance: known to be off

With the provisional fares and costs, a 20 km passenger and mail line made
about $1.5M profit in its first year on roughly $1M invested. That is
probably far too generous. Balancing needs real numbers: what a 20-mile RT3
passenger run pays, and RT3's track prices and starting capital. These are
in the research backlog.
