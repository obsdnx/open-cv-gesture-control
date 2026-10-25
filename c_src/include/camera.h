/*
 * camera.h — native macOS camera capture (AVFoundation), exposed as plain C.
 *
 * WHY NOT cv::VideoCapture?
 *   1. Device order is unpredictable. With an iPhone nearby, macOS
 *      "Continuity Camera" registers the phone as a webcam and OpenCV's
 *      index 0 may be the phone — giving black frames (phone locked) or
 *      heavy lag (frames travel over Wi-Fi).
 *   2. OpenCV's own permission check is unreliable on recent macOS.
 *   3. OpenCV queues frames internally; we want only the newest one.
 *
 * camera.mm talks to AVFoundation directly:
 *   - picks the BUILT-IN camera by device type, not by index
 *   - sets alwaysDiscardsLateVideoFrames = YES  → zero frame backlog
 *   - converts each frame to a cv::Mat (BGR) on a background queue
 *
 * The Objective-C objects are hidden behind a void* so callers
 * (main.cpp) stay plain C++ and never see Objective-C syntax.
 *
 * USAGE:
 *   Camera cam;
 *   camera_open(&cam, CAMERA_AUTO, 320, 240);
 *   cv::Mat frame;
 *   while (camera_read(&cam, &frame)) { ... }
 *   camera_close(&cam);
 */

#pragma once
#include <opencv2/core.hpp>
#include "types.h"

/* Pass as `device` to select the built-in FaceTime camera automatically. */
#define CAMERA_AUTO (-1)

typedef struct {
    void *impl;    /* CameraImpl* (Objective-C++ internals, see camera.mm) */
    int   width;   /* size of frames returned by camera_read               */
    int   height;
} Camera;

/* Print every camera macOS can see, with the index to pass to --camera. */
void camera_list(void);

/*
 * camera_open — start capturing.
 *   device : CAMERA_AUTO (built-in camera) or an index from camera_list()
 *   width/height : size of the frames handed to the caller (resized
 *                  from the 640x480 sensor stream)
 * Returns 0 on success, -1 on failure (prints the reason).
 */
int  camera_open (Camera *cam, int device, int width, int height);

/*
 * camera_read — wait for the NEXT new frame (up to 2 s) and copy it out.
 * Never returns the same frame twice, never returns a stale frame.
 * Returns false on timeout.
 */
bool camera_read (Camera *cam, cv::Mat *frame);

void camera_close(Camera *cam);
