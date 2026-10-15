"""HIROBA complete drawing set (A3 x2, 1:100).
Sheet 1: whole plan with the lower-right detail merged in.
Sheet 2: elevation A (glass face) + section B-B + notes.
"""
import math, sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager as fm
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Rectangle, Polygon, Circle
from matplotlib.transforms import Affine2D

fm.fontManager.addfont("/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf")
plt.rcParams["font.family"] = "IPAGothic"
plt.rcParams["hatch.linewidth"] = 0.3
plt.rcParams["pdf.fonttype"] = 42      # TrueType text -> editable in Illustrator
plt.rcParams["svg.fonttype"] = "none"
OUT = sys.argv[1]
PHOTO = sys.argv[2]

GX0, GP, GY_C = 320.5, 60.42, 493.5   # grid F (src px), bay = 6,400mm, facade column row
S = 6400 / GP              # mm per source px (structural grid 6,400 = 60.42px)
K = 1 / 100.0              # 1:100
W, H = 420, 297
WOOD, WOOD2, WOODE = "#f1dfbd", "#dcc095", "#8a5a2b"
GREEN, GREENE = "#cfe3c4", "#3c6e27"
GLASS, RED = "#d6ecff", "#c8102e"
GX, GY0, GY1, FAC, TIER_S = 519.5, 410.5, 497.5, 497.5, 427.0
CEIL, STG, TIER, TOP, SEAT = 2750, 250, 800, 1200, 420

ax = None
OX, OY, X0, Y0 = 30.0, 45.0, 290.0, 498.0

def new_sheet():
    global ax
    fig = plt.figure(figsize=(W / 25.4, H / 25.4))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, W); ax.set_ylim(0, H); ax.set_aspect("equal"); ax.axis("off")
    ax.add_patch(Rectangle((8, 8), W - 16, H - 16, fc="none", ec="k", lw=1.0))
    return fig

def P(x, y): return OX + (x - X0) * S * K, OY + (Y0 - y) * S * K
def L(mm): return mm * K
def mm_of(px): return px * S
def r50(v): return f"{int(round(v / 50.0) * 50):,}"

def line(p, q, lw=0.3, c="k", ls="-", z=4):
    ax.plot([p[0], q[0]], [p[1], q[1]], lw=lw, color=c, ls=ls, zorder=z, solid_capstyle="butt")

def poly(pts, fc="none", ec="k", lw=0.3, z=2, hatch=None, ls="-", alpha=1):
    ax.add_patch(Polygon([P(*p) for p in pts], closed=True, fc=fc, ec=ec, lw=lw, zorder=z,
                         hatch=hatch, ls=ls, alpha=alpha))

def box(x1, y1, x2, y2, **kw):
    poly([(x1, y1), (x2, y1), (x2, y2), (x1, y2)], **kw)

def rrect(cx, cy, w, d, ang, fc="white", ec="k", lw=0.3, z=5, ls="-", hatch=None):
    r = Rectangle((-L(w) / 2, -L(d) / 2), L(w), L(d), fc=fc, ec=ec, lw=lw, zorder=z, ls=ls, hatch=hatch)
    r.set_transform(Affine2D().rotate_deg(ang).translate(cx, cy) + ax.transData)
    ax.add_patch(r)

def band(a, b, width, side=0.0, **kw):
    p, q = P(*a), P(*b)
    ang = math.degrees(math.atan2(q[1] - p[1], q[0] - p[0]))
    ln = math.hypot(q[0] - p[0], q[1] - p[1]) / K
    nx, ny = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
    rrect((p[0] + q[0]) / 2 + nx * L(side), (p[1] + q[1]) / 2 + ny * L(side), ln, width, ang, **kw)
    return ang, ln

def T(x, y, s, size=4.2, **kw):
    ax.text(x, y, s, fontsize=size, ha=kw.pop("ha", "center"), va=kw.pop("va", "center"), zorder=12, **kw)

def leader(src, txt_xy, s, size=4.0, c="k"):
    ax.annotate(s, xy=src, xytext=txt_xy, fontsize=size, ha="left", va="center", color=c, zorder=12,
                arrowprops=dict(arrowstyle="-", lw=0.25, color=c, shrinkA=0.5, shrinkB=0))
    ax.add_patch(Circle(src, 0.35, fc=c, ec="none", zorder=12))

def lab(src, dst, s, c="k", size=3.9):
    ax.annotate(s, xy=P(*src), xytext=dst, fontsize=size, ha="center", va="center", color=c, zorder=12,
                arrowprops=dict(arrowstyle="-", lw=0.25, color=c), bbox=dict(fc="white", ec="none", pad=0.3))

def dimh(x1, x2, y, label, size=3.8, c="k", ext_from=None):
    line((x1, y), (x2, y), 0.25, c, z=9)
    for xx in (x1, x2):
        line((xx - 0.7, y - 0.7), (xx + 0.7, y + 0.7), 0.4, c, z=9)
        if ext_from is not None:
            line((xx, ext_from), (xx, y + (0.8 if y > ext_from else -0.8)), 0.15, c, z=9)
    T((x1 + x2) / 2, y + (1.4 if ext_from is None or y > ext_from else -1.4), label, size, color=c)

