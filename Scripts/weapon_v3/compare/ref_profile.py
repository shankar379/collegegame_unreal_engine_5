"""Measure a Free Fire reference gun so a V3 model can be laid out with the same proportions.

python ref_profile.py <id> <length_cm> [stations]
Prints, for stations along the gun (muzzle on the RIGHT, stock/rear at y=0 ... muzzle at y=length),
the vertical runs of gun pixels in cm (z=0 at the top of the gun), and saves an enlarged, gridded
picture to Comparison/<id>_ref.png (grid every 5 cm) for checking by eye.
"""
import sys, os
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from refs import REF
from silhouette_match import ref_mask, OUT


def main(gid, L, stations=40):
    m = ref_mask(gid)[:, ::-1]
    h, w = m.shape
    k = L / w                      # cm per pixel
    print('%s: %dx%d px, %.2f cm/px, height %.1f cm' % (gid, w, h, k, h * k))
    for i in range(stations + 1):
        x = min(w - 1, round(i * (w - 1) / stations))
        col = m[:, x]
        runs, inrun = [], False
        for y, v in enumerate(col):
            if v and not inrun:
                start, inrun = y, True
            if not v and inrun:
                runs.append((start, y - 1)); inrun = False
        if inrun:
            runs.append((start, h - 1))
        txt = '  '.join('%.1f..%.1f' % (-a * k, -b * k) for a, b in runs if b - a >= 0)
        print('y=%5.1f  %s' % (x * k, txt))
    f, box = REF[gid]
    im = Image.open(f).convert('RGB').crop(box).transpose(Image.FLIP_LEFT_RIGHT)
    # crop to the gun only, enlarge, grid every 5 cm
    ys, xs = np.nonzero(ref_mask(gid)[:, ::-1])
    S = 8
    big = im.resize((im.width * S, im.height * S), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    full = ref_mask(gid)  # same crop offsets as the mask: find the bbox in the un-trimmed crop
    arr = np.asarray(im.convert('L')) < 175
    yy, xx = np.nonzero(arr)
    x0, y0 = xx.min(), yy.min()
    for c in range(0, int(L) + 1, 5):
        X = (x0 + c / k) * S
        d.line([(X, 0), (X, big.height)], fill=(255, 0, 0) if c % 10 == 0 else (255, 170, 170))
        if c % 10 == 0:
            d.text((X + 2, 2), str(c), fill=(255, 0, 0))
    for c in range(0, int(h * k) + 1, 5):
        Y = (y0 + c / k) * S
        d.line([(0, Y), (big.width, Y)], fill=(0, 0, 255) if c % 10 == 0 else (170, 170, 255))
        d.text((2, Y + 2), str(-c), fill=(0, 0, 255))
    os.makedirs(OUT, exist_ok=True)
    big.save(os.path.join(OUT, gid + '_ref.png'))


if __name__ == '__main__':
    main(sys.argv[1], float(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 40)
