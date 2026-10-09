# Railroad Tycoon 3 (2003) + Coast to Coast: research notes for a clean-room reimplementation

**About the sources.** The egress proxy blocked direct fetches of Wikipedia, StrategyWiki, PCGamingWiki, MobyGames, GameFAQs, the Railroad Tycoon fandom wiki, FED2k (forum.dune2k.com), hawkdawg.com, Steam and GOG. The DNS lookup for rrtycoon.net failed. Only GitHub could be fetched directly. Almost everything below therefore comes from search-engine summaries of those pages, so treat it as secondary or tertiary. Every claim carries a URL. **[UNCERTAIN]** marks claims that are unverified, that conflict between sources, or that may come from RT2 rather than RT3. Before treating any number as authoritative, check it against the original manual or a legally owned copy, using observed behaviour only and not the copyrighted data.

---

## 1. Product facts

| Item | Detail | Source |
|---|---|---|
| Developer | PopTop Software (St. Louis). Phil Steinmeyer was designer, programmer and technical director. Franz Felsl was lead artist. Guillermo J. Garcia-Sampedro is credited with the 3D engine coding. | https://en.wikipedia.org/wiki/Railroad_Tycoon_3 , https://www.mobygames.com/game/10844/railroad-tycoon-3/ |
| Original publisher | Gathering of Developers, a Take-Two label | https://en.wikipedia.org/wiki/Railroad_Tycoon_3 |
| Windows release | October 2003. Sources disagree on the day: Oct 23 (GOG/GameFAQs), Oct 24 (WorthPlaying review), Oct 27 NA and Oct 31 EU (Wikipedia). **[UNCERTAIN exact day]** | https://gamefaqs.gamespot.com/pc/534361-railroad-tycoon-3/data , https://worthplaying.com/article/2003/11/19/reviews/13818-pc-review-railroad-tycoon-3/ |
| Mac OS X | Ported by Beenox and published by MacSoft (Destineer), not Aspyr. Macworld reported it shipping Sept 13, 2004, while Wikipedia and Fandom say Nov 22, 2004. **[UNCERTAIN]** | https://www.macworld.com/article/172182/rrt3.html , https://www.macworld.com/article/172325/rt3.html |
| Coast to Coast | A free expansion released in 2004 and bundled with patch 1.04. It added 13 maps (3 had already appeared as post-launch bonus maps), 8 locomotives, and 3 customisation tools: TrainSkin, CompanyLogo and PlayerPortrait. | https://www.neowin.net/news/railroad-tycoon-3-coast-to-coast-expansion/ , https://www.moddb.com/games/railroad-tycoon-3/downloads/railroad-tycoon-3-v104-patch-coast-to-coast-exp , https://www.gameindustry.com/news-industry-happenings/railroad-tycoon-3-expands-for-free/ |
| Patches | 1.03 and 1.04 (C2C), then 1.05. The known 1.05 fixes are a multiplayer crash that 1.04 introduced, trains crawling near stations and service towers, and two-input industries running at half rate. 1.05 also relaxed some scenario conditions. The 1.05 date is given as Dec 2003 in one place and Sept 2004 in another. **[UNCERTAIN]** A community "1.06 / TrainMaster" project also exists. | https://www.gamepressure.com/download/railroad-tycoon-3-v105-eng-patch/z21755 , https://www.gamewatcher.com/downloads/railroad-tycoon-3-download/patch-1-05--33 , https://rrtycoon.blogspot.com/2021/02/rr-tycoon-ver106-trainmaster-parallel.html |
| IP owner | Take-Two Interactive. It bought PopTop on 24 July 2000 for about 559,100 Take-Two shares (about $5.8M). PopTop moved under the 2K label in 2005 and was merged into Firaxis in March 2006. The brand is now with Take-Two/2K. | https://en.wikipedia.org/wiki/PopTop_Software , https://www.shacknews.com/article/41132/firaxis-and-poptop-strategically-combined |
| Sold today | Steam (app 7610, publisher 2K, Steam release 4 May 2007) and GOG (DRM-free, Windows, about 1.2 GB). It is unconfirmed whether the store builds include Coast to Coast; community reports suggest Steam includes it. **[UNCERTAIN]** | https://store.steampowered.com/app/7610/Railroad_Tycoon_3/ , https://www.gog.com/en/game/railroad_tycoon_3 , https://steampulse.org/game/7610 , https://steamcommunity.com/app/7610/discussions/0/619574421293204504/ |
| Successor | Sid Meier's Railroads! (Firaxis, 2006) | https://en.wikipedia.org/wiki/Sid_Meier%27s_Railroads! |

