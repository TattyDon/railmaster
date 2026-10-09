# Railroad Tycoon 3 (PopTop, 2003, plus Coast to Coast): locomotives, cars, track and operations

**How this was gathered:** The egress proxy blocked direct page fetches (WebFetch and curl) to every source I tried: fandom, Wikipedia, StrategyWiki, Steam, GameFAQs, dune2k, rrtycoon.net and the Steam-hosted manual PDF. GitHub search was also blocked. So every fact below comes from **WebSearch result extracts** of those pages. These are reliable for short facts, but I could not read whole tables. Locomotive numbers come mainly from one fan wiki table (railroad-tycoon.fandom.com) and **should be checked against the game or its data files**. Many search hits were about Railroad Tycoon II (RT2) or the original game (RT1). Those are flagged as such and are not RT3 data.

Key source URLs:
- [W1] RT3 locomotive table on the fan wiki: https://railroad-tycoon.fandom.com/wiki/Specifications_of_locomotives_in_Railroad_Tycoon_3 (also served at /wiki/List_of_Locomotives_in_Railroad_Tycoon_3)
- [M] Official RT3 manual: http://cdn.akamai.steamstatic.com/steam/apps/7610/manuals/manual_en.pdf (mirror: https://manualmachine.com/gamespc/railroadtycoon3/1118742-user-manual/)
- [SW] StrategyWiki: https://strategywiki.org/wiki/Railroad_Tycoon_3/Gameplay
- [GN] Gaming Nexus review: https://www.gamingnexus.com/Article/380/Railroad-Tycoon-3
- [WP] Wikipedia: https://en.wikipedia.org/wiki/Railroad_Tycoon_3
- [RG] Steam routing guide: https://steamcommunity.com/sharedfiles/filedetails/?id=284155250 (copy: https://steamsolo.com/guide/railroad-tycoon-3-routing-guide-railroad-tycoon-3/)
- [MD] ModDB page for the v1.04 / Coast to Coast download: https://www.moddb.com/games/railroad-tycoon-3/downloads/railroad-tycoon-3-v104-patch-coast-to-coast-exp
- [NW] Neowin on Coast to Coast: https://www.neowin.net/news/railroad-tycoon-3-coast-to-coast-expansion/
- [TT] TV Tropes: https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/RailroadTycoon
- [SC1] Steam thread "Ghost trains?": https://steamcommunity.com/app/7610/discussions/0/1842367319518941660/
- [SC2] Steam thread "What is your Favourite Locomotives?": https://steamcommunity.com/app/7610/discussions/0/1495615865226710337/
- [SC3] Steam thread "Mystery Loco?": https://steamcommunity.com/app/7610/discussions/0/4361242944241174961
- [FD] FED2k "Grade adjustment" thread: https://forum.dune2k.com/topic/22703-grade-adjustment/
- [HB] Hawk & Badger fan utilities (has fan locomotive lists and a comparison spreadsheet for patch 1.06): https://hawkdawg.com/rrt/rrt3_extras/rrt3_utilities.php
- [EM] Series wiki page on engine maintenance: https://railroad-tycoon.fandom.com/wiki/Engine_Maintenance
- [RL] Series wiki page on reliability: https://railroad-tycoon.fandom.com/wiki/Reliability

---

## 1. Locomotive roster

