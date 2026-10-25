/*
 * calibration.cpp — guided skin + reach calibration, persisted to disk
 *
 * See calibration.h for what the user sees in each step.
 */

#include "calibration.h"
#include "gesture.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const double SKIN_SECONDS  = 3.0;
static const double REACH_SECONDS = 8.0;

/* The reach box must span at least this fraction of the frame on each
 * axis, otherwise the cursor would be far too sensitive — we keep the
 * previous box instead. */
static const float MIN_SPAN = 0.15f;

/* Value at fraction q (0–1) of the sorted samples. */
static float percentile(std::vector<float> v, float q)
{
    std::sort(v.begin(), v.end());
    return v[(size_t)(q * (v.size() - 1))];
}

static double now_s(void)
{
    return (double)cv::getTickCount() / cv::getTickFrequency();
}

static std::string calib_path(void)
{
    const char *home = getenv("HOME");
    return std::string(home ? home : ".") + "/.gesture_control_calibration";
}

/* =========================================================================
 * SECTION 1 — DEFAULTS AND PERSISTENCE
 *
 * File format (plain text, easy to inspect or hand-edit):
 *   skin <h_lo> <h_hi> <s_lo> <s_hi> <v_lo> <v_hi>
 *   box  <x0> <y0> <x1> <y1>
 *   filter <min_cutoff> <beta>      (optional; older files lack it)
 * ========================================================================= */

Calibration calib_default(void)
{
    Calibration c;
    c.skin = gesture_default_skin();
    /* Fingertip rarely reaches the bottom of the frame (your hand would be
     * out of view), so the default box sits in the upper-middle region. */
    c.box  = ActiveBox{0.20f, 0.10f, 0.80f, 0.65f};
    c.steady = Steadiness{CFG_EURO_MIN_CUTOFF, CFG_EURO_BETA};
    return c;
}

bool calib_load(Calibration *cal)
{
    FILE *f = fopen(calib_path().c_str(), "r");
    if (!f) return false;
    Calibration c = calib_default();
    int ok = fscanf(f, "skin %d %d %d %d %d %d\nbox %f %f %f %f",
                    &c.skin.h_lo, &c.skin.h_hi, &c.skin.s_lo, &c.skin.s_hi,
                    &c.skin.v_lo, &c.skin.v_hi,
                    &c.box.x0, &c.box.y0, &c.box.x1, &c.box.y1);
    if (ok == 10)
        fscanf(f, " filter %f %f", &c.steady.min_cutoff, &c.steady.beta);
    fclose(f);
    if (ok != 10) return false;
    *cal = c;
    return true;
}

void calib_save(const Calibration *c)
{
    FILE *f = fopen(calib_path().c_str(), "w");
    if (!f) { fprintf(stderr, "[calib] Cannot write %s\n", calib_path().c_str()); return; }
    fprintf(f, "skin %d %d %d %d %d %d\nbox %.4f %.4f %.4f %.4f\nfilter %.4f %.5f\n",
            c->skin.h_lo, c->skin.h_hi, c->skin.s_lo, c->skin.s_hi,
            c->skin.v_lo, c->skin.v_hi,
            c->box.x0, c->box.y0, c->box.x1, c->box.y1,
            c->steady.min_cutoff, c->steady.beta);
    fclose(f);
    printf("[calib] Saved to %s\n", calib_path().c_str());
}

/* =========================================================================
 * SECTION 2 — THE GUIDED FLOW (a small state machine)
 *
 *   CAL_OFF ──C──▶ CAL_SKIN ──3s──▶ CAL_REACH ──6s──▶ CAL_OFF (saved)
 * ========================================================================= */

cv::Rect calib_skin_box(int w, int h)
{
    int side = h / 4;                          /* a palm-sized square */
    return cv::Rect(w / 2 - side / 2, h / 2 - side / 2, side, side);
}

void calib_start(CalSession *s)
{
    *s = {};
    s->phase       = CAL_SKIN;
    s->phase_start = now_s();
    printf("[calib] Step 1: hold your palm inside the box.\n");
}

