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
- **Bonds** have no maturity. The interest rate is fixed when issued.
  Repaying pays off the most expensive bond at face value.
- **Book value** = cash + assets at cost − bonds.
- **Negative cash** is currently allowed: running costs are always charged.
  Bankruptcy comes later.

## Provisional constants

| Item | Value |
|---|---|
| Starting cash | $6,000,000 |
| Fuel per km | Steam $20, diesel $15, electric $10, plus $2 per car |
| Track and building upkeep | 0.5% of cost a month |
| Rating by debt ÷ assets | AAA < 5%, AA < 15%, A < 25%, BBB < 35%, BB < 45%, B < 55%, C < 70%, else D |
| Interest by rating | AAA 4%, AA 4.5%, A 5%, BBB 6%, BB 7%, B 8%, C 10%, D 12% |

## Balance: known to be off

With the provisional fares and costs, a 20 km passenger and mail line made
about $1.5M profit in its first year on roughly $1M invested. That is
probably far too generous. Balancing needs real numbers: what a 20-mile RT3
passenger run pays, and RT3's track prices and starting capital. These are
in the research backlog.
