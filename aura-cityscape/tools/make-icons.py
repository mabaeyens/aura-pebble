#!/usr/bin/env python3
"""Draw the Aura Cityscape Weather store art with Pillow: the dashboard app icons
(80x80, 144x144) and the 25x25 menu icon. Everything is original: Aura's blue gradient and warm
corner glow (the shared brand colours) with a skyline that matches the in-app town.

Usage (any Python with Pillow):  python3 tools/make-icons.py
Writes into ../docs/store/ : cityscape-app-icon-80.png, cityscape-app-icon-144.png,
cityscape-menu-icon.png, and cityscape-icon-master.png (1024 px).
"""
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'docs', 'store'))

S = 1024  # master size


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def background():
    top_left, bottom_right = (70, 167, 244), (0, 101, 220)   # sampled from the Aura brand icon
    img = Image.new('RGB', (S, S))
    px = img.load()
    for y in range(S):
        for x in range(S):
            px[x, y] = lerp(top_left, bottom_right, (x + y) / (2.0 * (S - 1)))
    glow = Image.new('RGB', (S, S), (254, 217, 105))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).ellipse([-330, -330, 560, 560], fill=235)
    mask = mask.filter(ImageFilter.GaussianBlur(150))
    img.paste(glow, (0, 0), mask)
    return img


# (x, width, height) in master px. Heights echo the in-app town; two rows give depth.
BACK = [(40, 120, 330), (170, 100, 420), (290, 130, 360), (440, 110, 470), (570, 120, 380),
        (710, 100, 430), (830, 150, 350)]
FRONT = [(0, 150, 250), (160, 120, 340), (290, 110, 280), (410, 160, 440), (580, 120, 300),
         (710, 130, 370), (850, 174, 260)]


def skyline(img):
    base = S - 44
    layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for x, w, h in BACK:
        d.rectangle([x, base - h, x + w, base], fill=(255, 255, 255, 110))
    for x, w, h in FRONT:
        d.rectangle([x, base - h, x + w, base], fill=(255, 255, 255, 255))
        # windows: blue squares punched into the white, a few warm ones lit
        n = 0
        for wy in range(base - h + 34, base - 40, 52):
            for wx in range(x + 22, x + w - 30, 40):
                n += 1
                lit = (wx * 7 + wy * 3) % 5 == 0
                d.rectangle([wx, wy, wx + 20, wy + 26], fill=(254, 217, 105, 255) if lit else (46, 140, 238, 255))
    d.rectangle([0, base, S, S], fill=(255, 255, 255, 255))   # ground
    img.paste(layer, (0, 0), layer)
    # a soft sun disc in the glow
    sun = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sun).ellipse([170, 130, 330, 290], fill=(255, 244, 196, 255))
    img.paste(sun, (0, 0), sun.filter(ImageFilter.GaussianBlur(2)))
    return img


def master():
    return skyline(background())


def menu_icon():
    """25x25, white on transparent (the launcher tints it). Hard pixels, no antialiasing."""
    im = Image.new('LA', (25, 25), (0, 0))
    px = im.load()

    def box(x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px[x, y] = (255, 255)

    def hole(x, y):
        px[x, y] = (0, 0)

    # skyline: x0, x1, top row; the baseline is row 23
    for x0, x1, top in [(1, 4, 14), (6, 10, 8), (12, 15, 12), (17, 21, 5), (22, 23, 15)]:
        box(x0, top, x1, 23)
    box(0, 24, 24, 24)
    # windows punched out of the taller buildings
    for y in (11, 15, 19):
        for x in (7, 9):
            if y >= 10:
                hole(x, y)
    for y in (8, 12, 16, 20):
        for x in (18, 20):
            hole(x, y)
    for y in (16, 20):
        hole(2, y)
        hole(13, y - 3)
    # a small sun above the left roofs
    for (x, y) in [(x, y) for y, xs in ((3, (3, 4, 5)), (4, (2, 3, 4, 5, 6)), (5, (2, 3, 4, 5, 6)), (6, (2, 3, 4, 5, 6)), (7, (3, 4, 5))) for x in xs]:
        px[x, y] = (255, 255)
    return im


if __name__ == '__main__':
    m = master()
    m.save(os.path.join(OUT, 'cityscape-icon-master.png'))
    m.resize((144, 144), Image.LANCZOS).save(os.path.join(OUT, 'cityscape-app-icon-144.png'))
    m.resize((80, 80), Image.LANCZOS).save(os.path.join(OUT, 'cityscape-app-icon-80.png'))
    menu_icon().save(os.path.join(OUT, 'cityscape-menu-icon.png'))
    print('wrote cityscape icons to', OUT)
