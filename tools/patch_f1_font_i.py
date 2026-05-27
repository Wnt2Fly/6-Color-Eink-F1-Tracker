#!/usr/bin/env python3
"""
Fix Formula1 u8g2 fonts: lowercase 'i' was identical to 'l' (no tittle).
Build new 'i' = period dot above 'l' stem with 1px gap; re-encode glyph;
patch src/f1_fonts.c (updates only the 'i' glyph records + length bytes).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONTS_C = ROOT / "src" / "f1_fonts.c"

FONT_NAMES = [
    "u8g2_font_f1bold10_tf",
    "u8g2_font_f1bold14_tf",
    "u8g2_font_f1bold18_tf",
    "u8g2_font_f1bold24_tf",
]


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
            if i + 2 > len(s):
                raise ValueError(r"truncated \xHH escape")
            hx = s[i : i + 2]
            if not all(c in "0123456789abcdefABCDEF" for c in hx):
                raise ValueError(r"bad \xHH escape")
            out.append(int(hx, 16))
            i += 2
        elif e in "01234567":
            octs = e
            for _ in range(2):
                if i < len(s) and s[i] in "01234567":
                    octs += s[i]
                    i += 1
            out.append(int(octs, 8) % 256)
        elif e == "n":
            out.append(10)
        elif e == "t":
            out.append(9)
        elif e == "r":
            out.append(13)
        else:
            out.append(ord(e))
    return bytes(out)


def extract_font_bytes(text: str, name: str) -> bytes:
    m = re.search(
        rf"const\s+uint8_t\s+{re.escape(name)}\[\d+\][^=]*="
        r"\s*((?:\s*\"(?:\\.|[^\"\\])*\"\s*)+)\s*;",
        text,
        re.DOTALL,
    )
    if not m:
        raise SystemExit(f"Could not find font array {name}")
    frags = re.findall(r'"((?:\\.|[^"\\])*)"', m.group(1))
    return c_unescape_concat("".join(frags))


def iter_glyphs(font: bytes):
    pos = 23
    while pos < len(font):
        enc = font[pos]
        ln = font[pos + 1]
        if ln == 0:
            break
        yield pos, enc, font[pos + 2 : pos + ln]
        pos += ln


def find_glyph_blob(font: bytes, ch: str) -> bytes:
    t = ord(ch)
    for _pos, enc, blob in iter_glyphs(font):
        if enc == t:
            return blob
    raise KeyError(ch)


def read_font_info(font: bytes) -> dict[str, int]:
    return {
        "glyph_cnt": font[0],
        "bbx_mode": font[1],
        "bits_per_0": font[2],
        "bits_per_1": font[3],
        "bits_per_char_width": font[4],
        "bits_per_char_height": font[5],
        "bits_per_char_x": font[6],
        "bits_per_char_y": font[7],
        "bits_per_delta_x": font[8],
    }


class BitDecoder:
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


def sgn(b: BitDecoder, cnt: int) -> int:
    v = b.u(cnt)
    d = 1 << (cnt - 1)
    return int(v - d)


def decode_glyph(fi: dict[str, int], payload: bytes):
    b = BitDecoder(payload)
    gw = b.u(fi["bits_per_char_width"])
    gh = b.u(fi["bits_per_char_height"])
    ox = sgn(b, fi["bits_per_char_x"])
    oy = sgn(b, fi["bits_per_char_y"])
    dx = sgn(b, fi["bits_per_delta_x"])
    bmp = [[0] * gw for _ in range(gh)]
    if gw == 0 or gh == 0:
        return gw, gh, ox, oy, dx, bmp
    x = y = 0
    h = gh
    while True:
        a = b.u(fi["bits_per_0"])
        fg = b.u(fi["bits_per_1"])
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


class BitEncoder:
    def __init__(self):
        self.bits = 0
        self.nbits = 0
        self.buf = bytearray()

    def put(self, val: int, cnt: int) -> None:
        for i in range(cnt):
            bit = (val >> i) & 1
            self.bits |= bit << self.nbits
            self.nbits += 1
            if self.nbits >= 8:
                self.buf.append(self.bits & 0xFF)
                self.bits >>= 8
                self.nbits -= 8

    def finish(self) -> bytes:
        if self.nbits:
            self.buf.append(self.bits & 0xFF)
        return bytes(self.buf)


def encode_glyph(fi: dict[str, int], gw: int, gh: int, ox: int, oy: int, dx: int, bmp: list[list[int]]) -> bytes:
    e = BitEncoder()
    mpw = (1 << fi["bits_per_char_width"]) - 1
    mph = (1 << fi["bits_per_char_height"]) - 1
    mz0 = (1 << fi["bits_per_0"]) - 1
    mz1 = (1 << fi["bits_per_1"]) - 1

    def put_u(val: int, bits: int, mx: int) -> None:
        if val < 0 or val > mx:
            raise ValueError(f"unsigned {val} max {mx}")
        e.put(val, bits)

    def put_s(val: int, bits: int) -> None:
        d = 1 << (bits - 1)
        u = val + d
        if u < 0 or u > (1 << bits) - 1:
            raise ValueError(f"signed {val} out of range bits={bits}")
        e.put(u, bits)

    put_u(gw, fi["bits_per_char_width"], mpw)
    put_u(gh, fi["bits_per_char_height"], mph)
    put_s(ox, fi["bits_per_char_x"])
    put_s(oy, fi["bits_per_char_y"])
    put_s(dx, fi["bits_per_delta_x"])

    if gw == 0 or gh == 0:
        return e.finish()

    def simulate_apply(xx: int, yy: int, aaa: int, ffg: int) -> tuple[int, int, bool]:
        """Match decoder; fail if we traverse past (gw-1,gh-1) before consuming that cell."""
        xa, ya = xx, yy

        for _ in range(aaa):
            if xa >= gw or ya >= gh:
                return xa, ya, False
            if bmp[ya][xa] != 0:
                return xa, ya, False
            xa += 1
            if xa >= gw:
                xa = 0
                ya += 1

        for _ in range(ffg):
            if xa >= gw or ya >= gh:
                return xa, ya, False
            if bmp[ya][xa] != 1:
                return xa, ya, False
            xa += 1
            if xa >= gw:
                xa = 0
                ya += 1

        return xa, ya, True

    x = y = 0
    h = gh

    while True:
        if y >= h:
            break
        best_choice = None
        best_key = None

        for aaa in range(0, mz0 + 1):
            for ffg in range(0, mz1 + 1):
                if aaa == 0 and ffg == 0:
                    continue
                last_ok = 0
                xx, yy2 = x, y
                for _rep in range(1, 64):
                    xx, yy2, ok = simulate_apply(xx, yy2, aaa, ffg)
                    if not ok:
                        break
                    last_ok = _rep
                if last_ok == 0:
                    continue
                xx, yy2 = x, y
                for _ in range(last_ok):
                    xx, yy2, _ = simulate_apply(xx, yy2, aaa, ffg)
                key = (yy2 * gw + xx, last_ok, aaa, ffg)
                if best_key is None or key > best_key:
                    best_key = key
                    best_choice = (aaa, ffg, last_ok)

        if best_choice is None:
            raise ValueError(f"encode_glyph failed at {(x,y)}")

        a, fg, reps = best_choice
        put_u(a, fi["bits_per_0"], mz0)
        put_u(fg, fi["bits_per_1"], mz1)
        for ri in range(reps):
            x, y, ok = simulate_apply(x, y, a, fg)
            assert ok
            e.put(0 if ri == reps - 1 else 1, 1)
        if y >= h:
            break

    return e.finish()


def crop_ink(bmp: list[list[int]]) -> tuple[list[list[int]], int, int, int, int]:
    """Return cropped bitmap and (x0,y0,x1,y1) in original coords."""
    gh = len(bmp)
    gw = len(bmp[0]) if gh else 0
    rows = [y for y in range(gh) if any(bmp[y])]
    if not rows:
        return [[0]], 0, 0, 0, 0
    y0, y1 = rows[0], rows[-1]
    cols = [x for x in range(gw) if any(bmp[y][x] for y in range(y0, y1 + 1))]
    x0, x1 = cols[0], cols[-1]
    out = [row[x0 : x1 + 1] for row in bmp[y0 : y1 + 1]]
    return out, x0, y0, x1, y1


def compose_i_from_period_and_l(
    dot_bmp: list[list[int]],
    dot_gw: int,
    dot_gh: int,
    stem_bmp: list[list[int]],
    stem_gw: int,
    stem_gh: int,
    stem_ox: int,
    stem_oy: int,
    stem_dx: int,
) -> tuple[int, int, int, int, int, list[list[int]]]:
    dot, _dx0, _dy0, _dx1, _dy1 = crop_ink(dot_bmp)
    dgh = len(dot)
    dgw = len(dot[0]) if dgh else 0
    gap = 1
    gw = max(stem_gw, dgw)
    gh = dgh + gap + stem_gh
    out = [[0] * gw for _ in range(gh)]
    sx = (gw - stem_gw) // 2
    sy = gh - stem_gh
    for y in range(stem_gh):
        for x in range(stem_gw):
            out[sy + y][sx + x] = stem_bmp[y][x]
    ddx = (gw - dgw) // 2
    for y in range(dgh):
        for x in range(dgw):
            out[y][ddx + x] |= dot[y][x]
    return gw, gh, stem_ox, stem_oy, stem_dx, out


def c_escape_payload(payload: bytes) -> str:
    """Non-printables as \\000–\\377 octal so adjacent escapes don't merge (\\x issue across strings)."""
    parts: list[str] = []
    for b in payload:
        if 32 <= b < 127 and chr(b) not in '\\"?':
            parts.append(chr(b))
            continue
        parts.append("\\%03o" % b)
    return "".join(parts)


