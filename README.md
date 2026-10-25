# open-cv-gesture-control

Control your Mac's cursor, clicks and desktops with hand gestures from the built-in webcam.
Written in C++17 + Objective-C++ (camera), using OpenCV 5 to run Google's MediaPipe hand CNNs. No Python at runtime.

## Gestures

| Gesture | Action |
|---|---|
| ☝️ Point with your index finger | Cursor follows your fingertip |
| ✊ Make a fist | Press and hold the mouse button (like pressing the trackpad); move to drag, open to release |
| 👌 OK sign with your **other** hand | Left click |
| 🖐️ Open hand, swipe sideways | Switch desktop (Ctrl+← / Ctrl+→) |
| No hand on camera | Nothing — the trackpad works normally |

Touching the trackpad while a hand is visible pauses camera control for 1 second.

## Quick start

Requirements: macOS, Homebrew, Xcode Command Line Tools.

```bash
brew install opencv
cd c_src
make            # first run downloads the hand models (~8 MB)
./gesture_control
```

Run it from **Terminal.app** (or iTerm). macOS asks for two permissions the first time:

- **Camera** — System Settings → Privacy & Security → Camera → your terminal
- **Accessibility** (to move the cursor) — System Settings → Privacy & Security → Accessibility → your terminal

Desktop switching needs at least two desktops (Mission Control → **+**) and the default
"Move left/right a space" shortcuts enabled (System Settings → Keyboard → Keyboard Shortcuts → Mission Control).

### Options and keys

```bash
./gesture_control --list        # list cameras
./gesture_control --camera 1    # use a different camera
./gesture_control --calibrate   # redo calibration at start
```

| Key (preview window) | Does |
|---|---|
| `C` | Calibrate: point and trace a big rectangle for 8 s |
| `[` / `]` | Snappier / steadier cursor (saved) |
| `D` | Debug overlay (pose values, timing) |
| `Q` | Quit |

Calibration and steadiness are saved to `~/.gesture_control_calibration`.

## How it works

```
camera.mm        AVFoundation capture — picks the built-in camera (not an iPhone
     │           Continuity Camera), drops late frames, keeps the real aspect ratio
     ▼
hand_model.cpp   Two MediaPipe CNNs via OpenCV DNN:
     │             palm detector (192×192, SSD anchors) → rotated crop →
     │             landmark model (224×224) → 21 joints + left/right per hand.
     │           Tracks up to 2 hands between frames without re-detecting.
     ▼
gesture.cpp      Joint geometry → POINT / FIST / OK / OPEN.
     │           Distance-based, so it works at any pointing angle.
     ▼
swipe.cpp        Fast sideways travel of an open hand; bridges motion-blur gaps
     │           and ignores the return stroke.
     ▼
controller.cpp   Picks the pointer hand and the click hand, maps the fingertip
     │           through the calibrated box to the screen, smooths it with a
     │           One Euro filter, posts CoreGraphics mouse/keyboard events.
     ▼
main.cpp         Per-frame loop, preview window, keys.
calibration.cpp  Reach box (3rd–97th percentile of fingertip samples) + settings file.
```

Every source file starts with a comment explaining its part of the pipeline.

## Project layout

```
c_src/                  the application (C++)
  include/              one header per module, documented
  src/                  implementation
  Makefile              build + `make models`
gesture_control/, main.py   original Python prototype (MediaPipe + pyautogui)
```

The project started as a Python prototype and was rewritten in C++ for lower latency and
tighter macOS integration; the prototype is kept for reference.

## Credits

Hand models: [MediaPipe Hand Landmarker](https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker) (Apache 2.0).
Smoothing: Casiez, Roussel & Vogel, *1€ Filter*, CHI 2012.