def dimv(y1, y2, x, label, size=3.8, c="k", ext_from=None, side=1):
    line((x, y1), (x, y2), 0.25, c, z=9)
    for yy in (y1, y2):
        line((x - 0.7, yy - 0.7), (x + 0.7, yy + 0.7), 0.4, c, z=9)
        if ext_from is not None:
            line((ext_from, yy), (x + (0.8 if x > ext_from else -0.8), yy), 0.15, c, z=9)
    T(x + 1.5 * side, (y1 + y2) / 2, label, size, rotation=90, color=c)

def chair(cx, cy, ang): rrect(cx, cy, 450, 450, ang, lw=0.2, z=3)

def table4(x, y, ang=15):
    cx, cy = P(x, y)
    rrect(cx, cy, 750, 750, ang, lw=0.35, z=4)
    for k in range(4):
        a = math.radians(ang + 90 * k)
        chair(cx + L(620) * math.cos(a), cy + L(620) * math.sin(a), ang + 90 * k)

def title_block(name, sheet):
    tb = (349, 14, 61, 30)
    ax.add_patch(Rectangle(tb[:2], tb[2], tb[3], fc="none", ec="k", lw=0.8))
    for yy in (24, 34):
        line((tb[0], yy), (tb[0] + tb[2], yy), 0.4)
    line((tb[0] + 15, 14), (tb[0] + 15, 44), 0.4)
    for (lbl, val, yy) in [("図面名", name, 39), ("縮尺", "1/100（A3）", 29), ("日付/番号", f"2026.10.15　{sheet}", 19)]:
        ax.text(tb[0] + 2, yy, lbl, fontsize=4.6, va="center")
        ax.text(tb[0] + 17, yy, val, fontsize=5.4, va="center")

def scale_bar(x, y):
    for i in range(5):
        ax.add_patch(Rectangle((x + i * 10, y), 10, 1.4, fc="k" if i % 2 == 0 else "white", ec="k", lw=0.3))
        T(x + i * 10, y - 2.2, str(i), 3.6)
    T(x + 52, y - 2.2, "5m", 3.6)
    ax.text(x, y + 3.5, "SCALE 1/100 (A3)", fontsize=4.2, ha="left")

pdf = PdfPages(OUT + ".pdf")
sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from dxf_export import Exporter
DXF = Exporter()

# ======================================================================
# SHEET 1 : PLAN  (v2 — calibrated to structural grid 6,400 / checked vs reference plan)
# ======================================================================
fig1 = new_sheet()
LW_WALL, LW_MID = 1.4, 0.6
xl, yt = 291, 288
GRID_X = [GX0 + GP * k for k in range(4)]          # F, G, H, I
GRID_Y = [GY_C - GP * k for k in range(4)]         # facade row + 3 rows north

def chair_sq(x, y, ang=0, size=430, fc="white"):
    rrect(*P(x, y), size, size, ang, fc=fc, lw=0.3, z=5)

def rtable4(x, y, ang=-15):
    cx, cy = P(x, y)
    rrect(cx, cy, 850, 850, ang, lw=0.4, z=5)
    for k in range(4):
        a = math.radians(ang + 90 * k)
        rrect(cx + L(700) * math.cos(a), cy + L(700) * math.sin(a), 420, 380, ang + 90 * k, lw=0.25, z=4)

def roundt(x, y, d=500, dot=True):
    ax.add_patch(Circle(P(x, y), L(d / 2), fc="white", ec="k", lw=0.3, zorder=5))
    if dot:
        ax.add_patch(Circle(P(x, y), L(d / 5), fc="none", ec="k", lw=0.2, zorder=6))

# --- structural grid ----------------------------------------------------
for i, gx_ in enumerate(GRID_X):
    line(P(gx_, 280), P(gx_, 509), 0.2, "#777", ls=(0, (8, 2, 1, 2)), z=0)
    c = P(gx_, 512.5)
    T(c[0], c[1], "FGHI"[i], 4.6, bbox=dict(boxstyle="circle,pad=0.25", fc="white", ec="#555", lw=0.4))
for gy_ in GRID_Y:
    line(P(283, gy_), P(540, gy_), 0.2, "#777", ls=(0, (8, 2, 1, 2)), z=0)
GDY = P(0, 509)[1] - 9
for a, b in zip(GRID_X[:-1], GRID_X[1:]):
    dimh(P(a, 0)[0], P(b, 0)[0], GDY, "6,400", 4.0)
dimh(P(GRID_X[0], 0)[0], P(GRID_X[-1], 0)[0], GDY - 6, "19,200", 4.2)
GDX = P(283, 0)[0] - 4
for a, b in zip(GRID_Y[1:], GRID_Y[:-1]):
    dimv(P(0, a)[1], P(0, b)[1], GDX, "6,400", 3.8, side=-1)

