/*
 * gesture.h — find the hand, its fingertip, and whether it is a fist
 *
 * PIPELINE (per frame, no neural network)
 * =======================================
 *
 *  1. SKIN MASK
 *     BGR → HSV, rotate hue so red is contiguous, keep pixels inside the
 *     calibrated SkinRange. Result: white = skin, black = everything else.
 *
 *  2. REMOVE THE FACE
 *     Your face is skin-coloured and usually bigger than your hand, so it
 *     would "win". A Haar cascade face detector finds the face, and we
 *     black out that rectangle (extended down to cover the neck).
 *
 *  3. CLEAN UP
 *     Morphological open (removes specks) + close (fills holes).
 *
 *  4. LARGEST BLOB = HAND
 *     cv::findContours traces every white blob; the biggest is the hand.
 *
 *  5. FINGERTIP = TOPMOST POINT
 *     Your arm enters from below, so the highest point of the blob is the
 *     tip of your highest finger. This is what drives the cursor.
 *
 *  6. POSE FROM SOLIDITY
 *     solidity = blob area / convex hull area.
 *       Fist           → compact, almost convex     → solidity ≈ 0.9+
 *       Pointing/open  → fingers leave gaps in hull → solidity lower
 *     solidity > CFG_FIST_SOLIDITY → FIST, else POINT.
 *     Press D in the preview to see the live solidity value and tune it.
 */

#pragma once
#include <opencv2/core.hpp>
#include <vector>
#include "types.h"

/* Extra data for the debug overlay (key D). Not needed for control. */
struct GestureDebug {
    cv::Mat                mask;      /* final skin mask, CV_8U             */
    std::vector<cv::Point> contour;   /* hand outline, processing pixels    */
    std::vector<cv::Point> hull;      /* convex hull of the hand            */
    cv::Rect               face;      /* removed face area (may be empty)   */
};

/* Load the face detector. Call once at startup. Returns false if the
 * cascade file is missing (detection still works, just without face removal). */
bool gesture_init(void);

/* Analyse one mirrored BGR frame. `dbg` may be NULL. */
HandResult gesture_detect(const cv::Mat &frame_bgr,
                          const SkinRange &skin,
                          GestureDebug *dbg);

/* Measure the skin colour inside `roi` and return a SkinRange around it.
 * Used by calibration while your hand covers the box on screen. */
SkinRange gesture_sample_skin(const cv::Mat &frame_bgr, const cv::Rect &roi);

/* Reasonable starting range for most skin tones under indoor light. */
SkinRange gesture_default_skin(void);
