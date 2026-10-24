/*
 * calibration.cpp — reach box from fingertip samples, settings file I/O
 * (see calibration.h)
 */

#include "calibration.h"
#include "controller.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const double REACH_SECONDS = 8.0;
static const float  MIN_SPAN      = 0.15f;  /* box must cover ≥15% per axis */
static const int    MIN_SAMPLES   = 30;

static std::string calib_path(void)
{
    const char *home = getenv("HOME");
    return std::string(home ? home : ".") + "/.gesture_control_calibration";
}

/* =========================================================================
 * SECTION 1 — DEFAULTS AND FILE
 * ========================================================================= */

Calibration calib_default(void)
{
    Calibration c;
    /* Upper-middle of the frame: where a raised hand naturally points. */
    c.box    = ActiveBox{0.20f, 0.10f, 0.80f, 0.65f};
    c.steady = Steadiness{CFG_EURO_MIN_CUTOFF, CFG_EURO_BETA};
    return c;
}

/* Line-by-line "key values…" parsing, so unknown/old lines (like the
 * skin line from the previous version) are simply ignored. */
bool calib_load(Calibration *cal)
{
    FILE *f = fopen(calib_path().c_str(), "r");
    if (!f) return false;
    Calibration c = calib_default();
    bool have_box = false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        ActiveBox b;
        if (sscanf(line, "box %f %f %f %f", &b.x0, &b.y0, &b.x1, &b.y1) == 4) { c.box = b; have_box = true; }
        sscanf(line, "filter %f %f", &c.steady.min_cutoff, &c.steady.beta);
    }
    fclose(f);
    if (have_box) *cal = c;
    return have_box;
}

void calib_save(const Calibration *c)
{
    FILE *f = fopen(calib_path().c_str(), "w");
    if (!f) { fprintf(stderr, "[calib] Cannot write %s\n", calib_path().c_str()); return; }
    fprintf(f, "box %.4f %.4f %.4f %.4f\nfilter %.4f %.5f\n",
            c->box.x0, c->box.y0, c->box.x1, c->box.y1,
            c->steady.min_cutoff, c->steady.beta);
    fclose(f);
}

/* =========================================================================
 * SECTION 2 — REACH CALIBRATION
 * ========================================================================= */

static float percentile(std::vector<float> v, float q)
{
    std::sort(v.begin(), v.end());
    return v[(size_t)(q * (v.size() - 1))];
}

void calib_start(CalSession *s)
{
    *s = {};
    s->active = true;
    s->start  = controller_now();
    s->min_x = s->min_y = 1.0f;
    s->max_x = s->max_y = 0.0f;
    printf("[calib] Point your index finger and trace a big rectangle (8 s).\n");
}

bool calib_step(CalSession *s, const HandInfo *p, Calibration *cal)
{
    if (!s->active) return false;

    if (p && p->pose == POSE_POINT) {
        s->xs.push_back(p->tip_x);  s->ys.push_back(p->tip_y);
        s->min_x = std::min(s->min_x, p->tip_x);  s->max_x = std::max(s->max_x, p->tip_x);
        s->min_y = std::min(s->min_y, p->tip_y);  s->max_y = std::max(s->max_y, p->tip_y);
    }
    if (controller_now() - s->start < REACH_SECONDS) return true;

    s->active = false;
    if ((int)s->xs.size() < MIN_SAMPLES) {
        printf("[calib] Pointing finger seen in only %zu frames — kept previous box. Press C to retry.\n",
               s->xs.size());
        return false;
    }
    ActiveBox b = { percentile(s->xs, 0.03f), percentile(s->ys, 0.03f),
                    percentile(s->xs, 0.97f), percentile(s->ys, 0.97f) };
    if (b.x1 - b.x0 < MIN_SPAN || b.y1 - b.y0 < MIN_SPAN) {
        printf("[calib] Reach too small (%.2f x %.2f) — kept previous box. Use bigger arm moves, press C.\n",
               b.x1 - b.x0, b.y1 - b.y0);
        return false;
    }
    cal->box = b;
    calib_save(cal);
    printf("[calib] Saved. Reach box x %.2f-%.2f, y %.2f-%.2f (%zu samples)\n",
           b.x0, b.x1, b.y0, b.y1, s->xs.size());
    return false;
}

void calib_draw(const CalSession *s, cv::Mat &img)
{
    if (!s->active) return;
    if (!s->xs.empty())
        cv::rectangle(img, cv::Point((int)(s->min_x * img.cols), (int)(s->min_y * img.rows)),
                           cv::Point((int)(s->max_x * img.cols), (int)(s->max_y * img.rows)),
                      cv::Scalar(0, 255, 255), 2);

    char buf[64];
    snprintf(buf, sizeof buf, "CALIBRATING  -  %.0f",
             REACH_SECONDS - (controller_now() - s->start) + 0.5);
    cv::rectangle(img, cv::Point(0, img.rows - 70), cv::Point(img.cols, img.rows), cv::Scalar(0, 0, 0), -1);
    cv::putText(img, buf, cv::Point(14, img.rows - 42), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
    cv::putText(img, "Point your index finger and trace a BIG rectangle - all 4 corners",
                cv::Point(14, img.rows - 14), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(230, 230, 230), 1);
}
