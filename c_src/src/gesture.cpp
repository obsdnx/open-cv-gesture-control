/*
 * gesture.cpp — skin mask → face removal → hand blob → fingertip + pose
 *
 * See gesture.h for the step-by-step explanation of the pipeline.
 */

#include "gesture.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>   /* convexHull, contourArea — moved here in OpenCV 5 */
#include <opencv2/xobjdetect.hpp>    /* CascadeClassifier — moved to xobjdetect in OpenCV 5 */
#include <algorithm>
#include <cmath>
#include <cstdio>

/* FACE_CASCADE is defined by the Makefile as the path to OpenCV's
 * bundled haarcascade_frontalface_default.xml. */
#ifndef FACE_CASCADE
#define FACE_CASCADE ""
#endif

/* =========================================================================
 * SECTION 1 — MODULE STATE
 * ========================================================================= */

static cv::CascadeClassifier g_face;          /* Haar face detector        */
static bool                  g_face_ok = false;
static cv::Rect              g_last_face;     /* cached between detections */
static int                   g_frame_no = 0;

/* Running the face detector every frame is wasteful — faces move slowly.
 * Detect every Nth frame and reuse the last rectangle in between. */
static const int FACE_EVERY_N = 5;

bool gesture_init(void)
{
    g_face_ok = g_face.load(FACE_CASCADE);
    if (!g_face_ok)
        fprintf(stderr, "[gesture] Face detector not loaded — face may be mistaken for a hand.\n");
    return g_face_ok;
}

/* =========================================================================
 * SECTION 2 — HSV WITH ROTATED HUE
 *
 * OpenCV hue runs 0–179 and skin sits around 0, so skin pixels appear at
 * both ~5 and ~175. Adding 90 (mod 180) moves that to ~95 and ~85 — one
 * contiguous range. A 256-entry lookup table does it in one pass.
 * ========================================================================= */

static cv::Mat to_rotated_hsv(const cv::Mat &bgr)
{
    static cv::Mat lut;
    if (lut.empty()) {
        lut.create(1, 256, CV_8U);
        for (int i = 0; i < 256; ++i)
            lut.at<uchar>(i) = (uchar)(i < 180 ? (i + 90) % 180 : i);
    }

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    std::vector<cv::Mat> ch;
    cv::split(hsv, ch);
    cv::LUT(ch[0], lut, ch[0]);       /* rotate hue channel only */
    cv::merge(ch, hsv);
    return hsv;
}

SkinRange gesture_default_skin(void)
{
    /* Original hue 0–25 and 160–179 → rotated 70–115. */
    return SkinRange{70, 115, 30, 175, 60, 255};
}

/* =========================================================================
 * SECTION 3 — SKIN SAMPLING (used by calibration)
 *
 * Take the pixels inside the box, compute mean and standard deviation of
 * each channel, and accept mean ± 2.5 σ. A minimum half-width stops the
 * range from becoming so narrow that a shadow drops your hand out.
 * ========================================================================= */

SkinRange gesture_sample_skin(const cv::Mat &frame_bgr, const cv::Rect &roi)
{
    cv::Mat hsv = to_rotated_hsv(frame_bgr(roi & cv::Rect(0, 0, frame_bgr.cols, frame_bgr.rows)));

    cv::Scalar mean, sd;
    cv::meanStdDev(hsv, mean, sd);

    auto range = [](double m, double s, double min_half, int lo_cap, int hi_cap,
                    int *lo, int *hi) {
        double half = std::max(2.5 * s, min_half);
        *lo = std::max(lo_cap, (int)(m - half));
        *hi = std::min(hi_cap, (int)(m + half));
    };

    SkinRange r;
    range(mean[0], sd[0],  8, 0, 179, &r.h_lo, &r.h_hi);
    range(mean[1], sd[1], 40, 0, 255, &r.s_lo, &r.s_hi);
    range(mean[2], sd[2], 70, 0, 255, &r.v_lo, &r.v_hi);
    r.v_hi = 255;   /* bright highlights on skin are still skin */
    return r;
}

/* =========================================================================
 * SECTION 4 — DETECTION
 * ========================================================================= */

HandResult gesture_detect(const cv::Mat &frame_bgr,
                          const SkinRange &skin,
                          GestureDebug *dbg)
{
    HandResult r = {};
    const int W = frame_bgr.cols, H = frame_bgr.rows;

    /* --- 1. Skin mask --- */
    cv::Mat mask;
    cv::inRange(to_rotated_hsv(frame_bgr),
                cv::Scalar(skin.h_lo, skin.s_lo, skin.v_lo),
                cv::Scalar(skin.h_hi, skin.s_hi, skin.v_hi),
                mask);

    /* --- 2. Remove the face (and neck below it) --- */
    if (g_face_ok && (g_frame_no++ % FACE_EVERY_N) == 0) {
        cv::Mat gray;
        cv::cvtColor(frame_bgr, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Rect> faces;
        g_face.detectMultiScale(gray, faces, 1.2, 4, 0, cv::Size(W / 8, W / 8));
        g_last_face = faces.empty() ? cv::Rect()
            : *std::max_element(faces.begin(), faces.end(),
                  [](const cv::Rect &a, const cv::Rect &b){ return a.area() < b.area(); });
    }
    cv::Rect face_block;
    if (g_last_face.area() > 0) {
        /* Grow the box: a bit wider for ears/hair, down 70% for the neck. */
        cv::Rect f = g_last_face;
        face_block = cv::Rect(f.x - f.width / 6, f.y - f.height / 5,
                              f.width + f.width / 3, f.height + f.height / 5 + f.height * 7 / 10)
                     & cv::Rect(0, 0, W, H);
        mask(face_block).setTo(0);
    }

    /* --- 3. Clean up --- */
    static const cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,  k);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, k);

    if (dbg) { dbg->mask = mask.clone(); dbg->face = face_block; dbg->contour.clear(); dbg->hull.clear(); }

    /* --- 4. Largest blob = hand --- */
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int    best = -1;
    double best_area = CFG_MIN_HAND_FRAC * W * H;   /* minimum size to count */
    for (int i = 0; i < (int)contours.size(); ++i) {
        double a = cv::contourArea(contours[i]);
        if (a > best_area) { best_area = a; best = i; }
    }
    if (best < 0) return r;                         /* no hand */

    const std::vector<cv::Point> &hand = contours[best];
    r.hand_detected = true;

    /* --- 5. Fingertip = topmost point ---
     * Average the x of all points within 3 px of the top so the tip
     * doesn't jitter between the two edges of the finger. */
    int top_y = H;
    for (const cv::Point &p : hand) top_y = std::min(top_y, p.y);
    double sx = 0; int n = 0;
    for (const cv::Point &p : hand)
        if (p.y <= top_y + 3) { sx += p.x; ++n; }
    r.tip_x = (float)(sx / n) / W;
    r.tip_y = (float)top_y / H;

    /* Palm = centre of mass of the blob (image moments). */
    cv::Moments m = cv::moments(hand);
    r.palm_x = (float)(m.m10 / m.m00) / W;
    r.palm_y = (float)(m.m01 / m.m00) / H;

    /* --- 6. Pose from solidity --- */
    std::vector<cv::Point> hull;
    cv::convexHull(hand, hull);
    double hull_area = cv::contourArea(hull);
    r.solidity = hull_area > 0 ? (float)(best_area / hull_area) : 1.0f;
    r.pose     = r.solidity > CFG_FIST_SOLIDITY ? POSE_FIST : POSE_POINT;

    if (dbg) { dbg->contour = hand; dbg->hull = hull; }
    return r;
}
