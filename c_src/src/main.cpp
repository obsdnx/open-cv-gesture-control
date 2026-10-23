/*
 * main.cpp — program entry point and per-frame loop
 *
 * PROGRAM FLOW (one iteration = one camera frame)
 * ================================================
 *
 *   startup
 *     │
 *     ├─ camera_open()       — open webcam, warm up sensor
 *     ├─ controller_init()   — zero the smoothing / drag / scroll state
 *     │
 *     └─ loop ──────────────────────────────────────────────────────────
 *           │
 *           ├─ camera_read(frame)         — grab latest BGR frame
 *           │
 *           ├─ cv::flip(frame)            — mirror so your hand
 *           │                               matches the display
 *           │
 *           ├─ gesture_detect(frame)      — skin mask → contour
 *           │                               → convexity defects
 *           │                               → GestureResult
 *           │
 *           ├─ controller_update(result)  — smoothing → CGEvent
 *           │
 *           └─ (optional) draw HUD and show preview window
 *
 *   shutdown
 *     └─ camera_close()
 *
 * BUILD
 *   cd c_src && make
 *
 * USAGE
 *   ./gesture_control                  (built-in camera, with preview)
 *   ./gesture_control --list           (print cameras and their indices)
 *   ./gesture_control --no-window      (headless)
 *   ./gesture_control --camera 1       (pick a camera from --list)
 *   ./gesture_control --debug          (overlay skin mask / hull)
 *
 * PERMISSIONS REQUIRED (macOS)
 *   Camera      : System Settings → Privacy & Security → Camera → Terminal
 *   Accessibility: System Settings → Privacy & Security → Accessibility → Terminal
 */

#include "camera.h"
#include "gesture.h"
#include "controller.h"
#include <CoreGraphics/CoreGraphics.h>
#include <cstdio>
#include <cstring>

/* =========================================================================
 * SECTION 1 — HUD DRAWING
 *
 * A minimal "heads-up display" drawn onto the preview frame with OpenCV.
 * This is separate from gesture logic — removing it has no effect on
 * how the controller behaves.
 * ========================================================================= */

/* Gesture name strings for the status bar */
static const char *GESTURE_NAMES[] = {
    "IDLE",
    "POINTER  — cursor mode",
    "DRAG     — hold & drag",
    "SCROLL   — scroll wheel",
    "SWITCH   — Cmd+Tab",
    "MINIMIZE — Cmd+M",
    "MAXIMIZE — Cmd+Ctrl+F",
};

static const cv::Scalar GESTURE_COLORS[] = {
    cv::Scalar(128, 128, 128),   /* NONE    — grey                         */
    cv::Scalar(100, 255, 100),   /* POINTER — green                        */
    cv::Scalar(255, 120,   0),   /* DRAG    — orange                       */
    cv::Scalar(255, 200,   0),   /* SCROLL  — yellow                       */
    cv::Scalar(200,   0, 255),   /* SWITCH  — purple                       */
    cv::Scalar(  0,  80, 255),   /* MINIMIZE— red-blue                     */
    cv::Scalar(  0, 255, 200),   /* MAXIMIZE— cyan                         */
};

static const char *LEGEND[] = {
    "index only  -> POINTER (cursor)",
    "fist        -> DRAG",
    "peace (2)   -> SCROLL",
    "3 fingers   -> SWITCH WINDOW",
    "4 fingers   -> MINIMIZE",
    "open hand   -> MAXIMIZE",
    NULL
};

