#!/usr/bin/env python3
"""Generate mockup_main_window.png (1920x1080, Haiku-style, light only)."""
from PIL import Image, ImageDraw, ImageFont

W, H = 1920, 1080
FD = "/usr/share/fonts/truetype/dejavu/"
def font(sz, bold=False, mono=False):
    n = "DejaVuSansMono" if mono else "DejaVuSans"
    return ImageFont.truetype(FD + n + ("-Bold" if bold else "") + ".ttf", sz)

PANEL = (216, 216, 216)
LIGHT = (255, 255, 255)
SHADE = (110, 110, 110)
DARK = (90, 90, 90)
BLACK = (0, 0, 0)
YELLOW = (255, 203, 0)
BLUE = (51, 102, 152)   # Haiku-ish control accent
RED = (200, 40, 40)

img = Image.new("RGB", (W, H), (255, 255, 255))
d = ImageDraw.Draw(img)

def bevel(box, fill=PANEL, sunken=False):
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=fill)
    tl, br = (SHADE, LIGHT) if sunken else (LIGHT, SHADE)
    d.line([(x0, y0), (x1, y0)], fill=tl)
    d.line([(x0, y0), (x0, y1)], fill=tl)
    d.line([(x0, y1), (x1, y1)], fill=br)
    d.line([(x1, y0), (x1, y1)], fill=br)
    d.rectangle(box, outline=None)

def text_c(box, s, f, fill=BLACK):
    x0, y0, x1, y1 = box
    d.text(((x0 + x1) / 2, (y0 + y1) / 2), s, font=f, fill=fill, anchor="mm")

# ---- Window frame (x 40..1880, y 30..1050)
WX0, WY0, WX1, WY1 = 40, 30, 1880, 1050
d.rectangle((WX0, WY0, WX1, WY1), fill=PANEL, outline=BLACK, width=1)
d.rectangle((WX0 + 1, WY0 + 1, WX1 - 1, WY1 - 1), outline=LIGHT)

# ---- Haiku yellow title tab (left aligned, not full width)
d.rectangle((WX0, WY0 - 24, WX0 + 420, WY0), fill=YELLOW, outline=BLACK)
d.text((WX0 + 14, WY0 - 12), "TrimFast - GOPR0042.MP4", font=font(14, True), fill=BLACK, anchor="lm")
d.rectangle((WX0 + 396, WY0 - 18, WX0 + 412, WY0 - 4), fill=PANEL, outline=BLACK)  # close box
# window top stripe next to tab
d.line([(WX0 + 420, WY0), (WX1, WY0)], fill=BLACK)

# ---- Menu bar
MY0, MY1 = WY0 + 2, WY0 + 38
d.rectangle((WX0 + 2, MY0, WX1 - 2, MY1), fill=PANEL)
d.line([(WX0 + 2, MY1), (WX1 - 2, MY1)], fill=SHADE)
mx = WX0 + 16
for m in ("File", "Edit", "Help"):
    d.text((mx, (MY0 + MY1) / 2), m, font=font(18), fill=BLACK, anchor="lm")
    mx += 80

