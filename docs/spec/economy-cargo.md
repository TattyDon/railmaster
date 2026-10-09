# Railroad Tycoon 3 (2003) and Coast to Coast: cargo and economy research for a clean-room reimplementation

**How this was gathered:** WebFetch was blocked by the egress proxy for every relevant domain (Wikipedia, fandom, StrategyWiki, GameFAQs, Steam, dune2k, manualmachine, archive.org, neoseeker and others). Every fact below therefore came through WebSearch result summaries of those pages, not from reading the pages myself. Treat the numbers as second-hand. Before you hard-code anything, check it against the source pages or the game's own data files.

Confidence tags: **[RT3]** means a source confirms it for RT3. **[RT2/RT1]** means it comes from an earlier game in the series and is a hint only. **[UNCERTAIN]** means the sources conflict or the point is unverified.

---

## 1. The core economic model [RT3]

- **The map is a network of economy nodes.** Phil Steinmeyer (PopTop president) said in a pre-release interview that a typical map has about **15,000 economy nodes** spread evenly across it, not only in cities. RT2 was different: each city simply supplied or demanded cargo, and only the player's railroad moved it. He called the economy the feature he was most proud of, and said it was designed to be harder to exploit. He also stressed that before railroads, moving cargo inland was hard and costly, especially over mountains, so linking the interior to coasts and river towns is a key strategy. https://www.gamespot.com/articles/railroad-tycoon-3-qanda/1100-6076739/
- **Cargo flows on its own along a price field.** Carloads drift slowly across the map along the gradient of a scalar price field, standing in for road and water transport. Supply sites act as sources and demand sites as sinks. So raw materials can reach industries and be processed with no trains at all. https://en.wikipedia.org/wiki/Railroad_Tycoon_3 (via search summary)
- **Revenue is the price difference, not distance.** A delivery pays according to the gap between the price at pick-up and the price at delivery. A train does not have to collect goods at their source. https://en.wikipedia.org/wiki/Railroad_Tycoon_3, https://strategywiki.org/wiki/Railroad_Tycoon_3/Gameplay
- **Rivers and other modes compete with the railroad.** Freight also moves overland and along rivers. If shipping by river between two rail-linked cities is cheaper, shippers use the river. https://www.gamingnexus.com/Article/380/Railroad-Tycoon-3, https://www.gamespot.com/reviews/railroad-tycoon-3-review/1900-6077980/
- **Stations attract freight when service is good.** Once a depot is built, nearby freight starts moving toward it. Prompt pickup and delivery makes freight prefer that depot; poor service makes it pass the depot by. This suggests the depot's effective price or attractiveness depends on service quality. https://www.gamingnexus.com/Article/380/Railroad-Tycoon-3 (review wording, so the exact mechanism is **[UNCERTAIN]**)
- **Economic cycles and random events** have a large effect on how much cargo is available. https://www.gamespot.com/reviews/railroad-tycoon-3-review/1900-6077980/
- **Players see cargo through a cargo overview map.** For each good it shows high price, low price, sources and consumers. Red marks cheaper locations and green marks more expensive ones. Players report goods moving from low-price to high-price areas. https://steamcommunity.com/app/7610/discussions/0/3075370188236927642/
- **Oversupply pushes the local price down.** Finished goods hit a price floor quickly in a town when supply exceeds what the population can consume. Industries have very large demand and are hard to oversupply. Players have pushed a city's demand to zero (one load of tires zeroed Detroit's tire demand), after which nothing more ships there. https://steamcommunity.com/app/7610/discussions/0/3075370188236927642/, https://forum.dune2k.com/topic/22616-manually-forcing-a-cargo-move-even-if-no-demand-at-station/
- **Trains unload at the first station with a higher price.** Players report that a consist unloads at the next stop if the price there is even $1 higher. One player recommends planning chains so each hop gains at least about $5k. This implies the AI unloads greedily wherever price exceeds pick-up price. https://forum.dune2k.com/topic/22616-...
- **A waiting train is not demand.** A train sitting at a station does not create demand for a commodity. Cargo only loads if some destination wants it. https://forum.dune2k.com/topic/22616-...
- **Steinmeyer's later view:** in 2006 he said the RT3 model may have confused players more than RT2's, but he preferred it. Search summary of philsteinmeyer.com blog comments.
- **[UNCERTAIN] No exact price or revenue formula found.** One player claimed pay rises with distance travelled. That contradicts the price-difference model and is probably a side effect: distant locations tend to have bigger price gaps.

