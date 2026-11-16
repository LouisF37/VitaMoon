"""Draws VitaMoon's logo and every image made from it.

    python3 tools/art/make_brand.py

The mark is a disc with a V cut out of its top (V for Vita, the disc for the
moon), its left edge in shade like a moon in its phases. The wordmark is
Poppins Bold (SIL Open Font License), turned into outlines so that no font is
needed to show the SVGs. Needs Pillow, fontTools, rsvg-convert and the Poppins
and Inter Display fonts (fonts-poppins / fonts-inter, or Google Fonts).

Writes:
  docs/assets/vitamoon-icon.svg, -icon.png       rounded icon (README, 512 px)
  docs/assets/vitamoon-logo-dark.svg, -light.svg mark and stacked wordmark, for dark / light pages
  docs/assets/vitamoon-social.png               1280x640 preview for the GitHub repository
  sce_sys/icon0.png                             LiveArea bubble, 128x128 (the shell crops a circle)
  sce_sys/livearea/contents/startup.png         LiveArea gate, 280x158
  sce_sys/livearea/contents/bg.png              LiveArea background, 840x500
  assets/logo_mark.png                          the mark alone, for the menus' top bar

The Vita reads 8-bit indexed PNGs in sce_sys/, and icon0.png must have no
alpha channel: those are opaque and saved with a 256-colour palette.
"""
import math
import os
import subprocess
import tempfile

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

FONT_WORD = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'
FONT_TAG = '/usr/share/fonts/opentype/inter/InterDisplay-Medium.otf'

NAVY_TOP = '#1B4A8F'     # icon gradient, top left
NAVY_BOTTOM = '#0A1C3D'  # bottom right
NAVY_PAGE = '#0B1E40'    # dark backgrounds
MOON = '#F4F7FC'         # the lit disc
SHADE = '#B4CBEA'        # its shaded edge
ACCENT = '#8CC4FF'       # "MOON" on dark (the interface's accent, lighter)
INK = '#0B1E40'          # "VITA" on light
ACCENT_ON_LIGHT = '#2A6FD6'


# ---------------------------------------------------------------- geometry

def mark_outline(cx=50.0, cy=52.0, r=34.0, ax=50.0, ay=70.0, alpha=30.0):
    """The disc of radius r at (cx, cy) minus a V whose point is at (ax, ay)
    and whose sides lean alpha degrees from the vertical. 100-unit box."""
    a = math.radians(alpha)
    dy = ay - cy
    t = dy * math.cos(a) + math.sqrt((dy * math.cos(a)) ** 2 - dy * dy + r * r)
    lx, ly = ax - t * math.sin(a), ay - t * math.cos(a)
    rx = ax + t * math.sin(a)
    return f'M{lx:.3f},{ly:.3f} A{r},{r} 0 1 0 {rx:.3f},{ly:.3f} L{ax},{ay} Z'


MARK = mark_outline()
# The shaded edge: between the disc's left side and an ellipse through its top
# and bottom (a waxing gibbous moon).
SHADE_PATH = 'M50,18 A12,34 0 0 0 50,86 A34,34 0 0 1 50,18 Z'
_ids = [0]


def mark_svg(x, y, size, moon=MOON, shade=SHADE):
    """The mark in a size x size box at (x, y)."""
    _ids[0] += 1
    cid = f'vm{_ids[0]}'
    s = size / 100.0
    return (f'<g transform="translate({x:.2f},{y:.2f}) scale({s:.4f})">'
            f'<clipPath id="{cid}"><path d="{MARK}"/></clipPath>'
            f'<g clip-path="url(#{cid})"><rect width="100" height="100" fill="{moon}"/>'
            f'<path d="{SHADE_PATH}" fill="{shade}"/></g></g>')


def text_outline(font_file, text, x, baseline, size, tracking=0.0):
    """Outlines of text set at (x, baseline), and its advance width."""
    font = TTFont(font_file)
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    scale = size / font['head'].unitsPerEm
    pen = SVGPathPen(glyphs)
    cur = x
    for ch in text:
        name = cmap[ord(ch)]
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, cur, baseline)))
        cur += glyphs[name].width * scale + tracking
    return pen.getCommands(), cur - tracking - x


def cap_height(font_file, size):
    font = TTFont(font_file)
    os2 = font['OS/2']
    return getattr(os2, 'sCapHeight', 0.7 * font['head'].unitsPerEm) * size / font['head'].unitsPerEm


