"""Regenerate assets/hud.bmp with Pillow and Source Code Pro Medium."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

font = ImageFont.truetype(sys.argv[1], 64)
atlas = Image.new("L", (768, 968))
draw = ImageDraw.Draw(atlas)
glyphs = [chr(code) for code in range(32, 127)] + [chr(code) for code in range(0x410, 0x450)]
glyphs.extend("Ёё·—")
for index, glyph in enumerate(glyphs):
    x, y = index % 16 * 48, index // 16 * 88
    draw.text((x + 4, y + 65), glyph, font=font, fill=255, anchor="ls")
draw.rectangle((756, 952, 767, 967), fill=255)
atlas.save(Path(__file__).resolve().parents[1] / "assets/hud.bmp")