# --- general walls ------------------------------------------------------
line(P(xl, yt), P(xl, FAC), LW_WALL)
line(P(293, 318), P(293, FAC), 0.4)
line(P(304, 318), P(304, 400), LW_MID)
line(P(522, 308.5), P(522, 360), LW_WALL)
box(xl - 2, FAC, 536, 503.5, fc="#222", ec="k", lw=0.3, z=3)          # south facade
for y in range(320, 346, 5):
    box(296, y, 303.5, y + 5, lw=0.2); line(P(296, y), P(303.5, y + 5), 0.15)
for y in range(360, 380, 5):
    box(296, y, 303.5, y + 5, lw=0.2); line(P(296, y), P(303.5, y + 5), 0.15)
box(305.5, 319, 309.5, 346, lw=0.3)
for y in range(400, 496, 4):
    box(294.5, y, 302.5, y + 3.6, lw=0.15)
box(304.5, 376, 311, 386, lw=0.3)                                         # shelf unit
for yy in np.arange(377, 386, 1.5):
    line(P(305, yy), P(310.5, yy), 0.2)
for y in range(310, 358, 6):
    box(522, y, 528, y + 6, lw=0.2)

# back-of-house rooms (north)
for (x1, x2) in [(286, 357), (376, 447), (466, 535)]:
    box(x1, 288.5, x2, 308.5, fc="#bdbdbd", lw=LW_MID, z=1)
box(337, 301, 356, 306.5, fc="white", lw=0.3, z=2)
box(466, 301, 488, 308.5, fc="white", lw=0.3, z=2)
# columns: north row (square) + facade row (round)
for gx_ in GRID_X:
    box(gx_ - 4.5, 309.5, gx_ + 4.5, 318.5, fc="#a8a8a8", ec="k", lw=0.6, z=5, hatch="\\\\")
    ax.add_patch(Circle(P(gx_, 493.5), L(4.6 * S), fc="#a8a8a8", ec="k", lw=0.6, zorder=5, hatch="\\\\"))

# cafe counter (shape per reference)
poly([(376, 318), (386.5, 318), (386.5, 327), (388, 327), (388, 334.5), (399, 334.5), (399, 327),
      (447, 327), (447, 337.5), (376, 337.5)], fc="white", ec="k", lw=LW_MID, z=3)
line(P(386.5, 318), P(434, 318), 0.6)
for xx in (402, 422):
    box(xx - 2, 311, xx + 2, 316, lw=0.25); line(P(xx - 2, 311), P(xx + 2, 316), 0.2); line(P(xx - 2, 316), P(xx + 2, 311), 0.2)
T(*P(412, 322.5), "カフェカウンター", 4.5)
# pantry (NE)
box(468, 308.5, 518, 347.5, lw=LW_MID)
for (a, b, c, d) in [(469, 336, 474, 346), (475, 336, 485, 346), (486, 336, 496, 346), (497, 336, 506, 346),
                     (507, 336, 516, 346), (477, 325, 492, 333), (493, 325, 508, 333), (474, 314, 487, 320),
                     (488, 313, 499, 322), (509, 320, 517, 333)]:
    box(a, b, c, d, lw=0.25)
T(*P(493, 342), "パントリー", 4)
# library (planter-edged nook) + round table
poly([(323, 326), (343, 328), (351, 340), (329, 347)], fc="#eef5ea", ec=GREENE, lw=0.9, ls=(0, (2, 1)), z=3)
poly([(332, 332), (343, 336), (334, 341)], ec="k", lw=0.4, z=4)
roundt(348.5, 324.5, 520, dot=False)
box(304.5, 323, 307.5, 329, lw=0.3)
T(*P(328, 321), "Library", 4.5)
# large table (dotted outline per reference, no chairs drawn)
cx, cy = P(353.7, 367.5); rrect(cx, cy, 30.4 * S, 7.6 * S, 25.3, fc="white", ec="k", lw=0.9, z=4, ls=(0, (2, 1)))
# booth seating (north centre) : 2 booths = bench | table | bench
box(388, 357, 437, 361, fc="#555", ec="k", lw=0.3, z=4)
for (b1, t, b2) in [((392, 399), (400.5, 405), (406, 413)), ((413.5, 420.5), (421.5, 426.5), (428, 436))]:
    for (x1, x2) in (b1, b2):
        box(x1, 361, x2, 374, fc="#c0687a", ec="k", lw=0.4, z=4)
        line(P(x1, 368.5), P(x2, 368.5), 0.25)
    box(t[0], 362.5, t[1], 373, fc="white", ec="k", lw=0.4, z=4)
T(*P(412, 346.5), "437.87㎡", 4.2, color="#555")
# cafe tables (4-seat, per reference)
for (x, y) in [(363.8, 400.4), (384.8, 390.8), (415.5, 400.9), (436.7, 390.8), (394.2, 411.0), (425.0, 421.0),
               (445.9, 411.0), (372.2, 420.1), (403.8, 431.1), (452.0, 431.0), (382.5, 441.1), (432.2, 441.1),
               (411.0, 450.6), (389.8, 460.6)]:
    rtable4(x, y)
T(*P(408, 381.5), "HIROBA Cafe", 6)
# NE: 2-person table + chairs, high counter (trapezoid)
poly([(467, 360), (512, 360), (512, 366), (469, 366)], fc="#e8cfa3", ec="k", lw=0.5, z=4)
box(473, 372, 480, 379.5, lw=0.4); chair_sq(476.5, 368.5); chair_sq(476.5, 383)

