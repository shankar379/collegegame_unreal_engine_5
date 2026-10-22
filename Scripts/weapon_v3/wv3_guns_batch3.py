"""Weapon Library V3 - batch 3: bullpups and compact guns (Groza, P90, Vector, FAMAS, AUG, XM8)."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_batch2 import rail, grip, trigger, guard
from wv3_guns_snipers import scope, ring_mount


def flash_hider(y, z, r=1.0, length=4.0):
    slots = [cbox((0, y + length * .55, z + r), (.3, length * .5, 1.2))]
    lathe('Flash hider', [(0, r * .5), (0, r), (length, r), (length + .2, r * .85), (length + .2, r * .5)], 'steel', at=(0, y, z), seg=20, closed=True, bevel=.04, cuts=slots)


@gun
def groza():
    """Free Fire Groza: bullpup body, curved magazine behind the grip, carry handle with wood insert,
    long trigger guard, short barrel with vertical foregrip."""
    BZ = 10.0
    body = path(('M', -46.0, 14.4), ('L', 12.0, 14.4), ('L', 12.0, 8.0), ('L', -12.0, 7.6), ('L', -22.0, 5.2), ('L', -44.0, 5.0), ('L', -46.0, 6.0))
    extrude('Body', body, 3.8, 'steel', bevel=.16, cuts=[cbox((1.9, -22.0, 11.0), (.5, 8.0, 1.6))])
    extrude('Butt pad', rrect(-47.0, 4.6, -45.6, 14.8, .4), 4.0, 'rubber', bevel=.12)
    extrude('Cheek', rrect(-44.0, 10.0, -26.0, 14.2, .8), 4.1, 'polymer', bevel=.14)
    ch = path(('M', -12.0, 14.3), ('L', 10.4, 14.3), ('L', 10.4, 19.6), ('L', -10.0, 19.6), ('L', -12.0, 17.6))
    extrude('Carry handle', ch, 2.6, 'steel', bevel=.14, cuts=[cprism(rrect(-9.4, 15.2, 8.4, 18.2, .8), 4, 'YZ', 0)])
    extrude('Handle wood', rrect(-8.6, 14.3, 7.6, 15.6, .4), 2.8, 'wood', bevel=.1)
    # grip, guard, trigger
    gp = path(('M', 4.0, 7.8), ('L', -3.0, 7.8), ('C', -3.4, 3.0, -4.2, -2.0, -5.0, -5.6), ('Q', -5.2, -7.2, -3.6, -7.2), ('L', -.8, -7.0),
              ('Q', .2, -6.8, .2, -5.6), ('C', 1.0, -1.6, 2.6, 3.4, 4.0, 7.8))
    extrude('Grip', gp, 3.2, 'wood', crown=.35, inset=.5, bevel=.12)
    tg = path(('M', -4.4, 7.8), ('L', -4.6, 2.6), ('Q', -4.4, 1.6, -3.0, 1.6), ('L', 9.0, 1.6), ('Q', 10.4, 1.8, 10.4, 3.4), ('L', 10.4, 8.0), n=6)
    extrude('Trigger guard', band(tg, .55), 1.3, 'steel', bevel=.08)
    trigger(5.4, 7.8)
    # curved magazine behind the grip
    mg = path(('M', -17.0, 6.0), ('L', -9.6, 6.0), ('C', -9.4, .4, -10.4, -5.0, -12.6, -10.0), ('L', -19.8, -10.0), ('C', -18.6, -4.6, -17.6, .4, -17.0, 6.0), n=12)
    extrude('Magazine', mg, 2.8, 'steel', bevel=.12)
    for side in (1, -1):
        extrude('Mag rib', band(path(('M', -13.8, 4.0), ('C', -13.8, 0, -14.4, -4.0, -15.6, -8.4), n=6), .5), .3, 'steel', at=side * 1.45, bevel=.06, skin=True)
    extrude('Mag floorplate', rrect(-20.2, -10.8, -12.0, -9.8, .3), 3.1, 'steel', bevel=.08)
    # front: short handguard, foregrip, barrel, front sight
    extrude('Handguard', rrect(10.0, 6.2, 20.0, 12.6, .6), 3.4, 'polymer', bevel=.14)
    lathe('Foregrip', [(0, 1.15), (11.0, 1.25), (11.6, .9)], 'polymer', at=(0, 16.0, -5.4), axis='Z', seg=16, bevel=.04)
    lathe('Barrel', [(0, .9), (5.6, .85)], 'steel', at=(0, 20.0, BZ), seg=18, bevel=.03)
    flash_hider(25.4, BZ, .95, 3.4)
    extrude('Front sight', path(('M', 17.0, 12.6), ('L', 19.4, 12.6), ('L', 19.0, 15.0), ('L', 17.4, 15.0)), 1.4, 'steel', bevel=.06)
    return {'bore_z': BZ, 'muzzle': (29.0, BZ), 'optic': (-1.0, 19.6), 'mag': (-17.0, -2.0)}


@gun
def p90():
    """Free Fire P90: one-piece black body with thumbhole stock and grip hole, tan translucent top magazine."""
    BZ = 8.0
    body = path(('M', -36.0, 13.0), ('L', 6.0, 13.0), ('L', 9.0, 11.4), ('L', 9.0, 4.0), ('L', 8.0, 1.6), ('Q', 6.0, -1.6, 2.0, -1.4),
                ('L', -2.0, -1.0), ('L', -8.0, -.4), ('Q', -12.0, -.4, -14.0, .4), ('L', -33.0, 1.2), ('Q', -36.0, 1.6, -36.0, 4.6))
    holes = [cprism(rrect(-16.4, 2.6, -6.4, 7.0, 2.0), 8, 'YZ', 0), cprism(rrect(-3.6, 1.6, 4.0, 7.2, 2.0), 8, 'YZ', 0)]
    extrude('Body', body, 5.0, 'polymer', crown=.3, inset=.5, bevel=.2, cuts=holes)
    extrude('Top magazine', rrect(-20.0, 12.6, 6.0, 15.4, .6), 4.4, 'polymer_tan', bevel=.12, skin=True)
    for y in range(-18, 5, 4):
        extrude('Round', rrect(y, 13.2, y + 1.2, 14.8, .4), 4.6, 'brass', bevel=.05, skin=False)
    extrude('Sight housing', path(('M', -6.0, 15.2), ('L', 8.0, 15.2), ('L', 9.0, 13.0), ('L', 9.0, 17.0), ('L', -4.0, 18.0), ('L', -6.0, 17.0)), 3.6, 'polymer', bevel=.14)
    extrude('Sight glass', rrect(-3.0, 16.0, 5.0, 17.2, .3), 3.7, 'lens', bevel=.04)
    lathe('Barrel', [(0, .8), (5.0, .75)], 'steel', at=(0, 9.0, BZ), seg=16, bevel=.03)
    lathe('Muzzle', [(0, .45), (0, .95), (1.4, .95), (1.4, .45)], 'steel', at=(0, 13.6, BZ), seg=16, closed=True, bevel=.03)
    extrude('Butt pad', rrect(-37.0, 1.4, -35.6, 12.8, .5), 5.0, 'rubber', bevel=.14)
    return {'bore_z': BZ, 'muzzle': (15.0, BZ), 'optic': (1.0, 18.0), 'mag': (-7.0, 14.0)}


@gun
def vector():
    """Free Fire Vector: black upper with rail, angular tan lower housing with the slanted front,
    magazine inside the housing, skeleton folding stock on a thin arm."""
    BZ = 10.0
    up = path(('M', -3.0, 14.0), ('L', 24.0, 14.0), ('L', 26.0, 12.6), ('L', 26.0, 9.0), ('L', -3.0, 9.0))
    extrude('Upper', up, 3.4, 'polymer', bevel=.14)
    rail(-2.0, 24.0, 14.6, 2.2, 'alu')
    low = path(('M', -1.0, 9.1), ('L', 25.0, 9.1), ('L', 25.0, 6.6), ('L', 20.6, -.8), ('L', 9.0, -.8), ('L', 6.0, 1.2), ('L', 3.0, 1.2), ('L', 1.0, 6.0))
    extrude('Lower housing', low, 3.8, 'polymer_tan', bevel=.16, cuts=[cprism(path(('M', 3.4, 7.6), ('L', 8.4, 7.6), ('L', 8.4, 1.8), ('L', 5.0, 1.8)), 6, 'YZ', 0)])
    grip('Grip', 2.8, -1.6, 10.0, 2.6, 3.0, 'polymer', z_top=9.0)
    trigger(4.4, 7.6)
    extrude('Magazine', rrect(10.6, -10.6, 14.0, 1.0, .3), 2.4, 'polymer', bevel=.1)
    extrude('Mag floorplate', rrect(10.3, -11.4, 14.3, -10.4, .2), 2.6, 'polymer', bevel=.06)
    lathe('Barrel', [(0, .9), (4.4, .85)], 'steel', at=(0, 26.0, BZ), seg=16, bevel=.03)
    lathe('Muzzle', [(0, .5), (0, 1.0), (1.4, 1.0), (1.4, .5)], 'steel', at=(0, 30.2, BZ), seg=16, closed=True, bevel=.03)
    extrude('Front sight', path(('M', 21.0, 14.6), ('L', 23.6, 14.6), ('L', 23.2, 16.8), ('L', 21.4, 16.8)), 1.4, 'polymer', bevel=.06)
    extrude('Rear sight', path(('M', -2.0, 14.6), ('L', .8, 14.6), ('L', .4, 16.6), ('L', -1.6, 16.6)), 1.8, 'polymer', bevel=.06)
    # stock arm + skeleton butt
    extrude('Stock arm', path(('M', -3.2, 13.6), ('L', -22.0, 13.0), ('L', -24.0, 11.0), ('L', -22.6, 10.0), ('L', -3.2, 11.4)), 2.2, 'polymer', bevel=.12)
    st = path(('M', -24.6, 11.6), ('L', -31.0, 11.6), ('L', -31.0, -1.0), ('L', -28.4, -1.0), ('L', -24.6, 4.0))
    extrude('Stock', st, 3.0, 'polymer', bevel=.14, cuts=[cprism(rrect(-29.6, 2.0, -26.4, 8.0, .8), 6, 'YZ', 0)])
    return {'bore_z': BZ, 'muzzle': (31.6, BZ), 'optic': (10.0, 15.0), 'mag': (12.3, -4.0)}


@gun
def famas():
    """Free Fire FAMAS: bullpup with the long carry handle, magazine behind the grip, full-loop trigger guard,
    rubber butt, short barrel."""
    BZ = 8.4
    body = path(('M', -35.0, 14.0), ('L', 26.0, 11.4), ('L', 28.0, 9.6), ('L', 28.0, 5.0), ('L', 14.0, 4.6), ('L', -2.0, 3.4), ('L', -12.0, 3.0),
                ('L', -20.0, .4), ('L', -33.0, .0), ('L', -35.0, 1.6))
    extrude('Body', body, 4.4, 'polymer', crown=.3, inset=.45, bevel=.16, cuts=[cbox((2.2, -10.0, 9.6), (.5, 7.0, 1.6))])
    extrude('Butt pad', rrect(-36.4, -.2, -34.8, 14.2, .5), 4.6, 'rubber', bevel=.14)
    ch = path(('M', -8.0, 13.0), ('L', -6.6, 18.4), ('L', 24.0, 18.4), ('L', 26.0, 15.6), ('L', 26.0, 11.4), ('L', -8.0, 13.0))
    extrude('Carry handle', ch, 2.6, 'polymer', bevel=.16, cuts=[cprism(path(('M', -4.6, 13.4), ('L', 22.0, 12.4), ('L', 23.4, 14.0), ('L', 22.4, 16.6), ('L', -3.8, 16.6)), 4, 'YZ', 0)])
    lathe('Charging handle', [(0, .6), (1.4, .6)], 'polymer', at=(0, 6.0, 15.0), axis='Z', seg=12, bevel=.03)
    gp = path(('M', 2.6, 3.6), ('L', -2.4, 3.4), ('C', -3.0, 0, -3.8, -3.4, -4.6, -6.0), ('Q', -4.8, -7.4, -3.4, -7.4), ('L', -1.0, -7.2),
              ('Q', 0, -7.0, 0, -6.0), ('C', .6, -2.6, 1.6, .6, 2.6, 3.6))
    extrude('Grip', gp, 3.2, 'polymer', crown=.3, inset=.45, bevel=.12)
    loop = [(0, -3.4, -6.4), (0, -1.0, -9.6), (0, 5.0, -10.4), (0, 11.0, -8.4), (0, 14.4, -3.0), (0, 14.0, 4.4)]
    tube('Trigger guard loop', loop, .4, 'steel')
    trigger(4.2, 3.4)
    extrude('Magazine', rrect(-19.4, -10.6, -14.4, 1.0, .4), 2.6, 'alu', bevel=.12)
    extrude('Mag floorplate', rrect(-19.8, -11.4, -14.0, -10.4, .3), 2.9, 'polymer', bevel=.08)
    lathe('Barrel', [(0, .9), (13.0, .82)], 'steel', at=(0, 27.8, BZ), seg=18, bevel=.03)
    flash_hider(40.6, BZ, .95, 2.6)
    for x in (-1.0, 1.0):
        tube('Bipod leg', [(x, 26.4, 6.0), (x * 1.6, 25.0, 4.6)], .35, 'steel', smooth=False)
    return {'bore_z': BZ, 'muzzle': (43.4, BZ), 'optic': (6.0, 18.4), 'mag': (-16.9, -4.0)}


@gun
def aug():
    """Free Fire AUG: rounded polymer bullpup stock, tan translucent magazine, long barrel,
    scope/handle on top, big trigger guard enclosing the grip, vertical foregrip."""
    BZ = 11.0
    st = path(('M', -34.0, 11.4), ('Q', -34.6, 13.2, -32.6, 13.2), ('L', -4.0, 13.4), ('L', -2.0, 14.0), ('L', 18.0, 14.0), ('L', 18.0, 8.0),
              ('L', -2.0, 7.4), ('L', -10.0, 4.0), ('L', -26.0, -1.0), ('Q', -32.0, -2.0, -34.0, 2.0))
    extrude('Stock body', st, 4.6, 'polymer', crown=.45, inset=.7, bevel=.2)
    extrude('Magazine', path(('M', -15.6, 1.6), ('L', -9.4, 4.2), ('L', -10.6, -6.6), ('L', -16.0, -8.0)), 2.6, 'polymer_tan', bevel=.12, skin=True)
    # grip inside the big guard, foregrip
    gp = path(('M', 2.6, 7.6), ('L', -2.0, 7.6), ('C', -2.6, 3.0, -3.4, -.4, -3.8, -2.4), ('Q', -4.0, -3.6, -2.6, -3.6), ('L', -.6, -3.4),
              ('Q', .4, -3.2, .4, -2.4), ('C', .8, .8, 1.8, 4.4, 2.6, 7.6))
    extrude('Grip', gp, 3.0, 'polymer', crown=.3, inset=.45, bevel=.12)
    gd = path(('M', -3.0, 4.6), ('C', -6.0, -2.0, -5.0, -5.4, -1.0, -5.6), ('L', 12.0, -5.6), ('Q', 14.0, -5.4, 14.0, -3.2), ('L', 14.0, 8.2), n=10)
    extrude('Trigger guard', band(gd, .9), 2.4, 'polymer', bevel=.12)
    trigger(3.4, 7.6)
    lathe('Foregrip', [(0, 1.1), (9.6, 1.2), (10.2, .8)], 'polymer', at=(0, 15.6, -1.0), axis='Z', seg=16, bevel=.04)
    lathe('Barrel', [(0, 1.2), (3.0, 1.0), (20.0, .85)], 'steel', at=(0, 18.0, BZ), seg=18, bevel=.03)
    flash_hider(38.0, BZ, .95, 3.0)
    # scope / carry handle
    extrude('Scope mount', path(('M', -8.0, 14.0), ('L', 14.0, 14.0), ('L', 12.0, 17.6), ('L', -6.0, 17.6)), 2.2, 'alu', bevel=.12,
            cuts=[cprism(rrect(-4.0, 14.0, 9.0, 16.4, .8), 4, 'YZ', 0)])
    scope(-8.0, 22.0, 19.4, tube_r=1.3, bell_r=1.6, eye_r=1.5, kind='alu', turret=False)
    return {'bore_z': BZ, 'muzzle': (41.2, BZ), 'optic': (2.0, 19.4), 'mag': (-12.6, -2.0)}


@gun
def xm8():
    """Free Fire XM8: smooth rounded polymer body, integrated optic on a handle, slim stock,
    curved magazine, short barrel."""
    BZ = 8.6
    body = path(('M', -12.0, 12.6), ('C', 4.0, 14.2, 30.0, 13.8, 42.0, 10.4), ('Q', 44.6, 9.2, 44.4, 7.2), ('L', 44.0, 6.0), ('Q', 42.0, 4.2, 36.0, 4.6),
                ('L', 12.0, 5.0), ('L', 2.0, 4.0), ('L', -8.0, 4.0), ('L', -12.0, 6.0))
    extrude('Body', body, 4.2, 'polymer', crown=.5, inset=.7, bevel=.2, cuts=[cbox((2.1, 2.0, 9.0), (.5, 6.0, 1.6))])
    extrude('Optic handle', path(('M', -2.0, 13.0), ('L', 10.0, 13.6), ('L', 9.0, 16.4), ('L', -1.0, 16.0)), 2.4, 'polymer', bevel=.14)
    lathe('Optic', [(0, 1.3), (.4, 1.6), (8.0, 1.6), (8.0, 1.4)], 'polymer', at=(0, -1.0, 17.6), seg=22, bevel=.05)
    lathe('Lens', [(0, 1.3), (.06, 1.3)], 'lens', at=(0, 7.0, 17.6), seg=20, bevel=0)
    st = path(('M', -12.0, 12.0), ('L', -26.0, 11.6), ('L', -28.0, 10.6), ('L', -28.0, 1.0), ('L', -26.0, .6), ('L', -24.0, 4.0), ('L', -12.0, 6.0))
    extrude('Stock', st, 3.2, 'polymer', bevel=.16, cuts=[cprism(rrect(-23.6, 5.6, -14.0, 9.8, 1.2), 6, 'YZ', 0)])
    grip('Grip', 2.0, -2.6, 10.6, 3.0, 3.0, 'polymer', z_top=4.2)
    guard(2.4, 6.6, 4.2, 2.6, 'polymer')
    trigger(4.0, 4.2)
    mg = path(('M', 8.0, 4.4), ('L', 13.0, 4.4), ('C', 13.2, -.6, 14.2, -5.0, 15.8, -8.4), ('L', 10.8, -9.6), ('C', 9.4, -5.6, 8.4, -.6, 8.0, 4.4), n=10)
    extrude('Magazine', mg, 2.4, 'polymer', bevel=.12)
    lathe('Barrel', [(0, .85), (6.0, .8)], 'steel', at=(0, 44.0, BZ), seg=16, bevel=.03)
    flash_hider(49.8, BZ, .9, 2.6)
    return {'bore_z': BZ, 'muzzle': (52.6, BZ), 'optic': (3.0, 17.6), 'mag': (11.0, -2.0)}