# ---------------------------------------------------------------- pieces

def gradient(gid, top=NAVY_TOP, bottom=NAVY_BOTTOM):
    return (f'<linearGradient id="{gid}" x1="0" y1="0" x2="1" y2="1">'
            f'<stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient>')


def stacked_wordmark(x, top, height, first, second):
    """VITA over MOON, cap tops at `top`, block `height` tall. Returns svg, width."""
    gap = height * 0.22
    size = (height - gap) / 2 / (cap_height(FONT_WORD, 1.0))
    cap = cap_height(FONT_WORD, size)
    track = size * 0.05
    d1, w1 = text_outline(FONT_WORD, 'VITA', x, top + cap, size, track)
    d2, w2 = text_outline(FONT_WORD, 'MOON', x, top + 2 * cap + gap, size, track)
    return f'<path d="{d1}" fill="{first}"/><path d="{d2}" fill="{second}"/>', max(w1, w2)


def line_wordmark(x, baseline, size, first, second):
    d1, w1 = text_outline(FONT_WORD, 'Vita', x, baseline, size)
    d2, w2 = text_outline(FONT_WORD, 'Moon', x + w1, baseline, size)
    return f'<path d="{d1}" fill="{first}"/><path d="{d2}" fill="{second}"/>', w1 + w2


def svg_doc(w, h, body, defs=''):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">'
            f'<defs>{defs}</defs>{body}</svg>\n')


# ---------------------------------------------------------------- outputs

def icon(rounded=True):
    bg = (f'<rect width="100" height="100" rx="{22 if rounded else 0}" fill="url(#bgi)"/>')
    return svg_doc(100, 100, bg + mark_svg(12, 10, 76), gradient('bgi'))


def bubble():
    """LiveArea bubble: the shell shows it in a circle, so the mark stays well inside."""
    return svg_doc(100, 100, '<rect width="100" height="100" fill="url(#bgb)"/>' + mark_svg(17, 15, 66),
                   gradient('bgb'))


def logo(dark):
    first, second = (MOON, ACCENT) if dark else (INK, ACCENT_ON_LIGHT)
    shade = SHADE if dark else '#9DB6D9'
    moon = MOON if dark else INK
    # The disc spans y 18..86 of the mark box: the words take that height.
    mark_size = 120.0
    disc_top, disc_h = 18 * mark_size / 100, 68 * mark_size / 100
    words, w = stacked_wordmark(mark_size + 18, disc_top, disc_h, first, second)
    if not dark:
        # On a light page the disc is ink with a lighter edge, the V shows the page.
        body = mark_svg(0, 0, mark_size, moon=moon, shade='#3B5C8F') + words
    else:
        body = mark_svg(0, 0, mark_size, moon=moon, shade=shade) + words
    width = math.ceil(mark_size + 18 + w + 4)
    return svg_doc(width, int(mark_size), body)


def social():
    w, h = 1280, 640
    mark_size = 270
    words_size = 150
    words, ww = line_wordmark(0, 0, words_size, MOON, ACCENT)
    total = mark_size * 0.86 + 36 + ww
    x0 = (w - total) / 2 - mark_size * 0.07
    cap = cap_height(FONT_WORD, words_size)
    base = 300 + cap / 2
    words, _ = line_wordmark(x0 + mark_size * 0.93 + 36, base, words_size, MOON, ACCENT)
    tag, tw = text_outline(FONT_TAG, 'Moonlight game streaming for the PS Vita', 0, 0, 40)
    tag, _ = text_outline(FONT_TAG, 'Moonlight game streaming for the PS Vita', (w - tw) / 2, 470, 40)
    body = (f'<rect width="{w}" height="{h}" fill="url(#bgs)"/>'
            + mark_svg(x0, 300 - mark_size * 0.52, mark_size) + words
            + f'<path d="{tag}" fill="#A9BCD8"/>')
    return svg_doc(w, h, body, gradient('bgs', '#163E78', '#08162F'))


def startup():
    w, h = 280, 158
    size = 40
    words, ww = line_wordmark(0, 0, size, MOON, ACCENT)
    mark_size = 62
    total = mark_size * 0.86 + 10 + ww
    x0 = (w - total) / 2 - mark_size * 0.07
    cap = cap_height(FONT_WORD, size)
    words, _ = line_wordmark(x0 + mark_size * 0.93 + 10, h / 2 + cap / 2, size, MOON, ACCENT)
    body = (f'<rect width="{w}" height="{h}" fill="url(#bgg)"/>'
            + mark_svg(x0, h / 2 - mark_size * 0.52, mark_size) + words)
    return svg_doc(w, h, body, gradient('bgg'))


