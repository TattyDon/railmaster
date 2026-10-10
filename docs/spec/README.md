# Specification of the original game

These pages describe how *Railroad Tycoon 3* behaves, as facts with sources,
written in our own words (see [../CLEAN_ROOM.md](../CLEAN_ROOM.md)).

**[rt3-clone-spec.md](rt3-clone-spec.md) is the primary reference.** It tags
every rule as documented [D], community [C] or inferred [I], and where it
disagrees with the earlier research pages below, it wins.
[gap-analysis.md](gap-analysis.md) tracks how the implementation compares
with it.

| Page | Covers |
|------|--------|
| [rt3-clone-spec.md](rt3-clone-spec.md) | **Primary.** Full functional and technical spec, every rule tagged |
| [gap-analysis.md](gap-analysis.md) | Implementation against the primary spec: matched, changed, differing, missing |
| [economy-cargo.md](economy-cargo.md) | Price field, cargo list, production chains, industries, stations, decay |
| [trains-track-operations.md](trains-track-operations.md) | Locomotive roster, consists, grades, track, servicing, routing |
| [overview-finance-scenarios.md](overview-finance-scenarios.md) | Product facts, modes, campaign, finance, stock market, AI, engine |
| [calibration.md](calibration.md) | `railmaster_calibrate`: metrics, targets and their sources, results |
| [balance.md](balance.md) | `data/balance.json`: where every tunable number lives and how to change it |
| [scenarios.md](scenarios.md) | Scenario files: format, goals, medals and score |
| [rivals-model.md](rivals-model.md) | Rival companies: ownership, trackage rights, the market across companies, the AI |
| [m1-provisional-models.md](m1-provisional-models.md) | Stand-in rules used until real values are measured |
| [m2-economy-model.md](m2-economy-model.md) | How the cargo economy works: researched facts vs our design, and its constants |
| [m3-finance-model.md](m3-finance-model.md) | Company accounts, running costs, credit rating and bonds |

## Status of the research

The first pass was compiled from search-engine extracts, because the build
environment could not open the source pages directly. Treat every number
as **unverified** until someone checks it against the source page or the
game itself, and records how in the page. Gaps are listed at the end of each
page and in the research backlog in [../ROADMAP.md](../ROADMAP.md).

Facts tagged **[WP✓]** were checked against the full text of the Wikipedia
article on 2026-10-09, covering its gameplay, campaign, development and
add-ons sections. They are better sourced than the search-summary facts but
still worth confirming in-game.

Ways to help:
- Check a value in the game and replace its tag with "verified in-game (method)".
- Fill a gap from the manual or a guide, with the page or URL cited.
- Split a page into one file per system as it grows.
