"""Weapon Library V3 - Free Fire style rifles (registered into wv3_guns.GUNS).
Layouts are measured from the reference pictures; see compare/silhouette_match.py for the score."""
import math
from mathutils import Matrix
from wv3_kit import (path, rrect, band, arc, extrude, box, lathe, tube, cbox, ccyl, cprism, mirror_x, PARTS)
from wv3_guns import gun


def shift_all(dy, dz=0.0):
    """Move the whole gun so the grip lands on the origin after laying it out in reference coordinates."""
    for o, kind, skin in PARTS:
        o.data.transform(Matrix.Translation((0, dy, dz)))


@gun
def m4a1():
    """Free Fire M4A1: black carbine, A2 carry handle, forward triangle front sight, long ribbed round
    handguard, tall collapsible stock, grip set well back, short chunky 30-round magazine.
    Laid out with the grip 5 cm behind the origin (reference proportions), then shifted +5."""
    BZ = 6.1
    # lower receiver with flared magazine well
    low = path(('M', -9.5, 4.4), ('L', 14.6, 4.4), ('L', 14.6, 2.6), ('L', 12.0, 1.8), ('L', 11.8, -1.8), ('L', 2.8, -1.8),
               ('L', 2.8, 1.0), ('L', -3.4, 1.0), ('L', -6.2, 1.5), ('L', -8.4, 2.0), ('Q', -9.4, 2.5, -9.5, 4.4))
    extrude('Lower receiver', low, 2.5, 'alu', bevel=.12, cuts=[cbox((1.4, 7.3, -.4), (.5, 6.4, 1.6)), cbox((-1.4, 7.3, -.4), (.5, 6.4, 1.6))])
    extrude('Magwell flare', path(('M', 2.6, -1.0), ('L', 12.0, -1.0), ('L', 12.2, -2.3), ('L', 2.4, -2.3)), 3.0, 'alu', bevel=.12)
    lathe('Mag release', [(0, .45), (.25, .45), (.35, .3)], 'steel', at=(1.25, 2.0, 2.7), axis='X', seg=12, bevel=0)
    for y, z in ((-8.0, 3.6), (12.6, 3.6), (-1.6, 2.9), (.6, 2.9)):
        lathe('Receiver pin', [(0, .32), (.12, .32)], 'steel', at=(1.25, y, z), axis='X', seg=10, bevel=0)
        lathe('Receiver pin', [(0, .32), (-.12, .32)], 'steel', at=(-1.25, y, z), axis='X', seg=10, bevel=0)
    extrude('Selector', path(('M', -6.6, 3.4), ('L', -4.2, 3.0), ('L', -4.2, 3.6), ('L', -6.6, 3.9)), .25, 'steel', at=-1.38, bevel=.03)
    tg = path(('M', -2.9, 1.1), ('L', -2.9, -.5), ('Q', -2.8, -.9, -2.2, -.9), ('L', 2.3, -.9), ('L', 2.5, 1.1), n=6)
    extrude('Trigger guard', band(tg, .45), 1.4, 'alu', bevel=.08)
    extrude('Trigger', path(('M', -1.2, 1.2), ('L', -.5, 1.2), ('C', -.5, -.2, -.8, -.9, -1.5, -1.2), ('L', -1.6, -.9), ('C', -1.1, -.4, -1.1, .4, -1.2, 1.2)), .45, 'steel', bevel=.03)
    # A2 pistol grip with finger nub (5 cm behind origin before the final shift)
    gp = path(('M', -3.4, 1.1), ('L', -6.6, 1.5), ('C', -7.9, -2.2, -9.0, -6.0, -9.8, -9.2), ('Q', -10.0, -10.4, -8.8, -10.4),
              ('L', -6.6, -10.0), ('Q', -5.6, -9.7, -5.6, -8.6), ('L', -5.3, -6.2), ('Q', -4.5, -5.3, -5.1, -4.5), ('C', -4.5, -2.6, -4.0, -.8, -3.4, 1.1))
    extrude('Grip', gp, 3.0, 'polymer', crown=.3, inset=.45, bevel=.12)
    # upper receiver: flat sides, chamfered top, ejection port + dust cover, forward assist
    up = [(-1.25, 4.35), (1.25, 4.35), (1.25, 7.1), (.95, 7.75), (-.95, 7.75), (-1.25, 7.1)]
    extrude('Upper receiver', up, 24.1, 'alu', plane='XZ', at=2.55, bevel=.1, cuts=[cbox((1.3, 4.6, 6.0), (.5, 7.2, 1.5))])
    extrude('Bolt', rrect(1.2, 5.45, 8.0, 6.6, .2), 1.6, 'steel_bright', at=.45, skin=False, bevel=.05)
    extrude('Dust cover', rrect(1.0, 4.65, 8.2, 5.35, .15), .2, 'alu', at=1.36, bevel=.03)
    extrude('Brass deflector', path(('M', -.6, 5.6), ('L', .9, 5.6), ('L', .9, 7.4), ('L', .2, 7.4)), .9, 'alu', at=1.55, bevel=.08)
    lathe('Forward assist', [(0, .75), (2.2, .75), (2.4, .62), (2.6, .62)], 'alu', at=(1.55, -8.2, 6.6), seg=16, bevel=.04)
    extrude('Charging handle', path(('M', -10.7, 7.0), ('L', -9.3, 7.0), ('L', -9.3, 7.7), ('L', -10.7, 7.7), ('L', -11.2, 7.9), ('L', -11.2, 6.8)), 3.2, 'alu', bevel=.08)
    # A2 carry handle with rear sight
    ch = path(('M', -8.3, 7.7), ('L', 10.4, 7.7), ('L', 10.4, 8.6), ('L', 9.4, 11.0), ('L', -5.7, 11.0), ('L', -5.9, 12.6), ('L', -8.3, 12.6))
    win = cprism(path(('M', -4.9, 8.7), ('L', 8.4, 8.7), ('L', 7.9, 10.2), ('L', -4.9, 10.2)), 4, 'YZ', 0)
    extrude('Carry handle', ch, 1.9, 'alu', bevel=.12, cuts=[win])
    lathe('Windage knob', [(0, .7), (.45, .7), (.5, .55)], 'alu', at=(.95, -7.1, 11.6), axis='X', seg=16, bevel=.03)
    lathe('Elevation drum', [(-1.1, .9), (1.1, .9)], 'alu', at=(0, -6.9, 11.1), axis='X', seg=18, bevel=.05)
    # buffer tube + tall collapsible stock
    lathe('Buffer tube', [(0, 1.45), (.6, 1.55), (16.0, 1.55), (16.4, 1.3)], 'alu', at=(0, -26.0, BZ), seg=20, bevel=.04)
    lathe('Castle nut', [(0, 1.85), (.8, 1.85)], 'steel', at=(0, -10.5, BZ), seg=12, bevel=.04)
    stk = path(('M', -12.6, 8.0), ('L', -30.6, 8.9), ('L', -32.3, 8.9), ('L', -32.4, -3.8), ('L', -30.8, -4.2), ('Q', -28.4, -2.4, -26.6, -.8),
               ('L', -19.4, 3.9), ('L', -12.6, 4.2))
    extrude('Stock', stk, 3.6, 'polymer', crown=.25, inset=.4, bevel=.14,
            cuts=[cprism(path(('M', -29.0, 4.6), ('L', -21.6, 4.8), ('L', -25.6, 1.4), ('L', -29.0, .2)), 6, 'YZ', 0)])
    extrude('Butt pad', path(('M', -32.3, 9.0), ('L', -33.4, 9.0), ('Q', -33.7, 2.6, -33.4, -3.9), ('L', -32.4, -3.8)), 3.8, 'rubber', bevel=.12)
    extrude('Stock lever', path(('M', -21.0, 4.4), ('L', -17.6, 4.3), ('L', -17.6, 3.7), ('L', -21.0, 3.4)), 1.0, 'polymer', bevel=.06)
    # delta ring, long ribbed round handguard, cap
    lathe('Delta ring', [(0, 1.7), (.3, 2.4), (1.1, 2.4), (1.3, 1.9)], 'alu', at=(0, 14.4, BZ), seg=24, bevel=.04)
    rib = []
    for i in range(48):
        a = 2 * math.pi * i / 48
        r = 2.15 if (i // 2) % 2 == 0 else 2.0
        rib.append((r * math.cos(a), BZ + r * math.sin(a)))
    holes = [ccyl((0, 17.4 + j * 2.2, BZ + 1.3), .4, 6, 'X', 10) for j in range(7)]
    extrude('Handguard', rib, 18.2, 'polymer', plane='XZ', at=24.8, bevel=.06, cuts=holes)
    lathe('Handguard cap', [(0, 2.15), (.6, 2.0), (1.0, 1.2)], 'alu', at=(0, 33.9, BZ), seg=24, bevel=.03)
    # barrel, A2 front sight tower, birdcage flash hider
    lathe('Barrel', [(0, .82), (16.2, .82), (16.2, .7), (18.6, .7), (18.6, .78), (21.6, .78)], 'steel', at=(0, 33.8, BZ), seg=18, bevel=.03)
    fs = path(('M', 35.4, BZ - 1.2), ('L', 38.2, BZ - 1.2), ('L', 38.2, BZ + 1.2), ('L', 37.4, BZ + 4.8), ('L', 36.5, BZ + 4.8), ('L', 35.4, BZ + 1.4))
    extrude('Front sight tower', fs, 1.8, 'alu', bevel=.08)
    for x in (-.75, .75):
        extrude('Sight ear', path(('M', 36.3, BZ + 4.4), ('L', 37.7, BZ + 4.4), ('L', 37.4, BZ + 6.1), ('L', 36.6, BZ + 6.1)), .3, 'alu', at=x, bevel=.03)
    lathe('Front post', [(0, .17), (1.6, .14)], 'steel', at=(0, 37.0, BZ + 4.5), axis='Z', seg=8, bevel=0)
    extrude('Bayonet lug', rrect(35.8, BZ - 2.4, 37.8, BZ - 1.1, .25), .9, 'alu', bevel=.04)
    slots = [cbox((0, 57.0, BZ + 1.0), (.35, 2.6, 1.2)), cbox((1.0, 57.0, BZ), (1.2, 2.6, .35)), cbox((-1.0, 57.0, BZ), (1.2, 2.6, .35))]
    lathe('Flash hider', [(0, .5), (0, 1.0), (4.0, 1.0), (4.3, .9), (4.3, .5)], 'steel', at=(0, 55.4, BZ), seg=20, closed=True, bevel=.04, cuts=slots)
    # short chunky 30-round magazine
    mg = path(('M', 3.0, 0.0), ('L', 11.5, 0.0), ('C', 11.8, -5.0, 12.4, -9.6, 13.4, -13.8), ('L', 5.6, -15.0), ('C', 4.4, -10.0, 3.4, -5.0, 3.0, 0.0), n=12)
    extrude('Magazine', mg, 2.5, 'alu', bevel=.12)
    for side in (1, -1):
        for off in (-1.4, 1.4):
            extrude('Mag rib', band(path(('M', 7.2 + off, -2.0), ('C', 7.4 + off, -6.0, 7.9 + off, -9.4, 8.6 + off, -13.2), n=8), .45), .26, 'alu', at=side * 1.3, bevel=.08, skin=True)
    extrude('Mag floorplate', band([(5.2, -15.15), (13.7, -13.9)], .8), 2.85, 'polymer', bevel=.1, skin=True)
    shift_all(5.0)
    return {'bore_z': BZ, 'muzzle': (64.7, BZ), 'optic': (7.0, 7.75), 'mag': (12.3, -7.0)}
