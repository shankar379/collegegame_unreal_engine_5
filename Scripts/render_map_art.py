"""Renders scenic key-art banners of the real L_Match campus straight from the exported
box geometry (college_boxes.json, the same boxes the three.js -> UE import built the level
from). Painter's algorithm + flat shading + sky gradient + haze. No engine, no editor.

Output: 1024-wide PNGs for the map-select banners and mode key-art strips.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SRC = r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal\college_boxes.json"
OUT = sys.argv[1] if len(sys.argv) > 1 else "."

# ---------------------------------------------------------------- geometry

def load_boxes():
    raw = json.load(open(SRC))
    c = np.array([b["c"] for b in raw], dtype=np.float64)          # centre, UE cm
    ax = np.array([b["x"] for b in raw], dtype=np.float64)         # three.js local X, in UE space
    ay = np.array([b["y"] for b in raw], dtype=np.float64)         # three.js local Y, in UE space
    s = np.array([b["s"] for b in raw], dtype=np.float64) * 0.5    # half extents, in that same order
    ax /= np.linalg.norm(ax, axis=1, keepdims=True) + 1e-9
    ay /= np.linalg.norm(ay, axis=1, keepdims=True) + 1e-9
    az = np.cross(ax, ay)                                          # local Z = the exporter's depth
    az /= np.linalg.norm(az, axis=1, keepdims=True) + 1e-9
    # 8 corners: sign table
    signs = np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)], dtype=np.float64)
    # corners[i, k] = c + sum(sign_k * half * axis)
    corners = (c[:, None, :]
               + (signs[None, :, 0, None] * s[:, None, 0, None]) * ax[:, None, :]
               + (signs[None, :, 1, None] * s[:, None, 1, None]) * ay[:, None, :]
               + (signs[None, :, 2, None] * s[:, None, 2, None]) * az[:, None, :])
    return c, corners, s * 2.0, ax, ay, az


# corner index = 4*ix + 2*iy + iz with ix/iy/iz in {0,1} -> sign -1/+1
def ci(ix, iy, iz):
    return 4 * ix + 2 * iy + iz


# six faces as (corner quad, local-axis index, sign)
FACES = [
    ([ci(0, 0, 0), ci(0, 1, 0), ci(0, 1, 1), ci(0, 0, 1)], 0, -1),
    ([ci(1, 0, 0), ci(1, 0, 1), ci(1, 1, 1), ci(1, 1, 0)], 0, +1),
    ([ci(0, 0, 0), ci(0, 0, 1), ci(1, 0, 1), ci(1, 0, 0)], 1, -1),
    ([ci(0, 1, 0), ci(1, 1, 0), ci(1, 1, 1), ci(0, 1, 1)], 1, +1),
    ([ci(0, 0, 0), ci(1, 0, 0), ci(1, 1, 0), ci(0, 1, 0)], 2, -1),
    ([ci(0, 0, 1), ci(0, 1, 1), ci(1, 1, 1), ci(1, 0, 1)], 2, +1),
]


# ---------------------------------------------------------------- shading

def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


class Palette:
    """One mood = sky ramp + sun + ground + building tints + haze."""

    def __init__(self, sky_top, sky_mid, sky_low, sun, ground, wall, roof, accent, haze,
                 lawn, road, haze_strength=1.0):
        self.sky_top, self.sky_mid, self.sky_low = sky_top, sky_mid, sky_low
        self.sun, self.ground, self.wall, self.roof, self.accent = sun, ground, wall, roof, accent
        self.lawn, self.road = lawn, road
        self.haze, self.haze_strength = haze, haze_strength


DUSK = Palette(
    sky_top=(16, 22, 44), sky_mid=(72, 66, 104), sky_low=(228, 142, 84),
    sun=(255, 212, 146), ground=(72, 66, 60), wall=(206, 190, 160),
    roof=(92, 86, 88), accent=(255, 184, 0), haze=(198, 138, 100),
    lawn=(72, 92, 56), road=(38, 38, 42), haze_strength=0.80,
)
NOON = Palette(
    sky_top=(32, 82, 148), sky_mid=(102, 156, 204), sky_low=(198, 222, 238),
    sun=(255, 250, 230), ground=(104, 100, 90), wall=(228, 218, 192),
    roof=(122, 120, 120), accent=(255, 184, 0), haze=(182, 204, 222),
    lawn=(94, 124, 64), road=(52, 52, 56), haze_strength=0.70,
)
NIGHT = Palette(
    sky_top=(6, 9, 20), sky_mid=(14, 22, 44), sky_low=(40, 52, 86),
    sun=(150, 180, 230), ground=(20, 22, 26), wall=(58, 62, 76),
    roof=(30, 32, 40), accent=(255, 184, 0), haze=(26, 34, 56),
    lawn=(24, 34, 28), road=(16, 16, 20), haze_strength=1.10,
)

def sun_dir(v):
    a = np.array(v, dtype=np.float64)
    return a / np.linalg.norm(a)


# late-afternoon sun off to the east, low enough for long shadows: one visible facade is lit
# and the other falls into cool sky-light, which is what gives the massing its shape.
SUN_DIR = sun_dir([0.74, -0.34, 0.50])


def sky(w, h, pal, horizon=0.62, sun_xy=None):
    img = Image.new("RGB", (1, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        if t < horizon:
            u = t / horizon
            px[0, y] = lerp(pal.sky_top, pal.sky_mid, u ** 0.8)
        else:
            u = (t - horizon) / max(1e-6, 1 - horizon)
            px[0, y] = lerp(pal.sky_mid, pal.sky_low, u ** 0.55)
    img = img.resize((w, h), Image.BILINEAR)
    if sun_xy:
        glow = Image.new("L", (w, h), 0)
        g = ImageDraw.Draw(glow)
        cx, cy = int(sun_xy[0] * w), int(sun_xy[1] * h)
        for r, a in ((int(h * 0.75), 40), (int(h * 0.42), 70), (int(h * 0.16), 150), (int(h * 0.055), 255)):
            g.ellipse((cx - r, cy - r * 0.9, cx + r, cy + r * 0.9), fill=a)
        glow = glow.filter(ImageFilter.GaussianBlur(h * 0.05))
        img = Image.composite(Image.new("RGB", (w, h), pal.sun), img, glow)
    return img


# ---------------------------------------------------------------- camera

def view_basis(eye, target):
    fwd = np.array(target, dtype=np.float64) - np.array(eye, dtype=np.float64)
    fwd /= np.linalg.norm(fwd)
    world_up = np.array([0.0, 0.0, 1.0])
    right = np.cross(fwd, world_up)
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    return fwd, right, up


def raster(tris, cols, W, H, frame):
    """Z-buffered triangle fill. tris: (M,3,3) of (x_px, y_px, 1/z). Painter's order cannot
    cope with the long floor slabs punching through nearer walls, so we depth-test."""
    zbuf = np.zeros((H, W), dtype=np.float32)       # storing 1/z: bigger = nearer
    for t in range(len(tris)):
        tri = tris[t]
        x0 = int(max(0, math.floor(tri[:, 0].min())))
        x1 = int(min(W - 1, math.ceil(tri[:, 0].max())))
        y0 = int(max(0, math.floor(tri[:, 1].min())))
        y1 = int(min(H - 1, math.ceil(tri[:, 1].max())))
        if x1 < x0 or y1 < y0:
            continue
        ax, ay, aw = tri[0]
        bx, by, bw = tri[1]
        cx, cy, cw = tri[2]
        area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if abs(area) < 1e-6:
            continue
        xs = np.arange(x0, x1 + 1, dtype=np.float32) + 0.5
        ys = np.arange(y0, y1 + 1, dtype=np.float32) + 0.5
        px = xs[None, :]
        py = ys[:, None]
        w0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / area
        w1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) / area
        inside = (w0 >= 0) & (w1 >= 0) & ((1.0 - w0 - w1) >= 0)
        if not inside.any():
            continue
        # barycentric (u for c, v for a, rest b) -> interpolate 1/z, which is linear in screen space
        u = w0
        v = w1
        depth = v * aw + (1.0 - u - v) * bw + u * cw
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        win = inside & (depth > sub)
        if not win.any():
            continue
        sub[win] = depth[win]
        frame[y0:y1 + 1, x0:x1 + 1][win] = cols[t]
    return frame


def render(w, h, eye, target, hfov_deg, pal, horizon=0.62, sun_xy=None, fog_near=6000.0, fog_far=42000.0, sun=None):
    ss = 2
    W, H = w * ss, h * ss
    c, corners, size, ax, ay, az = BOXES
    eye = np.array(eye, dtype=np.float64)
    fwd, right, up = view_basis(eye, target)
    tan_h = math.tan(math.radians(hfov_deg) * 0.5)
    tan_v = tan_h * H / W

    sd = SUN_DIR if sun is None else sun_dir(sun)
    if sun_xy is None:
        # put the glow where the sun actually is, so the lit facades and the flare agree
        zs = float(sd @ fwd)
        if zs > 0.02:
            sun_xy = (float(sd @ right) / (zs * tan_h) * 0.5 + 0.5,
                      0.5 - float(sd @ up) / (zs * tan_v) * 0.5)
    img = sky(W, H, pal, horizon, sun_xy)

    d = corners - eye                               # (N,8,3)
    zc = d @ fwd
    xc = d @ right
    yc = d @ up
    near = 120.0
    safe = np.maximum(zc, near)
    sx = (xc / (safe * tan_h) * 0.5 + 0.5) * W
    sy = (0.5 - yc / (safe * tan_v) * 0.5) * H

    dc = c - eye
    depth = dc @ fwd
    visible = (zc > near).all(axis=1) & (depth > near)
    # screen-space bbox cull
    bx0, bx1 = sx.min(axis=1), sx.max(axis=1)
    by0, by1 = sy.min(axis=1), sy.max(axis=1)
    area = (bx1 - bx0) * (by1 - by0)
    on = (bx1 > -40) & (bx0 < W + 40) & (by1 > -40) & (by0 < H + 40)
    keep = np.where(visible & on & (area > 2.0 * ss * ss))[0]
    # near -> far: the z-buffer rejects most of the far geometry outright, so the big
    # foreground masses get laid down first and the 40k boxes cost far less fill.
    order = keep[np.argsort(depth[keep])]

    axes = np.stack([ax, ay, az], axis=1)           # (N,3,3)
    heights = c[:, 2]
    maxdim = size.max(axis=1)
    mindim = size.min(axis=1)

    wall = np.array(pal.wall, dtype=np.float64)
    roof = np.array(pal.roof, dtype=np.float64)
    ground = np.array(pal.ground, dtype=np.float64)
    hazec = np.array(pal.haze, dtype=np.float64)

    lawn = np.array(pal.lawn, dtype=np.float64)
    road = np.array(pal.road, dtype=np.float64)
    sunc = np.array(pal.sun, dtype=np.float64) / 255.0
    skyc = np.array(pal.sky_mid, dtype=np.float64) / 255.0 * 1.25

    inv_z = 1.0 / safe                              # (N,8) screen-linear depth key
    tris = []
    cols = []

    for i in order:
        hgt = heights[i]
        flat = mindim[i] < 60.0 and maxdim[i] > 300.0
        if hgt < 420.0 and flat:
            # ground zoning taken from the real campus layout (campus_map.json):
            # the quad lawn sits inside the wings, the highway runs along the south edge.
            px, py = c[i, 0], c[i, 1]
            if -4400 < px < 4400 and -10700 < py < -3100:
                base = lawn
            elif py < -13200:
                base = road
            else:
                base = ground
        elif mindim[i] < 25.0:
            base = wall * 0.82 + roof * 0.18        # railings, glazing bars
        else:
            base = wall
        # vertical tint: the upper storeys catch the sun, the plinth sits in its own shade
        base = base * (0.56 + 0.48 * min(max(hgt, 0.0) / 1500.0, 1.0))

        pts_x, pts_y, pts_w = sx[i], sy[i], inv_z[i]
        fog = float(np.clip((depth[i] - fog_near) / (fog_far - fog_near), 0.0, 1.0)) ** 0.85
        fog *= pal.haze_strength
        fog = min(fog, 0.92)

        for quad, axis_idx, sign in FACES:
            n = axes[i, axis_idx] * sign
            # back-face cull against the face centre
            fc = corners[i, quad].mean(axis=0)
            if float(n @ (fc - eye)) >= 0.0:
                continue
            # two lights: a warm sun and a cool sky dome. The colour split between a lit and an
            # unlit facade is what makes a flat-shaded massing read as key art instead of clay.
            sun_term = max(0.0, float(n @ sd)) ** 0.85
            sky_term = 0.30 + 0.34 * (float(n[2]) * 0.5 + 0.5)
            if n[2] < -0.7:
                sky_term *= 0.35                     # undersides see only bounce
            col = base * (sunc * sun_term * 1.15 + skyc * sky_term)
            col = col * (1.0 - fog) + hazec * fog
            rgb = np.clip(col, 0, 255).astype(np.uint8)
            v = np.stack([pts_x[list(quad)], pts_y[list(quad)], pts_w[list(quad)]], axis=1)
            tris.append(v[[0, 1, 2]])
            tris.append(v[[0, 2, 3]])
            cols.append(rgb)
            cols.append(rgb)

    frame = np.asarray(img, dtype=np.uint8).copy()
    if tris:
        raster(np.asarray(tris, dtype=np.float64), np.asarray(cols, dtype=np.uint8), W, H, frame)
    return Image.fromarray(frame).resize((w, h), Image.LANCZOS)


# ---------------------------------------------------------------- grading

def grade(img, pal, vignette=0.55, grain=5, bottom_fade=0.45):
    w, h = img.size
    # bottom darkening so the big italic map name always reads
    fade = Image.new("L", (1, h), 0)
    fp = fade.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        fp[0, y] = int(255 * (max(0.0, (t - (1.0 - bottom_fade)) / max(bottom_fade, 1e-6)) ** 1.25) * 0.94)
    fade = fade.resize((w, h), Image.BILINEAR)
    img = Image.composite(Image.new("RGB", (w, h), (4, 6, 10)), img, fade)

    if vignette > 0:
        vg = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(vg)
        d.ellipse((-w * 0.30, -h * 0.55, w * 1.30, h * 1.55), fill=255)
        vg = vg.filter(ImageFilter.GaussianBlur(min(w, h) * 0.18))
        vg = vg.point(lambda v: int(255 - (255 - v) * vignette))
        img = Image.composite(img, Image.new("RGB", (w, h), (0, 0, 0)), vg)

    if grain:
        n = (np.random.default_rng(7).normal(0, grain, (h, w, 1))).repeat(3, axis=2)
        arr = np.clip(np.asarray(img, dtype=np.float64) + n, 0, 255).astype(np.uint8)
        img = Image.fromarray(arr)
    return img


def save(img, name, colors=192):
    path = os.path.join(OUT, name)
    img.convert("RGB").quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG) \
        .save(path, optimize=True)
    print("%-28s %5dx%-4d %6.1f KB" % (name, img.size[0], img.size[1], os.path.getsize(path) / 1024.0))


# ---------------------------------------------------------------- locked plate

def locked_plate(w, h, seed, label_hint):
    """No art exists for an unreleased map, so we do not fake one: a dark blueprint plate
    with a soft massing silhouette. Honest, tiny, and obviously 'not yet'."""
    rng = np.random.default_rng(seed)
    img = Image.new("RGB", (w, h), (16, 21, 32))
    d = ImageDraw.Draw(img, "RGBA")
    for y in range(0, h, 22):
        d.line([(0, y), (w, y)], fill=(255, 255, 255, 16))
    for x in range(0, w, 22):
        d.line([(x, 0), (x, h)], fill=(255, 255, 255, 16))
    base = h - int(h * 0.18)
    x = -40
    while x < w + 40:
        bw = int(rng.integers(50, 150))
        bh = int(rng.integers(h * 0.16, h * 0.60))
        d.rectangle((x, base - bh, x + bw, base), fill=(38, 48, 68, 255))
        for wy in range(base - bh + 10, base - 10, 16):
            for wx in range(x + 10, x + bw - 8, 16):
                if rng.random() < 0.22:
                    d.rectangle((wx, wy, wx + 6, wy + 7), fill=(74, 92, 124, 255))
        x += bw + int(rng.integers(6, 26))
    d.rectangle((0, base, w, h), fill=(22, 27, 38, 255))
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    return grade(img, NIGHT, vignette=0.52, grain=0, bottom_fade=0.42)


# ---------------------------------------------------------------- main

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("loading geometry...")
    BOXES = load_boxes()
    print("%d boxes" % len(BOXES[0]))

    # 3:1 hero banner: elevated three-quarter from the south-east. The lit east facade, the
    # shadowed quad and the highway with its lamp posts are all genuinely in L_Match.
    print("campus banner...")
    campus = render(1024, 342,
                    eye=(23500, -29500, 12800), target=(-900, -8400, 1700), hfov_deg=45,
                    pal=DUSK, horizon=0.40, sun_xy=(0.07, 0.45), fog_near=24000, fog_far=105000)
    save(grade(campus, DUSK, vignette=0.46, grain=4, bottom_fade=0.52), "map_campus.png", 200)

    # mode key art, 16:5 strips. Same campus, different hour and angle, so the two cards never
    # read as the same picture twice.
    print("mode strips...")
    clash = render(640, 200,
                   eye=(13500, -23000, 4200), target=(-1800, -8800, 1100), hfov_deg=50,
                   pal=DUSK, horizon=0.46, sun_xy=(0.08, 0.50), fog_near=18000, fog_far=90000)
    save(grade(clash, DUSK, vignette=0.42, grain=4, bottom_fade=0.34), "mode_clash.png", 150)

    quick = render(640, 200,
                   eye=(-20000, -24000, 9000), target=(-200, -8400, 1500), hfov_deg=46,
                   pal=NOON, horizon=0.44, sun_xy=(0.90, 0.22), fog_near=24000, fog_far=110000)
    save(grade(quick, NOON, vignette=0.42, grain=4, bottom_fade=0.34), "mode_quick.png", 150)

    # No art exists for an unreleased map, so these are deliberately abstract blueprint plates,
    # not a faked screenshot. Half resolution: they are dark, soft and always dimmed behind a lock.
    print("locked plates...")
    save(locked_plate(512, 171, 11, "hostel"), "map_hostel.png", 24)
    save(locked_plate(512, 171, 29, "arena"), "map_arena.png", 24)
    save(locked_plate(320, 100, 47, "mode"), "mode_locked.png", 24)
