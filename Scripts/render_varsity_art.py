"""
Renders the map-select banner for Varsity Valley straight from the map's own layout - no engine, no
screenshot, so the picture cannot drift from the level.

    python Scripts/render_varsity_art.py Content/CampusUI/MapArt

Same idea as Scripts/render_map_art.py (painter's algorithm, flat shading, haze), but it reads
Scripts/varsity_layout.py instead of the exported three.js boxes, which is where that map's geometry is
defined. Output: map_valley.png, the name CampusCatalog's VarsityValley row asks for.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import varsity_layout as L  # noqa: E402

W, H = 1024, 576
YAW = math.radians(38.0)       # looking across the valley from the south-west
TILT = 0.52                    # how far the ground is laid down
ZS = 0.95                      # vertical exaggeration, so the water tower reads
FOG_NEAR, FOG_FAR = 0.15, 1.0  # fraction of the depth span where haze starts / is full
SHADE = {"top": 1.0, "x": 0.80, "y": 0.63}


def to_rgb(value):
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def shade(rgb, factor):
    return tuple(max(0, min(255, int(channel * factor))) for channel in rgb)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


class Painter(L.MissingEmitter):
    """Collects every face as (depth, screen polygon, colour) and paints them back to front."""

    def __init__(self):
        self.faces = []
        self.sky = (to_rgb(L.COL["sky_top"]), to_rgb(L.COL["sky_low"]))

    # ---- projection
    def project(self, point):
        x, y, z = point
        rx = x * math.cos(YAW) - y * math.sin(YAW)
        ry = x * math.sin(YAW) + y * math.cos(YAW)
        return rx, -ry * TILT - z * ZS, ry

    def face(self, corners, rgb, key):
        screen = [self.project(c) for c in corners]
        depth = sum(s[2] for s in screen) / len(screen)
        self.faces.append((depth, [(s[0], s[1]) for s in screen], shade(rgb, SHADE[key])))

    # ---- primitives
    def box(self, zone, colour, centre, size, yaw=0.0, solid=False):
        rgb = to_rgb(colour)
        hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
        angle = math.radians(yaw)
        cos, sin = math.cos(angle), math.sin(angle)

        def corner(sx, sy, sz):
            lx, ly = sx * hx, sy * hy
            return (centre[0] + lx * cos - ly * sin, centre[1] + lx * sin + ly * cos, centre[2] + sz * hz)

        self.face([corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1)], rgb, "top")
        self.face([corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1), corner(-1, -1, 1)], rgb, "y")
        self.face([corner(1, -1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(1, -1, 1)], rgb, "x")
        self.face([corner(-1, 1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(-1, 1, 1)], rgb, "y")
        self.face([corner(-1, -1, -1), corner(-1, 1, -1), corner(-1, 1, 1), corner(-1, -1, 1)], rgb, "x")

    def prism(self, zone, colour, centre, radius, height, sides, yaw=0.0, solid=False):
        rgb = to_rgb(colour)
        base, top = centre[2], centre[2] + height
        ring = [(centre[0] + radius * math.cos(math.radians(yaw) + 2.0 * math.pi * i / sides),
                 centre[1] + radius * math.sin(math.radians(yaw) + 2.0 * math.pi * i / sides)) for i in range(sides)]
        for i in range(sides):
            x0, y0 = ring[i]
            x1, y1 = ring[(i + 1) % sides]
            self.face([(x0, y0, base), (x1, y1, base), (x1, y1, top), (x0, y0, top)], rgb, "y")
        self.face([(x, y, top) for x, y in ring], rgb, "top")

    def cone(self, zone, colour, centre, radius, height, sides, yaw=0.0):
        rgb = to_rgb(colour)
        tip = (centre[0], centre[1], centre[2] + height)
        ring = [(centre[0] + radius * math.cos(math.radians(yaw) + 2.0 * math.pi * i / sides),
                 centre[1] + radius * math.sin(math.radians(yaw) + 2.0 * math.pi * i / sides),
                 centre[2]) for i in range(sides)]
        for i in range(sides):
            self.face([ring[i], ring[(i + 1) % sides], tip], rgb, "top" if i % 2 else "x")

    def campus(self, anchor):
        """The college block is existing art this renderer does not own: a believable silhouette stands in."""
        ax, ay, az = anchor
        for ox, oy, sx, sy, h in ((0.0, -3200.0, 9000.0, 1800.0, 1400.0), (-3600.0, 600.0, 1800.0, 7000.0, 1400.0),
                                  (3600.0, 600.0, 1800.0, 7000.0, 1400.0), (0.0, 3600.0, 6000.0, 1600.0, 900.0)):
            self.box("CollegeBlock", L.COL["wall"], (ax + ox, ay + oy, az + h * 0.5), (sx, sy, h))
            self.box("CollegeBlock", L.COL["roof"], (ax + ox, ay + oy, az + h + 60.0), (sx + 80.0, sy + 80.0, 120.0))


def render():
    painter = Painter()
    L.build(painter)

    xs = [p[0] for _, poly in ((f[0], f[1]) for f in painter.faces) for p in poly]
    ys = [p[1] for _, poly in ((f[0], f[1]) for f in painter.faces) for p in poly]
    # frame the play space, not the sky dome
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    # cropped in past the empty outfield: the banner should sell the built-up middle, not the border
    scale = min(W / (x1 - x0), H / (y1 - y0)) * 1.95
    cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5 + 2600.0

    image = Image.new("RGB", (W, H))
    draw = ImageDraw.Draw(image)
    top, low = painter.sky
    horizon = int(H * 0.40)
    for row in range(H):
        t = min(1.0, row / float(max(horizon, 1)))
        draw.line([(0, row), (W, row)], fill=mix(top, low, t) if row <= horizon else low)

    depths = [f[0] for f in painter.faces]
    near, far = min(depths), max(depths)
    span = max(far - near, 1.0)
    haze = mix(low, (255, 255, 255), 0.25)
    for depth, poly, rgb in sorted(painter.faces, key=lambda f: -f[0]):
        t = (depth - near) / span
        fade = max(0.0, min(1.0, (t - FOG_NEAR) / (FOG_FAR - FOG_NEAR))) * 0.72
        points = [((p[0] - cx) * scale + W * 0.5, (p[1] - cy) * scale + H * 0.52) for p in poly]
        if max(p[0] for p in points) < -40 or min(p[0] for p in points) > W + 40:
            continue
        if max(p[1] for p in points) < -40 or min(p[1] for p in points) > H + 40:
            continue
        draw.polygon(points, fill=mix(rgb, haze, fade))

    # a touch of bloom and a vignette, like the other banners
    image = Image.blend(image, image.filter(ImageFilter.GaussianBlur(2.2)), 0.18)
    vignette = Image.new("L", (W, H), 0)
    ImageDraw.Draw(vignette).ellipse((-W * 0.25, -H * 0.45, W * 1.25, H * 1.45), fill=255)
    vignette = vignette.filter(ImageFilter.GaussianBlur(90))
    image = Image.composite(image, Image.new("RGB", (W, H), (12, 16, 26)), vignette)
    return image


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "map_valley.png")
    render().convert("P", palette=Image.ADAPTIVE, colors=192).save(path, optimize=True)
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))
