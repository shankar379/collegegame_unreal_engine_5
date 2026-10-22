"""Weapon Library V3 - Free Fire style SMGs, shotguns and pistols (MP40, M1887, Desert Eagle)."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun


@gun
def mp40():
    """Free Fire MP40: black tubular receiver, dark bakelite lower + grip, wire stock extended,
    long straight magazine forward of the trigger, barrel with resting bar and hooded front sight."""
    BZ = 7.6
    lathe('Receiver tube', [(0, 1.6), (.4, 1.95), (27.2, 1.95), (27.6, 1.7)], 'blued', at=(0, -3.6, BZ), seg=28, bevel=.04, skin=True,
          cuts=[cbox((-1.9, 10.0, BZ + .3), (1.0, 14.0, .7))])
    for y in (4.0, 12.0, 19.0):
        lathe('Receiver ring', [(-.25, 2.02), (.25, 2.02)], 'blued', at=(0, y, BZ), seg=28, bevel=.02, skin=True)
    lathe('Rear cap', [(0, .4), (.3, 1.4), (1.0, 1.9), (1.0, 1.6)], 'blued', at=(0, -4.6, BZ), seg=24, bevel=.04, skin=True)
    lathe('Bolt knob', [(0, .35), (1.6, .35), (1.7, .6), (2.4, .62), (2.5, .3)], 'steel', at=(-1.6, 15.0, BZ + .3), axis='X', seg=12, bevel=0)
    tube('Bolt knob', [(-1.7, 15.0, BZ + .3), (-3.6, 15.0, BZ + .3)], .4, 'steel')
    lower = path(('M', -3.0, 5.9), ('L', 24.0, 5.9), ('L', 24.0, 3.2), ('L', 21.0, 1.6), ('L', 9.4, 1.6), ('L', 9.4, 2.4), ('L', 1.0, 2.4),
                 ('L', -1.6, 1.8), ('L', -3.0, 3.4))
    extrude('Lower frame', lower, 3.6, 'bakelite', bevel=.22, seg=3)
    gp = path(('M', 1.2, 2.4), ('L', -3.4, 2.4), ('C', -4.4, -1.0, -5.6, -4.8, -6.4, -8.0), ('Q', -6.6, -9.4, -5.2, -9.4), ('L', -2.6, -9.0),
              ('Q', -1.6, -8.8, -1.6, -7.8), ('C', -1.0, -4.4, .2, -1.0, 1.2, 2.4))
    extrude('Grip', gp, 3.2, 'bakelite', crown=.35, inset=.5, bevel=.16, seg=3)
    for z in (-1.5, -4.0, -6.5):
        extrude('Grip groove', band([(-4.6 + (z + 1.5) * -.27, z), (-2.0 + (z + 1.5) * -.27, z)], .3), 3.35, 'bakelite', bevel=.05)
    tg = path(('M', 2.6, 1.8), ('L', 2.8, -.2), ('Q', 3.2, -1.2, 4.4, -1.2), ('L', 8.0, -1.2), ('Q', 9.0, -1.0, 9.0, .2), ('L', 9.0, 1.8), n=6)
    extrude('Trigger guard', band(tg, .45), 1.1, 'blued', bevel=.06)
    extrude('Trigger', path(('M', 5.0, 1.8), ('L', 5.7, 1.8), ('C', 5.7, .4, 5.4, -.4, 4.6, -.8), ('L', 4.5, -.5), ('C', 5.0, -.1, 5.1, .7, 5.0, 1.8)), .45, 'steel', bevel=.03)
    # magazine housing + long straight 32-round stick magazine
    extrude('Mag housing', path(('M', 25.4, 6.4), ('L', 33.6, 6.4), ('L', 33.6, 1.0), ('L', 32.6, .2), ('L', 26.4, .2), ('L', 25.4, 1.0)), 3.4, 'blued', bevel=.12)
    extrude('Magazine', rrect(27.6, -14.4, 31.6, .6, .3), 2.6, 'blued', bevel=.1)
    for side in (1, -1):
        extrude('Mag rib', rrect(29.2, -13.4, 30.0, -1.0, .3), .3, 'blued', at=side * 1.35, bevel=.06, skin=True)
    extrude('Mag floorplate', rrect(27.3, -15.0, 31.9, -14.1, .2), 2.9, 'blued', bevel=.06)
    # barrel nut, barrel, resting bar, hooded front sight
    lathe('Barrel nut', [(0, 1.7), (.5, 2.0), (4.0, 2.0), (4.4, 1.6)], 'blued', at=(0, 23.8, BZ), seg=24, bevel=.04, skin=True)
    lathe('Barrel', [(0, 1.05), (6.0, 1.05), (6.2, .9), (30.2, .9)], 'blued', at=(0, 27.8, BZ), seg=20, bevel=.03, skin=True)
    extrude('Resting bar', path(('M', 27.6, BZ - .9), ('L', 40.2, BZ - .9), ('L', 40.6, BZ - 1.8), ('L', 27.6, BZ - 2.2)), 1.6, 'blued', bevel=.08)
    extrude('Front sight hood', path(('M', 53.6, BZ + .4), ('L', 56.6, BZ + .4), ('L', 56.4, BZ + 3.8), ('L', 54.0, BZ + 3.8)), 1.8, 'blued', bevel=.08,
            cuts=[cbox((0, 55.1, BZ + 2.6), (1.2, 4, 1.6))])
    lathe('Front post', [(0, .16), (2.6, .12)], 'steel', at=(0, 55.1, BZ + .8), axis='Z', seg=8, bevel=0)
    lathe('Muzzle', [(0, .5), (0, 1.1), (1.0, 1.1), (1.0, .5)], 'blued', at=(0, 56.6, BZ), seg=20, closed=True, bevel=.03, skin=True)
    extrude('Rear sight', path(('M', 17.4, BZ + 1.7), ('L', 21.2, BZ + 1.7), ('L', 20.6, BZ + 2.9), ('L', 18.0, BZ + 2.9)), 1.6, 'blued', bevel=.06)
    # Reference silhouette: gently tilted, CLOSED oval butt loop, not a square L.
    extrude('Stock hinge', rrect(-5.0, 1.0, -1.6, 4.4, .5), 3.0, 'blued', bevel=.1)
    for x in (-1.2, 1.2):
        tube('Stock arm', [(x, -3.0, 2.8), (x, -26.6, 1.7)], .42, 'blued', skin=True, smooth=False)
    loop = []
    for i in range(48):
        t = 2 * math.pi * i / 48
        z = 1.6 + 5.7 * math.sin(t)
        y = -26.8 + 1.6 * math.cos(t) - .20 * (z - 1.6)
        loop.append((0, y, z))
    tube('Rounded butt loop', loop, .44, 'blued', skin=True, smooth=False, res=3, cyclic=True)
    tube('Stock loop bridge', [(-1.2, -26.6, 1.7), (1.2, -26.6, 1.7)], .44, 'blued', skin=True, smooth=False)
    return {'bore_z': BZ, 'muzzle': (57.6, BZ), 'optic': (10.0, BZ + 2.0), 'mag': (29.6, -6.0)}


@gun
def m1887():
    """Free Fire M1887: sawn-off lever-action shotgun with angled wooden pistol butt, big lever loop,
    long red-brown forend, barrel over a magazine tube."""
    BZ = 5.6
    butt = path(('M', 5.4, -.6), ('L', -9.6, -6.6), ('Q', -11.2, -7.6, -10.6, -9.6), ('L', -9.0, -12.0), ('Q', -8.2, -12.8, -7.0, -12.2),
                ('L', 7.2, -5.6), ('L', 7.0, -1.6))
    extrude('Butt grip', butt, 3.8, 'wood', crown=.45, inset=.6, bevel=.15)
    extrude('Butt cap', band([(-10.9, -6.9), (-8.6, -12.5)], .9), 3.9, 'steel', bevel=.1)
    rec = path(('M', 5.2, -1.4), ('L', 13.4, 5.0), ('Q', 16.6, 7.3, 19.6, 7.3), ('L', 24.2, 7.3), ('L', 24.2, -.6), ('L', 13.8, -1.6), ('L', 8.6, -5.4), ('L', 6.4, -4.4))
    extrude('Receiver', rec, 3.6, 'steel', bevel=.14, cuts=[cbox((1.9, 18.2, 4.6), (.4, 7.0, 3.6))])
    extrude('Side plate', path(('M', 14.0, .6), ('L', 22.6, .6), ('L', 22.6, 5.6), ('L', 17.4, 5.6), ('L', 14.0, 3.4)), .3, 'steel', at=1.9, bevel=.06)
    for y, z in ((21.0, 4.0), (16.8, 1.6), (9.6, -2.4)):
        lathe('Receiver screw', [(0, .55), (.2, .55), (.3, .35)], 'steel_bright', at=(1.8, y, z), axis='X', seg=14, bevel=0)
        lathe('Receiver screw', [(0, .55), (-.2, .55), (-.3, .35)], 'steel_bright', at=(-1.8, y, z), axis='X', seg=14, bevel=0)
    extrude('Hammer', path(('M', 10.6, 4.0), ('L', 12.4, 4.6), ('L', 11.4, 7.4), ('L', 9.6, 7.8), ('L', 9.8, 6.8), ('L', 10.8, 6.4)), 1.0, 'steel', bevel=.06)
    # big lever loop
    loop = [(0, 10.4, -3.2), (0, 9.6, -9.0), (0, 6.0, -15.0), (0, 1.0, -18.6), (0, -3.4, -18.0), (0, -5.0, -14.6), (0, -2.0, -11.2), (0, 3.8, -7.0), (0, 7.4, -4.4)]
    tube('Lever loop', loop, .5, 'steel', smooth=True, res=3)
    extrude('Lever body', path(('M', 8.0, -4.0), ('L', 14.2, -1.4), ('L', 13.8, -.4), ('L', 7.2, -2.6)), 1.4, 'steel', bevel=.08)
    extrude('Trigger', path(('M', 9.0, -3.0), ('L', 9.8, -2.8), ('C', 9.6, -4.4, 9.0, -5.4, 8.0, -5.8), ('L', 7.9, -5.4), ('C', 8.6, -4.8, 8.9, -4.0, 9.0, -3.0)), .45, 'steel_bright', bevel=.03)
    # forend, barrel, magazine tube, barrel band, bead
    fe = path(('M', 24.0, 6.4), ('L', 59.4, 6.4), ('Q', 60.4, 6.2, 60.4, 5.2), ('L', 60.4, .6), ('Q', 60.2, -.4, 59.2, -.4), ('L', 24.0, -.6))
    extrude('Forend', fe, 4.6, 'wood', crown=.45, inset=.6, bevel=.15)
    lathe('Barrel', [(0, 1.55), (2.0, 1.45), (61.0, 1.35)], 'steel', at=(0, 24.0, BZ), seg=24, bevel=.03)
    lathe('Muzzle', [(0, .95), (0, 1.38), (.6, 1.38), (.6, .95)], 'steel', at=(0, 85.0, BZ), seg=24, closed=True, bevel=.03)
    lathe('Mag tube', [(0, 1.2), (56.0, 1.2), (56.4, 1.0)], 'steel', at=(0, 24.0, 2.3), seg=20, bevel=.03)
    lathe('Mag cap', [(0, 1.25), (1.2, 1.25), (1.4, .9)], 'steel_bright', at=(0, 79.6, 2.3), seg=20, bevel=.03)
    extrude('Barrel band', rrect(73.4, .9, 75.2, 7.1, .5), 3.4, 'steel', bevel=.1)
    lathe('Bead', [(0, .3), (.4, .3), (.5, .1)], 'brass', at=(0, 84.2, BZ + 1.3), axis='Z', seg=10, bevel=0)
    return {'bore_z': BZ, 'muzzle': (85.6, BZ), 'optic': (20.0, 7.3), 'mag': (40.0, 2.3)}


@gun
def deagle():
    """Desert Eagle matched to the user's reference render: worn stainless slide and frame, rear slide
    serrations, round safety lever, spurred hammer, pointed beavertail, slotted top rib, slanted muzzle face,
    square trigger guard with black slide-release / takedown levers, black textured grip panels with a
    medallion, stainless magazine base. Reference coordinates (y from the rear, z below the top), then placed."""
    from wv3_guns_batch4 import place
    # slide: tall rear section with serrations, slightly lower front section over the fixed barrel
    sl = path(('M', 1.8, -4.9), ('L', 1.8, -1.2), ('Q', 1.9, -.4, 2.8, -.4), ('L', 11.6, -.4), ('L', 11.9, -.7), ('L', 26.0, -.7),
              ('L', 26.7, -1.2), ('L', 26.7, -3.9), ('L', 24.0, -4.9))
    cuts = [cbox((s_ * 1.55, 3.9 + i * .68, -3.15), (.4, .26, 2.7)) for s_ in (1, -1) for i in range(10)]
    cuts += [cbox((s_ * 1.6, 19.0, -3.3), (.3, 13.6, .32)) for s_ in (1, -1)]          # side groove on the front section
    cuts += [cbox((0, 14.0 + i * 2.4, -.5), (.9, 1.4, .6)) for i in range(3)]             # slots in the top rib
    extrude('Slide', sl, 3.1, 'stainless', bevel=.1, cuts=cuts)
    lathe('Barrel collar', [(0, 1.15), (1.4, 1.15)], 'stainless', at=(0, 11.4, -1.9), seg=20, bevel=.04)
    lathe('Bore', [(0, .62), (.25, .62)], 'bore', at=(0, 26.55, -2.4), seg=16, bevel=0)
    extrude('Front sight', path(('M', 24.4, -.75), ('L', 25.8, -.75), ('L', 25.6, .3), ('L', 24.9, .3)), .55, 'blued', bevel=.03)
    extrude('Rear sight', rrect(3.6, -.45, 5.4, .2, .15), 2.2, 'blued', bevel=.04, cuts=[cbox((0, 4.5, .1), (.5, 3, .5))])
    # safety lever (round, with a red dot) and hammer
    tab = path(('M', 4.0, -1.25), ('L', 6.6, -1.45), ('Q', 7.1, -1.85, 6.6, -2.25), ('L', 4.6, -2.45), ('Q', 3.7, -2.1, 4.0, -1.25))
    extrude('Safety lever', tab, .3, 'blued', at=1.68, bevel=.06)
    extrude('Safety lever', tab, .3, 'blued', at=-1.68, bevel=.06)
    lathe('Safety dot', [(0, .15), (.2, .15)], 'ceramic_red', at=(1.8, 4.6, -1.9), axis='X', seg=10, bevel=0)
    extrude('Hammer', path(('M', 1.9, -2.2), ('L', 1.9, -3.4), ('L', 1.0, -3.0), ('L', .4, -1.8), ('L', .6, -1.0), ('L', 1.4, -1.2)), .9, 'blued', bevel=.05)
    # frame: dust cover under the slide, pointed beavertail, square trigger guard
    fr = path(('M', 1.8, -4.8), ('L', 14.6, -4.8), ('L', 14.6, -6.0), ('L', 9.2, -6.0), ('L', 2.4, -6.2), ('L', 1.6, -5.6), ('L', -.2, -5.8), ('L', 1.4, -5.2))
    extrude('Frame', fr, 2.9, 'stainless', bevel=.1)
    gd = path(('M', 9.2, -5.8), ('L', 9.2, -7.4), ('Q', 9.3, -8.1, 10.1, -8.1), ('L', 13.4, -8.1), ('Q', 14.2, -8.0, 14.2, -7.2), ('L', 14.2, -5.8), n=5)
    extrude('Trigger guard', band(gd, .55), 1.2, 'stainless', bevel=.08)
    extrude('Trigger', path(('M', 10.8, -5.9), ('L', 11.6, -5.9), ('C', 11.6, -6.9, 11.3, -7.5, 10.6, -7.8), ('L', 10.5, -7.5), ('C', 10.9, -7.1, 10.9, -6.6, 10.8, -5.9)), .5, 'blued', bevel=.03)
    extrude('Slide release', rrect(12.6, -5.3, 14.2, -4.6, .2), .35, 'blued', at=-1.6, bevel=.03)
    extrude('Takedown lever', rrect(6.4, -5.6, 9.2, -5.05, .2), .3, 'blued', at=1.55, bevel=.03)
    for y in (9.8, 12.0):
        lathe('Frame pin', [(0, .26), (.15, .26)], 'steel', at=(1.47, y, -5.5), axis='X', seg=10, bevel=0)
        lathe('Frame pin', [(0, .26), (-.15, .26)], 'steel', at=(-1.47, y, -5.5), axis='X', seg=10, bevel=0)
    # grip: stainless frame with front strap, black textured panels with a medallion, magazine base
    gp = path(('M', 9.4, -5.8), ('L', 2.2, -5.9), ('C', 2.0, -8.6, 2.1, -11.8, 2.2, -14.0), ('L', 9.0, -14.0),
              ('C', 9.0, -11.6, 8.8, -9.6, 9.0, -8.2), ('Q', 9.4, -7.0, 9.4, -5.8))
    extrude('Grip frame', gp, 2.8, 'stainless', bevel=.12,
            cuts=[cbox((0, 5.85, -10.7), (2.05, 5.1, 8.8))])
    panel = path(('M', 8.4, -6.2), ('L', 3.0, -6.2), ('C', 2.8, -8.8, 2.9, -11.6, 3.0, -13.6), ('L', 8.4, -13.6), ('C', 8.2, -11.4, 8.1, -9.0, 8.4, -6.2))
    extrude('Grip panel', panel, 3.3, 'rubber', crown=.12, inset=.2, bevel=.08,
            cuts=[cbox((0, 5.85, -10.7), (2.05, 5.1, 8.8))])
    lathe('Medallion', [(0, .75), (.12, .75), (.16, .55)], 'rubber', at=(1.7, 5.9, -10.2), axis='X', seg=18, bevel=0)
    lathe('Medallion', [(0, .75), (-.12, .75), (-.16, .55)], 'rubber', at=(-1.7, 5.9, -10.2), axis='X', seg=18, bevel=0)
    extrude('Magazine base', rrect(3.0, -14.6, 9.0, -13.9, .2), 2.7, 'stainless', bevel=.06)
    # Hidden when seated, but required when the magazine is removed during reload.
    extrude('Magazine', rrect(3.4, -14.0, 8.3, -6.7, .25), 1.9, 'blued', bevel=.08)
    # Reference grip leans back toward the heel; shear the entire grip assembly
    # together so panels, opening and removable magazine continue to align.
    from wv3_kit import PARTS
    for ob, _, _ in PARTS:
        if ob.name.split('.')[0] in {'Grip frame', 'Grip panel', 'Medallion', 'Magazine', 'Magazine base'}:
            for vertex in ob.data.vertices:
                vertex.co.y += .30 * min(0, vertex.co.z + 6.2)
    return place(5.6, 9.0, {'bore_z': -2.4, 'muzzle': (26.7, -2.4), 'optic': (8.0, -.4), 'mag': (5.6, -10.0)})
