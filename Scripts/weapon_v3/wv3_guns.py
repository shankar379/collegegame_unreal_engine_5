"""Weapon Library V3 - the guns.

Each function builds one gun from real-world proportions (cm), grip at the origin,
bore axis along +Y. Returns sockets: muzzle, optic rail, magazine, and the bore height.
Part kinds and skin flags are explained in wv3_kit.py.
"""
import math
from wv3_kit import (path, rrect, band, arc, extrude, box, lathe, tube, cbox, ccyl, cprism, mirror_x)

GUNS = {}


def gun(fn):
    GUNS[fn.__name__] = fn
    return fn


def rivet(x, y, z, r=.28, kind='steel', side=1):
    """Dome-headed rivet/pin on the side of a receiver (side=+1 right, -1 left)."""
    return lathe('Rivet', [(side * a, b) for a, b in [(0, r), (.12, r), (.2, r * .8), (.26, r * .45), (.28, .05)]], kind, at=(x, y, z), axis='X', seg=12, bevel=0)