# ---- Video preview area
PX0, PY0, PX1, PY1 = WX0 + 12, MY1 + 10, WX1 - 12, 650
bevel((PX0, PY0, PX1, PY1), fill=(245, 245, 245), sunken=True)
cx, cy = (PX0 + PX1) // 2, (PY0 + PY1) // 2
# 16:9 video frame placeholder
vw = int((PY1 - PY0 - 40) * 16 / 9)
vh = PY1 - PY0 - 40
d.rectangle((cx - vw // 2, cy - vh // 2, cx + vw // 2, cy + vh // 2), fill=(232, 232, 232), outline=DARK)
d.line([(cx - vw // 2, cy - vh // 2), (cx + vw // 2, cy + vh // 2)], fill=(200, 200, 200))
d.line([(cx - vw // 2, cy + vh // 2), (cx + vw // 2, cy - vh // 2)], fill=(200, 200, 200))
text_c((cx - 300, cy - 40, cx + 300, cy + 10), "Video Preview Area", font(34, True), DARK)
text_c((cx - 400, cy + 10, cx + 400, cy + 50), "1920x1080  H.264 / AAC  29.97 fps", font(20), DARK)
# current time overlay (bottom-right of preview)
d.rectangle((PX1 - 230, PY1 - 44, PX1 - 12, PY1 - 12), fill=LIGHT, outline=DARK)
d.text((PX1 - 121, PY1 - 28), "00:01:12.480", font=font(20, mono=True), fill=BLACK, anchor="mm")

# ---- Timeline
TY0, TY1 = PY1 + 10, 835
d.text((PX0 + 4, TY0 + 8), "Timeline", font=font(16, True), fill=BLACK, anchor="lm")
bevel((PX0, TY0 + 24, PX1, TY1), fill=LIGHT, sunken=True)
BX0, BX1 = PX0 + 20, PX1 - 20      # track extents
BY = (TY0 + 24 + TY1) // 2 + 6
TOTAL = 300.0                       # seconds (mock)
def tx(sec): return BX0 + (BX1 - BX0) * sec / TOTAL
IN_S, OUT_S, POS_S = 42.0, 215.5, 72.48
# ruler ticks
for s in range(0, 301, 10):
    x = tx(s)
    major = s % 60 == 0
    d.line([(x, TY0 + 30), (x, TY0 + (48 if major else 40))], fill=DARK)
    if major and s < 300:
        d.text((x + 4, TY0 + 42), f"{s//60}:00", font=font(13), fill=DARK, anchor="lm")
# track
d.rectangle((BX0, BY - 14, BX1, BY + 14), fill=(235, 235, 235), outline=SHADE)
# selected range (IN..OUT)
d.rectangle((tx(IN_S), BY - 14, tx(OUT_S), BY + 14), fill=(150, 190, 235), outline=BLUE)
# IN / OUT markers
for s, lbl, col in ((IN_S, "IN", BLUE), (OUT_S, "OUT", BLUE)):
    x = tx(s)
    d.line([(x, TY0 + 52), (x, TY1 - 8)], fill=col, width=3)
    tri = [(x, TY1 - 8), (x + (-14 if lbl == "IN" else 14), TY1 - 8), (x, TY1 - 24)]
    d.polygon(tri, fill=col)
    d.text((x + (-24 if lbl == "IN" else 24), TY1 - 20), lbl, font=font(13, True), fill=col, anchor="mm")
# playhead (red) with knob
x = tx(POS_S)
d.line([(x, TY0 + 52), (x, TY1 - 8)], fill=RED, width=2)
d.polygon([(x - 8, TY0 + 52), (x + 8, TY0 + 52), (x, TY0 + 66)], fill=RED)

# ---- IN / OUT readout
RY0 = TY1 + 12
d.text((PX0 + 4, RY0 + 18), "IN  :", font=font(24, True, mono=True), fill=BLACK, anchor="lm")
bevel((PX0 + 100, RY0 + 2, PX0 + 330, RY0 + 34), fill=LIGHT, sunken=True)
d.text((PX0 + 215, RY0 + 18), "00:00:42.000", font=font(22, mono=True), fill=BLACK, anchor="mm")
d.text((PX0 + 4, RY0 + 58), "OUT :", font=font(24, True, mono=True), fill=BLACK, anchor="lm")
bevel((PX0 + 100, RY0 + 42, PX0 + 330, RY0 + 74), fill=LIGHT, sunken=True)
d.text((PX0 + 215, RY0 + 58), "00:03:35.500", font=font(22, mono=True), fill=BLACK, anchor="mm")
d.text((PX0 + 380, RY0 + 18), "Duration : 00:02:53.500", font=font(18, mono=True), fill=DARK, anchor="lm")
d.text((PX0 + 380, RY0 + 58), "Mode : Stream Copy (lossless)", font=font(18, mono=True), fill=DARK, anchor="lm")

# ---- Buttons
BY0, BY1 = RY0 + 92, RY0 + 92 + 46
d.line([(PX0, BY0 - 8), (PX1, BY0 - 8)], fill=SHADE)
d.line([(PX0, BY0 - 7), (PX1, BY0 - 7)], fill=LIGHT)
bx = PX0
for lbl, key, w in (("Play", "Space", 150), ("Set IN", "I", 150), ("Set OUT", "O", 150), ("Export", "Ctrl+E", 170)):
    bevel((bx, BY0, bx + w, BY1))
    default = lbl == "Export"
    if default:
        d.rectangle((bx - 2, BY0 - 2, bx + w + 2, BY1 + 2), outline=BLACK, width=2)
    text_c((bx, BY0 + 2, bx + w, BY1 - 16), f"[{lbl}]", font(18, default), BLACK)
    text_c((bx, BY1 - 18, bx + w, BY1 - 2), key, font(11), DARK)
    bx += w + 16

# ---- Status bar
SY0 = WY1 - 28
d.line([(WX0 + 2, SY0), (WX1 - 2, SY0)], fill=SHADE)
d.text((WX0 + 14, (SY0 + WY1) / 2), "Ready  |  /home/user/Videos/GOPR0042.MP4  |  1.84 GB", font=font(14), fill=DARK, anchor="lm")
d.text((WX1 - 14, (SY0 + WY1) / 2), "Out: GOPR0042_trim.MP4", font=font(14), fill=DARK, anchor="rm")

img.save("mockup_main_window.png")
print("saved mockup_main_window.png", img.size)
