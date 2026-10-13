import math, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager as fm
from matplotlib.patches import Rectangle, Polygon, Circle, FancyBboxPatch
from matplotlib.transforms import Affine2D

fm.fontManager.addfont("/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf")
plt.rcParams["font.family"] = "IPAGothic"

# ---- scale -------------------------------------------------------------
# Source floor image: glass wall (red line) = 87 px  <=>  9,000 mm
S = 9000 / 87.0            # mm per source px
X0, Y0 = 290.0, 498.0      # source px of plan origin (inner-left / floor-line)
K = 1 / 100.0              # drawing scale 1:100  -> paper mm = real mm * K
OX, OY = 22.0, 40.0        # paper offset of plan origin (mm)

def P(x, y):
    """source image px -> paper mm"""
    return OX + (x - X0) * S * K, OY + (Y0 - y) * S * K

def L(mm):
    return mm * K

W, H = 420, 297  # A3 landscape
fig = plt.figure(figsize=(W / 25.4, H / 25.4))
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, W); ax.set_ylim(0, H); ax.set_aspect("equal"); ax.axis("off")

LW_WALL, LW_MID, LW_THIN = 1.4, 0.6, 0.3

def line(p, q, lw=LW_THIN, c="k", ls="-", z=2):
    ax.plot([p[0], q[0]], [p[1], q[1]], lw=lw, color=c, ls=ls, zorder=z, solid_capstyle="butt")

def rect_src(x1, y1, x2, y2, fc="none", ec="k", lw=LW_THIN, hatch=None, z=2):
    a = P(x1, y2); b = P(x2, y1)
    ax.add_patch(Rectangle(a, b[0] - a[0], b[1] - a[1], fc=fc, ec=ec, lw=lw, hatch=hatch, zorder=z))

def rot_rect(cx, cy, w, d, ang, fc="white", ec="k", lw=LW_THIN, z=3):
    """centre in paper mm, w/d real mm, angle deg"""
    r = Rectangle((-L(w) / 2, -L(d) / 2), L(w), L(d), fc=fc, ec=ec, lw=lw, zorder=z)
    r.set_transform(Affine2D().rotate_deg(ang).translate(cx, cy) + ax.transData)
    ax.add_patch(r)

def chair(cx, cy, ang):
    rot_rect(cx, cy, 450, 450, ang, lw=0.2, z=3)

def table4(x, y, ang=0):
    cx, cy = P(x, y)
    rot_rect(cx, cy, 750, 750, ang, lw=0.35, z=4)
    for k in range(4):
        a = math.radians(ang + 90 * k)
        chair(cx + L(620) * math.cos(a), cy + L(620) * math.sin(a), ang + 90 * k)

def text(x, y, s, size=5, **kw):
    ax.text(x, y, s, fontsize=size, ha=kw.pop("ha", "center"), va=kw.pop("va", "center"), zorder=10, **kw)

# ---- structure ---------------------------------------------------------
# exterior / partition walls
xl, xr, yt, yb = 291, 519, 288, 498
line(P(xl, yt), P(xl, yb), LW_WALL)                 # west wall
line(P(xr + 3, yt), P(xr + 3, yb), LW_WALL)         # east wall (to adjacent zone)
line(P(xl - 2, yb), P(xr + 12, yb), 2.2)            # south facade
# west inner partition + storage
line(P(304, 316), P(304, 450), LW_MID)
for y in range(318, 450, 6):
    rect_src(293, y, 303, y + 6, lw=0.2)
for y in range(452, 496, 5):
    rect_src(293, y, 299, y + 5, lw=0.2)
# east storage (inside, north of glass)
line(P(512, 360), P(512, 409), LW_MID)
for y in range(362, 409, 6):
    rect_src(512.5, y, 518, y + 6, lw=0.2)
# east lockers behind wall
for y in range(310, 496, 6):
    rect_src(522, y, 528, y + 6, lw=0.2)

