#!/usr/bin/env python3
"""
F1 Driver Flag Generator
Generates 24x16px 6-color dithered flag bitmaps for all current F1 driver nationalities
No internet needed — flags drawn from known color patterns
Output: .raw files for SD card at /flags/XX.raw
"""

from PIL import Image, ImageDraw
import os, struct

OUTPUT_DIR = "flags"
# Must match driver-row slot in main.cpp (24×16 px)
FLAG_W, FLAG_H = 24, 16

# EPD 6-color palette
EPD = {
    'black':  (0,   0,   0  ),
    'white':  (255, 255, 255),
    'yellow': (255, 215, 0  ),
    'red':    (228, 0,   27 ),
    'blue':   (0,   80,  200),
    'green':  (0,   160, 60 ),
}
EPD_IDX = {
    (0,0,0):       0x0,
    (255,255,255): 0x1,
    (255,215,0):   0x2,
    (228,0,27):    0x3,
    (0,80,200):    0x5,
    (0,160,60):    0x6,
}

def nearest_epd(r,g,b):
    best, bd = (255,255,255), float('inf')
    for c in EPD.values():
        d = sum((a-b_)**2 for a,b_ in zip((r,g,b),c))
        if d < bd: bd=d; best=c
    return best

def dither(img):
    img = img.convert("RGBA")
    bg = Image.new("RGBA",(FLAG_W,FLAG_H),(255,255,255,255))
    bg.paste(img, mask=img.split()[3])
    img = bg.convert("RGB")
    px = [[list(img.getpixel((x,y))) for x in range(FLAG_W)] for y in range(FLAG_H)]
    out_idx = []
    out_rgb = []
    for y in range(FLAG_H):
        ri, rr = [], []
        for x in range(FLAG_W):
            r,g,b = [max(0,min(255,int(v))) for v in px[y][x]]
            nr = nearest_epd(r,g,b)
            ri.append(EPD_IDX.get(nr,0x1))
            rr.append(nr)
            er,eg,eb = r-nr[0], g-nr[1], b-nr[2]
            def ae(py,px_,f):
                if 0<=py<FLAG_H and 0<=px_<FLAG_W:
                    px[py][px_][0]=max(0,min(255,px[py][px_][0]+er*f))
                    px[py][px_][1]=max(0,min(255,px[py][px_][1]+eg*f))
                    px[py][px_][2]=max(0,min(255,px[py][px_][2]+eb*f))
            ae(y,x+1,7/16); ae(y+1,x-1,3/16); ae(y+1,x,5/16); ae(y+1,x+1,1/16)
        out_idx.append(ri); out_rgb.append(rr)
    result = Image.new("RGB",(FLAG_W,FLAG_H))
    for y in range(FLAG_H):
        for x in range(FLAG_W):
            result.putpixel((x,y), tuple(out_rgb[y][x]))
    return result, out_idx

def save_raw(indices, path):
    w,h = FLAG_W, FLAG_H
    with open(path,'wb') as f:
        f.write(struct.pack('<HH',w,h))
        for row in indices:
            for i in range(0,w,2):
                hi = row[i] & 0x0F
                lo = row[i+1] & 0x0F if i+1<w else 0x01
                f.write(bytes([(hi<<4)|lo]))

def make_flag(draw_fn, name):
    img = Image.new("RGB",(FLAG_W,FLAG_H),(255,255,255))
    d = ImageDraw.Draw(img)
    draw_fn(img, d)
    dithered, indices = dither(img)
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    bmp_path = f"{OUTPUT_DIR}/{name}.bmp"
    raw_path = f"{OUTPUT_DIR}/{name}.raw"
    dithered.save(bmp_path)
    save_raw(indices, raw_path)
    print(f"  {name}: {bmp_path}")

B=EPD['blue']; W=EPD['white']; R=EPD['red']
G=EPD['green']; Y=EPD['yellow']; BK=EPD['black']

