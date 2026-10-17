"""Aura LCD icons: a slanted LCD "A" over a dot-matrix field that carries the Aura sky gradient.

Writes ../../docs/store/aura-lcd-icon.png (1024 master), app-icon-lcd-80/144.png and the
25x25 white-on-transparent launcher icon cgw50-menu-icon.png (also copied into resources).
Run: uv run --with pillow tools/make_icons.py
"""
from pathlib import Path
from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parents[2] / "docs" / "store"
RES = Path(__file__).resolve().parents[1] / "resources" / "images"
SHEAR = 0.12  # x shift per unit of height, like the slanted time digits


def seg_polys(x, y, w, h, t):
    """Polygons for a 7-segment A (a, b, c, e, f, g) plus unlit d, in a box at x,y."""
    g = t * 0.07  # gap between segments
    mid = y + h / 2
    xl, xr = x + t / 2, x + w - t / 2
    def hbar(cy):
        return [(xl + g, cy), (xl + g + t / 2, cy - t / 2), (xr - g - t / 2, cy - t / 2),
                (xr - g, cy), (xr - g - t / 2, cy + t / 2), (xl + g + t / 2, cy + t / 2)]
    def vbar(cx, yt, yb):
        return [(cx, yt + g), (cx + t / 2, yt + g + t / 2), (cx + t / 2, yb - g - t / 2),
                (cx, yb - g), (cx - t / 2, yb - g - t / 2), (cx - t / 2, yt + g + t / 2)]
    top, bot = y + t / 2, y + h - t / 2
    lit = [hbar(top), hbar(mid), vbar(x + t / 2, top, mid), vbar(x + t / 2, mid, bot),
           vbar(x + w - t / 2, top, mid), vbar(x + w - t / 2, mid, bot)]
    unlit = [hbar(bot)]
    return lit, unlit


def shear(poly, cy):
    return [(px + (cy - py) * SHEAR, py) for px, py in poly]


def lerp(a, b, k):
    return tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))


def master(size=1024):
    ss = 2
    n = size * ss
    img = Image.new("RGB", (n, n))
    px = img.load()
    glow, mid, deep = (255, 214, 92), (74, 160, 238), (0, 98, 222)
    for yy in range(n):
        for xx in range(n):
            d = (xx + yy) / (2 * n)
            k = d * 2
            px[xx, yy] = lerp(glow, mid, k) if k < 1 else lerp(mid, deep, k - 1)
    # dot-matrix field: dots take the gradient colour, lightened so they read on it
    dots = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    dd = ImageDraw.Draw(dots)
    pitch, r = n / 22, n / 22 * 0.24
    for j in range(22):
        for i in range(22):
            cx, cy = (i + 0.5) * pitch, (j + 0.5) * pitch
            base = img.getpixel((int(cx), int(cy)))
            c = lerp(base, (255, 255, 255), 0.16) + (255,)
            dd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)
    # darken the field under the dots so they sit as an unlit matrix, then lay dots on top
    base = img.copy()
    base = Image.blend(base, Image.new("RGB", (n, n), (0, 40, 120)), 0.28)
    base.paste(dots, (0, 0), dots)
    # the A
    bw, bh, t = n * 0.46, n * 0.60, n * 0.105
    x0, y0 = (n - bw) / 2 - bh * SHEAR / 2 + n * 0.01, (n - bh) / 2
    lit, unlit = seg_polys(x0, y0, bw, bh, t)
    cy = y0 + bh / 2
    layer = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    for p in unlit:
        ld.polygon(shear(p, cy), fill=(255, 255, 255, 0))
    shadow = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    for p in lit:
        sd.polygon([(a + n * 0.006, b + n * 0.010) for a, b in shear(p, cy)], fill=(0, 30, 100, 110))
    for p in lit:
        ld.polygon(shear(p, cy), fill=(255, 255, 255, 255))
    base.paste(shadow, (0, 0), shadow)
    base.paste(layer, (0, 0), layer)
    return base.resize((size, size), Image.LANCZOS)


def menu_icon(size=25):
    ss = 16
    n = size * ss
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # sparse dot field
    for j in range(5):
        for i in range(5):
            cx, cy = (i + 0.5) * n / 5, (j + 0.5) * n / 5
            r = n / 5 * 0.13
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 70))
    bw, bh, t = n * 0.58, n * 0.78, n * 0.16
    x0, y0 = (n - bw) / 2 - bh * SHEAR / 2, (n - bh) / 2
    lit, _ = seg_polys(x0, y0, bw, bh, t)
    cy = y0 + bh / 2
    # clear the dots under the A so it stays readable, then draw it
    clear = Image.new("L", (n, n), 0)
    cd = ImageDraw.Draw(clear)
    for p in lit:
        cd.polygon(shear(p, cy), fill=255, outline=255, width=int(n * 0.05))
    img.paste((0, 0, 0, 0), (0, 0), clear)
    d = ImageDraw.Draw(img)
    for p in lit:
        d.polygon(shear(p, cy), fill=(255, 255, 255, 255))
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    m = master()
    m.save(OUT / "aura-lcd-icon.png")
    m.resize((80, 80), Image.LANCZOS).save(OUT / "app-icon-lcd-80.png")
    m.resize((144, 144), Image.LANCZOS).save(OUT / "app-icon-lcd-144.png")
    mi = menu_icon()
    mi.save(OUT / "cgw50-menu-icon.png")
    mi.save(RES / "menu-icon.png")
    # preview of the launcher icon on a dark tile, 8x
    prev = Image.new("RGBA", (200, 200), (30, 30, 34, 255))
    prev.alpha_composite(mi.resize((200, 200), Image.NEAREST))
    prev.save(OUT.parent.parent / "aura-led-CGW-50" / "screenshots" / "menu-icon-preview.png")
