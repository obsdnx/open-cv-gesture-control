/*
 * gesture.h — hand detection and gesture recognition API
 *
 * HOW GESTURE DETECTION WORKS (no neural network)
 * ================================================
 * Instead of a deep learning model we use classical computer vision:
 *
 *  Step 1 — SKIN SEGMENTATION
 *    Convert the BGR frame to HSV colour space.  Skin tone occupies a
 *    consistent hue band (roughly H=0-25 and the wrap-around H=160-180).
 *    cv::inRange() creates a binary mask: white = skin, black = background.
 *    Morphological erode+dilate ("opening") removes noise speckles.
 *
 *  Step 2 — CONTOUR EXTRACTION
 *    cv::findContours() traces the boundary of every white blob.
 *    We keep only the largest one — that's the hand.
 *    Tiny blobs (area < CFG_MIN_HAND_AREA) are discarded.
 *
 *  Step 3 — CONVEX HULL + CONVEXITY DEFECTS
 *    The convex hull is the rubber-band shape stretched around the hand.
 *    Fingers poking out create "peaks"; the gaps between fingers create
 *    inward dents called "convexity defects".
 *
 *    Each defect is described by:
 *      start  — hull point where the dent begins
 *      end    — hull point where the dent ends
 *      far    — the deepest point of the dent (fingertip valley)
 *      depth  — how deep the dent is (in pixels × 256)
 *
 *  Step 4 — FINGER COUNTING
 *    For each defect we check two criteria:
 *      a) depth > 30% of palm radius  (filters shallow palm wrinkles)
 *      b) angle at the far point < 90° (finger valleys are acute)
 *    valid_valleys = defects that pass both tests
 *    finger_count  = valid_valleys + 1   (n valleys → n+1 fingers)
 *
 *    Special case: 0 valleys could mean a fist OR a single pointing
 *    finger.  We disambiguate by comparing contour area to hull area:
 *      ratio ≈ 1.0 → compact shape → FIST (0 fingers)
 *      ratio < 0.85 → elongated   → POINTER (1 finger)
 *
 *  Step 5 — CURSOR POSITION
 *    For cursor movement we use the topmost point of the hand contour
 *    (which corresponds to the raised fingertip) normalised to [0,1].
 */

#pragma once
#include <opencv2/opencv.hpp>
#include "types.h"

/*
 * gesture_detect — analyse a BGR frame and return a GestureResult.
 *
 *   frame : BGR image straight from the camera (NOT flipped yet;
 *           flipping happens in main before display).
 *
 * The function is stateless: it does not remember previous frames.
 * Debounce and smoothing live in controller.cpp.
 */
GestureResult gesture_detect(const cv::Mat &frame_bgr);

/*
 * gesture_draw_debug — overlay the skin mask, hull, and defect points
 * onto a copy of the frame.  Call this to visualise what the detector
 * "sees".  Only compiled in debug builds (controlled by CMake/CFLAGS).
 */
void gesture_draw_debug(cv::Mat &frame, const GestureResult &result);
