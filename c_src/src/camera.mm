/*
 * camera.mm — AVFoundation camera capture (Objective-C++)
 *
 * ".mm" = Objective-C++: lets us mix Apple's Objective-C camera API
 * with C++ (cv::Mat, std::mutex) in one file. Compiled with -fobjc-arc
 * so Objective-C objects are reference-counted automatically.
 *
 * HOW AVFOUNDATION CAPTURE WORKS
 * ==============================
 *
 *   AVCaptureDevice          the physical camera
 *        │
 *   AVCaptureDeviceInput     wraps the device as a session input
 *        │
 *   AVCaptureSession         the pipeline; startRunning() turns it on
 *        │
 *   AVCaptureVideoDataOutput delivers each frame to a delegate method
 *        │                   on a background dispatch queue
 *        ▼
 *   FrameGrabber (delegate)  converts the frame to cv::Mat and stores it
 *                            as "latest", bumping a sequence number
 *        │
 *   camera_read()            main thread waits for a NEW sequence number,
 *                            then copies "latest" out
 *
 * Producer (camera thread) and consumer (main thread) share one slot
 * protected by a mutex. The producer always overwrites, so the consumer
 * can never fall behind — this is what eliminates lag.
 */

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include "camera.h"
#include <opencv2/imgproc.hpp>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdio>

/* =========================================================================
 * SECTION 1 — FRAME GRABBER (runs on the camera's background queue)
 * ========================================================================= */

@interface FrameGrabber : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    std::mutex              mtx;      /* guards latest + seq                */
    std::condition_variable ready;    /* signalled on every new frame       */
    cv::Mat                 latest;   /* newest frame, BGR, output size     */
    uint64_t                seq;      /* increments per frame               */
    int                     out_w;    /* output width; height keeps aspect  */
}
@end

@implementation FrameGrabber

/* Called by AVFoundation once per captured frame. */
- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection
{
    CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pb) return;

    /* Lock the pixel buffer so the CPU can read it. */
    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

    int    w      = (int)CVPixelBufferGetWidth(pb);
    int    h      = (int)CVPixelBufferGetHeight(pb);
    size_t stride = CVPixelBufferGetBytesPerRow(pb);   /* may include padding */
    void  *base   = CVPixelBufferGetBaseAddress(pb);

    /* Output height preserves the sensor's aspect ratio. Forcing a fixed
     * 4:3 size onto a 16:9 sensor is what made the image look squished. */
    int out_h = (int)lround((double)out_w * h / w);

    /* Wrap the buffer as a cv::Mat WITHOUT copying (BGRA, 4 bytes/pixel),
     * then resize + drop alpha into our own memory before unlocking.     */
    cv::Mat bgra(h, w, CV_8UC4, base, stride);
    cv::Mat small, bgr;
    if (w != out_w)
        cv::resize(bgra, small, cv::Size(out_w, out_h), 0, 0, cv::INTER_AREA);
    else
        small = bgra;
    cv::cvtColor(small, bgr, cv::COLOR_BGRA2BGR);

    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

    /* Publish: overwrite the slot and wake the main thread. */
    {
        std::lock_guard<std::mutex> lock(mtx);
        latest = bgr;
        ++seq;
    }
    ready.notify_one();
}

@end

/* =========================================================================
 * SECTION 2 — INTERNAL STATE behind Camera::impl
 * ========================================================================= */

struct CameraImpl {
    AVCaptureSession *session;
    FrameGrabber     *grabber;
    dispatch_queue_t  queue;
    uint64_t          last_seq;   /* last frame handed to the caller */
};

/* =========================================================================
 * SECTION 3 — DEVICE DISCOVERY
 * ========================================================================= */

/* All video devices: built-in, USB, iPhone (Continuity), Desk View. */
static NSArray<AVCaptureDevice *> *all_cameras(void)
{
    NSArray *types = @[
        AVCaptureDeviceTypeBuiltInWideAngleCamera,
        AVCaptureDeviceTypeExternal,
        AVCaptureDeviceTypeContinuityCamera,
        AVCaptureDeviceTypeDeskViewCamera,
    ];
    AVCaptureDeviceDiscoverySession *ds =
        [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:types
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];
    return ds.devices;
}

void camera_list(void)
{
    NSArray<AVCaptureDevice *> *devs = all_cameras();
    printf("Cameras:\n");
    for (NSUInteger i = 0; i < devs.count; ++i) {
        AVCaptureDevice *d = devs[i];
        bool builtin = [d.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInWideAngleCamera];
        printf("  [%lu] %s%s\n", (unsigned long)i,
               d.localizedName.UTF8String,
               builtin ? "   <- built-in (default)" : "");
    }
}

