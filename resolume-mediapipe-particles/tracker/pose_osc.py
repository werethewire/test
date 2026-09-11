#!/usr/bin/env python3
"""Camera -> MediaPipe Pose -> OSC/UDP bridge for the Resolume FFGL plugin.

Sends one ``/mp/pose`` message per detected body per camera frame, containing
33 landmarks as ``(x, y, z, visibility)`` floats, or ``/mp/clear`` when nobody
is detected. Multiple bodies need the MediaPipe Tasks backend (``--model``);
the legacy ``mp.solutions.pose`` API only ever returns one.

Only OpenCV and MediaPipe are required; the OSC encoder below is deliberately
self-contained so ``python-osc`` is not another thing to install on a show
machine.  Heavy imports happen inside ``main`` so this module can be imported
(and its wire format tested) without them.
"""

from __future__ import annotations

import argparse
import math
import socket
import struct
import sys
import time
from typing import Iterable, List, Optional, Sequence, Tuple

NUM_LANDMARKS = 33
FLOATS_PER_LANDMARK = 4
POSE_FLOAT_COUNT = NUM_LANDMARKS * FLOATS_PER_LANDMARK
DEFAULT_PORT = 9010
# Must match MAX_PERSONS in plugin/src/PoseProtocol.h: the plugin drops any
# body beyond this rather than folding it onto someone else's slot.
MAX_PERSONS = 3

# Landmark index pairs the plugin draws particles along. Kept here only for the
# optional preview window; the plugin has its own copy.
PREVIEW_BONES: Sequence[Tuple[int, int]] = (
    (11, 12), (11, 23), (12, 24), (23, 24),
    (11, 13), (13, 15), (12, 14), (14, 16),
    (23, 25), (25, 27), (27, 31), (24, 26), (26, 28), (28, 32),
    (0, 7), (0, 8), (7, 11), (8, 12),
)


# --------------------------------------------------------------------- OSC


def _osc_string(value: str) -> bytes:
    """OSC strings are NUL terminated then padded to a 4 byte boundary."""
    raw = value.encode("utf-8") + b"\0"
    padding = (4 - len(raw) % 4) % 4
    return raw + b"\0" * padding


def build_pose_message(
    frame_id: int, landmarks: Sequence[float], person_id: int = 0
) -> bytes:
    """Encode ``/mp/pose``. ``landmarks`` must hold POSE_FLOAT_COUNT floats."""
    if len(landmarks) != POSE_FLOAT_COUNT:
        raise ValueError(
            f"expected {POSE_FLOAT_COUNT} floats, got {len(landmarks)}"
        )
    body = _osc_string("/mp/pose")
    body += _osc_string(",ii" + "f" * POSE_FLOAT_COUNT)
    body += struct.pack(">i", frame_id & 0x7FFFFFFF)
    body += struct.pack(">i", person_id)
    body += struct.pack(f">{POSE_FLOAT_COUNT}f", *landmarks)
    return body


def build_clear_message(frame_id: int, person_id: Optional[int] = None) -> bytes:
    """Encode ``/mp/clear``.

    Without ``person_id`` this means the frame is empty and every body should
    fade out; with one it retires just that body.
    """
    payload = _osc_string("/mp/clear")
    if person_id is None:
        payload += _osc_string(",i") + struct.pack(">i", frame_id & 0x7FFFFFFF)
    else:
        payload += _osc_string(",ii")
        payload += struct.pack(">i", frame_id & 0x7FFFFFFF)
        payload += struct.pack(">i", person_id)
    return payload


class OscSender:
    """Fire-and-forget UDP sender to one or more destinations."""

    def __init__(self, targets: Iterable[Tuple[str, int]]):
        self.targets = list(targets)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Large enough that a stalled receiver never blocks the capture loop.
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 18)

    def send(self, payload: bytes) -> None:
        for target in self.targets:
            try:
                self.sock.sendto(payload, target)
            except OSError:
                # A closed listener is normal (Resolume not running yet);
                # dropping the packet is the right response.
                pass

    def close(self) -> None:
        self.sock.close()


# ------------------------------------------------------------- pose backends


class PoseBackend:
    """Common interface over MediaPipe's legacy and Tasks pose APIs.

    ``detect`` returns one landmark list per detected body, newest first as
    MediaPipe ranked them. An empty list means nobody was found.
    """

    def detect(self, bgr_frame, timestamp_ms: int) -> List[List[float]]:
        raise NotImplementedError

    def close(self) -> None:
        pass