# --- west lounge (per reference) ---------------------------------------
# planter with plants (dark band) + thin screen line
band((334.0, 410.0), (362.0, 461.0), 550, fc=GREEN, ec=GREENE, lw=0.5, z=5, hatch="xxx")
line(P(331.0, 402.8), P(334.0, 410.0), 0.6, GREENE)
line(P(362.0, 461.0), P(364.6, 477.6), 0.6, "k")
for (x, y) in [(330.7, 427.1), (335.9, 444.2), (338.3, 450.6), (347.0, 464.9)]:
    rrect(*P(x, y), 1950, 700, 28, lw=0.4, z=5)
    rrect(*P(x, y), 1950, 220, 28, fc="none", lw=0.25, z=6)
for (x, y) in [(333.5, 435.8), (343.9, 457.4)]:
    rrect(*P(x, y), 1100, 450, 28, lw=0.35, z=5)
for (x, y) in [(345.5, 406.0), (351.8, 421.1), (355.4, 428.3), (361.4, 442.6), (367.0, 451.4), (373.0, 466.5)]:
    chair_sq(x, y, -30, 700)
for (x, y) in [(347.9, 413.1), (359.0, 435.5), (370.2, 458.6), (329.9, 415.1)]:
    roundt(x, y, 520)
for (x, y) in [(324.0, 468.1), (331.9, 479.3), (342.7, 478.9), (356.6, 484.1), (320.0, 486.0)]:
    chair_sq(x, y, 45, 680)
for (x, y) in [(314.4, 468.1), (321.6, 474.1), (318.4, 479.7), (337.5, 484.1), (351.0, 484.5), (332.7, 490.4)]:
    roundt(x, y, 450)
box(328.7, 490.8, 350.6, 497.0, lw=0.5, z=5); line(P(339.6, 490.8), P(339.6, 497.0), 0.3)
box(305.0, 465.0, 312.0, 487.0, lw=0.5, z=5); line(P(305, 476), P(312, 476), 0.3)
for y in (412.5, 429.5, 440.0, 456.5):
    chair_sq(316.0, y, 0, 650)
for y in (421.0, 448.5):
    roundt(316.0, y, 400)
# central screen (projector)
line(P(403.8, 475.1), P(455.6, 450.0), 1.2, "#555", z=5)

# ---- SE area (detailed per photo, positions per reference) ---------------
LINEA = ((384.8, 499.1), (502.0, 444.5))
poly([LINEA[0], LINEA[1], (498.5, 444.4), (498.5, 427.0), (517.0, 445.0), (517.0, FAC), (384.8, FAC)],
     fc=WOOD, ec="none", z=1)
line(P(*LINEA[0]), P(*LINEA[1]), 0.6, WOODE, z=4)
line(P(498.5, 427), P(498.5, 444.4), 0.6, WOODE, z=4)
poly([(505, 370), (517.4, 370), (517.4, 445), (507, 427), (505, 427)], fc=WOOD2, ec=WOODE, lw=0.5, z=2)
box(511, 386, 517.2, 404, fc="#c9a774", ec=WOODE, lw=0.5, z=3)
poly([(498.5, 427), (507, 427), (517, 437), (517, 445)], fc=WOOD2, ec="none", z=1, alpha=0.6)
for (sx, sy) in [(499.0, 427.0), (503.0, 427.0), (507.0, 427.0)]:
    line(P(sx, sy), P(517.0, sy + 517.0 - sx), 0.5, WOODE, z=4)
line(P(499.3, 427.8), P(507.2, 427.8), 0.9, "#333", z=6)
line(P(516.3, 437.5), P(516.3, 445.2), 0.9, "#333", z=6)
box(498, 370, 505, 427, fc="#e9d3ae", ec="k", lw=0.5, z=3)
line(P(504.2, 370), P(504.2, 427), 0.4, WOODE, z=4)
for (y1, y2) in [(371.0, 377.3), (377.6, 383.8), (391.0, 397.3), (397.6, 403.9), (411.0, 417.0), (417.3, 423.6)]:
    box(498.7, y1, 503.6, y2, fc="#c0687a", ec="k", lw=0.25, z=4)
    box(490.5, y1, 497.2, y2, fc="white", ec="k", lw=0.5, z=5)
    chair_sq(487.5, (y1 + y2) / 2)
