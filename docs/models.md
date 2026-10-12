# Models: art for the 3D client

The 3D client draws everything in code, but any object can instead come
from a glTF model (`.glb` or `.gltf`, with its textures). The models live
under `data/models/`, and `data/models/models.json` says which files play
which role. A role with no models, or whose files are missing or broken,
keeps being drawn in code, so art can be added one piece at a time.

```json
{
  "roles": {
    "house": ["my-pack/house-a.glb", "my-pack/house-b.glb"],
    "engine_steam": ["my-pack/locomotive.glb"],
    "industry:coal_mine": ["my-pack/mine.glb"]
  }
}
```

When a role lists several models, each object picks one (the same one
every time), so a town gets a mix of houses.

## Roles

| Role | What it replaces | How it is fitted |
|---|---|---|
| `house` | Cottages and two-storey houses in towns | To its plot, front to the street |
| `terrace` | Terraced rows | To its plot (neighbours join up) |
| `shop` | Shops and offices in town centres | To its plot, front to the street |
| `church` | Each town's church | To its plot |
| `station_small`, `station_medium`, `station_large` | The station building (platforms and canopies stay) | Along the platforms, front to the track |
| `engine_steam`, `engine_diesel`, `engine_electric` | Locomotives | Its length to the engine's, front forwards |
| `car:<cargo>` (e.g. `car:coal`, `car:lumber`, `car:oil`; keys in `data/cargo.json`) | A car carrying that cargo | Its length to the car's |
| `car_coach`, `car_mail`, `car_diner`, `car_boxcar`, `car_hopper`, `car_caboose` | Cars with no model for their cargo | Its length to the car's |
| `tree_broadleaf`, `tree_conifer` | The woods' trees | To each tree's height, turned at random |
| `industry:<key>` (e.g. `industry:coal_mine`, `industry:steel_mill`; keys in `data/industries.json`) | That industry's buildings | To 70% of its site, in proportion |
| `service_tower`, `engine_shed` | Water towers and engine sheds by the track | The tower to its height, the shed along the track, facing it |
| `props:<key>` | Nothing: a cluster of props in the corner of the industry's site (log piles, ore heaps, crates) | About half a site across |
| `beside:<key>` | Nothing: stands beside the industry's own shapes (a windmill by the fields) | To about half a site tall |

Models are scaled to fit: buildings across their plot and along it, with
height scaling with the width; engines and cars to their length, keeping
their proportions; trees to their height. They stand on their lowest
point. Cars pick a model by train, so a railroad's freight varies. Engines are
picked by owner: the player's always get the role's first model, rivals'
the others, and an engine from the art flies a pennant in its owner's
colour (the player's gold).

## Prefabs: models assembled from pieces

Modular kits (walls, roofs, chimneys that fill a one-unit cell) can be
assembled into buildings. A prefab lists pieces with their position in
map axes (x east, y south, z up), a turn about the up axis in degrees (+x
towards +y) and an optional scale, plus an optional `texture` replacing
every piece's palette:

```json
"prefabs": {
  "cottage": {
    "texture": "my-kit/Textures/palette.png",
    "pieces": [
      { "file": "my-kit/wall.glb", "at": [0, 0, 0], "turn": 90 },
      { "file": "my-kit/roof.glb", "at": [0, 0, 1], "turn": 0, "scale": [1, 1, 1.6] }
    ]
  }
}
```

Prefabs can sit in `models.json` or in `prefabs.json` beside it, and roles
name them as `"prefab:cottage"`. The town's houses, terraces, shops and
church are prefabs written by `tools/models/town_prefabs.py` (edit the
script and rerun it from the repository root rather than editing
`prefabs.json`).

Town buildings are fitted along their plot's street front and may be
stretched up to 1.8 times deeper to fill the plot.

## Fitting colours

A pack's palette may not suit the game. `material_colors` sets any
material's colour by its name, for every model that uses it, as a screen
(sRGB) colour from 0 to 1:

```json
"material_colors": { "leafsGreen": [0.28, 0.5, 0.2] }
```

## Making models fit

- **Axes:** glTF's usual axes, y up. The model's front (doors, the
  locomotive's smokebox) should face +z, as most exported models do.
- **Origin:** anywhere; the client centres the model on its footprint.
- **Size:** any units; the client scales it.
- **Materials:** the base colour factor and base colour texture are used
  (PNG or JPEG, beside the model or embedded in a `.glb`), plus vertex
  colours if present. Other material features are ignored.
- **Detail:** a few hundred to a couple of thousand triangles each. Towns
  and trains draw many.

## What ships

- `kenney-train-kit/`: from Kenney's Train Kit (CC0): steam, diesel and
  electric engines, coaches, boxcars, and coal, ore, log, lumber and tank
  wagons.
- `kenney-nature-kit/`: trees from Kenney's Nature Kit (CC0), recoloured
  through `material_colors`. Their files had their scene's root node
  corrected (the exporter listed a child node as a root, which cgltf
  rejects); nothing else was changed.

- `kenney-fantasy-town/`: timber and stone walls, gable roofs, spires and
  chimneys from Kenney's Fantasy Town Kit (CC0), assembled into the town's
  cottages, houses, terraces, shops and church, with period recolourings
  of its palette (slate and terracotta roofs, grey stone).

Stations (a timber depot, a two-storey station, a hall with a clock
tower), a windmill (beside grain and corn farms) and a watermill (the
lumber mill) are assembled from the same kit.
- `kenney-industrial/`: works from Kenney's City Kit (Industrial) (CC0) on
  a brick recolouring of its palette: sawtooth-roofed mills and factories,
  a works with three stacks (steel and aluminium), gabled works (breweries,
  packing plants), tank works (chemicals, refineries), a power station and
  cooling tower, warehouses and barracks, plus the water tower and engine
  shed by the track.

- `kenney-survival/`: props from Kenney's Survival Kit (CC0) on a
  weathered recolouring: log piles, stumps and a tent at logging camps,
  stone and ore heaps with crates at mines, barrels and crates at ports
  and warehouses.

Mines, farms, logging camps, oil wells and ports keep their code-drawn
headframes, fields and derricks, which say what they are; the props stand
beside them.

## Where to get art

Use art whose licence lets it be shared with the game: CC0 (public
domain) is simplest, and CC-BY is fine if you credit the author. Put each
pack in its own folder under `data/models/` with its licence file beside
it. Kenney (kenney.nl) and Quaternius (quaternius.com) publish many CC0
low-poly packs. Choose period-appropriate ones: the game starts in 1830,
and modern city blocks look wrong in a frontier town.

The client prints `models: N loaded for M roles` when it starts, and
names any file it could not read.
