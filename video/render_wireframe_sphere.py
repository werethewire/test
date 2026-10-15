"""Render a 10s weightless video from inside a tumbling geodesic wireframe sphere with drifting particles.

Usage: python3 render_wireframe_sphere.py [output.mp4]
Requires numpy, Pillow and ffmpeg on PATH.
"""
import subprocess
import sys

import numpy as np
from PIL import Image

W, H = 1920, 1080
FPS = 30
DURATION = 10
FRAMES = FPS * DURATION
OUT = sys.argv[1] if len(sys.argv) > 1 else "wireframe_sphere_particles.mp4"

# Camera floats inside the unit sphere, slightly off-centre, looking down -z with a wide lens
CAM = np.array([0.0, 0.0, 0.32])
FOCAL = (W / 2) / np.tan(np.radians(52))
NEAR = 0.04
GEO_FREQ = 4  # geodesic subdivision: uniform triangles, no poles -> no visual centre
ARC_SAMPLES = 260
N_PARTICLES = 7000
# Two rotations on unrelated oblique axes -> the effective spin axis keeps wandering (tumbling)
TUMBLE = [(np.array([0.8, 0.5, 0.3]), np.radians(34)), (np.array([-0.3, 0.6, -0.75]), np.radians(22))]

rng = np.random.default_rng(11)
TAU = 2 * np.pi


def build_wireframe():
    """Geodesic sphere edges as great-circle arcs: (edges, ARC_SAMPLES, 3)."""
    g = (1 + 5 ** 0.5) / 2
    v = np.array([[-1, g, 0], [1, g, 0], [-1, -g, 0], [1, -g, 0], [0, -1, g], [0, 1, g],
                  [0, -1, -g], [0, 1, -g], [g, 0, -1], [g, 0, 1], [-g, 0, -1], [-g, 0, 1]], float)
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
             (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    n = GEO_FREQ
    edges = set()
    for a, b, c in faces:
        A, B, C = v[a], v[b], v[c]
        grid = {}
        for i in range(n + 1):
            for j in range(n + 1 - i):
                p = A + (B - A) * i / n + (C - A) * j / n
                grid[i, j] = tuple(np.round(p / np.linalg.norm(p), 9))
        for i in range(n + 1):
            for j in range(n + 1 - i):
                for di, dj in ((1, 0), (0, 1), (1, -1)):
                    k = (i + di, j + dj)
                    if k in grid:
                        edges.add(tuple(sorted((grid[i, j], grid[k]))))
    p0 = np.array([e[0] for e in edges])
    p1 = np.array([e[1] for e in edges])
    om = np.arccos(np.clip((p0 * p1).sum(1), -1, 1))[:, None, None]
    tt = np.linspace(0, 1, ARC_SAMPLES)[None, :, None]
    return (np.sin((1 - tt) * om) * p0[:, None] + np.sin(tt * om) * p1[:, None]) / np.sin(om)


WIRE = build_wireframe()

# Particles fill the sphere volume and drift on slow periodic paths
u = rng.normal(size=(N_PARTICLES, 3))
u /= np.linalg.norm(u, axis=1, keepdims=True)
P_BASE = u * (rng.uniform(0, 1, N_PARTICLES) ** (1 / 3) * 0.92)[:, None]
# Two slow, incommensurate sway components per axis give an organic, buoyant float
P_AMP = rng.uniform(0.03, 0.16, (N_PARTICLES, 3))
P_FREQ = rng.uniform(0.5, 1.0, (N_PARTICLES, 3)) * TAU / DURATION
P_AMP2 = rng.uniform(0.01, 0.06, (N_PARTICLES, 3))
P_FREQ2 = rng.uniform(1.0, 1.6, (N_PARTICLES, 3)) * TAU / DURATION
P_PHASE2 = rng.uniform(0, TAU, (N_PARTICLES, 3))
P_PHASE = rng.uniform(0, TAU, (N_PARTICLES, 3))
P_VEL = rng.normal(size=(N_PARTICLES, 3)) * 0.011  # slow straight drift in every direction
P_BRIGHT = rng.uniform(0.3, 1.0, N_PARTICLES) ** 2
P_TWINKLE = rng.uniform(0, TAU, N_PARTICLES)
P_HUE = rng.uniform(0, 1, N_PARTICLES)

WIRE_COL = np.array([0.62, 0.80, 1.00])  # blue-tinted white
PART_COL_A = np.array([1.00, 0.92, 0.25])  # bright yellow
PART_COL_B = np.array([1.00, 0.70, 0.06])  # amber yellow

# Background: near-black with a faint warm core for high contrast
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
d = np.sqrt(((xx - W / 2) / W) ** 2 + ((yy - H / 2) / W) ** 2)
BG = np.array([0.008, 0.003, 0.0006], np.float32)  # flat: no glowing core pulling the eye to the middle
VIGNETTE = np.clip(1.3 - d * 0.9, 0, 1)[..., None]


def rot_axis(axis, a):
    """Rodrigues rotation matrix."""
    x, y, z = axis
    c, s, C = np.cos(a), np.sin(a), 1 - np.cos(a)
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def camera(t):
    """Floating camera: small drifting position plus slow yaw/pitch/roll sway."""
    u = TAU * t / DURATION
    pos = CAM + np.array([0.07 * np.sin(u + 0.4), 0.05 * np.sin(u * 0.5 + 1.3), 0.04 * np.sin(u + 2.1)])
    rot = rot_axis(np.array([0, 0, 1.0]), np.radians(12) * np.sin(u * 0.5 + 0.3)) \
        @ rot_axis(np.array([0, 1.0, 0]), np.radians(5) * np.sin(u + 1.0)) \
        @ rot_axis(np.array([1.0, 0, 0]), np.radians(4) * np.sin(u * 0.5 + 2.2))
    return pos, rot


def project(p, cam):
    pos, rot = cam
    rel = (p - pos) @ rot
    depth = -rel[..., 2]
    safe = np.maximum(depth, NEAR)
    x = W / 2 + FOCAL * rel[..., 0] / safe
    y = H / 2 - FOCAL * rel[..., 1] / safe
    return x, y, depth


def splat(buf, x, y, val):
    """Bilinear additive splat of RGB values (N,3) into buf (H*W,3)."""
    m = (x >= 0) & (x < W - 1) & (y >= 0) & (y < H - 1)
    x, y, val = x[m], y[m], val[m]
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        idx = (y0 + dy) * W + (x0 + dx)
        wv = val * w
        for c in range(3):
            buf[:, c] += np.bincount(idx, wv[:, c], minlength=W * H)


def _gauss1d(a, sigma, axis):
    r = int(3 * sigma) + 1
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r, r)
    a = np.pad(a, pad, mode="edge")
    n = a.shape[axis] - 2 * r
    out = np.zeros_like(a.take(range(n), axis=axis))
    for i, w in enumerate(k):
        out += w * a.take(range(i, i + n), axis=axis)
    return out


