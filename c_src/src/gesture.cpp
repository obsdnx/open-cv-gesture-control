/*
 * gesture.cpp — pose rules on CNN hand joints (see gesture.h)
 */

#include "gesture.h"
#include <algorithm>
#include <cmath>

/* Joint indices (MediaPipe numbering, drawn in hand_model.h). */
enum {
    WRIST = 0,
    THUMB_TIP = 4,
    INDEX_MCP = 5,  INDEX_PIP = 6,   INDEX_TIP = 8,
    MIDDLE_MCP = 9, MIDDLE_PIP = 10, MIDDLE_TIP = 12,
    RING_MCP = 13,  RING_PIP = 14,   RING_TIP = 16,
    PINKY_MCP = 17, PINKY_PIP = 18,  PINKY_TIP = 20,
};

static float dist(cv::Point2f a, cv::Point2f b) { return (float)cv::norm(a - b); }

/* Straight finger: its tip is clearly farther from the wrist than its
 * middle joint. Curled finger: the tip folds back toward the palm. */
static bool extended(const Hand &h, int pip, int tip)
{
    float d_tip = dist(h.joint[WRIST], h.joint[tip]);
    float d_pip = dist(h.joint[WRIST], h.joint[pip]);
    return d_tip > d_pip * CFG_FINGER_OUT;
}

HandInfo gesture_classify(const Hand &h, int W, int H)
{
    HandInfo r = {};
    r.is_right = h.is_right;

    /* Palm size makes every threshold independent of camera distance. */
    float palm = std::max(1.0f, dist(h.joint[WRIST], h.joint[MIDDLE_MCP]));

    bool index  = extended(h, INDEX_PIP,  INDEX_TIP);
    bool middle = extended(h, MIDDLE_PIP, MIDDLE_TIP);
    bool ring   = extended(h, RING_PIP,   RING_TIP);
    bool pinky  = extended(h, PINKY_PIP,  PINKY_TIP);

    r.pinch = dist(h.joint[THUMB_TIP], h.joint[INDEX_TIP]) / palm;

    /* Order matters: OK is checked first because in an OK sign the index
     * finger is bent, which would otherwise look like "not pointing". */
    if (r.pinch < CFG_OK_PINCH && (middle + ring + pinky) >= 2) r.pose = POSE_OK;
    else if (!index && !middle && !ring && !pinky)                r.pose = POSE_FIST;
    else if (index)                                               r.pose = POSE_POINT;
    else                                                          r.pose = POSE_NONE;

    r.tip_x = h.joint[INDEX_TIP].x / W;
    r.tip_y = h.joint[INDEX_TIP].y / H;

    /* Palm centre = average of wrist + four knuckles. These barely move
     * when you open or close the hand, so dragging stays steady. */
    const int pts[] = {WRIST, INDEX_MCP, MIDDLE_MCP, RING_MCP, PINKY_MCP};
    float sx = 0, sy = 0;
    for (int i : pts) { sx += h.joint[i].x; sy += h.joint[i].y; }
    r.palm_x = sx / 5 / W;
    r.palm_y = sy / 5 / H;
    return r;
}

HandsResult gesture_analyse(const std::vector<Hand> &hands, int W, int H)
{
    HandsResult r = {};
    for (const Hand &h : hands) {
        if (r.count == 2) break;
        r.hand[r.count++] = gesture_classify(h, W, H);
    }
    return r;
}

const char *pose_name(Pose p)
{
    switch (p) {
        case POSE_POINT: return "POINT";
        case POSE_FIST:  return "FIST";
        case POSE_OK:    return "OK";
        default:         return "-";
    }
}
