"""Weapon Library V3 - batch 2: SCAR, MP5, UMP, Thompson, Kar98, M1014 (Free Fire proportions)."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_snipers import scope, ring_mount


def rail(y0, y1, z, width=2.1, kind='alu', teeth=True):
    """Picatinny rail on top at height z."""
    extrude('Rail', rrect(y0, z - .55, y1, z, .08), width, kind, bevel=.04)
    if teeth:
        y = y0 + .5
        while y < y1 - .4:
            extrude('Rail tooth', rrect(y - .26, z - .02, y + .26, z + .4, .05), width, kind, bevel=0)
            y += 1.0


def grip(name, y_top_front, y_top_rear, length, rake, width, kind, z_top=1.2):
    """Ergonomic pistol grip hanging from z_top, raked back by `rake` cm over its length."""
    f, r = y_top_front, y_top_rear
    zb = z_top - length
    gp = path(('M', f, z_top), ('L', r, z_top), ('C', r - rake * .35, z_top - length * .35, r - rake * .7, z_top - length * .7, r - rake, zb + .8),
              ('Q', r - rake - .1, zb, r - rake + 1.0, zb), ('L', f - rake * .9 - .6, zb), ('Q', f - rake * .9 + .2, zb, f - rake * .9 + .2, zb + .9),
              ('C', f - rake * .6, z_top - length * .6, f - rake * .3, z_top - length * .3, f, z_top))
    return extrude(name, gp, width, kind, crown=.3, inset=.45, bevel=.12)


def trigger(y, z_top):
    extrude('Trigger', path(('M', y, z_top), ('L', y + .7, z_top), ('C', y + .7, z_top - 1.4, y + .4, z_top - 2.2, y - .4, z_top - 2.6),
                            ('L', y - .5, z_top - 2.3), ('C', y, z_top - 1.9, y + .1, z_top - 1.0, y, z_top)), .45, 'steel', bevel=.03)


def guard(y0, y1, z_top, depth, kind, width=1.3):
    tg = path(('M', y0, z_top), ('L', y0, z_top - depth + .6), ('Q', y0 + .1, z_top - depth, y0 + .8, z_top - depth), ('L', y1 - .6, z_top - depth),
              ('Q', y1, z_top - depth + .1, y1, z_top - depth + .7), ('L', y1, z_top), n=5)
    extrude('Trigger guard', band(tg, .5), width, kind, bevel=.08)


@gun
def scar():
    """Free Fire SCAR: tan receiver with full-length top rail, side/bottom rails, folding stock with cheek
    riser, black grip, slightly curved 30-round magazine, folding sights, flash hider."""
    BZ, tan = 6.0, 'polymer_tan'
    up = path(('M', -8.4, 9.5), ('L', 31.6, 9.5), ('L', 31.6, 4.0), ('L', 13.0, 4.0), ('L', 13.0, 4.2), ('L', -8.4, 4.2))
    extrude('Upper receiver', up, 3.4, tan, bevel=.14, cuts=[cbox((1.7, 4.4, 6.6), (.5, 6.0, 1.4))])
    rail(-8.2, 31.4, 10.1, 2.2, 'alu')
    rail(14.0, 31.0, 3.9, 1.8, 'alu')
    extrude('Side rail', rrect(16.0, 5.4, 30.4, 6.6, .1), .5, 'alu', at=1.9, bevel=.03)
    extrude('Bolt', rrect(1.6, 5.9, 7.2, 7.4, .2), 1.6, 'steel_bright', at=.6, skin=False, bevel=.04)
    lathe('Charging handle', [(0, .4), (-1.6, .4), (-1.8, .65), (-2.6, .65), (-2.8, .3)], 'polymer', at=(-1.7, 9.0, 7.8), axis='X', seg=12, bevel=0)
    low = path(('M', -7.6, 4.3), ('L', 13.0, 4.3), ('L', 13.0, 1.6), ('L', 12.4, -1.2), ('L', 3.6, -1.2), ('L', 3.6, .8), ('L', -3.8, .8), ('L', -7.6, 2.4))
    extrude('Lower receiver', low, 3.0, tan, bevel=.12)
    grip('Grip', -1.4, -5.0, 9.6, 2.6, 3.0, 'polymer', z_top=1.0)
    guard(-1.0, 3.4, 1.0, 2.0, tan)
    trigger(.6, 1.0)
    extrude('Selector', path(('M', -5.8, 2.6), ('L', -4.0, 2.4), ('L', -4.0, 3.0), ('L', -5.8, 3.2)), .25, 'polymer', at=1.62, bevel=.03)
    mg = path(('M', 4.0, 0.0), ('L', 12.0, 0.0), ('C', 12.3, -4.6, 12.9, -8.6, 14.0, -12.2), ('L', 6.6, -13.4), ('C', 5.4, -9.0, 4.4, -4.6, 4.0, 0.0), n=10)
    extrude('Magazine', mg, 2.6, 'polymer', bevel=.12)
    extrude('Mag floorplate', band([(6.2, -13.55), (14.3, -12.3)], .8), 2.9, 'polymer', bevel=.1)
    for side in (1, -1):
        extrude('Mag window', band(path(('M', 7.6, -2.0), ('C', 7.8, -5.0, 8.3, -8.0, 9.2, -11.0), n=6), .9), .2, 'polymer_tan', at=side * 1.35, bevel=.05, skin=True)
    # folding stock: hinge, top bar, cheek riser, skeleton lower, butt pad
    extrude('Stock hinge', rrect(-10.0, 2.4, -7.6, 9.0, .5), 3.2, 'polymer', bevel=.12)
    st = path(('M', -10.0, 8.2), ('L', -31.6, 8.2), ('L', -31.6, -6.0), ('L', -29.6, -6.2), ('L', -21.0, 1.8), ('L', -10.0, 3.0))
    extrude('Stock', st, 3.4, tan, crown=.25, inset=.4, bevel=.14, cuts=[cprism(path(('M', -27.2, 5.6), ('L', -13.0, 5.8), ('L', -13.0, 4.0), ('L', -21.6, 3.4), ('L', -27.2, -1.0)), 6, 'YZ', 0)])
    extrude('Cheek riser', rrect(-27.0, 8.1, -13.0, 9.6, .5), 3.0, 'polymer', bevel=.14)
    extrude('Butt pad', rrect(-32.8, -6.4, -31.4, 8.4, .5), 3.6, 'rubber', bevel=.12)
    # sights, barrel, flash hider
    extrude('Rear sight', path(('M', -6.0, 10.1), ('L', -3.4, 10.1), ('L', -3.8, 12.4), ('L', -5.6, 12.4)), 1.6, 'polymer', bevel=.08)
    extrude('Front sight', path(('M', 29.6, 10.1), ('L', 31.4, 10.1), ('L', 31.0, 14.2), ('L', 30.0, 14.2)), 1.4, 'polymer', bevel=.06)
    lathe('Barrel', [(0, 1.0), (14.4, .82)], 'steel', at=(0, 31.6, BZ), seg=18, bevel=.03)
    slots = [cbox((0, 48.6 + i * 1.2, BZ + 1.0), (.3, .6, 1.2)) for i in range(3)]
    lathe('Flash hider', [(0, .5), (0, 1.05), (4.6, 1.05), (4.8, .9), (4.8, .5)], 'steel', at=(0, 46.0, BZ), seg=20, closed=True, bevel=.04, cuts=slots)
    return {'bore_z': BZ, 'muzzle': (50.8, BZ), 'optic': (8.0, 10.5), 'mag': (8.6, -6.0)}


@gun
def mp5():
    """Free Fire MP5: stamped receiver with cocking tube, drum rear sight, hooded front sight, wide
    handguard, retractable stock, raked grip, curved 30-round magazine."""
    BZ = 7.6
    rec = path(('M', -14.0, 10.2), ('L', 11.0, 10.2), ('L', 11.0, 4.0), ('L', -14.0, 4.0))
    extrude('Receiver', rec, 3.0, 'steel', bevel=.14, cuts=[cbox((1.5, 2.0, 8.2), (.5, 5.6, 1.4))])
    extrude('Receiver rib', rrect(-14.0, 6.0, 11.0, 6.6, .2), .3, 'steel', at=1.6, bevel=.05)
    extrude('Receiver rib', rrect(-14.0, 6.0, 11.0, 6.6, .2), .3, 'steel', at=-1.6, bevel=.05)
    lathe('Cocking tube', [(0, 1.15), (21.0, 1.15)], 'steel', at=(0, 8.0, 9.6), seg=18, bevel=.03)
    tube('Cocking handle', [(-.9, 10.0, 9.9), (-2.2, 10.6, 10.6), (-2.6, 10.8, 11.4)], .35, 'steel')
    lathe('Drum sight', [(-1.5, 1.5), (1.5, 1.5)], 'steel', at=(0, -10.6, 11.4), axis='X', seg=22, bevel=.05)
    extrude('Drum base', rrect(-12.0, 10.1, -9.2, 11.0, .2), 2.2, 'steel', bevel=.05)
    # trigger housing (polymer), grip, guard
    th = path(('M', -6.0, 4.1), ('L', 6.4, 4.1), ('L', 6.4, 2.0), ('L', 4.4, .6), ('L', -3.0, .6), ('L', -6.0, 2.2))
    extrude('Trigger housing', th, 3.0, 'polymer', bevel=.12)
    grip('Grip', -1.6, -7.4, 11.4, 3.4, 3.2, 'polymer', z_top=.8)
    guard(-1.2, 4.0, .8, 2.2, 'polymer')
    trigger(.8, .8)
    extrude('Selector', path(('M', -5.2, 2.4), ('L', -3.4, 2.0), ('L', -3.4, 2.6), ('L', -5.2, 3.0)), .25, 'steel', at=-1.62, bevel=.03)
    # curved magazine in a short well
    extrude('Mag well', rrect(7.4, 2.6, 13.0, 4.4, .3), 3.2, 'steel', bevel=.1)
    mg = path(('M', 7.6, 3.0), ('L', 12.2, 3.0), ('C', 12.4, -3.6, 13.6, -10.4, 15.8, -16.0), ('L', 11.4, -17.4), ('C', 9.4, -11.6, 8.0, -5.4, 7.6, 3.0), n=12)
    extrude('Magazine', mg, 2.2, 'steel', bevel=.1)
    extrude('Mag floorplate', band([(11.0, -17.6), (16.2, -16.0)], .7), 2.5, 'steel', bevel=.08)
    # handguard (wide, ribbed), front sight hood, barrel with tri-lug
    hg = path(('M', 11.0, 9.0), ('L', 28.0, 9.0), ('Q', 29.0, 8.8, 29.0, 7.8), ('L', 29.0, 4.4), ('Q', 28.6, 3.2, 27.0, 3.2), ('L', 12.0, 3.6), ('L', 11.0, 4.6))
    extrude('Handguard', hg, 4.4, 'polymer', crown=.4, inset=.5, bevel=.14)
    for y in range(13, 28, 3):
        extrude('Handguard rib', rrect(y, 4.2, y + 1.0, 8.4, .3), 4.7, 'polymer', bevel=.1)
    extrude('Front sight hood', path(('M', 29.2, 8.6), ('L', 31.4, 8.6), ('L', 31.2, 12.6), ('L', 29.6, 12.6)), 2.0, 'steel', bevel=.08, cuts=[cbox((0, 30.4, 11.4), (1.4, 4, 1.6))])
    extrude('Front sight block', rrect(29.0, 6.0, 31.6, 10.6, .4), 2.4, 'steel', bevel=.1)
    lathe('Barrel', [(0, .9), (12.0, .8)], 'steel', at=(0, 31.6, BZ), seg=18, bevel=.03)
    for a in (90, 210, 330):
        ra = math.radians(a)
        lathe('Tri-lug', [(0, .38), (1.8, .38)], 'steel', at=(math.cos(ra) * .95, 41.8, BZ + math.sin(ra) * .95), seg=8, bevel=0)
    lathe('Muzzle', [(0, .45), (0, .85), (1.0, .85), (1.0, .45)], 'steel', at=(0, 43.6, BZ), seg=16, closed=True, bevel=.02)
    # retractable stock: two rods + butt plate
    lathe('Stock cap', [(0, 1.3), (1.6, 1.3)], 'steel', at=(0, -15.6, 8.4), seg=16, bevel=.04)
    for x in (-1.3, 1.3):
        tube('Stock rod', [(x, -14.2, 8.0), (x, -22.6, 8.0)], .38, 'steel', smooth=False)
    st = path(('M', -20.4, 10.0), ('L', -25.4, 10.0), ('L', -25.4, -4.4), ('L', -23.0, -4.6), ('L', -20.4, 4.0))
    extrude('Butt plate', st, 3.4, 'polymer', bevel=.14)
    return {'bore_z': BZ, 'muzzle': (44.6, BZ), 'optic': (0.0, 10.5), 'mag': (12.0, -6.0)}


@gun
def ump():
    """Free Fire UMP: boxy black polymer receiver, skeleton folding stock, long straight 25-round magazine."""
    BZ = 8.0
    rec = path(('M', -7.0, 12.0), ('L', 31.6, 12.0), ('L', 32.6, 11.0), ('L', 32.6, 5.0), ('L', 18.0, 4.0), ('L', 14.0, 4.0), ('L', 14.0, 3.4),
               ('L', 6.0, 3.4), ('L', 6.0, 4.0), ('L', -3.0, 4.0), ('L', -7.0, 6.0))
    extrude('Receiver', rec, 4.0, 'polymer', bevel=.16, cuts=[cbox((2.0, 9.0, 9.6), (.5, 6.0, 1.6))])
    rail(-4.0, 30.0, 12.6, 2.2, 'alu')
    rail(20.0, 31.0, 4.0, 1.8, 'alu')
    extrude('Bolt', rrect(6.4, 8.9, 11.6, 10.3, .2), 2.0, 'steel_bright', at=.8, skin=False, bevel=.04)
    extrude('Charging handle', rrect(24.0, 9.0, 26.0, 10.4, .3), 1.8, 'polymer', at=-2.6, bevel=.08)
    grip('Grip', 2.0, -2.6, 10.4, 3.2, 3.2, 'polymer', z_top=4.0)
    guard(2.4, 6.2, 4.0, 3.4, 'polymer', width=1.6)
    trigger(4.0, 4.0)
    extrude('Mag well', rrect(13.4, 1.6, 19.0, 4.4, .4), 3.6, 'polymer', bevel=.12)
    mg = path(('M', 14.0, 2.0), ('L', 18.4, 2.0), ('L', 20.0, -18.6), ('L', 15.4, -18.6))
    extrude('Magazine', mg, 2.6, 'polymer', bevel=.12)
    extrude('Mag floorplate', rrect(15.0, -19.4, 20.4, -18.4, .3), 2.9, 'polymer', bevel=.08)
    extrude('Front sight', path(('M', 30.0, 12.6), ('L', 32.0, 12.6), ('L', 31.6, 14.6), ('L', 30.4, 14.6)), 1.6, 'polymer', bevel=.06)
    extrude('Rear sight', path(('M', -5.2, 12.6), ('L', -2.6, 12.6), ('L', -3.0, 14.4), ('L', -4.8, 14.4)), 2.0, 'polymer', bevel=.06)
    lathe('Barrel', [(0, 1.0), (7.0, .9)], 'steel', at=(0, 32.6, BZ), seg=18, bevel=.03)
    lathe('Muzzle', [(0, .5), (0, 1.15), (2.0, 1.15), (2.0, .5)], 'steel', at=(0, 39.0, BZ), seg=18, closed=True, bevel=.03)
    # skeleton folding stock
    st = path(('M', -7.0, 11.0), ('L', -26.6, 10.0), ('L', -27.4, 9.0), ('L', -27.4, -3.0), ('L', -8.0, -3.0), ('L', -7.0, 5.0))
    hole = cprism(path(('M', -24.8, 8.2), ('L', -10.0, 9.0), ('L', -9.6, 5.0), ('L', -14.0, -1.0), ('L', -24.8, -1.0)), 8, 'YZ', 0)
    extrude('Stock', st, 3.0, 'polymer', bevel=.14, cuts=[hole])
    extrude('Butt pad', rrect(-28.4, -3.2, -27.2, 10.2, .4), 3.2, 'rubber', bevel=.12)
    return {'bore_z': BZ, 'muzzle': (41.0, BZ), 'optic': (10.0, 13.0), 'mag': (17.0, -8.0)}


@gun
def thompson():
    """Free Fire Thompson: blued box receiver, finned barrel with compensator, walnut stock, pistol grip and
    horizontal forend, stick magazine."""
    BZ = 9.0
    rec = path(('M', -13.0, 11.6), ('L', 10.0, 11.6), ('L', 10.0, 5.0), ('L', -13.0, 5.0))
    extrude('Receiver', rec, 3.6, 'blued', bevel=.16, cuts=[cbox((1.8, 2.0, 9.4), (.5, 5.6, 1.4))])
    extrude('Actuator knob', rrect(-1.0, 11.5, 1.6, 12.6, .4), 1.6, 'steel', bevel=.06)
    extrude('Rear sight', path(('M', -11.6, 11.6), ('L', -8.6, 11.6), ('L', -9.2, 13.4), ('L', -11.0, 13.4)), 2.2, 'blued', bevel=.06)
    low = path(('M', -11.0, 5.1), ('L', 8.0, 5.1), ('L', 8.0, 2.4), ('L', -2.0, 2.4), ('L', -11.0, 3.4))
    extrude('Trigger frame', low, 3.2, 'blued', bevel=.12)
    grip('Grip', 1.6, -3.0, 10.6, 3.4, 3.2, 'wood', z_top=2.6)
    guard(1.8, 6.0, 2.6, 2.6, 'blued')
    trigger(3.6, 2.6)
    extrude('Mag well', rrect(9.0, 2.6, 13.6, 5.2, .3), 3.0, 'blued', bevel=.1)
    extrude('Magazine', rrect(9.6, -9.8, 13.0, 3.0, .3), 2.4, 'blued', bevel=.1)
    extrude('Mag floorplate', rrect(9.3, -10.6, 13.3, -9.6, .2), 2.6, 'blued', bevel=.06)
    # finned barrel, compensator, front sight, horizontal forend
    for i in range(16):
        lathe('Barrel fin', [(-.25, 1.55), (.25, 1.55)], 'blued', at=(0, 11.5 + i * 1.0, BZ), seg=20, bevel=.02)
    lathe('Barrel', [(0, 1.15), (33.4, 1.0)], 'blued', at=(0, 10.0, BZ), seg=20, bevel=.03)
    slots = [cbox((0, 44.4 + i * .9, BZ + 1.2), (2.2, .4, 1.2)) for i in range(3)]
    lathe('Compensator', [(0, .6), (0, 1.35), (4.6, 1.35), (4.6, .6)], 'blued', at=(0, 43.0, BZ), seg=22, closed=True, bevel=.05, cuts=slots)
    extrude('Front sight', path(('M', 43.6, BZ + 1.2), ('L', 45.6, BZ + 1.2), ('L', 45.2, BZ + 3.0), ('L', 44.0, BZ + 3.0)), .6, 'blued', bevel=.03)
    fe = path(('M', 18.6, 7.8), ('L', 41.0, 7.8), ('Q', 42.2, 7.6, 42.2, 6.6), ('L', 42.2, 5.6), ('Q', 42.0, 4.6, 40.8, 4.6), ('L', 18.6, 4.8))
    extrude('Forend', fe, 3.6, 'wood', crown=.4, inset=.5, bevel=.14)
    # walnut buttstock with drop
    st = path(('M', -12.6, 10.4), ('L', -36.4, 4.0), ('L', -37.4, 3.6), ('L', -37.4, -9.4), ('L', -36.0, -9.6), ('C', -28.0, -5.0, -20.0, -.8, -12.6, 4.0))
    extrude('Stock', st, 3.8, 'wood', crown=.4, inset=.6, bevel=.15)
    extrude('Butt plate', band([(-37.8, 3.8), (-37.8, -9.6)], .7), 4.0, 'steel', bevel=.1)
    return {'bore_z': BZ, 'muzzle': (47.6, BZ), 'optic': (0.0, 12.0), 'mag': (11.3, -3.0)}


@gun
def kar98():
    """Free Fire Kar98k: full-length orange-brown wood stock with straight wrist, exposed muzzle with hooded
    front sight, turned-down bolt, short scope."""
    BZ = 7.6
    st = path(('M', 52.0, 8.4), ('L', 6.0, 8.4), ('L', 2.0, 7.4), ('L', -10.0, 6.4), ('L', -36.0, 6.2), ('L', -37.0, 5.6), ('L', -37.0, -7.6),
              ('L', -35.6, -8.0), ('C', -26.0, -3.0, -16.0, -.4, -6.0, 1.2), ('Q', -2.0, 1.4, 0.0, 3.4), ('L', 52.0, 5.0))
    extrude('Stock', st, 3.8, 'wood', crown=.4, inset=.55, bevel=.15,
            cuts=[cbox((2.0, -16.0, 1.0), (.4, 3.0, 2.0)), cbox((-2.0, -16.0, 1.0), (.4, 3.0, 2.0))])
    extrude('Butt plate', band([(-37.4, 5.8), (-37.4, -8.0)], .7), 4.0, 'steel', bevel=.1)
    lathe('Stock disc', [(0, .85), (.15, .85)], 'steel', at=(1.9, -18.0, 2.6), axis='X', seg=16, bevel=0)
    extrude('Barrel band', rrect(28.0, 4.6, 29.4, 9.0, .3), 4.0, 'steel', bevel=.08)
    extrude('Nose cap', rrect(50.4, 4.8, 53.2, 9.0, .4), 3.6, 'steel', bevel=.08)
    # receiver, bolt, trigger guard
    lathe('Receiver', [(0, 1.3), (.6, 1.55), (16.0, 1.55), (16.6, 1.35)], 'blued', at=(0, -1.6, BZ + .5), seg=22, bevel=.04)
    lathe('Bolt shroud', [(0, .7), (.6, 1.1), (2.6, 1.1)], 'blued', at=(0, -4.2, BZ + .6), seg=18, bevel=.04)
    extrude('Safety', rrect(-4.6, BZ + 1.6, -3.6, BZ + 2.6, .2), .4, 'steel', bevel=.03)
    tube('Bolt handle', [(1.4, 1.2, BZ + .8), (3.0, 1.0, BZ - .6), (3.4, .9, BZ - 2.4)], .35, 'steel_bright')
    lathe('Bolt knob', [(0, .2), (-.3, .7), (-1.0, .8), (-1.6, .5), (-1.8, .1)], 'steel_bright', at=(3.4, .9, BZ - 2.2), axis='Z', seg=14, bevel=0)
    guard(2.0, 9.0, 1.6, 2.4, 'blued', width=1.1)
    trigger(4.6, 1.6)
    extrude('Floorplate', rrect(.4, .9, 11.0, 1.8, .3), 2.4, 'blued', bevel=.06)
    lathe('Barrel', [(0, 1.15), (6.0, 1.05), (68.0, .85)], 'blued', at=(0, 14.6, BZ), seg=20, bevel=.03)
    extrude('Front sight hood', path(('M', 79.0, BZ + .4), ('L', 82.2, BZ + .4), ('L', 82.0, BZ + 2.8), ('L', 79.4, BZ + 2.8)), 1.8, 'blued', bevel=.08,
            cuts=[cbox((0, 80.6, BZ + 1.8), (1.2, 4, 1.4))])
    lathe('Muzzle', [(0, .45), (0, .95), (.8, .95), (.8, .45)], 'blued', at=(0, 82.6, BZ), seg=18, closed=True, bevel=.02)
    extrude('Rear sight', path(('M', 16.0, BZ + 1.0), ('L', 22.0, BZ + 1.0), ('L', 21.6, BZ + 1.8), ('L', 16.4, BZ + 1.8)), 1.6, 'blued', bevel=.05)
    SZ = BZ + 5.0
    scope(3.0, 22.0, SZ, tube_r=1.2, bell_r=1.75, eye_r=1.6, kind='blued')
    ring_mount(6.0, BZ + 2.0, SZ, 1.2, 'blued')
    ring_mount(19.0, BZ + 2.0, SZ, 1.2, 'blued')
    return {'bore_z': BZ, 'muzzle': (83.4, BZ), 'optic': (12.0, SZ), 'mag': (5.0, 1.0)}


@gun
def m1014():
    """Free Fire M1014: pistol-grip semi-auto shotgun, black receiver with ghost-ring sight rail,
    long barrel over a magazine tube with clamp, ribbed forend."""
    BZ = 8.2
    rec = path(('M', -6.0, 10.6), ('L', 28.0, 10.6), ('L', 28.0, 3.4), ('L', 10.0, 3.0), ('L', 2.0, 3.0), ('L', -6.0, 4.6))
    extrude('Receiver', rec, 4.0, 'alu', bevel=.16, cuts=[cbox((2.0, 14.0, 7.6), (.5, 8.0, 2.2))])
    extrude('Bolt', rrect(10.4, 6.6, 17.6, 8.6, .3), 2.4, 'steel_bright', at=.8, skin=False, bevel=.05)
    lathe('Charging handle', [(0, .4), (1.4, .4), (1.6, .7), (2.4, .7), (2.6, .3)], 'steel', at=(1.9, 16.0, 7.6), axis='X', seg=12, bevel=0)
    extrude('Top shield', rrect(-2.0, 10.4, 30.0, 12.6, .4), 2.6, 'alu', bevel=.1)
    rail(-2.0, 30.0, 13.2, 2.0, 'alu')
    extrude('Ghost ring', path(('M', -2.0, 11.2), ('L', 1.6, 11.2), ('L', 1.2, 13.6), ('L', -1.6, 13.6)), 2.4, 'alu', bevel=.08,
            cuts=[ccyl((0, -.2, 12.6), .55, 4, 'Y', 12)])
    grip('Grip', 1.4, -3.4, 11.0, 3.8, 3.4, 'polymer', z_top=3.2)
    guard(1.8, 7.0, 3.2, 2.8, 'alu', width=1.5)
    trigger(3.8, 3.2)
    extrude('Loading port', rrect(8.0, 2.4, 20.0, 3.4, .3), 3.2, 'alu', bevel=.08)
    fe = path(('M', 28.0, 8.6), ('L', 48.0, 8.6), ('Q', 49.0, 8.4, 49.0, 7.4), ('L', 49.0, 3.6), ('Q', 48.8, 2.4, 47.4, 2.4), ('L', 28.0, 2.6))
    extrude('Forend', fe, 4.4, 'polymer', crown=.4, inset=.5, bevel=.14)
    for y in range(31, 47, 2):
        extrude('Forend rib', rrect(y, 3.0, y + .8, 8.0, .25), 4.65, 'polymer', bevel=.08)
    lathe('Barrel', [(0, 1.45), (60.0, 1.35)], 'alu', at=(0, 28.0, BZ), seg=22, bevel=.03)
    lathe('Muzzle', [(0, 1.0), (0, 1.5), (1.0, 1.5), (1.0, 1.0)], 'alu', at=(0, 88.0, BZ), seg=22, closed=True, bevel=.03)
    lathe('Mag tube', [(0, 1.7), (44.0, 1.7), (44.6, 1.3)], 'alu', at=(0, 40.0, 4.2), seg=20, bevel=.03)
    lathe('Mag cap', [(0, 1.8), (1.6, 1.8), (1.8, 1.3)], 'steel', at=(0, 84.4, 4.2), seg=20, bevel=.03)
    extrude('Barrel clamp', rrect(80.0, 3.0, 82.4, 9.8, .6), 3.6, 'alu', bevel=.1)
    extrude('Front sight', path(('M', 84.0, BZ + 1.3), ('L', 86.6, BZ + 1.3), ('L', 86.2, BZ + 3.6), ('L', 84.4, BZ + 3.6)), .9, 'alu', bevel=.05)
    return {'bore_z': BZ, 'muzzle': (89.0, BZ), 'optic': (10.0, 11.2), 'mag': (60.0, 4.6)}
