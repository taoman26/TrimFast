#!/usr/bin/env python3
"""Minimal reader and writer for Haiku's vector icon format (HVIF).

Format (little endian), as used by Icon-O-Matic / BIconUtils:

    "ncif"
    u8 style_count, styles...      1 = RGBA, 3 = RGB, 4 = gray+alpha, 5 = gray, 2 = gradient
    u8 path_count, paths...        flags(u8) [closed=1<<1, uses_commands=1<<2, no_curves=1<<3], npoints(u8), data
    u8 shape_count, shapes...      type 10 (path source), style(u8), npaths(u8), path ids(u8...), flags(u8), ...

Coordinates live in a 64x64 space. A coordinate is one byte (value + 32, whole numbers in
[-32, 95]) or two bytes (high bit set; 15-bit value = (v + 128) * 102).

The reader exists to check the format knowledge against real system icons; the writer builds
TrimFast's icon (make_icon.py).
"""
import struct

PATH_CLOSED = 1 << 1
PATH_USES_COMMANDS = 1 << 2
PATH_NO_CURVES = 1 << 3

GRAD_TRANSFORM = 1 << 1
GRAD_NO_ALPHA = 1 << 2
GRAD_GRAY = 1 << 4

SHAPE_PATH_SOURCE = 10
SHAPE_TRANSFORM = 1 << 1
SHAPE_HINTING = 1 << 2
SHAPE_LOD_SCALE = 1 << 3
SHAPE_HAS_TRANSFORMERS = 1 << 4
SHAPE_TRANSLATION = 1 << 5


class Reader:
    def __init__(self, data):
        self.d = data
        self.i = 0

    def u8(self):
        v = self.d[self.i]
        self.i += 1
        return v

    def coord(self):
        b = self.u8()
        if b & 0x80:
            b2 = self.u8()
            return (((b & 0x7F) << 8) | b2) / 102.0 - 128.0
        return b - 32.0

    def float24(self):
        # 24-bit float: 1 sign, 6 exponent (bias 31), 17 mantissa
        b = (self.u8() << 16) | (self.u8() << 8) | self.u8()
        sign = (b >> 23) & 1
        exp = (b >> 17) & 0x3F
        mant = b & 0x1FFFF
        if exp == 0 and mant == 0:
            return 0.0
        val = (1 + mant / 131072.0) * 2.0 ** (exp - 31)
        return -val if sign else val


