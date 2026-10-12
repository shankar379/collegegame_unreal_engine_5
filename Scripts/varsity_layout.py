"""
Varsity Valley - the whole 800 x 800 m battle-royale map as data, in Unreal centimetres.

This module is the ONE source of truth for the map's shape. It imports nothing (no unreal, no PIL):
build(emitter) walks the map and calls back into an emitter, so the same geometry feeds

    Scripts/build_varsity_valley.py   -> Geometry Script static meshes + box collision in L_VarsityValley
    Scripts/render_varsity_art.py     -> the map-select banner PNG

An emitter implements as much of this as it cares about (see MissingEmitter for the contract):

    box(zone, colour, centre, size, yaw=0.0, solid=False)   axis-box, yaw degrees about Z, solid = collides
    prism(zone, colour, centre, radius, height, sides, yaw=0.0, solid=False)
    cone(zone, colour, centre, radius, height, sides, yaw=0.0)      tip up, never collides
    dome(zone, top, horizon, radius)                                the sky
    campus(anchor)                                   the existing /Game/Campus meshes, instanced once
    lamp(location)                                   a street lamp's point light (night only)
    start(kind, location, yaw)                       a PlayerStart ("BR" / "Red" / "Blue")

Axes: Unreal's. +X east, +Y south, +Z up. The ground's top face is z = 0 everywhere except the hills.
"""

HALF = 40000.0            # playable half-width: 800 m x 800 m
GROUND_OVER = 1000.0      # the ground slab runs a little past the play space so no edge shows
ROAD_W = 1400.0           # 14 m carriageway
ROAD_RING = 26000.0       # the loop road sits on +/- this in x and y
ROAD_Z = 8.0              # roads/markings float a few cm over the grass (no z-fighting, no step)
MARK_Z = 14.0
LAMP_H = 700.0

# Quadrant centres inside the loop: the six zones.
ZONE_CENTRES = {
    "CollegeBlock": (-13000.0, -13000.0),
    "Hostels": (13000.0, -13000.0),
    "Sports": (-13000.0, 13000.0),
    "Market": (13000.0, 13000.0),
    "WaterTower": (4600.0, -4600.0),
    "Waiting": (0.0, -36000.0),
}

# Keep-out rectangles for the scatter pass: (min x, min y, max x, max y).
ZONE_KEEPOUT = [
    (-23000.0, -23000.0, -3000.0, -3000.0),   # college block
    (3000.0, -18000.0, 23000.0, -8000.0),     # hostel row
    (-24000.0, 4000.0, -2000.0, 22000.0),     # sports ground
    (3000.0, 3000.0, 23000.0, 23000.0),       # market street
    (1600.0, -7600.0, 7600.0, -1600.0),       # water tower
    (-5000.0, -40000.0, 5000.0, -30000.0),    # waiting area
]

COL = {
    "grass": "9cc06e",
    "grass_dark": "86ad5d",
    "dirt": "c7b48a",
    "road": "3a3d42",
    "road_edge": "4a4e54",
    "mark": "f2efe4",
    "wall": "f7f3e8",
    "wall_alt": "e9e2d2",
    "wall_warm": "efd9b4",
    "roof": "c2544a",
    "roof_alt": "8a6a52",
    "concrete": "cfcabb",
    "steel": "7c848a",
    "tank": "9aa7ad",
    "glass": "6fa8c4",
    "wood": "a8793f",
    "trunk": "6b4f32",
    "leaf": "4f8a3f",
    "leaf_alt": "3f7a4a",
    "rock": "9b9790",
    "fence": "b9b2a2",
    "track": "b5553f",
    "awning_a": "d9663f",
    "awning_b": "3f7fa8",
    "lamp": "5b6166",
    "sky_top": "4a86c8",
    "sky_low": "cfe3f2",
}


class MissingEmitter(object):
    """Default no-op emitter: an emitter only overrides what it can draw."""

    def box(self, zone, colour, centre, size, yaw=0.0, solid=False):
        pass

    def prism(self, zone, colour, centre, radius, height, sides, yaw=0.0, solid=False):
        pass

    def cone(self, zone, colour, centre, radius, height, sides, yaw=0.0):
        pass

    def dome(self, zone, top, horizon, radius):
        pass

    def campus(self, anchor):
        pass

    def lamp(self, location):
        pass

    def start(self, kind, location, yaw):
        pass


