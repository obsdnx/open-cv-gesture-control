/*
 * hand_model.cpp — MediaPipe palm detector + hand landmark CNNs via OpenCV DNN
 *
 * See hand_model.h for the overall picture. This file has four parts:
 *   1. loading the networks
 *   2. STAGE 1: palm detection (anchor decoding + non-max suppression)
 *   3. turning a palm (or previous joints) into a rotated crop
 *   4. STAGE 2: landmark model + tracking between frames
 */

#include "hand_model.h"
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>   /* getAffineTransform — moved here in OpenCV 5 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

/* =========================================================================
 * SECTION 1 — NETWORKS
 * ========================================================================= */

static cv::dnn::Net g_palm;       /* stage 1 */
static cv::dnn::Net g_lmk;        /* stage 2 */

static const int   PALM_IN       = 192;   /* palm detector input size      */
static const int   LMK_IN        = 224;   /* landmark model input size     */
static const float PALM_MIN_SCORE = 0.5f; /* palm confidence threshold     */
static const float PRESENCE_MIN   = 0.5f; /* keep tracking above this      */
static const int   MAX_HANDS      = 2;
static const int   REDETECT_EVERY = 10;   /* frames between palm searches
                                             while fewer than 2 hands     */

bool hand_model_init(const char *dir)
{
    try {
        /* ENGINE_CLASSIC: OpenCV 5's new engine returns only one output per
         * forward pass; the classic engine returns all of them at once. */
        g_palm = cv::dnn::readNetFromTFLite(std::string(dir) + "/hand_detector.tflite",
                                            cv::dnn::ENGINE_CLASSIC);
        g_lmk  = cv::dnn::readNetFromTFLite(std::string(dir) + "/hand_landmarks_detector.tflite",
                                            cv::dnn::ENGINE_CLASSIC);
    } catch (const cv::Exception &e) {
        fprintf(stderr, "[hand] Cannot load models from %s\n"
                        "       Run `make models` in c_src/.\n", dir);
        return false;
    }
    printf("[hand] CNN models loaded.\n");
    return true;
}

/* Square crop, rotated by `angle` (radians), centred on `c`. */
struct RotRect {
    cv::Point2f c;
    float       size;
    float       angle;
};

/* =========================================================================
 * SECTION 2 — STAGE 1: PALM DETECTION
 *
 * The detector is an SSD ("single-shot detector"). Instead of predicting
 * boxes from scratch it predicts small corrections to 2016 fixed
 * "anchor" positions laid on a grid over the 192×192 input:
 *
 *   24×24 grid × 2 anchors  (stride 8)  = 1152
 *   12×12 grid × 6 anchors  (stride 16) =  864
 *                                        ------
 *                                         2016
 *
 * Output 1 (2016×18): per anchor — box dx, dy, w, h, then 7 keypoints (x,y)
 * Output 2 (2016×1) : per anchor — confidence logit (apply sigmoid)
 * ========================================================================= */

struct Palm {
    cv::Point2f center;    /* pixels in the original frame */
    float       w, h;
    cv::Point2f kp[7];     /* kp[0] = wrist, kp[2] = middle finger knuckle */
    float       score;
};

static const std::vector<cv::Point2f> &anchors(void)
{
    static std::vector<cv::Point2f> a;
    if (!a.empty()) return a;
    const int strides[] = {8, 16};
    const int per_cell[] = {2, 6};
    for (int s = 0; s < 2; ++s) {
        int n = PALM_IN / strides[s];
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                for (int k = 0; k < per_cell[s]; ++k)
                    a.push_back(cv::Point2f((x + 0.5f) / n, (y + 0.5f) / n));
    }
    return a;
}

static float iou(const Palm &a, const Palm &b)
{
    cv::Rect2f ra(a.center.x - a.w / 2, a.center.y - a.h / 2, a.w, a.h);
    cv::Rect2f rb(b.center.x - b.w / 2, b.center.y - b.h / 2, b.w, b.h);
    float inter = (ra & rb).area();
    return inter / (ra.area() + rb.area() - inter + 1e-6f);
}

