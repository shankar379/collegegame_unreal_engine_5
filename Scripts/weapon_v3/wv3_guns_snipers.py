"""Weapon Library V3 - Free Fire style sniper / marksman rifles."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun


def scope(y0, length, axis_z, tube_r=1.5, bell_r=2.3, eye_r=2.1, kind='alu', turret=True):
    """Telescopic sight along +Y starting at y0: eyepiece, tube, objective bell, lenses, turrets."""
    L = length
    lathe('Scope', [(0, eye_r * .78), (.4, eye_r), (L * .19, eye_r), (L * .25, tube_r), (L * .70, tube_r), (L * .77, bell_r), (L, bell_r), (L, bell_r * .88)],
          kind, at=(0, y0, axis_z), seg=28, bevel=.04)
    lathe('Lens', [(0, eye_r * .7), (.08, eye_r * .7)], 'lens', at=(0, y0 - .02, axis_z), seg=24, bevel=0)
    lathe('Lens', [(0, bell_r * .82), (.08, bell_r * .82)], 'lens', at=(0, y0 + L - .07, axis_z), seg=28, bevel=0)
    if turret:
        mid = y0 + L * .47
        lathe('Turret', [(0, 1.1), (1.6, 1.1), (1.7, .95), (2.1, .95)], kind, at=(0, mid, axis_z + tube_r - .2), axis='Z', seg=18, bevel=.04)
        lathe('Turret', [(0, 1.0), (1.4, 1.0), (1.8, .85)], kind, at=(tube_r - .2, mid, axis_z), axis='X', seg=18, bevel=.04)
        lathe('Turret base', [(-1.3, 1.65), (1.3, 1.65)], kind, at=(0, mid, axis_z), seg=24, bevel=.04)


def ring_mount(y, base_z, axis_z, r, kind='alu'):
    lathe('Scope ring', [(-.6, r + .45), (.6, r + .45)], kind, at=(0, y, axis_z), seg=24, bevel=.05)
    extrude('Ring base', rrect(y - .9, base_z, y + .9, axis_z - r + .2, .2), 1.8, kind, bevel=.06)


@gun
def awm():
    """Free Fire AWM: olive-green thumbhole stock and forend, black round action with bolt,
    long barrel with muzzle brake, big scope. Proportions from guns2.jpg."""
    BZ = 6.0
    green = 'polymer_green'
    # stock: butt, cheek riser, thumbhole behind the vertical grip, lower bar back to the butt
    st = path(('M', -32.0, 5.6), ('L', -16.0, 5.6), ('L', -14.2, 6.7), ('L', -8.0, 6.7), ('L', -6.4, 5.5), ('L', 1.6, 5.0),
              ('L', 1.6, 2.7), ('L', 3.4, 2.7), ('C', 3.2, .0, 2.9, -3.0, 2.6, -5.6), ('Q', 2.4, -6.7, 1.3, -6.7),
              ('L', -29.6, -6.7), ('Q', -32.0, -6.7, -32.0, -4.4))
    hole = cprism(rrect(-7.6, -2.8, -1.7, 1.9, 1.7), 8, 'YZ', 0)
    extrude('Stock', st, 4.0, green, crown=.35, inset=.6, bevel=.15, cuts=[hole])
    extrude('Butt pad', path(('M', -32.0, 5.7), ('L', -33.3, 5.7), ('Q', -33.6, -.5, -33.3, -6.8), ('L', -32.0, -6.8)), 4.2, 'rubber', bevel=.14)
    for y in (-26.0, -20.0):
        lathe('Stock spacer', [(-2.15, .45), (2.15, .45)], 'steel', at=(0, y, 4.6), axis='X', seg=12, bevel=0)
    # chassis under the action, trigger guard
    extrude('Chassis', rrect(1.4, 1.5, 18.6, 4.6, .4), 4.2, green, bevel=.14)
    tg = path(('M', 3.6, 1.6), ('L', 3.8, -1.0), ('Q', 4.2, -1.9, 5.4, -1.9), ('L', 7.6, -1.9), ('Q', 8.2, -1.6, 8.2, -.6), ('L', 8.2, 1.6), n=6)
    extrude('Trigger guard', band(tg, .6), 1.3, green, bevel=.1)
    extrude('Trigger', path(('M', 5.4, 1.6), ('L', 6.1, 1.6), ('C', 6.1, .2, 5.8, -.6, 5.0, -1.0), ('L', 4.9, -.7), ('C', 5.4, -.3, 5.5, .5, 5.4, 1.6)), .45, 'steel_bright', bevel=.03)
    # detachable box magazine
    extrude('Magazine', rrect(9.4, -3.9, 16.4, 1.8, .45), 2.6, 'steel', bevel=.12)
    extrude('Mag floorplate', rrect(9.2, -4.4, 16.6, -3.4, .3), 2.9, 'steel', bevel=.08)
    # round action: flat-sided, ejection port, bolt shroud, bolt handle with ball knob, scope rail
    act = [(x, z) for x, z in rrect(-1.65, 4.4, 1.65, 8.6, 1.0, 5)]
    extrude('Action', act, 18.6, 'steel', plane='XZ', at=9.7, bevel=.1, cuts=[cbox((1.6, 9.2, 7.2), (.8, 6.8, 1.6))])
    extrude('Bolt body', rrect(6.0, 6.5, 12.4, 7.9, .35), 1.6, 'steel_bright', at=.55, skin=False, bevel=.05)
    lathe('Bolt shroud', [(0, .9), (.4, 1.3), (2.0, 1.3), (2.0, 1.0)], 'steel', at=(0, -1.6, 6.5), seg=20, bevel=.05)
    lathe('Bolt handle', [(0, .42), (3.2, .42), (3.4, .62), (4.2, .78), (4.9, .62), (5.2, .25)], 'steel', at=(1.5, 3.6, 6.9), axis='X', seg=14, bevel=0)
    extrude('Scope rail', rrect(.6, 8.5, 18.4, 9.3, .1), 2.0, 'steel', bevel=.05)
    for y in range(2, 18, 2):
        extrude('Rail slot', rrect(y - .25, 9.1, y + .25, 9.5, .05), 2.05, 'steel', bevel=0)
    # long green forend with sling stud
    fe = path(('M', 18.4, 7.5), ('L', 40.0, 7.5), ('Q', 41.8, 7.3, 41.8, 5.7), ('L', 41.8, 3.4), ('Q', 41.4, 1.9, 39.4, 1.9), ('L', 18.4, 1.6))
    extrude('Forend', fe, 4.6, green, crown=.35, inset=.5, bevel=.15)
    lathe('Sling stud', [(0, .45), (.8, .45)], 'steel', at=(0, 38.0, 1.95), axis='Z', seg=10, bevel=0)
    lathe('Sling stud', [(0, .45), (-.8, .45)], 'steel', at=(0, 38.0, 1.95), axis='Z', seg=10, bevel=0)
    # long barrel + muzzle brake with side ports
    lathe('Barrel', [(0, 1.25), (6.0, 1.05), (55.6, .82)], 'blued', at=(0, 18.4, BZ), seg=20, bevel=.03)
    ports = [cbox((0, 76.0 + i * 1.6, BZ), (4, .7, 1.2)) for i in range(3)]
    lathe('Muzzle brake', [(0, .6), (0, 1.35), (6.4, 1.35), (6.6, 1.2), (6.6, .6)], 'steel', at=(0, 74.0, BZ), seg=24, closed=True, bevel=.05, cuts=ports)
    # scope
    SZ = 11.1
    scope(-3.0, 34.0, SZ, tube_r=1.5, bell_r=2.4, eye_r=2.1)
    ring_mount(4.6, 9.3, SZ, 1.5)
    ring_mount(17.0, 9.3, SZ, 1.5)
    return {'bore_z': BZ, 'muzzle': (80.6, BZ), 'optic': (10.0, SZ), 'mag': (12.9, -1.0)}