# GLASS WALL (W9000 x H2500) --------------------------------------------
gy1, gy2 = 410, 497
g1, g2 = P(518.5, gy1), P(518.5, gy2)
ax.add_patch(Rectangle((g1[0] - 0.35, g2[1]), 0.7, g1[1] - g2[1], fc="#cfe8ff", ec="#d0021b", lw=0.8, zorder=6))
n = 6
for i in range(n + 1):
    yy = g2[1] + (g1[1] - g2[1]) * i / n
    line((g1[0] - 0.9, yy), (g1[0] + 0.9, yy), 0.5, "#d0021b", z=7)

# back-of-house rooms (north)
for (x1, x2) in [(287, 358), (375, 446), (466, 535)]:
    rect_src(x1, 289, x2, 307, fc="#bdbdbd", lw=LW_MID, z=1)
    for xx in range(int(x1) + 10, int(x2), 18):
        rect_src(xx, 292, xx + 8, 296, fc="white", lw=0.2, z=2)
# columns
for (x1, y1) in [(317, 490), (378, 490), (438, 490), (498, 490), (377, 307), (440, 307), (305, 308), (335, 310)]:
    rect_src(x1, y1, x1 + 7, y1 + 8, fc="#7a7a7a", ec="k", lw=0.4, z=5)

# cafe counter
rect_src(375, 308, 446, 337, lw=LW_MID)
rect_src(379, 327, 446, 336, lw=0.3)
rect_src(375, 315, 421, 324, lw=0.3)
cx, cy = P(410, 321); text(cx, cy, "カフェカウンター", 4.5)
# kitchen / storage (NE)
rect_src(468, 308, 518, 348, lw=LW_MID)
for (a, b, c, d) in [(470, 333, 488, 345), (490, 333, 508, 345), (480, 318, 494, 327), (496, 318, 510, 327), (470, 310, 478, 330)]:
    rect_src(a, b, c, d, lw=0.25)
cx, cy = P(493, 340); text(cx, cy, "パントリー", 4)

# NW library
lib = [P(322, 327), P(348, 333), P(345, 346), P(327, 343)]
ax.add_patch(Polygon(lib, closed=True, fc="none", ec="k", lw=0.8, ls=(0, (3, 1)), zorder=3))
cx, cy = P(330, 322); text(cx, cy, "Library", 4.5)
line(P(304, 316), P(330, 316), LW_MID); line(P(310, 316), P(310, 345), LW_MID)

# large communal table (rotated)
cx, cy = P(354, 367); rot_rect(cx, cy, 3600, 950, 22, lw=0.5)
for k in range(-3, 4):
    for s in (-1, 1):
        a = math.radians(22)
        dx, dy = k * L(480) * math.cos(a), k * L(480) * math.sin(a)
        nx, ny = -math.sin(a) * L(780) * s, math.cos(a) * L(780) * s
        chair(cx + dx + nx, cy + dy + ny, 22)

# booth seating (north centre)
for i in range(4):
    x = 388 + i * 12.5
    rect_src(x, 357, x + 11, 373, lw=0.4)
    rect_src(x + 2, 361, x + 9, 369, lw=0.25)
rect_src(387, 357, 438, 359.5, fc="k", lw=0.2)

# cafe tables (4-seat)
tbls = [(362, 398), (382, 388), (415, 400), (435, 389), (370, 418), (393, 409), (420, 419), (445, 408),
        (380, 440), (403, 430), (430, 438), (452, 428), (390, 458), (413, 453)]
for (x, y) in tbls:
    table4(x, y, 15)

