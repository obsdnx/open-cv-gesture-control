/*
 * main.cpp — program entry point and per-frame loop
 *
 * PROGRAM FLOW
 * ============
 *
 *   startup
 *     ├─ camera_open()        built-in camera, live frames only
 *     ├─ gesture_init()       load face detector
 *     ├─ controller_init()    check Accessibility permission
 *     └─ calib_load()         no file yet? → start calibration automatically
 *
 *   every frame
 *     ├─ camera_read()        newest frame (320 wide, real aspect ratio)
 *     ├─ cv::flip()           mirror, so moving your hand right moves
 *     │                       the cursor right
 *     ├─ gesture_detect()     fingertip + palm + POINT/FIST
 *     ├─ calibrating?  → calib_step()          (mouse is left alone)
 *     │  otherwise     → controller_update()   (cursor / grab window)
 *     └─ draw preview         camera image ×2, active box, fingertip, status
 *
 * KEYS (preview window focused)
 *   C  recalibrate      D  debug overlay      Q  quit
 *   ]  steadier (less shake, a little more lag)
 *   [  snappier (less lag, a little more shake)
 *
 * BUILD & RUN
 *   make && ./gesture_control
 *   ./gesture_control --list        list cameras
 *   ./gesture_control --camera 1    use another camera
 *   ./gesture_control --calibrate   force calibration at start
 */

#include "camera.h"
#include "gesture.h"
#include "controller.h"
#include "calibration.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <CoreGraphics/CoreGraphics.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

/* =========================================================================
 * SECTION 1 — PREVIEW DRAWING
 *
 * The preview is the processing frame scaled up by CFG_PREVIEW_SCALE.
 * Everything is drawn in preview pixels; P() converts a normalised (0–1)
 * position into those pixels.
 * ========================================================================= */

static cv::Point P(const cv::Mat &img, float nx, float ny)
{
    return cv::Point((int)(nx * img.cols), (int)(ny * img.rows));
}

static void draw_preview(cv::Mat &img, const HandResult &h, const ControllerState &c,
                         const Calibration &cal, bool calibrating)
{
    const ActiveBox &box = cal.box;
    /* Active box: the part of the camera view that maps to your screen. */
    cv::rectangle(img, P(img, box.x0, box.y0), P(img, box.x1, box.y1),
                  cv::Scalar(255, 180, 0), 2);
    cv::putText(img, "screen area", P(img, box.x0, box.y0) + cv::Point(4, -6),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 180, 0), 1);

    if (calibrating) {        /* calibration draws its own banner */
        if (h.hand_detected)
            cv::circle(img, P(img, h.tip_x, h.tip_y), 8, cv::Scalar(0, 255, 255), -1);
        return;
    }

    /* Status pill */
    const char *label; cv::Scalar col;
    if (!h.hand_detected)            { label = "NO HAND";  col = cv::Scalar(140, 140, 140); }
    else if (c.grabbing)             { label = c.window ? "GRAB - moving window" : "GRAB - no window here";
                                       col = cv::Scalar(0, 140, 255); }
    else                             { label = "POINTER";  col = cv::Scalar(80, 230, 80); }

    cv::rectangle(img, cv::Point(10, 10), cv::Point(img.cols - 10, 50), cv::Scalar(0, 0, 0), -1);
    cv::rectangle(img, cv::Point(10, 10), cv::Point(img.cols - 10, 50), col, 2);
    cv::putText(img, label, cv::Point(22, 38), cv::FONT_HERSHEY_SIMPLEX, 0.8, col, 2);

    /* Fingertip (pointer) or palm (grab) marker */
    if (h.hand_detected) {
        if (c.grabbing) cv::circle(img, P(img, h.palm_x, h.palm_y), 14, col, 3);
        else            cv::circle(img, P(img, h.tip_x,  h.tip_y),   9, col, -1);
    }

    char steady[64];
    snprintf(steady, sizeof steady, "steadiness %.2f   [ snappier   ] steadier",
             1.0f / cal.steady.min_cutoff);
    cv::putText(img, steady, cv::Point(12, img.rows - 34), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(255, 180, 0), 1);

    cv::putText(img, "point = move cursor   fist = grab window   C calibrate   D debug   Q quit",
                cv::Point(12, img.rows - 12), cv::FONT_HERSHEY_SIMPLEX, 0.42,
                cv::Scalar(220, 220, 220), 1);
}

