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

GLW = r50(mm_of(GY1 - GY0))
GRID_X = [GX0 + GP * k for k in range(4)]
fig = new_sheet()
EX0, EY0 = 40, 200
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

# ======================================================================
# ELEVATION C : south face (window side) seen from the north
#   viewing south -> east is on the LEFT
# ======================================================================
CX0, CY0 = 30, 62
XE, XW = 521.5, 289.0                     # east / west ends (source px)
def C(xs, h): return CX0 + (XE - xs) * S * K, CY0 + h * K
def crect(x1, x2, h0, h1, **kw):
    a = C(max(x1, x2), h0); b = C(min(x1, x2), h1)
    ax.add_patch(Rectangle(a, b[0] - a[0], b[1] - a[1], zorder=kw.pop("z", 4), **kw))
ax.text(CX0, CY0 + 50, "C 展開図（窓面・北より見る）　S=1/100", fontsize=9, ha="left", va="center")
xL, _ = C(XE, 0); xR, _ = C(XW, 0)
line((xL - 4, CY0), (xR + 4, CY0), 0.8)
line((xL - 4, C(0, CEIL)[1]), (xR + 4, C(0, CEIL)[1]), 0.5)
T(xR + 6, C(0, CEIL)[1], "天井", 3.6, ha="left"); T(xR + 6, CY0, "FL±0", 3.6, ha="left")

# back plane: windows with mullions (pitch 1,600 assumed from grid)
crect(519.8, 293.0, 0, CEIL, fc="#e3f1ff", ec="k", lw=0.5, z=1)
mp = 1600 / S
k0 = math.ceil((293.0 - GX0) / mp); k1 = math.floor((519.8 - GX0) / mp)
for k in range(k0, k1 + 1):
    xm = GX0 + k * mp
    line(C(xm, 0), C(xm, CEIL), 0.3, "#6d8fb0", z=2)
line(C(519.8, 100), C(293.0, 100), 0.3, "#6d8fb0", z=2)        # sill
# columns (round, dia ~970) at grid lines
for gx_ in GRID_X:
    crect(gx_ + 4.6, gx_ - 4.6, 0, CEIL, fc="#cfcfcf", ec="k", lw=0.6, z=3)
    T(C(gx_, 0)[0], C(0, CEIL - 250)[1], "柱", 3.2)
# west wall (section) / east glass partition (section) + wall beyond
crect(293.0, 289.0, 0, CEIL, fc="#9e9e9e", ec="k", lw=0.6, z=6, hatch="////")
crect(519.8, 519.2, 0, CEIL, fc=GLASS, ec=RED, lw=0.7, z=6)
crect(521.5, 519.8, 0, CEIL, fc="#f4f4f4", ec="k", lw=0.3, z=5)

# stage (+250) along the window, east part
crect(517.0, 384.8, 0, STG, fc=WOOD, ec=WOODE, lw=0.5, z=4)
# window counter (sinuous in plan) + chairs
crect(514.5, 410.0, 680, 720, fc="#e7e7e7", ec="k", lw=0.4, z=7)
for xx in (412, 512):
    line(C(xx, 0), C(xx, 680), 0.4, z=7)
for (x, y) in [(420.5, 490.2), (428.3, 487.4), (437.3, 485.0), (448.1, 485.3), (456.4, 486.7), (463.9, 488.6),
               (470.9, 490.9), (478.8, 491.3), (486.2, 489.0), (492.7, 486.2), (501.1, 482.5), (508.0, 484.0),
               (513.1, 488.6)]:
    base = STG if x > 384.8 + (499.1 - y) / 0.4659 else 0
    crect(x + 2.0, x - 2.0, base + 430, base + 460, fc="white", ec="k", lw=0.3, z=8)
    line(C(x - 1.8, base), C(x - 1.8, base + 430), 0.3, z=8); line(C(x + 1.8, base), C(x + 1.8, base + 430), 0.3, z=8)
# high counter (timber, slatted) on stage + stools
crect(498.9, 473.0, STG, STG + 1000, fc="#e8cfa3", ec=WOODE, lw=0.5, z=9)
for xx in np.arange(474.5, 498.5, 2.4):
    line(C(xx, STG), C(xx, STG + 960), 0.25, WOODE, z=10)
