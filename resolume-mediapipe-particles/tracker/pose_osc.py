#!/usr/bin/env python3
"""Camera -> MediaPipe Pose -> OSC/UDP bridge for the Resolume FFGL plugin.

Sends one ``/mp/pose`` message per camera frame containing 33 landmarks as
``(x, y, z, visibility)`` floats, or ``/mp/clear`` when nobody is detected.

Only OpenCV and MediaPipe are required; the OSC encoder below is deliberately
self-contained so ``python-osc`` is not another thing to install on a show
machine.  Heavy imports happen inside ``main`` so this module can be imported
(and its wire format tested) without them.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
from typing import Iterable, List, Optional, Sequence, Tuple

NUM_LANDMARKS = 33
FLOATS_PER_LANDMARK = 4
POSE_FLOAT_COUNT = NUM_LANDMARKS * FLOATS_PER_LANDMARK
DEFAULT_PORT = 9010

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


def build_pose_message(frame_id: int, landmarks: Sequence[float]) -> bytes:
    """Encode ``/mp/pose``. ``landmarks`` must hold POSE_FLOAT_COUNT floats."""
    if len(landmarks) != POSE_FLOAT_COUNT:
        raise ValueError(
            f"expected {POSE_FLOAT_COUNT} floats, got {len(landmarks)}"
        )
    body = _osc_string("/mp/pose")
    body += _osc_string("," + "i" + "f" * POSE_FLOAT_COUNT)
    body += struct.pack(">i", frame_id & 0x7FFFFFFF)
    body += struct.pack(f">{POSE_FLOAT_COUNT}f", *landmarks)
    return body


def build_clear_message(frame_id: int) -> bytes:
    """Encode ``/mp/clear``: no person in frame."""
    return (
        _osc_string("/mp/clear")
        + _osc_string(",i")
        + struct.pack(">i", frame_id & 0x7FFFFFFF)
    )


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
    """Common interface over MediaPipe's legacy and Tasks pose APIs."""

    def detect(self, bgr_frame, timestamp_ms: int) -> Optional[List[float]]:
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

    def detect(self, bgr_frame, timestamp_ms: int) -> Optional[List[float]]:
        rgb = self._cv2.cvtColor(bgr_frame, self._cv2.COLOR_BGR2RGB)
        rgb.flags.writeable = False
        result = self._pose.process(rgb)
        if not result.pose_landmarks:
            return None
        values: List[float] = []
        for landmark in result.pose_landmarks.landmark:
            values.extend(
                (landmark.x, landmark.y, landmark.z, landmark.visibility)
            )
        return values

    def close(self) -> None:
        self._pose.close()


class TasksBackend(PoseBackend):
    """``mediapipe.tasks`` PoseLandmarker -- needs a downloaded .task model."""

    def __init__(self, model_path: str, min_confidence: float):
        import mediapipe as mp
        from mediapipe.tasks import python as mp_python
        from mediapipe.tasks.python import vision

        self._mp = mp
        self._vision = vision
        options = vision.PoseLandmarkerOptions(
            base_options=mp_python.BaseOptions(model_asset_path=model_path),
            running_mode=vision.RunningMode.VIDEO,
            num_poses=1,
            min_pose_detection_confidence=min_confidence,
            min_pose_presence_confidence=min_confidence,
            min_tracking_confidence=min_confidence,
        )
        self._landmarker = vision.PoseLandmarker.create_from_options(options)

    def detect(self, bgr_frame, timestamp_ms: int) -> Optional[List[float]]:
        import cv2

        rgb = cv2.cvtColor(bgr_frame, cv2.COLOR_BGR2RGB)
        image = self._mp.Image(
            image_format=self._mp.ImageFormat.SRGB, data=rgb
        )
        result = self._landmarker.detect_for_video(image, timestamp_ms)
        if not result.pose_landmarks:
            return None
        values: List[float] = []
        for landmark in result.pose_landmarks[0]:
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
        return values

    def close(self) -> None:
        self._landmarker.close()


def make_backend(args) -> PoseBackend:
    """Prefer the Tasks API when a model was supplied, else the legacy one."""
    if args.model:
        return TasksBackend(args.model, args.min_confidence)

    import mediapipe as mp

    if hasattr(mp, "solutions") and hasattr(mp.solutions, "pose"):
        return LegacySolutionBackend(
            args.complexity, args.min_confidence, not args.no_smooth
        )
    raise SystemExit(
        "This mediapipe build has no mp.solutions.pose. Download a pose "
        "landmarker model and pass it with --model, e.g.\n"
        "  curl -LO https://storage.googleapis.com/mediapipe-models/"
        "pose_landmarker/pose_landmarker_full/float16/latest/"
        "pose_landmarker_full.task"
    )


# ---------------------------------------------------------------- preview


def draw_preview(cv2, frame, landmarks: Optional[Sequence[float]]) -> None:
    height, width = frame.shape[:2]
    if landmarks is None:
        cv2.putText(
            frame, "no pose", (12, 30),
            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2
        )
        return

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
        cv2.line(frame, point(a), point(b), (80, 220, 255), 2)
    for i in range(NUM_LANDMARKS):
        if landmarks[i * FLOATS_PER_LANDMARK + 3] < 0.35:
            continue
        cv2.circle(frame, point(i), 3, (255, 255, 255), -1)


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
            landmarks = backend.detect(frame, timestamp_ms)

            if landmarks is None:
                sender.send(build_clear_message(frame_id))
            else:
                sender.send(build_pose_message(frame_id, landmarks))
                detections_since_report += 1

            frames_since_report += 1
            now = time.perf_counter()
            if not args.quiet and now - reported >= 2.0:
                fps = frames_since_report / (now - reported)
                hit = detections_since_report / max(frames_since_report, 1)
                print(f"\r{fps:5.1f} fps   pose {hit * 100:3.0f}%   "
                      f"frame {frame_id}", end="", flush=True)
                reported = now
                frames_since_report = 0
                detections_since_report = 0

            if args.preview:
                shown = cv2.flip(frame, 1) if args.mirror_preview else frame
                preview = shown.copy()
                if args.mirror_preview and landmarks is not None:
                    mirrored = list(landmarks)
                    for i in range(NUM_LANDMARKS):
                        mirrored[i * FLOATS_PER_LANDMARK] = (
                            1.0 - mirrored[i * FLOATS_PER_LANDMARK]
                        )
                    draw_preview(cv2, preview, mirrored)
                else:
                    draw_preview(cv2, preview, landmarks)
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
