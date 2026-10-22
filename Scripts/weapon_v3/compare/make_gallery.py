"""Build the proof images for Weapon Library V3.

python make_gallery.py            -> scores every gun that has a reference, then writes
  Comparison/V3_vs_Reference.png   reference | V3 side view | measured shape match % (and the V2 score)
  Comparison/V3_Gallery.png        every V3 weapon in 3/4 view
  Comparison/V3_scores.csv
"""
import os, sys, json, csv
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageChops
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from refs import REF
from silhouette_match import run as shape_score, LIB, OUT

ORDER = ['ak47', 'm4a1', 'm416', 'scar', 'an94', 'xm8', 'famas', 'aug', 'groza', 'm14', 'sks', 'svd', 'vss', 'woodpecker', 'awm', 'kar98', 'm82',
         'mp40', 'mp5', 'ump', 'p90', 'thompson', 'vector', 'bizon', 'mac10', 'm249', 'm60', 'kord', 'm1887', 'm1014', 'spas12', 'mag7', 'm870',
         'aa12', 'deagle', 'usp', 'g18', 'm500', 'm1917', 'handcannon', 'm79', 'mgl', 'gatling', 'crossbow', 'plasma', 'charge_buster',
         'healing_pistol', 'railgun', 'pan', 'machete', 'bat', 'katana', 'scythe', 'knife']
ROTATED = {'pan', 'katana', 'scythe'}          # shown at an angle in the reference picture: not measurable
ORIGINAL = {'plasma', 'charge_buster', 'healing_pistol', 'railgun'}  # original designs in the reference style
P = os.path.join(LIB, 'Previews')


def font(sz, bold=False):
    try:
        return ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf' if bold else 'C:/Windows/Fonts/segoeui.ttf', sz)
    except OSError:
        return ImageFont.load_default()


def trim(im, bg, t=14):
    d = ImageChops.difference(im, Image.new('RGB', im.size, bg)).convert('L').point(lambda v: 255 if v > t else 0)
    b = d.getbbox()
    return im.crop(b) if b else im


