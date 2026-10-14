"""
Varsity Valley's IN-GAME MAP picture: a square top-down plan of L_VarsityValley that covers EXACTLY
the CampusBRMap square, so the minimap / full map and the safe circle line up 1:1 with the world.

    python Scripts/render_varsity_map.py Content/CampusUI/MapArt

Output: minimap_valley.png - a loose PNG under Content/CampusUI/MapArt, which DefaultGame.ini already
stages (+DirectoriesToAlwaysStageAsUFS=(Path="CampusUI/MapArt")) and Campus/CampusMapArt.cpp already
decodes at runtime through the pak-aware file system. No editor, no cook, no new .uasset: it ships in
an Android Shipping build the moment it exists, exactly like the map-select banners beside it.

THE SQUARE, AND WHY IT IS NOT THE ONE render_varsity_art.py USES
    CampusBRMap::For(World) (Source/CollegeCampusGame/Campus/CampusBRZone.cpp) gives Varsity Valley
    as { middle (0,0), span 80000 } = varsity_layout.HALF * 2, and CampusBRMap::ToUV maps a world
    point onto it as

        u = (x + HALF) / (2 * HALF)        v = (y + HALF) / (2 * HALF)

    so this image's LEFT edge is x = -HALF, its TOP edge is y = -HALF, pixel columns run with +X and
    pixel rows run with +Y. varsity_layout's axes are Unreal's (+X east, +Y south), so -Y is north
    and NORTH IS UP - the same orientation the college picture already uses. The ground slab runs
    GROUND_OVER past the play space; that overspill is deliberately CROPPED OFF, because the picture
    has to be the UV square and nothing else.

Sibling of Scripts/render_varsity_art.py (the 3/4-view map-select banner). Both read
Scripts/varsity_layout.py, the one source of truth for the map geometry, so neither picture can
drift from the level the editor script builds from the same data.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import varsity_layout as L  # noqa: E402

# 1024 ships; rendered at SS x that and filtered down, which is what gives the roads clean edges at
# the ~330 px the minimap actually draws it. 2048 is a 4 MB PNG for detail no phone screen resolves.
PX = 1024
SS = 3
GRID = PX * SS

# Anything whose top face is above this (cm) is a STRUCTURE: it gets a cast shadow and roof shading.
# Below it is ground dressing - roads, markings, pitches, aprons - which must stay dead flat.
FLAT_Z = 120.0
# Sun from the north-west, so shadows fall south-east (down-right on the picture).
SUN = (0.62, 0.78)
SHADOW_PER_CM = 1.0           # cm of shadow per cm of height: a 45 deg sun, so a roof reads as tall
SHADOW_INK = (26, 34, 24)
SHADOW_ALPHA = 104
# Roof brightness ramp: a 6 m hostel and a 30 m water tower must not read as the same slab.
ROOF_LIFT = 0.14              # extra brightness at the ramp top
ROOF_FULL_Z = 2600.0
EDGE_DARK = 0.72              # the thin outline that separates two touching roofs


def to_rgb(value, factor=1.0):
    return tuple(max(0, min(255, int(int(value[i:i + 2], 16) * factor))) for i in (0, 2, 4))


def project(x, y):
    """World cm -> pixel, on EXACTLY the CampusBRMap square (CampusBRMap::ToUV, mirrored)."""
    return ((x + L.HALF) / (2.0 * L.HALF) * GRID, (y + L.HALF) / (2.0 * L.HALF) * GRID)


class Plan(L.MissingEmitter):
    """Collects the footprint of every piece as (top z, polygon, colour, height) and paints them
    bottom-up, the way a painter's algorithm reads a plan view."""

    def __init__(self):
        self.pieces = []

    def add(self, top, points, colour, height):
        # The base ground slab (varsity_layout.ground, top face z = 0) is NOT collected: the image's
        # own mottled grass is that slab. Collected, it would be painted over the mottle and over
        # every cast shadow, which is exactly what flattened the first render.
        if top > 0.0:
            self.pieces.append((top, points, colour, height))

    def box(self, zone, colour, centre, size, yaw=0.0, solid=False):
        cx, cy, cz = centre
        sx, sy, sz = size
        angle = math.radians(yaw)
        cos, sin = math.cos(angle), math.sin(angle)
        corners = ((-sx * 0.5, -sy * 0.5), (sx * 0.5, -sy * 0.5), (sx * 0.5, sy * 0.5), (-sx * 0.5, sy * 0.5))
        self.add(cz + sz * 0.5,
                 [project(cx + dx * cos - dy * sin, cy + dx * sin + dy * cos) for dx, dy in corners],
                 colour, sz)

    def prism(self, zone, colour, centre, radius, height, sides, yaw=0.0, solid=False):
        cx, cy, cz = centre
        start = math.radians(yaw)
        self.add(cz + height,
                 [project(cx + radius * math.cos(start + 2.0 * math.pi * i / sides),
                          cy + radius * math.sin(start + 2.0 * math.pi * i / sides)) for i in range(sides)],
                 colour, height)

    def cone(self, zone, colour, centre, radius, height, sides, yaw=0.0):
        # Seen from overhead a cone is its base disc; the tip only decides how bright it is.
        self.prism(zone, colour, centre, radius, height, max(sides, 10), yaw=yaw)

    def campus(self, anchor):
        """The college block is existing /Game/Campus art this renderer does not own, so it stands in
        with the same silhouette Scripts/render_varsity_art.py uses - one shape, two pictures."""
        ax, ay, az = anchor
        for ox, oy, sx, sy, h in ((0.0, -3200.0, 9000.0, 1800.0, 1400.0), (-3600.0, 600.0, 1800.0, 7000.0, 1400.0),
                                  (3600.0, 600.0, 1800.0, 7000.0, 1400.0), (0.0, 3600.0, 6000.0, 1600.0, 900.0)):
            self.box("CollegeBlock", L.COL["roof"], (ax + ox, ay + oy, az + h + 60.0), (sx + 80.0, sy + 80.0, 120.0))