crect(499.4, 472.5, STG + 960, STG + 1000, fc="#c9a774", ec=WOODE, lw=0.4, z=10)
# low planter on stage edge + stools
crect(490.0, 443.0, STG, STG + 400, fc="#e8cfa3", ec=WOODE, lw=0.4, z=11)
for xx in np.arange(444.0, 490.0, 3.0):
    ax.add_patch(Circle(C(xx, STG + 520), L(220), fc="#a9d18e", ec=GREENE, lw=0.25, zorder=11))
for xx in (448.5, 462.0, 476.0):
    crect(xx + 2.3, xx - 2.3, STG + 430, STG + 460, fc="white", ec="k", lw=0.3, z=12)
    line(C(xx, STG), C(xx, STG + 430), 0.5, z=12)
# desk + white chairs + screen (edge-on) near the glass
crect(517.0, 514.8, STG + 700, STG + 740, fc="white", ec="k", lw=0.4, z=12)
line(C(516.6, STG), C(516.6, STG + 700), 0.4, z=12); line(C(515.2, STG), C(515.2, STG + 700), 0.4, z=12)
for xx in (506.0, 508.5, 512.2):
    crect(xx + 2.2, xx - 2.2, STG + 430, STG + 460, fc="white", ec="k", lw=0.3, z=12)
    line(C(xx + 2.0, STG + 460), C(xx + 2.0, STG + 800), 0.4, z=12)
crect(518.4, 517.6, STG + 450, CEIL, fc="white", ec="#555", lw=0.5, z=12)
# SE large planter + west planter (box + plants)
for (x1, x2) in ((472.0, 453.0), (364.6, 331.0)):
    crect(x1, x2, 0, 600, fc="#e8cfa3", ec=WOODE, lw=0.4, z=13)
    for xx in np.arange(x2 + 1.5, x1 - 1.0, 3.2):
        ax.add_patch(Circle(C(xx, 1050), L(450), fc="#a9d18e", ec=GREENE, lw=0.25, zorder=13))
# front-most east group: tier / top block / bench / booth tables (north part)
crect(517.4, 505.0, 0, TIER, fc=WOOD2, ec=WOODE, lw=0.5, z=14)
crect(517.2, 511.0, TIER, TOP, fc="#c9a774", ec=WOODE, lw=0.5, z=14)
crect(505.0, 498.0, 0, SEAT, fc="#e9d3ae", ec=WOODE, lw=0.4, z=14)
crect(503.6, 498.7, SEAT, SEAT + 80, fc="#c0687a", ec="k", lw=0.3, z=14)
crect(505.0, 504.2, SEAT, TIER, fc=WOOD2, ec=WOODE, lw=0.3, z=14)
crect(497.2, 490.5, 690, 720, fc="white", ec="k", lw=0.5, z=14)
line(C(493.8, 0), C(493.8, 690), 0.5, z=14)
crect(489.6, 485.4, 430, 460, fc="white", ec="k", lw=0.3, z=14)
line(C(485.6, 460), C(485.6, 820), 0.5, z=14); line(C(489.4, 0), C(489.4, 430), 0.3, z=14)
line(C(485.6, 0), C(485.6, 430), 0.3, z=14)
# green wall (edge-on) on east wall above top block
crect(518.2, 517.4, TOP, CEIL, fc="#7fb069", ec=GREENE, lw=0.3, z=14, hatch="xxx")

# labels
for (x, h, s, c) in [(511.0, 1500, "段床・ベンチ\n（手前）", "#6b4220"), (486.0, STG + 1250, "ハイカウンター", "#6b4220"),
                     (466.5, STG + 900, "低植栽", GREENE), (462.5, 1700, "大型プランター", GREENE),
                     (348.0, 1700, "大型プランター", GREENE), (430.0, 900, "窓際カウンター", "k"),
                     (400.0, 120, "ステージ +250", "#6b4220"), (517.8, CEIL + 120, "スクリーン", "k"),
                     (320.0, 2000, "窓", "#2b5c8a")]:
    T(C(x, 0)[0], C(0, h)[1], s, 3.3, color=c, bbox=dict(fc="white", ec="none", pad=0.2, alpha=0.8))
