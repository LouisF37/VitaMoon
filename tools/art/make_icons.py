"""Draws the LiveArea bubble icon and gate image of Lune (an original full
moon on the night-blue of the interface), into sce_sys/.

    python3 tools/art/make_icons.py

Needs Pillow. Drawn at 8x and scaled down for clean edges. The Vita only reads
8-bit indexed PNGs here, and icon0.png must have no alpha channel: both are
opaque and saved with a 256-colour palette."""
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
SS = 8  # supersampling factor

SKY_TOP = (14, 52, 102)     # matches the interface background
SKY_BOTTOM = (3, 12, 30)
MOON = (246, 248, 252)
MOON_SHADE = (196, 207, 226)
MARIA = (150, 166, 196)
GLOW = (109, 180, 255)      # TH_ACCENT


def radial(size, center, radius, inner, outer):
    """Radial gradient from inner (at center) to outer (at radius and beyond)."""
    w, h = size
    small = (max(1, w // 8), max(1, h // 8))  # drawn small and scaled up: smooth and fast
    img = Image.new('RGB', small)
    px = img.load()
    cx, cy, r = center[0] / 8, center[1] / 8, radius / 8
    for y in range(small[1]):
        for x in range(small[0]):
            t = min(1.0, ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / r)
            px[x, y] = tuple(round(inner[i] + (outer[i] - inner[i]) * t) for i in range(3))
    return img.resize(size, Image.BICUBIC)


def disc(size, cx, cy, r):
    mask = Image.new('L', size, 0)
    ImageDraw.Draw(mask).ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    return mask


def moon_art(size):
    """Night sky with a full moon, as RGBA, at size (already supersampled)."""
    w, h = size
    s = min(w, h)
    cx, cy, r = w * 0.5, h * 0.5, s * 0.30
    img = radial(size, (cx, cy * 0.85), s * 0.75, SKY_TOP, SKY_BOTTOM).convert('RGBA')
    moon_mask = disc(size, cx, cy, r)

    # soft halo around the moon
    halo = Image.new('RGBA', size, GLOW + (0,))
    halo.putalpha(disc(size, cx, cy, r * 1.08).filter(ImageFilter.GaussianBlur(s * 0.07))
                  .point(lambda v: v * 0.45))
    img = Image.alpha_composite(img, halo)

    # the disc, lit from the top left
    face = radial(size, (cx - r * 0.35, cy - r * 0.40), r * 1.6, MOON, MOON_SHADE).convert('RGBA')
    face.putalpha(moon_mask)
    img = Image.alpha_composite(img, face)

    # a few flat, low-contrast maria, kept inside the disc
    maria = Image.new('L', size, 0)
    d = ImageDraw.Draw(maria)
    for dx, dy, rx, ry in ((-0.32, -0.20, 0.26, 0.20), (-0.08, -0.36, 0.16, 0.12),
                           (0.24, 0.02, 0.20, 0.24), (-0.18, 0.34, 0.12, 0.09)):
        x, y = cx + dx * r, cy + dy * r
        d.ellipse((x - rx * r, y - ry * r, x + rx * r, y + ry * r), fill=255)
    maria = maria.filter(ImageFilter.GaussianBlur(s * 0.014))
    maria = Image.composite(maria, Image.new('L', size, 0), moon_mask)
    tint = Image.new('RGBA', size, MARIA + (0,))
    tint.putalpha(maria.point(lambda v: v * 0.42))
    return Image.alpha_composite(img, tint)


def save(img, size, path):
    """Scaled down, then reduced to a 256-colour palette with dithering."""
    img = img.resize(size, Image.LANCZOS).convert('RGB')
    img = img.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    img.save(path, optimize=True)
    print(path, img.size, img.mode)


def main():
    # Bubble icon: 128x128, square (the shell draws it in a circle).
    save(moon_art((128 * SS, 128 * SS)), (128, 128), os.path.join(ROOT, 'sce_sys', 'icon0.png'))
    # Gate (start button) image: 280x158, the same sky with the moon in the middle.
    save(moon_art((280 * SS, 158 * SS)), (280, 158),
         os.path.join(ROOT, 'sce_sys', 'livearea', 'contents', 'startup.png'))


if __name__ == '__main__':
    main()