def locate_glyph(font: bytes, ch: str) -> tuple[int, int]:
    """Return (byte_offset, glyph_total_len_including_encoding_and_len_byte)."""
    pos = 23
    t = ord(ch)
    while pos < len(font):
        enc = font[pos]
        glen = font[pos + 1]
        if glen == 0:
            break
        if enc == t:
            return pos, glen
        pos += glen
    raise KeyError(ch)


def splice_i_record(font: bytes, new_payload: bytes) -> bytes:
    pos, glen = locate_glyph(font, "i")
    new_glen = 2 + len(new_payload)
    if new_glen > 255:
        raise ValueError("glyph record > 255 bytes")
    new_record = bytes([ord("i"), new_glen]) + new_payload
    assert len(new_record) == new_glen
    return font[:pos] + new_record + font[pos + glen :]


def format_font_c_array(fname: str, fb: bytes) -> str:
    """Rebuild array; split on raw-byte boundaries so octal escapes stay intact."""
    lines_out: list[str] = []
    chunk_sz = 16
    for i in range(0, len(fb), chunk_sz):
        chunk = fb[i : i + chunk_sz]
        lines_out.append('  "' + c_escape_payload(chunk) + '"')
    hdr = (
        f"const uint8_t {fname}[{len(fb)}] "
        f'U8G2_FONT_SECTION("{fname}") = \n'
    )
    return hdr + "\n".join(lines_out) + ";\n"


