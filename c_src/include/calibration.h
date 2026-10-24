/*
 * calibration.h — guided two-step setup, started with key C
 *
 * STEP 1 — SKIN (3 seconds)
 *   A box appears in the middle of the preview. Hold your open palm so it
 *   fills the box. When the countdown ends we sample the colour inside it.
 *   This makes hand detection work for YOUR skin under YOUR lighting.
 *
 * STEP 2 — REACH (6 seconds)
 *   Point with one finger and touch all four corners of the area you can
 *   comfortably reach. We record the left/right/top/bottom extremes of
 *   your fingertip. Those extremes become the ActiveBox, so:
 *     fingertip at your top-right extreme → cursor at screen top-right.
 *
 * The result is saved to ~/.gesture_control_calibration and loaded on
 * every start. If the file doesn't exist, calibration starts by itself.
 */

#pragma once
#include <opencv2/core.hpp>
#include "types.h"

typedef enum {
    CAL_OFF   = 0,
    CAL_SKIN  = 1,
    CAL_REACH = 2,
} CalPhase;

typedef struct {
    CalPhase phase;
    double   phase_start;         /* seconds (cv::getTickCount based)   */
    float    min_x, min_y;        /* fingertip extremes seen in REACH   */
    float    max_x, max_y;
    int      samples;
} CalSession;

/* Defaults used before the first calibration. */
Calibration calib_default(void);

/* Load/save ~/.gesture_control_calibration. load returns false if missing. */
bool calib_load(Calibration *cal);
void calib_save(const Calibration *cal);

/* Begin the guided flow at STEP 1. */
void calib_start(CalSession *s);

/*
 * Advance the flow by one frame. Updates `cal` when a step completes and
 * saves to disk at the end. Returns true while calibration is running
 * (the caller should NOT move the mouse during that time).
 */
bool calib_step(CalSession *s, const cv::Mat &frame,
                const HandResult &hand, Calibration *cal);

/* Draw instructions, countdown, and the skin box onto the preview.
 * `scale` = preview size / processing size. */
void calib_draw(const CalSession *s, cv::Mat &preview, int scale);

/* The STEP 1 sampling box, in processing-frame pixels. */
cv::Rect calib_skin_box(int frame_w, int frame_h);
