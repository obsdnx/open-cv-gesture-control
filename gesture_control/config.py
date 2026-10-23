import pyautogui

SCREEN_W, SCREEN_H = pyautogui.size()

# Camera capture index (0 = built-in webcam)
CAMERA_INDEX = 0

# Frame display size (lower = faster MediaPipe processing)
FRAME_W = 320
FRAME_H = 240

# The region of the frame used for cursor mapping (avoids edges)
# Values are fractions of frame dimensions
MARGIN_LEFT = 0.1
MARGIN_RIGHT = 0.9
MARGIN_TOP = 0.1
MARGIN_BOTTOM = 0.9

# Pinch click threshold: normalised distance between thumb tip and index tip
PINCH_THRESHOLD = 0.05

# Minimum frames a gesture must be held before triggering an action
GESTURE_HOLD_FRAMES = 3

# Mouse smoothing: fraction of the way to move each frame (0=no move, 1=instant)
SMOOTHING = 0.25

# Scroll speed multiplier
SCROLL_SENSITIVITY = 8

# Drag deadzone in pixels — prevents micro-jitter when fist is held still
DRAG_DEADZONE = 4
