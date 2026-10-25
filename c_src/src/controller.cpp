/*
 * controller.cpp — translate GestureResult into macOS CGEvent input
 *
 * CoreGraphics CGEvent is a pure C API included in macOS SDK.
 * No Objective-C, no Swift, no AppKit — just #include and link.
 *
 * ACCESSIBILITY PERMISSION
 * The process must have Accessibility access to post CGEvents.
 * System Settings → Privacy & Security → Accessibility → Terminal
 *
 * Virtual key codes below come from the Carbon HIToolbox event table.
 * They're stable across macOS versions.
 */

#include "controller.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <cmath>
#include <cstdio>

/* -------------------------------------------------------------------------
 * Virtual key codes (from Carbon HIToolbox/Events.h)
 * We define them ourselves to avoid including the whole Carbon header.
 * ------------------------------------------------------------------------- */
static const CGKeyCode KEY_M   = 0x2E;   /* 'm' — used in Cmd+M (minimize) */
static const CGKeyCode KEY_F   = 0x03;   /* 'f' — used in Cmd+Ctrl+F       */
static const CGKeyCode KEY_TAB = 0x30;   /* Tab — used in Cmd+Tab          */

/* =========================================================================
 * SECTION 1 — LOW-LEVEL INPUT PRIMITIVES
 *
 * Each function creates a CGEvent, posts it to the HID event tap
 * (which injects it at the hardware level), then immediately releases
 * the event object with CFRelease to avoid memory leaks.
 * ========================================================================= */