# high counters (diagonal)
for (a, b) in [((339, 418), (362, 460)), ((453, 390), (475, 428))]:
    p, q = P(*a), P(*b)
    ang = math.degrees(math.atan2(q[1] - p[1], q[0] - p[0]))
    ln = math.hypot(q[0] - p[0], q[1] - p[1]) / K
    rot_rect((p[0] + q[0]) / 2, (p[1] + q[1]) / 2, ln, 600, ang, fc="#e6e6e6", lw=0.5)
    for t in range(1, int(ln // 650)):
        u = t * 650 / ln
        bx, by = p[0] + (q[0] - p[0]) * u, p[1] + (q[1] - p[1]) * u
        nx, ny = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
        for s in (-1, 1):
            ax.add_patch(Circle((bx + nx * L(550) * s, by + ny * L(550) * s), L(180), fc="white", ec="k", lw=0.2, zorder=3))

# south counter + bench row toward glass
for (a, b, d) in [((403, 473), (455, 452), 500), ((388, 497), (490, 450), 300)]:
    p, q = P(*a), P(*b)
    ang = math.degrees(math.atan2(q[1] - p[1], q[0] - p[0]))
    ln = math.hypot(q[0] - p[0], q[1] - p[1]) / K
    rot_rect((p[0] + q[0]) / 2, (p[1] + q[1]) / 2, ln, d, ang, fc="#e6e6e6", lw=0.4)

# stepped wooden seating in front of glass (from photo)
p, q = P(506.5, gy1 + 3), P(517.5, gy2 - 2)
ax.add_patch(Rectangle((p[0], q[1]), q[0] - p[0], p[1] - q[1], fc="#f3e3c3", ec="#8a5a2b", lw=0.5, zorder=2))
for f in (0.5,):
    xx = p[0] + (q[0] - p[0]) * f
    line((xx, q[1]), (xx, p[1]), 0.3, "#8a5a2b", z=3)
text((p[0] + q[0]) / 2, (p[1] + q[1]) / 2, "段床ベンチ\n(木製)", 4, rotation=90, color="#6b4220")
# planter (from photo)
p = P(497, 485); q = P(502.5, 452)
ax.add_patch(Rectangle(p, q[0] - p[0], q[1] - p[1], fc="#d9ead3", ec="#38761d", lw=0.4, zorder=2, hatch="...."))
# green wall at north end of glass (from photo)
gw = P(512, 409)
ax.add_patch(Rectangle((gw[0], gw[1] - 0.6), L(600), 1.2, fc="#6aa84f", ec="#274e13", lw=0.4, zorder=6))

# east meeting booths
line(P(467, 360), P(512, 360), LW_MID)
line(P(504, 363), P(504, 405), LW_MID)
for (x, y) in [(485, 372), (485, 392), (485, 409)]:
    rect_src(x, y, x + 16, y + 10, lw=0.35)
    rect_src(x + 3, y + 2.5, x + 13, y + 7.5, lw=0.25)
cx, cy = P(494, 365); text(cx, cy, "ブース", 4)

# west lounge
for (x, y) in [(333, 430), (324, 443), (318, 456)]:
    cx, cy = P(x, y); rot_rect(cx, cy, 2000, 700, -55, lw=0.4)
for (x, y, a) in [(318, 470, 30), (330, 476, 10), (341, 479, -20), (318, 484, 45)]:
    cx, cy = P(x, y); rot_rect(cx, cy, 750, 750, a, lw=0.35)
rect_src(330, 486, 350, 493, lw=0.4)
rect_src(306, 465, 313, 485, lw=0.4)
# south arc of seats
for i in range(14):
    t = i / 13
    x = 418 + t * 82
    y = 488 + 4 * math.sin(math.pi * t) - 2
    cx, cy = P(x, y); ax.add_patch(Circle((cx, cy), L(250), fc="white", ec="k", lw=0.25, zorder=3))

# room label
cx, cy = P(408, 382); text(cx, cy, "HIROBA Cafe", 6, weight="bold")

# ---- dimensions --------------------------------------------------------
def dim(p, q, off, label, vertical=False, c="k", size=4.5):
    if vertical:
        x = p[0] + off
        line((p[0], p[1]), (x + 1 * (1 if off > 0 else -1), p[1]), 0.2, c)
        line((q[0], q[1]), (x + 1 * (1 if off > 0 else -1), q[1]), 0.2, c)
        line((x, p[1]), (x, q[1]), 0.3, c)
        for yy in (p[1], q[1]):
            line((x - 0.8, yy - 0.8), (x + 0.8, yy + 0.8), 0.4, c)
        text(x + (2.2 if off > 0 else -2.2), (p[1] + q[1]) / 2, label, size, rotation=90, color=c)
    else:
        y = p[1] + off
        line((p[0], p[1]), (p[0], y + (1 if off > 0 else -1)), 0.2, c)
        line((q[0], q[1]), (q[0], y + (1 if off > 0 else -1)), 0.2, c)
        line((p[0], y), (q[0], y), 0.3, c)
        for xx in (p[0], q[0]):
            line((xx - 0.8, y - 0.8), (xx + 0.8, y + 0.8), 0.4, c)
        text((p[0] + q[0]) / 2, y + (1.8 if off > 0 else -1.8), label, size, color=c)

def mm(a): return f"{round(a / 50) * 50:,.0f}"

pl, pr = P(xl, yb), P(xr + 3, yb)
dim(pl, pr, -9, f"約 {mm((xr + 3 - xl) * S)}")
pt = P(xl, yt)
dim((pl[0], pt[1]), pl, -9, f"約 {mm((yb - yt) * S)}", vertical=True)
dim(g1, g2, 18, "ガラス面 W 約9,000", vertical=True, c="#d0021b", size=5)
dim(P(xr + 3, yt), P(xr + 3, gy1), 18, f"約 {mm((gy1 - yt) * S)}", vertical=True)

# north arrow
na = (272, 262)
ax.add_patch(Polygon([(na[0], na[1] + 8), (na[0] - 3, na[1] - 4), (na[0], na[1] - 1.5)], fc="k", ec="k", lw=0.4))
ax.add_patch(Polygon([(na[0], na[1] + 8), (na[0] + 3, na[1] - 4), (na[0], na[1] - 1.5)], fc="white", ec="k", lw=0.4))
text(na[0], na[1] + 11, "N(図上)", 5)

# section mark for elevation
e1 = P(530, gy1); e2 = P(530, gy2)
text(e1[0] + 4, (e1[1] + e2[1]) / 2, "A", 6, weight="bold",
     bbox=dict(boxstyle="circle,pad=0.25", fc="white", ec="k", lw=0.5))
ax.annotate("", xy=(e1[0] - 0.5, (e1[1] + e2[1]) / 2), xytext=(e1[0] + 2, (e1[1] + e2[1]) / 2),
            arrowprops=dict(arrowstyle="-|>", lw=0.5, color="k"))

text(OX, 32 - 9 - 3, "", 1)
ax.text(OX, H - 14, "HIROBA 平面図　S=1/100", fontsize=10, weight="bold", ha="left", va="center")

# ---- elevation A (glass wall) -------------------------------------------
EX, EY = 292, 196
GW, GH = L(9000), L(2500)
STEP = L(450)       # raised wooden floor at glass (photo)
CEIL = L(2900)      # ceiling (assumed)
ax.text(EX, EY + 52, "A 展開図（ガラス面）　S=1/100", fontsize=10, weight="bold", ha="left", va="center")
# floor line, ceiling line
line((EX - 8, EY), (EX + GW + 10, EY), 1.0)
line((EX - 8, EY + CEIL), (EX + GW + 10, EY + CEIL), 0.5, ls=(0, (4, 2)))
text(EX + GW + 12, EY + CEIL, "CH(推定)\n約2,900", 4, ha="left")
# raised wooden platform + bench
ax.add_patch(Rectangle((EX, EY), GW, STEP, fc="#f3e3c3", ec="#8a5a2b", lw=0.5, zorder=2))
ax.add_patch(Rectangle((EX - 6, EY), 6, L(420), fc="#f3e3c3", ec="#8a5a2b", lw=0.4, zorder=2))
text(EX + GW / 2, EY + STEP / 2, "木製段床・ベンチ", 4.5, color="#6b4220")
# glass
gy = EY + STEP
ax.add_patch(Rectangle((EX, gy), GW, GH, fc="#e3f1ff", ec="k", lw=0.7, zorder=2))
for i in range(1, 6):
    xx = EX + GW * i / 6
    line((xx, gy), (xx, gy + GH), 0.35, z=3)
for i in range(6):
    x0 = EX + GW * i / 6
    line((x0 + 2, gy + GH * 0.75), (x0 + 5, gy + GH * 0.9), 0.2, "#7aa7d6", z=3)
# green wall (left) and wall (right)
ax.add_patch(Rectangle((EX - 6, gy), 6, CEIL - STEP, fc="#93c47d", ec="#274e13", lw=0.4, hatch="xxx", zorder=2))
text(EX - 3, gy + (CEIL - STEP) / 2, "壁面緑化", 4, rotation=90)
ax.add_patch(Rectangle((EX + GW, EY), 4, CEIL, fc="#eeeeee", ec="k", lw=0.4, zorder=2))
# soffit above glass
ax.add_patch(Rectangle((EX, gy + GH), GW, CEIL - STEP - GH, fc="white", ec="k", lw=0.3, zorder=2))
# dims
dim((EX, gy), (EX + GW, gy), -9 - STEP, "W 約9,000", c="#d0021b", size=5)
dim((EX + GW + 4, gy), (EX + GW + 4, gy + GH), 6, "H 約2,500", vertical=True, c="#d0021b", size=5)
dim((EX + GW + 4, EY), (EX + GW + 4, gy), 6, "約450", vertical=True, size=4)

# ---- notes ------------------------------------------------------------
notes = [
    "【作図条件】",
    "1. 縮尺はガラス面幅 約9,000mm（写真・指示値）を基準に元図面から換算。",
    "　 元図面の解像度上、各寸法の誤差は ±300mm 程度を想定。",
    "2. 家具・什器位置は元図面のトレース（概略）。実測で要確認。",
    "3. ガラス面高さ 約2,500mm は木製段床上端からの寸法（写真の矢印位置）。",
    "4. 段床高さ・天井高・マリオン割付（6分割）・壁面緑化位置は写真から推定。",
    "5. A3 横で出力時に S=1/100。",
]
for i, s in enumerate(notes):
    ax.text(292, 120 - i * 6.2, s, fontsize=5.6 if i else 6.5, ha="left", va="top",
            weight="bold" if i == 0 else "normal")

# scale bar
sb_x, sb_y = 292, 62
for i in range(5):
    ax.add_patch(Rectangle((sb_x + i * 10, sb_y), 10, 1.6, fc="k" if i % 2 == 0 else "white", ec="k", lw=0.3))
    text(sb_x + i * 10, sb_y - 2.5, f"{i}", 4)
text(sb_x + 50, sb_y - 2.5, "5m", 4)
ax.text(sb_x, sb_y + 4.5, "SCALE 1/100 (A3)", fontsize=5, ha="left")

# title block
tb = (292, 14, 116, 34)
ax.add_patch(Rectangle(tb[:2], tb[2], tb[3], fc="none", ec="k", lw=0.8))
for yy in (14 + 11, 14 + 22):
    line((tb[0], yy), (tb[0] + tb[2], yy), 0.4)
line((tb[0] + 28, 14), (tb[0] + 28, 48), 0.4)
for (lbl, val, yy) in [("図面名", "HIROBA 平面図・A展開図", 42.5), ("縮尺", "1/100（A3）", 31.5), ("日付", "2026.10.13", 19.5)]:
    ax.text(tb[0] + 2, yy, lbl, fontsize=5.5, va="center")
    ax.text(tb[0] + 31, yy, val, fontsize=6.5, va="center")

# frame
ax.add_patch(Rectangle((8, 8), W - 16, H - 16, fc="none", ec="k", lw=1.0))
line((286, 8), (286, H - 8), 0.5)

out = sys.argv[1]
fig.savefig(out + ".pdf")
fig.savefig(out + ".png", dpi=150)
