#!/usr/bin/env python3
"""Lay out town buildings from Kenney's Fantasy Town Kit pieces.

Writes data/models/prefabs.json: each prefab is a list of kit pieces, placed
cell by cell, that the client compiles into one model (docs/models.md).
Run from the repository root: tools/models/town_prefabs.py

The kit's pieces fill a one-unit cell: a wall stands on the cell's +x face,
so walls on other faces are turned (degrees about the up axis, +x towards
+y). Positions are in map axes (x east, y south, z up). A building runs
along x, one cell deep, its front (door) on the +y side, as the client
expects a model's front.
"""
import json
from pathlib import Path

PACK = "kenney-fantasy-town"
FRONT, BACK, LEFT, RIGHT = 90, 270, 180, 0  # turns for each face


def piece(name, x, y, z, turn=0, scale=None, spin=None):
    p = {"file": f"{PACK}/{name}.glb", "at": [x, y, z], "turn": turn}
    if scale:
        p["scale"] = scale
    if spin:
        # A part the client turns each frame: about its own axis, at rpm.
        p["spin"] = spin
    return p


# Roofs stand this much taller than the kit's, for a steep period pitch
# that survives the client stretching buildings deeper to fill a plot.
ROOF_RISE = 1.6


def building(length, floors, material, palette, roof="roof-gable", door="door", windows="shutters", chimney=True):
    """A one-cell-deep building `length` cells long and `floors` high.

    material: "wood" or "stone"; palette: the recoloured kit palette.
    """
    wall = "wall-wood" if material == "wood" else "wall"
    window = f"{wall}-window-{windows}"
    door_piece = {"door": f"{wall}-door", "wide": "wall-doorway-square-wide"}[door]
    pieces = []
    for f in range(floors):
        for i in range(length):
            # Front: the door in the middle of the ground floor, windows elsewhere.
            front = door_piece if f == 0 and i == length // 2 else window
            pieces.append(piece(front, i, 0, f, FRONT))
            pieces.append(piece(window if (i + f) % 2 == 0 else wall, i, 0, f, BACK))
        pieces.append(piece(wall, 0, 0, f, LEFT))
        pieces.append(piece(window if floors > 1 and f == floors - 1 else wall, length - 1, 0, f, RIGHT))
    # The roof: a gable along the building (the kit's gable piece runs its
    # ridge along x and closes both its ends).
    if roof:
        for i in range(length):
            pieces.append(piece(roof, i, 0, floors, 0, [1, 1, ROOF_RISE]))
    if chimney:
        pieces.append(piece("chimney", length - 1, 0, floors - 0.3, 0))
    return {"texture": f"{PACK}/Textures/colormap-{palette}.png", "pieces": pieces}


def back_wing(b, x, floors, material, chimney=False, reach_ridge=False):
    """A wing one cell deep behind the cell at x, `floors` high, its gable
    running back from the house. With reach_ridge its roof runs on into
    the main roof's, as a cross gable (when both are the same height)."""
    wall = "wall-wood" if material == "wood" else "wall"
    window = f"{wall}-window-small"
    for f in range(floors):
        b["pieces"].append(piece(window if f == floors - 1 else wall, x, -1, f, BACK))
        b["pieces"].append(piece(window, x, -1, f, LEFT))
        b["pieces"].append(piece(wall, x, -1, f, RIGHT))
    # The kit's gable runs along x: turned, it runs back along y.
    b["pieces"].append(piece("roof-gable", x, -1, floors, LEFT, [1, 1, ROOF_RISE]))
    if reach_ridge:
        b["pieces"].append(piece("roof-gable", x, -0.55, floors, LEFT, [1, 1, ROOF_RISE]))
    if chimney:
        b["pieces"].append(piece("chimney", x, -1, floors - 0.3, BACK))
    return b


def l_house(material, palette, wing_at=2):
    """Two storeys three cells long, a one-storey back wing (kitchen and
    scullery) behind one end: an L from above."""
    return back_wing(building(3, 2, material, palette), wing_at, 1, material, chimney=True)


def cross_cottage(material, palette):
    """A one-storey cottage with a wing behind one end under a cross gable."""
    return back_wing(building(3, 1, material, palette, windows="small"), 0, 1, material, reach_ridge=True)


