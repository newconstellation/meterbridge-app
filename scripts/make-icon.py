#!/usr/bin/env python3
"""Draws the Meter Bridge app icon: a small black metering rack unit with a
four-colour overlay spectrum on the left screen and a cyan loudness readout
on the right. Writes Resources/AppIcon.iconset (all sizes macOS needs).

    python3 scripts/make-icon.py      # needs Pillow
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 1024
ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "Resources", "AppIcon.iconset")

BLUE = (51, 153, 255)
ORANGE = (255, 158, 26)
GREEN = (64, 217, 115)
MAGENTA = (237, 77, 191)
CYAN = (54, 216, 245)


def rounded(draw, box, r, **kw):
    draw.rounded_rectangle(box, radius=r, **kw)


def vertical_gradient(size, top, bottom):
    w, h = size
    img = Image.new("RGB", size, top)
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def find_font(size):
    for path in [
        "/System/Library/Fonts/SFNSRounded.ttf",
        "/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf",
        "/usr/share/fonts/opentype/inter/Inter-Bold.otf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ]:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def spectrum(x, centre, width, height):
    """Smooth bump-shaped spectrum value 0..1 at position x (0..1)."""
    return height * math.exp(-((x - centre) ** 2) / (2 * width ** 2))


def draw_icon():
    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # macOS icon grid: 824 px rounded square centred in 1024.
    m = 100
    body = (m, m, S - m, S - m)
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rounded(ImageDraw.Draw(shadow), (m, m + 18, S - m, S - m + 18), 185, fill=(0, 0, 0, 150))
    icon.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(22)))

    chassis = vertical_gradient((S - 2 * m, S - 2 * m), (46, 48, 54), (12, 12, 14)).convert("RGBA")
    mask = Image.new("L", chassis.size, 0)
    rounded(ImageDraw.Draw(mask), (0, 0, chassis.size[0] - 1, chassis.size[1] - 1), 185, fill=255)
    icon.paste(chassis, (m, m), mask)
    d = ImageDraw.Draw(icon)
    rounded(d, body, 185, outline=(90, 94, 104), width=4)

    # Screws
    for sx, sy in [(m + 70, m + 70), (S - m - 70, m + 70), (m + 70, S - m - 70), (S - m - 70, S - m - 70)]:
        d.ellipse((sx - 20, sy - 20, sx + 20, sy + 20), fill=(28, 28, 30), outline=(110, 110, 116), width=4)
        d.line((sx - 11, sy - 11, sx + 11, sy + 11), fill=(130, 130, 136), width=5)

    # Main screen (spectrum)
    scr = (m + 60, m + 140, S - m - 60, m + 560)
    rounded(d, scr, 26, fill=(5, 10, 22), outline=(82, 102, 128), width=5)
    sx0, sy0, sx1, sy1 = scr[0] + 22, scr[1] + 22, scr[2] - 22, scr[3] - 22
    for i in range(1, 5):
        y = sy0 + (sy1 - sy0) * i / 5
        d.line((sx0, y, sx1, y), fill=(32, 44, 62), width=2)

    layers = [
        (BLUE, [(0.18, 0.08, 0.85), (0.55, 0.12, 0.35)]),
        (ORANGE, [(0.38, 0.07, 0.95), (0.75, 0.10, 0.30)]),
        (GREEN, [(0.62, 0.08, 0.80), (0.25, 0.10, 0.25)]),
        (MAGENTA, [(0.82, 0.07, 0.70), (0.45, 0.10, 0.25)]),
    ]
    n = 120
    for colour, bumps in layers:
        pts = []
        for i in range(n + 1):
            x = i / n
            v = 0.06 + sum(spectrum(x, c, w, h) for c, w, h in bumps)
            v = min(v, 0.97)
            pts.append((sx0 + x * (sx1 - sx0), sy1 - v * (sy1 - sy0)))
        fill_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        fd = ImageDraw.Draw(fill_layer)
        fd.polygon(pts + [(sx1, sy1), (sx0, sy1)], fill=colour + (70,))
        icon.alpha_composite(fill_layer)
        glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(glow).line(pts, fill=colour + (255,), width=14, joint="curve")
        icon.alpha_composite(glow.filter(ImageFilter.GaussianBlur(9)))
        d = ImageDraw.Draw(icon)
        d.line(pts, fill=colour + (255,), width=9, joint="curve")

    # Lower readout screens
    d = ImageDraw.Draw(icon)
    left = (m + 60, m + 590, m + 470, S - m - 120)
    right = (m + 500, m + 590, S - m - 60, S - m - 120)
    for box in (left, right):
        rounded(d, box, 22, fill=(5, 10, 22), outline=(82, 102, 128), width=5)

    # Left: segmented cyan loudness bar
    bx0, by0, bx1, by1 = left[0] + 30, left[1] + 30, left[2] - 30, left[3] - 30
    segs = 11
    seg_w = (bx1 - bx0) / segs
    for i in range(segs):
        on = i < 8
        colour = CYAN if on else (30, 44, 60)
        d.rectangle((bx0 + i * seg_w + 4, by0, bx0 + (i + 1) * seg_w - 4, by1), fill=colour)

    # Right: big readout
    font = find_font(118)
    text = "-18"
    tw = d.textlength(text, font=font)
    tx = (right[0] + right[2]) / 2 - tw / 2
    ty = (right[1] + right[3]) / 2 - 70
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((tx, ty), text, font=font, fill=CYAN + (255,))
    icon.alpha_composite(glow.filter(ImageFilter.GaussianBlur(10)))
    ImageDraw.Draw(icon).text((tx, ty), text, font=font, fill=(205, 242, 255, 255))

    return icon


def main():
    os.makedirs(OUT, exist_ok=True)
    icon = draw_icon()
    icon.save(os.path.join(ROOT, "Resources", "AppIcon-1024.png"))
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = size * scale
            name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
            icon.resize((px, px), Image.LANCZOS).save(os.path.join(OUT, name))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
