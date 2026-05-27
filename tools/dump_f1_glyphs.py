#!/usr/bin/env python3
"""Decode and print u8g2 glyphs from f1bold10 for inspection."""
import re
import sys
from pathlib import Path


def c_unescape_concat(s: str) -> bytes:
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        i += 1
        e = s[i]
        i += 1
        if e == "x":
            hx = ""
            while i < len(s) and s[i] in "0123456789abcdefABCDEF":
                hx += s[i]
                i += 1
            out.append(int(hx, 16))
        elif e in "01234567":
            octs = e
            for _ in range(2):
                if i < len(s) and s[i] in "01234567":
                    octs += s[i]
                    i += 1
            out.append(int(octs, 8) % 256)
        elif e == "n":
            out.append(10)
        else:
            out.append(ord(e))
    return bytes(out)


ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / "src" / "f1_fonts.c").read_text(encoding="utf-8")
m = re.search(
    r"const\s+uint8_t\s+u8g2_font_f1bold10_tf\[\d+\][^=]*=\s*([\s\S]*?);",
    text,
)
assert m
frags = re.findall(r'"([\s\S]*?)"', m.group(1))
literals = "".join(frags)
font = c_unescape_concat(literals)
print("string fragments", len(frags), "font bytes", len(font), "expect 1169")

bcw, bch, bcx, bcy, bcdx = font[4], font[5], font[6], font[7], font[8]
bp0, bp1 = font[2], font[3]


class B:
    __slots__ = ("d", "i", "bp")

    def __init__(self, d: bytes):
        self.d = d
        self.i = 0
        self.bp = 0

    def u(self, cnt: int) -> int:
        val = self.d[self.i] >> self.bp
        bpp = self.bp + cnt
        if bpp >= 8:
            s = 8 - self.bp
            self.i += 1
            bpp -= 8
            if bpp > 0:
                val |= self.d[self.i] << s
            self.bp = bpp
        else:
            self.bp = bpp
        return val & ((1 << cnt) - 1)


def sgn(b: B, cnt: int) -> int:
    v = b.u(cnt)
    d = 1 << (cnt - 1)
    return v - d


def iter_glyphs(font: bytes):
    pos = 23
    while pos < len(font):
        enc = font[pos]
        ln = font[pos + 1]
        if ln == 0:
            break
        yield enc, font[pos + 2 : pos + ln], pos, ln
        pos += ln


def find_glyph(font: bytes, ch: str) -> bytes:
    target = ord(ch)
    for enc, payload, _, _ in iter_glyphs(font):
        if enc == target:
            return payload
    raise KeyError(ch)


def decode(payload: bytes):
    b = B(payload)
    gw = b.u(bcw)
    gh = b.u(bch)
    ox = sgn(b, bcx)
    oy = sgn(b, bcy)
    dx = sgn(b, bcdx)
    bmp = [[0] * gw for _ in range(gh)]
    if gw and gh:
        x = y = 0
        h = gh
        while y < h:
            a = b.u(bp0)
            fg = b.u(bp1)
            while True:
                for _ in range(a):
                    if x < gw and y < gh:
                        bmp[y][x] = 0
                    x += 1
                    if x >= gw:
                        x = 0
                        y += 1
                for _ in range(fg):
                    if x < gw and y < gh:
                        bmp[y][x] = 1
                    x += 1
                    if x >= gw:
                        x = 0
                        y += 1
                if b.u(1) == 0:
                    break
            if y >= h:
                break
    return gw, gh, ox, oy, dx, bmp


def dump(name: str, bmp):
    print(name)
    for row in bmp:
        print("".join("#" if v else "." for v in row))
    print("---")


gens = list(iter_glyphs(font))
print("total glyphs", len(gens), "last", chr(gens[-1][0]), gens[-1][0])

# find 'i' explicitly
found = False
for enc, pl, pos, ln in iter_glyphs(font):
    if enc == ord("i"):
        print("FOUND i at", pos, "len", ln, "hex", pl.hex())
        found = True

if not found:
    print("ERROR: no glyph for i in font")
    sys.exit(1)

for ch in "ilj!.":
    gw, gh, ox, oy, dx, bmp = decode(find_glyph(font, ch))
    print(ch, "gw", gw, "gh", gh, "ox", ox, "oy", oy, "dx", dx)
    dump(ch, bmp)
