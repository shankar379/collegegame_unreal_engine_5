"""Weapon Library V3 - batch 8: guns with no picture in the user's references, built from their
well-known real designs in the same style (Bizon, MAC10, MAG7, M870, AA12, Hand Cannon, Gatling)
plus an original sci-fi Railgun. Reference-style coordinates (y from the rear, z below the top)."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_batch2 import rail, grip, trigger, guard
from wv3_guns_batch3 import flash_hider
from wv3_guns_batch4 import place


@gun
def bizon():
    """PP-19 Bizon: AK-style receiver and dust cover, folding stock, helical magazine under the handguard."""
    st = path(('M', 0.0, -3.4), ('L', 22.0, -3.6), ('L', 22.0, -6.4), ('L', 4.0, -8.6), ('L', 1.6, -14.0), ('L', 0.0, -14.0))
    extrude('Stock', st, 2.6, 'steel', bevel=.12, cuts=[cprism(path(('M', 2.6, -5.0), ('L', 19.6, -5.0), ('L', 4.0, -7.0), ('L', 2.6, -11.0)), 6, 'YZ', 0)])
    rec = path(('M', 21.0, -3.0), ('L', 46.0, -3.0), ('L', 46.0, -9.0), ('L', 21.0, -9.0))
    extrude('Receiver', rec, 3.2, 'steel', bevel=.12, cuts=[cbox((1.6, 36.0, -4.6), (.5, 7.0, 1.2))])
    extrude('Dust cover', arc(0, -3.1, 1.65, 180, 0, 14), 24.0, 'steel', plane='XZ', at=33.0, bevel=.08)
    grip('Grip', 30.0, 26.0, 9.6, 2.8, 2.8, 'polymer', z_top=-9.0)
    guard(30.4, 36.0, -9.0, 2.4, 'steel')
    trigger(32.4, -9.0)
    lathe('Helical magazine', [(0, 2.0), (.6, 2.6), (24.0, 2.6), (24.6, 2.0)], 'polymer', at=(0, 44.0, -11.6), seg=28, bevel=.06)
    for i in range(10):
        lathe('Mag ridge', [(-.2, 2.7), (.2, 2.7)], 'polymer', at=(0, 46.0 + i * 2.2, -11.6), seg=28, bevel=.02)
    extrude('Handguard', rrect(46.0, -8.6, 66.0, -3.4, .6), 3.4, 'polymer', bevel=.14)
    lathe('Barrel', [(0, .8), (8.0, .75)], 'steel', at=(0, 66.0, -5.6), seg=16, bevel=.03)
    lathe('Muzzle brake', [(0, .5), (0, 1.0), (2.6, 1.0), (2.6, .5)], 'steel', at=(0, 74.0, -5.6), seg=18, closed=True, bevel=.03)
    extrude('Front sight', path(('M', 70.0, -5.0), ('L', 72.0, -5.0), ('L', 71.6, -2.0), ('L', 70.4, -2.0)), 1.0, 'steel', bevel=.04)
    return place(28.0, 14.0, {'bore_z': -5.6, 'muzzle': (76.6, -5.6), 'optic': (34.0, -1.4), 'mag': (56.0, -11.6)})


@gun
def mac10():
    """MAC-10: boxy stamped receiver, magazine in the grip, folded wire stock, threaded barrel with suppressor."""
    rec = path(('M', 0.0, -1.0), ('L', 26.0, -1.0), ('L', 26.0, -8.0), ('L', 0.0, -8.0))
    extrude('Receiver', rec, 4.4, 'blued', bevel=.16, cuts=[cbox((0, 14.0, -.8), (1.0, 12.0, .8))])
    extrude('Bolt handle', rrect(13.0, -1.2, 15.0, .4, .3), 1.6, 'steel', bevel=.06)
    gp = path(('M', 11.0, -7.8), ('L', 4.6, -7.8), ('L', 4.0, -22.0), ('L', 10.6, -22.0))
    extrude('Grip', gp, 3.6, 'polymer', crown=.2, inset=.3, bevel=.14,
            cuts=[cbox((0, 7.5, -15.3), (2.4, 4.6, 15.0))])
    extrude('Magazine', rrect(5.4, -22.0, 9.6, -8.2, .2), 2.2, 'blued', bevel=.06)
    extrude('Magazine base', rrect(3.8, -23.0, 10.8, -21.8, .3), 3.4, 'blued', bevel=.08)
    guard(11.4, 17.0, -7.9, 2.6, 'blued')
    trigger(13.2, -7.9)
    for x in (-1.8, 1.8):
        tube('Folded stock', [(x, 1.0, -8.4), (x, 24.0, -8.8)], .35, 'blued', smooth=False)
    tube('Stock butt', [(-1.8, 24.0, -8.8), (1.8, 24.0, -8.8)], .4, 'blued', smooth=False)
    lathe('Barrel', [(0, .7), (3.0, .7)], 'steel', at=(0, 26.0, -3.4), seg=16, bevel=.02)
    lathe('Suppressor', [(0, 1.4), (.4, 1.8), (16.0, 1.8), (16.4, 1.4)], 'blued', at=(0, 29.0, -3.4), seg=24, bevel=.05)
    return place(8.0, 13.0, {'bore_z': -3.4, 'muzzle': (45.4, -3.4), 'optic': (12.0, -1.0), 'mag': (7.4, -15.0)})


@gun
def mag7():
    """MAG-7: compact pump shotgun, box magazine in the pistol grip, ribbed pump, short barrel, folding stock."""
    st = path(('M', 0.0, -4.0), ('L', 18.0, -4.0), ('L', 18.0, -7.0), ('L', 2.0, -7.0), ('L', 1.4, -13.0), ('L', 0.0, -13.0))
    extrude('Stock', st, 2.6, 'steel', bevel=.12)
    rec = path(('M', 17.0, -1.6), ('L', 40.0, -1.6), ('L', 40.0, -9.6), ('L', 17.0, -9.6))
    extrude('Receiver', rec, 4.6, 'polymer', bevel=.18)
    gp = path(('M', 28.0, -9.4), ('L', 20.0, -9.4), ('L', 19.4, -24.0), ('L', 27.4, -24.0))
    extrude('Grip magazine well', gp, 4.4, 'polymer', crown=.25, inset=.4, bevel=.16,
            cuts=[cbox((0, 23.7, -17.0), (3.2, 5.6, 15.0))])
    extrude('Magazine', rrect(21.1, -26.0, 26.3, -10.0, .3), 3.0, 'steel', bevel=.1)
    extrude('Mag floorplate', rrect(20.0, -26.6, 27.0, -25.6, .3), 3.8, 'steel', bevel=.08)
    guard(28.4, 34.0, -9.6, 2.6, 'polymer')
    trigger(30.4, -9.6)
    fe = rrect(40.0, -10.6, 56.0, -3.6, 1.2)
    extrude('Pump', fe, 5.2, 'polymer', bevel=.16)
    for y in range(41, 56, 2):
        extrude('Pump rib', rrect(y, -10.2, y + .9, -4.0, .3), 5.45, 'polymer', bevel=.06)
    lathe('Barrel', [(0, 1.5), (22.0, 1.45)], 'steel', at=(0, 40.0, -3.4), seg=22, bevel=.03)
    lathe('Muzzle', [(0, 1.0), (0, 1.6), (1.4, 1.6), (1.4, 1.0)], 'steel', at=(0, 62.0, -3.4), seg=22, closed=True, bevel=.03)
    return place(25.0, 16.0, {'bore_z': -3.4, 'muzzle': (63.4, -3.4), 'optic': (28.0, -1.6), 'mag': (23.5, -18.0)})


@gun
def m870():
    """Remington 870: walnut stock and pump, black receiver, barrel over the magazine tube, bead sight."""
    st = path(('M', 0.0, -3.6), ('L', 30.0, -2.6), ('L', 33.0, -4.0), ('L', 33.0, -8.4), ('L', 30.0, -8.8), ('C', 22.0, -12.0, 10.0, -15.0, 2.0, -17.0),
              ('L', 0.0, -17.0))
    extrude('Stock', st, 3.8, 'wood', crown=.4, inset=.6, bevel=.15)
    extrude('Butt pad', rrect(-1.6, -17.2, .2, -3.4, .5), 4.0, 'rubber', bevel=.12)
    rec = path(('M', 32.0, -1.2), ('L', 54.0, -1.2), ('L', 54.0, -8.6), ('L', 32.0, -8.6))
    extrude('Receiver', rec, 3.8, 'blued', bevel=.16, cuts=[cbox((1.9, 44.0, -4.6), (.5, 7.0, 2.0))])
    guard(36.0, 42.6, -8.6, 2.8, 'blued')
    trigger(38.6, -8.6)
    fe = path(('M', 58.0, -5.2), ('L', 76.0, -5.2), ('L', 76.0, -10.4), ('L', 58.0, -10.4))
    extrude('Pump', fe, 4.4, 'wood', crown=.4, inset=.5, bevel=.14)
    for y in range(60, 75, 2):
        extrude('Pump groove', rrect(y, -10.0, y + .7, -5.6, .3), 4.55, 'wood_dark', bevel=.04)
    lathe('Barrel', [(0, 1.45), (46.0, 1.35)], 'blued', at=(0, 54.0, -3.0), seg=22, bevel=.03)
    lathe('Mag tube', [(0, 1.3), (36.0, 1.3), (36.6, 1.0)], 'blued', at=(0, 54.0, -7.4), seg=20, bevel=.03)
    lathe('Bead', [(0, .3), (.4, .3), (.5, .1)], 'brass', at=(0, 98.6, -1.6), axis='Z', seg=10, bevel=0)
    lathe('Muzzle', [(0, .95), (0, 1.48), (1.0, 1.48), (1.0, .95)], 'blued', at=(0, 99.6, -3.0), seg=22, closed=True, bevel=.03)
    return place(36.0, 14.0, {'bore_z': -3.0, 'muzzle': (100.6, -3.0), 'optic': (44.0, -1.2), 'mag': (70.0, -7.4)})


@gun
def aa12():
    """AA-12: boxy full-auto shotgun, carry handle with rail, drum magazine, fixed stock, ventilated barrel shroud."""
    st = path(('M', 0.0, -3.0), ('L', 22.0, -3.0), ('L', 22.0, -9.0), ('L', 6.0, -10.0), ('L', 2.0, -17.0), ('L', 0.0, -17.0))
    extrude('Stock', st, 3.6, 'polymer', crown=.3, inset=.5, bevel=.16)
    rec = path(('M', 21.0, -2.0), ('L', 66.0, -2.0), ('L', 66.0, -12.0), ('L', 21.0, -12.0))
    extrude('Receiver', rec, 4.8, 'polymer', bevel=.2)
    extrude('Carry handle', path(('M', 26.0, -2.1), ('L', 28.0, 2.0), ('L', 52.0, 2.0), ('L', 54.0, -2.1)), 2.6, 'alu', bevel=.12,
            cuts=[cprism(rrect(30.0, -1.6, 50.0, .8, .6), 4, 'YZ', 0)])
    rail(28.0, 52.0, 2.6, 2.2, 'alu')
    grip('Grip', 32.0, 27.6, 10.0, 2.6, 3.2, 'polymer', z_top=-12.0)
    guard(32.4, 38.0, -12.0, 2.6, 'polymer')
    trigger(34.4, -12.0)
    lathe('Drum magazine', [(-2.2, 6.6), (-1.6, 7.2), (1.6, 7.2), (2.2, 6.6)], 'polymer', at=(0, 47.0, -17.0), axis='X', seg=36, bevel=.08)
    lathe('Drum cap', [(-2.4, 2.4), (2.4, 2.4)], 'alu', at=(0, 47.0, -17.0), axis='X', seg=24, bevel=.06)
    lathe('Barrel shroud', [(0, 2.0), (24.0, 2.0)], 'polymer', at=(0, 66.0, -6.6), seg=24, bevel=.05,
          cuts=[ccyl((0, 69.0 + i * 3.0, -5.2), .6, 6, 'X', 12) for i in range(7)])
    lathe('Muzzle', [(0, 1.0), (0, 1.6), (2.0, 1.6), (2.0, 1.0)], 'steel', at=(0, 90.0, -6.6), seg=22, closed=True, bevel=.04)
    return place(30.0, 18.0, {'bore_z': -6.6, 'muzzle': (92.0, -6.6), 'optic': (40.0, 2.6), 'mag': (47.0, -17.0)})


@gun
def handcannon():
    """Hand Cannon: oversized heavy pistol with a ported barrel shroud, black slide with gold inlays,
    big hammer and a wide wood grip."""
    sl = path(('M', 0.0, -6.0), ('L', 0.0, -1.0), ('L', 1.0, 0.0), ('L', 30.0, 0.0), ('L', 32.0, -1.4), ('L', 32.0, -5.2), ('L', 30.0, -6.0))
    cuts = [cbox((s_ * 1.85, 2.0 + i * .8, -3.2), (.4, .3, 3.6)) for s_ in (1, -1) for i in range(6)]
    cuts += [ccyl((0, 20.0 + i * 3.2, -.2), .7, 1.2, 'Z', 12) for i in range(3)]
    extrude('Slide', sl, 3.8, 'blued', bevel=.14, cuts=cuts)
    for s_ in (1, -1):
        extrude('Gold inlay', band([(9.0, -2.6), (28.0, -2.6)], .5), .3, 'brass', at=s_ * 1.95, bevel=.03)
    lathe('Bore', [(0, .9), (.25, .9)], 'bore', at=(0, 31.9, -3.0), seg=18, bevel=0)
    fr = path(('M', 1.0, -6.0), ('L', 22.0, -6.0), ('L', 22.0, -8.0), ('L', 4.0, -8.4), ('L', 1.0, -7.4))
    extrude('Frame', fr, 3.4, 'blued', bevel=.12)
    guard(10.0, 17.6, -8.0, 3.2, 'blued', width=1.3)
    trigger(12.4, -8.0)
    extrude('Hammer', path(('M', .6, -1.4), ('L', 1.6, -2.4), ('L', 1.0, -4.4), ('L', -.8, -3.6), ('L', -1.6, -1.4), ('L', -.4, -.8)), 1.0, 'blued', bevel=.05)
    gp = path(('M', 9.6, -7.8), ('L', 1.8, -8.0), ('C', .6, -11.0, .2, -15.0, .4, -18.0), ('Q', .6, -19.2, 2.0, -19.2), ('L', 7.6, -19.0),
              ('Q', 8.8, -18.8, 8.6, -17.6), ('C', 8.2, -14.6, 8.6, -10.6, 9.6, -7.8))
    extrude('Grip', gp, 3.6, 'wood', crown=.35, inset=.5, bevel=.14)
    lathe('Grip medallion', [(0, .8), (.15, .8)], 'brass', at=(1.8, 4.6, -13.6), axis='X', seg=18, bevel=0)
    lathe('Grip medallion', [(0, .8), (-.15, .8)], 'brass', at=(-1.8, 4.6, -13.6), axis='X', seg=18, bevel=0)
    return place(5.0, 12.0, {'bore_z': -3.0, 'muzzle': (32.0, -3.0), 'optic': (14.0, 0.0), 'mag': (4.6, -13.0)})


@gun
def gatling():
    """Gatling / minigun: motor housing, six-barrel rotating cluster with clamps, top and side handles,
    belt-fed ammo box."""
    lathe('Motor housing', [(0, 4.0), (.8, 4.8), (20.0, 4.8), (21.0, 4.2)], 'steel', at=(0, 0.0, -8.0), seg=32, bevel=.08)
    extrude('Housing cap', rrect(-3.0, -12.0, .4, -4.0, .8), 7.0, 'steel', bevel=.14)
    for k in range(6):
        a = k * math.pi / 3
        lathe('Barrel', [(0, .9), (52.0, .9)], 'steel', at=(math.cos(a) * 2.6, 21.0, -8.0 + math.sin(a) * 2.6), seg=14, bevel=.02)
        lathe('Muzzle', [(0, .55), (0, 1.0), (1.0, 1.0), (1.0, .55)], 'steel', at=(math.cos(a) * 2.6, 73.0, -8.0 + math.sin(a) * 2.6), seg=14, closed=True, bevel=.02)
    for y in (32.0, 56.0, 70.0):
        lathe('Barrel clamp', [(-.8, 4.0), (.8, 4.0)], 'steel', at=(0, y, -8.0), seg=28, bevel=.05)
    lathe('Spindle', [(0, 1.2), (52.0, 1.2)], 'blued', at=(0, 21.0, -8.0), seg=16, bevel=.02)
    tube('Top handle', [(0, 2.0, -3.4), (0, 4.0, 1.0), (0, 14.0, 1.0), (0, 16.0, -3.4)], .7, 'polymer')
    grip('Grip', 6.0, 2.0, 10.0, 2.0, 3.2, 'polymer', z_top=-12.4)
    trigger(7.4, -12.6)
    extrude('Ammo box', rrect(4.0, -26.0, 18.0, -14.0, .8), 7.0, 'polymer_green', bevel=.16)
    tube('Ammo belt', [(0, 16.0, -14.0), (0, 19.0, -12.0), (0, 21.0, -11.0)], .9, 'brass')
    return place(4.0, 22.0, {'bore_z': -8.0, 'muzzle': (74.0, -8.0), 'optic': (9.0, 1.0), 'mag': (11.0, -20.0)})


@gun
def railgun():
    """Original sci-fi railgun: white ceramic body, two long parallel rails with glowing coils between,
    black grip and stock, red trim, capacitor bank."""
    st = path(('M', 0.0, -4.0), ('L', 22.0, -5.0), ('L', 26.0, -9.0), ('L', 24.0, -16.0), ('L', 4.0, -14.0), ('L', 0.0, -12.0))
    extrude('Stock', st, 3.4, 'polymer', bevel=.16, cuts=[cprism(rrect(6.0, -12.6, 18.0, -7.0, 2.4), 6, 'YZ', 0)])
    body = path(('M', 20.0, -2.0), ('L', 52.0, -2.6), ('Q', 55.0, -3.0, 55.0, -6.0), ('L', 55.0, -12.0), ('L', 26.0, -12.0), ('L', 20.0, -7.0))
    extrude('Body', body, 5.0, 'ceramic', crown=.5, inset=.7, bevel=.2)
    extrude('Red trim', band([(24.0, -9.4), (54.0, -9.4)], .7), 5.15, 'ceramic_red', bevel=.06)
    extrude('Capacitors', rrect(30.0, -11.4, 46.0, -9.9, .5), 5.2, 'glow_cyan', bevel=.05)
    grip('Grip', 30.0, 26.0, 9.0, 2.4, 3.2, 'polymer', z_top=-12.0)
    trigger(32.0, -12.0)
    for z in (-3.6, -9.0):
        extrude('Rail', rrect(54.0, z - .9, 100.0, z + .9, .3), 2.4, 'steel', bevel=.08)
    for i in range(10):
        lathe('Coil', [(-.6, 1.6), (.6, 1.6)], 'glow_cyan', at=(0, 58.0 + i * 4.2, -6.3), seg=18, bevel=.02)
    for y in (60.0, 80.0, 98.0):
        extrude('Rail brace', rrect(y - .8, -10.4, y + .8, -2.2, .3), 3.0, 'ceramic', bevel=.08)
    lathe('Scope', [(0, 1.0), (.4, 1.3), (14.0, 1.3), (14.0, 1.1)], 'ceramic', at=(0, 30.0, 1.0), seg=22, bevel=.04)
    lathe('Scope lens', [(0, 1.0), (.06, 1.0)], 'glow_red', at=(0, 44.0, 1.0), seg=20, bevel=0)
    extrude('Scope mount', rrect(34.0, -2.4, 40.0, -.2, .4), 2.0, 'polymer', bevel=.08)
    return place(28.0, 20.0, {'bore_z': -6.3, 'muzzle': (100.0, -6.3), 'optic': (37.0, 1.0), 'mag': (38.0, -10.6)})
