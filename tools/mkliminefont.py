#!/usr/bin/env python3
"""Convert a TTF font to Limine's CP437 bitmap font format (8x16)."""

import sys
from PIL import Image, ImageDraw, ImageFont

def main():
    ttf_path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/chromeos/monotype/arialn.ttf"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "limine.fnt"
    font_size = int(sys.argv[3]) if len(sys.argv) > 3 else 16

    font = ImageFont.truetype(ttf_path, font_size)
    glyph_w, glyph_h = 8, 16

    data = bytearray()

    for code in range(256):
        try:
            char = bytes([code]).decode('cp437')
        except ValueError:
            char = ' '
        if code < 32:
            char = ' '

        img = Image.new('1', (glyph_w, glyph_h), 1)
        draw = ImageDraw.Draw(img)

        bbox = draw.textbbox((0, 0), char, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        ox = (glyph_w - tw) // 2 - bbox[0]
        oy = (glyph_h - th) // 2 - bbox[1]

        draw.text((ox, oy), char, font=font, fill=0)

        for y in range(glyph_h):
            byte = 0
            for x in range(glyph_w):
                if img.getpixel((x, y)) == 0:
                    byte |= (1 << (7 - x))
            data.append(byte)

    with open(out_path, 'wb') as f:
        f.write(data)
    print(f"Created {out_path} ({len(data)} bytes)")

if __name__ == '__main__':
    main()
