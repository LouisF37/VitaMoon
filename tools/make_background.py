#!/usr/bin/env python3
"""Generates assets/bg_<theme>.png, the 960x544 background of every menu, one
per colour theme (src/gui/theme.c lists the themes and their colours).

The picture is drawn from scratch (no third-party artwork): a lunar sky. A
vertical gradient, a few faint stars (none behind the top bar's text, fewer
towards the horizon), a large faint crescent low on the right whose disc is the
one of the logo seen from closer, and a soft glow on the horizon. Each theme has
one touch of its own: aurora curtains, a warm dawn horizon, and for Eclipse a
dark disc with a thin corona. Everything is kept faint so that text drawn on top
stays readable, and the picture is dithered so that it shows no banding on the
Vita screen. The app draws it as one texture per frame, like any picture.

    python3 tools/make_background.py [output folder]

Needs numpy and Pillow.
"""
import sys

import numpy as np
from PIL import Image

W, H = 960, 544

THEMES = {
    "night": ([(0, (4, 20, 48)), (120, (8, 45, 94)), (330, (14, 66, 128)), (544, (16, 56, 102))],
              (0.55, 0.85, 1.0)),
    "dawn": ([(0, (30, 9, 6)), (120, (70, 26, 15)), (330, (118, 50, 30)), (544, (96, 40, 25))],
             (1.0, 0.75, 0.55)),
    "aurora": ([(0, (2, 18, 20)), (120, (4, 42, 44)), (330, (8, 70, 66)), (544, (7, 56, 54))],
               (0.55, 1.0, 0.85)),
    "twilight": ([(0, (16, 6, 32)), (120, (38, 15, 72)), (330, (66, 30, 114)), (544, (52, 24, 90))],
                 (0.82, 0.62, 1.0)),
    "eclipse": ([(0, (7, 7, 9)), (120, (16, 16, 19)), (330, (30, 30, 35)), (544, (24, 24, 28))],
                (1.0, 0.82, 0.5)),
}

# Per theme: how many stars, how bright the moon disc is, horizon glow strength.
STYLE = {
    "night": dict(stars=60, moon=11.0, horizon=10.0),
    "dawn": dict(stars=25, moon=9.0, horizon=26.0),
    "aurora": dict(stars=50, moon=8.0, horizon=8.0),
    "twilight": dict(stars=80, moon=10.0, horizon=14.0),
    "eclipse": dict(stars=70, moon=0.0, horizon=4.0),
}

BAYER = np.array([
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
]) / 64.0

MOON_C = (858.0, 610.0)   # centre of the big moon, below the bottom-right corner
MOON_R = 300.0


def draw(name, stops, tint, out):
    st = STYLE[name]
    rows = np.arange(H, dtype=np.float64)
    cols = np.arange(W, dtype=np.float64)
    yy, xx = np.meshgrid(rows, cols, indexing="ij")

    img = np.zeros((H, W, 3))
    stop_rows = [s[0] for s in stops]
    for c in range(3):
        img[:, :, c] = np.interp(rows, stop_rows, [s[1][c] for s in stops])[:, None]

    light = np.zeros((H, W))

    # Horizon: a soft glow rising from the bottom edge.
    light += st["horizon"] * np.exp(-(H - 1 - yy) / 70.0)

    # The big moon: a faint disc with a brighter limb and a soft halo.
    d = np.hypot(xx - MOON_C[0], yy - MOON_C[1])
    if name == "eclipse":
        # Dark disc, thin bright corona around it.
        light += 34.0 * np.exp(-((d - MOON_R) ** 2) / (2 * 2.2 ** 2))
        light += 16.0 * np.exp(-np.maximum(d - MOON_R, 0) / 26.0) * (d >= MOON_R)
        img *= np.where(d < MOON_R, 0.55, 1.0)[:, :, None]
    else:
        inside = d < MOON_R
        # Lit crescent: the part of the disc not covered by a second disc shifted down-right.
        d2 = np.hypot(xx - (MOON_C[0] + 70.0), yy - (MOON_C[1] + 40.0))
        lit = inside & (d2 > MOON_R * 0.98)
        light += st["moon"] * inside
        light += st["moon"] * 1.6 * lit
        light += 7.0 * np.exp(-((d - MOON_R) ** 2) / (2 * 1.2 ** 2)) * (d2 > MOON_R)
        light += 9.0 * np.exp(-np.maximum(d - MOON_R, 0) / 40.0) * (d >= MOON_R)

    # Aurora curtains: soft vertical bands in the upper half.
    if name == "aurora":
        band = (np.sin(xx / 61.0 + 0.8) * 0.5 + 0.5) * (np.sin(xx / 23.0 + 2.1) * 0.3 + 0.7)
        curtain = np.exp(-((yy - (120 + 40 * np.sin(xx / 140.0))) ** 2) / (2 * 55.0 ** 2))
        light += 26.0 * band * curtain

    img += light[:, :, None] * np.array(tint)[None, None, :]

    # Stars: few, small, faint; fewer towards the horizon.
    rng = np.random.default_rng({"night": 7, "dawn": 11, "aurora": 3, "twilight": 5, "eclipse": 9}[name])
    star = np.zeros((H, W))
    n = 0
    while n < st["stars"]:
        x, y = rng.uniform(0, W), rng.uniform(0, H)
        if rng.uniform() > (1.0 - y / H) ** 1.4:
            continue
        if np.hypot(x - MOON_C[0], y - MOON_C[1]) < MOON_R + 20:
            continue
        # Keep the top bar's text clear (name on the left, status on the right).
        if y < 58 and (x < 280 or x > 690):
            continue
        size = rng.choice([0.55, 0.7, 0.9, 1.2], p=[0.45, 0.3, 0.17, 0.08])
        bright = rng.uniform(45, 115) * (1.0 - 0.6 * y / H)
        x0, x1 = int(max(x - 4, 0)), int(min(x + 5, W))
        y0, y1 = int(max(y - 4, 0)), int(min(y + 5, H))
        gy, gx = np.meshgrid(np.arange(y0, y1), np.arange(x0, x1), indexing="ij")
        star[y0:y1, x0:x1] += bright * np.exp(-((gx + 0.5 - x) ** 2 + (gy + 0.5 - y) ** 2) / (2 * size ** 2))
        n += 1
    white = np.array([0.92, 0.95, 1.0])
    img += np.minimum(star, 200)[:, :, None] * white[None, None, :]

    tile = np.tile(BAYER, (H // 8 + 1, W // 8 + 1))[:H, :W]
    img = np.floor(img + tile[:, :, None])
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").save(out, optimize=True)
    print("written:", out)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "assets"
    for name, (stops, tint) in THEMES.items():
        draw(name, stops, tint, "%s/bg_%s.png" % (folder, name))


if __name__ == "__main__":
    main()