## 2. Passengers, mail and troops (express cargo) [RT3]

- **Passengers and mail travel to specific destinations.** They only appear if your network connects to the place they want to go. https://www.gamespot.com/reviews/railroad-tycoon-3-review/1900-6077980/
- **Each city has a mail demand cap.** The station's mail panel shows how much each city demands, and deliveries beyond that are not paid. https://forum.dune2k.com/topic/22616-... (forum, via search summary)
- **Passenger demand grows with connections.** The more cities a player connects, the higher the demand for passenger service. https://theravenscall.substack.com/p/railroad-tycoon-iii-and-the-dire (opinion piece)

## 3. Cargo list [RT3]

Source for the whole table: https://railroad-tycoon.fandom.com/wiki/List_of_Cargo_in_Railroad_Tycoon_3, a fan wiki read through search summaries. Its columns are Name, Type, Year available, Base price, Delivery-time sensitivity, Produced at and Consumed at.

**About the sensitivity column:** the wiki never defines the 1–10 scale. Milk and mail are 10, passengers 9, livestock and produce 8, troops 7, while steel, coal and goods are 1. That pattern strongly suggests a higher number means more perishable, i.e. faster loss of value. **[UNCERTAIN]**

| Cargo | Type | Year | Base $ | Sens. | Produced at | Consumed at |
|---|---|---|---|---|---|---|
| Alcohol | Freight | 1800 | 100 | 2 | Brewery, Distillery | Barracks, House |
| Aluminum | Freight | 1910 | 85 | 1 | Aluminum Mill, Recycling Plant | Tool and Die |
| Ammunition | Freight | 1848 | 160 | 2 | Munitions Factory | Barracks |
| Automobiles | Freight | 1900 | 200 | 3 | Auto Plant | House |
| Bauxite | Freight | 1910 | 30 | 1 | Bauxite Mine | Aluminum Mill |
| Cheese | Freight | 1880 | 235 | 5 | Dairy Processor | House |
| Chemicals | Freight | 1905 | 30 | 1 | Chemical Plant | Fertilizer Factory, Munitions Factory |
| Clothing | Freight | 1800 | 95 | 3 | Textile Mill | House |
| Coal | Freight | 1800 | 30 | 1 | Coal Mine | Electric Plant, Steel Mill, House (until 1910) |
| Coffee | Freight | 1800 | 45 | 2 | Coffee Farm | House |
| Corn | Freight | 1800 | 25 | 3 | Corn Farm | Cattle Ranch, Dairy Farm, House |
| Cotton | Freight | 1800 | 30 | 2 | Cotton Farm | Textile Mill |
| Diesel | Freight | 1890 | 100 | 1 | (none listed) | (none listed) |
| Fertilizer | Freight | 1905 | 80 | 2 | Fertilizer Factory | Coffee, Corn, Cotton, Grain, Rice and Sugar farms, Fruit Orchard |
| Furniture | Freight | 1880 | 220 | 1 | Furniture Factory | House |
| Goods | Freight | 1800 | 170 | 1 | Tool and Die | House |
| Grain | Freight | 1800 | 30 | 3 | Grain Farm | Brewery, Bakery, House |
| Iron | Freight | 1800 | 30 | 1 | Iron Mine | Steel Mill, Munitions Factory, Tool and Die (until 1877) |
| Livestock | Freight | 1800 | 90 | 8 | Cattle Ranch | Meat Packing Plant |
| Logs | Freight | 1800 | 30 | 2 | Logging Camp | Lumber Mill |
| Lumber | Freight | 1800 | 85 | 1 | Lumber Mill | Furniture Factory, Weapons Factory, House, Toy Factory (until 1956) |
| Mail | Express | 1800 | (blank) | 10 | House | House |
| Meat | Freight | 1800 | 195 | 5 | Meat Packing Plant | House |
| Milk | Freight | 1800 | 110 | 10 | Dairy Farm | Dairy Processor, House |
| Oil | Freight | 1860 | 40 | 1 | Oil Well | Electric Plant, Plastics Factory, House (until 1910) |
| Paper | Freight | 1800 | 85 | 1 | Paper Mill, Recycling Plant | Commercial Building, House |
| Passengers | Express | 1800 | (blank) | 9 | House | House |
| Plastic | Freight | 1935 | 85 | 1 | Plastics Factory | Tool and Die, Toy Factory |
| Produce | Freight | 1800 | 45 | 8 | Fruit Orchard | Distillery, House |
| Pulpwood | Freight | 1800 | 30 | 2 | Logging Camp | Paper Mill |
| Rice | Freight | 1800 | 30 | 2 | Rice Farm | Brewery, House |
| Rubber | Freight | 1900 | 30 | 1 | (none listed; probably port-supplied) | Tire Factory |
| Steel | Freight | 1856 | 85 | 1 | Steel Mill, Recycling Plant | Auto Plant, Munitions Factory, Tool and Die, Weapons Factory |
| Sugar | Freight | 1800 | 35 | 2 | Sugar Farm | Distillery, Bakery, House |
| Tires | Freight | 1900 | 85 | 1 | Tire Factory | Auto Plant, Weapons Factory |
| Toys | Freight | 1880 | 175 | 3 | Toy Factory | House |
| Troops | Express | 1848 | (blank) | 7 | Barracks | Barracks |
| Uranium | Freight | 1950 | 200 | 3 | Uranium Mine | Nuclear Power Plant |
| Waste (Recyclables) | Freight | 1990 | 40 | 6 | House | Recycling Plant |
| Weapons | Freight | 1848 | 235 | 2 | Weapons Factory | Barracks |
| Wool | Freight | 1800 | 30 | 1 | Sheep Farm | Textile Mill |

