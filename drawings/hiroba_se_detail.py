"""HIROBA lower-right detail: plan / elevation A / section B at 1:100 on A3."""
import math, sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager as fm
from matplotlib.patches import Rectangle, Polygon, Circle
from matplotlib.transforms import Affine2D
from PIL import Image

fm.fontManager.addfont("/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf")
plt.rcParams["font.family"] = "IPAGothic"
plt.rcParams["hatch.linewidth"] = 0.3

SRC = sys.argv[1]           # source floor image (img-000.png)
OUT = sys.argv[2]

S = 9000 / 87.0             # mm per source px (glass wall 87px = 9,000mm)
K = 1 / 100.0               # 1:100
RX0, RX1, RY0, RY1 = 436, 536, 384, 504   # detail window (source px)
OX, OY = 28, 118             # paper position of window's lower-left (mm)

W, H = 420, 297
fig = plt.figure(figsize=(W / 25.4, H / 25.4))
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, W); ax.set_ylim(0, H); ax.set_aspect("equal"); ax.axis("off")

def P(x, y):
    return OX + (x - RX0) * S * K, OY + (RY1 - y) * S * K

def L(mm): return mm * K

def mm_of(px): return px * S

def r50(v): return f"{int(round(v / 50.0) * 50):,}"

WOOD, WOOD2, WOODE = "#f1dfbd", "#dcc095", "#8a5a2b"
GREEN, GREENE = "#cfe3c4", "#3c6e27"
GLASS, RED = "#d6ecff", "#c8102e"

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
    """rotated rect along source points a->b; side shifts it perpendicular (mm)."""
    p, q = P(*a), P(*b)
    ang = math.degrees(math.atan2(q[1] - p[1], q[0] - p[0]))
    ln = math.hypot(q[0] - p[0], q[1] - p[1]) / K
    nx, ny = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
    rrect((p[0] + q[0]) / 2 + nx * L(side), (p[1] + q[1]) / 2 + ny * L(side), ln, width, ang, **kw)
    return ang, ln

def T(x, y, s, size=4.2, **kw):
    ax.text(x, y, s, fontsize=size, ha=kw.pop("ha", "center"), va=kw.pop("va", "center"), zorder=12, **kw)

def leader(src, txt_xy, s, size=4.2, ha="left", c="k"):
    ax.annotate(s, xy=src, xytext=txt_xy, fontsize=size, ha=ha, va="center", color=c, zorder=12,
                arrowprops=dict(arrowstyle="-", lw=0.25, color=c, shrinkA=0.5, shrinkB=0),)
    ax.add_patch(Circle(src, 0.35, fc=c, ec="none", zorder=12))

def dimh(x1, x2, y, label, size=3.8, c="k", ext_from=None):
    line((x1, y), (x2, y), 0.25, c, z=9)
    for xx in (x1, x2):
        line((xx - 0.7, y - 0.7), (xx + 0.7, y + 0.7), 0.4, c, z=9)
        if ext_from is not None:
            line((xx, ext_from), (xx, y + (0.8 if y > ext_from else -0.8)), 0.15, c, z=9)
    T((x1 + x2) / 2, y + 1.4, label, size, color=c)

def dimv(y1, y2, x, label, size=3.8, c="k", ext_from=None, side=1):
    line((x, y1), (x, y2), 0.25, c, z=9)
    for yy in (y1, y2):
        line((x - 0.7, yy - 0.7), (x + 0.7, yy + 0.7), 0.4, c, z=9)
        if ext_from is not None:
            line((ext_from, yy), (x + (0.8 if x > ext_from else -0.8), yy), 0.15, c, z=9)
    T(x + 1.5 * side, (y1 + y2) / 2, label, size, rotation=90, color=c)

# ======================================================================
# PLAN (detail of lower-right)
# ======================================================================
GX = 519.5                       # glass centre line
GY0, GY1 = 410.5, 497.5          # glass north / south end  (87 px = 9,000)
FAC = 497.5                      # inner face of facade
TIER_S = 427.0                   # south end of bench / upper tier
STEP0 = [(499.0, 427.0), (503.0, 427.0), (507.0, 427.0)]  # stair nosings start (slope 1:1 to SE)
LINEA = ((436.0, 472.6), (499.0, 444.4))                    # stage edge

