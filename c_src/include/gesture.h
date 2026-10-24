/*
 * gesture.h — turn 21 hand joints into a Pose
 *
 * The CNN (hand_model) already did the hard part: it knows where every
 * joint is. Recognising a pose is now simple geometry, and it works at
 * any angle (pointing up, sideways, at the camera) because we only
 * compare distances, never "up" or "down".
 *
 * IS A FINGER EXTENDED?
 *   Compare how far the fingertip and the finger's middle joint (PIP) are
 *   from the wrist:
 *
 *     extended : tip is FARTHER than the middle joint  (finger straight)
 *     curled   : tip is CLOSER   than the middle joint (folded into palm)
 *
 *   ratio = dist(wrist, tip) / dist(wrist, PIP) > CFG_FINGER_OUT
 *
 * POSES
 *   OK    : thumb tip near index tip  (gap < CFG_OK_PINCH × palm size)
 *           AND at least two of middle/ring/pinky extended
 *   FIST  : index, middle, ring and pinky all curled
 *   POINT : index extended (whatever the others do)
 *   NONE  : anything else
 *
 *   "palm size" = distance wrist → middle knuckle, so all thresholds
 *   scale with how close you sit to the camera.
 */

#pragma once
#include <vector>
#include "types.h"
#include "hand_model.h"

/* Classify one hand. frame_w/h normalise positions to [0,1]. */
HandInfo gesture_classify(const Hand &h, int frame_w, int frame_h);

/* Classify every detected hand. */
HandsResult gesture_analyse(const std::vector<Hand> &hands, int frame_w, int frame_h);

const char *pose_name(Pose p);
