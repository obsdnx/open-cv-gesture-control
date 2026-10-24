/*
 * types.h — shared types and tuning constants
 *
 * ARCHITECTURE OVERVIEW
 * =====================
 *
 *  camera.mm       — grabs live BGR frames from the built-in camera
 *        │
 *        ▼
 *  gesture.cpp     — finds the hand (skin colour, face removed) and reports
 *        │           fingertip position + pose (POINT or FIST)
 *        ▼
 *  controller.cpp  — maps fingertip → screen pixel, moves the cursor,
 *        │           grabs/moves the window under the cursor on FIST
 *        ▼
 *  main.cpp        — runs the per-frame loop, draws the preview
 *
 *  calibration.cpp — guided setup (key C): samples your skin colour and
 *                    records how far your fingertip reaches, saved to disk
 */

#pragma once
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Pose — what the hand is doing this frame.
 *   POINT : hand open / finger pointing → cursor follows fingertip
 *   FIST  : hand closed                  → grab window under cursor
 * ----------------------------------------------------------------------- */
typedef enum {
    POSE_NONE  = 0,   /* no hand found */
    POSE_POINT = 1,
    POSE_FIST  = 2,
} Pose;

/* -----------------------------------------------------------------------
 * HandResult — output of gesture_detect() for one frame.
 * All positions are normalised to [0,1] in the (mirrored) camera frame:
 * (0,0) = top-left of the preview, (1,1) = bottom-right.
 * ----------------------------------------------------------------------- */
typedef struct {
    bool  hand_detected;
    Pose  pose;          /* raw, per-frame (controller debounces it)        */
    float tip_x, tip_y;  /* topmost point of the hand = fingertip           */
    float palm_x, palm_y;/* centre of mass of the hand                      */
    float solidity;      /* hand area / convex hull area (fist ≈ 1.0)       */
} HandResult;

/* -----------------------------------------------------------------------
 * SkinRange — HSV thresholds for "this pixel is skin".
 *
 * Hue is stored ROTATED by +90 (mod 180). Skin hue sits around red, which
 * wraps from 179 back to 0 in OpenCV's hue scale. Rotating moves red to
 * the middle (≈90) so one contiguous range [h_lo, h_hi] covers it.
 * ----------------------------------------------------------------------- */
typedef struct {
    int h_lo, h_hi;   /* rotated hue, 0–179 */
    int s_lo, s_hi;   /* saturation,  0–255 */
    int v_lo, v_hi;   /* brightness,  0–255 */
} SkinRange;

/* -----------------------------------------------------------------------
 * ActiveBox — the part of the camera frame that maps to the whole screen.
 * Fingertip at (x0,y0) → screen top-left; at (x1,y1) → bottom-right.
 * Calibration sets this to the extremes you actually reached.
 * ----------------------------------------------------------------------- */
typedef struct {
    float x0, y0, x1, y1;
} ActiveBox;

typedef struct {
    SkinRange skin;
    ActiveBox box;
} Calibration;

/* -----------------------------------------------------------------------
 * Tuning constants
 * ----------------------------------------------------------------------- */
#define CFG_FRAME_W         320    /* processing width; height follows the
                                      camera's aspect ratio (no squish)     */
#define CFG_PREVIEW_SCALE   2      /* preview window = processing size × 2  */

#define CFG_MIN_HAND_FRAC   0.015f /* hand must cover ≥1.5% of the frame    */
#define CFG_FIST_SOLIDITY   0.86f  /* solidity above this = FIST            */
#define CFG_POSE_FRAMES     3      /* frames a pose must hold to switch     */
#define CFG_LOST_FRAMES     10     /* frames without a hand before release  */

/* Cursor smoothing: small movements are smoothed heavily (kills jitter),
 * fast movements pass through almost directly (no lag).                   */
#define CFG_SMOOTH_MIN      0.15f  /* EMA factor for tiny movements         */
#define CFG_SMOOTH_FAST_PX  150.0f /* movement (px) that gets factor 1.0    */
#define CFG_DEADZONE_PX     2.0f   /* ignore moves smaller than this        */