# --- stage (raised timber floor) -------------------------------------
stage = [LINEA[0], LINEA[1], (498.5, 444.4), (498.5, 427.0), (517.0, 445.0),
         (517.0, FAC), (RX0, FAC)]
poly(stage, fc=WOOD, ec="none", z=1)
line(P(*LINEA[0]), P(*LINEA[1]), 0.6, WOODE, z=4)
line(P(498.5, 427), P(498.5, 444.4), 0.6, WOODE, z=4)

# --- upper tier (stepped timber seating, behind bench) -----------------
poly([(505, RY0), (517.4, RY0), (517.4, 445), (507, 427 + 0), (505, 427)], fc=WOOD2, ec=WOODE, lw=0.5, z=2)
box(511, 390, 517.2, 406, fc="#c9a774", ec=WOODE, lw=0.5, z=3)            # top block
# stair nosings (3 steps) between tier and stage
for (sx, sy) in STEP0:
    ex = 517.0
    ey = sy + (ex - sx)
    poly([(sx, sy), (ex, ey), (ex, ey + 0.01)], ec=WOODE, lw=0.5, z=4)
    line(P(sx, sy), P(ex, ey), 0.5, WOODE, z=4)
poly([(498.5, 427), (507, 427), (517, 437), (517, 445)], fc=WOOD2, ec="none", z=1, alpha=0.6)
# handrails (photo)
line(P(499.3, 427.8), P(507.2, 427.8), 0.9, "#333", z=6)
line(P(516.3, 437.5), P(516.3, 445.2), 0.9, "#333", z=6)
# bench (red cushions) with timber back
box(498, RY0, 505, 427, fc="#e9d3ae", ec="k", lw=0.5, z=3)
line(P(504.2, RY0), P(504.2, 427), 0.4, WOODE, z=4)
for (y1, y2) in [(RY0, 390.5), (391.0, 397.3), (397.6, 403.9), (411.0, 417.0), (417.3, 423.6)]:
    box(498.7, y1, 503.6, y2, fc="#c0687a", ec="k", lw=0.25, z=4)
# booth tables + chairs
for (y1, y2) in [(391.0, 397.3), (397.6, 403.9), (411.0, 417.0), (417.3, 423.6)]:
    box(490.0, y1, 497.2, y2, fc="white", ec="k", lw=0.5, z=5)
for cy in (394.2, 400.8, 414.0, 420.5):
    cx_, cy_ = P(486.0, cy); rrect(cx_, cy_, 430, 430, 0, lw=0.3)
box(490.0, RY0, 497.2, 389.5, fc="white", ec="k", lw=0.5, z=5)
cx_, cy_ = P(486.0, 386.5); rrect(cx_, cy_, 430, 430, 0, lw=0.3)

# --- east wall / green wall / glass ----------------------------------
box(518.2, RY0, 521.0, GY0, fc="#9e9e9e", ec="k", lw=0.6, z=3, hatch="////")
box(517.4, 391.0, 518.2, GY0, fc="#7fb069", ec=GREENE, lw=0.4, z=4, hatch="xxxx")
gx0, gy0 = P(GX, GY0); gx1, gy1 = P(GX, GY1)
ax.add_patch(Rectangle((gx0 - 0.45, gy1), 0.9, gy0 - gy1, fc=GLASS, ec=RED, lw=0.7, zorder=6))
line((gx0, gy0), (gx0, gy1), 0.25, RED, z=7)
# planter strip along glass base (photo)
box(516.6, GY0, 518.6, 445.0, fc=GREEN, ec=GREENE, lw=0.3, z=3, hatch="....")
# facade
box(RX0, FAC, RX1, 503.5, fc="#222", ec="k", lw=0.3, z=3)
for x1 in (436.5, 497.0):
    box(x1, 489.0, x1 + 9.0, FAC, fc="#a8a8a8", ec="k", lw=0.6, z=5, hatch="\\\\")
# window counter + stools
box(445.5, 492.2, 497.0, 494.4, fc="#e7e7e7", ec="k", lw=0.4, z=4)
for (x, y) in [(447.2, 483.9), (456.7, 486.7), (463.9, 488.3), (471.3, 490.2), (478.4, 490.3),
               (485.2, 489.2), (492.6, 485.9), (500.2, 482.8), (507.2, 482.8), (513.1, 487.8)]:
    c = P(x, y)
    ax.add_patch(Circle(c, L(230), fc="white", ec="k", lw=0.3, zorder=5))
    ax.add_patch(Circle(c, 0.25, fc="k", ec="none", zorder=6))

