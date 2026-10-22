import cv2
import mediapipe as mp
from dataclasses import dataclass
from typing import Optional


@dataclass
class HandLandmarks:
    landmarks: list  # list of (x, y, z) normalised to [0,1]
    handedness: str  # "Left" or "Right"


class HandTracker:
    """Wraps MediaPipe Hands, returns normalised landmarks for one hand."""

    # MediaPipe landmark indices
    WRIST = 0
    THUMB_CMC, THUMB_MCP, THUMB_IP, THUMB_TIP = 1, 2, 3, 4
    INDEX_MCP, INDEX_PIP, INDEX_DIP, INDEX_TIP = 5, 6, 7, 8
    MIDDLE_MCP, MIDDLE_PIP, MIDDLE_DIP, MIDDLE_TIP = 9, 10, 11, 12
    RING_MCP, RING_PIP, RING_DIP, RING_TIP = 13, 14, 15, 16
    PINKY_MCP, PINKY_PIP, PINKY_DIP, PINKY_TIP = 17, 18, 19, 20

    def __init__(self, max_hands: int = 1, detection_confidence: float = 0.7,
                 tracking_confidence: float = 0.7):
        self._mp_hands = mp.solutions.hands
        self._hands = self._mp_hands.Hands(
            max_num_hands=max_hands,
            min_detection_confidence=detection_confidence,
            min_tracking_confidence=tracking_confidence,
        )
        self._mp_draw = mp.solutions.drawing_utils
        self._mp_draw_styles = mp.solutions.drawing_styles

    def process(self, frame_bgr) -> tuple[Optional[HandLandmarks], object]:
        """
        Returns (HandLandmarks | None, annotated_frame).
        frame_bgr: BGR numpy array from cv2.
        """
        rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        results = self._hands.process(rgb)

        annotated = frame_bgr.copy()
        hand = None

        if results.multi_hand_landmarks:
            raw = results.multi_hand_landmarks[0]
            label = results.multi_handedness[0].classification[0].label

            lm_list = [(lm.x, lm.y, lm.z) for lm in raw.landmark]
            hand = HandLandmarks(landmarks=lm_list, handedness=label)

            self._mp_draw.draw_landmarks(
                annotated,
                raw,
                self._mp_hands.HAND_CONNECTIONS,
                self._mp_draw_styles.get_default_hand_landmarks_style(),
                self._mp_draw_styles.get_default_hand_connections_style(),
            )

        return hand, annotated

    def close(self):
        self._hands.close()
