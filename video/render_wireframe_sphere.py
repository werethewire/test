"""Render a 10s video from inside a slowly, diagonally rotating wireframe sphere with drifting particles.

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

# Camera sits inside the unit sphere, slightly off-centre, looking down -z with a wide lens
CAM = np.array([0.0, 0.0, 0.35])
FOCAL = (W / 2) / np.tan(np.radians(52))
NEAR = 0.04
LAT_LINES = 17
MERIDIANS = 18  # full great circles through the poles -> 36 meridians
SEG = 9000
N_PARTICLES = 7000
ROT_AXIS = np.array([1.0, 0.85, 0.35]) / np.linalg.norm([1.0, 0.85, 0.35])
ROT_TOTAL = np.radians(40)  # wireframe rotation over the whole clip

rng = np.random.default_rng(11)
TAU = 2 * np.pi


def build_wireframe():
    """Return (lines, SEG, 3): every line is a closed circle sampled SEG times."""
    th = np.linspace(0, TAU, SEG, endpoint=False)
    lines = []
    for i in range(1, LAT_LINES + 1):
        phi = -np.pi / 2 + np.pi * i / (LAT_LINES + 1)
        r, y = np.cos(phi), np.sin(phi)
        lines.append(np.stack([r * np.cos(th), np.full_like(th, y), r * np.sin(th)], 1))
    for j in range(MERIDIANS):
        a = np.pi * j / MERIDIANS
        lines.append(np.stack([np.cos(th) * np.cos(a), np.sin(th), np.cos(th) * np.sin(a)], 1))
    return np.stack(lines)


WIRE = build_wireframe()

# Particles fill the sphere volume and drift on slow periodic paths
u = rng.normal(size=(N_PARTICLES, 3))
u /= np.linalg.norm(u, axis=1, keepdims=True)
P_BASE = u * (rng.uniform(0, 1, N_PARTICLES) ** (1 / 3) * 0.92)[:, None]
P_AMP = rng.uniform(0.03, 0.16, (N_PARTICLES, 3))
P_FREQ = rng.integers(1, 3, (N_PARTICLES, 3)) * TAU / DURATION
P_PHASE = rng.uniform(0, TAU, (N_PARTICLES, 3))
P_ORBIT = rng.choice([-1, 1], N_PARTICLES) * rng.uniform(0.3, 1.0, N_PARTICLES) * np.radians(30) / DURATION
P_BRIGHT = rng.uniform(0.3, 1.0, N_PARTICLES) ** 2
P_TWINKLE = rng.uniform(0, TAU, N_PARTICLES)
P_HUE = rng.uniform(0, 1, N_PARTICLES)

WIRE_COL = np.array([1.00, 0.55, 0.10])
PART_COL_A = np.array([1.00, 0.82, 0.35])  # golden yellow
PART_COL_B = np.array([1.00, 0.38, 0.05])  # deep orange

# Background: near-black with a faint warm core for high contrast
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
d = np.sqrt(((xx - W / 2) / W) ** 2 + ((yy - H / 2) / W) ** 2)
BG = np.stack([0.012 * np.exp(-d * 3), 0.005 * np.exp(-d * 3), 0.001 * np.exp(-d * 3)], -1)
VIGNETTE = np.clip(1.25 - d * 1.2, 0, 1)[..., None] ** 1.5


def rot_axis(axis, a):
    """Rodrigues rotation matrix."""
    x, y, z = axis
    c, s, C = np.cos(a), np.sin(a), 1 - np.cos(a)
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def project(p):
    rel = p - CAM
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
    sharp = np.zeros((H * W, 3), np.float32)
    soft = np.zeros((H * W, 3), np.float32)

    # Wireframe: diagonal-axis rotation; intensity follows on-screen segment length so lines stay even
    R = rot_axis(ROT_AXIS, ROT_TOTAL * t / DURATION + 0.6)
    p = WIRE @ R.T
    x, y, depth = project(p)
    ds = np.hypot(np.roll(x, -1, 1) - x, np.roll(y, -1, 1) - y)
    ok = (depth > NEAR) & (np.roll(depth, -1, 1) > NEAR) & (ds < 40)
    fade = np.clip(1.6 / (0.25 + depth), 0, 3.0)  # nearer wires glow harder
    inten = (1.0 * ds * fade)[ok]
    splat(sharp, x[ok], y[ok], inten[:, None] * WIRE_COL[None])

    # Particles
    q = P_BASE + P_AMP * np.sin(P_FREQ * t + P_PHASE)
    ang = P_ORBIT * t
    c, s = np.cos(ang), np.sin(ang)
    q = np.stack([c * q[:, 0] + s * q[:, 2], q[:, 1], -s * q[:, 0] + c * q[:, 2]], 1)
    x, y, depth = project(q)
    ok = depth > NEAR
    twinkle = 0.65 + 0.35 * np.sin(TAU * 2 * t / DURATION + P_TWINKLE)
    col = PART_COL_A[None] * (1 - P_HUE[:, None]) + PART_COL_B[None] * P_HUE[:, None]
    v = (P_BRIGHT * twinkle * np.clip(0.9 / depth, 0.4, 6.0))[:, None] * col
    near = depth < 0.45  # particles close to the lens become soft bokeh
    splat(sharp, x[ok & ~near], y[ok & ~near], v[ok & ~near] * 8.0)
    splat(soft, x[ok & near], y[ok & near], v[ok & near] * 70.0)

    sharp = sharp.reshape(H, W, 3)
    soft = soft.reshape(H, W, 3)
    img = (sharp + blur(soft, 3.0, 2)
           + blur(sharp, 2, 2) * 3.0 + blur(sharp, 6, 4) * 4.0 + blur(sharp, 20, 4) * 3.5)
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
