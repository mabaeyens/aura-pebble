#!/usr/bin/env python3
"""Compose the optional 720x320 appstore banner from the real emulator screenshots.

Usage (any Python with Pillow):  python3 tools/make-banner.py
Reads  ../docs/store/cityscape-clear-day.png and cityscape-clear-night.png
Writes ../docs/store/cityscape-banner.png (720x320, the size the dashboard asks for).
The title is set in Atkinson Hyperlegible Next Bold, the font the app itself uses (SIL OFL).
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
STORE = os.path.normpath(os.path.join(HERE, '..', '..', 'docs', 'store'))
FONT = os.path.join(HERE, '..', 'resources', 'fonts', 'AtkinsonHyperlegibleNext-Bold.otf')
W, H = 720, 320

bg = Image.new('RGB', (W, H))
px = bg.load()
a, b = (30, 128, 236), (0, 72, 190)       # Aura brand blue, light to deep
for y in range(H):
    for x in range(W):
        t = min(1.0, (x * 0.55 + y) / (W * 0.55 + H))
        px[x, y] = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
glow = Image.new('RGB', (W, H), (254, 217, 105))
mask = Image.new('L', (W, H), 0)
ImageDraw.Draw(mask).ellipse([-210, -230, 150, 110], fill=200)
bg.paste(glow, (0, 0), mask.filter(ImageFilter.GaussianBlur(90)))

d = ImageDraw.Draw(bg)
title = ImageFont.truetype(FONT, 42)
sub = ImageFont.truetype(FONT, 18)
for i, line in enumerate(('Aura', 'Cityscape', 'Weather')):
    d.text((34, 62 + i * 48), line, font=title, fill=(255, 255, 255))
for i, line in enumerate(('A small town under', 'a living sky.', 'For the Pebble Time 2.')):
    d.text((36, 218 + i * 26), line, font=sub, fill=(255, 255, 255) if i < 2 else (214, 234, 255))

for name, x in (('clear-day', 292), ('clear-night', 508)):
    shot = Image.open(os.path.join(STORE, 'cityscape-%s.png' % name)).convert('RGB')
    y = (H - shot.height) // 2
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rectangle([x + 3, y + 7, x + shot.width + 3, y + shot.height + 7], fill=(0, 20, 70, 140))
    bg.paste(shadow, (0, 0), shadow.filter(ImageFilter.GaussianBlur(6)))
    d = ImageDraw.Draw(bg)
    d.rectangle([x - 3, y - 3, x + shot.width + 2, y + shot.height + 2], fill=(255, 255, 255))
    bg.paste(shot, (x, y))

bg.save(os.path.join(STORE, 'cityscape-banner.png'))
print('wrote cityscape-banner.png', bg.size)
