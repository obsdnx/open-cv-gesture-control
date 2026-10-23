/*
 * camera.h — thin wrapper around OpenCV VideoCapture.
 *
 * WHY A WRAPPER?
 * We hide the OpenCV C++ object behind a plain C struct so that
 * main.cpp and gesture.cpp don't need to know it's OpenCV underneath.
 * If we ever swap to AVFoundation or ffmpeg we only change camera.cpp.
 *
 * USAGE:
 *   Camera cam;
 *   if (camera_open(&cam, 0, 320, 240) != 0) { ... error ... }
 *   cv::Mat frame;
 *   while (camera_read(&cam, &frame)) { ... process frame ... }
 *   camera_close(&cam);
 */

#pragma once
#include <opencv2/opencv.hpp>
#include "types.h"

/* Opaque handle — callers treat this as a black box. */
typedef struct {
    cv::VideoCapture *cap;   /* heap-allocated so C++ ctor runs properly */
    int width;
    int height;
} Camera;

/*
 * camera_open — initialise the camera.
 *
 *   device  : OS device index (0 = built-in FaceTime webcam on Mac)
 *   width/h : requested resolution; actual may differ slightly
 *
 * Returns 0 on success, -1 if the device cannot be opened.
 * Prints a diagnostic message on failure.
 */
int  camera_open (Camera *cam, int device, int width, int height);

/*
 * camera_read — grab the next frame.
 *
 *   frame : output Mat; caller must declare it but need not allocate.
 *
 * Returns true if a frame was delivered, false on error or end-of-stream.
 * Non-blocking: if no new frame is ready it blocks until one arrives
 * (OpenCV's default grab-decode behaviour).
 */
bool camera_read (Camera *cam, cv::Mat *frame);

/* camera_close — release the device and free memory. */
void camera_close(Camera *cam);