class LegacySolutionBackend(PoseBackend):
    """``mediapipe.solutions.pose`` -- present in mediapipe <= 0.10.x."""

    def __init__(self, complexity: int, min_confidence: float, smooth: bool):
        import cv2  # noqa: F401  (imported for the colour conversion below)
        import mediapipe as mp

        self._cv2 = cv2
        self._pose = mp.solutions.pose.Pose(
            static_image_mode=False,
            model_complexity=complexity,
            smooth_landmarks=smooth,
            enable_segmentation=False,
            min_detection_confidence=min_confidence,
            min_tracking_confidence=min_confidence,
        )

    def detect(self, bgr_frame, timestamp_ms: int) -> List[List[float]]:
        rgb = self._cv2.cvtColor(bgr_frame, self._cv2.COLOR_BGR2RGB)
        rgb.flags.writeable = False
        result = self._pose.process(rgb)
        if not result.pose_landmarks:
            return []
        values: List[float] = []
        for landmark in result.pose_landmarks.landmark:
            values.extend(
                (landmark.x, landmark.y, landmark.z, landmark.visibility)
            )
        # This API is single person by design.
        return [values]

    def close(self) -> None:
        self._pose.close()


class TasksBackend(PoseBackend):
    """``mediapipe.tasks`` PoseLandmarker -- needs a downloaded .task model."""

    def __init__(self, model_path: str, min_confidence: float, num_poses: int):
        import mediapipe as mp
        from mediapipe.tasks import python as mp_python
        from mediapipe.tasks.python import vision

        self._mp = mp
        self._vision = vision
        options = vision.PoseLandmarkerOptions(
            base_options=mp_python.BaseOptions(model_asset_path=model_path),
            running_mode=vision.RunningMode.VIDEO,
            num_poses=num_poses,
            min_pose_detection_confidence=min_confidence,
            min_pose_presence_confidence=min_confidence,
            min_tracking_confidence=min_confidence,
        )
        self._landmarker = vision.PoseLandmarker.create_from_options(options)

    def detect(self, bgr_frame, timestamp_ms: int) -> List[List[float]]:
        import cv2

        rgb = cv2.cvtColor(bgr_frame, cv2.COLOR_BGR2RGB)
        image = self._mp.Image(
            image_format=self._mp.ImageFormat.SRGB, data=rgb
        )
        result = self._landmarker.detect_for_video(image, timestamp_ms)
        if not result.pose_landmarks:
            return []
        bodies: List[List[float]] = []
        for pose in result.pose_landmarks:
            values: List[float] = []
            for landmark in pose:
                # The Tasks API calls it `visibility` too, but it can be None.
                visibility = getattr(landmark, "visibility", None)
                values.extend(
                    (
                        landmark.x,
                        landmark.y,
                        landmark.z,
                        1.0 if visibility is None else float(visibility),
                    )
                )
            bodies.append(values)
        return bodies

    def close(self) -> None:
        self._landmarker.close()


MODEL_HINT = (
    "  curl -LO https://storage.googleapis.com/mediapipe-models/"
    "pose_landmarker/pose_landmarker_full/float16/latest/"
    "pose_landmarker_full.task"
)


def make_backend(args) -> PoseBackend:
    """Prefer the Tasks API when a model was supplied, else the legacy one."""
    if args.model:
        return TasksBackend(args.model, args.min_confidence, args.people)

    if args.people > 1:
        raise SystemExit(
            "--people > 1 needs the MediaPipe Tasks backend. Download a pose "
            "landmarker model and pass it with --model, e.g.\n" + MODEL_HINT
        )

    import mediapipe as mp

    if hasattr(mp, "solutions") and hasattr(mp.solutions, "pose"):
        return LegacySolutionBackend(
            args.complexity, args.min_confidence, not args.no_smooth
        )
    raise SystemExit(
        "This mediapipe build has no mp.solutions.pose. Download a pose "
        "landmarker model and pass it with --model, e.g.\n" + MODEL_HINT
    )


# ----------------------------------------------------------- identity


def body_centroid(landmarks: Sequence[float]) -> Tuple[float, float]:
    """Visibility weighted centre of a body, in normalised image coordinates."""
    total = 0.0
    cx = 0.0
    cy = 0.0
    for i in range(NUM_LANDMARKS):
        base = i * FLOATS_PER_LANDMARK
        weight = landmarks[base + 3]
        cx += landmarks[base] * weight
        cy += landmarks[base + 1] * weight
        total += weight
    if total <= 0.0:
        return (0.5, 0.5)
    return (cx / total, cy / total)


