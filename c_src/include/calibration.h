/*
 * calibration.h — reach calibration (key C) + saved settings
 *
 * REACH (8 seconds)
 *   Point with your index finger and trace a big rectangle — all four
 *   corners of the area you can comfortably reach. Every fingertip
 *   position is recorded; the 3rd–97th percentile becomes the ActiveBox:
 *     fingertip at your top-right extreme → cursor at screen top-right.
 *   Percentiles (not min/max) mean a few glitchy frames can't stretch it.
 *
 *   BIGGER REACH = STEADIER CURSOR: the box is stretched over the whole
 *   screen, so a small box magnifies every tremor.
 *
 * SAVED TO ~/.gesture_control_calibration (plain text):
 *   box    <x0> <y0> <x1> <y1>
 *   filter <min_cutoff> <beta>
 * Loaded on every start; if missing, calibration starts by itself.
 */

#pragma once
#include <opencv2/core.hpp>
#include <vector>
#include "types.h"

typedef struct {
    bool   active;
    double start;                 /* seconds                              */
    float  min_x, min_y;          /* live extremes (drawn on the preview) */
    float  max_x, max_y;
    std::vector<float> xs, ys;    /* every fingertip sample               */
} CalSession;

Calibration calib_default(void);
bool calib_load(Calibration *cal);   /* false if no file */
void calib_save(const Calibration *cal);

void calib_start(CalSession *s);

/* Feed one frame. Returns true while calibrating (don't move the mouse).
 * `pointer` may be NULL when no hand is visible. Saves when finished. */
bool calib_step(CalSession *s, const HandInfo *pointer, Calibration *cal);

/* Instructions, countdown and live reach box on the preview. */
void calib_draw(const CalSession *s, cv::Mat &preview);