static void draw_hud(cv::Mat       &frame,
                     const GestureResult &result,
                     double         fps,
                     bool           show_legend)
{
    int W = frame.cols;
    int H = frame.rows;

    /* --- Gesture pill at the top --- */
    int g = (int)result.gesture;
    cv::Scalar col = GESTURE_COLORS[g];

    cv::rectangle(frame, cv::Point(10, 10), cv::Point(W-10, 48),
                  cv::Scalar(0,0,0), -1);
    cv::rectangle(frame, cv::Point(10, 10), cv::Point(W-10, 48), col, 2);
    cv::putText(frame, GESTURE_NAMES[g],
                cv::Point(18, 36),
                cv::FONT_HERSHEY_SIMPLEX, 0.65, col, 2);

    /* --- FPS counter (bottom-right) --- */
    char fps_buf[32];
    snprintf(fps_buf, sizeof(fps_buf), "FPS %.0f", fps);
    cv::putText(frame, fps_buf,
                cv::Point(W - 90, H - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(180,180,180), 1);

    /* --- Hand indicator dot --- */
    if (result.hand_detected) {
        int dot_x = (int)(result.norm_x * W);
        int dot_y = (int)(result.norm_y * H);
        cv::circle(frame, cv::Point(dot_x, dot_y), 8, col, -1);
    }

    /* --- Legend (toggleable with 'L') --- */
    if (show_legend) {
        int y = 60;
        for (int i = 0; LEGEND[i]; ++i) {
            cv::putText(frame, LEGEND[i],
                        cv::Point(14, y),
                        cv::FONT_HERSHEY_SIMPLEX, 0.42,
                        cv::Scalar(220,220,220), 1);
            y += 20;
        }
    }
}

/* =========================================================================
 * SECTION 2 — ARGUMENT PARSING
 * ========================================================================= */

struct Args {
    int  camera     = CAMERA_AUTO;   /* built-in camera unless --camera N */
    bool no_window  = false;
    bool debug      = false;
    bool list       = false;
};

static Args parse_args(int argc, char **argv)
{
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-window") == 0) a.no_window = true;
        if (strcmp(argv[i], "--debug")     == 0) a.debug     = true;
        if (strcmp(argv[i], "--list")      == 0) a.list      = true;
        if (strcmp(argv[i], "--camera") == 0 && i+1 < argc)
            a.camera = atoi(argv[++i]);
    }
    return a;
}

/* =========================================================================
 * SECTION 3 — MAIN LOOP
 * ========================================================================= */

int main(int argc, char **argv)
{
    Args args = parse_args(argc, argv);

    if (args.list) { camera_list(); return 0; }

    /* --- Open camera --- */
    Camera cam = {};
    if (camera_open(&cam, args.camera, CFG_FRAME_W, CFG_FRAME_H) != 0)
        return 1;

    /* --- Get screen size for the controller ---
     * CGMainDisplayID() returns the display with the menu bar.
     * CGDisplayBounds() gives its dimensions in points (=pixels on non-HiDPI).
     */
    CGRect screen = CGDisplayBounds(CGMainDisplayID());
    int scr_w = (int)screen.size.width;
    int scr_h = (int)screen.size.height;
    printf("[main] Screen: %d × %d\n", scr_w, scr_h);

    /* --- Initialise controller state --- */
    ControllerState ctrl = {};
    controller_init(&ctrl);

    printf("[main] Running. Press Q in preview to quit.\n");

    /* --- FPS measurement --- */
    double   fps       = 0;
    int64_t  tick_prev = cv::getTickCount();
    bool     show_legend = true;

    cv::Mat frame;

    while (true) {

        /* 1. Grab frame */
        if (!camera_read(&cam, &frame)) continue;

        /* 2. Mirror: flip horizontally so your right hand appears on the
         *    right side of the preview (webcams default to a mirror image). */
        cv::flip(frame, frame, 1);

        /* 3. Detect gesture */
        GestureResult result = gesture_detect(frame);

        /* 4. Execute macOS input */
        controller_update(&ctrl, &result, scr_w, scr_h);

        /* 5. FPS calculation
         *    cv::getTickCount() and cv::getTickFrequency() are monotonic
         *    high-resolution timers — faster than clock() on macOS.     */
        int64_t tick_now = cv::getTickCount();
        fps = cv::getTickFrequency() / (double)(tick_now - tick_prev);
        tick_prev = tick_now;

        /* 6. Preview window */
        if (!args.no_window) {
            if (args.debug)
                gesture_draw_debug(frame, result);

            draw_hud(frame, result, fps, show_legend);
            cv::imshow("Gesture Control", frame);

            int key = cv::waitKey(1) & 0xFF;
            if (key == 'q' || key == 'Q') break;
            if (key == 'l' || key == 'L') show_legend = !show_legend;
            if (key == 'd' || key == 'D') args.debug  = !args.debug;
        }
    }

    camera_close(&cam);
    if (!args.no_window) cv::destroyAllWindows();
    printf("[main] Stopped.\n");
    return 0;
}
