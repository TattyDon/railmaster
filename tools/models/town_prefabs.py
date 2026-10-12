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


def piece(name, x, y, z, turn=0, scale=None):
    p = {"file": f"{PACK}/{name}.glb", "at": [x, y, z], "turn": turn}
    if scale:
        p["scale"] = scale
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
    for i in range(length):
        pieces.append(piece(roof, i, 0, floors, 0, [1, 1, ROOF_RISE]))
    if chimney:
        pieces.append(piece("chimney", length - 1, 0, floors - 0.3, 0))
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
    b["pieces"].append(piece("windmill", 0, 0.78, 2.4, FRONT))
    return b


def watermill():
    """A timber mill with its wheel turning at one end."""
    b = building(3, 1, "wood", "terracotta")
    b["pieces"].append(piece("watermill", 2.95, 0, 0.75, RIGHT))
    return b


def main():
    prefabs = {
        "cottage-wood-terracotta": building(3, 1, "wood", "terracotta"),
        "cottage-wood-slate": building(3, 1, "wood", "slate", windows="small"),
        "house-wood": building(3, 2, "wood", "terracotta"),
        "house-stone": building(3, 2, "stone", "slate"),
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
    }
    out = Path("data/models/prefabs.json")
    out.write_text(json.dumps({
        "about": "Generated by tools/models/town_prefabs.py: town buildings, stations and mills assembled from Kenney's Fantasy Town Kit (CC0). Edit the script, not this file.",
        "prefabs": prefabs,
    }, indent=1) + "\n")


if __name__ == "__main__":
    main()