def grass_weave():
    """A faint mottle over the grass. Deterministic (varsity_layout own LCG), built tiny and blown
    up, so the field is not a flat slab of one green without costing a texture read per pixel."""
    small = 96
    rng = L.Rng(90210)
    noise = Image.new("L", (small, small))
    noise.putdata([int(128 + 118 * (rng.next() - 0.5)) for _ in range(small * small)])
    return noise.resize((GRID, GRID), Image.BICUBIC).filter(ImageFilter.GaussianBlur(GRID / 420.0))


def render():
    plan = Plan()
    L.build(plan)

    # Grass first, mottled: this IS the ground slab (see Plan.add).
    image = Image.composite(Image.new("RGB", (GRID, GRID), to_rgb(L.COL["grass"], 1.10)),
                            Image.new("RGB", (GRID, GRID), to_rgb(L.COL["grass"], 0.90)), grass_weave())

    order = sorted(plan.pieces, key=lambda piece: piece[0])
    flat = [piece for piece in order if piece[0] <= FLAT_Z]
    built = [piece for piece in order if piece[0] > FLAT_Z]

    # 1. Ground dressing - roads, markings, pitches, aprons, dirt. Dead flat, no shading, no outline:
    #    a road is painted ON the map, it is not an object standing on it.
    pen = ImageDraw.Draw(image)
    for top, points, colour, height in flat:
        pen.polygon(points, fill=to_rgb(colour))

    # 2. ONE shadow pass for everything that stands up, so two buildings never double-darken each
    #    other and every shadow falls on the roads as well as on the grass.
    shadows = Image.new("L", (GRID, GRID), 0)
    shadow_pen = ImageDraw.Draw(shadows)
    scale = GRID / (2.0 * L.HALF)
    for top, points, colour, height in built:
        reach = top * SHADOW_PER_CM * scale
        shadow_pen.polygon([(px + SUN[0] * reach, py + SUN[1] * reach) for px, py in points], fill=SHADOW_ALPHA)
    shadows = shadows.filter(ImageFilter.GaussianBlur(GRID / 900.0))
    image = Image.composite(Image.new("RGB", (GRID, GRID), SHADOW_INK), image, shadows)

    # 3. The roofs themselves, bottom-up, brighter the taller they are.
    pen = ImageDraw.Draw(image)
    for top, points, colour, height in built:
        lift = 1.0 + ROOF_LIFT * min(1.0, top / ROOF_FULL_Z)
        pen.polygon(points, fill=to_rgb(colour, lift), outline=to_rgb(colour, EDGE_DARK))

    # The playable edge, so a player reading the map can see where the world stops.
    inset = GRID / 256.0
    ImageDraw.Draw(image).rectangle((inset, inset, GRID - inset, GRID - inset),
                                    outline=to_rgb(L.COL["mark"], 0.8), width=max(1, int(GRID / 420.0)))

    # Down to the shipped size: the supersample IS the anti-aliasing (PIL draws hard-edged polygons).
    image = image.resize((PX, PX), Image.LANCZOS)
    # Taken down a stop. The map is a BACKGROUND: the blue safe circle, the white next circle and the
    # yellow player arrow are drawn on top of it and have to win, which they do not against full-
    # strength daylight greens.
    return Image.blend(image, Image.new("RGB", (PX, PX), (28, 38, 46)), 0.12)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "minimap_valley.png")
    # 224 colours, like the map-select banners: this is flat-shaded art with no gradients to band, and
    # a paletted PNG is a third of the size of the truecolour one for a picture no phone can tell apart.
    render().convert("P", palette=Image.ADAPTIVE, colors=224).save(path, optimize=True)
    print("wrote %s (%d bytes, %d px, exactly %.0f m square)"
          % (path, os.path.getsize(path), PX, 2.0 * L.HALF / 100.0))