box(518.2, 352, 521.0, GY0, fc="#9e9e9e", ec="k", lw=0.6, z=3, hatch="////")
box(517.4, 391.0, 518.2, GY0, fc="#7fb069", ec=GREENE, lw=0.4, z=4, hatch="xxxx")
gx0, gy0 = P(GX, GY0); gx1, gy1 = P(GX, GY1)
ax.add_patch(Rectangle((gx0 - 0.45, gy1), 0.9, gy0 - gy1, fc=GLASS, ec=RED, lw=0.7, zorder=6))
line((gx0, gy0), (gx0, gy1), 0.25, RED, z=7)
box(516.6, GY0, 518.6, 445.0, fc=GREEN, ec=GREENE, lw=0.3, z=3, hatch="....")
# window counter: sinuous, wraps round the facade columns (per reference)
wc = [(410.0, 495.5), (420.0, 493.6), (430.0, 491.2), (436.0, 489.4), (440.0, 488.4), (444.0, 488.6),
      (450.0, 489.6), (458.0, 491.6), (466.0, 493.4), (474.0, 494.0), (482.0, 493.6), (488.0, 491.8),
      (493.0, 489.2), (497.0, 487.6), (501.0, 487.0), (505.0, 487.8), (509.0, 490.2), (512.0, 493.0),
      (514.5, 496.5)]
ax.plot([P(*p)[0] for p in wc], [P(*p)[1] for p in wc], lw=1.6, color="k", zorder=5, solid_capstyle="round")
ax.plot([P(*p)[0] for p in wc], [P(*p)[1] for p in wc], lw=0.9, color="#e7e7e7", zorder=5, solid_capstyle="round")
for (x, y) in [(420.5, 490.2), (428.3, 487.4), (437.3, 485.0), (448.1, 485.3), (456.4, 486.7), (463.9, 488.6),
               (470.9, 490.9), (478.8, 491.3), (486.2, 489.0), (492.7, 486.2), (501.1, 482.5), (508.0, 484.0),
               (513.1, 488.6)]:
    chair_sq(x, y, 0, 430)
# stage edge counter (low planter) + stools/chairs (per reference)
band((443.0, 472.3), (490.0, 451.6), 320, fc=GREEN, ec=GREENE, lw=0.35, z=5, hatch="....")
for (x, y) in [(448.5, 472.5), (462.0, 467.0), (476.0, 461.5)]:
    roundt(x, y, 480)
for (x, y) in [(455.5, 471.0), (469.0, 466.0), (482.5, 460.5)]:
    chair_sq(x, y, -20, 430)
ang2, _ = band((473.0, 478.5), (497.5, 468.5), 580, fc="#e8cfa3", ec=WOODE, lw=0.5, z=5)
roundt(509.5, 461.0, 480)
for (x, y, a) in [(506.0, 456.0, 20), (508.5, 467.0, -10), (508.0, 480.5, 15)]:
    chair_sq(x, y, a, 470)
box(514.8, 461.0, 517.0, 471.0, fc="white", ec="k", lw=0.45, z=5)
line(P(518.0, 459.0), P(518.0, 483.0), 1.4, "#888", z=6)
pj = P(486.0, 473.5)
rrect(*pj, 500, 450, ang2, fc="none", ec="k", lw=0.35, z=7, ls=(0, (2, 1)))
for tgt in ((518.0, 459.0), (518.0, 483.0)):
    line(pj, P(*tgt), 0.2, "#666", ls=(0, (3, 2)), z=6)
poly([(453, 389), (458, 389), (463, 397), (468, 409), (472, 420), (470, 426), (466, 426),
      (462, 416), (457, 405), (453, 395)], fc=GREEN, ec=GREENE, lw=0.5, z=4, hatch="xxx")
line(P(468.0, 412.0), P(477.0, 430.0), 0.6, "k")
rrect(*P(466.0, 399.0), 500, 450, 0, fc="none", ec="k", lw=0.35, z=7, ls=(0, (2, 1)))
box(521.0, 360, 532.5, FAC, fc="#f4f4f4", ec="none", z=0)
line(P(532.5, 360), P(532.5, FAC), 0.5, "k")
for y in np.arange(411.0, 496.0, 4.2):
    roundt(523.8, y + 2.0, 380, dot=False)

# --- labels ---------------------------------------------------------------
T(*P(509, 491.5), "ステージ\n（木製床）", 3.8, color="#6b4220")
T(*P(511, 418), "上段\n（木製段床）", 3.5, color="#6b4220")
T(*P(514, 395), "最上段", 3.1, color="#6b4220")
T(*P(501, 407.5), "ベンチ", 3.3, rotation=90)
T(*P(512, 433.5), "階段3段", 3.3, rotation=-45, color="#6b4220")
T(*P(527, 452), "隣室\n「3」", 3.3, color="#555")
T(*P(430, 480.5), "ステージ端（段差）", 3.3, rotation=25, color="#6b4220")
lab((474, 458.5), P(468, 447), "低植栽", GREENE)
lab((462.0, 467.0), P(452, 480), "スツール")
lab((462, 410), P(476, 386), "大型プランター", GREENE)
lab((348, 436), P(323, 395), "大型プランター", GREENE)
lab((340, 455), P(357, 489.5), "ソファ")
GLW = r50(mm_of(GY1 - GY0))
RL = 300
for (src, ty, s, c) in [((517.8, 400), 398, "壁面緑化", GREENE),
                        ((GX, 430), 418, f"ガラス面（W 約{GLW}）", RED),
                        ((517.6, 436), 430, "植栽（ガラス際）", GREENE),
                        ((516.3, 441), 441, "手摺（黒）", "k"),
                        ((509.5, 461), 452, "チェア（白）", "k"),
                        ((515.8, 466), 463, "デスク（白）", "k"),
                        ((518.0, 474), 474, "スクリーン（天井吊）", "k"),
                        ((487, 471.5), 485, "ハイカウンター（木製）／PJ（天井）", "k"),
                        ((480, 494.0), 496, "窓際カウンター＋チェア", "k")]:
    leader(P(*src), (RL, P(0, ty)[1]), s, 3.8, c)

