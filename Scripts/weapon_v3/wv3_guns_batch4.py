"""Weapon Library V3 - batch 4: marksman / heavy rifles (SVD, VSS, M14, SKS, Woodpecker, M82).

These are written directly in REFERENCE coordinates, measured off the Free Fire pictures with
compare/ref_profile.py: y = cm from the butt (muzzle at +y), z = cm below the top of the gun (negative).
`place(gy, gz)` then moves the gun so the grip sits at the origin and the bore is at a positive height."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_rifles import shift_all
from wv3_guns_batch2 import rail, grip, trigger, guard
from wv3_guns_snipers import scope, ring_mount
from wv3_guns_batch3 import flash_hider


def place(gy, gz, meta):
    shift_all(-gy, gz)
    out = {'bore_z': meta['bore_z'] + gz}
    for k in ('muzzle', 'optic', 'mag'):
        out[k] = (meta[k][0] - gy, meta[k][1] + gz)
    return out


@gun
def svd():
    """Free Fire SVD: skeleton thumbhole wood stock with cheek rest, black receiver, box magazine,
    long wood handguard, PSO scope on a side mount, flash hider."""
    BZ = -9.6
    st = path(('M', 0.0, -9.6), ('L', 6.0, -9.2), ('L', 22.0, -9.0), ('L', 31.0, -8.8), ('L', 31.0, -13.4), ('L', 34.0, -14.0), ('L', 35.0, -20.0),
              ('Q', 34.6, -22.8, 32.0, -22.6), ('L', 2.0, -23.0), ('Q', 0, -23.0, 0, -21.0))
    hole = cprism(path(('M', 4.6, -12.2), ('L', 25.0, -12.4), ('Q', 28.0, -12.6, 29.0, -15.6), ('L', 30.4, -19.4), ('L', 4.6, -20.0)), 8, 'YZ', 0)
    extrude('Stock', st, 3.6, 'wood', crown=.35, inset=.5, bevel=.14, cuts=[hole])
    extrude('Cheek rest', rrect(6.0, -9.4, 22.0, -7.4, .8), 3.2, 'polymer', bevel=.14)
    extrude('Butt plate', rrect(-1.0, -23.2, .4, -9.4, .4), 3.8, 'rubber', bevel=.1)
    rec = path(('M', 30.0, -7.6), ('L', 60.0, -7.6), ('L', 60.0, -12.6), ('L', 33.0, -13.4), ('L', 30.0, -12.6))
    extrude('Receiver', rec, 3.2, 'steel', bevel=.14, cuts=[cbox((1.6, 47.0, -9.4), (.5, 7.0, 1.4))])
    lathe('Charging handle', [(0, .35), (1.4, .35), (1.6, .6), (2.2, .6)], 'steel', at=(1.5, 52.0, -9.2), axis='X', seg=12, bevel=0)
    guard(37.0, 44.0, -13.2, 3.4, 'steel', width=1.2)
    trigger(39.6, -13.2)
    extrude('Magazine', path(('M', 46.4, -13.0), ('L', 54.4, -13.0), ('L', 55.6, -23.6), ('L', 47.6, -24.2)), 2.8, 'steel', bevel=.12)
    for side in (1, -1):
        extrude('Mag rib', band([(50.0, -14.0), (51.2, -23.0)], .7), .3, 'steel', at=side * 1.45, bevel=.06, skin=True)
    hg = path(('M', 60.0, -7.0), ('L', 94.0, -7.6), ('Q', 96.0, -7.8, 96.0, -9.4), ('L', 96.0, -10.4), ('Q', 95.6, -11.6, 94.0, -11.6), ('L', 60.0, -12.8))
    extrude('Handguard', hg, 4.0, 'wood', crown=.45, inset=.55, bevel=.14)
    for y in range(64, 92, 6):
        extrude('Vent slot', rrect(y, -10.8, y + 3.0, -9.4, .5), 4.15, 'steel', bevel=.04)
    lathe('Gas tube', [(0, .8), (4.0, .8)], 'steel', at=(0, 95.6, -8.4), seg=16, bevel=.03)
    lathe('Barrel', [(0, 1.05), (16.0, .85)], 'blued', at=(0, 96.0, BZ), seg=18, bevel=.03)
    extrude('Front sight', path(('M', 103.6, -9.0), ('L', 105.4, -9.0), ('L', 105.0, -6.6), ('L', 104.0, -6.6)), 1.4, 'steel', bevel=.06)
    flash_hider(108.0, BZ, 1.0, 4.4)
    extrude('Scope mount', rrect(38.0, -7.7, 46.0, -5.2, .3), 2.2, 'steel', bevel=.1)
    scope(22.0, 40.0, -3.0, tube_r=1.6, bell_r=2.2, eye_r=2.4, kind='steel')
    return place(36.0, 18.0, {'bore_z': BZ, 'muzzle': (112.6, BZ), 'optic': (42.0, -3.0), 'mag': (51.0, -18.0)})


@gun
def vss():
    """Free Fire VSS: skeleton wood stock with two openings, green-grey receiver, fat integral suppressor,
    PSO scope."""
    BZ = -10.4
    st = path(('M', 0.0, -10.6), ('L', 33.0, -9.8), ('L', 35.0, -13.4), ('L', 35.0, -21.0), ('Q', 34.6, -23.0, 32.0, -23.0), ('L', 2.0, -23.2), ('Q', 0, -23.2, 0, -21.2))
    holes = [cprism(rrect(3.4, -20.4, 13.6, -12.8, 1.4), 8, 'YZ', 0), cprism(rrect(16.6, -20.4, 27.6, -12.6, 1.4), 8, 'YZ', 0)]
    extrude('Stock', st, 3.4, 'wood', crown=.3, inset=.45, bevel=.14, cuts=holes)
    rec = path(('M', 32.0, -7.8), ('L', 56.0, -7.8), ('L', 56.0, -14.0), ('L', 34.0, -14.6), ('L', 32.0, -13.0))
    extrude('Receiver', rec, 3.2, 'steel', bevel=.14, cuts=[cbox((1.6, 45.0, -10.0), (.5, 6.0, 1.4))])
    guard(37.4, 43.6, -14.4, 2.8, 'steel', width=1.2)
    trigger(39.8, -14.4)
    extrude('Magazine', rrect(46.0, -21.0, 52.0, -14.0, .4), 2.6, 'steel', bevel=.1)
    lathe('Suppressor', [(0, 1.6), (.6, 2.15), (33.0, 2.15), (33.6, 1.7), (33.6, .6)], 'blued', at=(0, 56.0, BZ), seg=28, bevel=.05)
    for y in (60.0, 72.0, 84.0):
        lathe('Suppressor ring', [(-.3, 2.25), (.3, 2.25)], 'blued', at=(0, y, BZ), seg=28, bevel=.03)
    extrude('Front sight', path(('M', 86.4, -8.4), ('L', 88.4, -8.4), ('L', 88.0, -6.6), ('L', 86.8, -6.6)), 1.2, 'steel', bevel=.05)
    extrude('Scope mount', rrect(37.0, -7.9, 47.0, -5.4, .3), 2.2, 'steel', bevel=.1)
    scope(22.0, 33.0, -3.2, tube_r=1.4, bell_r=1.9, eye_r=2.1, kind='steel')
    return place(38.0, 20.0, {'bore_z': BZ, 'muzzle': (89.6, BZ), 'optic': (40.0, -3.2), 'mag': (49.0, -17.0)})


@gun
def m14():
    """Free Fire M14 (EBR style): tan collapsible stock on a buffer tube, black chassis with top rail,
    pistol grip, straight box magazine, railed handguard, long barrel with front sight and flash hider."""
    BZ = -6.6
    st = path(('M', 0.0, -5.0), ('L', 17.0, -5.2), ('L', 17.0, -9.6), ('L', 6.0, -12.4), ('L', 1.6, -16.4), ('L', 0.0, -16.0))
    extrude('Stock', st, 3.4, 'polymer_tan', crown=.25, inset=.4, bevel=.14, cuts=[cprism(rrect(3.0, -11.0, 12.0, -7.4, 1.0), 6, 'YZ', 0)])
    extrude('Butt pad', rrect(-1.0, -16.6, .4, -4.8, .4), 3.6, 'rubber', bevel=.1)
    lathe('Buffer tube', [(0, 1.3), (12.0, 1.3)], 'alu', at=(0, 14.0, -6.8), seg=18, bevel=.03)
    ch = path(('M', 24.0, -3.4), ('L', 52.0, -3.4), ('L', 52.0, -10.0), ('L', 30.0, -10.6), ('L', 26.0, -9.6), ('L', 24.0, -8.0))
    extrude('Chassis', ch, 3.4, 'alu', bevel=.14, cuts=[cbox((1.7, 40.0, -6.0), (.5, 6.0, 1.4))])
    rail(25.0, 84.0, -2.8, 2.2, 'alu')
    grip('Grip', 31.6, 27.0, 9.4, 3.0, 3.0, 'polymer', z_top=-9.4)
    guard(32.0, 37.6, -9.6, 2.4, 'alu')
    trigger(34.0, -9.6)
    extrude('Magazine', path(('M', 45.0, -10.0), ('L', 52.4, -10.0), ('L', 52.8, -19.4), ('L', 45.4, -19.4)), 2.8, 'steel', bevel=.12)
    hg = path(('M', 52.0, -3.4), ('L', 84.0, -3.4), ('L', 84.0, -9.0), ('L', 52.0, -9.6))
    extrude('Handguard', hg, 3.8, 'alu', bevel=.14)
    for y in range(55, 82, 4):
        extrude('Handguard slot', rrect(y, -7.6, y + 2.4, -5.0, .5), 3.95, 'alu', bevel=0)
    lathe('Barrel', [(0, 1.0), (22.0, .85)], 'steel', at=(0, 84.0, BZ), seg=18, bevel=.03)
    extrude('Front sight', path(('M', 99.6, -6.0), ('L', 101.6, -6.0), ('L', 101.2, -2.4), ('L', 100.0, -2.4)), 1.2, 'steel', bevel=.05)
    flash_hider(106.0, BZ, .95, 3.6)
    return place(30.0, 16.0, {'bore_z': BZ, 'muzzle': (109.8, BZ), 'optic': (38.0, -2.4), 'mag': (49.0, -15.0)})


@gun
def sks():
    """Free Fire SKS (tactical): wood buttstock, black receiver and long handguard, big vertical
    pistol grip, front sight, short barrel."""
    BZ = -7.0
    st = path(('M', 0.0, -6.6), ('L', 25.0, -4.4), ('L', 27.0, -6.4), ('L', 27.0, -10.0), ('L', 2.0, -21.4), ('L', 0.0, -20.6))
    extrude('Stock', st, 3.8, 'wood', crown=.4, inset=.55, bevel=.15)
    extrude('Butt plate', rrect(-1.0, -21.2, .4, -6.4, .4), 4.0, 'rubber', bevel=.1)
    rec = path(('M', 25.0, -3.6), ('L', 74.0, -3.6), ('L', 74.0, -10.4), ('L', 48.0, -11.0), ('L', 30.0, -10.6), ('L', 25.0, -8.0))
    extrude('Receiver', rec, 3.6, 'steel', bevel=.14, cuts=[cbox((1.8, 40.0, -6.0), (.5, 7.0, 1.4))])
    rail(28.0, 60.0, -3.0, 2.2, 'alu')
    grip('Grip', 41.6, 35.0, 16.0, 4.2, 3.6, 'polymer', z_top=-10.4)
    guard(42.0, 48.0, -10.6, 3.2, 'steel')
    trigger(44.0, -10.6)
    hg = path(('M', 50.0, -3.8), ('L', 74.0, -4.0), ('L', 74.0, -11.6), ('Q', 73.6, -12.6, 72.0, -12.6), ('L', 54.0, -11.6), ('L', 50.0, -11.0))
    extrude('Handguard', hg, 4.2, 'polymer', crown=.35, inset=.5, bevel=.14)
    lathe('Barrel', [(0, 1.1), (23.0, .9)], 'steel', at=(0, 74.0, BZ), seg=18, bevel=.03)
    extrude('Front sight', path(('M', 88.0, -6.2), ('L', 90.4, -6.2), ('L', 90.0, -3.6), ('L', 88.4, -3.6)), 1.4, 'steel', bevel=.05)
    lathe('Muzzle', [(0, .5), (0, 1.1), (2.6, 1.1), (2.6, .5)], 'steel', at=(0, 97.0, BZ), seg=18, closed=True, bevel=.03)
    return place(38.0, 15.0, {'bore_z': BZ, 'muzzle': (99.6, BZ), 'optic': (44.0, -3.0), 'mag': (52.0, -12.0)})


@gun
def woodpecker():
    """Free Fire Woodpecker (FAL style): thin folding skeleton stock, black receiver with carry handle,
    pistol grip, box magazine, thick handguard, long barrel with front sight and flash hider."""
    BZ = -5.6
    st = path(('M', 0.0, -4.4), ('L', 20.0, -4.0), ('L', 20.0, -7.0), ('L', 4.0, -10.0), ('L', 1.6, -15.4), ('L', 0.0, -15.0))
    extrude('Stock', st, 2.6, 'polymer', bevel=.12, cuts=[cprism(path(('M', 3.4, -6.0), ('L', 18.0, -5.6), ('L', 18.0, -6.4), ('L', 4.0, -8.8), ('L', 3.0, -12.0)), 6, 'YZ', 0)])
    extrude('Butt pad', rrect(-1.0, -15.6, .4, -4.0, .4), 3.0, 'rubber', bevel=.1)
    rec = path(('M', 19.0, -2.0), ('L', 52.0, -2.0), ('L', 52.0, -9.6), ('L', 26.0, -10.0), ('L', 19.0, -8.0))
    extrude('Receiver', rec, 3.4, 'steel', bevel=.14, cuts=[cbox((1.7, 40.0, -5.0), (.5, 6.4, 1.4))])
    extrude('Carry handle', path(('M', 31.0, -2.1), ('L', 33.0, .6), ('L', 37.0, .6), ('L', 38.0, -2.1)), 1.0, 'steel', at=1.2, bevel=.06)
    grip('Grip', 31.0, 26.6, 9.4, 3.0, 3.0, 'polymer', z_top=-9.6)
    guard(31.2, 37.0, -9.8, 2.4, 'steel')
    trigger(33.2, -9.8)
    extrude('Magazine', path(('M', 44.0, -9.6), ('L', 50.0, -9.6), ('L', 50.6, -18.6), ('L', 44.6, -18.6)), 2.6, 'steel', bevel=.12)
    hg = path(('M', 52.0, -1.0), ('L', 84.0, -1.2), ('Q', 85.4, -1.4, 85.4, -2.8), ('L', 85.4, -6.6), ('Q', 85.0, -7.6, 83.6, -7.6), ('L', 52.0, -9.0))
    extrude('Handguard', hg, 4.0, 'polymer', crown=.35, inset=.5, bevel=.14)
    lathe('Barrel', [(0, 1.0), (19.0, .85)], 'steel', at=(0, 85.4, BZ), seg=18, bevel=.03)
    extrude('Front sight', path(('M', 86.2, -5.0), ('L', 88.2, -5.0), ('L', 87.8, -1.2), ('L', 86.6, -1.2)), 1.2, 'steel', bevel=.05)
    flash_hider(101.0, BZ, .95, 3.6)
    return place(29.0, 15.0, {'bore_z': BZ, 'muzzle': (104.8, BZ), 'optic': (40.0, -2.0), 'mag': (47.0, -14.0)})


@gun
def m82():
    """Free Fire M82: long receiver with full top rail and carry handle, big scope, skeleton butt with
    monopod, pistol grip, big box magazine, thin long barrel with the double-chamber muzzle brake."""
    BZ = -12.6
    st = path(('M', 0.0, -9.4), ('L', 20.0, -9.4), ('L', 20.0, -16.4), ('L', 6.0, -16.4), ('L', 3.0, -22.0), ('L', 0.0, -22.0))
    extrude('Butt', st, 3.6, 'alu', bevel=.14, cuts=[cprism(rrect(4.4, -15.0, 17.0, -10.8, .8), 6, 'YZ', 0)])
    extrude('Butt pad', rrect(-1.2, -22.4, .4, -9.2, .5), 4.0, 'rubber', bevel=.12)
    rec = path(('M', 18.0, -9.0), ('L', 96.0, -9.0), ('L', 96.0, -16.6), ('L', 60.0, -16.6), ('L', 40.0, -17.0), ('L', 18.0, -16.8))
    extrude('Receiver', rec, 4.2, 'alu', bevel=.16, cuts=[cbox((2.1, 52.0, -12.0), (.5, 8.0, 2.0))])
    for y in range(64, 94, 4):
        extrude('Cooling hole', rrect(y, -14.2, y + 2.2, -11.4, 1.0), 4.35, 'alu', bevel=0)
    rail(22.0, 94.0, -8.4, 2.6, 'alu')
    extrude('Carry handle', path(('M', 70.0, -8.4), ('L', 72.0, -5.6), ('L', 80.0, -5.6), ('L', 82.0, -8.4)), 1.2, 'alu', bevel=.08,
            cuts=[cprism(rrect(72.6, -8.0, 79.4, -6.6, .4), 4, 'YZ', 0)])
    grip('Grip', 37.0, 31.6, 10.4, 3.4, 3.4, 'polymer', z_top=-16.6)
    guard(37.4, 44.0, -16.8, 2.6, 'alu')
    trigger(39.6, -16.8)
    extrude('Magazine', rrect(46.0, -25.6, 58.0, -16.4, .5), 3.6, 'steel', bevel=.14)
    extrude('Monopod', rrect(8.0, -23.6, 10.0, -16.2, .4), 1.6, 'alu', bevel=.08)
    lathe('Barrel', [(0, 1.3), (37.0, 1.05)], 'steel', at=(0, 96.0, BZ), seg=20, bevel=.03)
    ports = [cbox((0, 134.6 + i * 2.6, BZ), (5, 1.2, 1.6)) for i in range(2)]
    lathe('Muzzle brake', [(0, .7), (0, 1.9), (7.0, 1.9), (7.0, .7)], 'steel', at=(0, 133.0, BZ), seg=24, closed=True, bevel=.06, cuts=ports)
    scope(30.0, 36.0, -4.4, tube_r=1.7, bell_r=2.7, eye_r=2.3, kind='alu')
    ring_mount(38.0, -8.0, -4.4, 1.7)
    ring_mount(54.0, -8.0, -4.4, 1.7)
    return place(35.0, 22.0, {'bore_z': BZ, 'muzzle': (140.0, BZ), 'optic': (46.0, -4.4), 'mag': (52.0, -21.0)})