# --- stage furniture ---------------------------------------------------
band((460.0, 462.4), (488.0, 450.2), 350, side=-220, fc=GREEN, ec=GREENE, lw=0.35, z=5, hatch="....")
ang, _ = band((472.2, 477.8), (498.9, 466.1), 600, fc="#e8cfa3", ec=WOODE, lw=0.5, z=5)
for (x, y) in [(451.1, 474.1), (464.4, 468.3), (477.6, 462.3)]:
    for dx, dy in ((0, 0), (4.5, -2.0)):
        c = P(x + dx, y + dy)
        ax.add_patch(Circle(c, L(200), fc="white", ec="k", lw=0.3, zorder=5))
    ax.add_patch(Circle(P(x, y), 0.25, fc="k", ec="none", zorder=6))
for (x, y, a) in [(507.8, 456.1, 20), (512.2, 460.6, -10), (511.7, 466.1, 15)]:
    cx_, cy_ = P(x, y); rrect(cx_, cy_, 480, 480, a, lw=0.3)
box(514.3, 459.5, 517.0, 472.0, fc="white", ec="k", lw=0.45, z=5)       # desk
# screen in front of glass + projector (ceiling)
line(P(518.2, 461.0), P(518.2, 487.0), 1.4, "#888", z=6)
pj = P(486.0, 472.0)
rrect(pj[0], pj[1], 500, 450, ang, fc="none", ec="k", lw=0.35, z=7, ls=(0, (2, 1)))
for tgt in ((518.2, 461.0), (518.2, 487.0)):
    line(pj, P(*tgt), 0.2, "#666", ls=(0, (3, 2)), z=6)
# large planter (NW of detail)
poly([(453, 389), (458, 389), (463, 397), (468, 409), (472, 420), (470, 426), (466, 426),
      (462, 416), (457, 405), (453, 395)], fc=GREEN, ec=GREENE, lw=0.5, z=4, hatch="xxx")
pj1 = P(466.0, 399.0)
rrect(pj1[0], pj1[1], 500, 450, 0, fc="none", ec="k", lw=0.35, z=7, ls=(0, (2, 1)))
# cafe tables (west edge)
for (x, y) in [(442.0, 391.0), (446.5, 409.5), (452.0, 430.0)]:
    c = P(x, y); a = 30
    rrect(c[0], c[1], 700, 700, a, lw=0.4, z=5)
    for k in range(4):
        t = math.radians(a + 90 * k)
        rrect(c[0] + L(600) * math.cos(t), c[1] + L(600) * math.sin(t), 420, 420, a + 90 * k, lw=0.25)
# central screen line (context)
line(P(436.0, 457.5), P(455.6, 450.0), 1.2, "#555", z=4)

# --- room beyond glass (context) -------------------------------------
box(521.0, 410.5, 532.5, FAC, fc="#f4f4f4", ec="none", z=0)
line(P(532.5, RY0), P(532.5, FAC), 0.5, "k")
for y in np.arange(411.0, 496.0, 4.2):
    box(521.6, y, 526.0, y + 4.0, fc="white", ec="#777", lw=0.2, z=2)
for y1 in (430.0, 487.0):
    box(531.3, y1, 533.6, y1 + 3.5, fc="#555", ec="k", lw=0.2, z=3)

# break lines (detail window edges)
for (a, b) in [((RX0, RY0), (RX1, RY0)), ((RX0, RY0), (RX0, FAC))]:
    line(P(*a), P(*b), 0.25, "#888", ls=(0, (6, 2, 1, 2)), z=8)

# --- labels -----------------------------------------------------------
x, y = P(510.5, 482); T(x, y, "ステージ\n（木製床）", 4.2, color="#6b4220")
x, y = P(511, 418); T(x, y, "上段\n（木製段床）", 3.8, color="#6b4220")
x, y = P(514, 398); T(x, y, "最上段", 3.4, color="#6b4220")
x, y = P(501, 407.5); T(x, y, "ベンチ", 3.6, rotation=90)
x, y = P(512, 433.5); T(x, y, "階段3段", 3.6, rotation=-45, color="#6b4220")
x, y = P(527, 450); T(x, y, "隣室\n（サイン「3」）", 3.6, color="#555")