def fit(im, w, h, bg):
    s = min(w / im.width, h / im.height)
    im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.LANCZOS)
    c = Image.new('RGB', (w, h), bg)
    c.paste(im, ((w - im.width) // 2, (h - im.height) // 2))
    return c


def view(gid, which):
    """which = 'right' or 'front34'"""
    for base in ('_views', '_quick'):
        if which == 'right':
            p = os.path.join(P, gid + base + '_right.png')
            if os.path.exists(p):
                return Image.open(p).convert('RGB')
        else:
            p = os.path.join(P, gid + base + '.png')
            if os.path.exists(p):
                sheet = Image.open(p).convert('RGB')
                w = sheet.width // 3
                idx = 3 if base == '_views' else 1          # views sheet: right,left,top / front34,... ; quick: right,front34,rear34
                r, c = divmod(idx, 3)
                h = sheet.height // (2 if base == '_views' else 1)
                return sheet.crop((c * w, r * h, (c + 1) * w, (r + 1) * h))
    return None


def v2_scores():
    p = os.path.join(OUT, 'V2_vs_Reference_scores.csv')
    out = {}
    if os.path.exists(p):
        for row in csv.DictReader(open(p)):
            try:
                out[row['gun']] = int(row['match_percent'])
            except (ValueError, KeyError):
                pass
    return out


def main():
    v2 = v2_scores()
    scores = {}
    for g in ORDER:
        if REF.get(g) and g not in ROTATED and g not in ORIGINAL and view(g, 'right') is not None:
            try:
                scores[g] = shape_score(g)
            except Exception as e:
                print('score failed', g, e)
    json.dump(scores, open(os.path.join(OUT, 'shape_scores.json'), 'w'), indent=1)
    with open(os.path.join(OUT, 'V3_scores.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['gun', 'v2_visual_score', 'v3_shape_match', 'note'])
        for g in ORDER:
            note = 'rotated in reference' if g in ROTATED else 'original design' if g in ORIGINAL else '' if REF.get(g) else 'no reference'
            w.writerow([g, v2.get(g, ''), scores.get(g, ''), note])

    # ---- V3 vs reference sheet
    F, f, T, B = font(22, True), font(16), font(40, True), font(30, True)
    CW, CH, COLS = 300, 120, 3
    cellw, cellh = CW * 2 + 30, CH + 54
    rows = (len(ORDER) + COLS - 1) // COLS
    head = 200
    img = Image.new('RGB', (COLS * cellw + 40, head + rows * cellh + 20), (18, 21, 26))
    d = ImageDraw.Draw(img)
    measured = [scores[g] for g in scores]
    v2m = [v2[g] for g in scores if g in v2]
    d.text((20, 14), 'Weapon Library V3 vs your Free Fire reference images', font=T, fill=(240, 240, 240))
    d.text((20, 70), 'Left = reference  |  Right = V3 model (side view)  |  % = measured outline overlap at the reference thumbnail size (about 85-90% is the ceiling for blurry thumbnails)',
           font=f, fill=(180, 190, 200))
    d.text((20, 96), 'Grey tag = V2 score for comparison.  Melee shown at an angle in the reference and the original sci-fi designs are not measured.', font=f, fill=(180, 190, 200))
    if measured:
        d.text((20, 132), 'Average shape match: V3 %d%%   (V2 was %d%% on the same guns)' % (round(sum(measured) / len(measured)), round(sum(v2m) / max(1, len(v2m)))),
               font=B, fill=(255, 200, 80))
    col = lambda p: (220, 60, 60) if p < 40 else (235, 150, 40) if p < 60 else (220, 200, 50) if p < 75 else (80, 200, 90)
    for i, g in enumerate(ORDER):
        r, c = divmod(i, COLS)
        x, y = 20 + c * cellw, head + r * cellh
        d.rectangle([x, y, x + cellw - 12, y + cellh - 10], fill=(30, 34, 41))
        d.text((x + 10, y + 6), '%d. %s' % (i + 1, g.upper().replace('_', ' ')), font=F, fill=(235, 235, 235))
        if REF.get(g):
            fn, b = REF[g]
            ref = Image.open(fn).convert('RGB').crop(b)
            img.paste(fit(ref, CW - 10, CH - 6, (255, 255, 255)), (x + 8, y + 40))
        else:
            d.rectangle([x + 8, y + 40, x + CW - 2, y + CH + 34], fill=(45, 45, 50))
            d.text((x + 90, y + 86), 'no reference', font=F, fill=(150, 150, 150))
        v = view(g, 'right')
        if v is not None:
            v = trim(v, v.getpixel((3, 3)))
            img.paste(fit(v, CW - 10, CH - 6, (38, 43, 52)), (x + CW + 12, y + 40))
        tag = '%d%%' % scores[g] if g in scores else ('angled' if g in ROTATED else 'original' if g in ORIGINAL else '')
        if tag:
            w = d.textlength(tag, font=B)
            d.rounded_rectangle([x + cellw - 30 - w - 16, y + 4, x + cellw - 24, y + 36], 6, fill=col(scores[g]) if g in scores else (90, 120, 170))
            d.text((x + cellw - 30 - w - 8, y + 2), tag, font=B, fill=(0, 0, 0))
        if g in v2:
            t2 = 'V2 %d%%' % v2[g]
            w2 = d.textlength(t2, font=f)
            d.rounded_rectangle([x + cellw - 30 - w2 - 130, y + 8, x + cellw - 30 - 118, y + 32], 5, fill=(70, 74, 82))
            d.text((x + cellw - 30 - w2 - 124, y + 9), t2, font=f, fill=(220, 220, 220))
    img.save(os.path.join(OUT, 'V3_vs_Reference.png'))

    # ---- gallery: every weapon in 3/4 view
    G, W_, H_ = 6, 420, 250
    gal = Image.new('RGB', (G * W_ + 20, ((len(ORDER) + G - 1) // G) * (H_ + 34) + 90), (22, 26, 32))
    d = ImageDraw.Draw(gal)
    d.text((16, 12), 'Campus Combat - Weapon Library V3 (%d weapons)' % len(ORDER), font=T, fill=(240, 240, 240))
    for i, g in enumerate(ORDER):
        r, c = divmod(i, G)
        x, y = 10 + c * W_, 80 + r * (H_ + 34)
        v = view(g, 'front34')
        if v is not None:
            v = trim(v, v.getpixel((3, 3)))
            gal.paste(fit(v, W_ - 10, H_, (36, 41, 50)), (x, y))
        d.text((x + 6, y + H_ + 4), g.upper().replace('_', ' '), font=F, fill=(225, 225, 225))
    gal.save(os.path.join(OUT, 'V3_Gallery.png'))
    print('scored', len(scores), 'avg', round(sum(measured) / max(1, len(measured))))


if __name__ == '__main__':
    main()