static void mouse_move(int x, int y)
{
    CGPoint pt = CGPointMake(x, y);
    CGEventRef e = CGEventCreateMouseEvent(
        NULL, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

static void mouse_down(int x, int y)
{
    CGPoint pt = CGPointMake(x, y);
    CGEventRef e = CGEventCreateMouseEvent(
        NULL, kCGEventLeftMouseDown, pt, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

static void mouse_drag(int x, int y)
{
    /* kCGEventLeftMouseDragged = move with button held.
     * Using MouseMoved while the button is down does NOT work —
     * the system won't treat it as a drag.                                */
    CGPoint pt = CGPointMake(x, y);
    CGEventRef e = CGEventCreateMouseEvent(
        NULL, kCGEventLeftMouseDragged, pt, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

static void mouse_up(int x, int y)
{
    CGPoint pt = CGPointMake(x, y);
    CGEventRef e = CGEventCreateMouseEvent(
        NULL, kCGEventLeftMouseUp, pt, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

static void scroll_lines(int lines)
{
    /* CGScrollEventUnitLine scrolls by logical line units (respects
     * the user's scroll speed setting in System Preferences).           */
    CGEventRef e = CGEventCreateScrollWheelEvent(
        NULL, kCGScrollEventUnitLine, 1, lines);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

/*
 * send_hotkey — press a key with modifier flags.
 *
 * flags examples:
 *   kCGEventFlagMaskCommand                            → Cmd
 *   kCGEventFlagMaskCommand | kCGEventFlagMaskControl  → Cmd+Ctrl
 */
static void send_hotkey(CGKeyCode key, CGEventFlags flags)
{
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, key, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(NULL, key, false);
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up,   flags);
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
}

/* =========================================================================
 * SECTION 2 — HELPER: cursor smoothing
 *
 * Exponential Moving Average:
 *   new_val = old_val + α × (target − old_val)
 * α = CFG_SMOOTHING (0 = no movement, 1 = instant jump)
 * ========================================================================= */

static void smooth_cursor(ControllerState *s, float target_x, float target_y)
{
    s->cursor_x += CFG_SMOOTHING * (target_x - s->cursor_x);
    s->cursor_y += CFG_SMOOTHING * (target_y - s->cursor_y);
}

/* =========================================================================
 * SECTION 3 — PUBLIC API
 * ========================================================================= */

void controller_init(ControllerState *s)
{
    s->cursor_x      = 0;
    s->cursor_y      = 0;
    s->is_dragging   = false;
    s->prev_gesture  = GESTURE_NONE;
    s->gesture_hold  = 0;
    s->scroll_ref_y  = 0;
    s->scroll_active = false;
}

void controller_update(ControllerState     *s,
                       const GestureResult *r,
                       int                  scr_w,
                       int                  scr_h)
{
    /* --- Debounce: count consecutive frames with the same gesture --- */
    if (r->gesture == s->prev_gesture) {
        ++s->gesture_hold;
    } else {
        s->gesture_hold = 1;
    }
    s->prev_gesture = r->gesture;

    /* --- Map normalised [0,1] position → screen pixels --- */
    float target_x = r->norm_x * (float)scr_w;
    float target_y = r->norm_y * (float)scr_h;

    /*
     * Only update the smoothed cursor for gestures that use position.
     * For one-shot gestures (minimize, maximize, switch) we don't move
     * the cursor so the user can trigger them from any hand position.
     */
    bool uses_cursor = (r->gesture == GESTURE_POINTER  ||
                        r->gesture == GESTURE_DRAG     ||
                        r->gesture == GESTURE_SCROLL);
    if (uses_cursor && r->hand_detected) {
        smooth_cursor(s, target_x, target_y);
    }

    int cx = (int)s->cursor_x;
    int cy = (int)s->cursor_y;

    /* --- Release drag if gesture changed away from DRAG --- */
    if (s->is_dragging && r->gesture != GESTURE_DRAG) {
        mouse_up(cx, cy);
        s->is_dragging = false;
    }

    /* --- Reset scroll reference when leaving SCROLL mode --- */
    if (!s->scroll_active && r->gesture != GESTURE_SCROLL) {
        s->scroll_active = false;
    }

    /* ---------------------------------------------------------------
     * GESTURE DISPATCH
     * --------------------------------------------------------------- */
    switch (r->gesture) {

        /* -------- POINTER: index finger only -------- */
        case GESTURE_POINTER:
            mouse_move(cx, cy);
            break;

        /* -------- DRAG: fist — hold left button + move -------- */
        case GESTURE_DRAG:
            if (!s->is_dragging) {
                /* First frame of fist: press the button down */
                mouse_down(cx, cy);
                s->is_dragging = true;
            } else {
                /* Subsequent frames: drag to new position */
                mouse_drag(cx, cy);
            }
            break;

        /* -------- SCROLL: peace sign -------- */
        case GESTURE_SCROLL:
            mouse_move(cx, cy);  /* keep cursor visible while scrolling */
            if (!s->scroll_active) {
                /* Record the hand Y when scrolling begins */
                s->scroll_ref_y  = r->norm_y;
                s->scroll_active = true;
            } else {
                /* dy > 0 means hand moved DOWN → scroll down (negative) */
                float dy    = r->norm_y - s->scroll_ref_y;
                int   lines = (int)(dy * CFG_SCROLL_SPEED);
                if (lines != 0) {
                    scroll_lines(-lines);         /* invert: hand up = scroll up */
                    s->scroll_ref_y = r->norm_y;  /* reset reference each frame  */
                }
            }
            break;

        /* -------- SWITCH_WINDOW: 3 fingers — Cmd+Tab -------- */
        case GESTURE_SWITCH_WINDOW:
            if (s->gesture_hold == CFG_HOLD_FRAMES) {
                send_hotkey(KEY_TAB, kCGEventFlagMaskCommand);
            }
            break;

        /* -------- MINIMIZE: 4 fingers — Cmd+M -------- */
        case GESTURE_MINIMIZE:
            if (s->gesture_hold == CFG_HOLD_FRAMES) {
                send_hotkey(KEY_M, kCGEventFlagMaskCommand);
            }
            break;

        /* -------- MAXIMIZE: open hand — Cmd+Ctrl+F -------- */
        case GESTURE_MAXIMIZE:
            if (s->gesture_hold == CFG_HOLD_FRAMES) {
                send_hotkey(KEY_F,
                    kCGEventFlagMaskCommand | kCGEventFlagMaskControl);
            }
            break;

        /* -------- NONE / no hand detected -------- */
        default:
            s->scroll_active = false;
            break;
    }
}
