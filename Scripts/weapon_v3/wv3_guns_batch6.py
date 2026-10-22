"""Weapon Library V3 - batch 6: pistols, revolvers, grenade launchers (USP, G18, M500, M1917, M79, MGL).
Reference coordinates (y from the rear, z below the top), then place() puts the grip at the origin."""
import math
from wv3_kit import (path, rrect, band, arc, extrude, lathe, tube, cbox, ccyl, cprism)
from wv3_guns import gun
from wv3_guns_batch2 import trigger, guard
from wv3_guns_snipers import scope, ring_mount
from wv3_guns_batch4 import place


def slide_pistol(L, top, slide_h, frame_h, grip_top, grip_bot, rake, kind_slide, kind_frame, boxy=False):
    """Modern polymer-frame pistol: slide with rear serrations and ejection port, dust-cover frame,
    rounded trigger guard, raked grip with texture panel, magazine base, sights."""
    zb = top - slide_h
    sl = path(('M', 0.0, zb), ('L', 0.0, top - .5), ('L', .6, top), ('L', L - .6, top), ('L', L, top - (.8 if not boxy else .2)), ('L', L, zb))
    cuts = [cbox((1.5, L * .45, top - slide_h * .42), (.5, L * .25, slide_h * .38))]
    cuts += [cbox((s * 1.5, .8 + i * .55, top - slide_h * .55), (.4, .22, slide_h * .7)) for s in (1, -1) for i in range(5)]
    extrude('Slide', sl, 2.9, kind_slide, bevel=.1, cuts=cuts)
    lathe('Bore', [(0, .45), (.2, .45)], 'bore', at=(0, L - .1, top - slide_h * .45), seg=14, bevel=0)
    extrude('Front sight', rrect(L - 1.6, top - .05, L - .8, top + .6, .1), .5, 'steel', bevel=.02)
    extrude('Rear sight', rrect(.6, top - .05, 1.6, top + .6, .1), 1.6, 'steel', bevel=.02)
    fr = path(('M', 1.0, zb + .05), ('L', L - .4, zb + .05), ('L', L - .4, zb - frame_h), ('L', 13.0 * L / 20, zb - frame_h), ('L', 4.0, zb - frame_h * .6), ('L', 1.0, zb - .6))
    extrude('Frame', fr, 2.6, kind_frame, bevel=.1)
    gy0, gy1 = 6.2 * L / 20, 12.6 * L / 20
    guard(gy0, gy1, zb - frame_h + .2, 2.4, kind_frame, width=1.1)
    trigger(gy0 + 1.8, zb - frame_h + .2)
    gw = grip_top[1] - grip_top[0]          # grip width front-to-back
    r0, r1 = grip_top[0], grip_top[1]        # rear / front at the top
    b0, bz = grip_bot                        # rear x and height at the bottom
    gp = path(('M', r1, zb - .2), ('L', r0, zb - .2),
              ('C', r0 - rake * .3, zb - 3.0, b0 + .2, bz + 3.0, b0, bz + .6), ('Q', b0, bz, b0 + .8, bz),
              ('L', b0 + gw - .6, bz), ('Q', b0 + gw + .2, bz, b0 + gw, bz + .8),
              ('C', r1 - rake * .5, bz + 4.0, r1 - .2, zb - 2.6, r1, zb - .2))
    opening = [(b0 + .8, bz - .4), (b0 + gw - .8, bz - .4), (r1 - .9, zb - 1.0), (r0 + .9, zb - 1.0)]
    extrude('Grip', gp, 3.0, kind_frame, crown=.2, inset=.35, bevel=.12,
            cuts=[cprism(opening, 2.0, 'YZ', 0)])
    extrude('Grip texture', path(('M', r0 + .6, zb - 2.0), ('L', r1 - 1.0, zb - 2.0), ('L', b0 + gw - 1.4, bz + 1.2), ('L', b0 + .6, bz + 1.2)), 3.15, 'rubber', bevel=.06,
            cuts=[cprism(opening, 2.0, 'YZ', 0)])
    extrude('Magazine', [(b0 + 1.0, bz), (b0 + gw - 1.0, bz), (r1 - 1.1, zb - 1.2), (r0 + 1.1, zb - 1.2)], 1.8, 'steel', bevel=.06)
    extrude('Magazine base', rrect(b0 - .1, bz - .7, b0 + gw + .1, bz + .1, .25), 2.9, 'steel', bevel=.06)


@gun
def usp():
    """Free Fire USP: black slide with front and rear serrations, dust-cover frame, rounded guard, raked grip."""
    slide_pistol(19.6, -.4, 4.0, 1.2, (.6, 7.0), (1.0, -13.4), 1.0, 'blued', 'polymer')
    return place(4.0, 9.0, {'bore_z': -2.2, 'muzzle': (19.6, -2.2), 'optic': (8.0, -.4), 'mag': (3.6, -9.0)})


