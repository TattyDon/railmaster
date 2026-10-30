#!/usr/bin/env python3
"""Draw the game's icon: data/icon.png, 256 x 256.

A brass-rimmed tile: a dusk sky with a low sun, and a line of track
running away to the horizon over green land. Drawn shape by shape with
4 x 4 samples a pixel; only the standard library (zlib for the PNG).
Run from the repository root: tools/icon/make_icon.py
"""
import math
import struct
import zlib
from pathlib import Path

SIZE = 256
SAMPLES = 4
HORIZON = 0.47  # of the height, from the top


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def rounded(x, y, inset, radius):
    """Inside a rounded square inset from the edges (all in 0..1 units)."""
    lo, hi = inset + radius, 1 - inset - radius
    dx = max(lo - x, 0, x - hi)
    dy = max(lo - y, 0, y - hi)
    return dx * dx + dy * dy <= radius * radius


def track(x, y):
    """The colour of the track at (x, y) below the horizon, or None."""
    depth = (y - HORIZON) / (1 - HORIZON)  # 0 at the horizon, 1 at the bottom
    if depth <= 0:
        return None
    half = 0.03 + 0.3 * depth  # the track narrows away into the distance
    off = x - 0.5
    rail = 0.012 + 0.05 * depth
    for side in (-1, 1):
        if abs(off - side * half * 0.62) < rail * 0.5:
            return (0.93, 0.78, 0.45) if depth > 0.02 else None
    # Sleepers: evenly spaced in distance, so closer together far off.
    if abs(off) < half * 0.85:
        z = 1 / max(depth, 1e-3)
        if (z * 1.6) % 1 < 0.45:
            return (0.42, 0.28, 0.17)
        return (0.5, 0.45, 0.38)  # ballast
    return None


def colour(x, y):
    if not rounded(x, y, 0.0, 0.16):
        return None
    if not rounded(x, y, 0.035, 0.13):
        return (0.85, 0.66, 0.32)  # the brass rim
    if y < HORIZON:
        sky = mix((0.18, 0.24, 0.45), (0.98, 0.7, 0.42), (y / HORIZON) ** 1.6)
        d = math.hypot(x - 0.5, y - HORIZON + 0.02)
        if d < 0.13:
            return (1.0, 0.9, 0.62)
        glow = max(0, 1 - d / 0.35) ** 2 * 0.35
        return mix(sky, (1.0, 0.85, 0.55), glow)
    t = track(x, y)
    if t:
        return t
    depth = (y - HORIZON) / (1 - HORIZON)
    return mix((0.36, 0.5, 0.28), (0.2, 0.36, 0.16), depth)


def main():
    rows = []
    for py in range(SIZE):
        row = bytearray([0])
        for px in range(SIZE):
            r = g = b = a = 0.0
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    c = colour((px + (sx + 0.5) / SAMPLES) / SIZE, (py + (sy + 0.5) / SAMPLES) / SIZE)
                    if c:
                        r, g, b, a = r + c[0], g + c[1], b + c[2], a + 1
            n = SAMPLES * SAMPLES
            if a:
                row += bytes([round(255 * r / a), round(255 * g / a), round(255 * b / a), round(255 * a / n)])
            else:
                row += bytes(4)
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    Path("data/icon.png").write_bytes(png)


if __name__ == "__main__":
    main()