R = OX + (RX1 - RX0) * S * K + 6          # right label column x
leader(P(GX, 430), (R + 18, P(0, 430)[1]), "ガラス面（W 約9,000）", 4.2, c=RED)
leader(P(517.8, 400), (R + 18, P(0, 398)[1]), "壁面緑化", 4.2, c=GREENE)
leader(P(517.6, 436), (R + 18, P(0, 440)[1]), "植栽（ガラス際）", 4.2, c=GREENE)
leader(P(518.2, 474), (R + 18, P(0, 472)[1]), "スクリーン W≈2,700（天井吊）", 4.2)
leader(P(515.6, 466), (R + 18, P(0, 463)[1]), "デスク（白）", 4.2)
leader(P(510, 461), (R + 18, P(0, 456)[1]), "チェア（白）", 4.2)
leader(P(516.3, 441), (R + 18, P(0, 447)[1]), "手摺（黒）", 4.2)
leader(P(487, 470.2), (R + 18, P(0, 484)[1]), "ハイカウンター（木製）／PJ（天井）", 4.2)
x0_, y0_ = P(486, 386.5)
T(OX + 1, P(0, 380)[1] + 1.5, "", 1)

def lab(src, dst, s, c="k"):
    ax.annotate(s, xy=P(*src), xytext=dst, fontsize=4.0, ha="center", va="center", color=c, zorder=12,
                arrowprops=dict(arrowstyle="-", lw=0.25, color=c), bbox=dict(fc="white", ec="none", pad=0.3))
lab((474, 456.0), P(462, 446), "低植栽", GREENE)
lab((464.4, 468.3), P(452, 482), "スツール")
lab((462, 410), P(457, 386.5), "大型プランター", GREENE)
lab((493.6, 394.2), P(480, 431), "テーブル＋チェア")
x, y = P(465, 495.3); T(x, y, "窓際カウンター＋スツール", 3.6, bbox=dict(fc="white", ec="none", pad=0.2))
x, y = P(445, 466); T(x, y, "ステージ端（段差）", 3.6, rotation=24, color="#6b4220")

# --- dimensions --------------------------------------------------------
ex = P(521, 0)[0]
DX = ex + 3.0
ys = [391.0, GY0, TIER_S, 445.0, GY1]
labels = ["約2,100", "約1,700", "約1,850", "約5,450"]
for (a, b), s in zip(zip(ys[:-1], ys[1:]), labels):
    dimv(P(0, b)[1], P(0, a)[1], DX, s, ext_from=ex - 0.5, size=3.4)
dimv(P(0, GY1)[1], P(0, GY0)[1], DX + 8, "ガラス 9,000", c=RED, size=4.0)
TY = P(0, RY0)[1] + 4
xs = [484.0, 490.0, 497.2, 505.0, 517.4, GX]
dl = ["", "", "", "", ""]
for a, b in zip(xs[:-1], xs[1:]):
    dimh(P(a, 0)[0], P(b, 0)[0], TY, r50(mm_of(b - a)), size=3.2)
dimh(P(498.0, 0)[0], P(GX, 0)[0], TY + 5, "ベンチ〜ガラス 約" + r50(mm_of(GX - 498.0)), size=3.4)
# section / elevation marks
def mark(p, txt, rot):
    T(p[0], p[1], txt, 5.5, bbox=dict(boxstyle="circle,pad=0.2", fc="white", ec="k", lw=0.5))
pa = P(536.0, 455.0); mark((pa[0] + 2.5, pa[1]), "A", 0)
ax.annotate("", xy=(pa[0] - 1.6, pa[1]), xytext=(pa[0] + 0.6, pa[1]),
            arrowprops=dict(arrowstyle="-|>", lw=0.5, color="k"))
by = P(0, 400.0)[1]
line((P(480.5, 0)[0], by), (P(523, 0)[0], by), 0.35, "k", ls=(0, (6, 1.5, 1, 1.5)), z=9)
for xx in (P(480.5, 0)[0], P(523, 0)[0]):
    mark((xx, by + 2.6), "B", 0)

ax.text(OX, H - 18, "HIROBA 右下部分 詳細平面図　S=1/100", fontsize=10, ha="left", va="center")
ax.text(OX, H - 24, "元図面トレース＋現況写真照合（ガラス面 W9,000 を基準に縮尺換算）", fontsize=5.5, ha="left",
        va="center", color="#444")