/* Debug overlay: hand outline, hull, removed face, solidity, mask inset. */
static void draw_debug(cv::Mat &img, const GestureDebug &d, const HandResult &h, int scale)
{
    auto up = [scale](std::vector<cv::Point> v) {
        for (cv::Point &p : v) p *= scale;
        return v;
    };
    if (!d.contour.empty()) {
        std::vector<std::vector<cv::Point>> cs = { up(d.contour), up(d.hull) };
        cv::drawContours(img, cs, 0, cv::Scalar(0, 255, 0), 2);
        cv::drawContours(img, cs, 1, cv::Scalar(255, 0, 255), 1);
    }
    if (d.face.area() > 0)
        cv::rectangle(img, cv::Rect(d.face.x * scale, d.face.y * scale,
                                    d.face.width * scale, d.face.height * scale),
                      cv::Scalar(0, 0, 255), 2);

    char buf[64];
    snprintf(buf, sizeof buf, "solidity %.2f  (fist > %.2f)", h.solidity, CFG_FIST_SOLIDITY);
    cv::putText(img, buf, cv::Point(14, 76), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(0, 255, 255), 2);

    /* Skin mask thumbnail, top-right: white = what the program thinks is skin. */
    cv::Mat thumb;
    cv::cvtColor(d.mask, thumb, cv::COLOR_GRAY2BGR);
    cv::Rect where(img.cols - thumb.cols - 10, 60, thumb.cols, thumb.rows);
    thumb.copyTo(img(where));
    cv::rectangle(img, where, cv::Scalar(255, 255, 255), 1);
}

/* =========================================================================
 * SECTION 2 — ARGUMENTS
 * ========================================================================= */

struct Args {
    int  camera    = CAMERA_AUTO;
    bool list      = false;
    bool calibrate = false;
};

static Args parse_args(int argc, char **argv)
{
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list"))      a.list = true;
        if (!strcmp(argv[i], "--calibrate")) a.calibrate = true;
        if (!strcmp(argv[i], "--camera") && i + 1 < argc) a.camera = atoi(argv[++i]);
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

    Camera cam = {};
    if (camera_open(&cam, args.camera, CFG_FRAME_W) != 0) return 1;

    gesture_init();

    ControllerState ctrl;
    controller_init(&ctrl);

    /* Screen size in points — the coordinate system CGEvent and AX use. */
    CGRect screen = CGDisplayBounds(CGMainDisplayID());
    int scr_w = (int)screen.size.width;
    int scr_h = (int)screen.size.height;
    printf("[main] Screen %d x %d\n", scr_w, scr_h);

    Calibration cal = calib_default();
    CalSession  cs  = {};
    bool have_file  = calib_load(&cal);
    if (!have_file || args.calibrate) calib_start(&cs);
    else printf("[main] Loaded calibration. Press C in the preview to redo it.\n");

    const char *WIN = "Gesture Control";
    cv::namedWindow(WIN, cv::WINDOW_AUTOSIZE);   /* never stretch the image */

    bool debug = false;
    cv::Mat frame, preview;
    GestureDebug dbg;

    while (true) {
        if (!camera_read(&cam, &frame)) continue;
        cv::flip(frame, frame, 1);                       /* mirror */

        HandResult hand = gesture_detect(frame, cal.skin, debug ? &dbg : nullptr);

        bool calibrating = calib_step(&cs, frame, hand, &cal);
        if (!calibrating)
            controller_update(&ctrl, &hand, &cal, scr_w, scr_h);

        /* --- preview --- */
        cv::resize(frame, preview, cv::Size(), CFG_PREVIEW_SCALE, CFG_PREVIEW_SCALE,
                   cv::INTER_LINEAR);
        if (debug) draw_debug(preview, dbg, hand, CFG_PREVIEW_SCALE);
        draw_preview(preview, hand, ctrl, cal, calibrating);
        calib_draw(&cs, preview, CFG_PREVIEW_SCALE);
        cv::imshow(WIN, preview);

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q' || key == 'Q') break;
        if (key == 'd' || key == 'D') debug = !debug;
        if (key == 'c' || key == 'C') { controller_release(&ctrl); calib_start(&cs); }

        /* Steadiness: lower min_cutoff = heavier smoothing when still.
         * Each press changes it by 25%; saved so it sticks next launch. */
        if (key == ']' || key == '[') {
            float f = (key == ']') ? 1.0f / 1.25f : 1.25f;
            cal.steady.min_cutoff = std::max(0.05f, std::min(5.0f, cal.steady.min_cutoff * f));
            printf("[main] steadiness %.2f\n", 1.0f / cal.steady.min_cutoff);
            calib_save(&cal);
        }
    }

    controller_release(&ctrl);
    camera_close(&cam);
    cv::destroyAllWindows();
    printf("[main] Stopped.\n");
    return 0;
}
