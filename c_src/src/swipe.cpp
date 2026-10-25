/*
 * swipe.cpp — fast sideways open-hand swipe detection (see swipe.h)
 */

#include "swipe.h"
#include <cmath>

/* Follow an OPEN hand if there is one, otherwise the first hand. */
static const HandInfo *pick(const HandsResult *h)
{
    for (int i = 0; i < h->count; ++i)
        if (h->hand[i].pose == POSE_OPEN) return &h->hand[i];
    return h->count ? &h->hand[0] : nullptr;
}

int swipe_update(SwipeDetector *d, const HandsResult *hands, double now)
{
    /* 1. Drop samples older than the time window. */
    while (!d->trail.empty() && now - d->trail.front().t > CFG_SWIPE_TIME)
        d->trail.pop_front();

    const HandInfo *h = pick(hands);
    if (!h) return 0;               /* lost frames add nothing — gaps are fine */
    d->trail.push_back(SwipeSample{now, h->palm_x, h->palm_y, h->pose == POSE_OPEN});
    if (d->trail.size() < 2) return 0;

    /* 2. Biggest sideways travel from any earlier sample to the newest. */
    const SwipeSample &b = d->trail.back();
    const SwipeSample *a = nullptr;
    for (const SwipeSample &s : d->trail)
        if (!a || std::fabs(b.x - s.x) > std::fabs(b.x - a->x)) a = &s;
    float dx = b.x - a->x, dy = b.y - a->y;
    if (std::fabs(dx) < CFG_SWIPE_DIST || std::fabs(dx) < 1.5f * std::fabs(dy)) return 0;

    /* 3. Was the hand open at some point during that movement? */
    bool any_open = false;
    for (const SwipeSample &s : d->trail)
        if (s.t >= a->t && s.open) { any_open = true; break; }
    if (!any_open) return 0;

    /* 4. Ignore the return stroke; rate-limit repeats. */
    int dir = dx > 0 ? +1 : -1;
    double since = now - d->last_t;
    if (d->last_dir == -dir && since < CFG_SWIPE_RETURN)   return 0;
    if (d->last_dir ==  dir && since < CFG_SWIPE_SAME_GAP) return 0;

    d->trail.clear();
    d->last_t   = now;
    d->last_dir = dir;
    return dir;
}
