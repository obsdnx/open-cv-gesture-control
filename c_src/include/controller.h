/*
 * controller.h — translate GestureResult into macOS input events
 *
 * WHAT THIS MODULE DOES
 * =====================
 * It receives one GestureResult per frame and decides what macOS action
 * (if any) to perform.  It maintains state across frames for:
 *
 *   Cursor smoothing
 *     Raw finger-position jumps frame-to-frame.  We apply an exponential
 *     moving average:
 *       cursor = cursor + SMOOTHING * (target - cursor)
 *     SMOOTHING=0.25 means we move 25% of the remaining distance each
 *     frame — smooth but still responsive.
 *
 *   Gesture debounce
 *     One-shot actions (click, minimize, etc.) must be held for
 *     CFG_HOLD_FRAMES consecutive frames before they fire.  This stops
 *     accidental triggers from a single mis-classified frame.
 *
 *   Drag state
 *     Drag requires mouseDown to remain held across frames.  We track
 *     is_dragging and call mouseUp when the gesture changes.
 *
 * HOW MACOS INPUT IS SYNTHESISED
 * ================================
 * We use the CoreGraphics CGEvent API — a pure C interface, no
 * Objective-C or Swift needed.
 *
 *   Mouse move   : CGEventCreateMouseEvent(kCGEventMouseMoved, …)
 *   Mouse down   : kCGEventLeftMouseDown
 *   Mouse drag   : kCGEventLeftMouseDragged
 *   Mouse up     : kCGEventLeftMouseUp
 *   Scroll       : CGEventCreateScrollWheelEvent(kCGScrollEventUnitLine, …)
 *   Key press    : CGEventCreateKeyboardEvent + CGEventSetFlags
 *
 * All events are posted to kCGHIDEventTap which injects them at the
 * hardware input level — indistinguishable from real mouse/keyboard.
 *
 * ACCESSIBILITY PERMISSION REQUIRED
 * ----------------------------------
 * Posting CGEvents requires the running app to have Accessibility access.
 * System Settings → Privacy & Security → Accessibility → add Terminal.app
 */

#pragma once
#include "types.h"

/* controller_init — zero-initialise the state struct. Call once at startup. */
void controller_init(ControllerState *state);

/*
 * controller_update — consume one GestureResult and emit macOS events.
 *
 *   state  : mutable state carried between frames (smoothing, drag, etc.)
 *   result : the GestureResult from gesture_detect() for this frame
 *   scr_w  : screen width  in pixels (from CGDisplayBounds)
 *   scr_h  : screen height in pixels
 */
void controller_update(ControllerState        *state,
                       const GestureResult    *result,
                       int                     scr_w,
                       int                     scr_h);
