/*
 * swipe.h — detect a fast sideways swipe of an open hand
 *
 * WHAT COUNTS AS A SWIPE
 *   The palm travels at least CFG_SWIPE_DIST of the frame width within
 *   CFG_SWIPE_TIME seconds, mostly sideways (horizontal > 1.5 × vertical),
 *   and the hand is OPEN in at least one frame of that movement.
 *
 *   "At least one OPEN frame" (not all) because a fast hand blurs: the
 *   CNN may read it as cupped/pointing for a few frames, or lose it
 *   completely mid-swipe. A pointing finger moving fast is never OPEN,
 *   so moving the cursor quickly does not trigger a swipe.
 *
 * GAPS
 *   We keep a timeline of (time, palm x, palm y, open?) samples. Frames
 *   with no hand just add nothing, so "last seen on the left, reappeared
 *   on the right 0.3 s later" still measures as a big, fast movement.
 *
 * THE RETURN STROKE
 *   After swiping you swing your hand back. That is just as fast, so:
 *     opposite direction  → blocked for CFG_SWIPE_RETURN  seconds
 *     same direction      → allowed after CFG_SWIPE_SAME_GAP seconds
 *   Swipe-return-swipe-return keeps going the same way. To go the other
 *   way, hold still for a second, then swipe.
 *
 * DIRECTION
 *   Positions are in the mirrored preview: +1 = your hand moved to YOUR
 *   right, -1 = to your left.
 */

#pragma once
#include <deque>
#include "types.h"

typedef struct {
    double t;
    float  x, y;
    bool   open;
} SwipeSample;

typedef struct {
    std::deque<SwipeSample> trail;
    double last_t;      /* when the last swipe fired */
    int    last_dir;    /* its direction (+1 / -1), 0 = none yet */
} SwipeDetector;

/* Feed one frame. Returns +1 (swiped right), -1 (swiped left) or 0. */
int swipe_update(SwipeDetector *d, const HandsResult *hands, double now);