# --- dimensions -------------------------------------------------------------
pl, pr = P(xl, FAC), P(522, FAC)
dimv(pl[1], P(0, yt)[1], P(283, 0)[0] - 11, f"約 {r50(mm_of(FAC - yt))}", 4.2, ext_from=pl[0] - 1, side=-1)
ex = P(521, 0)[0]; DX = ex + 15
ys = [yt, 391.0, GY0, TIER_S, 445.0, GY1]
for (a, b) in zip(ys[:-1], ys[1:]):
    dimv(P(0, b)[1], P(0, a)[1], DX, "約" + r50(mm_of(b - a)), size=3.2, ext_from=ex + 12)
dimv(P(0, GY1)[1], P(0, GY0)[1], DX + 6, f"ガラス 約{GLW}", c=RED, size=3.6)
TY = P(0, yt)[1] + 4
for a, b in zip([484.0, 490.5, 497.2, 505.0, 517.4], [490.5, 497.2, 505.0, 517.4, GX]):
    dimh(P(a, 0)[0], P(b, 0)[0], TY, r50(mm_of(b - a)), size=2.9)
dimh(P(xl, 0)[0], P(522, 0)[0], TY + 6, f"約 {r50(mm_of(522 - xl))}", 4.2)

def mark(p, txt):
    T(p[0], p[1], txt, 5.0, bbox=dict(boxstyle="circle,pad=0.2", fc="white", ec="k", lw=0.5))
pa = P(536.5, 455.0); mark((pa[0] + 2.5, pa[1]), "A")
ax.annotate("", xy=(pa[0] - 1.6, pa[1]), xytext=(pa[0] + 0.6, pa[1]),
            arrowprops=dict(arrowstyle="-|>", lw=0.5, color="k"))
by = P(0, 400.0)[1]
line((P(480.5, 0)[0], by), (P(523, 0)[0], by), 0.35, "k", ls=(0, (6, 1.5, 1, 1.5)), z=9)
for xx in (P(480.5, 0)[0], P(523, 0)[0]):
    mark((xx, by + 2.6), "B")

ax.text(355, H - 18, "HIROBA 平面図\n（完全版）S=1/100", fontsize=9, ha="left", va="top", linespacing=1.4)
ax.text(355, H - 34, "通り芯 6,400（F〜I 19,200）を\n基準に縮尺確定。\n右下部分は現況写真と照合。", fontsize=4.6, ha="left", va="top", color="#444", linespacing=1.5)
na = (395, 225)
ax.add_patch(Polygon([(na[0], na[1] + 8), (na[0] - 3, na[1] - 4), (na[0], na[1] - 1.5)], fc="k", ec="k", lw=0.3))
ax.add_patch(Polygon([(na[0], na[1] + 8), (na[0] + 3, na[1] - 4), (na[0], na[1] - 1.5)], fc="white", ec="k", lw=0.3))
T(na[0], na[1] + 11, "図上北", 4)
scale_bar(357, 60)
lg = [(WOOD, WOODE, None, "ステージ（木製床）"), (WOOD2, WOODE, None, "木製段床"),
      ("#c0687a", "k", None, "ベンチ座面"), (GREEN, GREENE, "....", "植栽"),
      ("#7fb069", GREENE, "xxxx", "壁面緑化"), (GLASS, RED, None, "ガラス面"),
      ("#a8a8a8", "k", "\\\\", "柱")]
ax.text(357, 190, "凡例", fontsize=5.5, ha="left")
for i, (fc, ec, h, s) in enumerate(lg):
    yy = 183 - i * 6
    ax.add_patch(Rectangle((357, yy - 1.5), 6, 3, fc=fc, ec=ec, lw=0.4, hatch=h))
    ax.text(365, yy, s, fontsize=4.6, va="center")
ax.add_patch(Rectangle((357, 183 - 7 * 6 - 1.5), 6, 3, fc="none", ec="k", lw=0.35, ls=(0, (2, 1))))
ax.text(365, 183 - 7 * 6, "天井設置（PJ）", fontsize=4.6, va="center")
line((357, 183 - 8 * 6), (363, 183 - 8 * 6), 0.2, "#777", ls=(0, (8, 2, 1, 2)))
ax.text(365, 183 - 8 * 6, "通り芯", fontsize=4.6, va="center")
line((350, 8), (350, H - 8), 0.4)
title_block("HIROBA 平面図（完全版）", "1/2")
DXF.add_sheet(ax, 0.0); pdf.savefig(fig1); fig1.savefig(OUT + "_1.png", dpi=150)
fig1.savefig(OUT + "_1.svg"); plt.close(fig1)