class BodyAssigner:
    """Keeps each person on the same plugin slot from frame to frame.

    MediaPipe does not promise a stable order for multiple poses, and the
    plugin's particles are bound to a slot: if two people swapped slots mid
    show, their particles would swap with them. Detections are therefore
    matched to the previous frame's slots by proximity, greedily and closest
    pair first, which is enough for people who do not teleport.
    """

    def __init__(self, max_persons: int = MAX_PERSONS, match_radius: float = 0.35,
                 retire_after: int = 15):
        self.max_persons = max_persons
        self.match_radius = match_radius
        self.retire_after = retire_after
        self.centroids: List[Optional[Tuple[float, float]]] = [None] * max_persons
        self.missing: List[int] = [0] * max_persons
        self._retired: List[int] = []

    def assign(self, bodies: Sequence[Sequence[float]]) -> List[Tuple[int, Sequence[float]]]:
        """Returns (person_id, landmarks) pairs for this frame."""
        detections = list(bodies)[: self.max_persons]
        centroids = [body_centroid(b) for b in detections]

        # Score every plausible detection/slot pairing, then take them in order
        # of increasing distance so the most obvious match wins first.
        candidates = []
        for d_index, centre in enumerate(centroids):
            for slot, previous in enumerate(self.centroids):
                if previous is None:
                    continue
                dx = centre[0] - previous[0]
                dy = centre[1] - previous[1]
                distance = math.hypot(dx, dy)
                if distance <= self.match_radius:
                    candidates.append((distance, d_index, slot))
        candidates.sort()

        slot_for: dict = {}
        used_slots = set()
        for _, d_index, slot in candidates:
            if d_index in slot_for or slot in used_slots:
                continue
            slot_for[d_index] = slot
            used_slots.add(slot)

        # Anything unmatched takes the first free slot.
        for d_index in range(len(detections)):
            if d_index in slot_for:
                continue
            for slot in range(self.max_persons):
                if slot not in used_slots:
                    slot_for[d_index] = slot
                    used_slots.add(slot)
                    break

        result: List[Tuple[int, Sequence[float]]] = []
        for d_index, slot in sorted(slot_for.items(), key=lambda kv: kv[1]):
            self.centroids[slot] = centroids[d_index]
            self.missing[slot] = 0
            result.append((slot, detections[d_index]))

        # Slots nobody claimed this frame count down to being freed.
        self._retired = []
        for slot in range(self.max_persons):
            if slot in used_slots or self.centroids[slot] is None:
                continue
            self.missing[slot] += 1
            if self.missing[slot] >= self.retire_after:
                self.centroids[slot] = None
                self.missing[slot] = 0
                self._retired.append(slot)

        return result

    def take_retired(self) -> List[int]:
        """Slots that just went empty and should be cleared on the plugin."""
        retired = self._retired
        self._retired = []
        return retired

    def active_slots(self) -> int:
        return sum(1 for c in self.centroids if c is not None)


# ---------------------------------------------------------------- preview


# One colour per plugin slot, so the preview shows who is on which body.
SLOT_COLORS = ((80, 220, 255), (120, 255, 140), (255, 160, 90))


def draw_preview(cv2, frame, bodies: Sequence[Tuple[int, Sequence[float]]]) -> None:
    height, width = frame.shape[:2]
    if not bodies:
        cv2.putText(
            frame, "no pose", (12, 30),
            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2
        )
        return

    for person_id, landmarks in bodies:
        colour = SLOT_COLORS[person_id % len(SLOT_COLORS)]

        def point(index: int) -> Tuple[int, int]:
            base = index * FLOATS_PER_LANDMARK
            return (
                int(landmarks[base] * width),
                int(landmarks[base + 1] * height),
            )

        for a, b in PREVIEW_BONES:
            va = landmarks[a * FLOATS_PER_LANDMARK + 3]
            vb = landmarks[b * FLOATS_PER_LANDMARK + 3]
            if min(va, vb) < 0.35:
                continue
            cv2.line(frame, point(a), point(b), colour, 2)
        for i in range(NUM_LANDMARKS):
            if landmarks[i * FLOATS_PER_LANDMARK + 3] < 0.35:
                continue
            cv2.circle(frame, point(i), 3, (255, 255, 255), -1)

        cx, cy = body_centroid(landmarks)
        cv2.putText(
            frame, f"#{person_id}", (int(cx * width) - 12, int(cy * height)),
            cv2.FONT_HERSHEY_SIMPLEX, 0.7, colour, 2
        )


# ------------------------------------------------------------------- main


