/*
 * types.h — shared types and tuning constants
 *
 * ARCHITECTURE OVERVIEW
 * =====================
 *
 *  camera.mm       — live BGR frames from the built-in camera
 *        │
 *        ▼
 *  hand_model.cpp  — CNN: finds up to 2 hands, 21 joints each, left/right
 *        │
 *        ▼
 *  gesture.cpp     — rules on the joints: POINT / FIST / OK per hand
 *        │
 *        ▼
 *  controller.cpp  — picks the POINTER hand and the CLICK hand, then:
 *        │             pointer index finger → cursor position
 *        │             pointer fist         → mouse button held (drag)
 *        │             other hand OK sign   → left click
 *        │             no hand              → nothing (trackpad works)
 *        ▼
 *  main.cpp        — per-frame loop, preview window, keys
 *
 *  calibration.cpp — reach box + steadiness, saved to disk (key C)
 */

#pragma once
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Pose — what one hand is doing, decided from its 21 joints.
 * ----------------------------------------------------------------------- */
typedef enum {
    POSE_NONE  = 0,   /* unclear shape — ignored                          */
    POSE_POINT = 1,   /* index finger out, not all four fingers out       */
    POSE_FIST  = 2,   /* all four fingers curled                          */
    POSE_OK    = 3,   /* thumb tip touching index tip, other fingers out  */
    POSE_OPEN  = 4,   /* all four fingers out — flat hand, used to swipe  */
} Pose;

/* -----------------------------------------------------------------------
 * HandInfo — one hand, reduced to what the controller needs.
 * Positions are normalised to [0,1] in the mirrored camera frame:
 * (0,0) = top-left of the preview, (1,1) = bottom-right.
 * ----------------------------------------------------------------------- */
typedef struct {
    bool  is_right;        /* the person's right hand                    */
    Pose  pose;
    float tip_x, tip_y;    /* index fingertip (joint 8)                  */
    float palm_x, palm_y;  /* centre of the palm                         */
    float pinch;           /* thumb-tip↔index-tip gap ÷ palm size        */
} HandInfo;

typedef struct {
    int      count;        /* 0, 1 or 2 */
    HandInfo hand[2];
} HandsResult;

/* -----------------------------------------------------------------------
 * ActiveBox — the part of the camera frame that maps to the whole screen.
 * Index fingertip at (x0,y0) → screen top-left; at (x1,y1) → bottom-right.
 * ----------------------------------------------------------------------- */
typedef struct {
    float x0, y0, x1, y1;
} ActiveBox;

/* -----------------------------------------------------------------------
 * Steadiness — One Euro filter settings (see controller.h).
 *   min_cutoff : lower = steadier when still, slightly laggier.
 *                Adjust live with [ and ] in the preview.
 *   beta       : how quickly smoothing relaxes when you move fast.
 * ----------------------------------------------------------------------- */
typedef struct {
    float min_cutoff;   /* Hz */
    float beta;
} Steadiness;

typedef struct {
    ActiveBox  box;
    Steadiness steady;
} Calibration;

/* -----------------------------------------------------------------------
 * Tuning constants
 * ----------------------------------------------------------------------- */
#define CFG_FRAME_W         640    /* processing width (height keeps aspect) */
#define CFG_PREVIEW_SCALE   1      /* preview window = processing size × 1   */

#define CFG_POSE_FRAMES     3      /* frames a pose must hold to count       */
#define CFG_LOST_FRAMES     6      /* frames without the hand before letting
                                      go of the mouse button                 */

/* Gesture thresholds (relative to palm size, so distance from camera
 * doesn't matter). Press D in the preview to see live values. */
#define CFG_FINGER_OUT      1.10f  /* tip this much farther from the wrist
                                      than the middle joint = extended       */
#define CFG_OK_PINCH        0.65f  /* thumb–index gap below this = touching.
                                      (Fist also scores ~0.5 — OK is told
                                      apart by the other 3 fingers being out) */

/* Trackpad takeover: if the cursor moves by more than this many pixels
 * without us moving it, you're using the trackpad — camera control pauses. */
#define CFG_TRACKPAD_PX     25.0f
#define CFG_TRACKPAD_PAUSE  1.0    /* seconds */

/* Swipe = open hand moving sideways fast (see swipe.h). */
#define CFG_SWIPE_DIST      0.30f  /* fraction of frame width to travel     */
#define CFG_SWIPE_TIME      0.60   /* …within this many seconds             */
#define CFG_SWIPE_SAME_GAP  0.50   /* min time between same-direction swipes */
#define CFG_SWIPE_RETURN    1.00   /* opposite direction blocked this long
                                      (that's your hand swinging back)      */
#define CFG_SWIPE_NATURAL   1      /* 1: like the trackpad — swipe left goes
                                      to the desktop on the right.
                                      0: swipe left goes to the left one.   */

/* One Euro filter defaults (tunable live with [ and ]). */
#define CFG_EURO_MIN_CUTOFF 0.6f
#define CFG_EURO_BETA       0.004f
#define CFG_EURO_D_CUTOFF   1.0f
