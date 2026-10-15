"""Render a 10s weightless video from inside a tumbling lat/long wireframe sphere with drifting particles.

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
LAT_LINES = 9  # 18deg spacing
MERIDIANS = 10  # full great circles through the poles -> 20 meridians, 18deg spacing
SEG = 6000
N_PARTICLES = 875
# Two rotations on unrelated oblique axes -> the effective spin axis keeps wandering (tumbling)
TUMBLE = [(np.array([0.8, 0.5, 0.3]), np.radians(34)), (np.array([-0.3, 0.6, -0.75]), np.radians(22))]

rng = np.random.default_rng(11)
TAU = 2 * np.pi


def build_wireframe():
    """Return (lines, SEG, 3): every line is a closed circle sampled SEG times."""
    th = np.linspace(0, TAU, SEG)
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
# Soften where meridians converge so the poles read as a glow rather than a blown-out star
_pf = np.clip((1 - np.abs(WIRE[..., 1])) / (1 - np.cos(np.radians(16))), 0, 1)
WIRE_POLE_FADE = 0.3 + 0.7 * _pf * _pf * (3 - 2 * _pf)
# A soft band of light sweeps through space; the grid tumbles through it like drifting through a current
WAVE_DIR = np.array([0.45, -0.3, 0.84]) / np.linalg.norm([0.45, -0.3, 0.84])

# Particles fill the sphere volume and drift on slow periodic paths
u = rng.normal(size=(N_PARTICLES, 3))
u /= np.linalg.norm(u, axis=1, keepdims=True)
P_BASE = u * (rng.uniform(0, 1, N_PARTICLES) ** (1 / 3) * 0.92)[:, None]
# Two slow, incommensurate sway components per axis give an organic, buoyant float
P_AMP = rng.uniform(0.03, 0.16, (N_PARTICLES, 3))
P_FREQ = rng.uniform(0.25, 0.5, (N_PARTICLES, 3)) * TAU / DURATION
P_AMP2 = rng.uniform(0.01, 0.06, (N_PARTICLES, 3))
P_FREQ2 = rng.uniform(0.5, 0.8, (N_PARTICLES, 3)) * TAU / DURATION
P_PHASE2 = rng.uniform(0, TAU, (N_PARTICLES, 3))
P_PHASE = rng.uniform(0, TAU, (N_PARTICLES, 3))
P_VEL = rng.normal(size=(N_PARTICLES, 3)) * 0.0055  # slow straight drift in every direction
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
    mid = np.zeros((H * W, 3), np.float32)
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
    band = (p @ WAVE_DIR) - (-1.3 + 2.6 * t / DURATION)
    wave = 0.8 + 1.4 * np.exp(-0.5 * (band / 0.16) ** 2)
    inten = (0.9 * ds * fade * wave * WIRE_POLE_FADE)[ok]
    splat(wire, x[ok], y[ok], inten[:, None] * WIRE_COL[None])

    # Particles
    q = (P_BASE + P_VEL * (t - DURATION / 2) + P_AMP * np.sin(P_FREQ * t + P_PHASE)
         + P_AMP2 * np.sin(P_FREQ2 * t + P_PHASE2))
    x, y, depth = project(q, cam)
    ok = depth > NEAR
    twinkle = 0.65 + 0.35 * np.sin(TAU * 2 * t / DURATION + P_TWINKLE)
    col = PART_COL_A[None] * (1 - P_HUE[:, None]) + PART_COL_B[None] * P_HUE[:, None]
    v = (P_BRIGHT * twinkle * np.clip(0.9 / depth, 0.4, 6.0))[:, None] * col
    # Focus slowly breathes from near to far, so particles drift in and out of sharpness
    focus = 0.8 - 0.4 * np.cos(np.pi * t / DURATION)
    coc = np.abs(1 / np.maximum(depth, NEAR) - 1 / focus)
    sharp_m, mid_m = ok & (coc < 0.45), ok & (coc >= 0.45) & (coc < 1.4)
    soft_m = ok & (coc >= 1.4)
    splat(dots, x[sharp_m], y[sharp_m], v[sharp_m] * 16.0)
    splat(mid, x[mid_m], y[mid_m], v[mid_m] * 32.0)
    splat(soft, x[soft_m], y[soft_m], v[soft_m] * 75.0)

    wire = wire.reshape(H, W, 3)
    # Particles: small discs in focus, softer discs slightly out, large bokeh far out of focus
    part = (blur(dots.reshape(H, W, 3), 1.4, 1) + blur(mid.reshape(H, W, 3), 2.0, 2)
            + blur(soft.reshape(H, W, 3), 6.0, 2))
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
