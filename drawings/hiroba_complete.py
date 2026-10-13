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
OUT = sys.argv[1]
PHOTO = sys.argv[2]

S = 9000 / 87.0            # mm per source px (glass wall 87px = 9,000mm)
K = 1 / 100.0              # 1:100
W, H = 420, 297
WOOD, WOOD2, WOODE = "#f1dfbd", "#dcc095", "#8a5a2b"
GREEN, GREENE = "#cfe3c4", "#3c6e27"
GLASS, RED = "#d6ecff", "#c8102e"
GX, GY0, GY1, FAC, TIER_S = 519.5, 410.5, 497.5, 497.5, 427.0
CEIL, STG, TIER, TOP, SEAT = 2750, 250, 800, 1200, 420

ax = None
OX, OY, X0, Y0 = 22.0, 40.0, 290.0, 498.0

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
    for (lbl, val, yy) in [("図面名", name, 39), ("縮尺", "1/100（A3）", 29), ("日付/番号", f"2026.10.13　{sheet}", 19)]:
        ax.text(tb[0] + 2, yy, lbl, fontsize=4.6, va="center")
        ax.text(tb[0] + 17, yy, val, fontsize=5.4, va="center")

def scale_bar(x, y):
    for i in range(5):
        ax.add_patch(Rectangle((x + i * 10, y), 10, 1.4, fc="k" if i % 2 == 0 else "white", ec="k", lw=0.3))
        T(x + i * 10, y - 2.2, str(i), 3.6)
    T(x + 52, y - 2.2, "5m", 3.6)
    ax.text(x, y + 3.5, "SCALE 1/100 (A3)", fontsize=4.2, ha="left")

pdf = PdfPages(OUT + ".pdf")

# ======================================================================
# SHEET 1 : PLAN
# ======================================================================
fig1 = new_sheet()
LW_WALL, LW_MID = 1.4, 0.6
xl, yt = 291, 288

# --- general walls ------------------------------------------------------
line(P(xl, yt), P(xl, FAC), LW_WALL)
line(P(522, yt), P(522, 360), LW_WALL)
line(P(467, 360), P(532.5, 360), LW_MID)
box(xl - 2, FAC, 536, 503.5, fc="#222", ec="k", lw=0.3, z=3)          # south facade
line(P(304, 316), P(304, 450), LW_MID)
for y in range(318, 450, 6):
    box(293, y, 303, y + 6, lw=0.2)
for y in range(452, 496, 5):
    box(293, y, 299, y + 5, lw=0.2)
for y in range(310, 358, 6):
    box(522, y, 528, y + 6, lw=0.2)

# back-of-house rooms (north)
for (x1, x2) in [(287, 358), (375, 446), (466, 535)]:
    box(x1, 289, x2, 307, fc="#bdbdbd", lw=LW_MID, z=1)
    for xx in range(int(x1) + 10, int(x2), 18):
        box(xx, 292, xx + 8, 296, fc="white", lw=0.2, z=2)
for (x1, y1) in [(317, 490), (378, 490), (377, 307), (440, 307), (305, 308), (335, 310)]:
    box(x1, y1, x1 + 7, y1 + 8, fc="#a8a8a8", ec="k", lw=0.6, z=5, hatch="\\\\")
for x1 in (436.5, 497.0):
    box(x1, 489.0, x1 + 9.0, FAC, fc="#a8a8a8", ec="k", lw=0.6, z=5, hatch="\\\\")

# cafe counter / pantry / library
box(375, 308, 446, 337, lw=LW_MID); box(379, 327, 446, 336); box(375, 315, 421, 324)
T(*P(410, 321), "カフェカウンター", 4.5)
box(468, 308, 518, 348, lw=LW_MID)
for (a, b, c, d) in [(470, 333, 488, 345), (490, 333, 508, 345), (480, 318, 494, 327), (496, 318, 510, 327), (470, 310, 478, 330)]:
    box(a, b, c, d, lw=0.25)