### 1.1 General facts
- **Roster size [WP✓]:** nearly 60 locomotives in the base game and nearly 70 with Coast to Coast, the most of any game in the series. Engines come from the United States, Britain, France, Germany, Italy, Japan, South Africa, Poland, Russia and elsewhere, plus fictional engines (E-88, and TransEuro as a renamed Eurostar).
  - Our table in §1.2 has about 35 rows, so roughly 25 to 35 engines are still missing, likely including the Italian, Japanese, South African, Polish and Russian ones.
  - Earlier note: The Steam and GOG store pages say "over 40" [GOG: https://www.gog.com/en/game/railroad_tycoon_3]. *These conflict.* The likely reason is that the store counts only the North American / base set, or omits fictional and regional engines.
- **Era range:** The roster starts with the Planet 2-2-0 in 1829. It ends with fictional future trains: the **TransEuro** (2005, a fictional stand-in for Eurostar) and the **E-88** electric (2012, 300 mph) [WP, GN, W1]. Diesels start appearing in the 1940s [GN].
- **Stats the game shows for each engine:**
  - purchase cost (cars are free; you pay only for the engine) [GN]
  - annual maintenance [GN, M]
  - top speed, which varies with freight, express or mixed loads [SW, M]
  - a grade-climbing rating on a word scale from "Atrocious" to "Mountain King" [GN]
  - reliability, the relative chance of a breakdown or crash [M]
  - "passenger appeal" [GN]
  - fuel type: steam, diesel or electric [M]
- **Not found:** I found no published horsepower, weight or tractive-effort figures for RT3. The fan wiki table has only years, speed, cost and maintenance. These values live in the game's locomotive data files; the fan comparison spreadsheet at [HB] may expose them.
- **Regional availability:** Some early engines appear as region-specific pairs with identical stats on North American and European maps. TV Tropes gives the Grasshopper 0-4-0 and Planet 2-2-0 as an example [TT]. Scenarios can also restrict engines, for example a fan Asian map with no electrics (https://hawkdawg.com/rrt/rrt3/rrt3_asia.php). **I found no source that tabulates which region each engine belongs to (unverified).** The roster mixes American, British, German, Swiss, French, Chinese and Canadian designs.

### 1.2 Stats recovered from the fan wiki table [W1]

Columns are availability years, top speed, cost and annual maintenance. Fuel type is inferred from the real prototype.

| Locomotive | Years | Top speed | Cost | Maint./yr | Fuel | Notes |
|---|---|---|---|---|---|---|
| Planet 2-2-0 | 1829–1840 | 25 mph | $10,000 | $6,000 | Steam | |
| Adler 2-2-2 | 1835–1857 | 31 | $20,000 | $5,000 | Steam | German |
| Norris 4-2-0 | 1837–1873 | 35 | $40,000 | $8,000 | Steam | |
| Firefly 2-2-2 | 1840–1870 | 58 | $70,000 | $11,000 | Steam | British |
| Baldwin 0-6-0 | 1845–1868 | 40 | $45,000 | $5,000 | Steam | |
| Beuth 2-2-2 | 1845–1870 | 45 | $60,000 | $8,000 | Steam | German (Borsig) |
| Crampton 4-2-0 | 1852–1889 | 65 | $100,000 | $10,000 | Steam | |
| American 4-4-0 | 1855–1895 | 55 | $40,000 | $7,000 | Steam | |
| Fairlie 0-6-6-0 | 1862–1906 | 18 | $30,000 | $8,000 | Steam | grade specialist |
| Consolidation 2-8-0 | 1865–1912 | 60 | $120,000 | $9,000 | Steam | players call it the best all-rounder [SC2] |
| Stirling 4-2-2 | 1870–1905 | 75 | $140,000 | $8,000 | Steam | British |
| Duke Class 4-4-0 | 1874–1902 | 50 | $100,000 | $9,000 | Steam | British |
| Shay (2-Truck) | 1882–1930 | 23 | $40,000 | $15,000 | Steam | best steep-grade hauler [TT] |
| Eight Wheeler 4-4-0 | 1893–1927 | 102 | $180,000 | $6,000 | Steam | |
| Camelback 0-6-0 | 1896–1926 | 60 | $80,000 | $7,000 | Steam | |
| Atlantic 4-4-2 | 1910–1948 | 75 | $90,000 | $18,000 | Steam | |
| Be 5/7 | 1912–1952 | 47 | $60,000 | $17,000 | Electric | Swiss |
| Pacific 4-6-2 | 1914–1950 | 95 | $120,000 | $21,000 | Steam | |
| H10 2-8-2 | 1918–1950 | 55 | $170,000 | $16,000 | Steam | "fairly reliable" [SC2] |
| Northern 4-8-4 | 1926–1966 | 67 | $230,000 | $23,000 | Steam | |
| Zephyr | 1934–1950 | 90 | $200,000 | $10,000 | Diesel | **Coast to Coast** |
| Mallard 4-6-2 | 1935–1968 | 126 | $300,000 | $19,000 | Steam | British |
| GG1 | 1935–1985 | 100 | $310,000 | $22,000 | Electric | |
| E18 | 1936–1966 | 93 | $160,000 | $10,000 | Electric | German |
| F3 A-B | 1940–1993 | 85 | $250,000 | $16,000 | Diesel | |
| Kriegslok 2-10-0 | 1942–1967 | 50 | $100,000 | $14,000 | Steam | German |
| Big Boy 4-8-8-4 | 1945–1971 | 70 | $400,000 | $27,000 | Steam | |
| V200 | 1953–1973 | 87 | $400,000 | $25,000 | Diesel | German |
| QJ Class | 1956–2000 | 50 | $100,000 | $15,000 | Steam | Chinese, **Coast to Coast** |
| GP35 | 1963–1985 | 83 | $450,000 | $20,000 | Diesel | **Coast to Coast** |
| Class 103 | 1970–2010 | 125 | $650,000 | $15,000 | Electric | German |
| USA 103 | 1993–end | 103 | $400,000 | $20,000 | Diesel | RT2's "AMD-103" / Genesis renamed |
| TransEuro | 2005– | 191 | $1,000,000 | $25,000 | Electric | fictional |
| E-88 | 2012– | 300 | $800,000 | $22,000 | Electric | fictional |
| Orca NX 462 | 1941–end | 104 | $200,000 | $24,000 | Steam | scenario-only engine (Orient Express) [SC3] |
| 242 A1 | ? | 95 | ? | ? | Steam | **Coast to Coast**; only the speed was seen in a snippet |

**Named in RT3 sources, but stats not recovered:** GP7 (on the wiki list), U1 and Class 460 (both Coast to Coast). The Ee 3/3, TGV, Eurostar and Shinkansen figures I found were all from RT2 pages, so I did not use them.

**Not in RT3:** One RT3 forum post says RT2's John Bull, Prairie and Hudson are absent (source in [SC3]-area search results; *treat as unverified*).

### 1.3 Coast to Coast (free download, announced 20 Aug 2004, bundled with patch 1.04)
- Adds 8 locomotives, 13 maps and 3 skinning tools [NW, MD].
- Locomotives confirmed by name: **Class 460** (Swiss, Gotthard freight), **GP 35**, **U1** (a CN 4-8-2 used on Montreal–Toronto expresses), **Zephyr**, **242 A1** (Chapelon's French steam engine) and **QJ** (Chinese steam) [NW, MD, Trains.com forum https://forum.trains.com/t/railroad-tycoon-3-coast-to-coast-expansion-pack/144040].
- The other 2 additions were not identified.
- Patch 1.05 should be installed after Coast to Coast [FD]. The final patch is 1.06 [HB].

### 1.4 Engine aging and maintenance
- Maintenance is a fixed yearly cost; it does not depend on distance run [EM].
- It rises with engine age, reaching about 3× the new-engine cost by age 20 [EM].
- It rises sharply when oil is low or empty [EM].
- *[EM] is a series-wide wiki page and may describe RT2; it matches RT3's oil gauge mechanic, but confirm in RT3.*

### 1.5 Reliability and breakdowns
- The RT3 manual describes reliability as the relative chance of breaking down or crashing [M].
- Without regular maintenance, the chance of a breakdown or crash rises greatly. Servicing refills the "oil gauge" [M].
- Sandbox games have an option, **"Allow breakdown/crash for locomotives"**, which is **off by default**. With it off, engines never break down or crash, even without maintenance [M].
- From RT2, not confirmed for RT3:
  - Breakdown chance scales with distance travelled, so faster engines break down more often [RL].
  - The rating tiers run from Atrocious to Near Perfect; Near Perfect averages about 24,000 cells between breakdowns [RL].
  - Running the throttle above 85% sharply raises breakdown risk [RL].

---

## 2. Cars and consists
- **Maximum of 8 cars per train.** This was new in RT3, up from 4 in earlier games [M].
- If fewer loaded cars are waiting than the maximum, the train leaves with what is there [M].
- Each stop can have its own consist:
  - specific cargo types, or "any cargo"
  - a minimum car count
  - Players commonly set the maximum and apply it to every stop in the route [dune2k: https://forum.dune2k.com/topic/22492-how-do-i-retain-cargo-in-a-consist/, https://forum.dune2k.com/topic/22557-latercomer-discovers-rt3-and-some-questions/].
- **Automatic car setup [WP✓]:** consist selection can be automated, so a train always takes the cars that will earn the most revenue. Implement it as a per-stop "auto" option alongside manual consists.
- Cargo cannot be kept on board through an intermediate stop's consist change; this is a reported limitation [same dune2k threads].
- **Train classes:** freight, express and mixed [SW, M].
  - Passengers, mail (and troops) are express cargo.
  - Each express load has a specific destination [GameSpot review https://www.gamespot.com/reviews/railroad-tycoon-3-review/1900-6077980/; W1 cargo page https://railroad-tycoon.fandom.com/wiki/List_of_Cargo_in_Railroad_Tycoon_3].
  - The Add Train window shows the top speed separately for freight, express and mixed loads [SW].
- **Weight:** Some car types weigh more than others. Freight cars **get heavier over the course of the game**, which partly offsets newer, stronger locomotives [M]. A reviewer reads this as a balancing device rather than a physical simulation [search extract].
- **Car weights:** No numeric weights per car were found (gap).
- **Cargo decay:** Most cargo loses value with time in transit, including while the train is being serviced. Passengers and mail have waiting limits [RG, W1 cargo page].

## 3. Speed, grade and curves
- Real top speed depends on the number and type of cars and on the grade [M].
- The Add Train window shows each engine's grade performance as a percentage [M, SW].
- At close zoom, each track segment shows its grade figure; higher means slower trains [SW]. *Possible conflict:* one extract says the steepest track is drawn red; another says red in overview mode means heavy traffic and is a candidate for double track. Both overlays may exist.
- Sand affects every fuel type: an engine out of sand loses much of its grade performance [M].
- A steam engine out of water limps along at greatly reduced speed [M].
- Train speed is averaged over several track cells, so one short bump (for example at a station) has little effect [FD, player claim].
- Building stations, maintenance facilities and growing cities reshapes terrain and can change the grade of existing track. Patch 1.05 mostly fixed this [FD].
- Building track in one continuous drag smooths terrain more than laying it tile by tile [SW-area extract; series claim].
- **Not found:** an RT3 formula for speed versus grade, a maximum allowed grade, or a curve penalty. RT1 warned at grades of 1.5% or more, and RT2 forum threads discuss sharp corners slowing trains; *neither is confirmed for RT3*.

## 4. Track laying
- **Tools:**
  - single track, double track and bulldoze
  - draw over existing track to upgrade it (single to double, or plain to electric)
  - an Overpasses toggle
  - an Electric toggle once electrification becomes available
  - undo with refund
  - a one-click "electrify all track" button [SW]
- Tunnels and overpasses are supported [Steam store; SW]. A "Tunnels" setting controls how often the auto-router digs tunnels instead of following the surface [SW extract].
- **Bridges:** wood, stone and steel [SW].
  - Wood is cheapest but single-track only.
  - Stone and steel both carry double track.
  - Steel appears later and costs slightly less than stone.
  - RT1 bridge prices ($50k, $200k, $400k) and RT1 flooding rules are *not RT3*.
- **Tunnels:**
  - RT1: tunnels are level and cannot be double-tracked.
  - RT3: double-track tunnels are unverified.
- **Electric track:** Electric engines need electrified track on their whole route; it costs more than plain track [SW; RT2 sources]. No RT3 price was found.
- **Track costs:** No RT3 per-tile figures were found for terrain, cities, double track or electric track. They appear in the in-game build cost readout.
- **Track maintenance (RT2, unconfirmed for RT3):**
  - charged monthly at 0.5% of total track value (6% a year) [https://railroad-tycoon.fandom.com/wiki/Income_Statement_(Railroad_Tycoon_II)]
  - double track reportedly costs 2× to maintain; electrification adds roughly 50% (one forum poster's estimate)
- **Gauges:** Fan scenarios use gauge differences (for example Queensland versus NSW, or standard versus broad gauge in Europe). This may come from scenario scripting or from track ownership rules (unverified) [hawkdawg Australasia and Europe map pages].

## 5. Servicing buildings
- The RT3 manual names two support structures besides stations, and both can be placed almost anywhere on track [M]:
  - **Service tower:** combines the water tower and sand tower.
  - **Maintenance facility:** wrench icon; refills oil (maintenance). Players also call it a "maintenance shed"; one guide calls it a "roundhouse", RT2's name [RG].
- Trains stop automatically at a service building they pass if water, sand or oil is low. Players can also schedule these stops manually [M].
- **A train being serviced stops completely, and its cargo keeps decaying** [RG].
- The common strategy is hub-and-spoke routing that places service buildings where trains run empty [RG].
- Station add-on buildings (hotels, post offices and so on) exist, but I did not research them.

## 6. Routing, orders, priority and collisions
- **Route:** an ordered list of stops that loops back to the start [Steam threads, search extract]. Each stop carries its own consist orders (see §2). Service and maintenance stops can be added as scheduled stops [M].
- **Priority:** used to decide which train yields when two meet [M].
  - A higher-priority train of the same company always gets right of way.
  - A train on another company's track always yields to the owner's trains, whatever its priority.
  - Players often give express trains priority over freight [Steam threads].
- **No signals and no real collisions in normal play [WP✓ for passing and no signal towers]:** Wikipedia confirms that trains pass each other on single track, as on the original Railroad Tycoon's lowest difficulty, and that signal towers are not needed.
  - Trains can pass "through" each other on single track [SC1].
  - When two trains meet, the lower-priority train (players say the less profitable one) stops while the other passes; players describe this as an abstracted siding meet [SC1].
  - Double track reduces these waits.
  - Players note there are no collision or explosion effects [SC1].
- The manual does mention locomotives that "crash" as a breakdown-type random event tied to reliability and maintenance, switchable in sandbox [M]. **So "crash" in RT3 means a random failure event, not a collision between trains.** This is my interpretation of the extracts, so flag it as such.
- **Track rights:** Trains can run on rival companies' track at low priority [M]. Fees are not researched.

## 7. Conflicts and gaps
1. Roster size: "over 40" (store pages) versus "nearly 60/70" (Wikipedia).
2. Several stats differ between the RT2 and RT3 wiki pages for the same engine:
   - Big Boy: $400k / 70 mph in RT3; 68 mph / 1941–1955 in RT2
   - GG1: retires 1985 in RT3; 1970 / $285k in RT2
   - E18: $160k / $10k maintenance in RT3; $93k / $16k on its standalone page
   - Use only the RT3 table, and confirm it in game.
3. Red track overlay: steep grade or heavy traffic.
4. Missing entirely: horsepower, weight, exact reliability and grade-tier values per engine; car weights; track, bridge and tunnel prices; region tags for each engine; the grade-to-speed formula; curve effects; 2 of the 8 Coast to Coast engines. The best path to these is the game's own data files, or the fan spreadsheet and 1.06 locomotive-list PDFs linked from [HB].
5. Everything in this report comes from search-engine extracts rather than full-page reads, because the proxy blocked page fetches. Rows were rebuilt piece by piece from [W1] snippets and could contain transcription errors.

All content above is paraphrased; no large verbatim passages were copied.