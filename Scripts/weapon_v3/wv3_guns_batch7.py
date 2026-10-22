"""Weapon Library V3 - batch 7: crossbow, melee, and original sci-fi guns in the Free Fire style.

Melee items: the hand sits at the origin, the blade/head points along +Y.
Sci-fi guns (plasma, charge_buster, healing_pistol) are ORIGINAL designs that follow the style of the
references (white ceramic shells, black frames, red accents, glowing parts) - they are not copies."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_batch2 import rail, grip, trigger, guard
from wv3_guns_snipers import scope, ring_mount
from wv3_guns_batch4 import place


@gun
def crossbow():
    """Free Fire crossbow: skeleton stock, camo body with top rail, grip, recurve limbs with cams,
    string, loaded bolt with a broadhead."""
    st = path(('M', 0.0, -7.6), ('L', 18.0, -6.6), ('L', 18.0, -10.6), ('L', 6.0, -13.0), ('L', 2.0, -20.0), ('L', 0.0, -19.6))
    extrude('Stock', st, 3.0, 'polymer', bevel=.14, cuts=[cprism(path(('M', 2.6, -9.4), ('L', 15.0, -8.6), ('L', 5.4, -11.6), ('L', 2.6, -16.0)), 6, 'YZ', 0)])
    body = path(('M', 16.0, -5.4), ('L', 66.0, -6.4), ('L', 66.0, -10.6), ('L', 30.0, -11.6), ('L', 16.0, -11.0))
    extrude('Body', body, 3.2, 'polymer_tan', bevel=.14)
    rail(18.0, 36.0, -4.8, 2.0, 'alu')
    grip('Grip', 27.0, 22.4, 8.4, 2.6, 3.0, 'polymer', z_top=-11.0)
    guard(27.2, 33.0, -11.2, 2.4, 'polymer')
    trigger(29.4, -11.2)
    # recurve limbs (seen from the side they fan above and below the body), cams, string
    for s in (1, -1):
        limb = [(s * 1.2, 62.0, -8.4), (s * 10.0, 60.0, -8.0), (s * 18.0, 56.0, -8.2), (s * 22.0, 51.0, -8.2)]
        tube('Limb', limb, .9, 'polymer_tan')
        lathe('Cam', [(-.5, 1.6), (.5, 1.6)], 'alu', at=(s * 22.4, 50.6, -8.2), axis='Z', seg=20, bevel=.04)
        tube('String', [(s * 22.4, 49.4, -8.2), (0, 40.0, -7.6)], .12, 'rubber', smooth=False)
    extrude('Limb pocket', rrect(60.0, -11.6, 66.4, -4.6, .6), 3.8, 'alu', bevel=.12)
    lathe('Bolt', [(0, .3), (36.0, .3)], 'alu', at=(0, 40.0, -5.0), seg=10, bevel=0)
    lathe('Broadhead', [(0, .5), (1.4, 1.1), (4.6, .05)], 'blade', at=(0, 76.0, -5.0), seg=4, bevel=0)
    extrude('Fletching', path(('M', 41.0, -4.8), ('L', 45.0, -4.8), ('L', 44.0, -3.4), ('L', 41.6, -3.4)), .12, 'ceramic_red', bevel=0)
    return place(25.0, 16.0, {'bore_z': -5.0, 'muzzle': (80.6, -5.0), 'optic': (27.0, -4.8), 'mag': (40.0, -5.0)})


@gun
def machete():
    """Machete: broad blade widening to an angled tip, wooden handle with three brass rivets."""
    hd = path(('M', -9.0, -1.4), ('Q', -9.6, 0, -9.0, 1.4), ('L', 7.0, 1.8), ('L', 7.0, -1.8))
    extrude('Handle', hd, 2.4, 'wood', crown=.3, inset=.4, bevel=.14, plane='YZ')
    for y in (-5.6, -1.0, 3.6):
        lathe('Rivet', [(-1.25, .38), (1.25, .38)], 'brass', at=(0, y, 0.0), axis='X', seg=12, bevel=.03)
    bl = path(('M', 7.0, 1.6), ('L', 44.0, 2.4), ('L', 48.0, 4.8), ('L', 50.6, 4.6), ('L', 50.0, -2.6), ('L', 7.0, -2.0))
    extrude('Blade', bl, .45, 'blade', bevel=.03)
    extrude('Blade spine', band([(7.2, 1.45), (44.0, 2.25)], .35), .55, 'blade', bevel=.02)
    return {'bore_z': 0, 'muzzle': (50.6, 0), 'optic': (0, 2), 'mag': (0, 0)}


@gun
def bat():
    """Baseball bat: worn wood, taped handle, knob."""
    lathe('Bat', [(0, 1.4), (.6, 1.9), (1.4, 1.9), (1.8, 1.25), (30.0, 1.4), (46.0, 2.9), (68.0, 3.3), (70.0, 3.1), (70.4, 2.4)], 'wood', at=(0, -6.0, 0), seg=28, bevel=.04)
    for i in range(10):
        lathe('Grip tape', [(-.55, 1.42), (.55, 1.42)], 'rubber', at=(0, -3.0 + i * 1.6, 0), seg=24, bevel=.02)
    return {'bore_z': 0, 'muzzle': (64.4, 0), 'optic': (0, 2), 'mag': (0, 0)}


@gun
def katana():
    """Katana: diamond-wrapped handle, red guard, slightly curved blade with a kissaki tip."""
    lathe('Tsuka', [(0, 1.2), (.6, 1.45), (25.0, 1.45), (25.4, 1.3)], 'polymer', at=(0, -13.0, 0), seg=16, bevel=.04)
    for i in range(9):
        y = -11.6 + i * 2.6
        extrude('Ito wrap', path(('M', y, -1.5), ('L', y + 1.3, 0.0), ('L', y, 1.5), ('L', y - 1.3, 0.0)), 3.05, 'ceramic_red', bevel=.04)
    lathe('Kashira', [(0, 1.5), (1.0, 1.5), (1.4, 1.0)], 'brass', at=(0, -14.0, 0), seg=16, bevel=.04)
    lathe('Tsuba', [(-.35, 4.2), (.35, 4.2)], 'ceramic_red', at=(0, 12.6, 0), axis='Y', seg=8, bevel=.06)
    lathe('Habaki', [(0, 1.3), (2.0, 1.2)], 'brass', at=(0, 13.0, 0), seg=12, bevel=.03)
    pts = []
    N = 16
    for i in range(N + 1):
        t = i / N
        y = 15.0 + 68.0 * t
        lift = 3.4 * t * t                  # sori: the blade curves up toward the tip
        w = 3.1 - 0.6 * t
        pts.append((y, lift + w * .5))
    top = pts
    bot = [(y, z - (3.1 - 0.6 * ((y - 15.0) / 68.0))) for y, z in pts]
    tip = (86.6, 3.4 + .6)
    outline = top + [tip] + bot[::-1]
    extrude('Blade', outline, .7, 'blade', taper=lambda a, b: 1.0, bevel=.02)
    return {'bore_z': 0, 'muzzle': (86.6, 3.4), 'optic': (0, 2), 'mag': (0, 0)}


@gun
def scythe():
    """Scythe: long dark pole with grip wraps and a big crescent blade at the top."""
    lathe('Pole', [(0, 1.05), (110.0, 1.05)], 'wood_dark', at=(0, -30.0, 0), seg=16, bevel=.03)
    for y in (-2.0, 30.0):
        for i in range(5):
            lathe('Wrap', [(-.4, 1.15), (.4, 1.15)], 'rubber', at=(0, y + i * 1.0, 0), seg=16, bevel=.02)
    lathe('Collar', [(0, 1.5), (4.0, 1.5)], 'steel', at=(0, 76.0, 0), seg=16, bevel=.04)
    blade = path(('M', 79.0, 1.2), ('C', 92.0, 4.0, 104.0, -6.0, 106.0, -30.0), ('C', 100.0, -14.0, 92.0, -4.0, 79.0, -1.6))
    extrude('Blade', blade, .55, 'blade', bevel=.03)
    return {'bore_z': 0, 'muzzle': (106.0, -30.0), 'optic': (0, 2), 'mag': (0, 0)}


@gun
def pan():
    """Frying pan: thick cast-iron pan with rim and riveted handle."""
    lathe('Pan', [(0, .05), (0, 12.6), (.3, 13.4), (3.6, 13.8), (3.6, 13.2), (.6, 12.4), (.6, .05)], 'steel', at=(0, 26.0, 0), axis='X', seg=48, closed=False, bevel=.05)
    extrude('Handle', path(('M', -6.0, -1.2), ('Q', -7.0, 0, -6.0, 1.2), ('L', 12.4, 1.6), ('L', 12.4, -1.6)), 2.0, 'steel', crown=.2, inset=.3, bevel=.12)
    for y in (10.6, 11.8):
        lathe('Rivet', [(-1.2, .3), (1.2, .3)], 'steel_bright', at=(0, y, 0), axis='X', seg=10, bevel=.02)
    return {'bore_z': 0, 'muzzle': (39.8, 0), 'optic': (0, 2), 'mag': (0, 0)}


@gun
def knife():
    """Combat knife: textured handle, crossguard, clip-point blade with fuller."""
    lathe('Handle', [(0, 1.1), (.6, 1.45), (5.0, 1.3), (9.6, 1.45), (10.4, 1.2)], 'rubber', at=(0, -10.0, 0), seg=16, bevel=.04)
    lathe('Pommel', [(0, .9), (.8, 1.2), (1.2, .8)], 'steel', at=(0, -11.0, 0), seg=16, bevel=.03)
    extrude('Guard', rrect(.4, -2.4, 1.2, 2.4, .3), 1.4, 'steel', bevel=.06)
    bl = path(('M', 1.2, 1.4), ('L', 12.0, 1.5), ('L', 15.0, 1.0), ('L', 18.0, .2), ('Q', 15.0, -1.6, 11.0, -1.7), ('L', 1.2, -1.6))
    extrude('Blade', bl, .5, 'blade', bevel=.03)
    extrude('Fuller', band([(3.0, .2), (11.0, .2)], .5), .52, 'steel', bevel=.02)
    return {'bore_z': 0, 'muzzle': (18.0, .2), 'optic': (0, 2), 'mag': (0, 0)}


@gun
def plasma():
    """Original sci-fi plasma rifle: white ceramic shell over a black frame, ring-hole stock,
    cyan energy coils along the barrel, red trim, top optic."""
    fr = path(('M', 0.0, -6.0), ('L', 30.0, -6.0), ('L', 34.0, -9.0), ('L', 52.0, -9.0), ('L', 52.0, -12.4), ('L', 30.0, -12.4), ('L', 26.0, -18.0),
              ('L', 20.0, -20.4), ('L', 2.0, -16.0), ('L', 0.0, -12.0))
    extrude('Frame', fr, 3.2, 'polymer', bevel=.16, cuts=[cprism(rrect(10.0, -16.4, 20.0, -9.0, 3.4), 6, 'YZ', 0)])
    sh = path(('M', 24.0, -3.4), ('L', 54.0, -4.4), ('Q', 58.0, -5.0, 58.0, -7.0), ('L', 58.0, -9.4), ('L', 24.0, -10.0))
    extrude('Shell', sh, 4.0, 'ceramic', crown=.4, inset=.5, bevel=.18)
    extrude('Red trim', band([(26.0, -8.6), (56.0, -8.2)], .6), 4.15, 'ceramic_red', bevel=.06)
    grip('Grip', 30.0, 26.0, 9.0, 2.6, 3.0, 'polymer', z_top=-12.4)
    trigger(32.0, -12.4)
    lathe('Barrel', [(0, .9), (30.0, .9)], 'steel', at=(0, 52.0, -7.0), seg=16, bevel=.03)
    for i in range(6):
        lathe('Energy coil', [(-.5, 1.5), (.5, 1.5)], 'glow_cyan', at=(0, 60.0 + i * 3.4, -7.0), seg=20, bevel=.02)
    lathe('Emitter', [(0, .7), (0, 1.6), (3.0, 1.2), (3.0, .7)], 'ceramic', at=(0, 82.0, -7.0), seg=20, closed=True, bevel=.04)
    extrude('Energy cell', rrect(38.0, -10.2, 48.0, -8.8, .5), 4.3, 'glow_cyan', bevel=.06)
    lathe('Optic', [(0, 1.0), (.4, 1.3), (12.0, 1.3), (12.0, 1.1)], 'ceramic', at=(0, 30.0, -.4), seg=22, bevel=.04)
    lathe('Optic lens', [(0, 1.0), (.06, 1.0)], 'glow_red', at=(0, 42.0, -.4), seg=20, bevel=0)
    extrude('Optic mount', rrect(33.0, -3.6, 39.0, -1.2, .4), 2.0, 'polymer', bevel=.08)
    return place(28.0, 20.0, {'bore_z': -7.0, 'muzzle': (85.0, -7.0), 'optic': (36.0, -.4), 'mag': (43.0, -9.5)})


@gun
def charge_buster():
    """Original sci-fi charge gun: thick layered white shell, black skeleton stock with a grip ring,
    red charge strip and a glowing chamber window."""
    st = path(('M', 0.0, -4.0), ('L', 20.0, -6.0), ('L', 28.0, -10.0), ('L', 26.0, -20.0), ('L', 18.0, -22.0), ('L', 4.0, -16.0), ('L', 0.0, -14.0))
    extrude('Stock', st, 3.2, 'polymer', bevel=.16, cuts=[cprism(rrect(12.0, -18.0, 22.0, -10.0, 3.6), 6, 'YZ', 0)])
    sh = path(('M', 18.0, -1.4), ('L', 66.0, -2.4), ('Q', 70.0, -3.0, 70.0, -6.0), ('L', 70.0, -12.0), ('Q', 69.4, -14.0, 66.0, -14.0), ('L', 26.0, -14.0), ('L', 18.0, -8.0))
    extrude('Shell', sh, 5.0, 'ceramic', crown=.5, inset=.7, bevel=.2)
    extrude('Lower shell', rrect(30.0, -15.4, 62.0, -11.6, .8), 4.4, 'polymer', bevel=.16)
    extrude('Charge strip', band([(28.0, -6.6), (64.0, -6.8)], 1.2), 5.2, 'ceramic_red', bevel=.08)
    extrude('Chamber window', rrect(40.0, -11.0, 54.0, -8.6, .6), 5.25, 'glow_cyan', bevel=.06)
    grip('Grip', 28.0, 24.0, 8.0, 2.4, 3.0, 'polymer', z_top=-14.0)
    trigger(30.0, -14.0)
    lathe('Muzzle', [(0, 1.0), (0, 2.0), (5.0, 2.0), (5.0, 1.0)], 'steel', at=(0, 70.0, -8.0), seg=22, closed=True, bevel=.05)
    lathe('Muzzle glow', [(0, 1.0), (.1, 1.0)], 'glow_cyan', at=(0, 74.9, -8.0), seg=20, bevel=0)
    extrude('Top fin', path(('M', 30.0, -1.5), ('L', 52.0, -2.2), ('L', 50.0, .6), ('L', 34.0, .8)), 1.4, 'ceramic', bevel=.1)
    return place(27.0, 20.0, {'bore_z': -8.0, 'muzzle': (75.0, -8.0), 'optic': (42.0, .8), 'mag': (47.0, -10.0)})


@gun
def healing_pistol():
    """Original sci-fi healing pistol: white medical shell with red cross panels, black lower frame,
    red grip, glowing green-cyan dart chamber."""
    sh = path(('M', 4.0, -1.0), ('L', 30.0, -1.4), ('Q', 32.0, -1.6, 32.0, -3.4), ('L', 32.0, -7.6), ('L', 4.0, -7.6), ('L', 3.0, -4.0))
    extrude('Shell', sh, 4.6, 'ceramic', crown=.4, inset=.6, bevel=.18)
    extrude('Red panel', rrect(12.0, -6.4, 28.0, -2.6, .6), 4.75, 'ceramic_red', bevel=.08)
    for s in (1, -1):
        extrude('Cross', path(('M', 7.0, -3.4), ('L', 8.0, -3.4), ('L', 8.0, -2.6), ('L', 8.8, -2.6), ('L', 8.8, -1.8), ('L', 8.0, -1.8), ('L', 8.0, -1.0), ('L', 7.0, -1.0),
                                  ('L', 7.0, -1.8), ('L', 6.2, -1.8), ('L', 6.2, -2.6), ('L', 7.0, -2.6)), .2, 'ceramic_red', at=s * 2.35, bevel=.02)
    extrude('Lower frame', rrect(10.0, -12.6, 31.0, -7.4, .8), 4.0, 'polymer', bevel=.16)
    lathe('Chamber', [(0, 1.3), (14.0, 1.3)], 'glow_cyan', at=(0, 14.0, -10.0), seg=20, bevel=.03)
    gp = path(('M', 9.0, -7.6), ('L', 4.0, -7.6), ('C', 3.4, -10.0, 2.6, -13.0, 2.0, -15.6), ('Q', 2.0, -16.6, 3.2, -16.6), ('L', 5.6, -16.4),
              ('Q', 6.6, -16.2, 6.6, -15.2), ('C', 7.0, -12.4, 8.0, -9.8, 9.0, -7.6))
    extrude('Grip', gp, 3.2, 'ceramic_red', crown=.3, inset=.45, bevel=.12)
    trigger(10.4, -12.6)
    lathe('Nozzle', [(0, .6), (0, 1.4), (2.0, 1.0), (2.0, .6)], 'steel', at=(0, 31.6, -9.6), seg=18, closed=True, bevel=.04)
    return place(6.0, 16.0, {'bore_z': -9.6, 'muzzle': (33.6, -9.6), 'optic': (18.0, -1.4), 'mag': (21.0, -10.0)})