That is 41 cargos. Marketing copy says "over 35 cargo types".

**Gaps and flags**
- **Diesel** has no producer or consumer in the wiki. It may be a leftover or used only by ports. **[UNCERTAIN]**
- **Rubber** has no listed producer; it is likely supplied by ports. **[UNCERTAIN]**
- **Bakery** is listed only as a consumer (grain and sugar). Its output does not appear in the RT3 table. In RT2 the bakery made "Food", but RT3 has no Food cargo. Either the output is missing from the wiki or the bakery is a pure sink. **[UNCERTAIN]**
- **Missing cargos:** Food, Bread, Electricity, Gold, Nitrate, Petroleum and Tools are absent from the RT3 list.
- **Sinks that produce nothing:** Electric Plant (coal and oil), Nuclear Power Plant (uranium), Commercial Building (paper) and Barracks (alcohol, ammunition, weapons, and troops in and out) appear to consume without producing cargo. A forum player reports that electric plants can still be profitable and can upgrade. https://forum.dune2k.com/topic/22495-help-with-custom-consist/
- **Unofficial 1.06 patch:** reportedly adds cargos and allows passengers in port recipes. Its additions are not part of the retail game.

### Coast to Coast
Coast to Coast was a free 2004 add-on bundled with patch 1.04. Its listed content is:
- a large map of the whole USA, plus maps of Russia, Poland, China, Mexico, Ireland and others;
- 8 new locomotives (Class 460, GP35, U1, Zephyr and others);
- tools for painting trains, designing logos and adding player portraits;
- a cumulative "108 new features, balance tweaks and revisions".

**No source mentions new cargos or industries in Coast to Coast.** **[UNCERTAIN]** Note that the fandom table does not mark which game version each cargo belongs to. https://www.neowin.net/news/railroad-tycoon-3-coast-to-coast-expansion/, https://www.bluesnews.com/s/50893/railroad-tycoon-3-patch-free-expansion, https://www.moddb.com/games/railroad-tycoon-3/downloads/railroad-tycoon-3-v104-patch-coast-to-coast-exp

