#!/usr/bin/env python3
"""Send a synthetic walking/waving figure to the plugin, with no camera.

Useful for checking the plugin is receiving and for dialling in looks before a
show. Uses only the standard library.

    python3 send_test_pose.py --port 9010
"""

from __future__ import annotations

import argparse
import math
import time

from pose_osc import (
    FLOATS_PER_LANDMARK,
    MAX_PERSONS,
    NUM_LANDMARKS,
    DEFAULT_PORT,
    OscSender,
    build_clear_message,
    build_pose_message,
    parse_targets,
)

# Rest pose in normalised camera coordinates (origin top-left).
BASE = {
    0: (0.50, 0.14),   # nose
    # eyes (inner, centre, outer) and mouth corners, for Emit From = Head
    1: (0.49, 0.125), 2: (0.48, 0.125), 3: (0.47, 0.125),
    4: (0.51, 0.125), 5: (0.52, 0.125), 6: (0.53, 0.125),
    9: (0.485, 0.17), 10: (0.515, 0.17),
    7: (0.46, 0.15), 8: (0.54, 0.15),
    11: (0.42, 0.30), 12: (0.58, 0.30),
    13: (0.32, 0.42), 14: (0.68, 0.42),
    15: (0.26, 0.55), 16: (0.74, 0.55),
    # pinky, index, thumb, for Emit From = Hands
    17: (0.235, 0.585), 19: (0.245, 0.60), 21: (0.27, 0.59),
    18: (0.765, 0.585), 20: (0.755, 0.60), 22: (0.73, 0.59),
    23: (0.45, 0.58), 24: (0.55, 0.58),
    25: (0.44, 0.76), 26: (0.56, 0.76),
    27: (0.43, 0.93), 28: (0.57, 0.93),
    31: (0.40, 0.97), 32: (0.60, 0.97),
}


def synth_pose(t: float, motion: float, shift_x: float = 0.0,
               depth: float = 0.0, scale: float = 1.0) -> list:
    """A simple wave + weight shift, enough to see motion inheritance work."""
    values = [0.0] * (NUM_LANDMARKS * FLOATS_PER_LANDMARK)
    swing = math.sin(t * 2.4) * 0.16 * motion
    bob = math.sin(t * 4.8) * 0.02 * motion
    sway = math.sin(t * 1.1) * 0.05 * motion

    for index in range(NUM_LANDMARKS):
        x, y = BASE.get(index, (0.5, 0.5))
        visibility = 1.0 if index in BASE else 0.0

        x = 0.5 + (x - 0.5) * scale
        y = 0.5 + (y - 0.5) * scale
        x += sway
        y += bob
        # The fingers ride along with their wrist.
        if index in (13, 15, 17, 19, 21):    # left arm swings up
            y -= abs(swing) * 1.6
            x -= swing * 0.5
        if index in (14, 16, 18, 20, 22):    # right arm swings the other way
            y += swing * 0.8
            x += swing * 0.5
        if index in (25, 27, 31):
            x += swing * 0.25
        if index in (26, 28, 32):
            x -= swing * 0.25

        base = index * FLOATS_PER_LANDMARK
        values[base + 0] = min(max(x + shift_x, 0.0), 1.0)
        values[base + 1] = min(max(y, 0.0), 1.0)
        values[base + 2] = depth
        values[base + 3] = visibility
    return values


def figure_layout(count: int):
    """Spread `count` figures across the frame at different depths.

    The middle one is nearest, so the plugin's Depth parameter has something
    obvious to act on.
    """
    if count == 1:
        return [(0.0, 0.0, 1.0)]
    layout = []
    for i in range(count):
        # Evenly spaced, with the depth alternating near/far.
        shift = (i + 0.5) / count - 0.5
        depth = -0.35 if i % 2 == 0 else 0.35
        scale = 1.0 - 0.18 * abs(depth) / 0.35 * (1 if depth > 0 else -1)
        layout.append((shift * 0.9, depth, scale))
    return layout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", action="append", default=[],
                        metavar="HOST:PORT")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--motion", type=float, default=1.0,
                        help="0 = a still figure, 1 = normal, 2 = frantic")
    parser.add_argument("--gap", type=float, default=0.0,
                        help="seconds of /mp/clear every 5 s, to test fade out")
    parser.add_argument("--people", type=int, default=1,
                        choices=range(1, MAX_PERSONS + 1), metavar=f"1-{MAX_PERSONS}",
                        help="how many synthetic figures to send (default: 1)")
    args = parser.parse_args()

    targets = parse_targets(args.target, args.port)
    sender = OscSender(targets)
    layout = figure_layout(args.people)
    print(f"sending {args.people} synthetic figure(s) to "
          + ", ".join(f"{h}:{p}" for h, p in targets))
    print("ctrl-c to stop")

    period = 1.0 / max(args.fps, 1.0)
    started = time.perf_counter()
    frame = 0
    try:
        while True:
            now = time.perf_counter()
            t = now - started
            frame += 1
            in_gap = args.gap > 0.0 and (t % 5.0) < args.gap
            if in_gap:
                sender.send(build_clear_message(frame))
            else:
                for person_id, (shift, depth, scale) in enumerate(layout):
                    # Offset each figure in time too, so they do not move as one.
                    pose = synth_pose(t + person_id * 0.7, args.motion,
                                      shift, depth, scale)
                    sender.send(
                        build_pose_message(frame, pose, person_id)
                    )
            sleep_for = period - (time.perf_counter() - now)
            if sleep_for > 0:
                time.sleep(sleep_for)
    except KeyboardInterrupt:
        pass
    finally:
        sender.send(build_clear_message(frame + 1))
        sender.close()
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
