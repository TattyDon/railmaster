#!/usr/bin/env python3
"""Recolour Kenney kit palettes for an 1830s railroad town.

The kit colours every piece from one palette image (Textures/colormap.png):
its roofs are teal and its stone lavender. This writes period variants
beside it, keeping every other colour:

  colormap-slate.png       teal roofs -> blue-grey slate, lavender -> grey stone
  colormap-terracotta.png  teal roofs -> red-brown tile, lavender -> grey stone

With --industrial it recolours the City Kit (Industrial) palette instead,
writing colormap-brick.png: pale lavender walls -> red brick, blue-grey
roofs and trim -> dark slate and iron, orange bands -> rust.

Usage: period_palettes.py data/models/kenney-fantasy-town/Textures
       period_palettes.py --industrial data/models/kenney-industrial/Textures
"""
import colorsys
import sys
from pathlib import Path

from PIL import Image


def recolour(src: Image.Image, roof: tuple[float, float]) -> Image.Image:
    """roof: (hue, saturation scale) for the teal swatches."""
    out = src.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            deg = h * 360
            if s > 0.2 and 150 <= deg <= 195:      # teal: roofs
                h, s, v = roof[0] / 360, s * roof[1], v * 0.78
            elif s > 0.08 and 225 <= deg <= 300:   # lavender: stone
                s, v = s * 0.12, v * 0.92
            else:
                continue
            nr, ng, nb = colorsys.hsv_to_rgb(h, s, v)
            px[x, y] = (round(nr * 255), round(ng * 255), round(nb * 255), a)
    return out


def industrial(src: Image.Image) -> Image.Image:
    out = src.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            deg = h * 360
            if v > 0.72 and s < 0.3 and (s < 0.06 or 200 <= deg <= 300):  # pale walls
                h, s, v = 10 / 360, 0.55, v * 0.62
            elif 200 <= deg <= 300 and s < 0.45:                            # blue-grey roofs, trim
                s, v = s * 0.25, v * 0.8
            elif 20 <= deg <= 45 and s > 0.4:                               # orange bands
                h, s, v = 18 / 360, s * 0.7, v * 0.55
            else:
                continue
            nr, ng, nb = colorsys.hsv_to_rgb(h, s, v)
            px[x, y] = (round(nr * 255), round(ng * 255), round(nb * 255), a)
    return out


def main() -> None:
    if sys.argv[1] == "--industrial":
        folder = Path(sys.argv[2])
        industrial(Image.open(folder / "colormap.png").convert("RGBA")).save(folder / "colormap-brick.png")
        return
    folder = Path(sys.argv[1])
    src = Image.open(folder / "colormap.png").convert("RGBA")
    recolour(src, (215, 0.3)).save(folder / "colormap-slate.png")
    recolour(src, (12, 0.85)).save(folder / "colormap-terracotta.png")


if __name__ == "__main__":
    main()
