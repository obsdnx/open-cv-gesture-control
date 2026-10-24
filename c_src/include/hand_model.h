/*
 * hand_model.h — find up to two hands and their 21 joints with a CNN
 *
 * WHY A CNN INSTEAD OF SKIN COLOUR
 *   Skin colour can't tell a hand from a face, a wooden wall or a cartoon
 *   on the TV, and can't tell your left hand from your right. Google's
 *   MediaPipe hand models are convolutional neural networks trained on
 *   tens of thousands of hand photos; they give 21 joint positions per
 *   hand regardless of background, lighting or which way you point.
 *   We run them with OpenCV's DNN module — no Python, no MediaPipe library.
 *
 * TWO NETWORKS, TWO STAGES
 *
 *   ┌──────────── whole frame ────────────┐
 *   │  STAGE 1: PALM DETECTOR (192×192)   │  "where are the palms?"
 *   │  → boxes + 7 palm keypoints         │  slow-ish, run occasionally
 *   └──────────────────┬──────────────────┘
 *                      │ rotated square crop around each palm
 *   ┌──────────────────▼──────────────────┐
 *   │  STAGE 2: LANDMARK MODEL (224×224)  │  "where is every joint?"
 *   │  → 21 joints, presence, left/right  │  run every frame per hand
 *   └──────────────────┬──────────────────┘
 *                      │ next frame: crop computed from these joints
 *                      └──► (tracking — skips stage 1 while the hand stays)
 *
 * JOINT NUMBERING (MediaPipe standard)
 *
 *        8   12  16  20        tips
 *        7   11  15  19
 *    4   6   10  14  18
 *    3   5    9  13  17        knuckles (MCP)
 *     2
 *      1
 *          0                   wrist
 *   thumb index middle ring pinky
 */

#pragma once
#include <opencv2/core.hpp>
#include <vector>

#define HAND_JOINTS 21

typedef struct {
    cv::Point2f joint[HAND_JOINTS];  /* pixel coords in the input frame     */
    float       presence;            /* 0–1 confidence a hand is there      */
    bool        is_right;            /* the person's right hand (expects a
                                        mirrored, selfie-view frame)        */
} Hand;

/* Load both networks from `model_dir`. Returns false (and prints why) if
 * the .tflite files are missing — run `make models` to fetch them. */
bool hand_model_init(const char *model_dir);

/* Forget tracked hands (next call searches the whole frame again). */
void hand_model_reset(void);

/* Detect/track up to two hands in a mirrored BGR frame. */
std::vector<Hand> hand_model_detect(const cv::Mat &frame_bgr);
