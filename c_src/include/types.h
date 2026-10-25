/*
 * types.h — shared types used across all modules
 *
 * ARCHITECTURE OVERVIEW
 * =====================
 * The program has four layers, each in its own file:
 *
 *  camera.cpp      — grabs raw BGR frames from the webcam
 *        |
 *        v
 *  gesture.cpp     — skin-color segmentation + convex-hull finger counting
 *        |
 *        v
 *  controller.cpp  — translates a Gesture into macOS CoreGraphics events
 *        |
 *        v
 *  main.cpp        — ties everything together; runs the per-frame loop
 *
 * This header defines the data types that flow between those layers.
 * Everything is plain C structs and enums so the interfaces are simple
 * to read even if the implementation files use C++ (for OpenCV).
 */

#pragma once
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Gesture — the set of hand poses we recognise.
 *
 * Detection is done by counting "valleys" between raised fingers using
 * convexity defects on the hand contour.  No neural network is involved,
 * so it runs fast on CPU.
 *
 *  Fist  (0 fingers up) → DRAG   — hold the mouse button while moving
 *  Index (1 finger up)  → POINTER — move the cursor
 *  Peace (2 fingers up) → SCROLL  — scroll up/down
 *  Three (3 fingers up) → SWITCH_WINDOW — Cmd+Tab
 *  Four  (4 fingers up) → MINIMIZE      — Cmd+M
 *  Open  (5 fingers up) → MAXIMIZE      — Cmd+Ctrl+F
 * ----------------------------------------------------------------------- */
typedef enum {
    GESTURE_NONE          = 0,
    GESTURE_POINTER       = 1,   /* index only  — cursor mode            */
    GESTURE_DRAG          = 2,   /* fist        — click-hold drag         */
    GESTURE_SCROLL        = 3,   /* peace sign  — scroll wheel            */
    GESTURE_SWITCH_WINDOW = 4,   /* 3 fingers   — Cmd+Tab                */
    GESTURE_MINIMIZE      = 5,   /* 4 fingers   — Cmd+M                  */
    GESTURE_MAXIMIZE      = 6,   /* open hand   — Cmd+Ctrl+F             */
} Gesture;

/* -----------------------------------------------------------------------
 * GestureResult — everything gesture.cpp tells the controller.
 *
 *  gesture       — which pose was detected
 *  norm_x/norm_y — cursor position, normalised to [0, 1] inside the
 *                  active region of the camera frame.  (0,0) = top-left.
 *  finger_count  — raw finger count (0-5) for debug display
 *  hand_detected — false when no hand contour was found
 * ----------------------------------------------------------------------- */
typedef struct {
    Gesture gesture;
    float   norm_x;
    float   norm_y;
    int     finger_count;
    bool    hand_detected;
} GestureResult;

/* -----------------------------------------------------------------------
 * ControllerState — mutable state the controller carries between frames.
 *
 * The controller uses this to implement:
 *   - cursor smoothing  (exponential moving average)
 *   - gesture debounce  (require N consecutive identical frames)
 *   - drag state        (track whether mouseDown is currently held)
 *   - scroll reference  (store the y position where scrolling started)
 * ----------------------------------------------------------------------- */
typedef struct {
    float   cursor_x;          /* smoothed screen X, pixels              */
    float   cursor_y;          /* smoothed screen Y, pixels              */
    bool    is_dragging;       /* true while mouseDown is held            */
    Gesture prev_gesture;      /* gesture seen in the previous frame      */
    int     gesture_hold;      /* consecutive frames with same gesture    */
    float   scroll_ref_y;      /* normalised Y when scrolling started     */
    bool    scroll_active;     /* true once a scroll session has begun    */
} ControllerState;

/* -----------------------------------------------------------------------
 * Config — compile-time constants, editable here.
 * ----------------------------------------------------------------------- */
#define CFG_FRAME_W        320    /* capture width  (pixels)              */
#define CFG_FRAME_H        240    /* capture height (pixels)              */
#define CFG_FPS            30     /* target frames per second             */
#define CFG_SMOOTHING      0.25f  /* cursor EMA factor  (0=none, 1=jump) */
#define CFG_HOLD_FRAMES    3      /* frames before one-shot action fires  */
#define CFG_SCROLL_SPEED   10.0f  /* scroll lines per unit dy             */
#define CFG_MARGIN         0.10f  /* dead-zone margin at camera edges     */
#define CFG_MIN_HAND_AREA  3000   /* minimum contour px² to be a hand    */
