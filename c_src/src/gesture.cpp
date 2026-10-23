/*
 * gesture.cpp — skin segmentation + convexity-defect finger counting
 *
 * See gesture.h for a full walkthrough of the algorithm.
 */

#include "gesture.h"
#include <opencv2/geometry/2d.hpp>   /* convexHull, convexityDefects, contourArea, boundingRect — moved here in OpenCV 5 */
#include <cmath>
#include <vector>
#include <algorithm>

/* =========================================================================
 * SECTION 1 — SKIN SEGMENTATION
 *
 * Human skin in HSV colour space occupies:
 *   H (hue)        0–25  AND  160–180  (reds/oranges, plus wrap-around)
 *   S (saturation) 30–170             (not too grey, not too vivid)
 *   V (value)      80–255             (not too dark)
 *
 * These ranges work for a wide range of skin tones under indoor lighting.
 * You may need to widen H or lower the V floor for darker skin tones or
 * dim environments.
 * ========================================================================= */

static const cv::Scalar SKIN_LOW1  (  0,  30,  80);   /* warm side       */
static const cv::Scalar SKIN_HIGH1 ( 25, 170, 255);
static const cv::Scalar SKIN_LOW2  (160,  30,  80);   /* red wrap-around */
static const cv::Scalar SKIN_HIGH2 (180, 170, 255);

/*
 * make_skin_mask — produce a binary mask of skin pixels.
 *
 * Morphological "opening" (erode then dilate) removes isolated noise
 * pixels while preserving the overall hand shape.
 */
static cv::Mat make_skin_mask(const cv::Mat &bgr)
{
    /* 1. Convert colour space: BGR → HSV */
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    /* 2. Threshold the two skin-hue ranges and OR them together */
    cv::Mat mask1, mask2, mask;
    cv::inRange(hsv, SKIN_LOW1, SKIN_HIGH1, mask1);
    cv::inRange(hsv, SKIN_LOW2, SKIN_HIGH2, mask2);
    cv::bitwise_or(mask1, mask2, mask);

    /* 3. Morphological opening — removes tiny noise blobs.
     *    Kernel size 5×5 removes specks up to ~5 px in diameter.        */
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                               cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,  kernel);

    /* 4. Morphological closing — fills small holes inside the hand.     */
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    return mask;
}

/* =========================================================================
 * SECTION 2 — CONTOUR EXTRACTION
 * ========================================================================= */

/*
 * find_hand_contour — return the largest contour in the mask.
 *
 * RETR_EXTERNAL keeps only outermost contours (ignores holes inside the
 * hand like the gaps between fingers, which are filled by morphological
 * closing anyway).
 *
 * CHAIN_APPROX_NONE keeps every contour pixel — important because
 * cv::convexityDefects requires a dense contour representation.
 *
 * Returns false if no suitable contour was found.
 */
static bool find_hand_contour(const cv::Mat &mask,
                               std::vector<cv::Point> &out_contour)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    if (contours.empty()) return false;

    /* Pick the largest by area */
    int best = 0;
    double best_area = 0;
    for (int i = 0; i < (int)contours.size(); ++i) {
        double a = cv::contourArea(contours[i]);
        if (a > best_area) { best_area = a; best = i; }
    }

    if (best_area < CFG_MIN_HAND_AREA) return false;   /* too small       */

    out_contour = std::move(contours[best]);
    return true;
}

/* =========================================================================
 * SECTION 3 — FINGER COUNTING
 * ========================================================================= */

/*
 * count_fingers — count raised fingers using convexity defects.
 *
 * Parameters
 *   contour  : the hand contour (dense, from CHAIN_APPROX_NONE)
 *   bbox     : bounding rectangle of the contour
 *
 * Returns an integer 0–5.
 */
static int count_fingers(const std::vector<cv::Point> &contour,
                          const cv::Rect               &bbox)
{
    /* --- Convex hull (index form required for convexityDefects) --- */
    std::vector<int> hull_idx;
    cv::convexHull(contour, hull_idx, false, true);

    /* convexityDefects needs at least 4 hull points */
    if ((int)hull_idx.size() < 4) return 0;

    /* --- Convexity defects --- */
    std::vector<cv::Vec4i> defects;
    cv::convexityDefects(contour, hull_idx, defects);

    /*
     * palm_radius — rough estimate of the palm size.
     * We use it as a scale for depth thresholding: defects shallower
     * than 30% of the palm radius are wrinkles / noise, not valleys.
     */
    float palm_radius = std::min(bbox.width, bbox.height) * 0.35f;

    int valleys = 0;

    for (const cv::Vec4i &d : defects) {
        /* Each defect is stored as (start_idx, end_idx, far_idx, depth×256) */
        cv::Point start  = contour[d[0]];
        cv::Point end    = contour[d[1]];
        cv::Point far    = contour[d[2]];
        float     depth  = d[3] / 256.0f;   /* convert back to pixels      */

        /* --- Filter 1: depth threshold --- */
        if (depth < palm_radius * 0.30f) continue;

        /*
         * --- Filter 2: angle at the valley point ---
         *
         * Draw vectors from the valley (far) to the two ridge tops
         * (start and end).  The angle between them is acute (<90°) for
         * genuine finger valleys; obtuse for random hull artefacts.
         *
         * Law of cosines:  cos θ = (a² + b² - c²) / (2ab)
         *   a = |far → end|
         *   b = |far → start|
         *   c = |start → end|
         */
        float a = (float)cv::norm(end   - far);
        float b = (float)cv::norm(start - far);
        float c = (float)cv::norm(start - end);

        if (a < 1.0f || b < 1.0f) continue;   /* degenerate defect       */

        float cos_theta = (a*a + b*b - c*c) / (2.0f * a * b);
        cos_theta = std::max(-1.0f, std::min(1.0f, cos_theta)); /* clamp   */
        float theta = acosf(cos_theta);

        if (theta < (float)M_PI / 2.0f)        /* acute angle → real valley */
            ++valleys;
    }

    /*
     * valleys → finger_count conversion:
     *
     *   0 valleys: either a fist OR a single pointing finger.
     *   Distinguish by the ratio of contour area to convex hull area.
     *   A fist is compact (ratio ≈ 1.0); a pointing finger is elongated
     *   (ratio < ~0.85 because the finger creates a protrusion).
     */
    if (valleys == 0) {
        std::vector<cv::Point> hull_pts;
        cv::convexHull(contour, hull_pts);
        double hull_area    = cv::contourArea(hull_pts);
        double contour_area = cv::contourArea(contour);
        float  ratio        = (hull_area > 1.0) ? (float)(contour_area / hull_area) : 1.0f;
        return (ratio > 0.85f) ? 0 : 1;
    }

    return std::min(valleys + 1, 5);
}

