#!/usr/bin/env python3
"""
Gesture Control — entry point.
Run:  python main.py
      python main.py --no-window   (headless, no preview)
      python main.py --camera 1    (use a different camera index)

Press Q in the preview window to quit.
"""
import argparse
import sys
import cv2
import numpy as np

from gesture_control import (
    HandTracker, Gesture, recognize, index_tip_position,
    WindowController, config,
)


GESTURE_LABELS = {
    Gesture.NONE: ("IDLE", (128, 128, 128)),
    Gesture.POINTER: ("POINTER  — cursor mode", (0, 255, 100)),
    Gesture.CLICK: ("CLICK  — left click", (0, 200, 255)),
    Gesture.DRAG: ("DRAG  — hold & drag", (0, 120, 255)),
    Gesture.SCROLL: ("SCROLL  — scroll", (255, 200, 0)),
    Gesture.SWITCH_WINDOW: ("SWITCH WINDOW  — Cmd+Tab", (200, 0, 255)),
    Gesture.MINIMIZE: ("MINIMIZE  — Cmd+M", (0, 80, 255)),
    Gesture.MAXIMIZE: ("MAXIMIZE  — full screen", (0, 255, 200)),
}

LEGEND = [
    "☝  Index only  → POINTER (move cursor)",
    "🤌  Pinch        → CLICK",
    "✊  Fist         → DRAG",
    "✌  Peace        → SCROLL",
    "🤟  3 fingers    → SWITCH WINDOW",
    "🖖  4 fingers    → MINIMIZE",
    "🖐  Open hand   → MAXIMIZE",
]


def _remap(val: float, src_lo: float, src_hi: float) -> float:
    """Clamp val from [src_lo, src_hi] then remap to [0, 1]."""
    val = max(src_lo, min(src_hi, val))
    return (val - src_lo) / (src_hi - src_lo)


def _draw_hud(frame, gesture: Gesture, fps: float, show_legend: bool):
    h, w = frame.shape[:2]
    label, colour = GESTURE_LABELS[gesture]

    # Gesture pill at top
    cv2.rectangle(frame, (10, 10), (w - 10, 50), (0, 0, 0), -1)
    cv2.rectangle(frame, (10, 10), (w - 10, 50), colour, 2)
    cv2.putText(frame, label, (20, 38),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, colour, 2)

    # FPS
    cv2.putText(frame, f"FPS {fps:.0f}", (w - 90, h - 15),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)

    # Legend toggle hint
    cv2.putText(frame, "L = toggle legend", (10, h - 15),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (150, 150, 150), 1)

    if show_legend:
        overlay = frame.copy()
        cv2.rectangle(overlay, (10, 55), (370, 55 + len(LEGEND) * 24 + 10),
                      (0, 0, 0), -1)
        cv2.addWeighted(overlay, 0.6, frame, 0.4, 0, frame)
        for i, line in enumerate(LEGEND):
            cv2.putText(frame, line, (18, 75 + i * 24),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (220, 220, 220), 1)

    # Mapping zone border
    zone_x1 = int(config.MARGIN_LEFT * w)
    zone_x2 = int(config.MARGIN_RIGHT * w)
    zone_y1 = int(config.MARGIN_TOP * h)
    zone_y2 = int(config.MARGIN_BOTTOM * h)
    cv2.rectangle(frame, (zone_x1, zone_y1), (zone_x2, zone_y2),
                  (60, 60, 60), 1)


def run(camera: int, show_window: bool):
    tracker = HandTracker()
    controller = WindowController()

    cap = cv2.VideoCapture(camera)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.FRAME_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.FRAME_H)

    if not cap.isOpened():
        print(f"ERROR: cannot open camera {camera}", file=sys.stderr)
        sys.exit(1)

    print("Gesture control running. Press Q (in preview) or Ctrl-C to quit.")

    import time
    prev_time = time.time()
    show_legend = True

    while True:
        ok, frame = cap.read()
        if not ok:
            break

        frame = cv2.flip(frame, 1)  # mirror so left hand = left on screen
        hand, annotated = tracker.process(frame)

        gesture = recognize(hand, config.PINCH_THRESHOLD)

        norm_x, norm_y = 0.5, 0.5
        scroll_y = None

        if hand is not None:
            raw_x, raw_y = index_tip_position(hand)
            # Flip x because we mirrored the frame
            norm_x = _remap(raw_x, config.MARGIN_LEFT, config.MARGIN_RIGHT)
            norm_y = _remap(raw_y, config.MARGIN_TOP, config.MARGIN_BOTTOM)
            scroll_y = raw_y

        controller.update(gesture, norm_x, norm_y, scroll_y)

        # FPS calc
        now = time.time()
        fps = 1.0 / (now - prev_time + 1e-9)
        prev_time = now

        if show_window:
            _draw_hud(annotated, gesture, fps, show_legend)
            cv2.imshow("Gesture Control", annotated)
            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break
            if key == ord("l"):
                show_legend = not show_legend

    cap.release()
    tracker.close()
    if show_window:
        cv2.destroyAllWindows()
    print("Stopped.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="OpenCV hand-gesture window controller")
    parser.add_argument("--camera", type=int, default=config.CAMERA_INDEX,
                        help="Camera index (default 0)")
    parser.add_argument("--no-window", action="store_true",
                        help="Run headless (no preview window)")
    args = parser.parse_args()
    run(args.camera, show_window=not args.no_window)
