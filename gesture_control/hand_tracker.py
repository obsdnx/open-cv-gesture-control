import os
import time
import urllib.request
import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision as mp_vision
from dataclasses import dataclass
from typing import Optional

_MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/"
    "hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"
)
_MODEL_PATH = os.path.join(os.path.dirname(__file__), "hand_landmarker.task")

# Pairs of landmark indices that form the hand skeleton
_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 4),          # thumb
    (0, 5), (5, 6), (6, 7), (7, 8),           # index
    (0, 9), (9, 10), (10, 11), (11, 12),      # middle
    (0, 13), (13, 14), (14, 15), (15, 16),    # ring
    (0, 17), (17, 18), (18, 19), (19, 20),    # pinky
    (5, 9), (9, 13), (13, 17),                 # palm
]


@dataclass
class HandLandmarks:
    landmarks: list  # list of (x, y, z) normalised to [0,1]
    handedness: str  # "Left" or "Right"


class HandTracker:
    """MediaPipe Tasks hand landmarker wrapper."""

    WRIST = 0
    THUMB_CMC, THUMB_MCP, THUMB_IP, THUMB_TIP = 1, 2, 3, 4
    INDEX_MCP, INDEX_PIP, INDEX_DIP, INDEX_TIP = 5, 6, 7, 8
    MIDDLE_MCP, MIDDLE_PIP, MIDDLE_DIP, MIDDLE_TIP = 9, 10, 11, 12
    RING_MCP, RING_PIP, RING_DIP, RING_TIP = 13, 14, 15, 16
    PINKY_MCP, PINKY_PIP, PINKY_DIP, PINKY_TIP = 17, 18, 19, 20

    def __init__(self, max_hands: int = 1, detection_confidence: float = 0.7,
                 tracking_confidence: float = 0.7):
        if not os.path.exists(_MODEL_PATH):
            print("Downloading hand landmark model (~10 MB)…", flush=True)
            urllib.request.urlretrieve(_MODEL_URL, _MODEL_PATH)
            print("Model downloaded.", flush=True)

        options = mp_vision.HandLandmarkerOptions(
            base_options=mp_python.BaseOptions(model_asset_path=_MODEL_PATH),
            running_mode=mp_vision.RunningMode.VIDEO,
            num_hands=max_hands,
            min_hand_detection_confidence=detection_confidence,
            min_tracking_confidence=tracking_confidence,
        )
        self._landmarker = mp_vision.HandLandmarker.create_from_options(options)
        self._start_ms = time.time_ns() // 1_000_000

    def process(self, frame_bgr) -> tuple[Optional[HandLandmarks], object]:
        """
        Returns (HandLandmarks | None, annotated_frame).
        frame_bgr: BGR numpy array from cv2.
        """
        rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        timestamp_ms = (time.time_ns() // 1_000_000) - self._start_ms
        result = self._landmarker.detect_for_video(mp_image, timestamp_ms)

        annotated = frame_bgr.copy()
        hand = None

        if result.hand_landmarks:
            raw = result.hand_landmarks[0]
            label = result.handedness[0][0].category_name

            lm_list = [(lm.x, lm.y, lm.z) for lm in raw]
            hand = HandLandmarks(landmarks=lm_list, handedness=label)

            h, w = frame_bgr.shape[:2]
            pts = [(int(lm.x * w), int(lm.y * h)) for lm in raw]

            for a, b in _CONNECTIONS:
                cv2.line(annotated, pts[a], pts[b], (0, 200, 80), 2)
            for pt in pts:
                cv2.circle(annotated, pt, 6, (255, 255, 255), -1)
                cv2.circle(annotated, pt, 6, (0, 160, 60), 2)

        return hand, annotated

    def close(self):
        self._landmarker.close()