/* =========================================================================
 * SECTION 4 — PUBLIC API
 * ========================================================================= */

GestureResult gesture_detect(const cv::Mat &frame_bgr)
{
    GestureResult result = {};
    result.norm_x = 0.5f;
    result.norm_y = 0.5f;

    int W = frame_bgr.cols;
    int H = frame_bgr.rows;

    /* --- Skin mask --- */
    cv::Mat mask = make_skin_mask(frame_bgr);

    /* --- Find hand contour --- */
    std::vector<cv::Point> contour;
    if (!find_hand_contour(mask, contour)) {
        result.gesture = GESTURE_NONE;
        return result;
    }
    result.hand_detected = true;

    cv::Rect bbox = cv::boundingRect(contour);

    /* --- Cursor position: topmost point of the contour ---
     *
     * The topmost pixel is approximately the highest fingertip.
     * We normalise to [CFG_MARGIN, 1-CFG_MARGIN] then remap to [0,1]
     * so the cursor can reach the screen edges without cramping the hand
     * into the frame corners.
     */
    cv::Point top_pt = *std::min_element(contour.begin(), contour.end(),
        [](const cv::Point &a, const cv::Point &b){ return a.y < b.y; });

    float raw_x = (float)(bbox.x + bbox.width  / 2) / W;   /* horizontal: centroid */
    float raw_y = (float)top_pt.y / H;                      /* vertical:   tip      */

    /* Remap from [MARGIN, 1-MARGIN] → [0, 1] and clamp */
    auto remap = [](float v) -> float {
        v = (v - CFG_MARGIN) / (1.0f - 2.0f * CFG_MARGIN);
        return std::max(0.0f, std::min(1.0f, v));
    };
    result.norm_x = remap(raw_x);
    result.norm_y = remap(raw_y);

    /* --- Count fingers --- */
    int fingers = count_fingers(contour, bbox);
    result.finger_count = fingers;

    /* --- Map finger count → Gesture --- */
    switch (fingers) {
        case 0:  result.gesture = GESTURE_DRAG;          break;
        case 1:  result.gesture = GESTURE_POINTER;       break;
        case 2:  result.gesture = GESTURE_SCROLL;        break;
        case 3:  result.gesture = GESTURE_SWITCH_WINDOW; break;
        case 4:  result.gesture = GESTURE_MINIMIZE;      break;
        default: result.gesture = GESTURE_MAXIMIZE;      break;  /* 5+ */
    }

    return result;
}

/* -------------------------------------------------------------------------
 * gesture_draw_debug — visualise skin mask, hull, and defects on the frame.
 * Useful when calibrating skin colour thresholds or debugging misfires.
 * ------------------------------------------------------------------------- */
void gesture_draw_debug(cv::Mat &frame, const GestureResult &result)
{
    if (!result.hand_detected) return;

    cv::Mat mask = make_skin_mask(frame);

    std::vector<cv::Point> contour;
    if (!find_hand_contour(mask, contour)) return;

    /* Draw contour in green */
    std::vector<std::vector<cv::Point>> cs = {contour};
    cv::drawContours(frame, cs, 0, cv::Scalar(0, 255, 0), 2);

    /* Draw convex hull in blue */
    std::vector<cv::Point> hull;
    cv::convexHull(contour, hull);
    std::vector<std::vector<cv::Point>> hs = {hull};
    cv::drawContours(frame, hs, 0, cv::Scalar(255, 100, 0), 2);

    /* Draw defect far-points as red dots */
    std::vector<int> hull_idx;
    cv::convexHull(contour, hull_idx, false, true);
    if ((int)hull_idx.size() >= 4) {
        std::vector<cv::Vec4i> defects;
        cv::convexityDefects(contour, hull_idx, defects);
        for (const cv::Vec4i &d : defects) {
            cv::circle(frame, contour[d[2]], 6, cv::Scalar(0, 0, 255), -1);
        }
    }

    /* Finger count label */
    cv::putText(frame,
                "fingers=" + std::to_string(result.finger_count),
                cv::Point(10, 20),
                cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(0, 255, 255), 2);
}