bool calib_step(CalSession *s, const cv::Mat &frame,
                const HandResult &hand, Calibration *cal)
{
    if (s->phase == CAL_OFF) return false;
    double t = now_s() - s->phase_start;

    if (s->phase == CAL_SKIN) {
        if (t >= SKIN_SECONDS) {
            cal->skin = gesture_sample_skin(frame, calib_skin_box(frame.cols, frame.rows));
            printf("[calib] Skin sampled. Step 2: point to all four corners of your reach.\n");
            s->phase       = CAL_REACH;
            s->phase_start = now_s();
            s->min_x = s->min_y =  1.0f;
            s->max_x = s->max_y =  0.0f;
            s->samples = 0;
            s->xs.clear(); s->ys.clear();
        }
        return true;
    }

    /* CAL_REACH: record every fingertip position (any pose — if the fist
     * threshold misjudges your pointing hand we still want the samples). */
    if (hand.hand_detected) {
        s->xs.push_back(hand.tip_x);
        s->ys.push_back(hand.tip_y);
        s->min_x = std::min(s->min_x, hand.tip_x);
        s->max_x = std::max(s->max_x, hand.tip_x);
        s->min_y = std::min(s->min_y, hand.tip_y);
        s->max_y = std::max(s->max_y, hand.tip_y);
        ++s->samples;
    }

    if (t >= REACH_SECONDS) {
        if (s->samples < 30) {
            printf("[calib] Hand only seen in %d frames — kept previous box.\n"
                   "        Check the debug view (D): your hand should be white in the mask.\n",
                   s->samples);
        } else {
            ActiveBox b = { percentile(s->xs, 0.03f), percentile(s->ys, 0.03f),
                            percentile(s->xs, 0.97f), percentile(s->ys, 0.97f) };
            if (b.x1 - b.x0 >= MIN_SPAN && b.y1 - b.y0 >= MIN_SPAN) {
                cal->box = b;
                printf("[calib] Reach box: x %.2f-%.2f, y %.2f-%.2f (%d samples)\n",
                       b.x0, b.x1, b.y0, b.y1, s->samples);
            } else {
                printf("[calib] Reach too small (%.2f x %.2f, need %.2f) — kept previous box.\n"
                       "        Move your whole arm to the corners and press C to retry.\n",
                       b.x1 - b.x0, b.y1 - b.y0, MIN_SPAN);
            }
        }
        calib_save(cal);
        s->phase = CAL_OFF;
        return false;
    }
    return true;
}

/* =========================================================================
 * SECTION 3 — ON-SCREEN INSTRUCTIONS
 * ========================================================================= */

static void banner(cv::Mat &img, const std::string &line1, const std::string &line2)
{
    cv::rectangle(img, cv::Point(0, img.rows - 70), cv::Point(img.cols, img.rows),
                  cv::Scalar(0, 0, 0), -1);
    cv::putText(img, line1, cv::Point(14, img.rows - 42),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
    cv::putText(img, line2, cv::Point(14, img.rows - 14),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(230, 230, 230), 1);
}

void calib_draw(const CalSession *s, cv::Mat &preview, int scale)
{
    if (s->phase == CAL_OFF) return;
    double t = now_s() - s->phase_start;
    char buf[64];

    if (s->phase == CAL_SKIN) {
        cv::Rect b = calib_skin_box(preview.cols / scale, preview.rows / scale);
        cv::Rect pb(b.x * scale, b.y * scale, b.width * scale, b.height * scale);
        cv::rectangle(preview, pb, cv::Scalar(0, 255, 255), 3);
        snprintf(buf, sizeof buf, "STEP 1/2  -  %.0f", SKIN_SECONDS - t + 0.5);
        banner(preview, buf, "Hold your open palm so it fills the yellow box");
    } else {
        /* Show the extremes collected so far as a growing rectangle. */
        if (s->samples > 0)
            cv::rectangle(preview,
                cv::Point((int)(s->min_x * preview.cols), (int)(s->min_y * preview.rows)),
                cv::Point((int)(s->max_x * preview.cols), (int)(s->max_y * preview.rows)),
                cv::Scalar(0, 255, 255), 2);
        snprintf(buf, sizeof buf, "STEP 2/2  -  %.0f", REACH_SECONDS - t + 0.5);
        banner(preview, buf, "Point one finger and trace a BIG rectangle - all 4 corners");
    }
}