## 4. Production chains (derived from the table above) [RT3]

- **Steel:** Iron + Coal → Steel Mill → Steel. Steel then feeds the Auto Plant, Munitions, Tool and Die and Weapons Factory. A steel mill with two inputs **produces nothing unless both inputs are supplied**, per the RT3 manual. https://manualmachine.com/gamespc/railroadtycoon3/1118742-user-manual/
- **Livestock:** Corn → Cattle Ranch → Livestock → Meat Packing → Meat → House. This implies the ranch can take corn as an input, a boost or a requirement; which one is **[UNCERTAIN]**.
- **Dairy:** Corn → Dairy Farm → Milk → Dairy Processor → Cheese → House.
- **Timber:** Logging Camp → Logs → Lumber Mill → Lumber → Furniture Factory → Furniture. The same logging camp also produces Pulpwood → Paper Mill → Paper.
- **Textiles:** Cotton or Wool → Textile Mill → Clothing.
- **Alcohol:** Grain or Rice → Brewery → Alcohol. Separately, Produce or Sugar → Distillery → Alcohol.
- **Aluminum:** Bauxite → Aluminum Mill → Aluminum → Tool and Die.
- **Tool and Die:** Steel, Aluminum, Plastic and Iron (iron only until 1877) → Goods.
- **Plastics and toys:** Oil → Plastics Factory → Plastic → Toy Factory (also takes Lumber until 1956) → Toys.
- **Autos:** Rubber → Tire Factory → Tires → Auto Plant (with Steel) → Automobiles.
- **Fertilizer:** Chemical Plant → Chemicals → Fertilizer Factory → Fertilizer → crop farms. Fertilizer is presumably a production booster for farms. **[UNCERTAIN]**
- **Munitions:** Chemicals + Iron + Steel → Ammunition. Separately, Lumber + Steel + Tires → Weapons. Both go to the Barracks.
- **Recycling:** Waste (from houses) → Recycling Plant → Steel, Aluminum and Paper.

**Conversion ratios: none found for RT3.** RT2's steel recipe was 1 iron + 1 coal → 2 steel (https://en.wikipedia.org/wiki/Railroad_Tycoon_II). Treat that only as a hint **[RT2]**.

- **Most lucrative chains** in player opinion: steel, and meat packing. https://theravenscall.substack.com/p/railroad-tycoon-iii-and-the-dire

## 5. Industries (ownership and profit) [RT3]

- **Buy, build and upgrade.** Per the manual, players can buy any industry on the map, build new ones, and upgrade owned ones to raise capacity. Upgrades raise overhead, so an under-supplied upgraded factory can swing into loss. https://manualmachine.com/gamespc/railroadtycoon3/1118742-user-manual/
- **Buying cheap before a turnaround.** An unprofitable industry is cheap to buy, and the manual suggests buying it before your line reaches it so you capture the turnaround. (same source)
- **Three income streams.** Hauling inputs to a factory and outputs to consumers roughly doubles hauled volume, and owning the factory adds its own profit as a third stream. Non-player agents form the market for each cargo, and players cannot control them directly. (same source)
- **Factory profit model.** Profit equals the value of the output minus input cost and labor. A factory is profitable when it buys inputs cheaply and sells its products at high prices. https://steamcommunity.com/app/7610/discussions/0/620700960756515594/ and search summaries
- **Purchase price.** One player example: a refinery earning about $750K a year sold for about $7.5M, i.e. roughly **10× annual profit**. A Substack essay also says about 10× annual profit. A search summary described this as "8–10% of profit", which is a misstatement because the example itself works out to 10×. **[UNCERTAIN]** These are player estimates, not a documented formula.
- **Build cost and return.** Example: about $1.5M to build, earning about $700K a year after roughly a decade (forum). **[UNCERTAIN]**
- **Demand ramps up slowly.** Demand for a new or bought industry's inputs builds over time, so trains may not start serving it for a year or two. https://gamefaqs.gamespot.com/pc/534361-railroad-tycoon-3/answers/292342-when-i-buy-an-industry-it-doesnt-grow
- **Unowned receivers upgrade on demand.** Ports, warehouses and electric plants that the player does not own upgrade automatically based on demand. https://forum.dune2k.com/topic/26439-warehouses-and-ports/
- **New industries appear** as the economy develops, and the player can buy them. https://www.gamespot.com/reviews/railroad-tycoon-3-review/1900-6077980/
- **Cheat:** a code doubles building production rates, which is useful for testing. https://gamefaqs.gamespot.com/pc/534361-railroad-tycoon-3/cheats