def read_hvif(data):
    if data[:4] != b"ncif":
        raise ValueError("not an HVIF file")
    r = Reader(data)
    r.i = 4
    styles, paths, shapes = [], [], []
    for _ in range(r.u8()):
        t = r.u8()
        if t == 1:
            styles.append(("rgba", tuple(r.u8() for _ in range(4))))
        elif t == 3:
            styles.append(("rgb", tuple(r.u8() for _ in range(3))))
        elif t == 4:
            styles.append(("gray", (r.u8(), r.u8())))   # gray + alpha
        elif t == 5:
            styles.append(("gray", (r.u8(), 255)))      # gray
        elif t == 2:
            gtype = r.u8()
            flags = r.u8()
            n = r.u8()
            matrix = [r.float24() for _ in range(6)] if flags & GRAD_TRANSFORM else None
            stops = []
            for _ in range(n):
                off = r.u8() / 255.0
                if flags & GRAD_GRAY:
                    g = r.u8()
                    a = 255 if flags & GRAD_NO_ALPHA else r.u8()
                    stops.append((off, (g, g, g, a)))
                elif flags & GRAD_NO_ALPHA:
                    stops.append((off, (r.u8(), r.u8(), r.u8(), 255)))
                else:
                    stops.append((off, tuple(r.u8() for _ in range(4))))
            styles.append(("gradient", gtype, matrix, stops))
        else:
            raise ValueError("unknown style type %d at %d" % (t, r.i))
    for _ in range(r.u8()):
        flags = r.u8()
        n = r.u8()
        pts = []
        if flags & PATH_NO_CURVES:
            for _ in range(n):
                x = r.coord(); y = r.coord()
                pts.append(((x, y), (x, y), (x, y)))
        elif flags & PATH_USES_COMMANDS:
            cmds = []
            nbytes = (n + 3) // 4
            raw = [r.u8() for _ in range(nbytes)]
            for k in range(n):
                cmds.append((raw[k // 4] >> ((k % 4) * 2)) & 3)
            cx = cy = 0.0
            for c in cmds:
                if c == 0:      # horizontal line
                    cx = r.coord(); p = (cx, cy); pts.append((p, p, p))
                elif c == 1:    # vertical line
                    cy = r.coord(); p = (cx, cy); pts.append((p, p, p))
                elif c == 2:
                    cx = r.coord(); cy = r.coord(); p = (cx, cy); pts.append((p, p, p))
                else:
                    cx = r.coord(); cy = r.coord()
                    ix = r.coord(); iy = r.coord(); ox = r.coord(); oy = r.coord()
                    pts.append(((cx, cy), (ix, iy), (ox, oy)))
        else:
            for _ in range(n):
                p = (r.coord(), r.coord()); i = (r.coord(), r.coord()); o = (r.coord(), r.coord())
                pts.append((p, i, o))
        paths.append({"closed": bool(flags & PATH_CLOSED), "points": pts})
    for _ in range(r.u8()):
        t = r.u8()
        if t != SHAPE_PATH_SOURCE:
            raise ValueError("unknown shape type %d at %d" % (t, r.i))
        style = r.u8()
        ids = [r.u8() for _ in range(r.u8())]
        flags = r.u8()
        shape = {"style": style, "paths": ids, "transformers": []}
        if flags & SHAPE_TRANSFORM:
            shape["matrix"] = [r.float24() for _ in range(6)]
        if flags & SHAPE_TRANSLATION:
            shape["translation"] = (r.coord(), r.coord())
        if flags & SHAPE_LOD_SCALE:
            shape["lod"] = (r.u8(), r.u8())
        if flags & SHAPE_HAS_TRANSFORMERS:
            for _ in range(r.u8()):
                tt = r.u8()
                if tt == 20:   # affine
                    shape["transformers"].append(("affine", [r.float24() for _ in range(6)]))
                elif tt == 21:  # contour
                    shape["transformers"].append(("contour", r.u8() - 128, r.u8(), r.u8()))
                elif tt == 22:  # perspective
                    shape["transformers"].append(("perspective",))
                elif tt == 23:  # stroke
                    shape["transformers"].append(("stroke", r.u8(), r.u8(), r.u8()))
                else:
                    raise ValueError("unknown transformer %d at %d" % (tt, r.i))
        shapes.append(shape)
    return {"styles": styles, "paths": paths, "shapes": shapes, "consumed": r.i, "size": len(data)}


# ----------------------------------------------------------------------------- writer

def _coord(v):
    if abs(v - round(v)) < 1e-9 and -32 <= round(v) <= 95:
        return bytes([int(round(v)) + 32])
    n = int(round((v + 128.0) * 102.0))
    if not 0 <= n < 0x8000:
        raise ValueError("coordinate out of range: %r" % v)
    return bytes([0x80 | (n >> 8), n & 0xFF])


def write_hvif(styles, paths, shapes):
    """styles: [(r,g,b,a)]; paths: [(points, closed)] where points is a list of (x,y) corners
    (straight edges) or of ((x,y),(in_x,in_y),(out_x,out_y)) for curves;
    shapes: [(style_index, [path_index, ...])]."""
    out = bytearray(b"ncif")
    out.append(len(styles))
    for (r, g, b, a) in styles:
        if a == 255:
            out += bytes([3, r, g, b])
        else:
            out += bytes([1, r, g, b, a])
    out.append(len(paths))
    for points, closed in paths:
        curved = isinstance(points[0][0], tuple)
        flags = PATH_CLOSED if closed else 0
        if not curved:
            flags |= PATH_NO_CURVES
        out += bytes([flags, len(points)])
        for p in points:
            if curved:
                for xy in p:
                    out += _coord(xy[0]) + _coord(xy[1])
            else:
                out += _coord(p[0]) + _coord(p[1])
    out.append(len(shapes))
    for style, ids in shapes:
        out += bytes([SHAPE_PATH_SOURCE, style, len(ids)]) + bytes(ids) + bytes([0])
    return bytes(out)


if __name__ == "__main__":
    import sys, json
    d = open(sys.argv[1], "rb").read()
    icon = read_hvif(d)
    print("styles %d, paths %d, shapes %d; consumed %d of %d bytes"
          % (len(icon["styles"]), len(icon["paths"]), len(icon["shapes"]), icon["consumed"], icon["size"]))
