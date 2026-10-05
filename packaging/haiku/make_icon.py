#!/usr/bin/env python3
"""Draws TrimFast's icon once and writes it in every format we need.

    python3 packaging/haiku/make_icon.py

writes:
    packaging/haiku/trimfast.hvif       Haiku vector icon (binary)
    packaging/haiku/trimfast_icon.rdef  the same, as a resource definition for `rc`
    packaging/linux/icons/              trimfast.svg and hicolor/<n>x<n>/apps/trimfast.png
    docs/icon.png, docs/icon_sizes.png  previews

The icon is a video player: a screen with a play mark, and below it a timeline whose
yellow selection is fenced by two red IN/OUT markers. Flat colours, 64x64 units.
"""
import os
from PIL import Image, ImageDraw

import hvif

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(HERE, "..", "..", "docs")

KAPPA = 0.5522847498

# ---- palette (matches the app: the blue of its range, Haiku's yellow) --------------------
BODY = (0x26, 0x48, 0x6F, 255)
SCREEN = (0x6F, 0xA8, 0xDC, 255)
PLAY = (255, 255, 255, 255)
TRACK = (0xE6, 0xEC, 0xF3, 255)
RANGE = (0xFF, 0xCB, 0x00, 255)
MARK = (0xD9, 0x3A, 0x2B, 255)


def rounded_rect(x0, y0, x1, y1, r):
    """Closed path with curved corners: a list of (point, in-handle, out-handle)."""
    k = KAPPA * r
    pts = [
        ((x0 + r, y0), (x0 + r - k, y0), (x0 + r, y0)),
        ((x1 - r, y0), (x1 - r, y0), (x1 - r + k, y0)),
        ((x1, y0 + r), (x1, y0 + r - k), (x1, y0 + r)),
        ((x1, y1 - r), (x1, y1 - r), (x1, y1 - r + k)),
        ((x1 - r, y1), (x1 - r + k, y1), (x1 - r, y1)),
        ((x0 + r, y1), (x0 + r, y1), (x0 + r - k, y1)),
        ((x0, y1 - r), (x0, y1 - r + k), (x0, y1 - r)),
        ((x0, y0 + r), (x0, y0 + r), (x0, y0 + r - k)),
    ]
    return pts


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


# ---- the drawing: shapes from back to front -----------------------------------------------
styles = [BODY, SCREEN, PLAY, TRACK, RANGE, MARK]
S_BODY, S_SCREEN, S_PLAY, S_TRACK, S_RANGE, S_MARK = range(6)

paths = [
    (rounded_rect(2, 8, 62, 56, 7), True),        # 0 body
    (rounded_rect(6, 12, 58, 40, 3.5), True),     # 1 screen
    ([(27, 17), (27, 35), (42, 26)], True),       # 2 play mark
    (rect(6, 45, 58, 52), True),                  # 3 timeline track
    (rect(19, 45, 47, 52), True),                 # 4 selected range
    (rect(16, 41, 20, 55), True),                 # 5 IN marker
    (rect(46, 41, 50, 55), True),                 # 6 OUT marker
]
shapes = [
    (S_BODY, [0]),
    (S_SCREEN, [1]),
    (S_PLAY, [2]),
    (S_TRACK, [3]),
    (S_RANGE, [4]),
    (S_MARK, [5, 6]),
]


# ---- rendering (preview only; Haiku draws the real thing) -----------------------------------
def flatten(points, closed, steps=14):
    if not isinstance(points[0][0], tuple):
        return [tuple(p) for p in points]
    out = []
    n = len(points)
    for i in range(n if closed else n - 1):
        p, q = points[i], points[(i + 1) % n]
        p0, p1, p2, p3 = p[0], p[2], q[1], q[0]
        for s in range(steps):
            t = s / steps
            u = 1 - t
            out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                        u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def render(size, background=(255, 255, 255, 0)):
    ss = 8  # supersampling
    img = Image.new("RGBA", (size * ss, size * ss), background)
    d = ImageDraw.Draw(img)
    scale = size * ss / 64.0
    for style, ids in shapes:
        for pid in ids:
            pts, closed = paths[pid]
            poly = [(x * scale, y * scale) for x, y in flatten(pts, closed)]
            d.polygon(poly, fill=styles[style])
    return img.resize((size, size), Image.LANCZOS)