/* =========================================================================
 * SECTION 4 — PERMISSION
 *
 * If permission was never asked, request it and block until the user
 * answers the macOS dialog. Returns true if access is granted.
 * ========================================================================= */

static bool ensure_permission(void)
{
    AVAuthorizationStatus st =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    if (st == AVAuthorizationStatusAuthorized) return true;

    if (st == AVAuthorizationStatusNotDetermined) {
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block BOOL granted = NO;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL ok) {
            granted = ok;
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        if (granted) return true;
    }

    fprintf(stderr,
        "[camera] Camera access denied.\n"
        "         System Settings > Privacy & Security > Camera >\n"
        "         enable the terminal app you are running this from.\n");
    return false;
}

/* =========================================================================
 * SECTION 5 — PUBLIC API
 * ========================================================================= */

int camera_open(Camera *cam, int device, int width)
{
    cam->impl = nullptr;
    if (!ensure_permission()) return -1;

    /* --- Pick the device --- */
    NSArray<AVCaptureDevice *> *devs = all_cameras();
    if (devs.count == 0) {
        fprintf(stderr, "[camera] No cameras found.\n");
        return -1;
    }

    AVCaptureDevice *dev = nil;
    if (device == CAMERA_AUTO) {
        /* Prefer the built-in camera; never auto-pick the iPhone. */
        for (AVCaptureDevice *d in devs)
            if ([d.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInWideAngleCamera]) {
                dev = d; break;
            }
        if (!dev) dev = devs[0];
    } else if (device >= 0 && (NSUInteger)device < devs.count) {
        dev = devs[device];
    } else {
        fprintf(stderr, "[camera] No camera at index %d.\n", device);
        camera_list();
        return -1;
    }

    /* --- Build the session --- */
    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    /* 1280x720 is the native 16:9 mode of Mac FaceTime cameras — full
     * field of view, no cropping. The grabber shrinks it to `width`.    */
    if ([session canSetSessionPreset:AVCaptureSessionPreset1280x720])
        session.sessionPreset = AVCaptureSessionPreset1280x720;

    NSError *err = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
    if (!input || ![session canAddInput:input]) {
        fprintf(stderr, "[camera] Cannot open %s: %s\n",
                dev.localizedName.UTF8String,
                err ? err.localizedDescription.UTF8String : "unknown error");
        return -1;
    }
    [session addInput:input];

    /* --- Output: BGRA pixels, drop late frames (the key to low latency) --- */
    AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
    output.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    output.alwaysDiscardsLateVideoFrames = YES;

    FrameGrabber *grabber = [[FrameGrabber alloc] init];
    grabber->seq   = 0;
    grabber->out_w = width;

    dispatch_queue_t queue = dispatch_queue_create("camera.frames", DISPATCH_QUEUE_SERIAL);
    [output setSampleBufferDelegate:grabber queue:queue];

    if (![session canAddOutput:output]) {
        fprintf(stderr, "[camera] Cannot add video output.\n");
        return -1;
    }
    [session addOutput:output];

    [session startRunning];   /* camera LED turns on here */

    CameraImpl *impl = new CameraImpl{session, grabber, queue, 0};
    cam->impl   = impl;
    cam->width  = width;

    printf("[camera] Using \"%s\"\n", dev.localizedName.UTF8String);
    return 0;
}

bool camera_read(Camera *cam, cv::Mat *frame)
{
    CameraImpl *impl = (CameraImpl *)cam->impl;
    if (!impl) return false;

    FrameGrabber *g = impl->grabber;
    std::unique_lock<std::mutex> lock(g->mtx);

    /* Sleep until the grabber publishes a frame we haven't seen yet. */
    bool got = g->ready.wait_for(lock, std::chrono::seconds(2),
                                 [&]{ return g->seq != impl->last_seq; });
    if (!got) {
        fprintf(stderr, "[camera] No frames for 2 s — is the camera in use by another app?\n");
        return false;
    }

    impl->last_seq = g->seq;
    g->latest.copyTo(*frame);
    return true;
}

void camera_close(Camera *cam)
{
    CameraImpl *impl = (CameraImpl *)cam->impl;
    if (!impl) return;
    [impl->session stopRunning];
    delete impl;               /* ARC releases session/grabber/queue */
    cam->impl = nullptr;
}