# north + scale bar
na = (OX + 4, OY - 30)
ax.add_patch(Polygon([(na[0], na[1] + 7), (na[0] - 2.5, na[1] - 3), (na[0], na[1] - 1.2)], fc="k", ec="k", lw=0.3))
ax.add_patch(Polygon([(na[0], na[1] + 7), (na[0] + 2.5, na[1] - 3), (na[0], na[1] - 1.2)], fc="white", ec="k", lw=0.3))
T(na[0], na[1] + 9.5, "図上北", 3.8)
sb = (OX + 12, OY - 30)
for i in range(5):
    ax.add_patch(Rectangle((sb[0] + i * 10, sb[1]), 10, 1.4, fc="k" if i % 2 == 0 else "white", ec="k", lw=0.3))
    T(sb[0] + i * 10, sb[1] - 2.2, str(i), 3.6)
T(sb[0] + 52, sb[1] - 2.2, "5m", 3.6)
ax.text(sb[0], sb[1] + 3.5, "1/100 (A3)", fontsize=4.2, ha="left")

# ======================================================================
# ELEVATION A (east face, seen from west)
# ======================================================================
EX0, EY0 = 205, 196            # paper origin = north end of green wall, FL
def E(ysrc, h):                # source y (N->S runs left->right), height mm
    return EX0 + (ysrc - 391.0) * S * K, EY0 + h * K

CEIL, STG, TIER, TOP, SEAT = 2750, 250, 800, 1200, 420
ax.text(EX0, EY0 + 41, "A 展開図（ガラス面・西より見る）　S=1/100", fontsize=8.5, ha="left", va="center")
x0, _ = E(391.0, 0); x1, _ = E(GY1, 0)
line((x0 - 4, EY0), (x1 + 4, EY0), 0.8)
line((x0 - 4, E(0, CEIL)[1]), (x1 + 4, E(0, CEIL)[1]), 0.5)
T(x1 + 6, E(0, CEIL)[1], "天井", 3.6, ha="left")
T(x1 + 6, EY0, "FL±0", 3.6, ha="left")
# wall + green wall
gx, _ = E(GY0, 0)
ax.add_patch(Rectangle((x0, EY0), gx - x0, L(CEIL), fc="#93c47d", ec=GREENE, lw=0.4, hatch="xxx", zorder=2))
# glass
ax.add_patch(Rectangle((gx, EY0), x1 - gx, L(CEIL), fc=GLASS, ec="k", lw=0.6, zorder=2))
for i in range(6):
    xx = gx + 3 + i * (x1 - gx) / 6
    line((xx, EY0 + L(1900)), (xx + 3, EY0 + L(2350)), 0.2, "#7aa7d6", z=3)
sx, sy = E(450.0, 1650)
T(sx, sy, "3", 5, color="#2b5c8a", bbox=dict(fc="white", ec="#2b5c8a", lw=0.3, pad=0.6))
# front objects (timber)
def erect(y1, y2, h0, h1, **kw):
    a = E(y1, h0); b = E(y2, h1)
    ax.add_patch(Rectangle(a, b[0] - a[0], b[1] - a[1], zorder=kw.pop("z", 4), **kw))
erect(391.0, TIER_S, 0, TIER, fc=WOOD2, ec=WOODE, lw=0.5)
erect(391.0, 406.0, TIER, TOP, fc="#c9a774", ec=WOODE, lw=0.5)
erect(391.0, TIER_S, 0, SEAT, fc="#e9d3ae", ec=WOODE, lw=0.4, z=5)
# stair (3 steps)
for i in range(3):
    yA = TIER_S + i * 6.0
    hh = TIER - (i + 1) * (TIER - STG) / 3
    erect(yA, yA + 6.0, 0, hh + (TIER - STG) / 3, fc=WOOD2, ec=WOODE, lw=0.4)
erect(445.0, GY1, 0, STG, fc=WOOD, ec=WOODE, lw=0.5)
# railing
ra, rb = E(TIER_S + 1, TIER + 1000), E(445.0, STG + 1000)
line(ra, rb, 0.8, "#333", z=6)
for yy, hh in ((TIER_S + 1, TIER), (445.0, STG)):
    p = E(yy, hh); q = E(yy, hh + 1000)
    line(p, q, 0.6, "#333", z=6)
