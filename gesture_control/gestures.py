from enum import Enum, auto
import math
from .hand_tracker import HandLandmarks, HandTracker as HT


class Gesture(Enum):
    NONE = auto()
    POINTER = auto()       # index only up  → move cursor
    CLICK = auto()         # pinch (thumb + index close)
    DRAG = auto()          # fist (all curled) → hold + drag
    SCROLL = auto()        # index + middle up (peace) → scroll
    SWITCH_WINDOW = auto() # index + middle + ring up → Cmd+Tab
    MINIMIZE = auto()      # index + middle + ring + pinky up (no thumb)
    MAXIMIZE = auto()      # all 5 fingers spread (open hand)


def _dist(a, b) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _finger_up(lm: list, tip: int, pip: int, is_thumb: bool = False,
               handedness: str = "Right") -> bool:
    if is_thumb:
        # Thumb is up when tip is to the left of IP for right hand (mirrored cam)
        tip_x = lm[tip][0]
        ip_x = lm[HT.THUMB_IP][0]
        return tip_x < ip_x if handedness == "Right" else tip_x > ip_x
    return lm[tip][1] < lm[pip][1]


def _fingers_up(hand: HandLandmarks) -> list[bool]:
    lm = hand.landmarks
    h = hand.handedness
    return [
        _finger_up(lm, HT.THUMB_TIP, HT.THUMB_IP, is_thumb=True, handedness=h),
        _finger_up(lm, HT.INDEX_TIP, HT.INDEX_PIP),
        _finger_up(lm, HT.MIDDLE_TIP, HT.MIDDLE_PIP),
        _finger_up(lm, HT.RING_TIP, HT.RING_PIP),
        _finger_up(lm, HT.PINKY_TIP, HT.PINKY_PIP),
    ]


def recognize(hand: HandLandmarks, pinch_threshold: float = 0.05) -> Gesture:
    if hand is None:
        return Gesture.NONE

    lm = hand.landmarks
    fingers = _fingers_up(hand)
    thumb, index, middle, ring, pinky = fingers
    up_count = sum(fingers)

    # Pinch: thumb and index tips close together regardless of other fingers
    pinch_dist = _dist(lm[HT.THUMB_TIP], lm[HT.INDEX_TIP])
    if pinch_dist < pinch_threshold:
        return Gesture.CLICK

    # Fist: all fingers curled
    if up_count == 0:
        return Gesture.DRAG

    # Open hand: all fingers up
    if up_count == 5:
        return Gesture.MAXIMIZE

    # index + middle + ring + pinky (no thumb)
    if not thumb and index and middle and ring and pinky:
        return Gesture.MINIMIZE

    # index + middle + ring (no thumb, no pinky)
    if not thumb and index and middle and ring and not pinky:
        return Gesture.SWITCH_WINDOW

    # Peace: index + middle only
    if not thumb and index and middle and not ring and not pinky:
        return Gesture.SCROLL

    # Pointer: index only
    if not thumb and index and not middle and not ring and not pinky:
        return Gesture.POINTER

    return Gesture.NONE


def index_tip_position(hand: HandLandmarks) -> tuple[float, float]:
    lm = hand.landmarks
    return lm[HT.INDEX_TIP][0], lm[HT.INDEX_TIP][1]
