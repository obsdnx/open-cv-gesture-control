/*
 * main.cpp — program entry point and per-frame loop
 *
 * PROGRAM FLOW
 * ============
 *   startup
 *     ├─ camera_open()       built-in camera, live frames only
 *     ├─ hand_model_init()   load the two hand CNNs
 *     ├─ controller_init()   check Accessibility permission
 *     └─ calib_load()        no file yet? → start calibration
 *
 *   every frame
 *     ├─ camera_read() + flip      mirrored, so right = right
 *     ├─ hand_model_detect()       CNN → up to 2 hands × 21 joints
 *     ├─ gesture_analyse()         joints → POINT / FIST / OK
 *     ├─ calibrating? calib_step() : controller_update()
 *     └─ draw preview
 *
 * GESTURES
 *   pointer hand, index out   → move cursor
 *   pointer hand, fist        → hold mouse button (drag); open = release
 *   other hand, OK sign       → left click
 *   open hand, swipe sideways → switch desktop
 *   no hand                   → trackpad works as normal
 *
 * KEYS (preview window focused)
 *   C calibrate   D debug   [ snappier   ] steadier   Q quit
 *
 * BUILD & RUN
 *   make && ./gesture_control         (first `make` downloads the models)
 *   ./gesture_control --list          list cameras
 *   ./gesture_control --camera 1      use another camera
 *   ./gesture_control --calibrate     force calibration at start
 */

#include "camera.h"
#include "hand_model.h"
#include "gesture.h"
#include "controller.h"
#include "calibration.h"
#include "swipe.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef MODEL_DIR
#define MODEL_DIR "models"
#endif

/* =========================================================================
 * SECTION 1 — PREVIEW DRAWING
 * ========================================================================= */

/* Pairs of joints joined by a line when drawing the hand skeleton. */
static const int BONES[][2] = {
    {0,1},{1,2},{2,3},{3,4},           /* thumb  */
    {0,5},{5,6},{6,7},{7,8},           /* index  */
    {5,9},{9,10},{10,11},{11,12},      /* middle */
    {9,13},{13,14},{14,15},{15,16},    /* ring   */
    {13,17},{0,17},{17,18},{18,19},{19,20}, /* pinky + palm edge */
};

static const cv::Scalar COL_POINTER(80, 230, 80);    /* green   */
static const cv::Scalar COL_CLICKER(255, 80, 255);   /* magenta */
static const cv::Scalar COL_BOX    (255, 180, 0);    /* blue    */
static const cv::Scalar COL_TEXT   (230, 230, 230);

static cv::Point P(const cv::Mat &img, float nx, float ny)
{
    return cv::Point((int)(nx * img.cols), (int)(ny * img.rows));
}

static void draw_hand(cv::Mat &img, const Hand &h, const HandInfo &info,
                      const char *role, cv::Scalar col)
{
    for (const auto &b : BONES)
        cv::line(img, h.joint[b[0]], h.joint[b[1]], col, 2);
    for (int i = 0; i < HAND_JOINTS; ++i)
        cv::circle(img, h.joint[i], 3, cv::Scalar(255, 255, 255), -1);
    cv::circle(img, h.joint[8], 7, col, -1);           /* index fingertip */

    char buf[64];
    snprintf(buf, sizeof buf, "%s  %s  %s", role, info.is_right ? "R" : "L", pose_name(info.pose));
    cv::putText(img, buf, h.joint[0] + cv::Point2f(-30, 24), cv::FONT_HERSHEY_SIMPLEX, 0.55, col, 2);
}