## 6. Houses, towns and growth [RT3, mostly community observation]

- **Houses are the basic population unit.** They produce and consume Passengers and Mail, produce Waste, and consume many final goods. Coal and Oil stop being house consumables after 1910. (Section 3 table)
- **Commercial Buildings** consume Paper.
- **What makes towns grow.** Growth is driven by cargo moving in and out: deliveries of goods the town demands, plus shipping out its local production. More connected cities also helps. Once a network is efficient, towns keep growing on their own, adding houses and businesses. https://steamcommunity.com/app/7610/discussions/0/3198119199538466276/, https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/RailroadTycoon, https://www.gamespot.com/reviews/railroad-tycoon-3-review/1900-6077980/
- **Some randomization.** Cities are partly randomized each time a map is played.
- **No formula found** for when or where houses spawn. **[UNCERTAIN]**

## 7. Stations [RT3]

- **Three sizes:** Small $50K, Medium $100K, Large $200K. Size sets the radius used to gather cargo. A station can be upgraded later if the space is not obstructed. https://strategywiki.org/wiki/Railroad_Tycoon_3/Gameplay
- **Increasing area of effect** with size. A station inside city limits also picks up nearby industries. https://www.gamingnexus.com/Article/380/Railroad-Tycoon-3
- **Players default to large stations** even in villages to capture as much cargo as possible. https://forum.quartertothree.com/t/railroad-tycoon-3/6792
- **Radius in tiles: NOT FOUND for RT3.** The fandom "Station" page gives a 2-tile radius for a $100K station, but that page covers the original 1990 Railroad Tycoon, whose prices happen to match ($50K Depot, $100K Station, $200K Terminal). **[RT1, UNCERTAIN]**

## 8. Station-area buildings [RT3]

The RT3 manual says these buildings are placed freely in the world, not on a separate station screen.

| Building | Effect | Source |
|---|---|---|
| Post Office | Earns nothing directly. Mail waits longer, losing value more slowly. | Steam routing guide 284155250; manualmachine RT3 manual |
| Hotel | Earns from passengers transferring, since they stay between trains. Hotels compete: the one closest to the station gets the largest share. Passengers at a hotel station also lose value more slowly (forum). | same; https://forum.dune2k.com/topic/22616-... |
| Restaurant | Earns per passenger passing through, whether or not they transfer. | same |
| Tavern | Earns only when a passenger boards a train. | same |
| Restaurant, Hotel, Tavern (shared) | Revenue scales with passenger traffic. One building can serve several stations if they are close together. | RT3 manual (search summary) |
| Maintenance facility | "Relatively expensive." A train passing on the track stops automatically for maintenance and an oil refill when needed. | RT3 manual |
| Service tower | Combines water and sand. Steam games typically need one between each pair of stations. | RT3 manual |
| Warehouse | Stores cargo longer and avoids spoilage ("depreciation"). Completes the commodity market the way ports do. Its profit is the loss it saves. | https://forum.dune2k.com/topic/26439-warehouses-and-ports/; Wikipedia |
| Port | Each port can be set to receive, supply or exchange cargo. Recipes are editable in the map editor's Port/Warehouse Cargos tab; passengers are not allowed there without the 1.06 patch. | dune2k 26439 |