def parse_targets(values: Sequence[str], default_port: int) -> List[Tuple[str, int]]:
    """Accepts ``host``, ``host:port`` or ``port``."""
    targets: List[Tuple[str, int]] = []
    for value in values:
        if ":" in value:
            host, _, port = value.rpartition(":")
            targets.append((host or "127.0.0.1", int(port)))
        elif value.isdigit():
            targets.append(("127.0.0.1", int(value)))
        else:
            targets.append((value, default_port))
    return targets or [("127.0.0.1", default_port)]


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Stream MediaPipe pose landmarks to the Resolume "
                    "MediaPipe Particles FFGL plugin over OSC."
    )
    parser.add_argument("--device", default="0",
                        help="camera index or a video file path (default: 0)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=60,
                        help="requested camera frame rate")
    parser.add_argument("--target", action="append", default=[],
                        metavar="HOST:PORT",
                        help="OSC destination, repeatable "
                             f"(default: 127.0.0.1:{DEFAULT_PORT})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="default port for --target entries without one")
    parser.add_argument("--people", type=int, default=1,
                        choices=range(1, MAX_PERSONS + 1), metavar=f"1-{MAX_PERSONS}",
                        help="how many bodies to track; more than 1 requires "
                             "--model (default: 1)")
    parser.add_argument("--match-radius", type=float, default=0.35,
                        help="how far a body may move between frames and still "
                             "keep its slot, in image widths (default: 0.35)")
    parser.add_argument("--model", default=None,
                        help="path to a pose_landmarker .task model; enables "
                             "the MediaPipe Tasks backend")
    parser.add_argument("--complexity", type=int, default=1, choices=(0, 1, 2),
                        help="legacy backend model complexity (default: 1)")
    parser.add_argument("--min-confidence", type=float, default=0.5)
    parser.add_argument("--no-smooth", action="store_true",
                        help="disable MediaPipe's own smoothing; the plugin "
                             "filters too, so this is often the better look")
    parser.add_argument("--preview", action="store_true",
                        help="show a window with the detected skeleton")
    parser.add_argument("--mirror-preview", action="store_true",
                        help="flip the preview only; the plugin has its own "
                             "Mirror parameter")
    parser.add_argument("--quiet", action="store_true")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)

    import cv2

    device: object = args.device
    if str(args.device).isdigit():
        device = int(args.device)

    capture = cv2.VideoCapture(device)
    if not capture.isOpened():
        print(f"could not open capture device {args.device!r}", file=sys.stderr)
        return 1
    capture.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    capture.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    capture.set(cv2.CAP_PROP_FPS, args.fps)
    # A one frame queue keeps latency down on cameras that buffer.
    try:
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    except Exception:
        pass

    backend = make_backend(args)
    assigner = BodyAssigner(args.people, args.match_radius)
    targets = parse_targets(args.target, args.port)
    sender = OscSender(targets)

    if not args.quiet:
        print("sending pose to " + ", ".join(f"{h}:{p}" for h, p in targets))
        print("ctrl-c to stop")

    frame_id = 0
    started = time.perf_counter()
    reported = started
    frames_since_report = 0
    detections_since_report = 0

    try:
        while True:
            ok, frame = capture.read()
            if not ok:
                # Video files simply end; cameras occasionally drop a frame.
                if isinstance(device, str):
                    break
                continue

            frame_id += 1
            timestamp_ms = int((time.perf_counter() - started) * 1000.0)
            bodies = assigner.assign(backend.detect(frame, timestamp_ms))

            for person_id, landmarks in bodies:
                sender.send(build_pose_message(frame_id, landmarks, person_id))
            # Retire slots that emptied, so the plugin fades just that body.
            for person_id in assigner.take_retired():
                sender.send(build_clear_message(frame_id, person_id))
            if not bodies and assigner.active_slots() == 0:
                sender.send(build_clear_message(frame_id))
            if bodies:
                detections_since_report += 1

            frames_since_report += 1
            now = time.perf_counter()
            if not args.quiet and now - reported >= 2.0:
                fps = frames_since_report / (now - reported)
                hit = detections_since_report / max(frames_since_report, 1)
                print(f"\r{fps:5.1f} fps   pose {hit * 100:3.0f}%   "
                      f"bodies {len(bodies)}   frame {frame_id}",
                      end="", flush=True)
                reported = now
                frames_since_report = 0
                detections_since_report = 0

            if args.preview:
                shown = cv2.flip(frame, 1) if args.mirror_preview else frame
                preview = shown.copy()
                drawn = bodies
                if args.mirror_preview:
                    drawn = []
                    for person_id, landmarks in bodies:
                        mirrored = list(landmarks)
                        for i in range(NUM_LANDMARKS):
                            mirrored[i * FLOATS_PER_LANDMARK] = (
                                1.0 - mirrored[i * FLOATS_PER_LANDMARK]
                            )
                        drawn.append((person_id, mirrored))
                draw_preview(cv2, preview, drawn)
                cv2.imshow("pose_osc", preview)
                if cv2.waitKey(1) & 0xFF in (27, ord("q")):
                    break
    except KeyboardInterrupt:
        pass
    finally:
        if not args.quiet:
            print()
        # Tell the plugin to fade out rather than freeze on the last pose.
        sender.send(build_clear_message(frame_id + 1))
        sender.close()
        backend.close()
        capture.release()
        if args.preview:
            cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
