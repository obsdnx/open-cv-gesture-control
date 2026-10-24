/*
 * controller.h — turn HandResult into cursor movement and window grabbing
 *
 * POINTER (hand open / pointing)
 * ==============================
 *   The fingertip position inside the calibrated ActiveBox is mapped
 *   linearly onto the whole screen:
 *
 *     screen_x = (tip_x - box.x0) / (box.x1 - box.x0) × screen_width
 *     screen_y = (tip_y - box.y0) / (box.y1 - box.y0) × screen_height
 *
 *   Clamped to the screen edges, so pointing past your calibrated reach
 *   pins the cursor to the edge/corner.
 *
 *   Smoothing is ADAPTIVE: a tiny movement (hand tremor) moves the cursor
 *   only 15% of the way per frame; a big fast movement moves it ~100%.
 *   You get a steady cursor without lag on large moves.
 *
 * GRAB (fist)
 * ===========
 *   On the frame the fist is confirmed:
 *     1. Put the cursor back where it was a few frames ago — closing your
 *        hand pulls the fingertip down, and we don't want that to count.
 *     2. Ask macOS (Accessibility API) which window is under the cursor
 *        and remember its position.
 *   While the fist is held:
 *     3. Follow the PALM (not the fingertip — there isn't one in a fist).
 *        Palm movement is scaled exactly like the pointer, so the window
 *        moves as far as the cursor would.
 *     4. Set the window's position = start position + movement.
 *   Opening the hand (or losing it for CFG_LOST_FRAMES) drops the window.
 *
 *   Using AX instead of a fake mouse-drag means you can grab a window
 *   anywhere, not just by its title bar.
 *
 * DEBOUNCE
 *   The pose must be the same for CFG_POSE_FRAMES frames in a row before
 *   we switch between POINT and FIST, so a single bad frame can't drop
 *   the window or start a grab.
 *
 * PERMISSION
 *   Moving the cursor and windows requires Accessibility access:
 *   System Settings → Privacy & Security → Accessibility → your terminal.
 */

#pragma once
#include "types.h"

#define CTRL_HISTORY 8     /* cursor positions remembered (ring buffer) */
#define CTRL_REWIND  4     /* frames to rewind when a fist starts       */

typedef struct {
    /* cursor */
    float cur_x, cur_y;            /* smoothed cursor, screen pixels        */
    bool  have_cursor;             /* false until the first hand is seen    */
    float hist_x[CTRL_HISTORY];    /* recent cursor positions               */
    float hist_y[CTRL_HISTORY];
    int   hist_i;                  /* next write slot                       */
    int   hist_n;                  /* how many slots are filled             */

    /* pose debounce */
    Pose  stable_pose;             /* pose currently acted on               */
    Pose  candidate;               /* pose being counted                    */
    int   candidate_n;             /* consecutive frames of candidate       */
    int   lost_n;                  /* consecutive frames with no hand       */

    /* grab */
    bool  grabbing;
    void *window;                  /* AXUIElementRef of grabbed window      */
    float win_x0, win_y0;          /* window position at grab start         */
    float grab_cur_x, grab_cur_y;  /* cursor at grab start                  */
    float grab_palm_x, grab_palm_y;/* palm (normalised) at grab start       */
} ControllerState;

/* Zero the state. Also checks Accessibility permission and, if missing,
 * opens the macOS prompt. Returns false if permission is not granted yet. */
bool controller_init(ControllerState *s);

/* Process one frame. `box` is the calibrated ActiveBox. */
void controller_update(ControllerState *s, const HandResult *hand,
                       const ActiveBox *box, int scr_w, int scr_h);

/* Drop any grabbed window (call on exit or when calibration starts). */
void controller_release(ControllerState *s);