def blur(img, sigma, scale):
    """Gaussian blur at reduced resolution (cheap glow), returned at full size."""
    small = img.reshape(H // scale, scale, W // scale, scale, 3).mean((1, 3))
    small = _gauss1d(_gauss1d(small, sigma, 0), sigma, 1)
    out = []
    for c in range(3):
        im = Image.fromarray(small[..., c].astype(np.float32), "F").resize((W, H), Image.BILINEAR)
        out.append(np.asarray(im))
    return np.stack(out, -1)


def render(f):
    t = f / FPS
    wire = np.zeros((H * W, 3), np.float32)
    dots = np.zeros((H * W, 3), np.float32)
    soft = np.zeros((H * W, 3), np.float32)

    cam = camera(t)

    # Wireframe: tumbling rotation; intensity follows on-screen segment length so lines stay even
    R = np.eye(3)
    for axis, total in TUMBLE:
        R = rot_axis(axis / np.linalg.norm(axis), total * t / DURATION + 0.5) @ R
    p = WIRE @ R.T
    x, y, depth = project(p, cam)
    ds = np.hypot(np.diff(x, axis=1), np.diff(y, axis=1))
    ds = np.concatenate([ds, ds[:, -1:]], 1)
    ok = (depth > NEAR) & (ds < 40)
    fade = np.clip(1.6 / (0.25 + depth), 0, 3.0)  # nearer wires glow harder
    inten = (0.9 * ds * fade)[ok]
    splat(wire, x[ok], y[ok], inten[:, None] * WIRE_COL[None])

    # Particles
    q = (P_BASE + P_VEL * (t - DURATION / 2) + P_AMP * np.sin(P_FREQ * t + P_PHASE)
         + P_AMP2 * np.sin(P_FREQ2 * t + P_PHASE2))
    x, y, depth = project(q, cam)
    ok = depth > NEAR
    twinkle = 0.65 + 0.35 * np.sin(TAU * 2 * t / DURATION + P_TWINKLE)
    col = PART_COL_A[None] * (1 - P_HUE[:, None]) + PART_COL_B[None] * P_HUE[:, None]
    v = (P_BRIGHT * twinkle * np.clip(0.9 / depth, 0.4, 6.0))[:, None] * col
    near = depth < 0.45  # particles close to the lens become soft bokeh
    splat(dots, x[ok & ~near], y[ok & ~near], v[ok & ~near] * 16.0)
    splat(soft, x[ok & near], y[ok & near], v[ok & near] * 75.0)

    wire = wire.reshape(H, W, 3)
    # Particles are drawn as small discs (blurred at full res) and large bokeh, twice the old size
    part = blur(dots.reshape(H, W, 3), 1.4, 1) + blur(soft.reshape(H, W, 3), 6.0, 2)
    sharp = wire + part
    img = sharp + blur(sharp, 2, 2) * 3.2 + blur(sharp, 5, 4) * 3.0 + blur(sharp, 20, 4) * 1.0
    img = BG + img
    img = 1 - np.exp(-img * 1.8)  # tonemap: hot cores roll off toward white-gold
    img = img * VIGNETTE
    img = np.clip(img, 0, 1) ** 1.35  # deepen shadows for contrast
    img = np.clip(img * 255 + rng.uniform(-0.8, 0.8, (H, W, 1)), 0, 255)  # dither
    return img.astype(np.uint8)


def main():
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "slow", "-crf", "18",
           "-pix_fmt", "yuv420p", "-movflags", "+faststart", OUT]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for f in range(FRAMES):
        proc.stdin.write(render(f).tobytes())
        if f % 30 == 0:
            print(f"frame {f}/{FRAMES}", flush=True)
    proc.stdin.close()
    proc.wait()


if __name__ == "__main__":
    main()