def livearea_bg():
    """Calm background: the LiveArea writes the title over it. A large, faint
    mark on the right."""
    w, h = 840, 500
    body = (f'<rect width="{w}" height="{h}" fill="url(#bgl)"/>'
            + f'<g opacity="0.10">{mark_svg(470, 40, 420, moon="#FFFFFF", shade="#9DB6D9")}</g>')
    return svg_doc(w, h, body, gradient('bgl', '#143769', '#050E20'))


def app_mark():
    return svg_doc(100, 100, mark_svg(0, 0, 100))


MOON_STEPS = 11   # src/gui/moon.h: MOON_PHASES - 1
MOON_CELL = 112   # pixels per picture in assets/moon.png


def moon_lit(f, cx=50.0, cy=52.0, r=34.0):
    """Waxing moon lit on the right, f = lit fraction of the disc (0..1): the
    right half of the disc and a half ellipse through its top and bottom."""
    k = abs(1.0 - 2.0 * f) * r
    sweep = 0 if f < 0.5 else 1
    return (f'M{cx},{cy - r} A{r},{r} 0 0 1 {cx},{cy + r} '
            f'A{k:.3f},{r} 0 0 {sweep} {cx},{cy - r} Z')


def moon_phase(step):
    """The waiting moon at step 0 (new: only a faint disc) to MOON_STEPS (full:
    the mark itself). Same disc as the mark."""
    dim = ('<circle cx="50" cy="52" r="34" fill="#FFFFFF" fill-opacity="0.10"/>'
           '<circle cx="50" cy="52" r="33.5" fill="none" stroke="#FFFFFF" stroke-opacity="0.28" stroke-width="1"/>')
    if step == 0:
        return svg_doc(100, 100, dim)
    if step == MOON_STEPS:
        return svg_doc(100, 100, mark_svg(0, 0, 100))
    return svg_doc(100, 100, dim + f'<path d="{moon_lit(step / MOON_STEPS)}" fill="{MOON}"/>')


def moon_sheet():
    """assets/moon.png: the MOON_STEPS + 1 pictures side by side."""
    sheet = Image.new('RGBA', ((MOON_STEPS + 1) * MOON_CELL, MOON_CELL), (0, 0, 0, 0))
    for step in range(MOON_STEPS + 1):
        sheet.alpha_composite(render(moon_phase(step), MOON_CELL, MOON_CELL).convert('RGBA'), (step * MOON_CELL, 0))
    return sheet


# ---------------------------------------------------------------- files

def render(svg, w, h):
    with tempfile.NamedTemporaryFile('w', suffix='.svg', delete=False) as f:
        f.write(svg)
        src = f.name
    out = src[:-4] + '.png'
    subprocess.run(['rsvg-convert', '-w', str(w), '-h', str(h), src, '-o', out], check=True)
    img = Image.open(out)
    img.load()
    os.unlink(src)
    os.unlink(out)
    return img


def write_svg(rel, svg):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(svg)
    print(rel)


def write_png(rel, img, vita=False):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if vita:
        img = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT,
                                          dither=Image.Dither.FLOYDSTEINBERG)
    img.save(path, optimize=True)
    print(rel, img.size, img.mode)


def main():
    write_svg('docs/assets/vitamoon-icon.svg', icon())
    write_png('docs/assets/vitamoon-icon.png', render(icon(), 512, 512))
    write_svg('docs/assets/vitamoon-logo-dark.svg', logo(dark=True))
    write_svg('docs/assets/vitamoon-logo-light.svg', logo(dark=False))
    write_png('docs/assets/vitamoon-social.png', render(social(), 1280, 640).convert('RGB'))
    write_png('sce_sys/icon0.png', render(bubble(), 128, 128), vita=True)
    write_png('sce_sys/livearea/contents/startup.png', render(startup(), 280, 158), vita=True)
    write_png('sce_sys/livearea/contents/bg.png', render(livearea_bg(), 840, 500), vita=True)
    write_png('assets/logo_mark.png', render(app_mark(), 64, 64))
    write_png('assets/moon.png', moon_sheet())


if __name__ == '__main__':
    main()