def flag_gb(img, d):
    # Simplified Union Jack — recognizable at small size
    # Blue background
    d.rectangle([0, 0, FLAG_W, FLAG_H], fill=B)
    # White horizontal cross (thick)
    d.rectangle([0, FLAG_H//2-3, FLAG_W, FLAG_H//2+3], fill=W)
    # White vertical cross (thick)  
    d.rectangle([FLAG_W//2-3, 0, FLAG_W//2+3, FLAG_H], fill=W)
    # Red horizontal cross (thinner, centered)
    d.rectangle([0, FLAG_H//2-1, FLAG_W, FLAG_H//2+2], fill=R)
    # Red vertical cross (thinner, centered)
    d.rectangle([FLAG_W//2-1, 0, FLAG_W//2+2, FLAG_H], fill=R)
    # Diagonal red stripes (St Patrick's cross simplified)
    for i in range(3):
        d.line([0+i, 0, FLAG_W, FLAG_H-i], fill=R, width=2)
        d.line([FLAG_W-i, 0, 0, FLAG_H-i], fill=R, width=2)

def flag_nl(img,d):   # Netherlands — red/white/blue horizontal
    h3=FLAG_H//3
    d.rectangle([0,0,FLAG_W,h3], fill=R)
    d.rectangle([0,h3,FLAG_W,h3*2], fill=W)
    d.rectangle([0,h3*2,FLAG_W,FLAG_H], fill=B)

def flag_mc(img,d):   # Monaco — red/white horizontal
    d.rectangle([0,0,FLAG_W,FLAG_H//2], fill=R)
    d.rectangle([0,FLAG_H//2,FLAG_W,FLAG_H], fill=W)

def flag_es(img,d):   # Spain — red/yellow/red horizontal
    h=FLAG_H//4
    d.rectangle([0,0,FLAG_W,h], fill=R)
    d.rectangle([0,h,FLAG_W,FLAG_H-h], fill=Y)
    d.rectangle([0,FLAG_H-h,FLAG_W,FLAG_H], fill=R)

def flag_fi(img,d):   # Finland — white + blue cross
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=W)
    d.rectangle([0,FLAG_H//2-2,FLAG_W,FLAG_H//2+2], fill=B)
    d.rectangle([FLAG_W//4-2,0,FLAG_W//4+2,FLAG_H], fill=B)

def flag_au(img,d):   # Australia — blue + union jack + stars
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=B)
    d.rectangle([0,0,FLAG_W//2,FLAG_H//2], fill=B)
    # Simple cross approximation
    d.rectangle([FLAG_W//4-1,0,FLAG_W//4+1,FLAG_H//2], fill=W)
    d.rectangle([0,FLAG_H//4-1,FLAG_W//2,FLAG_H//4+1], fill=W)

def flag_de(img,d):   # Germany — black/red/yellow horizontal
    h3=FLAG_H//3
    d.rectangle([0,0,FLAG_W,h3], fill=BK)
    d.rectangle([0,h3,FLAG_W,h3*2], fill=R)
    d.rectangle([0,h3*2,FLAG_W,FLAG_H], fill=Y)

def flag_fr(img,d):   # France — blue/white/red vertical
    w3=FLAG_W//3
    d.rectangle([0,0,w3,FLAG_H], fill=B)
    d.rectangle([w3,0,w3*2,FLAG_H], fill=W)
    d.rectangle([w3*2,0,FLAG_W,FLAG_H], fill=R)

def flag_ca(img,d):   # Canada — red/white/red + maple leaf
    w3=FLAG_W//4
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=W)
    d.rectangle([0,0,w3,FLAG_H], fill=R)
    d.rectangle([FLAG_W-w3,0,FLAG_W,FLAG_H], fill=R)
    # Simple maple leaf approximation — red cross
    cx,cy=FLAG_W//2,FLAG_H//2
    d.rectangle([cx-1,cy-4,cx+1,cy+4], fill=R)
    d.rectangle([cx-4,cy-1,cx+4,cy+1], fill=R)

def flag_mx(img,d):   # Mexico — green/white/red vertical
    w3=FLAG_W//3
    d.rectangle([0,0,w3,FLAG_H], fill=G)
    d.rectangle([w3,0,w3*2,FLAG_H], fill=W)
    d.rectangle([w3*2,0,FLAG_W,FLAG_H], fill=R)

def flag_cn(img,d):   # China — red + yellow stars
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=R)
    d.rectangle([2,2,8,8], fill=Y)

def flag_th(img,d):   # Thailand — red/white/blue/white/red
    h=FLAG_H//6
    d.rectangle([0,0,FLAG_W,h], fill=R)
    d.rectangle([0,h,FLAG_W,h*2], fill=W)
    d.rectangle([0,h*2,FLAG_W,h*4], fill=B)
    d.rectangle([0,h*4,FLAG_W,h*5], fill=W)
    d.rectangle([0,h*5,FLAG_W,FLAG_H], fill=R)

def flag_jp(img,d):   # Japan — white + red circle
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=W)
    cx,cy=FLAG_W//2,FLAG_H//2
    r=min(FLAG_W,FLAG_H)//4
    d.ellipse([cx-r,cy-r,cx+r,cy+r], fill=R)

def flag_nz(img,d):   # New Zealand — blue + red/white cross quarter
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=B)
    d.rectangle([0,0,FLAG_W//2,FLAG_H//2], fill=B)
    d.rectangle([FLAG_W//4-1,0,FLAG_W//4+1,FLAG_H//2], fill=W)
    d.rectangle([0,FLAG_H//4-1,FLAG_W//2,FLAG_H//4+1], fill=W)
    d.rectangle([FLAG_W//4-1,0,FLAG_W//4+1,FLAG_H//2], fill=R)

def flag_it(img,d):   # Italy — green/white/red vertical
    w3=FLAG_W//3
    d.rectangle([0,0,w3,FLAG_H], fill=G)
    d.rectangle([w3,0,w3*2,FLAG_H], fill=W)
    d.rectangle([w3*2,0,FLAG_W,FLAG_H], fill=R)

def flag_dk(img,d):   # Denmark — red + white cross
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=R)
    d.rectangle([0,FLAG_H//2-2,FLAG_W,FLAG_H//2+2], fill=W)
    d.rectangle([FLAG_W//3-2,0,FLAG_W//3+2,FLAG_H], fill=W)

def flag_be(img,d):   # Belgium — black/yellow/red vertical
    w3=FLAG_W//3
    d.rectangle([0,0,w3,FLAG_H], fill=BK)
    d.rectangle([w3,0,w3*2,FLAG_H], fill=Y)
    d.rectangle([w3*2,0,FLAG_W,FLAG_H], fill=R)

def flag_br(img,d):   # Brazil — green + yellow diamond + blue circle
    d.rectangle([0,0,FLAG_W,FLAG_H], fill=G)
    pts=[(FLAG_W//2,2),(FLAG_W-2,FLAG_H//2),(FLAG_W//2,FLAG_H-2),(2,FLAG_H//2)]
    d.polygon(pts, fill=Y)
    cx,cy=FLAG_W//2,FLAG_H//2
    r=FLAG_H//4
    d.ellipse([cx-r,cy-r,cx+r,cy+r], fill=B)

def flag_us(img,d):   # USA — stripes + blue canton (simplified)
    stripe_h = max(1, FLAG_H // 13)
    for i in range(13):
        y0 = i * stripe_h
        y1 = min(FLAG_H, y0 + stripe_h)
        fill_c = R if i % 2 == 0 else W
        d.rectangle([0, y0, FLAG_W, y1], fill=fill_c)
    cw = FLAG_W * 2 // 5
    ch = stripe_h * 7
    d.rectangle([0, 0, cw, ch], fill=B)

# F1 2025 driver nationality map: ISO code -> draw function
FLAGS = {
    'gb': flag_gb,   # Hamilton, Russell, Norris, Bearman
    'nl': flag_nl,   # Verstappen, Lawson
    'mc': flag_mc,   # Leclerc
    'es': flag_es,   # Sainz, Alonso, Gasly (FR actually)
    'fi': flag_fi,   # Bottas, Raikkonen
    'au': flag_au,   # Piastri, Ricciardo
    'de': flag_de,   # Hulkenberg, Schumacher
    'fr': flag_fr,   # Gasly, Ocon
    'ca': flag_ca,   # Stroll
    'mx': flag_mx,   # Perez
    'cn': flag_cn,   # Zhou
    'th': flag_th,   # Albon
    'jp': flag_jp,   # Tsunoda
    'nz': flag_nz,   # Lawson
    'it': flag_it,   # Antonelli
    'dk': flag_dk,   # Magnussen
    'be': flag_be,   # Vandoorne
    'br': flag_br,   # Piquet
    'us': flag_us,   # American drivers
}

# F1 API nationality string -> ISO code
NATIONALITY_MAP = {
    'British':     'gb',
    'Dutch':       'nl',
    'Monegasque':  'mc',
    'Spanish':     'es',
    'Finnish':     'fi',
    'Australian':  'au',
    'German':      'de',
    'French':      'fr',
    'Canadian':    'ca',
    'Mexican':     'mx',
    'Chinese':     'cn',
    'Thai':        'th',
    'Japanese':    'jp',
    'New Zealander':'nz',
    'Italian':     'it',
    'Danish':      'dk',
    'Belgian':     'be',
    'Brazilian':   'br',
    'American':    'us',
    'Argentine':   'ar',
    'Swiss':       'ch',
    'Polish':      'pl',
}

print(f"Generating {len(FLAGS)} flag bitmaps ({FLAG_W}x{FLAG_H}px, 6-color dithered)")
print(f"Output: ./{OUTPUT_DIR}/\n{'-'*50}")

for iso, fn in FLAGS.items():
    make_flag(fn, iso)

print(f"\nDone! Copy ./{OUTPUT_DIR}/*.raw to SD card under /flags/")
print("\nNationality -> ISO mapping for main.cpp:")
print("static const char* natToISO(const char* nat) {")
for nat, iso in NATIONALITY_MAP.items():
    print(f'  if(!strcmp(nat,"{nat}")) return "{iso}";')
print('  return nullptr;')
print("}")
