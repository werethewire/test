"""Render a 10s video: a slowly rotating wireframe sphere with drifting particles.

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

CAM_Z = 3.4
FOCAL = 1.05 * H
LAT_LINES = 11
LON_LINES = 24  # 15deg spacing -> rotating 30deg over 10s loops seamlessly
SEG = 2400
N_PARTICLES = 2600

rng = np.random.default_rng(7)
TAU = 2 * np.pi


def build_wireframe():
    pts = []
    th = np.linspace(0, TAU, SEG, endpoint=False)
    for i in range(1, LAT_LINES + 1):
        phi = -np.pi / 2 + np.pi * i / (LAT_LINES + 1)
        r, y = np.cos(phi), np.sin(phi)
        pts.append(np.stack([r * np.cos(th), np.full_like(th, y), r * np.sin(th)], 1))
    ph = np.linspace(-np.pi / 2, np.pi / 2, SEG // 2)
    for j in range(LON_LINES):
        a = TAU * j / LON_LINES
        pts.append(np.stack([np.cos(ph) * np.cos(a), np.sin(ph), np.cos(ph) * np.sin(a)], 1))
    return np.concatenate(pts)


WIRE = build_wireframe()

# Particles: base position in a shell around/inside the sphere + periodic Lissajous drift
u = rng.normal(size=(N_PARTICLES, 3))
u /= np.linalg.norm(u, axis=1, keepdims=True)
rad = rng.uniform(0.15, 2.4, N_PARTICLES) ** 0.8
P_BASE = u * rad[:, None]
P_AMP = rng.uniform(0.08, 0.45, (N_PARTICLES, 3))
P_FREQ = rng.integers(1, 3, (N_PARTICLES, 3)) * TAU / DURATION
P_PHASE = rng.uniform(0, TAU, (N_PARTICLES, 3))
P_ORBIT = rng.choice([-1, 1], N_PARTICLES) * TAU / DURATION / 4  # quarter turn per loop
P_BRIGHT = rng.uniform(0.35, 1.0, N_PARTICLES) ** 2
P_TWINKLE = rng.uniform(0, TAU, N_PARTICLES)
P_HUE = rng.uniform(0, 1, N_PARTICLES)

WIRE_COL = np.array([0.30, 0.75, 1.00])
PART_COL_A = np.array([0.55, 0.90, 1.00])
PART_COL_B = np.array([1.00, 0.78, 0.55])

# Background: deep navy radial gradient
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
d = np.sqrt(((xx - W / 2) / W) ** 2 + ((yy - H / 2) / W) ** 2)
BG = np.stack([0.010 + 0.025 * np.exp(-d * 4), 0.014 + 0.040 * np.exp(-d * 4), 0.030 + 0.070 * np.exp(-d * 3.5)], -1)
VIGNETTE = np.clip(1.15 - d * 1.1, 0, 1)[..., None]


def rot_y(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_x(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def project(p):
    z = CAM_Z - p[:, 2]
    x = W / 2 + FOCAL * p[:, 0] / z
    y = H / 2 - FOCAL * p[:, 1] / z
    return x, y, z


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

    # Wireframe sphere: slow Y rotation (30deg / loop) with fixed tilt and gentle wobble
    R = rot_x(0.38 + 0.05 * np.sin(TAU * t / DURATION)) @ rot_y(np.radians(30) * t / DURATION)
    p = WIRE @ R.T
    x, y, z = project(p)
    front = np.clip((p[:, 2] + 1) / 2, 0, 1)  # 1 = facing camera
    inten = (0.18 + 1.2 * front ** 1.6) * (260 / SEG)
    splat(sharp, x, y, inten[:, None] * WIRE_COL[None])

    # Particles
    ang = P_ORBIT * t
    q = P_BASE + P_AMP * np.sin(P_FREQ * t + P_PHASE)
    c, s = np.cos(ang), np.sin(ang)
    q = np.stack([c * q[:, 0] + s * q[:, 2], q[:, 1], -s * q[:, 0] + c * q[:, 2]], 1)
    q = q @ rot_x(0.38).T
    x, y, z = project(q)
    ok = z > 0.6
    twinkle = 0.7 + 0.3 * np.sin(TAU * 2 * t / DURATION + P_TWINKLE)
    depth = np.clip((CAM_Z + 2.4 - z) / 4.8, 0.05, 1.0)
    col = PART_COL_A[None] * (1 - P_HUE[:, None] ** 3) + PART_COL_B[None] * P_HUE[:, None] ** 3
    v = (P_BRIGHT * twinkle * (0.25 + 0.75 * depth))[:, None] * col
    near = z < 2.2  # close particles become soft bokeh dots
    splat(sharp, x[ok & ~near], y[ok & ~near], v[ok & ~near] * 2.6)
    splat(soft, x[ok & near], y[ok & near], v[ok & near] * 30.0)

    sharp = sharp.reshape(H, W, 3)
    soft = soft.reshape(H, W, 3)
    img = sharp + blur(soft, 2.5, 2) + blur(sharp, 3, 4) * 1.6 + blur(sharp, 10, 4) * 1.8
    img = BG + img
    img = 1 - np.exp(-img * 1.6)  # tonemap
    img = img * VIGNETTE
    img = np.clip(img ** (1 / 1.1) * 255 + rng.uniform(-1, 1, (H, W, 1)), 0, 255)  # dither
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