# plants at glass base
for i in range(14):
    yy = GY0 + 1 + i * 2.5
    base = TIER if yy < TIER_S else (STG if yy > 445 else TIER - (yy - TIER_S) / 18 * (TIER - STG))
    c = E(yy, base + 200)
    ax.add_patch(Circle(c, L(260), fc="#a9d18e", ec=GREENE, lw=0.25, zorder=5))
# screen
erect(461.0, 487.0, STG + 450, CEIL - 150, fc="white", ec="#555", lw=0.5, z=6)
erect(461.0, 487.0, CEIL - 150, CEIL, fc="#666", ec="k", lw=0.3, z=6)
T((E(461, 0)[0] + E(487, 0)[0]) / 2, E(0, 1500)[1], "スクリーン", 3.6, z=7) if False else \
    T((E(461, 0)[0] + E(487, 0)[0]) / 2, E(0, 1500)[1], "スクリーン", 3.6)
# desk & chair on stage
erect(459.5, 472.0, STG + 700, STG + 740, fc="white", ec="k", lw=0.4, z=7)
for yy in (461.0, 470.5):
    line(E(yy, STG), E(yy, STG + 700), 0.4, "k", z=7)
# dims
ytop = E(0, CEIL)[1] + 4
dimh(E(391.0, 0)[0], gx, ytop, "緑化 約2,100", size=3.4)
dimh(gx, x1, ytop, "ガラス W 約9,000", c=RED, size=4.0)
xr = x1 + 14
dimv(E(0, STG)[1], E(0, CEIL)[1], xr - 6, "H 約2,500", c=RED, size=3.8, ext_from=x1)
dimv(EY0, E(0, STG)[1], xr - 6, "", size=3.2, ext_from=x1)
T(xr - 3.5, EY0 + L(STG) / 2, "≈250", 3.0, ha="left")
dimv(EY0, E(0, CEIL)[1], xr, "CH≈2,750", size=3.4)
x_l = E(391.0, 0)[0] - 4
dimv(EY0, E(0, SEAT)[1], x_l, "≈420", size=3.0, side=-1)
dimv(EY0, E(0, TIER)[1], x_l - 4, "≈800", size=3.0, side=-1)
dimv(EY0, E(0, TOP)[1], x_l - 8, "≈1,200", size=3.0, side=-1)
T(E(395, 0)[0], EY0 - 3.5, "北", 3.6); T(E(495, 0)[0], EY0 - 3.5, "南（窓側）", 3.6)

# ======================================================================
# SECTION B-B (through booth / bench / tier / green wall at y=400)
# ======================================================================
BX0, BY0 = 205, 118
def B(xsrc, h):
    return BX0 + (xsrc - 482.0) * S * K, BY0 + h * K
ax.text(BX0, BY0 + 40, "B-B 断面（ベンチ・段床）　S=1/100", fontsize=8.5, ha="left", va="center")
line(B(480, 0), B(524, 0), 0.8)
line(B(480, CEIL), B(521, CEIL), 0.5)
def brect(x1, x2, h0, h1, **kw):
    a = B(x1, h0); b = B(x2, h1)
    ax.add_patch(Rectangle(a, b[0] - a[0], b[1] - a[1], zorder=kw.pop("z", 4), **kw))