def hipped_house(material, palette, floors=2):
    """A house whose roof slopes down at its ends as well as its sides."""
    b = building(3, floors, material, palette, roof=None, chimney=False)
    # The kit's hip closes the cell's -x end; turned half round, the +x end.
    b["pieces"].append(piece("roof-gable-end", 0, 0, floors, 0, [1, 1, ROOF_RISE]))
    b["pieces"].append(piece("roof-gable", 1, 0, floors, 0, [1, 1, ROOF_RISE]))
    b["pieces"].append(piece("roof-gable-end", 2, 0, floors, 180, [1, 1, ROOF_RISE]))
    b["pieces"].append(piece("chimney", 1, 0, floors - 0.15, 0))
    return b


def foursquare(material, palette):
    """A square house two cells by two, two storeys under a pyramid roof,
    the door to one side (the American Foursquare, from the 1890s)."""
    wall = "wall-wood" if material == "wood" else "wall"
    window = f"{wall}-window-shutters"
    pieces = []
    for f in range(2):
        for i in range(2):
            pieces.append(piece(f"{wall}-door" if f == 0 and i == 0 else window, i, 0, f, FRONT))
            pieces.append(piece(window if i == f else wall, i, -1, f, BACK))
        for j in (0, -1):
            pieces.append(piece(window if j == 0 or f == 1 else wall, 0, j, f, LEFT))
            pieces.append(piece(window if j == -1 or f == 1 else wall, 1, j, f, RIGHT))
    pieces.append(piece("roof-point", 0.5, -0.5, 2, 0, [2, 2, 2 * 0.9]))
    pieces.append(piece("chimney", 0.3, -0.5, 2.05, 0))
    return {"texture": f"{PACK}/Textures/colormap-{palette}.png", "pieces": pieces}


def church():
    """A stone nave three cells long with a tower and spire at one end."""
    b = building(3, 2, "stone", "slate", roof="roof-high-gable", windows="shutters", chimney=False)
    for f in range(4):
        for face, turn in (("front", FRONT), ("back", BACK), ("right", RIGHT)):
            name = "wall-door" if f == 0 and face == "front" else ("wall-window-small" if f == 3 else "wall")
            b["pieces"].append(piece(name, 3, 0, f, turn))
    b["pieces"].append(piece("roof-point", 3, 0, 4, 0))
    return b


def tower(b, x, floors_from, floors_to, roof="roof-point"):
    """A one-cell stone tower at cell x, its walls from one floor to another."""
    for f in range(floors_from, floors_to):
        for turn in (FRONT, BACK, LEFT, RIGHT):
            b["pieces"].append(piece("wall-window-small" if f == floors_to - 1 else "wall", x, 0, f, turn))
    b["pieces"].append(piece(roof, x, 0, floors_to, 0))
    return b


def station(size):
    """Station buildings, their front (and door) to the platform."""
    if size == "small":
        return building(2, 1, "wood", "slate", windows="small")
    if size == "medium":
        return building(3, 2, "stone", "terracotta")
    # Large: a long hall with a clock tower over its middle.
    return tower(building(5, 2, "stone", "slate", chimney=False), 2, 2, 4, roof="roof-high-point")


def windmill():
    """A stone tower mill, its sails on the front."""
    b = {"texture": f"{PACK}/Textures/colormap-slate.png", "pieces": []}
    tower(b, 0, 0, 3, roof="roof-high-point")
    b["pieces"].append(piece("windmill", 0, 0.78, 2.4, FRONT, spin={"axis": "x", "rpm": 8}))
    return b


def watermill():
    """A timber mill with its wheel turning at one end."""
    b = building(3, 1, "wood", "terracotta")
    b["pieces"].append(piece("watermill", 2.95, 0, 0.75, RIGHT, spin={"axis": "x", "rpm": 5}))
    return b


INDUSTRIAL = "kenney-industrial"


def works(*names, turn=0):
    """A model from Kenney's City Kit (Industrial) on the brick palette;
    several names stand side by side along x, a unit apart."""
    pieces = [{"file": f"{INDUSTRIAL}/{n}.glb", "at": [i * 1.2, 0, 0], "turn": turn} for i, n in enumerate(names)]
    return {"texture": f"{INDUSTRIAL}/Textures/colormap-brick.png", "pieces": pieces}


SURVIVAL = "kenney-survival"


def props(*items):
    """A cluster of props from Kenney's Survival Kit (weathered palette):
    each item is (name, x, y, z, turn)."""
    pieces = [{"file": f"{SURVIVAL}/{n}.glb", "at": [x, y, z], "turn": t} for n, x, y, z, t in items]
    return {"texture": f"{SURVIVAL}/Textures/colormap-weathered.png", "pieces": pieces}