static std::vector<Palm> detect_palms(const cv::Mat &frame)
{
    /* Letterbox: scale the frame to fit 192×192 without stretching, pad the
     * rest with black. Remember scale/padding to map results back. */
    const float scale = (float)PALM_IN / std::max(frame.cols, frame.rows);
    const int   nw = (int)std::round(frame.cols * scale);
    const int   nh = (int)std::round(frame.rows * scale);
    const int   px = (PALM_IN - nw) / 2, py = (PALM_IN - nh) / 2;

    cv::Mat input(PALM_IN, PALM_IN, CV_8UC3, cv::Scalar(0, 0, 0)), resized;
    cv::resize(frame, resized, cv::Size(nw, nh), 0, 0, cv::INTER_AREA);
    resized.copyTo(input(cv::Rect(px, py, nw, nh)));

    /* BGR→RGB, pixel values 0–255 → 0–1 */
    cv::Mat blob = cv::dnn::blobFromImage(input, 1.0 / 255, cv::Size(), cv::Scalar(), true, false, CV_32F);
    g_palm.setInput(blob);
    std::vector<cv::Mat> out;
    g_palm.forward(out, std::vector<cv::String>{"Identity", "Identity_1"});
    const float *reg = (const float *)out[0].data;   /* 2016 × 18 */
    const float *cls = (const float *)out[1].data;   /* 2016 × 1  */

    /* Decode every anchor above the score threshold. */
    const auto &A = anchors();
    auto to_frame = [&](float nx, float ny) {        /* 0–1 in input → frame px */
        return cv::Point2f((nx * PALM_IN - px) / scale, (ny * PALM_IN - py) / scale);
    };
    std::vector<Palm> cand;
    for (int i = 0; i < (int)A.size(); ++i) {
        float logit = std::max(-100.0f, std::min(100.0f, cls[i]));
        float score = 1.0f / (1.0f + std::exp(-logit));
        if (score < PALM_MIN_SCORE) continue;

        const float *r = reg + i * 18;
        Palm p;
        p.score  = score;
        p.center = to_frame(A[i].x + r[0] / PALM_IN, A[i].y + r[1] / PALM_IN);
        p.w      = r[2] / scale;
        p.h      = r[3] / scale;
        for (int k = 0; k < 7; ++k)
            p.kp[k] = to_frame(A[i].x + r[4 + 2 * k] / PALM_IN, A[i].y + r[5 + 2 * k] / PALM_IN);
        cand.push_back(p);
    }

    /* Non-max suppression: many anchors fire on the same palm; keep the
     * best one and drop anything overlapping it. */
    std::sort(cand.begin(), cand.end(), [](const Palm &a, const Palm &b){ return a.score > b.score; });
    std::vector<Palm> keep;
    for (const Palm &p : cand) {
        bool dup = false;
        for (const Palm &k : keep) if (iou(p, k) > 0.3f) { dup = true; break; }
        if (!dup) keep.push_back(p);
        if ((int)keep.size() == MAX_HANDS) break;
    }
    return keep;
}

/* =========================================================================
 * SECTION 3 — ROTATED CROPS
 *
 * The landmark model wants the hand upright (wrist at the bottom, fingers
 * up) and filling the crop. We rotate the crop so the line wrist → middle
 * knuckle points straight up, and make it larger than the palm so the
 * fingers fit.
 * ========================================================================= */

/* Angle that rotates the vector a→b to point straight up. */
static float upright_angle(cv::Point2f a, cv::Point2f b)
{
    float r = (float)M_PI / 2 - std::atan2(-(b.y - a.y), b.x - a.x);
    return r - 2 * (float)M_PI * std::floor((r + (float)M_PI) / (2 * (float)M_PI));
}

/* Move the crop centre by (sx, sy) × size in the crop's own rotated axes. */
static cv::Point2f shift(cv::Point2f c, float size, float angle, float sx, float sy)
{
    return cv::Point2f(c.x + size * (sx * std::cos(angle) - sy * std::sin(angle)),
                       c.y + size * (sx * std::sin(angle) + sy * std::cos(angle)));
}

static RotRect crop_from_palm(const Palm &p)
{
    RotRect r;
    r.angle = upright_angle(p.kp[0], p.kp[2]);
    float side = std::max(p.w, p.h);
    r.c    = shift(p.center, side, r.angle, 0.0f, -0.5f);  /* move toward fingers */
    r.size = side * 2.6f;                                    /* palm → whole hand  */
    return r;
}

static RotRect crop_from_joints(const Hand &h)
{
    RotRect r;
    r.angle = upright_angle(h.joint[0], h.joint[9]);   /* wrist → middle knuckle */

    /* Bounding box of all joints, measured in the rotated frame. */
    float c = std::cos(-r.angle), s = std::sin(-r.angle);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < HAND_JOINTS; ++i) {
        float x = h.joint[i].x * c - h.joint[i].y * s;
        float y = h.joint[i].x * s + h.joint[i].y * c;
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    /* rotate the centre back into frame coordinates */
    cv::Point2f centre(cx * std::cos(r.angle) - cy * std::sin(r.angle),
                       cx * std::sin(r.angle) + cy * std::cos(r.angle));
    float side = std::max(x1 - x0, y1 - y0);
    r.c    = shift(centre, side, r.angle, 0.0f, -0.1f);
    r.size = side * 2.0f;
    return r;
}

