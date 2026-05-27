import re
import sys

sys.path.insert(0, "s:/Arduino/F1 Tracker Large/tools")
import patch_f1_font_i as P
from pathlib import Path

text = Path("s:/Arduino/F1 Tracker Large/src/f1_fonts.c").read_text(encoding="utf-8")
font = P.extract_font_bytes(text, "u8g2_font_f1bold10_tf")
fi = P.read_font_info(font)
l = P.find_glyph_blob(font, "l")
gw, gh, ox, oy, dx, lbmp = P.decode_glyph(fi, l)
gw_d, gh_d, *_r, db = P.decode_glyph(fi, P.find_glyph_blob(font, "."))
nw, nh, nox, noy, ndx, nb = P.compose_i_from_period_and_l(
    db, gw_d, gh_d, lbmp, gw, gh, ox, oy, dx
)
np = P.encode_glyph(fi, nw, nh, nox, noy, ndx, nb)
nf = P.splice_i_record(font, np)
decl = P.format_font_c_array("u8g2_font_f1bold10_tf", nf)

work = decl[decl.index("=") + 1 : decl.rindex(";")]
work2 = re.sub(r'U8G2_FONT_SECTION\("(\\.|[^"\\])*"\)\s*', "", work)
print("stripped repr", repr(work2[:200]))
frags = re.findall(r'"((?:\\.|[^"\\])*)"', work2)
print("nfrags", len(frags), "first frag", repr(frags[0][:40]) if frags else None)
raw_joined = "".join(frags)
print("raw_joined chars", len(raw_joined), "bytes if unescaped", len(P.c_unescape_concat(raw_joined)))
vb = P.declaration_to_bytes(decl)
print("lens", len(nf), len(vb), "b0", nf[0], vb[0])


def esc_join(fb):
    return "".join(P.c_escape_payload(fb[i : i + 16]) for i in range(0, len(fb), 16))


sj = esc_join(nf)
print("direct concat unesc len", len(P.c_unescape_concat(sj)))
