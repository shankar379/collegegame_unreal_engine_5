"""Free Fire AK47 - proportions measured from the reference picture (guns2.jpg)."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, cbox, cprism)
from wv3_guns import gun, rivet


def _bez(p0, p1, p2, p3, s):
    return tuple((1 - s) ** 3 * p0[j] + 3 * (1 - s) ** 2 * s * p1[j] + 3 * (1 - s) * s * s * p2[j] + s ** 3 * p3[j] for j in (0, 1))


@gun
def ak47():
    """Black stamped receiver, orange-brown wood stock / grip / handguards, big banana magazine set
    well back, short dropped stock, front sight at the muzzle."""
    BZ, W = 5.0, 3.4
    rec = path(('M', -6.3, 3.0), ('L', -6.3, 7.0), ('L', 24.9, 7.0), ('L', 24.9, 1.6), ('L', 22.6, 1.2), ('L', 19.8, 1.2),
               ('L', 19.8, 1.5), ('L', -3.0, 1.5), ('Q', -5.8, 1.7, -6.3, 3.0))
    cuts = [cbox((W / 2 + .4, 16.6, 6.2), (1.6, 9.0, 1.05)), cbox((W / 2 + .2, 16.6, 3.0), (.32, 4.6, 2.2)), cbox((-W / 2 - .2, 16.6, 3.0), (.32, 4.6, 2.2))]
    extrude('Receiver', rec, W, 'steel', bevel=.12, cuts=cuts)
    extrude('Bolt carrier', rrect(12.4, 5.55, 21.2, 6.85, .3), 2.0, 'steel_bright', at=.3, skin=False, bevel=.06)
    extrude('Front trunnion', rrect(22.4, 1.8, 26.0, 7.6, .4), W + .3, 'steel', bevel=.1)
    extrude('Dust cover', arc(0, 6.85, 1.75, 180, 0, 16), 28.8, 'steel', plane='XZ', at=8.4, bevel=.08, skin=True)
    for y in (2.0, 9.0, 15.0):
        ribs = [(x * .92, 6.85 + (z - 6.85) * 1.12) for x, z in arc(0, 6.85, 1.7, 165, 15, 10)]
        extrude('Cover rib', band(ribs, .32), .9, 'steel', plane='XZ', at=y, bevel=.05)
    lathe('Cover button', [(0, .55), (.5, .55), (.7, .4), (.75, .2)], 'steel', at=(0, -6.45, 7.45), axis='Y', seg=12)
    lathe('Charging handle', [(0, .32), (1.0, .32), (1.15, .5), (1.9, .58), (2.1, .3)], 'steel_bright', at=(1.1, 20.4, 6.35), axis='X', seg=12)
    sel = path(('M', .4, 4.3), ('L', 1.6, 4.3), ('L', 2.4, 5.75), ('L', 9.6, 6.2), ('L', 9.8, 6.85), ('L', 2.1, 6.75), ('L', .8, 6.3), ('L', 0, 5.0))
    extrude('Selector lever', sel, .22, 'steel', at=W / 2 + .14, bevel=.04)
    lathe('Selector pivot', [(0, .5), (.2, .5), (.3, .28)], 'steel', at=(W / 2 + .1, 1.4, 5.6), axis='X', seg=12, bevel=0)
    for y, z in ((-4.5, 2.6), (-4.5, 4.8), (6.0, 2.3), (9.5, 2.3), (12.6, 2.3), (23.0, 2.6), (24.2, 4.9), (22.0, 5.6)):
        rivet(W / 2, y, z, side=1)
        rivet(-W / 2, y, z, side=-1)
    # chunky grip, same wood as the stock
    gp = path(('M', 2.6, 1.6), ('L', -2.6, 1.6), ('C', -3.0, -1.5, -3.4, -4.5, -3.8, -6.6), ('Q', -4.0, -7.9, -2.8, -7.9),
              ('L', .2, -7.9), ('Q', 1.0, -7.8, 1.0, -7.0), ('C', 1.4, -4.5, 2.0, -1.5, 2.6, 1.6))
    extrude('Grip', gp, 3.0, 'wood', crown=.35, inset=.5, taper=lambda a, b: 1 - max(0, -b) * .01, bevel=.12)
    tg = path(('M', 3.4, 1.7), ('L', 3.6, -1.4), ('Q', 4.0, -2.8, 5.6, -2.8), ('L', 11.4, -2.8), ('Q', 12.6, -2.7, 12.7, -1.2), ('L', 12.7, 1.7), n=8)
    extrude('Trigger guard', band(tg, .45), 1.1, 'steel', bevel=.06)
    extrude('Trigger', path(('M', 6.4, 1.5), ('L', 7.2, 1.5), ('C', 7.2, -.4, 6.8, -1.4, 5.8, -2.0), ('L', 5.6, -1.7), ('C', 6.3, -.9, 6.5, .2, 6.4, 1.5)), .5, 'steel_bright', bevel=.04)
    extrude('Mag catch', path(('M', 12.4, 1.4), ('L', 13.3, 1.4), ('L', 13.2, -1.6), ('L', 12.7, -2.1), ('L', 12.5, -1.6)), 1.5, 'steel', bevel=.06)
    # big banana magazine
    F = ((19.6, 1.4), (20.0, -4.8), (23.0, -10.2), (28.6, -14.6))
    Bk = ((13.4, 1.4), (14.2, -6.5), (19.4, -12.2), (23.4, -17.6))
    mg = path(('M', 13.4, 1.4), ('L', 19.6, 1.4), ('C', *F[1], *F[2], *F[3]), ('L', *Bk[3]), ('C', *Bk[2], *Bk[1], *Bk[0]), n=14)
    extrude('Magazine', mg, 2.8, 'steel', taper=lambda a, b: 1 - max(0, -b) * .004, bevel=.12)
    for t in (.3, .64):
        line = []
        for i in range(1, 12):
            s = i / 12
            f, b = _bez(*F, s), _bez(*Bk, s)
            line.append((b[0] + (f[0] - b[0]) * t, b[1] + (f[1] - b[1]) * t))
        for side in (1, -1):
            extrude('Mag rib', band(line, .6), .3, 'steel', at=side * 1.42, bevel=.08, skin=True)
    extrude('Mag floorplate', band([(23.0, -17.8), (29.1, -14.4)], .75), 3.1, 'steel', bevel=.1, skin=True)
    extrude('Mag lug', rrect(19.2, .2, 20.2, 1.6, .2), 1.6, 'steel', bevel=.05)
    # short dropped stock, butt plate, tang
    st = path(('M', -6.1, 5.6), ('L', -27.3, 3.6), ('L', -27.3, -6.8), ('C', -21.0, -4.4, -13.0, -1.2, -7.6, 1.2), ('Q', -6.4, 1.7, -6.1, 2.8), n=10)
    extrude('Stock', st, 3.8, 'wood', crown=.4, inset=.6, taper=lambda a, b: 1 + max(0, -a - 6) * .004, bevel=.12)
    extrude('Butt plate', path(('M', -27.3, 3.7), ('L', -28.2, 3.7), ('Q', -28.6, -1.6, -28.2, -6.9), ('L', -27.3, -6.8)), 4.15, 'steel', bevel=.1)
    extrude('Stock tang', rrect(-9.4, 5.2, -6.1, 6.95, .2), 3.9, 'steel', bevel=.05)
    # handguards
    lh = path(('M', 25.4, 6.8), ('L', 43.8, 6.8), ('L', 43.8, 3.2), ('Q', 43.6, 2.2, 42.4, 2.2), ('L', 27.2, 2.2), ('Q', 25.6, 2.3, 25.4, 3.6))
    extrude('Lower handguard', lh, 4.5, 'wood', crown=.45, inset=.6, taper=lambda a, b: .9 + .1 * math.sin(max(0, min(1, (a - 25.4) / 18.4)) * math.pi), bevel=.12)
    for y, w in ((25.2, 4.7), (44.1, 3.8)):
        extrude('Handguard retainer', rrect(y - .45, 1.9, y + .45, 6.9, .25), w, 'steel', bevel=.08)
    lathe('Gas tube', [(0, .95), (20.4, .95)], 'steel', at=(0, 26.6, 7.6), seg=18, bevel=.03)
    extrude('Upper handguard', arc(0, 7.25, 1.8, 180, 0, 14), 11.4, 'wood', plane='XZ', at=32.1, bevel=.15)
    for y in (38.2, 38.8):
        lathe('Gas port ring', [(0, 1.02), (.3, 1.02)], 'steel', at=(0, y, 7.6), seg=18, bevel=0)
    extrude('Rear sight block', path(('M', 24.2, 7.0), ('L', 28.6, 7.0), ('L', 28.6, 8.6), ('L', 25.0, 8.6), ('L', 24.2, 7.9)), 2.4, 'steel', bevel=.08)
    extrude('Rear sight leaf', path(('M', 24.8, 8.6), ('L', 29.4, 9.05), ('L', 29.4, 9.55), ('L', 24.8, 9.2)), 1.5, 'steel', bevel=.04)
    extrude('Sight slider', rrect(26.3, 8.65, 27.3, 9.75, .15), 1.9, 'steel', bevel=.04)
    # barrel, gas block, front sight at the muzzle, cleaning rod, slant brake
    lathe('Barrel', [(0, 1.25), (1.5, 1.25), (1.8, 1.0), (24.0, .95), (24.0, .88), (37.0, .82)], 'blued', at=(0, 25.4, BZ), seg=20, bevel=.03)
    extrude('Gas block', path(('M', 46.4, 3.9), ('L', 46.4, 8.7), ('L', 48.1, 8.7), ('L', 50.4, 6.1), ('L', 50.4, 3.9)), 2.3, 'steel', bevel=.1)
    extrude('Front sight base', path(('M', 58.6, 3.4), ('L', 58.6, 6.4), ('L', 59.6, 7.4), ('L', 61.6, 7.4), ('L', 62.2, 6.2), ('L', 62.2, 3.4)), 2.2, 'steel', bevel=.08)
    for x in (-.85, .85):
        extrude('Sight ear', path(('M', 59.5, 7.0), ('L', 61.5, 7.0), ('L', 61.2, 9.9), ('L', 60.0, 9.9)), .34, 'steel', at=x, bevel=.03)
    lathe('Front post', [(0, .16), (2.4, .12)], 'steel', at=(0, 60.6, 7.2), axis='Z', seg=8, bevel=0)
    extrude('Bayonet lug', rrect(59.0, 2.2, 61.6, 3.6, .3), 1.0, 'steel', bevel=.05)
    lathe('Cleaning rod', [(0, .26), (35.6, .26), (35.8, .2)], 'steel_bright', at=(0, 26.0, 3.2), seg=10, bevel=0)
    slant = cprism([(64.8, BZ + 1.5), (66.1, BZ + 1.5), (66.1, BZ + .2), (65.2, BZ - .2)], 4, 'YZ', 0)
    lathe('Muzzle brake', [(0, .62), (0, 1.12), (3.9, 1.12), (3.9, .62)], 'blued', at=(0, 62.2, BZ), seg=24, closed=True, bevel=.05, cuts=[slant])
    return {'bore_z': BZ, 'muzzle': (66.1, BZ), 'optic': (14.0, 8.6), 'mag': (21.0, -8.0)}
