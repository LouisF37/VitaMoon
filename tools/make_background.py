#!/usr/bin/env python3
"""Generates assets/bg_<theme>.png, the 960x544 background of every menu, one
per colour theme (src/gui/theme.c lists the themes and their colours).

The picture is drawn from scratch (no third-party artwork): a vertical
gradient and three soft wave crests in the lower part of the screen. The crests
are kept faint so that text drawn on top of them stays readable, and the
gradient is dithered so that it shows no banding on the Vita screen.

    python3 tools/make_background.py [output folder]

Needs numpy and Pillow.
"""
import sys

import numpy as np
from PIL import Image

W, H = 960, 544

# Per theme: vertical gradient as (row, colour) stops, rows in between being
# interpolated, and the tint of the light added by the wave crests.
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

# Wave crests: base row, (amplitude, wavelength, phase) terms, glow strength,
# glow depth in rows, crest line strength.
WAVES = [
    (452, [(26, 1250, 3.9), (9, 520, 1.2)], 9.0, 70.0, 20.0),
    (486, [(20, 1050, 0.6), (7, 430, 2.4)], 7.0, 60.0, 14.0),
    (508, [(14, 880, 2.2), (5, 360, 5.0)], 5.0, 44.0, 9.0),
]

# 8x8 ordered dither matrix, values in [0, 1).
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


def draw(stops, tint, out):
    rows = np.arange(H, dtype=np.float64)
    cols = np.arange(W, dtype=np.float64)

    img = np.zeros((H, W, 3))
    stop_rows = [s[0] for s in stops]
    for c in range(3):
        img[:, :, c] = np.interp(rows, stop_rows, [s[1][c] for s in stops])[:, None]

    # Light added by the waves, same amount on every channel then tinted.
    light = np.zeros((H, W))
    for base, terms, glow, depth, line in WAVES:
        crest = np.full(W, float(base))
        for amp, wavelength, phase in terms:
            crest += amp * np.sin(2 * np.pi * cols / wavelength + phase)
        below = rows[:, None] - crest[None, :]
        # Soft glow under the crest, fading with depth.
        light += np.where(below >= 0, glow * np.exp(-np.maximum(below, 0) / depth), 0.0)
        # Thin anti-aliased crest line, brighter towards the right of the screen.
        fade = 0.45 + 0.55 * (cols / (W - 1))
        light += line * fade[None, :] * np.exp(-(below ** 2) / (2 * 0.9 ** 2))

    img += light[:, :, None] * np.array(tint)[None, None, :]

    # Ordered dither before rounding down to 8 bits.
    tile = np.tile(BAYER, (H // 8 + 1, W // 8 + 1))[:H, :W]
    img = np.floor(img + tile[:, :, None])
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").save(out, optimize=True)
    print("written:", out)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "assets"
    for name, (stops, tint) in THEMES.items():
        draw(stops, tint, "%s/bg_%s.png" % (folder, name))


if __name__ == "__main__":
    main()