def declaration_to_bytes(decl: str) -> bytes:
    """Parse data string literals only (omit U8G2_FONT_SECTION(\"...\") helper name)."""
    work = decl[decl.index("=") + 1 : decl.rindex(";")]
    work = re.sub(r'U8G2_FONT_SECTION\("(\\.|[^"\\])*"\)\s*', "", work)
    frags = re.findall(r'"((?:\\.|[^"\\])*)"', work)
    if not frags:
        raise ValueError("no data strings in declaration")
    return c_unescape_concat("".join(frags))


def replace_font_array_block(text: str, fname: str, new_decl: str) -> str:
    needle = f"const uint8_t {fname}"
    start = text.find(needle)
    if start < 0:
        raise SystemExit(f"{fname}: array start not found")
    next_const = text.find("\nconst uint8_t ", start + len(needle))
    end_region = len(text) if next_const < 0 else next_const
    chunk = text[start:end_region]
    q = chunk.rfind('";')
    if q < 0:
        raise SystemExit(f"{fname}: array terminator not found")
    end_excl = start + q + len('";')
    return text[:start] + new_decl + text[end_excl:]


def patch_font_array(text: str, fname: str) -> str:
    font = extract_font_bytes(text, fname)
    fi = read_font_info(font)

    old_i_blob = find_glyph_blob(font, "i")
    old_l_blob = find_glyph_blob(font, "l")
    if old_i_blob != old_l_blob:
        print(f"NOTE {fname}: i payload differed from l", file=sys.stderr)

    gw_d, gh_d, _, _, _, dot_bmp = decode_glyph(fi, find_glyph_blob(font, "."))
    gw_l, gh_l, ox_l, oy_l, dx_l, l_bmp = decode_glyph(fi, old_l_blob)

    round_l = encode_glyph(fi, gw_l, gh_l, ox_l, oy_l, dx_l, l_bmp)
    if decode_glyph(fi, round_l)[5] != l_bmp:
        print(f"WARN {fname}: re-encoded l bitmap mismatch", file=sys.stderr)

    nw, nh, nox, noy, ndx, new_bmp = compose_i_from_period_and_l(
        dot_bmp, gw_d, gh_d, l_bmp, gw_l, gh_l, ox_l, oy_l, dx_l
    )
    new_payload = encode_glyph(fi, nw, nh, nox, noy, ndx, new_bmp)
    rw, rh, _, _, _, rb = decode_glyph(fi, new_payload)
    if rw != nw or rh != nh or rb != new_bmp:
        raise SystemExit(f"{fname}: new i roundtrip failed")

    new_font = splice_i_record(font, new_payload)
    decl = format_font_c_array(fname, new_font)

    verify = declaration_to_bytes(decl)
    if verify != new_font:
        raise SystemExit(f"{fname}: C emit round-trip mismatch")

    return replace_font_array_block(text, fname, decl)


def main() -> int:
    text = FONTS_C.read_text(encoding="utf-8")
    for fn in FONT_NAMES:
        print("patching", fn)
        text = patch_font_array(text, fn)
    FONTS_C.write_text(text, encoding="utf-8")
    print("Wrote", FONTS_C)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
