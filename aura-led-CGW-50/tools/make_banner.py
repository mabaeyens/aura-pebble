"""Aura LCD listing banner (720 x 320): the icon's sky gradient and dot field, the name, and two watches.

Writes ../../docs/store/cgw50-banner.png from three store screenshots: cgw50-night.png, cgw50-weather-clean.png and cgw50-banner-stack.png.
The title is DSEG14 Classic Bold (SIL OFL, fonts/) with no unlit segments; the rest is Liberation Sans Bold (SIL OFL), borrowed from aura-analog/resources/fonts.
Run: uv run --with pillow tools/make_banner.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
STORE = ROOT / "docs" / "store"
LCD_FONT = ROOT / "aura-led-CGW-50" / "fonts" / "DSEG14Classic-Bold.ttf"
FONT = ROOT / "aura-analog" / "resources" / "fonts" / "LiberationSans-Bold.ttf"
W, H = 720, 320
SS = 2  # draw at 2x and downsample for clean edges


def lerp(a, b, k):
    return tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))


def background():
    img = Image.new("RGB", (W * SS, H * SS))
    px = img.load()
    c0, c1, c2 = (250, 214, 118), (52, 126, 210), (0, 74, 190)  # icon: sun yellow, blue, deep blue
    for y in range(H * SS):
        for x in range(W * SS):
            t = (x / (W * SS)) * 0.8 + (y / (H * SS)) * 0.45
            px[x, y] = lerp(c0, c1, t / 0.45) if t < 0.45 else lerp(c1, c2, (t - 0.45) / 0.8)
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    step, r = 22 * SS, 4.2 * SS
    for gy in range(step // 2, H * SS, step):
        for gx in range(step // 2, W * SS, step):
            d.ellipse([gx - r, gy - r, gx + r, gy + r], fill=(255, 255, 255, 20))
    img.paste(over, (0, 0), over)
    return img


def watch(shot, scale=0.68):
    s = Image.open(STORE / shot).convert("RGB")
    s = s.resize((round(s.width * scale * SS), round(s.height * scale * SS)), Image.LANCZOS)
    pad = 10 * SS
    w, h = s.width + 2 * pad, s.height + 2 * pad
    body = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=15 * SS, fill=(24, 24, 28, 255))
    d.rounded_rectangle([2 * SS, 2 * SS, w - 1 - 2 * SS, h - 1 - 2 * SS], radius=13 * SS, outline=(70, 70, 78, 255), width=SS)
    body.paste(s, (pad, pad))
    return body


def lcd_text(img, text, x, y, font, pitch=36, thicken=1):
    """Letters on a fixed LCD pitch, strokes thickened so they read as solid segments."""
    for i, ch in enumerate(text):
        mask = Image.new("L", img.size, 0)
        ImageDraw.Draw(mask).text(((x + i * pitch) * SS, y * SS), ch, font=font, fill=255)
        cy = (y + font.size / SS * 0.62) * SS   # slant about the letter's middle, like the watch digits
        k = 0.12
        mask = mask.transform(mask.size, Image.AFFINE, (1, k, -k * cy, 0, 1, 0), Image.BICUBIC)
        mask = mask.filter(ImageFilter.MaxFilter(thicken * SS // 2 * 2 + 1))
        img.paste(Image.new("RGBA", img.size, (255, 255, 255, 255)), (0, 0), mask)


def main():
    img = background().convert("RGBA")
    # drawn back to front: dark red, deep blue, then white and navy on top
    shots = [watch("cgw50-banner-stack.png"), watch("cgw50-night.png"), watch("cgw50-weather-clean.png")]
    for body, x, y in zip(shots, (558, 322, 440), (62, 78, 30)):
        shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(shadow).rounded_rectangle(
            [x * SS + 4 * SS, y * SS + 10 * SS, x * SS + body.width + 4 * SS, y * SS + body.height + 10 * SS],
            radius=15 * SS, fill=(0, 20, 70, 110))
        img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(9 * SS)))
        img.alpha_composite(body, (x * SS, y * SS))
    d = ImageDraw.Draw(img)
    title = ImageFont.truetype(str(LCD_FONT), 46 * SS)
    sub = ImageFont.truetype(str(FONT), 18 * SS)
    small = ImageFont.truetype(str(FONT), 15 * SS)
    lcd_text(img, "AURA", 36, 72, title)
    lcd_text(img, "LCD", 36 + 4 * 36 + 16, 72, title)
    d.text((36 * SS, 142 * SS), "The Casio Cosmo Phase look,", font=sub, fill="white")
    d.text((36 * SS, 170 * SS), "built for a quick glance.", font=sub, fill="white")
    d.text((36 * SS, 262 * SS), "Pebble Time 2   |   Free and open source", font=small, fill=(10, 36, 110))
    img.convert("RGB").resize((W, H), Image.LANCZOS).save(STORE / "cgw50-banner.png")


main()
