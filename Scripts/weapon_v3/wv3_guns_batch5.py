"""Weapon Library V3 - batch 5: machine guns and remaining rifles (M249, M60, Kord, AN94, M416, SPAS12).
Written in reference coordinates (see wv3_guns_batch4.py)."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_batch2 import rail, grip, trigger, guard
from wv3_guns_snipers import scope, ring_mount
from wv3_guns_batch3 import flash_hider
from wv3_guns_batch4 import place


def bipod_folded(y0, y1, z, kind='steel'):
    for x in (-1.0, 1.0):
        tube('Bipod leg', [(x, y0, z), (x, y1, z - .4)], .35, kind, smooth=False)
    extrude('Bipod yoke', rrect(y0 - 1.0, z - .8, y0 + .6, z + 1.2, .3), 2.6, kind, bevel=.06)


@gun
def m249():
    """Free Fire M249: box butt, feed cover with rear sight, carry handle, raked grip, olive ammo pouch,
    ribbed handguard, gas tube, folded bipod, long barrel with front sight."""
    BZ = -11.6
    st = path(('M', 0.0, -9.4), ('L', 11.0, -9.4), ('L', 26.0, -10.0), ('L', 26.0, -14.4), ('L', 12.0, -16.6), ('L', 11.0, -22.0), ('L', 0.0, -22.0))
    extrude('Stock', st, 4.0, 'polymer', crown=.3, inset=.5, bevel=.16)
    rec = path(('M', 25.0, -8.6), ('L', 61.0, -8.6), ('L', 61.0, -15.4), ('L', 25.0, -15.4))
    extrude('Receiver', rec, 4.2, 'steel', bevel=.14, cuts=[cbox((2.1, 44.0, -12.0), (.5, 7.0, 1.6))])
    extrude('Feed cover', path(('M', 29.0, -8.7), ('L', 50.0, -8.7), ('L', 50.0, -7.0), ('L', 31.0, -6.6), ('L', 29.0, -7.4)), 4.0, 'steel', bevel=.12)
    extrude('Rear sight', path(('M', 30.0, -6.8), ('L', 33.0, -6.8), ('L', 32.4, -5.0), ('L', 30.6, -5.0)), 2.0, 'steel', bevel=.06)
    tube('Carry handle', [(0, 57.0, -8.6), (0, 59.0, -5.2), (0, 65.0, -3.8)], .55, 'polymer')
    grip('Grip', 37.6, 33.0, 11.0, 3.6, 3.2, 'polymer', z_top=-15.4)
    guard(37.8, 44.0, -15.4, 2.6, 'steel')
    trigger(40.0, -15.4)
    pouch = path(('M', 46.4, -15.0), ('L', 55.6, -15.0), ('Q', 56.6, -15.2, 56.4, -17.0), ('L', 55.4, -30.0), ('Q', 55.0, -31.4, 53.4, -31.4),
                 ('L', 48.6, -31.4), ('Q', 47.2, -31.4, 47.0, -30.0), ('L', 46.0, -17.0))
    extrude('Ammo pouch', pouch, 5.2, 'polymer_green', crown=.5, inset=.7, bevel=.2)
    hg = path(('M', 60.0, -9.6), ('L', 80.0, -10.0), ('L', 80.0, -15.6), ('L', 60.0, -16.4))
    extrude('Handguard', hg, 3.8, 'polymer', bevel=.14)
    for y in range(62, 79, 3):
        extrude('Handguard rib', rrect(y, -15.0, y + 1.2, -10.4, .3), 4.05, 'polymer', bevel=.06)
    lathe('Gas tube', [(0, .8), (20.0, .8)], 'steel', at=(0, 62.0, -14.6), seg=14, bevel=.03)
    lathe('Barrel', [(0, 1.15), (44.0, .95)], 'steel', at=(0, 60.0, BZ), seg=20, bevel=.03)
    extrude('Front sight', path(('M', 80.6, -11.0), ('L', 83.0, -11.0), ('L', 82.6, -7.6), ('L', 81.0, -7.6)), 1.4, 'steel', bevel=.06)
    flash_hider(100.0, BZ, 1.0, 3.6)
    bipod_folded(81.0, 96.0, -16.6)
    return place(36.0, 22.0, {'bore_z': BZ, 'muzzle': (103.8, BZ), 'optic': (40.0, -6.6), 'mag': (51.0, -23.0)})


@gun
def m60():
    """Free Fire M60: big butt and neck, receiver with feed cover, tall rear sight, grip,
    thick handguard, gas cylinder, long barrel with front sight."""
    BZ = -10.4
    st = path(('M', 0.0, -6.4), ('L', 21.0, -7.4), ('L', 30.0, -8.0), ('L', 30.0, -12.6), ('L', 21.0, -14.6), ('L', 18.0, -19.4), ('L', 0.0, -19.4))
    extrude('Stock', st, 4.2, 'polymer', crown=.35, inset=.55, bevel=.16)
    rec = path(('M', 28.0, -6.0), ('L', 50.0, -6.0), ('L', 50.0, -13.6), ('L', 28.0, -13.6))
    extrude('Receiver', rec, 4.2, 'steel', bevel=.14, cuts=[cbox((2.1, 40.0, -10.0), (.5, 6.0, 1.6))])
    extrude('Feed cover', rrect(32.0, -6.1, 48.0, -4.8, .4), 4.0, 'steel', bevel=.1)
    extrude('Rear sight', path(('M', 44.0, -4.9), ('L', 46.0, -4.9), ('L', 45.6, -.6), ('L', 44.6, -.6)), 1.4, 'steel', bevel=.05)
    grip('Grip', 34.4, 30.0, 10.4, 3.0, 3.2, 'polymer', z_top=-13.6)
    guard(34.6, 40.0, -13.6, 2.4, 'steel')
    trigger(36.6, -13.6)
    hg = path(('M', 49.0, -7.4), ('L', 73.0, -8.2), ('L', 73.0, -15.4), ('L', 49.0, -15.8))
    extrude('Handguard', hg, 4.4, 'polymer', crown=.3, inset=.5, bevel=.14)
    lathe('Gas cylinder', [(0, 1.3), (22.0, 1.3)], 'steel', at=(0, 73.0, -12.8), seg=18, bevel=.03)
    lathe('Barrel', [(0, 1.25), (37.0, 1.0)], 'steel', at=(0, 73.0, BZ), seg=20, bevel=.03)
    extrude('Front sight', path(('M', 96.4, -9.6), ('L', 98.6, -9.6), ('L', 98.2, -3.4), ('L', 96.8, -3.4)), 1.0, 'steel', bevel=.05)
    flash_hider(105.6, BZ, 1.05, 4.2)
    bipod_folded(75.0, 94.0, -14.4)
    return place(33.0, 20.0, {'bore_z': BZ, 'muzzle': (110.0, BZ), 'optic': (40.0, -4.8), 'mag': (40.0, -16.0)})


@gun
def kord():
    """Free Fire Kord: skeleton frame stock, long receiver with exposed ammo belt, scope on a side mount,
    frame grip, very long barrel with front sight, carrying handle and big flash hider."""
    BZ = -14.0
    st = path(('M', 0.0, -8.6), ('L', 6.0, -8.6), ('L', 20.0, -11.0), ('L', 20.0, -14.0), ('L', 4.0, -20.4), ('L', 0.0, -20.4))
    extrude('Stock', st, 3.0, 'steel', bevel=.12, cuts=[cprism(path(('M', 2.4, -10.4), ('L', 5.6, -10.4), ('L', 17.4, -12.6), ('L', 4.4, -18.0), ('L', 2.4, -18.0)), 6, 'YZ', 0)])
    rec = path(('M', 19.0, -8.6), ('L', 70.0, -8.6), ('L', 72.0, -11.0), ('L', 72.0, -17.4), ('L', 19.0, -17.4))
    extrude('Receiver', rec, 4.4, 'steel', bevel=.16)
    for y in range(32, 48, 2):
        extrude('Belt round', rrect(y, -14.4, y + 1.0, -11.0, .4), 4.6, 'brass', bevel=.05, skin=False)
    gp = path(('M', 36.0, -17.4), ('L', 31.0, -17.4), ('L', 29.0, -26.6), ('L', 33.0, -27.0), ('L', 37.6, -21.0))
    extrude('Grip frame', gp, 2.4, 'steel', bevel=.12, cuts=[cprism(path(('M', 33.0, -19.0), ('L', 35.6, -19.4), ('L', 33.6, -24.4), ('L', 31.4, -24.6)), 6, 'YZ', 0)])
    grip('Grip', 31.6, 29.6, 9.0, 1.4, 3.0, 'polymer', z_top=-17.4)
    trigger(34.0, -17.4)
    lathe('Barrel', [(0, 1.5), (6.0, 1.3), (78.0, 1.1)], 'steel', at=(0, 70.0, BZ), seg=20, bevel=.03)
    for y in (88.0, 108.0):
        lathe('Barrel collar', [(-1.0, 1.7), (1.0, 1.7)], 'steel', at=(0, y, BZ), seg=20, bevel=.04)
    extrude('Front sight', path(('M', 130.6, -13.0), ('L', 133.0, -13.0), ('L', 132.6, -7.0), ('L', 131.0, -7.0)), 1.2, 'steel', bevel=.05)
    lathe('Flash hider', [(0, .8), (0, 1.75), (7.0, 1.75), (7.0, .8)], 'steel', at=(0, 148.0, BZ), seg=22, closed=True, bevel=.05,
          cuts=[cbox((0, 151.0 + i * 1.8, BZ + 1.6), (.4, .9, 1.4)) for i in range(3)])
    tube('Carry handle', [(0, 76.0, -12.6), (0, 78.0, -9.6), (0, 86.0, -9.6), (0, 88.0, -12.6)], .5, 'steel')
    extrude('Scope mount', rrect(28.0, -8.7, 44.0, -7.2, .3), 2.2, 'steel', bevel=.1)
    scope(24.0, 26.0, -4.6, tube_r=1.5, bell_r=2.1, eye_r=2.1, kind='steel')
    return place(34.0, 25.0, {'bore_z': BZ, 'muzzle': (155.0, BZ), 'optic': (36.0, -4.6), 'mag': (40.0, -13.0)})


@gun
def an94():
    """Free Fire AN94: folding stock, receiver with top rail, raked grip, curved magazine,
    handguard, offset barrel with the lower shroud, front sight, muzzle brake."""
    BZ = -12.4
    st = path(('M', 0.0, -11.0), ('L', 26.0, -11.0), ('L', 26.0, -14.6), ('L', 6.0, -15.6), ('L', 2.0, -18.6), ('L', 0.0, -18.4))
    extrude('Stock', st, 3.2, 'polymer', bevel=.14, cuts=[cprism(rrect(5.0, -14.4, 23.0, -12.2, .6), 6, 'YZ', 0)])
    rec = path(('M', 27.0, -8.8), ('L', 52.0, -8.8), ('L', 52.0, -16.6), ('L', 30.0, -16.6), ('L', 27.0, -14.0))
    extrude('Receiver', rec, 3.4, 'steel', bevel=.14, cuts=[cbox((1.7, 40.0, -11.0), (.5, 6.0, 1.4))])
    rail(30.0, 68.0, -8.2, 2.2, 'alu')
    grip('Grip', 30.6, 26.6, 7.4, 2.4, 3.0, 'polymer', z_top=-16.4)
    guard(30.8, 36.4, -16.6, 2.4, 'steel')
    trigger(32.8, -16.6)
    mg = path(('M', 40.0, -16.4), ('L', 47.6, -16.4), ('C', 48.0, -21.0, 49.6, -25.4, 52.4, -28.6), ('L', 45.0, -30.0), ('C', 42.4, -26.0, 40.6, -21.6, 40.0, -16.4), n=10)
    extrude('Magazine', mg, 2.8, 'polymer', bevel=.12)
    hg = path(('M', 52.0, -8.8), ('L', 68.0, -8.8), ('L', 68.0, -16.0), ('L', 52.0, -16.6))
    extrude('Handguard', hg, 3.8, 'polymer', bevel=.14)
    lathe('Barrel', [(0, 1.0), (22.0, .85)], 'steel', at=(0, 68.0, BZ), seg=18, bevel=.03)
    lathe('Lower shroud', [(0, .9), (14.0, .9)], 'steel', at=(0, 68.0, -14.6), seg=16, bevel=.03)
    extrude('Front sight', path(('M', 84.4, -11.6), ('L', 86.6, -11.6), ('L', 86.2, -7.4), ('L', 84.8, -7.4)), 1.2, 'steel', bevel=.05)
    lathe('Muzzle brake', [(0, .5), (0, 1.15), (4.0, 1.15), (4.0, .5)], 'steel', at=(0, 90.0, BZ), seg=20, closed=True, bevel=.04,
          cuts=[cbox((0, 92.0, BZ + 1.0), (.4, 2.0, 1.2))])
    return place(29.0, 20.0, {'bore_z': BZ, 'muzzle': (94.0, BZ), 'optic': (40.0, -8.2), 'mag': (45.0, -24.0)})


@gun
def m416():
    """Free Fire M416: collapsible stock, flat-top receiver with rail, quad-rail handguard,
    folding sights, raked grip, curved 30-round magazine, flash hider."""
    BZ = -7.4
    st = path(('M', 0.0, -5.4), ('L', 17.0, -5.8), ('L', 17.0, -9.6), ('L', 8.0, -11.6), ('L', 2.0, -15.4), ('L', 0.0, -15.0))
    extrude('Stock', st, 3.4, 'polymer', crown=.25, inset=.4, bevel=.14)
    lathe('Buffer tube', [(0, 1.3), (6.0, 1.3)], 'alu', at=(0, 15.0, -7.4), seg=18, bevel=.03)
    up = path(('M', 20.0, -2.6), ('L', 45.0, -2.6), ('L', 45.0, -8.6), ('L', 20.0, -8.6))
    extrude('Upper receiver', up, 2.6, 'alu', bevel=.12, cuts=[cbox((1.3, 34.0, -6.2), (.5, 6.0, 1.4))])
    low = path(('M', 20.6, -8.5), ('L', 44.0, -8.5), ('L', 44.0, -11.4), ('L', 42.6, -13.6), ('L', 35.0, -13.6), ('L', 35.0, -11.4), ('L', 26.0, -11.4), ('L', 20.6, -10.0))
    extrude('Lower receiver', low, 2.6, 'alu', bevel=.12)
    rail(20.6, 64.0, -2.0, 2.2, 'alu')
    grip('Grip', 27.0, 22.8, 11.4, 3.6, 3.0, 'polymer', z_top=-11.0)
    guard(27.2, 33.4, -11.2, 2.4, 'alu')
    trigger(29.4, -11.2)
    mg = path(('M', 35.4, -13.0), ('L', 42.2, -13.0), ('C', 42.6, -18.0, 43.4, -22.6, 44.6, -26.4), ('L', 37.8, -27.4), ('C', 36.8, -22.6, 35.8, -18.0, 35.4, -13.0), n=10)
    extrude('Magazine', mg, 2.5, 'alu', bevel=.12)
    hg = path(('M', 45.0, -3.4), ('L', 64.0, -3.4), ('L', 64.0, -10.4), ('L', 45.0, -10.4))
    extrude('Handguard', hg, 3.4, 'alu', bevel=.14)
    for z in (-6.9,):
        rail_side = extrude('Side rail', rrect(46.0, z - .7, 63.0, z + .7, .1), 3.8, 'alu', bevel=.03)
    for y in range(47, 63, 2):
        extrude('Rail slot', rrect(y, -7.7, y + 1.0, -6.1, .1), 3.9, 'alu', bevel=0)
    extrude('Front sight', path(('M', 64.6, -2.0), ('L', 66.6, -2.0), ('L', 66.2, -.2), ('L', 65.0, -.2)), 1.2, 'polymer', bevel=.05)
    extrude('Rear sight', path(('M', 21.4, -2.0), ('L', 23.8, -2.0), ('L', 23.4, -.4), ('L', 21.8, -.4)), 1.6, 'polymer', bevel=.05)
    lathe('Barrel', [(0, .9), (19.0, .8)], 'steel', at=(0, 64.0, BZ), seg=18, bevel=.03)
    flash_hider(83.0, BZ, .95, 3.6)
    return place(24.0, 17.0, {'bore_z': BZ, 'muzzle': (86.8, BZ), 'optic': (34.0, -2.0), 'mag': (40.0, -20.0)})


@gun
def spas12():
    """Free Fire SPAS-12: pistol grip, receiver with ventilated heat shield on top, big ribbed pump forend,
    barrel over the magazine tube, front sight, muzzle."""
    BZ = -9.6
    gp = path(('M', 9.6, -12.0), ('L', 2.4, -10.0), ('C', 1.0, -16.0, .2, -22.0, 0.0, -28.0), ('Q', 0, -30.0, 2.0, -30.0), ('L', 6.0, -30.0),
              ('Q', 7.4, -30.0, 7.2, -28.4), ('C', 7.4, -22.0, 8.2, -16.6, 9.6, -12.0))
    extrude('Grip', gp, 3.6, 'polymer', crown=.35, inset=.5, bevel=.14)
    rec = path(('M', 5.0, -5.0), ('L', 45.0, -5.0), ('L', 45.0, -14.6), ('L', 12.0, -15.0), ('L', 5.0, -12.0))
    extrude('Receiver', rec, 4.4, 'steel', bevel=.16, cuts=[cbox((2.2, 28.0, -9.0), (.5, 9.0, 2.4))])
    hs = path(('M', 18.0, -2.6), ('L', 52.0, -2.6), ('L', 52.0, -5.2), ('L', 18.0, -5.2))
    extrude('Heat shield', hs, 3.6, 'steel', bevel=.12, cuts=[ccyl((0, 22.0 + i * 3.6, -3.9), 1.0, 6, 'X', 14) for i in range(7)])
    guard(10.6, 18.0, -14.8, 3.0, 'steel', width=1.4)
    trigger(13.0, -14.8)
    fe = path(('M', 32.0, -8.6), ('L', 72.0, -8.6), ('Q', 73.4, -8.8, 73.4, -10.4), ('L', 73.4, -15.0), ('Q', 73.0, -16.4, 71.4, -16.4), ('L', 32.0, -16.4))
    extrude('Pump', fe, 5.0, 'polymer', crown=.4, inset=.5, bevel=.16)
    for y in range(35, 71, 3):
        extrude('Pump rib', rrect(y, -15.8, y + 1.2, -9.2, .3), 5.25, 'polymer', bevel=.08)
    lathe('Barrel', [(0, 1.5), (53.0, 1.4)], 'steel', at=(0, 45.0, BZ), seg=22, bevel=.03)
    lathe('Muzzle', [(0, 1.0), (0, 1.6), (2.0, 1.6), (2.0, 1.0)], 'steel', at=(0, 97.6, BZ), seg=22, closed=True, bevel=.04)
    lathe('Mag tube', [(0, 1.25), (26.0, 1.25)], 'steel', at=(0, 72.0, -12.6), seg=20, bevel=.03)
    extrude('Barrel clamp', rrect(92.0, -14.4, 94.4, -7.6, .6), 3.6, 'steel', bevel=.1)
    extrude('Front sight', path(('M', 88.4, -8.2), ('L', 91.0, -8.2), ('L', 90.6, -5.6), ('L', 88.8, -5.6)), 1.2, 'steel', bevel=.05)
    return place(5.0, 22.0, {'bore_z': BZ, 'muzzle': (99.6, BZ), 'optic': (30.0, -2.6), 'mag': (85.0, -12.6)})
