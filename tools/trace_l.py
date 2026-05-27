#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import patch_f1_font_i as P

text = (ROOT / "src" / "f1_fonts.c").read_text(encoding="utf-8")
font = P.extract_font_bytes(text, "u8g2_font_f1bold10_tf")
fi = P.read_font_info(font)
blob = P.find_glyph_blob(font, "l")
print("blob", blob.hex(), "len", len(blob))

gw, gh, ox, oy, dx, bmp = P.decode_glyph(fi, blob)
print("gw gh", gw, gh, "ox oy dx", ox, oy, dx)
for row in bmp:
    print("".join("#" if v else "." for v in row))

b = P.BitDecoder(blob)
gw2 = b.u(fi["bits_per_char_width"])
gh2 = b.u(fi["bits_per_char_height"])
P.sgn(b, fi["bits_per_char_x"])
P.sgn(b, fi["bits_per_char_y"])
P.sgn(b, fi["bits_per_delta_x"])

x = y = 0
h = gh2
outer = 0
while True:
    outer += 1
    a = b.u(fi["bits_per_0"])
    fg = b.u(fi["bits_per_1"])
    inn = 0
    while True:
        inn += 1
        for _ in range(a):
            x += 1
            if x >= gw2:
                x = 0
                y += 1
        for _ in range(fg):
            x += 1
            if x >= gw2:
                x = 0
                y += 1
        cont = b.u(1)
        print(" outer", outer, "inner", inn, "a", a, "fg", fg, "cont", cont, "y", y)
        if cont == 0:
            break
    if y >= h:
        break

print("done y", y, "bi", b.i, "bp", b.bp, "payload len", len(blob))
if b.i >= len(blob):
    print("consumed all")
else:
    print("leftover", blob[b.i:], "bp", b.bp)

enc = P.encode_glyph(fi, gw, gh, ox, oy, dx, bmp)
print("re-encode", enc.hex(), "len", len(enc))
