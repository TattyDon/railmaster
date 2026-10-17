#!/usr/bin/env python3
"""Draw the example heightmaps in data/maps/ (rerun to remake them).

A heightmap is any PNG: brightness is height (black low, white the
highest hills), and blue, or near-black, is water. These three are drawn
from fractal noise shaped by hand; any image editor can make more.
Needs numpy and ImageMagick's `convert`. Run from the repository root.
"""
import subprocess
from pathlib import Path

import numpy as np

N = 512


def noise(seed, octaves=6, base=4):
    """Fractal value noise in 0..1, N x N."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = base * 2 ** o
        grid = rng.random((cells + 1, cells + 1))
        xs = np.linspace(0, cells, N, endpoint=False)
        i = xs.astype(int)
        f = xs - i
        f = f * f * (3 - 2 * f)  # smoothstep
        a = grid[np.ix_(i, i)] * (1 - f)[None, :] + grid[np.ix_(i, i + 1)] * f[None, :]
        b = grid[np.ix_(i + 1, i)] * (1 - f)[None, :] + grid[np.ix_(i + 1, i + 1)] * f[None, :]
        layer = a * (1 - f)[:, None] + b * f[:, None]
        out += amp * layer
        total += amp
        amp *= 0.5
    return out / total


def river(h, water, points, width, seed):
    """Carve a meandering river through the given points (x, y in 0..1)."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:N, 0:N] / N
    pts = np.array(points)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        for t in np.linspace(0, 1, 120):
            wob = 0.012 * np.sin(t * 9 + rng.random())
            x = x0 + (x1 - x0) * t + wob
            y = y0 + (y1 - y0) * t - wob
            d = np.hypot(xx - x, yy - y)
            water |= d < width
            h[:] = np.minimum(h, 0.12 + d * 2.0)  # a valley down to the river


def save(name, h, water):
    h = np.clip(h, 0, 1)
    grey = (40 + h * 215).astype(np.uint8)  # land never as dark as the sea line
    rgb = np.stack([grey, grey, grey], axis=-1)
    rgb[water] = (40, 80, 190)
    raw = Path(f"/tmp/{name}.rgb")
    raw.write_bytes(rgb.tobytes())
    subprocess.run(["convert", "-size", f"{N}x{N}", "-depth", "8", f"rgb:{raw}", f"data/maps/{name}.png"], check=True)
    raw.unlink()


def two_rivers():
    """Hills north and south, two rivers through the plain to an eastern sea."""
    yy, xx = np.mgrid[0:N, 0:N] / N
    h = 0.15 + 0.35 * noise(11) + 0.45 * (np.abs(yy - 0.5) * 2) ** 2
    water = xx + 0.04 * np.sin(yy * 14) > 0.9
    river(h, water, [(0.0, 0.32), (0.3, 0.38), (0.6, 0.33), (0.92, 0.42)], 0.007, 1)
    river(h, water, [(0.05, 0.8), (0.35, 0.66), (0.65, 0.62), (0.92, 0.55)], 0.006, 2)
    save("two_rivers", h, water)


def archipelago():
    """Islands in a sea, hills on the largest."""
    yy, xx = np.mgrid[0:N, 0:N] / N
    n = noise(23, base=3)
    coast = noise(29, base=6)  # ragged shores: each island's reach varies with direction
    islands = [(0.35, 0.4, 0.28), (0.72, 0.3, 0.14), (0.7, 0.72, 0.18), (0.22, 0.8, 0.1)]
    land = np.zeros((N, N))
    for x, y, r in islands:
        land = np.maximum(land, 1 - np.hypot(xx - x, yy - y) / (r * (0.6 + 0.8 * coast)))
    h = land * (0.4 + 0.6 * n) + 0.25 * n
    water = h < 0.22
    save("archipelago", h * 1.2, water)


def mountain_pass():
    """A range across the middle with one low pass, and a lake to the south."""
    yy, xx = np.mgrid[0:N, 0:N] / N
    ridge = np.exp(-((yy - 0.45 - 0.05 * np.sin(xx * 7)) / 0.12) ** 2)
    gap = 1 - 0.8 * np.exp(-((xx - 0.55) / 0.07) ** 2)
    h = 0.12 + 0.3 * noise(37) + 0.75 * ridge * gap * (0.6 + 0.4 * noise(41, base=8))
    water = np.hypot((xx - 0.3) / 1.6, yy - 0.8) < 0.07
    river(h, water, [(0.55, 0.62), (0.75, 0.8), (1.0, 0.9)], 0.005, 3)
    save("mountain_pass", h, water)


if __name__ == "__main__":
    two_rivers()
    archipelago()
    mountain_pass()