- **Costs for these buildings: NOT FOUND.**
- **Earning examples:** one player built hotels everywhere and earned about $0.5M a year, small next to rail and industry income.
- **RT1 Deluxe numbers, hint only [RT1]:** restaurant +$2000 per full passenger car, hotel +$5000; a maintenance shop cut train maintenance by 75%.
- **Customs house:** confirmed only for RT2, not RT3.

## 9. Cargo aging and decay [RT3]

- **Most cargo loses value over time** and disappears once it is worth nothing. Perishables decay faster than steel or finished goods. Passengers give up waiting, and mail has a limited shelf life. https://steamcommunity.com/sharedfiles/filedetails/?id=284155250
- **Decay is slower at a station than on a train.** The routing guide says decay inside a station's influence area is slower than while the cargo is loaded on a train. A post office (for mail), a hotel (for passengers) and a warehouse (for freight) slow it further. (same source; dune2k)
- **Practical tip from the guide:** service trains only when they are empty, to protect express and perishable revenue. (same source)
- **The sensitivity column (1–10) in Section 3** is likely the decay rate, but this is **[UNCERTAIN]**.
- **No decay timers or formula found.**
- **Older-game hints [RT1/RT2]:** pay falls along the route as a function of time and distance, mail falls fastest, and bulk cargo is nearly insensitive. One RT2 forum theory: a theoretical revenue is placed on the train at departure and then reduced during the trip by a per-cargo time factor. https://forum.dune2k.com/topic/23978-how-to-calculate-revenue

## 10. Operating costs [RT3 where marked]

- **Engine maintenance [RT3 fandom]:** a fixed yearly charge per locomotive, independent of distance. It rises with age to about 3× the new value by year 20, and rises sharply when oil is low or empty. https://railroad-tycoon.fandom.com/wiki/Engine_Maintenance
- **Locomotive prices and maintenance [RT3 fandom]:** annual maintenance ranges from $5K to $43K (the $43K cases are the ET22 and HST 125). Examples: Pacific 4-6-2 costs $120K with $21K maintenance; Class 55 Deltic $15K; Shay (2-truck) $15K. https://railroad-tycoon.fandom.com/wiki/List_of_Locomotives_in_Railroad_Tycoon_3
- **Oil [likely RT3, described in RT2 docs]:** oil depletes with distance travelled, and low oil raises breakdown risk. A maintenance facility or roundhouse refills it. https://railroad-tycoon.fandom.com/wiki/Locomotives_(Railroad_Tycoon_II)
- **Reliability [RT3 manual]:** a locomotive's reliability rating is its relative chance of breaking down or crashing.
- **Water and sand [RT3 manual]:** a steam engine that runs out of water slows sharply. Any locomotive without sand, diesel and electric included, loses much of its climbing performance on grades.
- **Fuel [RT2 wiki]:** a variable cost based on distance and cargo weight, so faster engines burn more.
- **Track maintenance and overhead [RT2]:** listed as expenses in RT2. **No RT3 per-mile figures found.**
- **Difficulty [RT3]:** Expert difficulty raises track-laying, maintenance and fuel costs. https://railroad-tycoon.fandom.com/wiki/Railroad_Tycoon_3

## 11. Key gaps to fill from the game files or the full manual

1. The exact price-field and diffusion formula and how node prices update.
2. The revenue formula, including the time-decay function and how the sensitivity scale maps to it.
3. Station radii in tiles.
4. Building costs and income rates.
5. Industry conversion ratios and production rates per upgrade level.
6. The Bakery's output, and the Diesel and Rubber sources.
7. House spawn rules.
8. Fuel and track cost rates.

Better sources if they become reachable:
- Scott "Zoogz" Jamison's GameFAQs/Neoseeker FAQ, which has sections on town consumption, the transportation model and supply/demand;
- the full RT3 manual (manualmachine.com);
- the FED2k (forum.dune2k.com) RT3 threads, including "Industry and Locomotive Charts Requested" (topic 22669).

For a clean-room project, inspecting the game's plain-text data files counts as observing behavior. Copying their contents is a separate legal and judgement call for the project.