T(*P(493, 340), "パントリー", 4)
poly([(322, 327), (348, 333), (345, 346), (327, 343)], ec="k", lw=0.8, ls=(0, (3, 1)), z=3)
T(*P(330, 322), "Library", 4.5)
line(P(304, 316), P(330, 316), LW_MID); line(P(310, 316), P(310, 345), LW_MID)

# communal table
cx, cy = P(354, 367); rrect(cx, cy, 3600, 950, 22, lw=0.5, z=4)
for k in range(-3, 4):
    for s in (-1, 1):
        a = math.radians(22)
        chair(cx + k * L(480) * math.cos(a) - math.sin(a) * L(780) * s,
              cy + k * L(480) * math.sin(a) + math.cos(a) * L(780) * s, 22)
# booth seating (north centre)
for i in range(4):
    x = 388 + i * 12.5
    box(x, 357, x + 11, 373, lw=0.4); box(x + 2, 361, x + 9, 369, lw=0.25)
box(387, 357, 438, 359.5, fc="k", lw=0.2)
# cafe tables
for (x, y) in [(362, 398), (382, 388), (415, 400), (370, 418), (393, 409), (420, 419),
               (380, 440), (403, 430), (430, 438), (390, 458), (413, 453)]:
    table4(x, y)
for (x, y) in [(442.0, 391.0), (446.5, 409.5), (452.0, 430.0)]:
    table4(x, y, 30)
