# open-cv-gesture-control

Mouse and window control via hand gesture recognition using OpenCV and MediaPipe.

## File structure

```
gesture_control/
  config.py            — screen size, thresholds, smoothing constants
  hand_tracker.py      — MediaPipe Hands wrapper, returns 21 landmarks/frame
  gestures.py          — stateless gesture recognition from landmarks
  window_controller.py — translates gestures → macOS actions
main.py                — camera loop with HUD overlay
requirements.txt
```

## Gesture → Action map

| Hand shape | Action |
|---|---|
| ☝ Index only | **POINTER** — cursor follows index tip |
| 🤌 Pinch (thumb+index close) | **CLICK** — left click |
| ✊ Fist | **DRAG** — mouseDown + move |
| ✌ Peace (index+middle) | **SCROLL** — vertical hand movement |
| 🤟 3 fingers | **SWITCH WINDOW** — Cmd+Tab |
| 🖖 4 fingers (no thumb) | **MINIMIZE** — Cmd+M |
| 🖐 Open hand (all 5) | **MAXIMIZE** — Cmd+Ctrl+F |
