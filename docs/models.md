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
| `car_coach`, `car_mail`, `car_diner`, `car_boxcar`, `car_hopper`, `car_caboose` | Cars | Its length to the car's |
| `industry:<key>` (e.g. `industry:coal_mine`, `industry:steel_mill`; keys in `data/industries.json`) | That industry's buildings | To 80% of its site |

Models are scaled to fit: across the plot (or the train's width) and
along it, with height scaling with the width. They stand on their lowest
point.

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

## Where to get art

Use art whose licence lets it be shared with the game: CC0 (public
domain) is simplest, and CC-BY is fine if you credit the author. Put each
pack in its own folder under `data/models/` with its licence file beside
it. Kenney (kenney.nl) and Quaternius (quaternius.com) publish many CC0
low-poly packs. Choose period-appropriate ones: the game starts in 1830,
and modern city blocks look wrong in a frontier town.

The client prints `models: N loaded for M roles` when it starts, and
names any file it could not read.
