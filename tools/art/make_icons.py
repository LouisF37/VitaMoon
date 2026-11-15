"""Draws the LiveArea bubble icon and gate image of Lune (an original crescent
moon on the night-blue of the interface), into sce_sys/.

    python3 tools/art/make_icons.py

Needs Pillow. Drawn at 8x and scaled down for clean edges. The Vita only reads
8-bit indexed PNGs here, and icon0.png must have no alpha channel: both are
saved with a 256-colour palette (the gate keeps its transparency in it)."""
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
SS = 8  # supersampling factor

SKY_TOP = (14, 52, 102)     # matches the interface background
SKY_BOTTOM = (3, 12, 30)
MOON = (236, 241, 250)
GLOW = (109, 180, 255)      # TH_ACCENT


def gradient(size, top, bottom):
    w, h = size
    img = Image.new('RGB', size)
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def crescent_mask(size, cx, cy, r, cut_dx, cut_dy, cut_r):
    """Disc of radius r minus a disc shifted by (cut_dx, cut_dy)."""
    mask = Image.new('L', size, 0)
    d = ImageDraw.Draw(mask)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    d.ellipse((cx + cut_dx - cut_r, cy + cut_dy - cut_r,
               cx + cut_dx + cut_r, cy + cut_dy + cut_r), fill=0)
    return mask


def star(draw, x, y, r, alpha):
    """Small four-pointed star."""
    w = r * 0.28
    fill = (255, 255, 255, alpha)
    draw.polygon([(x, y - r), (x + w, y - w), (x + r, y), (x + w, y + w),
                  (x, y + r), (x - w, y + w), (x - r, y), (x - w, y - w)], fill=fill)


def moon_art(size):
    """Night sky with the crescent, as RGBA, at size (already supersampled)."""
    w, h = size
    s = min(w, h)
    img = gradient(size, SKY_TOP, SKY_BOTTOM).convert('RGBA')
    cx, cy, r = w * 0.5, h * 0.5, s * 0.30
    mask = crescent_mask(size, cx, cy, r, r * 0.42, -r * 0.30, r * 0.86)
    # soft glow around the crescent
    glow = Image.new('RGBA', size, GLOW + (0,))
    glow.putalpha(mask.filter(ImageFilter.GaussianBlur(s * 0.06)).point(lambda v: v * 0.55))
    img = Image.alpha_composite(img, glow)
    moon = Image.new('RGBA', size, MOON + (255,))
    moon.putalpha(mask)
    img = Image.alpha_composite(img, moon)
    d = ImageDraw.Draw(img)
    star(d, cx + r * 0.95, cy - r * 0.62, s * 0.040, 235)
    star(d, cx + r * 0.62, cy + r * 0.30, s * 0.022, 170)
    return img


def save(img, size, path, mode):
    img = img.resize(size, Image.LANCZOS)
    if mode == 'RGB':
        img = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT,
                                          dither=Image.Dither.FLOYDSTEINBERG)
    else:
        img = img.quantize(256, method=Image.Quantize.FASTOCTREE,
                           dither=Image.Dither.FLOYDSTEINBERG)
    img.save(path, optimize=True)
    print(path, img.size, img.mode)


def main():
    # Bubble icon: 128x128, square (the shell draws it in a circle).
    icon = moon_art((128 * SS, 128 * SS))
    save(icon, (128, 128), os.path.join(ROOT, 'sce_sys', 'icon0.png'), 'RGB')

    # Gate (start button) image: 280x158, the round badge on a transparent background.
    gw, gh = 280 * SS, 158 * SS
    badge_d = 140 * SS
    badge = moon_art((badge_d, badge_d))
    circle = Image.new('L', (badge_d, badge_d), 0)
    ImageDraw.Draw(circle).ellipse((0, 0, badge_d - 1, badge_d - 1), fill=255)
    badge.putalpha(circle)
    gate = Image.new('RGBA', (gw, gh), (0, 0, 0, 0))
    gate.alpha_composite(badge, ((gw - badge_d) // 2, (gh - badge_d) // 2))
    save(gate, (280, 158), os.path.join(ROOT, 'sce_sys', 'livearea', 'contents', 'startup.png'), 'RGBA')


if __name__ == '__main__':
    main()