/* Affine transform mapping the rotated square onto a size×size image. */
static cv::Mat crop_transform(const RotRect &r, int size)
{
    float h = r.size / 2, c = std::cos(r.angle), s = std::sin(r.angle);
    auto corner = [&](float dx, float dy) {
        return cv::Point2f(r.c.x + dx * c - dy * s, r.c.y + dx * s + dy * c);
    };
    cv::Point2f src[3] = { corner(-h, -h), corner(h, -h), corner(-h, h) };
    cv::Point2f dst[3] = { {0, 0}, {(float)size, 0}, {0, (float)size} };
    return cv::getAffineTransform(src, dst);
}

/* =========================================================================
 * SECTION 4 — STAGE 2: LANDMARKS + TRACKING
 * ========================================================================= */

static bool run_landmarks(const cv::Mat &frame, const RotRect &r, Hand *out)
{
    cv::Mat M = crop_transform(r, LMK_IN), crop;
    cv::warpAffine(frame, crop, M, cv::Size(LMK_IN, LMK_IN), cv::INTER_LINEAR,
                   cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    cv::Mat blob = cv::dnn::blobFromImage(crop, 1.0 / 255, cv::Size(), cv::Scalar(), true, false, CV_32F);
    g_lmk.setInput(blob);
    std::vector<cv::Mat> o;
    g_lmk.forward(o, std::vector<cv::String>{"Identity", "Identity_1", "Identity_2"});

    out->presence = ((const float *)o[1].data)[0];
    out->is_right = ((const float *)o[2].data)[0] > 0.5f;
    if (out->presence < PRESENCE_MIN) return false;

    /* Joints come out in crop pixels (0–224); map them back to the frame
     * with the inverse of the crop transform. */
    cv::Mat Minv;
    cv::invertAffineTransform(M, Minv);
    const float *lm = (const float *)o[0].data;      /* 21 × (x, y, z) */
    for (int i = 0; i < HAND_JOINTS; ++i) {
        float x = lm[i * 3], y = lm[i * 3 + 1];
        out->joint[i] = cv::Point2f(
            (float)(Minv.at<double>(0, 0) * x + Minv.at<double>(0, 1) * y + Minv.at<double>(0, 2)),
            (float)(Minv.at<double>(1, 0) * x + Minv.at<double>(1, 1) * y + Minv.at<double>(1, 2)));
    }
    return true;
}

static float joint_spread(const Hand &h)
{
    return (float)cv::norm(h.joint[0] - h.joint[9]);   /* wrist → middle knuckle */
}

/* Two results on the same physical hand? (e.g. a new palm detection on a
 * hand we were already tracking) */
static bool same_hand(const Hand &a, const Hand &b)
{
    return cv::norm(a.joint[9] - b.joint[9]) < 0.8f * std::max(joint_spread(a), joint_spread(b));
}

static std::vector<Hand> tracked;   /* hands from the previous frame */
static int since_detect = 0;

void hand_model_reset(void) { tracked.clear(); since_detect = 0; }

std::vector<Hand> hand_model_detect(const cv::Mat &frame)
{
    std::vector<Hand> hands;

    /* 1. TRACK: re-run landmarks on crops derived from last frame's joints. */
    for (const Hand &prev : tracked) {
        Hand h;
        if (run_landmarks(frame, crop_from_joints(prev), &h)) hands.push_back(h);
    }

    /* 2. DETECT: look for new palms if we're missing a hand — immediately
     *    when we have none, otherwise every REDETECT_EVERY frames. */
    ++since_detect;
    if (hands.empty() || ((int)hands.size() < MAX_HANDS && since_detect >= REDETECT_EVERY)) {
        since_detect = 0;
        for (const Palm &p : detect_palms(frame)) {
            Hand h;
            if (!run_landmarks(frame, crop_from_palm(p), &h)) continue;
            bool dup = false;
            for (const Hand &k : hands) if (same_hand(h, k)) { dup = true; break; }
            if (!dup && (int)hands.size() < MAX_HANDS) hands.push_back(h);
        }
    }

    /* Drop duplicates that tracking may have produced (two crops converging
     * onto the same hand). */
    if (hands.size() == 2 && same_hand(hands[0], hands[1])) hands.pop_back();

    tracked = hands;
    return hands;
}