**System requirements**
- **Windows minimum:** Pentium III 700 MHz, 128 MB RAM, 32 MB 3D card, DirectX 8.1, 1.2 GB disk. Ensigame lists a lower minimum of PII 300 and a 16 MB card. **[UNCERTAIN]** Sources: https://gamicus.fandom.com/wiki/Railroad_Tycoon_3 , https://ensigame.com/games/railroad-tycoon-3/system-requirements
- **Mac:** OS X 10.2.8, G4 400 MHz, 256 MB RAM, a 32 MB Radeon or GeForce2 MX or better. Source: Macworld (above).
- **Modern play:** a widely used Steam guide covers crash fixes and high-resolution workarounds. Source: https://steamcommunity.com/sharedfiles/filedetails/?id=782327404

---

## 2. Game structure and modes

- **Campaign.** 16 scenarios grouped into themed "rooms". Some summaries say five continents, others four rooms. Every campaign scenario is unlocked from the start, though the narrator recommends playing them in order. Finishing all of them at bronze or better gives a reward (a "key to the city"). Sources: https://en.wikipedia.org/wiki/Railroad_Tycoon_3 , https://strategywiki.org/wiki/Railroad_Tycoon_3/Walkthrough , https://worthplaying.com/article/2003/11/19/reviews/13818-pc-review-railroad-tycoon-3/
- **Scenario mode.** Standalone scenarios beyond the campaign. GOG and Steam say "25 scenarios" in total, which presumably counts campaign plus standalone maps. **[UNCERTAIN count]** Source: https://www.gog.com/en/game/railroad_tycoon_3
- **Sandbox.** Pick a map and a period. The economy and financial constraints are switched off, so it plays like a model-railway mode. Source: https://www.mobygames.com/game/10844/railroad-tycoon-3/
- **Map/scenario editor.**
  - It ships with the game but cannot be opened during play.
  - It imports grayscale heightmaps; a "Map Builder" can grab real-world terrain for any patch of the globe.
  - Terrain tools include painting, hill raising, a "splatter" tool that scatters three textures by percentage, an auto-tree tool and sound painting.
  - It has a scripted event/trigger system. Territories, cities, chairmen, locomotives and car types all have numeric IDs that events can test.
  - Per-cargo production and price can be adjusted. The editor reports event errors (RT2's editor did not).
  - Sources: https://forum.dune2k.com/topic/22338-map-builder-and-gamemap-editor-for-rt3/ , https://forum.dune2k.com/topic/22971-map-editing/ , https://laperkovo.wordpress.com/wp-content/uploads/2015/08/railroad-tycoon-3-map-editor-tutorial.pdf
- **Multiplayer.** Internet play through GameSpy, with one player hosting and the others connecting to them. GameSpy shut down in 2014. LAN support was not confirmed. **[UNCERTAIN]** Source: https://worthplaying.com/article/2003/11/19/reviews/13818-pc-review-railroad-tycoon-3/
- **Medals.** Each scenario awards Bronze, Silver or Gold depending on which objectives are met before a deadline. Some scenarios lock options; for example, in Go West! you cannot be fired or resign. Source: https://strategywiki.org/wiki/Railroad_Tycoon_3/Go_West!
- **Difficulty.**
  - The campaign has Easy, Normal and Hard; scenarios add Expert. Source: https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/RailroadTycoon
  - Difficulty mainly scales the player's earnings, and to a lesser degree pushes the AI the opposite way: on Hard the player earns less from hauling and the AI somewhat more. Source: https://forum.quartertothree.com/t/railroad-tycoon-3-reviews/6499?page=2
  - Easy targets can be lower. For example, State of Germany bronze is about 7 states on Easy. Source: https://forum.dune2k.com/topic/22608-connecting-german-states-problem/
  - **[UNCERTAIN]** The exact multipliers are not documented.

---

## 3. Campaign scenario list

Format: name — region, years. Goals are listed where they were found.

**American room**
1. **Go West!** — New England / Boston–Buffalo, start about 1850s. Connect Boston and Buffalo by 1866 for Bronze, 1861 for Silver, 1856 for Gold. You can only build from existing track. Source: https://strategywiki.org/wiki/Railroad_Tycoon_3/Go_West!
2. **Germantown, USA** — Midwestern USA, 1850–80. Source: https://railroad-tycoon.fandom.com/wiki/Germantown,_USA
3. **Central Pacific** — San Francisco to Salt Lake City, 1855–76. Medal thresholds not found. Source: https://gamicus.fandom.com/wiki/Railroad_Tycoon_3
4. **Texas Tea** — Texas and NE Mexico, 1888–1918. Connect Houston, Austin, Dallas and Ft. Worth, and reach company-profit and personal-wealth targets; oil is the main earner. A partial snippet put bronze at about $15M. **[UNCERTAIN]** Source: https://railroad-tycoon.fandom.com/wiki/Texas_Tea
5. **The War Effort** — Mid-Atlantic USA, December 1941. This is a tactical scenario with no finance and no buying or building. Different versions of the goal circulate: deliver 10 loads each of clothing, meat and cheese within about 4 days, and/or 15 loads each of weapons, ammunition and diesel to Norfolk or New York. Patch 1.05 relaxed it. **[version-dependent]** Source: https://strategywiki.org/wiki/Railroad_Tycoon_3/The_War_Effort

**European room**
6. **The State of Germany** — German states, 1848. Put stations in N German states: about 7 on Easy for Bronze; one page says 11 including Bavaria for Gold. **[UNCERTAIN]** Sources: https://railroad-tycoon.fandom.com/wiki/The_State_of_Germany , https://railroad-tycoon.fandom.com/wiki/Knitting_with_Iron
7. **The Flying Scotsman** — Great Britain, 1848–65.
8. **Crossing the Alps** — Venice, Munich and Zürich, 1875–1910.
9. **The Third Republic** — France, 1871–96. Source: https://railroad-tycoon.fandom.com/wiki/The_Third_Republic
10. **Orient Express** — Balkans (Vienna to Istanbul), 1880–1914. Includes an average-train-speed objective. Source: https://railroad-tycoon.fandom.com/wiki/Orient_Express

**World room**
11. **Argentina** — 1880–1909.
12. **Rhodes Unfinished** — Eastern Africa, 1902–33.
13. **Japan Quakes** — Japan, 1964–85. Source: https://railroad-tycoon.fandom.com/wiki/Japan_Quakes

**Future room**
14. **The Seeder** — Greenland, 2020, tactical.
15. **Dutchlantis** — Western Europe, 2021–51.
16. **A Chip off the Old Block** — California, 2050–80.

The base-game campaign era range is roughly 1840s to 2080. Most medal thresholds were not retrievable. The best sources to fetch later are the StrategyWiki per-scenario pages, the GameFAQs "Zoogz" full guide (2008), and the fandom wiki's Category:Railroad_Tycoon_3.

**Coast to Coast maps (13 in total; 10 were found)**

| Map | Theme or goal |
|---|---|
| Coast to Coast | The whole USA; flagship map |
| East of the Mississippi | Modern US rail revival |
| Eastern China | Under Mao |
| Ireland | Potato famine; use other resources to end a depression |
| Louisiana | Post-Civil War reconstruction |
| Mexico | Central railroad linked to the US after civil unrest |
| Pacific Coastal | Scenic coastal railroad |
| Pacific Northwest | Serving Seattle's growth |
| Poland | Make an inherited outdated system profitable in a free market |
| Russia / Imperial Russia | Groundwork for the Trans-Siberian |
| Southern Pacific | Mentioned in a Steam thread |

Gamicus also mentions an alternate-history scenario in which the American Revolution never happened. Sources: https://www.moddb.com/games/railroad-tycoon-3/downloads/railroad-tycoon-3-v104-patch-coast-to-coast-exp , https://www.neowin.net/news/railroad-tycoon-3-coast-to-coast-expansion/ , https://gamicus.fandom.com/wiki/Railroad_Tycoon_3 , https://steamcommunity.com/app/7610/discussions/0/343787283768859730/

**[UNCERTAIN]** The remaining C2C names and all medal conditions are missing. Hawk & Badger's RT3 Map Archive (https://hawkdawg.com/rrt/rrt3/rrt3_base_maps.php) catalogues them.

---

## 4. Finance

### 4.1 Personal vs company
- **Two separate ledgers.** The player is chairman of a company and also has a personal account. Source: https://strategywiki.org/wiki/Railroad_Tycoon_3/Gameplay
- **Personal income** comes from salary (small), dividends on shares held, and stock trading. Source: https://steamcommunity.com/app/7610/discussions/0/133262487499570406/
- **No personal loans.** The player cannot take a normal personal loan; personal borrowing happens only through margin buying or short selling. Source: https://hawkdawg.com/hp/strategy.php
- **"Purchasing power"** = personal cash plus the amount that can be borrowed against holdings. One player estimates holdings count at about 50% of their value. **[UNCERTAIN]**
- **Margin rules.** Buying on margin can push personal cash negative, and interest is charged until cash is positive again. If purchasing power goes negative, the game forces sales of shares until it is positive. Those forced sales can drive the price down further and cause personal ruin. Sources: StrategyWiki Gameplay; https://steamcommunity.com/app/7610/discussions/0/595136643598395495/
- **Score.** Personal net worth is a common scenario win metric. The company also has its own metrics: book value, profit, and cargo hauled.

### 4.2 Company finance screens
- **Overview.** Yearly and lifetime revenue, expenses, interest and profit. The Almanac (key "A") shows the balance sheet, including book value. There is no historical chart of book value. Source: https://steamcommunity.com/app/7610/discussions/0/620702121652668984
- **Bonds.**
  - Bonds are $500,000 each. Issuing one requires a credit rating of at least B, and 2% of the face value goes to underwriting.
  - Each extra bond lowers the credit rating.
  - New companies start with a marginal rating, which means higher interest and usually room for only one or two bonds.
  - In boom periods with good credit you can issue more bonds and refinance old ones at lower rates.
  - The interest rate depends on the rating and on the economy when the bond is issued. **[UNCERTAIN: exact rate table]**
  - Sources: StrategyWiki Gameplay; https://theravenscall.substack.com/p/railroad-tycoon-iii-and-the-dire ; https://forum.quartertothree.com/t/railroad-tycoon-3-reviews/6499?page=2
- **Bankruptcy.** A company can declare bankruptcy to clear part of its bond debt, at a heavy cost to its credit rating. AI companies go bankrupt fairly often on crowded maps. An RT2-era exploit using personal bankruptcy was removed in RT3. Sources: StrategyWiki Gameplay; https://www.octopusoverlords.com/forum/viewtopic.php?t=40519

### 4.3 Stock market
- **Issuing stock.** Allowed at most twice a year. It raises cash at the current price but pushes the price down.
- **Buybacks.** Raise the share price but reduce book value.
- **Dividends.** The rate is set by the chairman as a per-share annual figure, paid quarterly (end of March, June, September and December). Raising the dividend while holding most of the shares is the standard way to move company cash into personal cash. Source: https://forum.dune2k.com/topic/22529-dividends/
- **Trading.**
  - Shares are bought and sold in blocks of 1,000. Ctrl-click trades 5,000 and Alt-click trades 25,000 (one forum report).
  - A sale executes at the price after your own trade's impact, so large blocks move the price. Possible brokerage fees are mentioned. **[UNCERTAIN]**
  - Prices update at least monthly.
- **Short selling.** Allowed on rival companies but not on your own. It is risky if the target's price rises; shorting a company that is going bankrupt is a known tactic.
- **Splits.** AI companies' shares can split as they grow; a forum post mentions it for RT3. **[UNCERTAIN on the split trigger and ratio]** Sources: https://hawkdawg.com/hp/strategy.php , https://forum.dune2k.com/topic/22464-stock-market/
- **Price drivers** reported by players: earnings, dividend, book value per share, the economy, issues and buybacks, and trading volume. Players buy when the price falls below book value per share in a downturn. **[UNCERTAIN]** No formula is published.
- **Control.**
  - Holding more than 50% protects you from being voted out.
  - You can be fired as chairman (scenario-dependent), resign, or attempt to take over the chairmanship of a company where you hold shares.
  - **Mergers** are attempted from the company/stock screen. The usual route is to build a stake of 51% or more and then merge. A merger needs enough shareholder votes, and owning a large but non-majority stake can fail.
  - **Tender offers / buyouts** for a whole rival company exist.
  - Starting a new company or switching companies is possible in some scenarios.
  - Sources: https://strategywiki.org/wiki/Railroad_Tycoon_3/Gameplay , https://steamcommunity.com/app/7610/discussions/0/4414172471677034154/ , https://steamcommunity.com/app/7610/discussions/0/34094415785755797/

### 4.4 Industries and economy
- **Prices, not distance.** The cargo economy is a price map. Revenue comes from the difference in price between pickup and delivery, not from distance as in RT1/RT2. Sources: https://en.wikipedia.org/wiki/Railroad_Tycoon_3 , https://strategywiki.org/wiki/Railroad_Tycoon_3
- **Off-rail flows.** Goods also move by road, barge and horse cart. Building a consumer reshapes the price map slowly, over about a year. Demand saturates. Source: https://forum.quartertothree.com/t/railroad-tycoon-3/6792
- **Owning industries.**
  - The company can buy existing industries and build processing industries anywhere, including any industry available in the current era even if it is absent from the map.
  - Capacity can be upgraded.
  - An owned industry's profit = output value − input cost − labour.
  - Hotels and restaurants near stations also earn money.
  - Sources: https://en.wikipedia.org/wiki/Railroad_Tycoon_3 , https://www.mobygames.com/game/10844/railroad-tycoon-3/
- **Cargo data.** The fan wiki lists, per cargo, the car type, a base price, producers and consumers (e.g. alcohol comes from breweries and distilleries and goes to barracks and houses). Source: https://railroad-tycoon.fandom.com/wiki/List_of_Cargo_in_Railroad_Tycoon_3

### 4.5 Economic cycles and interest rates
- **RT3 evidence.** Players describe recessions and booms that hit demand (e.g. for cars), industry prices, share prices and bond terms. Source: https://steamcommunity.com/app/7610/discussions/0/620700960756515594/
- **The model is not documented for RT3.** RT2 used five states (Booming, Prosperity, Normal, Recession, Depression), with bond rates, revenue and stock prices tied to the state. Source: https://en.wikipedia.org/wiki/Railroad_Tycoon_II
- **[UNCERTAIN]** RT3 probably uses the same or a similar five-state scale. A "Panic" state belongs to RT1, not RT3. Scenario events can also force economic changes through the event system.

---

## 5. AI opponents

- **Chairmen are real historical figures,** some of them controversial. Source: https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/RailroadTycoon
- **Names confirmed in community sources:** Cecil Rhodes, Lord Strathcona, Sir George Stephen, Sir John Forrest, Sir Sanford Fleming, Leland Stanford, I. K. Brunel, Charles Crocker, James J. Hill, Collis Huntington, John C. Frémont, Emperor Meiji, Jay Gould. Source: https://forum.dune2k.com/topic/24456-ai-players-missing-in-scenario/
- **Probably present but unconfirmed:** Vanderbilt, J. P. Morgan, Harriman, George Stephenson. **[UNCERTAIN]**
- **Personality model.** Each chairman appears to have numeric traits, shown on the forum as two percentage columns that are unlabeled in the excerpt; they may be aggressiveness or expansion and financial risk. **[UNCERTAIN]** Reported examples:
  - Hill: 200%/200%; expands track and issues bonds
  - Gould: 20%/200%; margin buying, short selling and investing in rivals
  - Stanford: 100%/185%
  - Fleming: 100%/200%; issues bonds
  - Crocker and Rhodes: 200%/200%; aggressive builders
  - Source: same FED2k thread.
- **Behaviour.**
  - The AI builds lines, issues stock and bonds, and trades shares.
  - It can use the player's track and compete for cargo. Source: https://steamcommunity.com/app/7610/discussions/0/353916184352417109/
  - Known weaknesses: it picks poor locomotives and sometimes starts between mediocre cities. Players generally find it beatable even on Hard. Sources: TV Tropes; https://hawkdawg.com/hp/strategy.php
- **Placement.** Scenario designers choose which chairmen appear and can script them through events.

---

## 6. Time progression

- **Era range.** Content spans the earliest steam locomotives (Planet 2-2-0, available 1829–1840) to future maglev and bullet trains around 2080. Each locomotive has an availability window, e.g. Crampton 1852–89 and Pacific 4-6-2 1914–50. Source: https://railroad-tycoon.fandom.com/wiki/List_of_Locomotives_in_Railroad_Tycoon_3
- **Roster size.** "Over 40" or "nearly 60" locomotives depending on the source; C2C added 8.
- **Calendar.** The simulation runs on days, months and years. Dividends are quarterly; reports and stock issue limits are annual. The time-of-day granularity in The War Effort suggests a fine-grained clock.
- **Speed settings.** Pause plus several speeds. **[UNCERTAIN: exact steps]**
- **Era events.** Delivered through each scenario's scripted event system (wars, quakes, economic shocks). **[UNCERTAIN: random events in sandbox]**

---

## 7. Technology and engine

- **Engine.** A proprietary full-3D engine by PopTop on DirectX 8.1, replacing RT2's 2D isometric view.
  - The camera is free, with zoom from satellite view down to trackside.
  - There is no rigid grid. Track is laid as curves at any angle, and buildings rotate 360°.
  - Tunnels and water bridges are supported.
  - Sources: https://en.wikipedia.org/wiki/Railroad_Tycoon_3 , https://www.gamespot.com/articles/railroad-tycoon-3-preview/1100-6030899/
- **Graphics options.** Hardware T&L can be toggled. The default resolution is 800x600; higher or widescreen resolutions need config hacks. Source: https://www.wsgf.org/dr/railroad-tycoon-3
- **Config files.** `Data\Configuration\engine.cfg` and `game.cfg` are binary. The open-source **rt3conf** project (GitHub) reverse-engineered engine.cfg by diffing the file before and after changing settings; unknown fields are labeled `fieldN`. Source: https://github.com/MichaelMcDonnell/rt3conf (fetched directly)
- **Map sizes.** Not found. The C2C full-USA map is reportedly the largest. **[UNCERTAIN]**

### File formats documented by the community
For data-structure insight only; do not copy assets.

| Extension | Purpose | Notes and source |
|---|---|---|
| `.gmp` | Maps and scenarios, kept in the `Maps` folder | Not the Global Mapper format despite sharing the extension. Sources: https://forums.civfanatics.com/threads/new-american-millennium-rrt3-mod.629764/ , https://steamcommunity.com/app/7610/discussions/0/5750503966481301804/ |
| `.pk4` | Pack/archive for assets and mods | TrainMaster is built from modular pk4 files. Editing contents needed Photoshop plug-ins, which suggests DDS textures (an inference). **[UNCERTAIN]** Source: https://forum.dune2k.com/topic/22509-pk4-files/ |
| `.3dp` | 3D models | Hex-editable; UV mapping fixed by hand. Source: http://wpandp.com/RRtycoon.html |
| `.cty` | Car/cargo type definitions for rolling stock | Source: http://wpandp.com/CarModHowTo.html |
| `.bca` | Building and industry definitions, edited with a hex editor | Community Excel planners exist. Source: https://rrtycoon.blogspot.com/ |
| `.cgo`, `.bty` | Listed as game file types | Likely cargo and building-type data. **[UNCERTAIN]** Source: https://file.tips/software/railroad-tycoon-3 |
| `.gmx`, `.car`, `.lco` | Not confirmed by any source | Possibly sandbox/scenario variants or locomotive definitions. **[UNCERTAIN]** |

- **Official modding tools.** The map/scenario editor in the game, plus TrainSkin, CompanyLogo and PlayerPortrait (added in 1.04/C2C).
- **Community resources.**
  - Hawk & Badger RT3 Extras (tips, tutorials, utilities): https://hawkdawg.com/rrt/rrt3_extras/rrt3_utilities.php
  - FED2k RT3 forums: https://forum.dune2k.com/forum/47-railroad-tycoon-3-maps/
  - ModDB: https://www.moddb.com/games/railroad-tycoon-3
  - Wine/Apple Silicon run notes: https://gist.github.com/samdoidge/c0cf8148b3df572240054bc3de6f8d21

---

## 8. Gaps to close later
1. Exact medal thresholds for the 15 campaign scenarios other than Go West!, and for all C2C maps. Fetch the StrategyWiki per-scenario pages and the GameFAQs Zoogz guide.
2. The economic state machine and the interest-rate table for each credit rating and economic state; RT3's manual is the best source.
3. The share-price formula, the margin percentage, and the stock split trigger and ratio.
4. The full chairman roster and the meaning of the two trait columns.
5. Map dimensions, and the internal structure of `.gmp`, `.pk4` and `.3dp`. No public spec was found. The only open-source RE project found is rt3conf, which covers config files only.
6. Whether the Steam and GOG builds include Coast to Coast and patch 1.05.
7. Whether LAN multiplayer is supported. GameSpy is defunct, so a reimplementation needs its own netcode either way.

**Clean-room note.** Reproduce the game's behaviour, not its data. Write your own numbers, scenarios and assets, and keep these real-person chairman names out of shipped content unless legal review approves them.