@gun
def g18():
    """Free Fire G18: boxy slide, polymer frame, steep grip angle."""
    slide_pistol(20.0, 0.0, 3.8, 1.6, (1.4, 7.0), (.4, -14.0), 2.2, 'blued', 'polymer', boxy=True)
    lathe('Selector', [(0, .45), (.2, .45)], 'steel', at=(-1.5, 1.4, -2.2), axis='X', seg=12, bevel=0)
    return place(4.0, 9.0, {'bore_z': -1.8, 'muzzle': (20.0, -1.8), 'optic': (8.0, 0.0), 'mag': (3.6, -9.0)})


def revolver(cyl_y, cyl_len, cyl_r, axis_z, barrel_end, barrel_r, top, grip_kind, frame_kind='blued', underlug=False):
    fr = path(('M', 2.6, top - .6), ('L', cyl_y + cyl_len + 1.6, top - .6), ('L', cyl_y + cyl_len + 1.6, axis_z - cyl_r - .2), ('L', cyl_y - .4, axis_z - cyl_r - .6),
              ('L', 4.2, axis_z - cyl_r - 2.4), ('L', 2.0, axis_z - cyl_r - 1.0))
    extrude('Frame', fr, 2.4, frame_kind, bevel=.1, cuts=[cprism(rrect(cyl_y - .2, axis_z - cyl_r - .1, cyl_y + cyl_len + .2, axis_z + cyl_r + .1, .2), 3.0, 'YZ', 0)])
    lathe('Cylinder', [(0, cyl_r * .85), (.3, cyl_r), (cyl_len - .3, cyl_r), (cyl_len, cyl_r * .85)], frame_kind, at=(0, cyl_y, axis_z), seg=24, bevel=.03,
          cuts=[ccyl((math.cos(a) * cyl_r * 1.05, cyl_y + cyl_len / 2, axis_z + math.sin(a) * cyl_r * 1.05), cyl_r * .28, cyl_len * .7, 'Y', 12)
                for a in [k * math.pi / 3 + math.pi / 6 for k in range(6)]])
    lathe('Barrel', [(0, barrel_r * 1.15), (1.0, barrel_r), (barrel_end - cyl_y - cyl_len - 1.2, barrel_r)], frame_kind, at=(0, cyl_y + cyl_len + 1.2, axis_z + .2), seg=20, bevel=.03)
    if underlug:
        extrude('Underlug', rrect(cyl_y + cyl_len + 1.0, axis_z - barrel_r - 1.0, barrel_end - .2, axis_z - barrel_r + .6, .4), barrel_r * 1.6, frame_kind, bevel=.1)
    extrude('Top strap rib', rrect(cyl_y - 1.0, top - .7, barrel_end - .2, top, .2), 1.0, frame_kind, bevel=.04)
    extrude('Front sight', path(('M', barrel_end - 1.4, top - .1), ('L', barrel_end - .3, top - .1), ('L', barrel_end - .5, top + .7), ('L', barrel_end - 1.2, top + .7)), .4, 'steel', bevel=.02)
    extrude('Hammer', path(('M', 3.0, top - .8), ('L', 4.2, top - .8), ('L', 3.6, top + .9), ('L', 2.2, top + 1.2), ('L', 2.6, top + .2)), .8, 'steel', bevel=.04)
    gd = path(('M', cyl_y - .8, axis_z - cyl_r - .5), ('C', cyl_y - 1.2, axis_z - cyl_r - 3.4, cyl_y + 3.0, axis_z - cyl_r - 3.6, cyl_y + 3.2, axis_z - cyl_r - .6), n=10)
    extrude('Trigger guard', band(gd, .45), 1.1, frame_kind, bevel=.06)
    trigger(cyl_y + .8, axis_z - cyl_r - .6)
    gp = path(('M', 5.4, axis_z - cyl_r - 1.2), ('L', 2.0, top - 1.6), ('C', .4, top - 5.0, -.4, top - 10.0, 0.0, top - 13.4), ('Q', .2, top - 14.6, 1.6, top - 14.6),
              ('L', 4.4, top - 14.4), ('Q', 5.6, top - 14.2, 5.4, top - 13.0), ('C', 4.6, top - 10.0, 4.6, top - 7.0, 5.4, axis_z - cyl_r - 1.2))
    extrude('Grip', gp, 3.0, grip_kind, crown=.35, inset=.5, bevel=.12)


@gun
def m500():
    """Free Fire M500: big-frame revolver, long barrel with underlug, scope, orange-brown wood grip."""
    revolver(7.8, 7.0, 2.6, -6.8, 35.0, 1.15, -4.2, 'wood', 'steel', underlug=True)
    scope(5.0, 20.0, -1.6, tube_r=.95, bell_r=1.25, eye_r=1.25, kind='steel', turret=True)
    ring_mount(9.0, -4.3, -1.6, .95, 'steel')
    ring_mount(21.0, -4.3, -1.6, .95, 'steel')
    return place(4.0, 15.0, {'bore_z': -6.6, 'muzzle': (35.0, -6.6), 'optic': (15.0, -1.6), 'mag': (11.3, -6.8)})