def log_pile():
    """Logs stacked three, two, one, with stumps, a tent and a sawbench."""
    logs = [("tree-log", x, 0, 0.11, 0) for x in (-0.24, 0, 0.24)]
    logs += [("tree-log", x, 0, 0.32, 0) for x in (-0.12, 0.12)]
    logs += [("tree-log", 0, 0, 0.53, 0)]
    return props(*logs,
                 ("tree-trunk", 0.75, 0.35, 0, 0), ("tree-trunk", 0.85, -0.45, 0, 40), ("tree-trunk", -0.7, 0.6, 0, 80),
                 ("tent-canvas", -0.8, -0.5, 0, 20), ("workbench", 0.5, 0.65, 0, 15), ("tree-log-small", 0.55, -0.1, 0, 90))


def ore_heaps():
    """Stone and ore heaped by the pithead, with crates."""
    return props(("resource-stone-large", 0, 0, 0, 0), ("resource-stone-large", 0.35, 0.1, 0, 60),
                 ("resource-stone-large", 0.15, -0.3, 0, 120), ("resource-stone", -0.3, 0.25, 0, 0),
                 ("rock-a", -0.6, -0.4, 0, 30), ("rock-b", 0.8, 0.5, 0, 200),
                 ("box-large", -0.55, 0.45, 0, 90), ("box-large", -0.3, 0.6, 0, 80))


def dock_goods():
    """Barrels and crates waiting on the quay."""
    return props(("barrel", 0, 0, 0, 0), ("barrel", 0.26, 0.04, 0, 0), ("barrel", 0.12, 0.25, 0, 0),
                 ("barrel", 0.13, 0.1, 0.34, 0), ("box-large", -0.5, 0, 0, 0), ("box-large", -0.5, 0.52, 0, 0),
                 ("box", -0.5, 0.26, 0.25, 15), ("box", 0.6, -0.3, 0, 30), ("box", 0.6, -0.3, 0.25, 0))


def main():
    prefabs = {
        "cottage-wood-terracotta": building(3, 1, "wood", "terracotta"),
        "cottage-wood-slate": building(3, 1, "wood", "slate", windows="small"),
        "house-wood": building(3, 2, "wood", "terracotta"),
        "house-stone": building(3, 2, "stone", "slate"),
        "cottage-cross-wood": cross_cottage("wood", "terracotta"),
        "cottage-hipped-stone": hipped_house("stone", "slate", floors=1),
        "house-l-stone": l_house("stone", "terracotta"),
        "house-l-wood": l_house("wood", "slate", wing_at=0),
        "house-hipped-wood": hipped_house("wood", "slate"),
        "foursquare-wood": foursquare("wood", "terracotta"),
        "foursquare-stone": foursquare("stone", "slate"),
        "terrace-stone": building(3, 2, "stone", "terracotta", chimney=False),
        "terrace-wood": building(3, 2, "wood", "slate", chimney=False),
        "shop-stone": building(3, 2, "stone", "terracotta"),
        "shop-wood": building(3, 2, "wood", "slate", windows="small"),
        "church": church(),
        "station-small": station("small"),
        "station-medium": station("medium"),
        "station-large": station("large"),
        "windmill": windmill(),
        "watermill": watermill(),
        # Works and mills from the industrial kit, in brick.
        "works-sawtooth": works("building-k"),
        "works-stacks": works("building-m"),
        "works-chimney": works("building-n"),
        "works-gabled": works("building-i"),
        "works-warehouse": works("building-h"),
        "works-long-shed": works("building-s"),
        "works-tanks": works("building-c"),
        "works-refinery": works("building-r", "detail-tank-large"),
        "works-power": works("building-e"),
        "works-cooling-tower": works("chimney-large", "building-g"),
        "works-workshop": works("building-g"),
        "water-tower": works("water-tower"),
        "engine-shed": works("building-h"),
        # Props beside industries, from the survival kit.
        "props-logs": log_pile(),
        "props-ore": ore_heaps(),
        "props-goods": dock_goods(),
    }
    out = Path("data/models/prefabs.json")
    out.write_text(json.dumps({
        "about": "Generated by tools/models/town_prefabs.py: town buildings, stations and mills assembled from Kenney's Fantasy Town Kit, works from the City Kit (Industrial) and props from the Survival Kit (all CC0). Edit the script, not this file.",
        "prefabs": prefabs,
    }, indent=1) + "\n")


if __name__ == "__main__":
    main()
