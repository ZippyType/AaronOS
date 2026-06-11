#!/usr/bin/env python3
"""Convert a TTF font to Limine's CP437 bitmap font format (8x16)."""

import sys
from PIL import Image, ImageDraw, ImageFont

def main():
    ttf_path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/chromeos/monotype/arialn.ttf"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "limine.fnt"
    font_size = int(sys.argv[3]) if len(sys.argv) > 3 else 16

    font = ImageFont.truetype(ttf_path, font_size)
    gw, gh = 8, 16

    data = bytearray()

    for code in range(256):
        try:
            char = bytes([code]).decode('cp437')
        except ValueError:
            char = ' '
        if code < 32:
            char = ' '

        img = Image.new('L', (gw * 4, gh * 4), 255)
        draw = ImageDraw.Draw(img)

        try:
            bbox = draw.textbbox((0, 0), char, font=font)
        except Exception:
            bbox = (0, 0, 0, 0)

        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]

        ox = (gw * 4 - tw) // 2 - bbox[0]
        oy = (gh * 4 - th) // 2 - bbox[1]

        draw.text((ox, oy), char, font=font, fill=0)

        result = Image.new('1', (gw, gh), 1)
        scale = min(gw / max(tw, 1), gh / max(th, 1), 1.0)
        if scale < 1.0:
            nw = max(int(gw * 4 * scale), 1)
            nh = max(int(gh * 4 * scale), 1)
            scaled = img.resize((nw, nh), Image.LANCZOS)
            iimg = Image.new('1', (gw * 4, gh * 4), 1)
            sx = (gw * 4 - nw) // 2
            sy = (gh * 4 - nh) // 2
            iimg.paste(scaled, (sx, sy))
            result = iimg.resize((gw, gh), Image.LANCZOS)
        else:
            rimg = img.resize((gw, gh), Image.LANCZOS)
            for y in range(gh):
                for x in range(gw):
                    if rimg.getpixel((x, y)) < 128:
                        result.putpixel((x, y), 0)
                    else:
                        result.putpixel((x, y), 1)

        for y in range(gh):
            byte = 0
            for x in range(gw):
                if result.getpixel((x, y)) == 0:
                    byte |= (1 << (7 - x))
            data.append(byte)

    with open(out_path, 'wb') as f:
        f.write(data)
    print(f"Created {out_path} ({len(data)} bytes)")

if __name__ == '__main__':
    main()
