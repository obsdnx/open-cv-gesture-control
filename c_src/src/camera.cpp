/*
 * camera.cpp — OpenCV VideoCapture wrapper
 *
 * WHY OPENCV FOR CAPTURE?
 * On macOS, OpenCV's VideoCapture uses AVFoundation under the hood.
 * It handles device enumeration, colour conversion, and frame
 * synchronisation for us.  We just call grab/retrieve each frame.
 *
 * IMPORTANT: the process must have camera permission in
 * System Settings → Privacy & Security → Camera before this works.
 */

#include "camera.h"
#include <cstdio>

/* -------------------------------------------------------------------------
 * camera_open
 *
 * We explicitly request the AVFoundation backend (cv::CAP_AVFOUNDATION)
 * so OpenCV doesn't fall back to a slower FFMPEG pipe on macOS.
 *
 * CAP_PROP_BUFFERSIZE = 1 tells OpenCV to keep at most 1 decoded frame
 * in its internal queue.  Without this, the queue fills up and you get
 * several seconds of lag — we always want the live frame, not an old one.
 * ------------------------------------------------------------------------- */
int camera_open(Camera *cam, int device, int width, int height)
{
    /* Allocate the C++ object on the heap.  We store a pointer so the
     * C struct doesn't need to know the size of cv::VideoCapture.       */
    cam->cap = new cv::VideoCapture(device, cv::CAP_AVFOUNDATION);

    if (!cam->cap->isOpened()) {
        fprintf(stderr, "[camera] ERROR: cannot open device %d\n", device);
        fprintf(stderr, "[camera] Check: System Settings → Privacy → Camera\n");
        delete cam->cap;
        cam->cap = nullptr;
        return -1;
    }

    /* Request the resolution.  The driver may round to the nearest
     * supported mode — we record what was actually granted.             */
    cam->cap->set(cv::CAP_PROP_FRAME_WIDTH,  width);
    cam->cap->set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cam->cap->set(cv::CAP_PROP_FPS,          CFG_FPS);
    cam->cap->set(cv::CAP_PROP_BUFFERSIZE,   1);   /* always-live frame */

    cam->width  = (int)cam->cap->get(cv::CAP_PROP_FRAME_WIDTH);
    cam->height = (int)cam->cap->get(cv::CAP_PROP_FRAME_HEIGHT);

    /* No blocking warm-up loop — blank/empty frames are skipped by the
     * caller (camera_read returns false, main loop uses continue).      */

    printf("[camera] Opened device %d at %dx%d\n",
           device, cam->width, cam->height);
    return 0;
}

/* -------------------------------------------------------------------------
 * camera_read
 *
 * cv::VideoCapture::read() = grab() + retrieve() in one call.
 * - grab()    : tells the hardware to capture a frame (fast, non-blocking)
 * - retrieve(): decodes the grabbed frame into a cv::Mat (slower)
 *
 * Returns false if the device was disconnected or read failed.
 * ------------------------------------------------------------------------- */
bool camera_read(Camera *cam, cv::Mat *frame)
{
    if (!cam->cap) return false;
    return cam->cap->read(*frame) && !frame->empty();
}

/* -------------------------------------------------------------------------
 * camera_close
 * ------------------------------------------------------------------------- */
void camera_close(Camera *cam)
{
    if (cam->cap) {
        cam->cap->release();
        delete cam->cap;
        cam->cap = nullptr;
    }
}