T(C(519.5, 0)[0], CY0 - 3.5, "東（ガラス面）", 3.4); T(C(291.0, 0)[0], CY0 - 3.5, "西", 3.4)
# grid + dims
for i, gx_ in enumerate(GRID_X):
    c = C(gx_, 0)
    line((c[0], CY0 - 6), (c[0], C(0, CEIL)[1] + 2), 0.2, "#777", ls=(0, (8, 2, 1, 2)), z=0)
    T(c[0], CY0 - 9, "FGHI"[i], 4.2, bbox=dict(boxstyle="circle,pad=0.25", fc="white", ec="#555", lw=0.4))
for a, b in zip(GRID_X[:-1], GRID_X[1:]):
    dimh(C(a, 0)[0], C(b, 0)[0], CY0 - 15, "6,400", 3.6)
dimh(C(XE, 0)[0], C(XW, 0)[0], C(0, CEIL)[1] + 5, f"約 {r50(mm_of(XE - XW))}", 3.8)
dimh(C(GX, 0)[0], C(GRID_X[-1], 0)[0], CY0 - 15, f"約{r50(mm_of(GX - GRID_X[-1]))}", 3.0)
xr_ = xR + 14
dimv(CY0, C(0, CEIL)[1], xr_, "CH≈2,750", 3.4)
x_l = xL - 4
dimv(CY0, C(0, STG)[1], x_l, "≈250", 3.0, side=-1)
dimv(CY0, C(0, TIER)[1], x_l - 4, "≈800", 3.0, side=-1)
dimv(CY0, C(0, TOP)[1], x_l - 8, "≈1,200", 3.0, side=-1)
dimv(C(0, STG)[1], C(0, STG + 1000)[1], C(470.0, 0)[0], "≈1,000", 2.8)

# key plan with view directions
KX, KY, KS = 304, 215, 1 / 500.0
def KP(x, y): return KX + (x - 291) * S * KS, KY + (498 - y) * S * KS
ax.add_patch(Polygon([KP(291, 288), KP(522, 288), KP(522, 498), KP(291, 498)], closed=True, fc="#fafafa", ec="k", lw=0.5))
ax.add_patch(Polygon([KP(*p) for p in [(384.8, 498), (502, 444.5), (498.5, 444.4), (498.5, 427), (517, 445), (517, 498)]],
                     closed=True, fc=WOOD, ec="none"))
line(KP(GX, GY0), KP(GX, GY1), 1.0, RED)
for (p, q, s) in [((470, 452), (510, 452), "A"), ((405, 300), (405, 340), "C")]:
    ax.annotate("", xy=KP(*q), xytext=KP(*p), arrowprops=dict(arrowstyle="-|>", lw=0.8, color="k"))
    T(KP(*p)[0] - 2.5, KP(*p)[1] + 2.5, s, 4.6, bbox=dict(boxstyle="circle,pad=0.2", fc="white", ec="k", lw=0.4))
ax.text(KX, KY + 210 * S * KS + 5, "キープラン（視点方向）", fontsize=5.5, ha="left")

notes = [
    "【注記】",
    "1. 通り芯 6,400（F〜I）基準。平面位置は完全版平面図 v2 と同一データ。",
    "2. 高さ（≈表記）は現況写真からの推定値。実測で要確認。",
    "3. C展開図の窓マリオン割付（1,600）・窓台高さは推定。",
    "4. C展開図は主要な造作・家具のみ表示（カフェテーブル等は省略）。",
    f"5. ガラス面 W約{GLW}（緑化端〜南端）。H2,500 はステージ床から。",
]
for i, s in enumerate(notes):
    ax.text(304, 130 - i * 5.4, s, fontsize=4.4 if i else 5.4, ha="left", va="top")

line((298, 8), (298, H - 8), 0.4)
scale_bar(304, 52)
title_block("A・C 展開図", "1/1")
fig.savefig(OUT + ".pdf"); fig.savefig(OUT + ".png", dpi=170); fig.savefig(OUT + ".svg")