# chair
c0 = B(484.0, 0)
line(B(484.0, 450), B(488.2, 450), 0.6); line(B(484.3, 0), B(484.3, 850), 0.5); line(B(488, 0), B(488, 450), 0.4)
# table
line(B(490.0, 720), B(497.2, 720), 1.0); line(B(493.6, 0), B(493.6, 720), 0.5)
# bench / tier / top block
brect(498.0, 504.2, 0, SEAT, fc="#e9d3ae", ec=WOODE, lw=0.5)
brect(498.6, 503.8, SEAT, SEAT + 80, fc="#c0687a", ec="k", lw=0.3, z=5)
brect(504.2, 517.4, 0, TIER, fc=WOOD2, ec=WOODE, lw=0.5)
brect(511.0, 517.2, TIER, TOP, fc="#c9a774", ec=WOODE, lw=0.5)
# green wall + wall
brect(517.4, 518.2, TOP, CEIL, fc="#93c47d", ec=GREENE, lw=0.4, hatch="xxx")
brect(518.2, 521.0, 0, CEIL, fc="#9e9e9e", ec="k", lw=0.6, hatch="////")
# dims
dimh(B(484.0, 0)[0], B(490.0, 0)[0], BY0 - 6, r50(mm_of(6.0)), size=3.0)
dimh(B(490.0, 0)[0], B(497.2, 0)[0], BY0 - 6, r50(mm_of(7.2)), size=3.0)
dimh(B(498.0, 0)[0], B(504.2, 0)[0], BY0 - 6, r50(mm_of(6.2)), size=3.0)
dimh(B(504.2, 0)[0], B(517.4, 0)[0], BY0 - 6, r50(mm_of(13.2)), size=3.0)
xr = B(521.0, 0)[0] + 4
dimv(BY0, B(0, SEAT)[1], xr, "≈420", size=3.0)
dimv(BY0, B(0, TIER)[1], xr + 5, "≈800", size=3.0)
dimv(BY0, B(0, TOP)[1], xr + 10, "≈1,200", size=3.0)
dimv(BY0, B(0, CEIL)[1], xr + 15, "CH≈2,750", size=3.2)
T(B(484, 0)[0], BY0 - 10.5, "西（カフェ側）", 3.4); T(B(519, 0)[0], BY0 - 10.5, "東", 3.4)

# ======================================================================
# KEY PLAN + NOTES + TITLE
# ======================================================================
src = Image.open(SRC).convert("L").crop((285, 280, 540, 506))
kx, ky, kw = 338, 205, 70
kh = kw * src.height / src.width
ax.imshow(np.asarray(src), cmap="gray", extent=(kx, kx + kw, ky, ky + kh), zorder=1, alpha=0.75)
fx = lambda x: kx + (x - 285) / src.width * kw
fy = lambda y: ky + kh - (y - 280) / src.height * kh
ax.add_patch(Rectangle((fx(RX0), fy(RY1)), fx(RX1) - fx(RX0), fy(RY0) - fy(RY1), fc="none", ec=RED, lw=0.9, zorder=3))
ax.text(kx, ky + kh + 4, "キープラン（元図面）", fontsize=6, ha="left")

notes = [
    "【作図根拠・注記】",
    "1. 平面は元図面（HIROBA図）を画素単位でトレース。縮尺はガラス面 87px＝9,000mm",
    "　 （≒103mm/px）で換算。テーブル700・椅子450・段板≈290 と整合を確認済み。",
    "2. 写真は本図ブース北西から南東方向を撮影したものと判断し、以下を照合：",
    "　 ・壁面緑化（ガラス北端）・赤クッションのベンチ＋木製段床・黒手摺付き階段",
    "　 ・木製ステージ（白チェア・白デスク）・天井吊スクリーン・PJ・木製ハイカウンター",
    "3. ガラス W9,000 は緑化端〜南端。写真の矢印終点はスクリーンで隠れる手前まで。",
    "4. 高さ（≈表記）は写真の透視からの推定値。H2,500 はステージ床からの値。",
    "5. 平面寸法の誤差は元図面解像度により ±100〜200mm 程度。実測で要確認。",
]
for i, s in enumerate(notes):
    ax.text(338, 190 - i * 5.0, s, fontsize=4.1 if i else 5.4, ha="left", va="top")

tb = (338, 14, 72, 30)
ax.add_patch(Rectangle(tb[:2], tb[2], tb[3], fc="none", ec="k", lw=0.8))
for yy in (24, 34):
    line((tb[0], yy), (tb[0] + tb[2], yy), 0.4)
line((tb[0] + 17, 14), (tb[0] + 17, 44), 0.4)
for (lbl, val, yy) in [("図面名", "HIROBA 右下部分 詳細", 39), ("縮尺", "1/100（A3）", 29), ("日付", "2026.10.13", 19)]:
    ax.text(tb[0] + 2, yy, lbl, fontsize=5, va="center")
    ax.text(tb[0] + 19, yy, val, fontsize=6, va="center")

ax.add_patch(Rectangle((8, 8), W - 16, H - 16, fc="none", ec="k", lw=1.0))
line((196, 8), (196, H - 8), 0.4)
line((334, 8), (334, H - 8), 0.4)

fig.savefig(OUT + ".pdf")
fig.savefig(OUT + ".png", dpi=170)
