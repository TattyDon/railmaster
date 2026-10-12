#!/usr/bin/env python3
"""Recolour the Kenney Fantasy Town Kit palette for an 1830s railroad town.

The kit colours every piece from one palette image (Textures/colormap.png):
its roofs are teal and its stone lavender. This writes period variants
beside it, keeping every other colour:

  colormap-slate.png       teal roofs -> blue-grey slate, lavender -> grey stone
  colormap-terracotta.png  teal roofs -> red-brown tile, lavender -> grey stone

Usage: period_palettes.py data/models/kenney-fantasy-town/Textures
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


def main() -> None:
    folder = Path(sys.argv[1])
    src = Image.open(folder / "colormap.png").convert("RGBA")
    recolour(src, (215, 0.3)).save(folder / "colormap-slate.png")
    recolour(src, (12, 0.85)).save(folder / "colormap-terracotta.png")


if __name__ == "__main__":
    main()