# ======================================================================
# SHEET 2 : ELEVATION A + SECTION B-B + NOTES
# ======================================================================
fig2 = new_sheet()
EX0, EY0 = 40, 190
def E(ysrc, h): return EX0 + (ysrc - 391.0) * S * K, EY0 + h * K
def erect(y1, y2, h0, h1, **kw):
    a = E(y1, h0); b = E(y2, h1)
    ax.add_patch(Rectangle(a, b[0] - a[0], b[1] - a[1], zorder=kw.pop("z", 4), **kw))
ax.text(EX0, EY0 + 45, "A 展開図（ガラス面・西より見る）　S=1/100", fontsize=9, ha="left", va="center")
x0, _ = E(391.0, 0); x1, _ = E(GY1, 0); gx, _ = E(GY0, 0)
line((x0 - 4, EY0), (x1 + 4, EY0), 0.8)
line((x0 - 4, E(0, CEIL)[1]), (x1 + 4, E(0, CEIL)[1]), 0.5)
T(x1 + 6, E(0, CEIL)[1], "天井", 3.6, ha="left"); T(x1 + 6, EY0, "FL±0", 3.6, ha="left")
ax.add_patch(Rectangle((x0, EY0), gx - x0, L(CEIL), fc="#93c47d", ec=GREENE, lw=0.4, hatch="xxx", zorder=2))
ax.add_patch(Rectangle((gx, EY0), x1 - gx, L(CEIL), fc=GLASS, ec="k", lw=0.6, zorder=2))
for i in range(6):
    xx = gx + 3 + i * (x1 - gx) / 6
    line((xx, EY0 + L(1900)), (xx + 3, EY0 + L(2350)), 0.2, "#7aa7d6", z=3)
sx, sy = E(450.0, 1650)
T(sx, sy, "3", 5, color="#2b5c8a", bbox=dict(fc="white", ec="#2b5c8a", lw=0.3, pad=0.6))
erect(391.0, TIER_S, 0, TIER, fc=WOOD2, ec=WOODE, lw=0.5)
erect(391.0, 406.0, TIER, TOP, fc="#c9a774", ec=WOODE, lw=0.5)
erect(391.0, TIER_S, 0, SEAT, fc="#e9d3ae", ec=WOODE, lw=0.4, z=5)
for i in range(3):
    yA = TIER_S + i * 6.0
    erect(yA, yA + 6.0, 0, TIER - i * (TIER - STG) / 3, fc=WOOD2, ec=WOODE, lw=0.4)
erect(445.0, GY1, 0, STG, fc=WOOD, ec=WOODE, lw=0.5)
line(E(TIER_S + 1, TIER + 1000), E(445.0, STG + 1000), 0.8, "#333", z=6)
for yy, hh in ((TIER_S + 1, TIER), (445.0, STG)):
    line(E(yy, hh), E(yy, hh + 1000), 0.6, "#333", z=6)
for i in range(14):
    yy = GY0 + 1 + i * 2.5
    base = TIER if yy < TIER_S else (STG if yy > 445 else TIER - (yy - TIER_S) / 18 * (TIER - STG))
    ax.add_patch(Circle(E(yy, base + 200), L(260), fc="#a9d18e", ec=GREENE, lw=0.25, zorder=5))
erect(461.0, 487.0, STG + 450, CEIL - 150, fc="white", ec="#555", lw=0.5, z=6)
erect(461.0, 487.0, CEIL - 150, CEIL, fc="#666", ec="k", lw=0.3, z=6)
T((E(461, 0)[0] + E(487, 0)[0]) / 2, E(0, 1500)[1], "スクリーン", 3.6)
erect(459.5, 472.0, STG + 700, STG + 740, fc="white", ec="k", lw=0.4, z=7)
for yy in (461.0, 470.5):
    line(E(yy, STG), E(yy, STG + 700), 0.4, "k", z=7)
ytop = E(0, CEIL)[1] + 4
dimh(E(391.0, 0)[0], gx, ytop, "緑化 約2,050", size=3.4)
dimh(gx, x1, ytop, f"ガラス W 約{GLW}", c=RED, size=4.0)
xr = x1 + 14
dimv(E(0, STG)[1], E(0, CEIL)[1], xr - 6, "H 約2,500", c=RED, size=3.8, ext_from=x1)
dimv(EY0, E(0, STG)[1], xr - 6, "", ext_from=x1)
T(xr - 3.5, EY0 + L(STG) / 2, "≈250", 3.0, ha="left")
dimv(EY0, E(0, CEIL)[1], xr, "CH≈2,750", size=3.4)
x_l = x0 - 4
dimv(EY0, E(0, SEAT)[1], x_l, "≈420", size=3.0, side=-1)
dimv(EY0, E(0, TIER)[1], x_l - 4, "≈800", size=3.0, side=-1)
dimv(EY0, E(0, TOP)[1], x_l - 8, "≈1,200", size=3.0, side=-1)
T(E(395, 0)[0], EY0 - 3.5, "北", 3.6); T(E(495, 0)[0], EY0 - 3.5, "南（窓側）", 3.6)
for (yy, s) in [(408, "最上段"), (418, "ベンチ・段床"), (436, "階段"), (452, "ステージ"), (466, "デスク")]:
    T(E(yy, 0)[0], EY0 - 8, s, 3.4, color="#6b4220")