LINUX_ICONS = os.path.join(HERE, "..", "linux", "icons")
PNG_SIZES = (16, 22, 24, 32, 48, 64, 128, 256, 512)


def svg():
    """The same drawing as a scalable SVG (viewBox = the 64x64 icon space)."""
    def n(v):
        return ("%.3f" % v).rstrip("0").rstrip(".")

    def path_d(points, closed):
        if not isinstance(points[0][0], tuple):
            d = "M" + " L".join("%s %s" % (n(x), n(y)) for x, y in points)
        else:
            d = "M%s %s" % (n(points[0][0][0]), n(points[0][0][1]))
            count = len(points)
            for i in range(count if closed else count - 1):
                p, q = points[i], points[(i + 1) % count]
                d += " C%s %s %s %s %s %s" % (n(p[2][0]), n(p[2][1]), n(q[1][0]), n(q[1][1]),
                                             n(q[0][0]), n(q[0][1]))
        return d + (" Z" if closed else "")

    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">',
           '  <title>TrimFast</title>']
    for style, ids in shapes:
        r, g, b, a = styles[style]
        d = " ".join(path_d(*paths[i]) for i in ids)
        out.append('  <path fill="#%02x%02x%02x" d="%s"/>' % (r, g, b, d))
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main():
    data = hvif.write_hvif(styles, paths, shapes)

    # Round trip: what we wrote must read back to the same drawing.
    back = hvif.read_hvif(data)
    assert back["consumed"] == len(data) == back["size"]
    assert len(back["styles"]) == len(styles) and len(back["paths"]) == len(paths)
    assert len(back["shapes"]) == len(shapes)
    for (pts, closed), got in zip(paths, back["paths"]):
        assert closed == got["closed"] and len(pts) == len(got["points"])
        for want, have in zip(pts, got["points"]):
            w = want if isinstance(want[0], tuple) else (want, want, want)
            for a, b in zip(w, have):
                assert abs(a[0] - b[0]) < 0.011 and abs(a[1] - b[1]) < 0.011, (want, have)

    with open(os.path.join(HERE, "trimfast.hvif"), "wb") as f:
        f.write(data)

    hexlines = []
    for i in range(0, len(data), 32):
        hexlines.append('\t$"%s"' % data[i:i + 32].hex().upper())
    rdef = ("/* Generated by make_icon.py - do not edit. TrimFast's vector icon (HVIF). */\n\n"
            "resource(101, \"BEOS:ICON\") #'VICN' array {\n%s\n};\n" % "\n".join(hexlines))
    with open(os.path.join(HERE, "trimfast_icon.rdef"), "w") as f:
        f.write(rdef)

    os.makedirs(DOCS, exist_ok=True)
    render(256).save(os.path.join(DOCS, "icon.png"))
    # Sheet: the sizes Haiku uses, on the desktop blue and on light grey.
    sheet = Image.new("RGBA", (440, 150), (216, 216, 216, 255))
    sheet.paste(Image.new("RGBA", (220, 150), (51, 102, 152, 255)), (0, 0))
    x = 10
    for size in (16, 32, 64):
        for bg_x in (0, 220):
            sheet.alpha_composite(render(size), (bg_x + x, 40))
        x += size + 14
    sheet = sheet.resize((880, 300), Image.NEAREST)
    sheet.save(os.path.join(DOCS, "icon_sizes.png"))
    # Linux (freedesktop icon theme): scalable SVG and the usual PNG sizes.
    os.makedirs(LINUX_ICONS, exist_ok=True)
    with open(os.path.join(LINUX_ICONS, "trimfast.svg"), "w") as f:
        f.write(svg())
    for size in PNG_SIZES:
        d = os.path.join(LINUX_ICONS, "hicolor", "%dx%d" % (size, size), "apps")
        os.makedirs(d, exist_ok=True)
        render(size).save(os.path.join(d, "trimfast.png"))
    print("HVIF: %d bytes (%d styles, %d paths, %d shapes)" % (len(data), len(styles), len(paths), len(shapes)))


if __name__ == "__main__":
    main()
