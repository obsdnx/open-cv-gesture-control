/*
 * controller.cpp — roles, cursor mapping, press/drag, click, trackpad yield
 *
 * Uses only CoreGraphics CGEvent (a plain C API) to post mouse events.
 * Behaviour is described in controller.h.
 */

#include "controller.h"
#include <ApplicationServices/ApplicationServices.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

/* =========================================================================
 * SECTION 1 — MOUSE OUTPUT (CoreGraphics)
 * ========================================================================= */

static void post_mouse(CGEventType type, float x, float y)
{
    CGEventRef e = CGEventCreateMouseEvent(NULL, type, CGPointMake(x, y), kCGMouseButtonLeft);
    CGEventSetIntegerValueField(e, kCGMouseEventClickState, 1);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

/* Where the cursor actually is right now (trackpad included). */
static CGPoint cursor_now(void)
{
    CGEventRef e = CGEventCreate(NULL);
    CGPoint p = CGEventGetLocation(e);
    CFRelease(e);
    return p;
}

double controller_now(void) { return (double)CFAbsoluteTimeGetCurrent(); }

/* =========================================================================
 * SECTION 2 — ONE EURO FILTER
 * ========================================================================= */

static float euro_alpha(float cutoff, float dt)
{
    float tau = 1.0f / (2.0f * (float)M_PI * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

static float euro_step(EuroAxis *a, float raw, float dt, const Steadiness *st)
{
    if (!a->init) { *a = EuroAxis{true, raw, 0, raw}; return raw; }
    float speed = (raw - a->raw) / dt;                      /* 1. speed      */
    a->dx += euro_alpha(CFG_EURO_D_CUTOFF, dt) * (speed - a->dx);
    a->raw = raw;
    float cutoff = st->min_cutoff + st->beta * std::fabs(a->dx); /* 2. adapt */
    a->x += euro_alpha(cutoff, dt) * (raw - a->x);          /* 3. low-pass   */
    return a->x;
}

/* Restart the filter at (x, y) — used when a hand (re)appears so the
 * cursor jumps there instead of gliding from wherever it was. */
static void smooth_reset(ControllerState *s, float x, float y)
{
    s->fx = EuroAxis{true, x, 0, x};
    s->fy = EuroAxis{true, y, 0, y};
    s->cur_x = x; s->cur_y = y;
    s->have_cursor = true;
    s->hist_n = 0;
}

static void smooth_to(ControllerState *s, float tx, float ty, const Steadiness *st)
{
    double now = controller_now();
    float  dt  = s->last_t > 0 ? (float)(now - s->last_t) : 1.0f / 30.0f;
    dt = std::max(1.0f / 240.0f, std::min(0.25f, dt));
    s->last_t = now;

    if (!s->have_cursor) { smooth_reset(s, tx, ty); return; }
    s->cur_x = euro_step(&s->fx, tx, dt, st);
    s->cur_y = euro_step(&s->fy, ty, dt, st);
}

/* =========================================================================
 * SECTION 3 — CURSOR HISTORY (to undo the fist-closing drift)
 * ========================================================================= */

static void history_push(ControllerState *s)
{
    s->hist_x[s->hist_i] = s->cur_x;
    s->hist_y[s->hist_i] = s->cur_y;
    s->hist_i = (s->hist_i + 1) % CTRL_HISTORY;
    if (s->hist_n < CTRL_HISTORY) ++s->hist_n;
}

static void history_get(const ControllerState *s, int back, float *x, float *y)
{
    back = std::min(back, s->hist_n);
    if (back == 0) { *x = s->cur_x; *y = s->cur_y; return; }
    int i = (s->hist_i - back + CTRL_HISTORY) % CTRL_HISTORY;
    *x = s->hist_x[i]; *y = s->hist_y[i];
}

/* =========================================================================
 * SECTION 4 — HELPERS
 * ========================================================================= */

static float map01(float v, float lo, float hi)
{
    return std::max(0.0f, std::min(1.0f, (v - lo) / (hi - lo)));
}

static void move_cursor(ControllerState *s)
{
    post_mouse(s->pressed ? kCGEventLeftMouseDragged : kCGEventMouseMoved, s->cur_x, s->cur_y);
    s->posted = true;
    s->posted_x = s->cur_x;
    s->posted_y = s->cur_y;
}

void controller_release(ControllerState *s)
{
    if (s->pressed) {
        post_mouse(kCGEventLeftMouseUp, s->cur_x, s->cur_y);
        printf("[control] release\n");
    }
    s->pressed = false;
}

/* Pick which detected hand is the pointer and which the clicker. */
static void assign_roles(ControllerState *s, const HandsResult *h)
{
    s->pointer_idx = s->clicker_idx = -1;
    if (h->count == 0) return;
    if (h->count == 1) { s->pointer_idx = 0; return; }

    bool ok0 = h->hand[0].pose == POSE_OK, ok1 = h->hand[1].pose == POSE_OK;
    int p;
    if (ok0 != ok1) {
        p = ok0 ? 1 : 0;                                  /* OK hand clicks */
    } else if (s->ptr_known && h->hand[0].is_right != h->hand[1].is_right) {
        p = (h->hand[0].is_right == s->ptr_is_right) ? 0 : 1;  /* keep same */
    } else {
        p = h->hand[1].is_right && !h->hand[0].is_right ? 1 : 0; /* prefer right */
    }
    s->pointer_idx = p;
    s->clicker_idx = 1 - p;
}

/* =========================================================================
 * SECTION 5 — PUBLIC API
 * ========================================================================= */

bool controller_init(ControllerState *s)
{
    *s = {};
    s->ptr_stable  = POSE_POINT;
    s->pointer_idx = s->clicker_idx = -1;

    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *vals[] = { kCFBooleanTrue };
    CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    bool trusted = AXIsProcessTrustedWithOptions(opts);
    CFRelease(opts);
    if (!trusted)
        fprintf(stderr,
            "[control] Accessibility permission missing — the cursor won't move.\n"
            "          System Settings > Privacy & Security > Accessibility >\n"
            "          enable your terminal app, then restart this program.\n");
    return trusted;
}

void controller_update(ControllerState *s, const HandsResult *h,
                       const Calibration *cal, int scr_w, int scr_h)
{
    const double now = controller_now();

    /* ---- 1. Trackpad takeover ----
     * If the cursor isn't where we left it, someone else moved it. */
    if (s->posted) {
        CGPoint p = cursor_now();
        if (std::hypot(p.x - s->posted_x, p.y - s->posted_y) > CFG_TRACKPAD_PX) {
            s->cur_x = (float)p.x; s->cur_y = (float)p.y;
            controller_release(s);
            s->pause_until = now + CFG_TRACKPAD_PAUSE;
            s->have_cursor = false;
            printf("[control] trackpad used — camera paused %.1fs\n", CFG_TRACKPAD_PAUSE);
        }
    }
    s->posted = false;
    s->paused = now < s->pause_until;

    assign_roles(s, h);

    /* ---- 2. No hands: hands-off, trackpad works ---- */
    if (h->count == 0) {
        s->ok_n = 0;
        if (++s->lost_n >= CFG_LOST_FRAMES) {
            controller_release(s);
            s->have_cursor = false;    /* next sighting jumps, no glide */
        }
        return;
    }
    s->lost_n = 0;

    if (s->paused) { s->have_cursor = false; return; }

    /* ---- 3. Clicker: OK sign → one click ---- */
    if (s->clicker_idx >= 0 && h->hand[s->clicker_idx].pose == POSE_OK) {
        if (++s->ok_n == CFG_POSE_FRAMES) {
            CGPoint c = cursor_now();   /* click where the cursor really is */
            post_mouse(kCGEventLeftMouseDown, (float)c.x, (float)c.y);
            post_mouse(kCGEventLeftMouseUp,   (float)c.x, (float)c.y);
            s->click_flash_until = now + 0.4;
            printf("[control] click\n");
        }
    } else {
        s->ok_n = 0;
    }

    /* ---- 4. Pointer ---- */
    const HandInfo &p = h->hand[s->pointer_idx];

    /* Switched to the other hand? Start the cursor fresh from it. */
    if (s->ptr_known && p.is_right != s->ptr_is_right) {
        s->have_cursor = false;
        s->ptr_cand_n  = 0;
    }
    s->ptr_known    = true;
    s->ptr_is_right = p.is_right;

    /* Debounce: only POINT and FIST change what the pointer does. */
    if (p.pose == s->ptr_cand) ++s->ptr_cand_n;
    else { s->ptr_cand = p.pose; s->ptr_cand_n = 1; }
    if (s->ptr_cand_n >= CFG_POSE_FRAMES &&
        (s->ptr_cand == POSE_POINT || s->ptr_cand == POSE_FIST))
        s->ptr_stable = s->ptr_cand;

    const ActiveBox &box = cal->box;

    if (s->ptr_stable == POSE_POINT) {
        controller_release(s);
        /* While the hand is changing shape (not a clean POINT), hold the
         * cursor still so curling fingers don't drag it. */
        if (p.pose != POSE_POINT) return;
        float tx = map01(p.tip_x, box.x0, box.x1) * (scr_w - 1);
        float ty = map01(p.tip_y, box.y0, box.y1) * (scr_h - 1);
        smooth_to(s, tx, ty, &cal->steady);
        history_push(s);
        move_cursor(s);
        return;
    }

    /* ptr_stable == POSE_FIST: press and drag. */
    if (!s->pressed) {
        float rx, ry;
        history_get(s, CTRL_REWIND, &rx, &ry);   /* undo closing drift */
        if (s->have_cursor) smooth_reset(s, rx, ry);
        else { CGPoint c = cursor_now(); smooth_reset(s, (float)c.x, (float)c.y); }
        post_mouse(kCGEventLeftMouseDown, s->cur_x, s->cur_y);
        s->pressed      = true;
        s->press_cur_x  = s->cur_x;  s->press_cur_y  = s->cur_y;
        s->press_palm_x = p.palm_x;  s->press_palm_y = p.palm_y;
        printf("[control] press\n");
    }
    /* Follow the palm (the fingertip is hidden in a fist), scaled like
     * the pointer so a fist moves the cursor as far as a finger would. */
    float sx = scr_w / (box.x1 - box.x0), sy = scr_h / (box.y1 - box.y0);
    float tx = std::max(0.0f, std::min((float)scr_w - 1, s->press_cur_x + (p.palm_x - s->press_palm_x) * sx));
    float ty = std::max(0.0f, std::min((float)scr_h - 1, s->press_cur_y + (p.palm_y - s->press_palm_y) * sy));
    smooth_to(s, tx, ty, &cal->steady);
    move_cursor(s);
}
