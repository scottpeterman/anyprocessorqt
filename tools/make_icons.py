#!/usr/bin/env python3
"""Render the app icon (PNG sizes, .ico, .icns) into packaging/icons/. Requires Pillow."""
from PIL import Image, ImageDraw

S = 1024


def render(size=S):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # rounded blue tile
    d.rounded_rectangle((40, 40, S - 40, S - 40), radius=200, fill=(0x1F, 0x4E, 0x9A, 255))
    # page with folded corner
    px0, py0, px1, py1 = 250, 170, 774, 854
    fold = 150
    d.polygon([(px0, py0), (px1 - fold, py0), (px1, py0 + fold), (px1, py1), (px0, py1)], fill=(255, 255, 255, 255))
    d.polygon([(px1 - fold, py0), (px1 - fold, py0 + fold), (px1, py0 + fold)], fill=(0xC9, 0xD6, 0xEA, 255))
    # heading bar + text lines
    d.rounded_rectangle((px0 + 70, py0 + 110, px0 + 330, py0 + 165), radius=20, fill=(0x1F, 0x4E, 0x9A, 255))
    y = py0 + 240
    for i, w in enumerate((380, 380, 300, 380, 340, 220)):
        d.rounded_rectangle((px0 + 70, y, px0 + 70 + w, y + 34), radius=17, fill=(0x9A, 0xA8, 0xBD, 255))
        y += 72 if i != 2 else 110
    return img.resize((size, size), Image.LANCZOS) if size != S else img


big = render()
for s in (16, 32, 48, 64, 128, 256, 512):
    big.resize((s, s), Image.LANCZOS).save(f"packaging/icons/anyprocessorqt-{s}.png")
big.resize((256, 256), Image.LANCZOS).save("packaging/icons/anyprocessorqt.png")
big.save("packaging/icons/anyprocessorqt.ico", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
big.save("packaging/icons/anyprocessorqt.icns")
print("icons written")
