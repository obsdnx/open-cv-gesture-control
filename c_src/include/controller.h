/*
 * controller.h — turn hands into mouse actions
 *
 * ROLES (decided every frame)
 * ===========================
 *   One hand visible  → it is the POINTER.
 *   Two hands visible → the hand making the OK sign is the CLICKER,
 *                       the other is the POINTER. If neither is doing OK,
 *                       the pointer stays the same hand as before.
 *
 * WHAT EACH ROLE DOES
 *   POINTER, index finger out → cursor follows the index fingertip
 *   POINTER, fist             → mouse button pressed and held, exactly
 *                               like pressing down on the trackpad.
 *                               Move the fist = drag. Open hand = release.
 *   CLICKER, OK sign          → one left click at the cursor
 *                               (fires once per OK; relax and redo for
 *                               another click)
 *   ANY HAND, open + swipe    → switch desktop (Ctrl+← / Ctrl+→, see
 *                               swipe.h). While the hand is open the
 *                               cursor holds still and the button is up.
 *
 * TRACKPAD
 *   No hand on camera → this program sends nothing; the trackpad works
 *   normally. If you touch the trackpad while a hand IS visible, we see
 *   the cursor move without us and pause for CFG_TRACKPAD_PAUSE seconds.
 *
 * POINTER MAPPING
 *   The fingertip inside the calibrated ActiveBox maps linearly onto the
 *   whole screen:
 *     screen_x = (tip_x − box.x0) / (box.x1 − box.x0) × screen_width
 *   Pointing past your calibrated reach pins the cursor to the edge.
 *
 * SMOOTHING — One Euro filter (Casiez et al., 2012)
 *     cutoff = min_cutoff + beta × |speed|
 *     alpha  = 1 / (1 + 1 / (2π × cutoff × dt))
 *     out    = out + alpha × (in − out)
 *   Still hand → heavy smoothing (no shake). Fast hand → light (no lag).
 *   min_cutoff is the "steadiness" knob: [ and ] in the preview.
 *
 * DEBOUNCE
 *   A pose must hold for CFG_POSE_FRAMES frames before it counts, so one
 *   misread frame can't press, release or click.
 *
 * PERMISSION
 *   System Settings → Privacy & Security → Accessibility → your terminal.
 */

#pragma once
#include "types.h"

#define CTRL_HISTORY 8     /* cursor positions remembered (ring buffer) */
#define CTRL_REWIND  4     /* frames to rewind when a fist starts       */

/* One Euro filter state for ONE axis. */
typedef struct {
    bool  init;
    float x;      /* filtered value        */
    float dx;     /* filtered speed (px/s) */
    float raw;    /* previous raw input    */
} EuroAxis;

typedef struct {
    /* ---- cursor ---- */
    float    cur_x, cur_y;          /* smoothed cursor, screen pixels     */
    EuroAxis fx, fy;
    double   last_t;                /* time of last filter step (s)       */
    bool     have_cursor;           /* false → next sighting starts fresh */
    float    hist_x[CTRL_HISTORY], hist_y[CTRL_HISTORY];
    int      hist_i, hist_n;

    /* ---- pointer hand ---- */
    bool  ptr_known;                /* have we chosen a pointer before?   */
    bool  ptr_is_right;             /* which hand it was                  */
    Pose  ptr_stable;               /* debounced pose acted on            */
    Pose  ptr_cand;
    int   ptr_cand_n;
    int   lost_n;                   /* frames with no hands               */

    /* ---- press (fist) ---- */
    bool  pressed;                  /* mouse button currently held        */
    float press_cur_x, press_cur_y; /* cursor when the press started      */
    float press_palm_x, press_palm_y;

    /* ---- clicker hand ---- */
    int   ok_n;                     /* consecutive OK frames              */
    double click_flash_until;       /* for the preview: show "CLICK"      */
    double swipe_flash_until;       /* for the preview: show the swipe    */
    int    swipe_flash_dir;         /* +1 desktop right, -1 left          */

    /* ---- trackpad takeover ---- */
    bool   posted;                  /* did we move the cursor last frame? */
    float  posted_x, posted_y;      /* where we put it                    */
    double pause_until;             /* camera control paused until (s)    */

    /* ---- for the preview ---- */
    int   pointer_idx;              /* index into HandsResult, or -1      */
    int   clicker_idx;
    bool  paused;
} ControllerState;

/* Zero the state and check Accessibility permission (opens the macOS
 * prompt if missing). Returns false if permission isn't granted yet. */
bool controller_init(ControllerState *s);

/* Process one frame of hands. */
void controller_update(ControllerState *s, const HandsResult *hands,
                       const Calibration *cal, int scr_w, int scr_h);

/* A swipe was detected (hand_dir from swipe_update): switch desktop. */
void controller_switch_space(ControllerState *s, int hand_dir);

/* Let go of the mouse button if held (call on exit / calibration). */
void controller_release(ControllerState *s);

/* Current time in seconds (shared clock for controller + preview). */
double controller_now(void);