# ------------------------------------------------------------------------------------------ helpers
class Rng(object):
    """A fixed 32-bit LCG: the same scatter in the editor and in the banner, on every machine."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next(self):
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return self.state / 4294967296.0

    def range(self, lo, hi):
        return lo + (hi - lo) * self.next()

    def pick(self, items):
        return items[min(int(self.next() * len(items)), len(items) - 1)]


def on_road(x, y, pad=0.0):
    half = ROAD_W * 0.5 + pad
    if abs(x) <= half or abs(y) <= half:
        return True
    for ring in (-ROAD_RING, ROAD_RING):
        if abs(x - ring) <= half or abs(y - ring) <= half:
            return True
    # the spur down to the waiting area
    return abs(x) <= half and y <= -ROAD_RING


def in_keepout(x, y, pad=0.0):
    for x0, y0, x1, y1 in ZONE_KEEPOUT:
        if x0 - pad <= x <= x1 + pad and y0 - pad <= y <= y1 + pad:
            return True
    return False


def strip(e, zone, colour, x0, y0, x1, y1, z, thickness, solid=False):
    """A flat slab between two corners, its TOP face at z."""
    e.box(zone, colour, ((x0 + x1) * 0.5, (y0 + y1) * 0.5, z - thickness * 0.5),
          (abs(x1 - x0), abs(y1 - y0), thickness), solid=solid)


def stairs(e, zone, colour, base, yaw_axis, steps, rise, run, width, up_y=True):
    """A straight flight of solid steps from base (x, y, z) climbing in +/-Y (yaw_axis='y') or X."""
    bx, by, bz = base
    for i in range(steps):
        h = rise * (i + 1)
        if yaw_axis == "y":
            cy = by + (run * (i + 0.5) if up_y else -run * (i + 0.5))
            e.box(zone, colour, (bx, cy, bz + h * 0.5), (width, run, h), solid=True)
        else:
            cx = bx + (run * (i + 0.5) if up_y else -run * (i + 0.5))
            e.box(zone, colour, (cx, by, bz + h * 0.5), (run, width, h), solid=True)


def building(e, zone, wall, roof, centre, size, floor_z=0.0, wall_t=40.0, door="south",
             roof_t=50.0, solid_block=False, parapet=0.0):
    """One enterable box: four walls with a door gap, a floor slab and a flat roof you can stand on.

    solid_block=True falls back to a plain filled box (used where an interior would be wasted draw calls).
    """
    cx, cy = centre
    sx, sy, h = size
    if solid_block:
        e.box(zone, wall, (cx, cy, floor_z + h * 0.5), (sx, sy, h), solid=True)
        e.box(zone, roof, (cx, cy, floor_z + h + roof_t * 0.5), (sx + 60.0, sy + 60.0, roof_t), solid=True)
        return
    door_w, door_h = 240.0, 300.0
    # floor
    e.box(zone, COL["concrete"], (cx, cy, floor_z - 10.0), (sx, sy, 20.0), solid=True)
    for side in ("north", "south", "east", "west"):
        along_x = side in ("north", "south")
        length = sx if along_x else sy
        if along_x:
            wy = cy - sy * 0.5 + wall_t * 0.5 if side == "north" else cy + sy * 0.5 - wall_t * 0.5
            wall_centre, wall_size = (cx, wy), (length, wall_t)
        else:
            wx = cx - sx * 0.5 + wall_t * 0.5 if side == "west" else cx + sx * 0.5 - wall_t * 0.5
            wall_centre, wall_size = (wx, cy), (wall_t, length)
        if side != door:
            e.box(zone, wall, (wall_centre[0], wall_centre[1], floor_z + h * 0.5),
                  (wall_size[0], wall_size[1], h), solid=True)
            continue
        # the doorway: two jambs and a lintel
        side_len = (length - door_w) * 0.5
        for sign in (-1.0, 1.0):
            if along_x:
                e.box(zone, wall, (cx + sign * (door_w + side_len) * 0.5, wall_centre[1], floor_z + h * 0.5),
                      (side_len, wall_t, h), solid=True)
            else:
                e.box(zone, wall, (wall_centre[0], cy + sign * (door_w + side_len) * 0.5, floor_z + h * 0.5),
                      (wall_t, side_len, h), solid=True)
        lintel = h - door_h
        if lintel > 1.0:
            if along_x:
                e.box(zone, wall, (cx, wall_centre[1], floor_z + door_h + lintel * 0.5),
                      (door_w, wall_t, lintel), solid=True)
            else:
                e.box(zone, wall, (wall_centre[0], cy, floor_z + door_h + lintel * 0.5),
                      (wall_t, door_w, lintel), solid=True)
    e.box(zone, roof, (cx, cy, floor_z + h + roof_t * 0.5), (sx + 60.0, sy + 60.0, roof_t), solid=True)
    if parapet > 0.0:
        top = floor_z + h + roof_t
        for side in (-1.0, 1.0):
            e.box(zone, wall, (cx + side * sx * 0.5, cy, top + parapet * 0.5), (60.0, sy + 60.0, parapet), solid=True)
            e.box(zone, wall, (cx, cy + side * sy * 0.5, top + parapet * 0.5), (sx + 60.0, 60.0, parapet), solid=True)


def lamp_post(e, zone, x, y, arm_x):
    e.box(zone, COL["lamp"], (x, y, LAMP_H * 0.5), (50.0, 50.0, LAMP_H), solid=True)
    e.box(zone, COL["lamp"], (x + arm_x * 0.5, y, LAMP_H - 30.0), (abs(arm_x), 36.0, 36.0))
    e.box(zone, COL["mark"], (x + arm_x, y, LAMP_H - 70.0), (120.0, 120.0, 50.0))
    e.lamp((x + arm_x, y, LAMP_H - 110.0))


# ------------------------------------------------------------------------------------------ zones
def ground(e):
    span = (HALF + GROUND_OVER) * 2.0
    e.box("Ground", COL["grass"], (0.0, 0.0, -150.0), (span, span, 300.0), solid=True)
    # worn patches, flat on the grass: pure art, the slab under them is what the player stands on
    patches = [(-30000.0, 0.0, 9000.0, 5000.0), (30000.0, -8000.0, 7000.0, 11000.0),
               (0.0, 33000.0, 14000.0, 6000.0), (-6000.0, -20000.0, 5000.0, 8000.0)]
    for px, py, sx, sy in patches:
        e.box("Ground", COL["dirt"], (px, py, ROAD_Z - 4.0), (sx, sy, 10.0))


def hills(e):
    """Gentle terraced mounds outside the loop road. Stacked rotated boxes: the art IS the collision,
    so there is never a slope a player slides off that their eyes did not show them."""
    spots = [(-32000.0, -31000.0, 25.0), (32000.0, -30000.0, -15.0),
             (-31000.0, 31000.0, 40.0), (33000.0, 32000.0, 10.0), (0.0, 24000.0, 0.0)]
    for hx, hy, yaw in spots:
        for tier, (size, rise) in enumerate(((13000.0, 0.0), (8600.0, 420.0), (4800.0, 840.0))):
            colour = COL["grass_dark"] if tier < 2 else COL["dirt"]
            e.box("Hills", colour, (hx, hy, rise + 210.0), (size, size * 0.82, 420.0), yaw=yaw + tier * 12.0, solid=True)


def roads(e):
    def lane(x0, y0, x1, y1):
        strip(e, "Roads", COL["road"], x0, y0, x1, y1, ROAD_Z, 24.0)

    reach = ROAD_RING + ROAD_W * 0.5
    for ring in (-ROAD_RING, ROAD_RING):
        lane(-reach, ring - ROAD_W * 0.5, reach, ring + ROAD_W * 0.5)
        lane(ring - ROAD_W * 0.5, -reach, ring + ROAD_W * 0.5, reach)
    lane(-reach, -ROAD_W * 0.5, reach, ROAD_W * 0.5)
    lane(-ROAD_W * 0.5, -reach, ROAD_W * 0.5, reach)
    # spur to the waiting area at the south edge
    lane(-ROAD_W * 0.5, -HALF + 1500.0, ROAD_W * 0.5, -ROAD_RING)

    # dashed centre lines
    def dashes(x0, y0, x1, y1):
        length = max(abs(x1 - x0), abs(y1 - y0))
        horizontal = abs(x1 - x0) > abs(y1 - y0)
        steps = int(length // 1200.0)
        for i in range(steps):
            t = (i + 0.5) / steps
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            size = (420.0, 50.0, 10.0) if horizontal else (50.0, 420.0, 10.0)
            e.box("Roads", COL["mark"], (x, y, MARK_Z - 5.0), size)

    for ring in (-ROAD_RING, ROAD_RING):
        dashes(-ROAD_RING, ring, ROAD_RING, ring)
        dashes(ring, -ROAD_RING, ring, ROAD_RING)
    dashes(-ROAD_RING, 0.0, ROAD_RING, 0.0)
    dashes(0.0, -ROAD_RING, 0.0, ROAD_RING)

    # street lamps: only along the roads, and few (phones)
    for ring in (-ROAD_RING, ROAD_RING):
        for t in (-0.55, 0.0, 0.55):
            lamp_post(e, "Roads", ring * t, ring, 220.0 if ring < 0 else -220.0)
            lamp_post(e, "Roads", ring, ring * t, 220.0 if ring < 0 else -220.0)
    lamp_post(e, "Roads", 0.0, -8000.0, 240.0)
    lamp_post(e, "Roads", 0.0, 8000.0, -240.0)


def college(e):
    """The existing campus art, instanced once, plus its own ground apron and perimeter wall."""
    ax, ay = ZONE_CENTRES["CollegeBlock"]
    strip(e, "CollegeBlock", COL["concrete"], ax - 10500.0, ay - 10500.0, ax + 10500.0, ay + 10500.0, ROAD_Z, 20.0)
    for side in (-1.0, 1.0):
        e.box("CollegeBlock", COL["wall_alt"], (ax + side * 10400.0, ay, 180.0), (120.0, 20800.0, 360.0), solid=True)
        e.box("CollegeBlock", COL["wall_alt"], (ax, ay + side * 10400.0, 180.0), (20800.0, 120.0, 360.0), solid=True)
    # gate gap on the road side: knock out the middle of the north wall by overdrawing the grass road in
    e.box("CollegeBlock", COL["concrete"], (ax, ay + 10400.0, 180.0), (900.0, 200.0, 360.0))
    e.campus((ax, ay, 0.0))


def hostels(e):
    hx, hy = ZONE_CENTRES["Hostels"]
    for i in range(6):
        x = hx - 9000.0 + i * 3600.0
        building(e, "Hostels", COL["wall"] if i % 2 == 0 else COL["wall_warm"], COL["roof"],
                 (x, hy), (2600.0, 1500.0, 620.0), door="north", parapet=180.0)
        stairs(e, "Hostels", COL["concrete"], (x + 1500.0, hy - 650.0, 0.0), "y", 9, 75.0, 90.0, 300.0)
        e.box("Hostels", COL["glass"], (x, hy - 760.0, 380.0), (1800.0, 20.0, 160.0))
    strip(e, "Hostels", COL["concrete"], hx - 11000.0, hy + 900.0, hx + 11000.0, hy + 1800.0, ROAD_Z, 20.0)
    for i in range(4):
        e.box("Hostels", COL["wood"], (hx - 7000.0 + i * 4500.0, hy + 2600.0, 120.0), (900.0, 300.0, 240.0), solid=True)


def sports(e):
    sx, sy = ZONE_CENTRES["Sports"]
    strip(e, "Sports", COL["track"], sx - 10000.0, sy - 7500.0, sx + 10000.0, sy + 7500.0, ROAD_Z, 20.0)
    strip(e, "Sports", COL["grass_dark"], sx - 8600.0, sy - 6000.0, sx + 8600.0, sy + 6000.0, ROAD_Z + 2.0, 20.0)
    for side in (-1.0, 1.0):
        strip(e, "Sports", COL["mark"], sx + side * 8500.0 - 30.0, sy - 6000.0,
              sx + side * 8500.0 + 30.0, sy + 6000.0, MARK_Z, 10.0)
        # goal: two posts and a bar
        gx = sx + side * 8200.0
        for py in (sy - 350.0, sy + 350.0):
            e.box("Sports", COL["mark"], (gx, py, 130.0), (60.0, 60.0, 260.0), solid=True)
        e.box("Sports", COL["mark"], (gx, sy, 280.0), (60.0, 760.0, 40.0), solid=True)
    strip(e, "Sports", COL["mark"], sx - 8600.0, sy - 30.0, sx + 8600.0, sy + 30.0, MARK_Z, 10.0)
    # bleachers on the north side: four steps you can climb and shoot from
    for i in range(4):
        e.box("Sports", COL["concrete"], (sx, sy - 7800.0 - i * 260.0, 90.0 + i * 180.0),
              (12000.0, 260.0, 180.0 + i * 180.0), solid=True)
    e.box("Sports", COL["wall_alt"], (sx, sy - 9000.0, 420.0), (12200.0, 120.0, 840.0), solid=True)
    # floodlight masts at the corners (night lighting, four lamps only)
    for ox in (-1.0, 1.0):
        for oy in (-1.0, 1.0):
            mx, my = sx + ox * 9600.0, sy + oy * 7000.0
            e.box("Sports", COL["steel"], (mx, my, 700.0), (90.0, 90.0, 1400.0), solid=True)
            e.box("Sports", COL["mark"], (mx, my, 1460.0), (420.0, 160.0, 120.0))
            e.lamp((mx, my, 1400.0))


def market(e):
    mx, my = ZONE_CENTRES["Market"]
    strip(e, "Market", COL["road"], mx - 9000.0, my - 700.0, mx + 9000.0, my + 700.0, ROAD_Z, 24.0)
    for i in range(5):
        x = mx - 7200.0 + i * 3600.0
        for side, roof in ((-1.0, COL["roof"]), (1.0, COL["roof_alt"])):
            building(e, "Market", COL["wall"] if (i + int(side)) % 2 else COL["wall_warm"], roof,
                     (x, my + side * 2200.0), (1900.0, 1700.0, 440.0),
                     door="south" if side < 0 else "north")
            # awning over the shop front
            awn = COL["awning_a"] if i % 2 else COL["awning_b"]
            e.box("Market", awn, (x, my + side * 1150.0, 420.0), (1900.0, 700.0, 40.0))
            for ox in (-850.0, 850.0):
                e.box("Market", COL["steel"], (x + ox, my + side * 850.0, 210.0), (40.0, 40.0, 420.0), solid=True)
    # stalls and barrels down the middle of the street
    rng = Rng(7717)
    for i in range(10):
        x = mx - 7500.0 + i * 1650.0
        y = my + rng.range(-420.0, 420.0)
        e.box("Market", COL["wood"], (x, y, 110.0), (560.0, 420.0, 220.0), yaw=rng.range(-20.0, 20.0), solid=True)
        e.box("Market", COL["awning_a"] if i % 2 else COL["awning_b"], (x, y, 250.0), (700.0, 560.0, 30.0))
    lamp_post(e, "Market", mx - 4000.0, my + 900.0, 0.0)
    lamp_post(e, "Market", mx + 4000.0, my - 900.0, 0.0)


def water_tower(e):
    tx, ty = ZONE_CENTRES["WaterTower"]
    legs = 760.0
    for ox in (-legs, legs):
        for oy in (-legs, legs):
            e.box("WaterTower", COL["steel"], (tx + ox, ty + oy, 900.0), (150.0, 150.0, 1800.0), solid=True)
    for z in (650.0, 1300.0):
        for offset in (-legs, legs):
            e.box("WaterTower", COL["steel"], (tx + offset, ty, z), (60.0, legs * 2.0, 60.0))
            e.box("WaterTower", COL["steel"], (tx, ty + offset, z), (legs * 2.0, 60.0, 60.0))
    e.box("WaterTower", COL["concrete"], (tx, ty, 1830.0), (2000.0, 2000.0, 60.0), solid=True)
    e.prism("WaterTower", COL["tank"], (tx, ty, 1860.0), 900.0, 820.0, 10, solid=True)
    e.cone("WaterTower", COL["steel"], (tx, ty, 2680.0), 940.0, 260.0, 10)
    # service ladder as climbable steps up one leg
    stairs(e, "WaterTower", COL["steel"], (tx - legs - 260.0, ty - legs, 0.0), "y", 18, 100.0, 95.0, 260.0)


def waiting(e):
    wx, wy = ZONE_CENTRES["Waiting"]
    deck_z = 3000.0
    for ox in (-2000.0, 0.0, 2000.0):
        for oy in (-1300.0, 1300.0):
            e.box("Waiting", COL["concrete"], (wx + ox, wy + oy, deck_z * 0.5), (300.0, 300.0, deck_z), solid=True)
    e.box("Waiting", COL["concrete"], (wx, wy, deck_z - 60.0), (5000.0, 3200.0, 120.0), solid=True)
    # fence all the way round: nobody wanders off a platform 30 m up
    for side in (-1.0, 1.0):
        e.box("Waiting", COL["fence"], (wx + side * 2470.0, wy, deck_z + 170.0), (60.0, 3200.0, 340.0), solid=True)
        e.box("Waiting", COL["fence"], (wx, wy + side * 1570.0, deck_z + 170.0), (5000.0, 60.0, 340.0), solid=True)
    e.box("Waiting", COL["awning_b"], (wx, wy, deck_z + 700.0), (5200.0, 3400.0, 60.0))
    for ox in (-2400.0, 2400.0):
        for oy in (-1500.0, 1500.0):
            e.box("Waiting", COL["steel"], (wx + ox, wy + oy, deck_z + 350.0), (70.0, 70.0, 700.0))
    e.lamp((wx, wy, deck_z + 600.0))
    # 24 BR starts in a 6 x 4 grid on the deck
    for ix in range(6):
        for iy in range(4):
            e.start("BR", (wx - 2000.0 + ix * 800.0, wy - 1050.0 + iy * 700.0, deck_z + 20.0), 90.0)
    # TDM: two eight-man pads on opposite sides of the loop
    for kind, (px, py, yaw) in (("Red", (-20000.0, -30000.0, 45.0)), ("Blue", (20000.0, 30000.0, -135.0))):
        strip(e, "Waiting", COL["concrete"], px - 900.0, py - 900.0, px + 900.0, py + 900.0, ROAD_Z + 4.0, 20.0)
        for i in range(8):
            e.start(kind, (px - 700.0 + (i % 4) * 470.0, py - 300.0 + (i // 4) * 600.0, 20.0), yaw)


def scatter(e):
    """Cover: trees, rocks and crates, placed off the roads and out of the zones. Canopies never
    collide (you fight through them); trunks, rocks and crates do."""
    rng = Rng(20261012)
    trees = rocks = crates = 0
    for _ in range(4200):
        if trees >= 120 and rocks >= 60 and crates >= 70:
            break
        x = rng.range(-HALF + 2000.0, HALF - 2000.0)
        y = rng.range(-HALF + 2000.0, HALF - 2000.0)
        if on_road(x, y, 700.0) or in_keepout(x, y, 600.0):
            continue
        roll = rng.next()
        if roll < 0.52 and trees < 120:
            h = rng.range(260.0, 420.0)
            e.prism("Scatter", COL["trunk"], (x, y, 0.0), rng.range(40.0, 60.0), h, 6, solid=True)
            leaf = COL["leaf"] if rng.next() < 0.6 else COL["leaf_alt"]
            e.cone("Scatter", leaf, (x, y, h * 0.75), rng.range(260.0, 400.0), rng.range(420.0, 620.0), 7)
            trees += 1
        elif roll < 0.76 and rocks < 60:
            s = rng.range(160.0, 320.0)
            e.box("Scatter", COL["rock"], (x, y, s * 0.35), (s, s * rng.range(0.7, 1.3), s * 0.7),
                  yaw=rng.range(0.0, 90.0), solid=True)
            rocks += 1
        elif crates < 70:
            s = 120.0
            yaw = rng.range(0.0, 90.0)
            e.box("Scatter", COL["wood"], (x, y, s * 0.5), (s, s, s), yaw=yaw, solid=True)
            if rng.next() < 0.45:
                e.box("Scatter", COL["wood"], (x + 20.0, y - 15.0, s * 1.5), (s, s, s), yaw=yaw + 12.0, solid=True)
            crates += 1


def sky(e):
    e.dome("Sky", COL["sky_top"], COL["sky_low"], 56000.0)


def build(e):
    ground(e)
    hills(e)
    roads(e)
    college(e)
    hostels(e)
    sports(e)
    market(e)
    water_tower(e)
    waiting(e)
    scatter(e)
    sky(e)