@gun
def m1917():
    """Free Fire M1917: classic blued revolver, slim barrel, round trigger guard, dark wood grip."""
    revolver(8.6, 5.4, 2.3, -4.6, 27.0, .75, -1.6, 'wood_dark', 'blued')
    return place(4.0, 13.0, {'bore_z': -4.4, 'muzzle': (27.0, -4.4), 'optic': (12.0, -1.6), 'mag': (11.3, -4.6)})


@gun
def m79():
    """Free Fire M79: orange-brown wood stock, short receiver, fat blued barrel with leaf sight."""
    st = path(('M', 0.0, -9.0), ('L', 14.0, -9.6), ('L', 22.0, -12.4), ('L', 31.0, -12.0), ('L', 31.0, -17.4), ('L', 24.0, -18.4), ('L', 18.0, -17.0),
              ('L', 10.0, -20.4), ('L', 0.0, -20.6))
    extrude('Stock', st, 4.0, 'wood', crown=.45, inset=.6, bevel=.15)
    extrude('Butt pad', rrect(-1.2, -20.8, .2, -8.8, .5), 4.2, 'rubber', bevel=.12)
    extrude('Receiver', rrect(29.0, -17.6, 37.4, -9.2, .6), 4.0, 'blued', bevel=.14)
    guard(29.6, 35.0, -17.4, 2.4, 'blued', width=1.2)
    trigger(31.6, -17.4)
    lathe('Barrel', [(0, 2.4), (.8, 2.8), (35.0, 2.8), (35.6, 2.5), (35.6, 2.0)], 'blued', at=(0, 37.0, -12.6), seg=32, bevel=.05)
    lathe('Bore', [(0, 2.0), (.1, 2.0)], 'bore', at=(0, 72.5, -12.6), seg=28, bevel=0)
    extrude('Forend', path(('M', 37.6, -14.6), ('L', 58.0, -14.6), ('L', 58.0, -16.6), ('L', 37.6, -17.6)), 3.6, 'wood', bevel=.12)
    extrude('Leaf sight', path(('M', 53.6, -9.9), ('L', 55.0, -9.9), ('L', 55.2, -2.2), ('L', 54.4, -2.2)), .5, 'steel', bevel=.03)
    extrude('Front sight', path(('M', 70.4, -9.9), ('L', 72.0, -9.9), ('L', 71.6, -8.6), ('L', 70.8, -8.6)), .6, 'steel', bevel=.03)
    return place(33.0, 20.0, {'bore_z': -12.6, 'muzzle': (72.6, -12.6), 'optic': (54.0, -9.8), 'mag': (40.0, -12.6)})


@gun
def mgl():
    """Free Fire MGL: six-shot revolving cylinder, skeleton stock arm, pistol grip, long launch tube,
    vertical foregrip."""
    extrude('Stock arm', path(('M', 0.0, -4.0), ('L', 40.0, -6.4), ('L', 40.0, -8.0), ('L', 2.0, -6.0)), 2.2, 'polymer', bevel=.12)
    extrude('Butt plate', rrect(-.6, -12.6, 1.6, -3.4, .5), 3.6, 'rubber', bevel=.12)
    extrude('Frame', path(('M', 22.0, -11.0), ('L', 50.0, -11.0), ('L', 50.0, -16.6), ('L', 32.0, -16.6), ('L', 22.0, -15.0)), 3.0, 'polymer', bevel=.14)
    lathe('Cylinder', [(0, 5.0), (.6, 5.9), (17.0, 5.9), (17.6, 5.0)], 'steel', at=(0, 32.6, -12.8), seg=36, bevel=.05,
          cuts=[cbox((math.cos(a) * 6.2, 41.0, -12.8 + math.sin(a) * 6.2), (1.6, 14.0, 1.6)) for a in [k * math.pi / 3 for k in range(6)]])
    for a in [k * math.pi / 3 + math.pi / 6 for k in range(6)]:
        lathe('Chamber', [(0, 1.9), (.2, 1.9)], 'bore', at=(math.cos(a) * 3.3, 50.1, -12.8 + math.sin(a) * 3.3), seg=18, bevel=0)
    from wv3_guns_batch2 import grip
    grip('Grip', 28.0, 23.6, 9.6, 3.0, 3.2, 'polymer', z_top=-15.0)
    guard(28.2, 32.4, -15.0, 2.6, 'polymer')
    trigger(30.0, -15.0)
    lathe('Launch tube', [(0, 2.4), (28.0, 2.4), (28.4, 2.1)], 'steel', at=(0, 50.0, -10.0), seg=30, bevel=.04)
    lathe('Tube bore', [(0, 1.95), (.1, 1.95)], 'bore', at=(0, 78.3, -10.0), seg=24, bevel=0)
    lathe('Foregrip', [(0, 1.15), (6.6, 1.25), (7.2, .9)], 'polymer', at=(0, 60.6, -19.4), axis='Z', seg=16, bevel=.04)
    extrude('Sight', path(('M', 44.0, -7.5), ('L', 47.0, -7.5), ('L', 46.6, -5.6), ('L', 44.4, -5.6)), 1.4, 'polymer', bevel=.06)
    return place(27.0, 22.0, {'bore_z': -10.0, 'muzzle': (78.4, -10.0), 'optic': (45.0, -5.6), 'mag': (41.0, -12.8)})