BX0, BY0 = 60, 75
def B(xsrc, h): return BX0 + (xsrc - 482.0) * S * K, BY0 + h * K
def brect(x1, x2, h0, h1, **kw):
    a = B(x1, h0); b = B(x2, h1)
    ax.add_patch(Rectangle(a, b[0] - a[0], b[1] - a[1], zorder=kw.pop("z", 4), **kw))
ax.text(BX0, BY0 + 42, "B-B 断面（ベンチ・段床）　S=1/100", fontsize=9, ha="left", va="center")
line(B(480, 0), B(524, 0), 0.8); line(B(480, CEIL), B(521, CEIL), 0.5)
line(B(484.0, 450), B(488.2, 450), 0.6); line(B(484.3, 0), B(484.3, 850), 0.5); line(B(488, 0), B(488, 450), 0.4)
line(B(490.0, 720), B(497.2, 720), 1.0); line(B(493.6, 0), B(493.6, 720), 0.5)
brect(498.0, 504.2, 0, SEAT, fc="#e9d3ae", ec=WOODE, lw=0.5)
brect(498.6, 503.8, SEAT, SEAT + 80, fc="#c0687a", ec="k", lw=0.3, z=5)
brect(504.2, 517.4, 0, TIER, fc=WOOD2, ec=WOODE, lw=0.5)
brect(511.0, 517.2, TIER, TOP, fc="#c9a774", ec=WOODE, lw=0.5)
brect(517.4, 518.2, TOP, CEIL, fc="#93c47d", ec=GREENE, lw=0.4, hatch="xxx")
brect(518.2, 521.0, 0, CEIL, fc="#9e9e9e", ec="k", lw=0.6, hatch="////")
for (a, b) in [(484.0, 490.0), (490.0, 497.2), (498.0, 504.2), (504.2, 517.4)]:
    dimh(B(a, 0)[0], B(b, 0)[0], BY0 - 6, r50(mm_of(b - a)), size=3.0)
xr = B(521.0, 0)[0] + 4
dimv(BY0, B(0, SEAT)[1], xr, "≈420", size=3.0)
dimv(BY0, B(0, TIER)[1], xr + 5, "≈800", size=3.0)
dimv(BY0, B(0, TOP)[1], xr + 10, "≈1,200", size=3.0)
dimv(BY0, B(0, CEIL)[1], xr + 15, "CH≈2,750", size=3.2)
T(B(484, 0)[0], BY0 - 10.5, "西（カフェ側）", 3.4); T(B(519, 0)[0], BY0 - 10.5, "東", 3.4)
for (xx, hh, s) in [(486, 1000, "チェア"), (493.6, 900, "テーブル"), (501, 650, "ベンチ"),
                    (509, 950, "段床"), (514, 1350, "最上段"), (517.8, 2000, "壁面緑化")]:
    T(B(xx, 0)[0], B(0, hh)[1], s, 3.2, rotation=90 if s == "壁面緑化" else 0)

notes = [
    "【作図根拠・注記】",
    "1. 元図面（HIROBA図）をトレース。縮尺は正式図面の通り芯 6,400（F〜I 19,200）で確定。",
    f"　 （≒{S:.1f}mm/px）。ガラス面は約{GLW}（指示値 約9m と整合）。",
    "2. 右下部分は現況写真（ブース北西→南東方向）と照合して詳細化：",
    "　 壁面緑化／赤クッションのベンチ＋木製段床／黒手摺付き階段3段／",
    "　 木製ステージ（白チェア・白デスク）／天井吊スクリーン・PJ／木製ハイカウンター",
    f"3. ガラス W約{GLW} は緑化端〜南端（スクリーン背面を含む）。",
    "4. 高さ（≈表記）は写真の透視からの推定値。H2,500 はステージ床からの値。",
    "5. 平面寸法の誤差：通り芯基準で ±100〜200mm 程度。家具位置は実測で要確認。",
    "6. 家具配置は正式図面（高解像度）と照合して修正済み。",
]
for i, s in enumerate(notes):
    ax.text(200, 128 - i * 5.6, s, fontsize=4.8 if i else 5.8, ha="left", va="top")
scale_bar(200, 52)
from PIL import Image as _I
ph = np.asarray(_I.open(PHOTO).convert("RGB"))
pw = 120; phh = pw * ph.shape[0] / ph.shape[1]
ax.imshow(ph, extent=(200, 200 + pw, 270 - phh, 270), zorder=1)
ax.add_patch(Rectangle((200, 270 - phh), pw, phh, fc="none", ec="k", lw=0.4))
ax.text(200, 274, "参考：現況写真（A面 北西より）", fontsize=5.5, ha="left")
title_block("A展開図・B-B断面", "2/2")
DXF.add_sheet(ax, 45000.0); pdf.savefig(fig2); fig2.savefig(OUT + "_2.png", dpi=150); fig2.savefig(OUT + "_2.svg"); plt.close(fig2)
pdf.close()
DXF.save(OUT + ".dxf")