# west high counter (diagonal) + stools
p, q = P(339, 418), P(362, 460)
ang = math.degrees(math.atan2(q[1] - p[1], q[0] - p[0])); ln = math.hypot(q[0] - p[0], q[1] - p[1]) / K
rrect((p[0] + q[0]) / 2, (p[1] + q[1]) / 2, ln, 600, ang, fc="#e6e6e6", lw=0.5, z=4)
for t in range(1, int(ln // 650)):
    u = t * 650 / ln
    bx, by = p[0] + (q[0] - p[0]) * u, p[1] + (q[1] - p[1]) * u
    nx, ny = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
    for s in (-1, 1):
        ax.add_patch(Circle((bx + nx * L(550) * s, by + ny * L(550) * s), L(180), fc="white", ec="k", lw=0.2, zorder=3))
# central screen (projector cone)
line(P(403, 473), P(455.6, 450.0), 1.2, "#555", z=5)
# west lounge
for (x, y) in [(333, 430), (324, 443), (318, 456)]:
    rrect(*P(x, y), 2000, 700, -55, lw=0.4)
for (x, y, a) in [(318, 470, 30), (330, 476, 10), (341, 479, -20), (318, 484, 45)]:
    rrect(*P(x, y), 750, 750, a, lw=0.35)
box(330, 486, 350, 493, lw=0.4); box(306, 465, 313, 485, lw=0.4)
T(*P(408, 382), "HIROBA Cafe", 6)

# ---- SE area (detailed per photo) ---------------------------------------
LINEA = ((388.0, 497.5), (499.0, 444.4))
poly([LINEA[0], LINEA[1], (498.5, 444.4), (498.5, 427.0), (517.0, 445.0), (517.0, FAC)],
     fc=WOOD, ec="none", z=1)
line(P(*LINEA[0]), P(*LINEA[1]), 0.6, WOODE, z=4)
line(P(498.5, 427), P(498.5, 444.4), 0.6, WOODE, z=4)
# upper tier + top block
poly([(505, 370), (517.4, 370), (517.4, 445), (507, 427), (505, 427)], fc=WOOD2, ec=WOODE, lw=0.5, z=2)
box(511, 390, 517.2, 406, fc="#c9a774", ec=WOODE, lw=0.5, z=3)
poly([(498.5, 427), (507, 427), (517, 437), (517, 445)], fc=WOOD2, ec="none", z=1, alpha=0.6)
for (sx, sy) in [(499.0, 427.0), (503.0, 427.0), (507.0, 427.0)]:
    line(P(sx, sy), P(517.0, sy + 517.0 - sx), 0.5, WOODE, z=4)
line(P(499.3, 427.8), P(507.2, 427.8), 0.9, "#333", z=6)
line(P(516.3, 437.5), P(516.3, 445.2), 0.9, "#333", z=6)
# bench + booth tables + chairs
box(498, 370, 505, 427, fc="#e9d3ae", ec="k", lw=0.5, z=3)
line(P(504.2, 370), P(504.2, 427), 0.4, WOODE, z=4)
for (y1, y2) in [(371.0, 377.3), (377.6, 383.8), (391.0, 397.3), (397.6, 403.9), (411.0, 417.0), (417.3, 423.6)]:
    box(498.7, y1, 503.6, y2, fc="#c0687a", ec="k", lw=0.25, z=4)
    box(490.0, y1, 497.2, y2, fc="white", ec="k", lw=0.5, z=5)
    rrect(*P(486.0, (y1 + y2) / 2), 430, 430, 0, lw=0.3)
# east wall / green wall / glass / planter strip
box(518.2, 352, 521.0, GY0, fc="#9e9e9e", ec="k", lw=0.6, z=3, hatch="////")
box(517.4, 391.0, 518.2, GY0, fc="#7fb069", ec=GREENE, lw=0.4, z=4, hatch="xxxx")
gx0, gy0 = P(GX, GY0); gx1, gy1 = P(GX, GY1)
ax.add_patch(Rectangle((gx0 - 0.45, gy1), 0.9, gy0 - gy1, fc=GLASS, ec=RED, lw=0.7, zorder=6))
line((gx0, gy0), (gx0, gy1), 0.25, RED, z=7)
box(516.6, GY0, 518.6, 445.0, fc=GREEN, ec=GREENE, lw=0.3, z=3, hatch="....")
# window counter + stools
box(410.0, 492.2, 497.0, 494.4, fc="#e7e7e7", ec="k", lw=0.4, z=4)
for (x, y) in [(418.5, 486.7), (428.0, 485.5), (436.1, 483.9), (447.2, 483.9), (456.7, 486.7), (463.9, 488.3),
               (471.3, 490.2), (478.4, 490.3), (485.2, 489.2), (492.6, 485.9), (500.2, 482.8), (507.2, 482.8),
               (513.1, 487.8)]:
    c = P(x, y)
    ax.add_patch(Circle(c, L(230), fc="white", ec="k", lw=0.3, zorder=5))
    ax.add_patch(Circle(c, 0.25, fc="k", ec="none", zorder=6))
# stage furniture
band((460.0, 462.4), (488.0, 450.2), 350, side=-220, fc=GREEN, ec=GREENE, lw=0.35, z=5, hatch="....")
ang2, _ = band((472.2, 477.8), (498.9, 466.1), 600, fc="#e8cfa3", ec=WOODE, lw=0.5, z=5)
for (x, y) in [(451.1, 474.1), (464.4, 468.3), (477.6, 462.3)]:
    for dx, dy in ((0, 0), (4.5, -2.0)):
        ax.add_patch(Circle(P(x + dx, y + dy), L(200), fc="white", ec="k", lw=0.3, zorder=5))
    ax.add_patch(Circle(P(x, y), 0.25, fc="k", ec="none", zorder=6))
for (x, y, a) in [(507.8, 456.1, 20), (512.2, 460.6, -10), (511.7, 466.1, 15)]:
    rrect(*P(x, y), 480, 480, a, lw=0.3)
box(514.3, 459.5, 517.0, 472.0, fc="white", ec="k", lw=0.45, z=5)
line(P(518.2, 461.0), P(518.2, 487.0), 1.4, "#888", z=6)
pj = P(486.0, 472.0)
rrect(*pj, 500, 450, ang2, fc="none", ec="k", lw=0.35, z=7, ls=(0, (2, 1)))
for tgt in ((518.2, 461.0), (518.2, 487.0)):
    line(pj, P(*tgt), 0.2, "#666", ls=(0, (3, 2)), z=6)
poly([(453, 389), (458, 389), (463, 397), (468, 409), (472, 420), (470, 426), (466, 426),
      (462, 416), (457, 405), (453, 395)], fc=GREEN, ec=GREENE, lw=0.5, z=4, hatch="xxx")
rrect(*P(466.0, 399.0), 500, 450, 0, fc="none", ec="k", lw=0.35, z=7, ls=(0, (2, 1)))
# room beyond glass
box(521.0, 360, 532.5, FAC, fc="#f4f4f4", ec="none", z=0)
line(P(532.5, 360), P(532.5, FAC), 0.5, "k")
for y in np.arange(411.0, 496.0, 4.2):
    box(521.6, y, 526.0, y + 4.0, fc="white", ec="#777", lw=0.2, z=2)
for y1 in (430.0, 487.0):
    box(531.3, y1, 533.6, y1 + 3.5, fc="#555", ec="k", lw=0.2, z=3)

# --- labels (SE) --------------------------------------------------------
T(*P(509, 485), "ステージ\n（木製床）", 4.0, color="#6b4220")
T(*P(511, 418), "上段\n（木製段床）", 3.6, color="#6b4220")
T(*P(514, 398), "最上段", 3.2, color="#6b4220")
T(*P(501, 407.5), "ベンチ", 3.4, rotation=90)
T(*P(512, 433.5), "階段3段", 3.4, rotation=-45, color="#6b4220")
T(*P(527, 452), "隣室\n「3」", 3.4, color="#555")
T(*P(445, 476.5), "ステージ端（段差）", 3.4, rotation=24, color="#6b4220")
T(*P(465, 495.3), "窓際カウンター＋スツール", 3.4, bbox=dict(fc="white", ec="none", pad=0.2))
lab((474, 456.0), P(470, 444), "低植栽", GREENE)
lab((464.4, 468.3), P(455, 482), "スツール")
lab((462, 410), P(476, 386), "大型プランター", GREENE)
lab((493.6, 394.2), P(478, 436), "テーブル＋チェア")

RL = 300
for (src, ty, s, c) in [((517.8, 400), 398, "壁面緑化", GREENE),
                        ((GX, 430), 418, "ガラス面（W 約9,000）", RED),
                        ((517.6, 436), 430, "植栽（ガラス際）", GREENE),
                        ((516.3, 441), 441, "手摺（黒）", "k"),
                        ((510, 461), 452, "チェア（白）", "k"),
                        ((515.6, 466), 463, "デスク（白）", "k"),
                        ((518.2, 474), 474, "スクリーン W≈2,700（天井吊）", "k"),
                        ((487, 470.2), 485, "ハイカウンター（木製）／PJ（天井）", "k"),
                        ]:
    leader(P(*src), (RL, P(0, ty)[1]), s, 4.0, c)

# --- dimensions ----------------------------------------------------------
pl, pr = P(xl, FAC), P(522, FAC)
dimh(pl[0], pr[0], pl[1] - 12, f"約 {r50(mm_of(522 - xl))}", 4.4, ext_from=pl[1] - 4)
dimv(pl[1], P(0, yt)[1], pl[0] - 9, f"約 {r50(mm_of(FAC - yt))}", 4.4, ext_from=pl[0] - 1, side=-1)
ex = P(521, 0)[0]; DX = ex + 15
ys = [yt, 391.0, GY0, TIER_S, 445.0, GY1]
for (a, b) in zip(ys[:-1], ys[1:]):
    dimv(P(0, b)[1], P(0, a)[1], DX, "約" + r50(mm_of(b - a)), size=3.3, ext_from=ex + 12)
dimv(P(0, GY1)[1], P(0, GY0)[1], DX + 6, "ガラス 9,000", c=RED, size=3.8)
TY = P(0, yt)[1] + 4
for a, b in zip([484.0, 490.0, 497.2, 505.0, 517.4], [490.0, 497.2, 505.0, 517.4, GX]):
    dimh(P(a, 0)[0], P(b, 0)[0], TY, r50(mm_of(b - a)), size=3.0)

# --- marks ----------------------------------------------------------------
def mark(p, txt):
    T(p[0], p[1], txt, 5.2, bbox=dict(boxstyle="circle,pad=0.2", fc="white", ec="k", lw=0.5))
pa = P(536.5, 455.0); mark((pa[0] + 2.5, pa[1]), "A")
ax.annotate("", xy=(pa[0] - 1.6, pa[1]), xytext=(pa[0] + 0.6, pa[1]),
            arrowprops=dict(arrowstyle="-|>", lw=0.5, color="k"))
by = P(0, 400.0)[1]
line((P(480.5, 0)[0], by), (P(523, 0)[0], by), 0.35, "k", ls=(0, (6, 1.5, 1, 1.5)), z=9)
for xx in (P(480.5, 0)[0], P(523, 0)[0]):
    mark((xx, by + 2.6), "B")
# detail zone outline
poly([(436, 384), (536, 384), (536, 504), (436, 504)], ec=RED, lw=0.4, ls=(0, (4, 2)), z=10)
T(*P(445, 381.5), "詳細照合範囲", 3.4, color=RED)

ax.text(22, H - 16, "HIROBA 平面図（完全版）　S=1/100", fontsize=10, ha="left", va="center")
ax.text(22, H - 22, "元図面トレース＋右下部分は現況写真と照合して詳細化", fontsize=5.2, ha="left", va="center", color="#444")
na = (395, 250)
ax.add_patch(Polygon([(na[0], na[1] + 8), (na[0] - 3, na[1] - 4), (na[0], na[1] - 1.5)], fc="k", ec="k", lw=0.3))
ax.add_patch(Polygon([(na[0], na[1] + 8), (na[0] + 3, na[1] - 4), (na[0], na[1] - 1.5)], fc="white", ec="k", lw=0.3))
T(na[0], na[1] + 11, "図上北", 4)
scale_bar(352, 60)

# legend
lg = [(WOOD, WOODE, None, "ステージ（木製床）"), (WOOD2, WOODE, None, "木製段床"),
      ("#c0687a", "k", None, "ベンチクッション"), (GREEN, GREENE, "....", "植栽"),
      ("#7fb069", GREENE, "xxxx", "壁面緑化"), (GLASS, RED, None, "ガラス面"),
      ("#a8a8a8", "k", "\\\\", "柱")]
ax.text(352, 205, "凡例", fontsize=5.5, ha="left")
for i, (fc, ec, h, s) in enumerate(lg):
    yy = 198 - i * 6
    ax.add_patch(Rectangle((352, yy - 1.5), 6, 3, fc=fc, ec=ec, lw=0.4, hatch=h))
    ax.text(360, yy, s, fontsize=4.6, va="center")
ax.add_patch(Rectangle((352, 198 - 7 * 6 - 1.5), 6, 3, fc="none", ec="k", lw=0.35, ls=(0, (2, 1))))
ax.text(360, 198 - 7 * 6, "天井設置（PJ）", fontsize=4.6, va="center")
line((345, 8), (345, H - 8), 0.4)
title_block("HIROBA 平面図（完全版）", "1/2")
pdf.savefig(fig1); fig1.savefig(OUT + "_1.png", dpi=150); plt.close(fig1)

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
dimh(gx, x1, ytop, "ガラス W 約9,000", c=RED, size=4.0)
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
    "1. 元図面（HIROBA図）をトレース。縮尺はガラス面 87px＝9,000mm（≒103mm/px）で換算。",
    "　 テーブル700・椅子450・段板≈290 と整合を確認済み。",
    "2. 右下部分は現況写真（ブース北西→南東方向）と照合して詳細化：",
    "　 壁面緑化／赤クッションのベンチ＋木製段床／黒手摺付き階段3段／",
    "　 木製ステージ（白チェア・白デスク）／天井吊スクリーン・PJ／木製ハイカウンター",
    "3. ガラス W9,000 は緑化端〜南端（スクリーン背面を含む）。",
    "4. 高さ（≈表記）は写真の透視からの推定値。H2,500 はステージ床からの値。",
    "5. 平面寸法の誤差：右下 ±100〜200mm、その他 ±300mm 程度。実測で要確認。",
    "6. 右下以外の家具配置は元図面の概略トレース。",
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
pdf.savefig(fig2); fig2.savefig(OUT + "_2.png", dpi=150); plt.close(fig2)
pdf.close()
