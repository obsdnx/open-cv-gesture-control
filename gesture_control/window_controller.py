import subprocess
import pyautogui
from .gestures import Gesture
from . import config

pyautogui.FAILSAFE = False
pyautogui.PAUSE = 0


class WindowController:
    """Translates recognised gestures into macOS window actions."""

    def __init__(self):
        self._cursor_x = config.SCREEN_W / 2
        self._cursor_y = config.SCREEN_H / 2
        self._is_dragging = False
        self._scroll_ref_y: float | None = None
        self._prev_gesture = Gesture.NONE
        self._gesture_count = 0

    def update(self, gesture: Gesture, norm_x: float, norm_y: float,
               norm_scroll_y: float | None = None):
        """
        norm_x, norm_y: normalised [0,1] position of the index fingertip,
                        already remapped for screen coords (margins removed).
        """
        # Debounce: action gestures require GESTURE_HOLD_FRAMES consecutive frames
        if gesture == self._prev_gesture:
            self._gesture_count += 1
        else:
            self._gesture_count = 1
        self._prev_gesture = gesture

        target_x = norm_x * config.SCREEN_W
        target_y = norm_y * config.SCREEN_H

        if gesture in (Gesture.POINTER, Gesture.CLICK, Gesture.DRAG, Gesture.SCROLL):
            # Smooth cursor towards target
            self._cursor_x += (target_x - self._cursor_x) * config.SMOOTHING
            self._cursor_y += (target_y - self._cursor_y) * config.SMOOTHING

        if gesture == Gesture.POINTER:
            self._release_drag()
            pyautogui.moveTo(int(self._cursor_x), int(self._cursor_y))

        elif gesture == Gesture.CLICK:
            self._release_drag()
            if self._gesture_count == config.GESTURE_HOLD_FRAMES:
                pyautogui.click(int(self._cursor_x), int(self._cursor_y))

        elif gesture == Gesture.DRAG:
            if not self._is_dragging:
                pyautogui.mouseDown(int(self._cursor_x), int(self._cursor_y))
                self._is_dragging = True
            else:
                dx = abs(self._cursor_x - pyautogui.position().x)
                dy = abs(self._cursor_y - pyautogui.position().y)
                if dx > config.DRAG_DEADZONE or dy > config.DRAG_DEADZONE:
                    pyautogui.moveTo(int(self._cursor_x), int(self._cursor_y))

        elif gesture == Gesture.SCROLL:
            self._release_drag()
            if norm_scroll_y is not None:
                if self._scroll_ref_y is None:
                    self._scroll_ref_y = norm_scroll_y
                delta = self._scroll_ref_y - norm_scroll_y
                if abs(delta) > 0.01:
                    pyautogui.scroll(int(delta * config.SCROLL_SENSITIVITY))
                    self._scroll_ref_y = norm_scroll_y

        elif gesture == Gesture.SWITCH_WINDOW:
            self._release_drag()
            if self._gesture_count == config.GESTURE_HOLD_FRAMES:
                pyautogui.hotkey("command", "tab")

        elif gesture == Gesture.MINIMIZE:
            self._release_drag()
            if self._gesture_count == config.GESTURE_HOLD_FRAMES:
                pyautogui.hotkey("command", "m")

        elif gesture == Gesture.MAXIMIZE:
            self._release_drag()
            if self._gesture_count == config.GESTURE_HOLD_FRAMES:
                # Toggle macOS full screen
                pyautogui.hotkey("command", "ctrl", "f")

        else:
            self._release_drag()
            self._scroll_ref_y = None

    def _release_drag(self):
        if self._is_dragging:
            pyautogui.mouseUp()
            self._is_dragging = False