static void draw_preview(cv::Mat &img, const std::vector<Hand> &hands, const HandsResult &hr,
                         const ControllerState &c, const Calibration &cal,
                         bool calibrating, bool debug, double ms)
{
    /* Active box = the part of the camera view that maps to your screen. */
    cv::rectangle(img, P(img, cal.box.x0, cal.box.y0), P(img, cal.box.x1, cal.box.y1), COL_BOX, 2);
    cv::putText(img, "screen area", P(img, cal.box.x0, cal.box.y0) + cv::Point(4, -6),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, COL_BOX, 1);

    for (int i = 0; i < hr.count; ++i) {
        bool clicker = (i == c.clicker_idx);
        draw_hand(img, hands[i], hr.hand[i], clicker ? "CLICK HAND" : "POINTER",
                  clicker ? COL_CLICKER : COL_POINTER);
        if (debug) {
            char buf[64];
            snprintf(buf, sizeof buf, "pinch %.2f (OK < %.2f)", hr.hand[i].pinch, CFG_OK_PINCH);
            cv::putText(img, buf, hands[i].joint[0] + cv::Point2f(-30, 46),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1);
        }
    }
    if (calibrating) return;

    /* Status bar */
    const char *label; cv::Scalar col;
    double now = controller_now();
    if (now < c.swipe_flash_until)              { label = c.swipe_flash_dir > 0 ? "SWIPE -> next desktop"
                                                                                : "SWIPE <- previous desktop";
                                                  col = cv::Scalar(0, 220, 255); }
    else if (now < c.click_flash_until)         { label = "CLICK!";                         col = COL_CLICKER; }
    else if (hr.count == 0)                     { label = "NO HAND - trackpad active";      col = cv::Scalar(150, 150, 150); }
    else if (c.paused)                          { label = "PAUSED - you used the trackpad"; col = cv::Scalar(150, 150, 150); }
    else if (c.pressed)                         { label = "PRESSED - dragging";             col = cv::Scalar(0, 140, 255); }
    else                                        { label = "POINTER";                        col = COL_POINTER; }
    cv::rectangle(img, cv::Point(10, 10), cv::Point(img.cols - 10, 50), cv::Scalar(0, 0, 0), -1);
    cv::rectangle(img, cv::Point(10, 10), cv::Point(img.cols - 10, 50), col, 2);
    cv::putText(img, label, cv::Point(22, 38), cv::FONT_HERSHEY_SIMPLEX, 0.8, col, 2);

    if (debug) {
        char buf[64];
        snprintf(buf, sizeof buf, "%.0f ms/frame", ms);
        cv::putText(img, buf, cv::Point(img.cols - 140, 72), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 255, 255), 1);
    }

    char steady[64];
    snprintf(steady, sizeof steady, "steadiness %.2f", 1.0f / cal.steady.min_cutoff);
    cv::putText(img, steady, cv::Point(12, img.rows - 34), cv::FONT_HERSHEY_SIMPLEX, 0.5, COL_BOX, 1);
    cv::putText(img, "C calibrate   D debug   [ snappier   ] steadier   Q quit",
                cv::Point(12, img.rows - 12), cv::FONT_HERSHEY_SIMPLEX, 0.45, COL_TEXT, 1);
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

    if (!hand_model_init(MODEL_DIR)) return 1;

    Camera cam = {};
    if (camera_open(&cam, args.camera, CFG_FRAME_W) != 0) return 1;

    ControllerState ctrl;
    controller_init(&ctrl);

    /* Screen size in points — the coordinate system CGEvent uses. */
    CGRect screen = CGDisplayBounds(CGMainDisplayID());
    int scr_w = (int)screen.size.width, scr_h = (int)screen.size.height;

    Calibration cal = calib_default();
    CalSession  cs  = {};
    if (!calib_load(&cal) || args.calibrate) calib_start(&cs);
    else printf("[main] Calibration loaded. Press C in the preview to redo it.\n");

    const char *WIN = "Gesture Control";
    cv::namedWindow(WIN, cv::WINDOW_AUTOSIZE);

    bool debug = false;
    cv::Mat frame;
    SwipeDetector swipe = {};

    while (true) {
        if (!camera_read(&cam, &frame)) continue;
        cv::flip(frame, frame, 1);                           /* mirror */

        int64 t0 = cv::getTickCount();
        std::vector<Hand> hands = hand_model_detect(frame);
        HandsResult hr = gesture_analyse(hands, frame.cols, frame.rows);
        double ms = (cv::getTickCount() - t0) * 1000.0 / cv::getTickFrequency();

        /* Calibration uses whichever hand is pointing. */
        const HandInfo *pointing = nullptr;
        for (int i = 0; i < hr.count; ++i)
            if (hr.hand[i].pose == POSE_POINT) { pointing = &hr.hand[i]; break; }

        bool calibrating = calib_step(&cs, pointing, &cal);
        if (!calibrating) {
            controller_update(&ctrl, &hr, &cal, scr_w, scr_h);
            if (int dir = swipe_update(&swipe, &hr, controller_now()))
                controller_switch_space(&ctrl, dir);
        }

        draw_preview(frame, hands, hr, ctrl, cal, calibrating, debug, ms);
        calib_draw(&cs, frame);
        cv::imshow(WIN, frame);

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q' || key == 'Q') break;
        if (key == 'd' || key == 'D') debug = !debug;
        if (key == 'c' || key == 'C') { controller_release(&ctrl); calib_start(&cs); }

        /* Steadiness: each press changes min_cutoff by 25%; saved to disk. */
        if (key == ']' || key == '[') {
            float f = (key == ']') ? 1.0f / 1.25f : 1.25f;
            cal.steady.min_cutoff = std::max(0.05f, std::min(5.0f, cal.steady.min_cutoff * f));
            calib_save(&cal);
        }
    }

    controller_release(&ctrl);
    camera_close(&cam);
    cv::destroyAllWindows();
    printf("[main] Stopped.\n");
    return 0;
}
