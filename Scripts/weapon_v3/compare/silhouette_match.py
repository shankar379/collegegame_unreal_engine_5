"""Objective shape match: V3 side render vs the Free Fire reference image.

python silhouette_match.py ak47 [m4a1 ...]
  - cuts the gun out of the reference picture (white background) and out of the V3 right-side render
  - turns both so the muzzle points right, scales both to the same length
  - slides/scales slightly to find the best fit, then reports IoU (overlap / union) as "shape match %"
  - writes ArtSource/WeaponLibraryV3/Comparison/<id>_overlay.png
      red = only in reference, cyan = only in V3, white = both
"""
import sys, os, json
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from refs import REF

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
LIB = os.path.join(ROOT, 'ArtSource', 'WeaponLibraryV3')
OUT = os.path.join(LIB, 'Comparison')
W = 600


def ref_mask(gid):
    f, box = REF[gid]
    im = np.asarray(Image.open(f).convert('RGB').crop(box)).astype(np.float32)
    lum = im.mean(-1)
    sat = im.max(-1) - im.min(-1)
    m = (lum < 175) | (sat > 45)          # gun pixels: not white, not the light-grey watermark
    return keep_largest(m)


def render_mask(gid, ref_shape):
    for name in ('%s_quick_right_alpha.png', '%s_views_right_alpha.png'):
        p = os.path.join(LIB, 'Previews', name % gid)
        if os.path.exists(p):
            break
    a = np.asarray(Image.open(p).convert('RGBA'))[..., 3] > 127
    a = keep_largest(a)
    # judge both at the reference's thumbnail resolution, so neither side gets extra detail
    small = Image.fromarray((a * 255).astype(np.uint8)).resize((ref_shape[1], max(1, round(a.shape[0] * ref_shape[1] / a.shape[1]))), Image.BILINEAR)
    return np.asarray(small) > 100


def keep_largest(m):
    """Keep only the biggest connected blob (drops stray bits of neighbouring guns and specks)."""
    if m.size < 400000:               # pure-python flood fill; masks here are small
        H, W_ = m.shape
        seen = np.zeros_like(m)
        best = None
        for y0, x0 in zip(*np.nonzero(m)):
            if seen[y0, x0]:
                continue
            stack, blob = [(y0, x0)], []
            seen[y0, x0] = True
            while stack:
                y, x = stack.pop()
                blob.append((y, x))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < H and 0 <= nx < W_ and m[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            stack.append((ny, nx))
            if best is None or len(blob) > len(best):
                best = blob
        m = np.zeros_like(m)
        ys, xs = zip(*best)
        m[list(ys), list(xs)] = True
    ys, xs = np.nonzero(m)
    return m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def norm(m, width):
    h = max(1, round(m.shape[0] * width / m.shape[1]))
    return np.asarray(Image.fromarray((m * 255).astype(np.uint8)).resize((width, h), Image.BILINEAR)) > 127


def best_iou(ref, mine):
    best = (0, None)
    P = 80
    H = max(ref.shape[0], mine.shape[0]) * 2 + 2 * P
    WW = W + 2 * P
    A = np.zeros((H, WW), bool)
    A[P:P + ref.shape[0], P:P + W] = ref
    for sx in (0.94, 0.97, 1.0, 1.03, 1.06):
        for sy in (0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15):
            m = np.asarray(Image.fromarray((mine * 255).astype(np.uint8)).resize((round(W * sx), max(1, round(mine.shape[0] * sy))))) > 127
            for dy in range(-15, 16, 3):
                for dx in (-12, -6, 0, 6, 12):
                    B = np.zeros((H, WW), bool)
                    y0, x0 = P + dy, P + dx + (W - m.shape[1]) // 2
                    B[y0:y0 + m.shape[0], x0:x0 + m.shape[1]] = m
                    iou = (A & B).sum() / max(1, (A | B).sum())
                    if iou > best[0]:
                        best = (iou, (A, B))
    A, B = best[1]
    ys, xs = np.nonzero(A | B)
    sl = (slice(ys.min(), ys.max() + 1), slice(xs.min(), xs.max() + 1))
    return best[0], (A[sl], B[sl])


def run(gid):
    ref = ref_mask(gid)[:, ::-1]           # Free Fire shots point the muzzle left; V3 renders point right
    mine = render_mask(gid, ref.shape)
    iou, (A, B) = best_iou(norm(ref, W), norm(mine, W))
    img = np.zeros(A.shape + (3,), np.uint8)
    img[A & ~B] = (230, 60, 60)
    img[B & ~A] = (60, 210, 230)
    img[A & B] = (235, 235, 235)
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(img).save(os.path.join(OUT, gid + '_overlay.png'))
    return round(iou * 100)


if __name__ == '__main__':
    res = {}
    for g in sys.argv[1:]:
        res[g] = run(g)
        print('%-12s shape match %d%%' % (g, res[g]))
    p = os.path.join(OUT, 'shape_scores.json')
    old = json.load(open(p)) if os.path.exists(p) else {}
    old.update(res)
    json.dump(old, open(p, 'w'), indent=1)
