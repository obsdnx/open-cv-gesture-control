/*
 * controller.cpp — cursor mapping + window grabbing via macOS C APIs
 *
 *   CoreGraphics (CGEvent)  : moves the mouse cursor
 *   Accessibility (AXUIElement): finds the window under the cursor and
 *                             sets its position
 *
 * Both are plain C APIs from ApplicationServices — no Objective-C here.
 * See controller.h for the behaviour description.
 */

#include "controller.h"
#include <ApplicationServices/ApplicationServices.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

/* =========================================================================
 * SECTION 1 — CURSOR OUTPUT
 * ========================================================================= */

static void mouse_move(float x, float y)
{
    CGEventRef e = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                           CGPointMake(x, y), kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

/* =========================================================================
 * SECTION 2 — WINDOW ACCESS (Accessibility API)
 *
 * Every on-screen UI item is an AXUIElement. We ask for the element at a
 * screen point, then for its containing window, then read/write that
 * window's "AXPosition" attribute (top-left corner, screen coordinates —
 * the same coordinate system CGEvent uses).
 * ========================================================================= */

static AXUIElementRef window_at(float x, float y)
{
    AXUIElementRef sys = AXUIElementCreateSystemWide();
    AXUIElementRef el  = NULL;
    AXError err = AXUIElementCopyElementAtPosition(sys, x, y, &el);
    CFRelease(sys);
    if (err != kAXErrorSuccess || !el) return NULL;

    /* Is the element itself a window? */
    CFTypeRef role = NULL;
    AXUIElementCopyAttributeValue(el, kAXRoleAttribute, &role);
    bool is_window = role && CFEqual(role, kAXWindowRole);
    if (role) CFRelease(role);
    if (is_window) return el;

    /* Otherwise ask for the window that contains it. */
    CFTypeRef win = NULL;
    AXUIElementCopyAttributeValue(el, kAXWindowAttribute, &win);
    CFRelease(el);
    return (AXUIElementRef)win;     /* may be NULL (desktop, menu bar) */
}

static bool window_get_pos(AXUIElementRef w, float *x, float *y)
{
    CFTypeRef v = NULL;
    if (AXUIElementCopyAttributeValue(w, kAXPositionAttribute, &v) != kAXErrorSuccess || !v)
        return false;
    CGPoint p;
    bool ok = AXValueGetValue((AXValueRef)v, (AXValueType)kAXValueCGPointType, &p);
    CFRelease(v);
    *x = (float)p.x; *y = (float)p.y;
    return ok;
}

static void window_set_pos(AXUIElementRef w, float x, float y)
{
    CGPoint p = CGPointMake(x, y);
    AXValueRef v = AXValueCreate((AXValueType)kAXValueCGPointType, &p);
    AXUIElementSetAttributeValue(w, kAXPositionAttribute, v);
    CFRelease(v);
}

/* =========================================================================
 * SECTION 3 — HELPERS
 * ========================================================================= */

/* Map v from [lo, hi] onto [0, 1], clamped. */
static float map01(float v, float lo, float hi)
{
    float t = (v - lo) / (hi - lo);
    return std::max(0.0f, std::min(1.0f, t));
}

/* Smoothing factor for a first-order low-pass at `cutoff` Hz. */
static float euro_alpha(float cutoff, float dt)
{
    float tau = 1.0f / (2.0f * (float)M_PI * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

/* One Euro filter step for one axis. See controller.h for the idea. */
static float euro_step(EuroAxis *a, float raw, float dt, const Steadiness *st)
{
    if (!a->init) { a->init = true; a->x = a->raw = raw; a->dx = 0; return raw; }

    /* 1. Estimate speed, smoothed with a fixed cutoff. */
    float speed = (raw - a->raw) / dt;
    a->dx  += euro_alpha(CFG_EURO_D_CUTOFF, dt) * (speed - a->dx);
    a->raw  = raw;

    /* 2. Faster movement → higher cutoff → less smoothing. */
    float cutoff = st->min_cutoff + st->beta * std::fabs(a->dx);
    a->x += euro_alpha(cutoff, dt) * (raw - a->x);
    return a->x;
}

/* Filter (tx, ty) into the cursor position. */
static void smooth_to(ControllerState *s, float tx, float ty, const Steadiness *st)
{
    double now = (double)CFAbsoluteTimeGetCurrent();
    float  dt  = s->last_t > 0 ? (float)(now - s->last_t) : 1.0f / 30.0f;
    dt = std::max(1.0f / 240.0f, std::min(0.25f, dt));   /* guard odd gaps */
    s->last_t = now;

    s->cur_x = euro_step(&s->fx, tx, dt, st);
    s->cur_y = euro_step(&s->fy, ty, dt, st);
    s->have_cursor = true;
}

/* Restart the filter at a position (no smoothing "catch-up" jump). */
static void smooth_reset(ControllerState *s, float x, float y)
{
    s->fx = EuroAxis{true, x, 0, x};
    s->fy = EuroAxis{true, y, 0, y};
    s->cur_x = x; s->cur_y = y;
}

static void history_push(ControllerState *s)
{
    s->hist_x[s->hist_i] = s->cur_x;
    s->hist_y[s->hist_i] = s->cur_y;
    s->hist_i = (s->hist_i + 1) % CTRL_HISTORY;
    if (s->hist_n < CTRL_HISTORY) ++s->hist_n;
}

/* Cursor position from `back` frames ago (clamped to what we have). */
static void history_get(const ControllerState *s, int back, float *x, float *y)
{
    back = std::min(back, s->hist_n);
    if (back == 0) { *x = s->cur_x; *y = s->cur_y; return; }
    int i = (s->hist_i - back + CTRL_HISTORY) % CTRL_HISTORY;
    *x = s->hist_x[i]; *y = s->hist_y[i];
}

/* =========================================================================
 * SECTION 4 — GRAB START / END
 * ========================================================================= */

static void grab_start(ControllerState *s, const HandResult *h)
{
    /* 1. Undo the downward drift caused by curling the fingers. */
    float rx, ry;
    history_get(s, CTRL_REWIND, &rx, &ry);
    smooth_reset(s, rx, ry);
    mouse_move(s->cur_x, s->cur_y);

    /* 2. Find and remember the window under the cursor. */
    s->window = (void *)window_at(s->cur_x, s->cur_y);
    if (s->window && !window_get_pos((AXUIElementRef)s->window, &s->win_x0, &s->win_y0)) {
        CFRelease((AXUIElementRef)s->window);
        s->window = NULL;
    }

    s->grab_cur_x  = s->cur_x;
    s->grab_cur_y  = s->cur_y;
    s->grab_palm_x = h->palm_x;
    s->grab_palm_y = h->palm_y;
    s->grabbing    = true;
    printf("[control] GRAB %s\n", s->window ? "window" : "(nothing under cursor)");
}

void controller_release(ControllerState *s)
{
    if (s->window) CFRelease((AXUIElementRef)s->window);
    if (s->grabbing) printf("[control] RELEASE\n");
    s->window   = NULL;
    s->grabbing = false;
}

/* =========================================================================
 * SECTION 5 — PUBLIC API
 * ========================================================================= */

bool controller_init(ControllerState *s)
{
    *s = {};
    s->stable_pose = POSE_POINT;

    /* AXIsProcessTrustedWithOptions with the prompt flag pops up the
     * macOS dialog that links straight to the Accessibility settings. */
    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *vals[] = { kCFBooleanTrue };
    CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    bool trusted = AXIsProcessTrustedWithOptions(opts);
    CFRelease(opts);

    if (!trusted)
        fprintf(stderr,
            "[control] Accessibility permission missing — cursor/windows won't move.\n"
            "          System Settings > Privacy & Security > Accessibility >\n"
            "          enable your terminal app, then restart this program.\n");
    return trusted;
}

void controller_update(ControllerState *s, const HandResult *h,
                       const Calibration *cal, int scr_w, int scr_h)
{
    const ActiveBox  *box = &cal->box;
    const Steadiness *st  = &cal->steady;

    /* --- No hand: hold still; drop the window if it stays gone --- */
    if (!h->hand_detected) {
        if (++s->lost_n >= CFG_LOST_FRAMES) {
            if (s->grabbing) controller_release(s);
            s->last_t = 0;   /* hand gone: don't let the gap skew the filter */
        }
        return;
    }
    s->lost_n = 0;

    /* --- Debounce the pose --- */
    if (h->pose == s->candidate) ++s->candidate_n;
    else { s->candidate = h->pose; s->candidate_n = 1; }
    if (s->candidate_n >= CFG_POSE_FRAMES) s->stable_pose = s->candidate;

    /* Pixels of screen per unit of normalised camera movement. */
    float sx = scr_w / (box->x1 - box->x0);
    float sy = scr_h / (box->y1 - box->y0);

    /* ================= POINTER ================= */
    if (s->stable_pose == POSE_POINT) {
        if (s->grabbing) controller_release(s);

        /* While a fist is forming (raw FIST, not yet confirmed) freeze the
         * cursor so the curling fingers don't drag it downward. */
        if (h->pose != POSE_POINT) return;

        float tx = map01(h->tip_x, box->x0, box->x1) * (scr_w - 1);
        float ty = map01(h->tip_y, box->y0, box->y1) * (scr_h - 1);
        smooth_to(s, tx, ty, st);
        history_push(s);
        mouse_move(s->cur_x, s->cur_y);
        return;
    }

    /* ================= GRAB ================= */
    if (!s->grabbing) grab_start(s, h);

    float tx = s->grab_cur_x + (h->palm_x - s->grab_palm_x) * sx;
    float ty = s->grab_cur_y + (h->palm_y - s->grab_palm_y) * sy;
    tx = std::max(0.0f, std::min((float)scr_w - 1, tx));
    ty = std::max(0.0f, std::min((float)scr_h - 1, ty));
    smooth_to(s, tx, ty, st);
    mouse_move(s->cur_x, s->cur_y);

    if (s->window)
        window_set_pos((AXUIElementRef)s->window,
                       s->win_x0 + (s->cur_x - s->grab_cur_x),
                       s->win_y0 + (s->cur_y - s->grab_cur_y));
}
