#!/usr/bin/env python3
"""
Run this once to grant camera permission to the venv Python binary.
macOS tracks permissions per binary, so .venv/python3.11 needs its own grant.

Usage:  python request_camera_permission.py
"""
import sys
import time

try:
    import AVFoundation as av
except ImportError:
    print("ERROR: pyobjc-framework-AVFoundation not installed.")
    print("Run:  pip install pyobjc-framework-AVFoundation")
    sys.exit(1)

import threading

AVMediaTypeVideo = av.AVMediaTypeVideo
status = av.AVCaptureDevice.authorizationStatusForMediaType_(AVMediaTypeVideo)

STATUS = {0: "Not Determined", 1: "Restricted", 2: "Denied", 3: "Authorized"}
print(f"Current camera auth status: {STATUS.get(status, status)}")

if status == 3:
    print("Camera already authorized. You can run main.py.")
    sys.exit(0)

if status == 2:
    print("\nCamera access was DENIED.")
    print("Fix: System Settings → Privacy & Security → Camera → enable your terminal app.")
    sys.exit(1)

if status == 1:
    print("Camera access is restricted (MDM/parental controls).")
    sys.exit(1)

# Status 0: not yet asked — request it now
print("Requesting camera permission — approve the dialog that appears…")

granted_event = threading.Event()
result = [False]

def handler(granted):
    result[0] = granted
    granted_event.set()

av.AVCaptureDevice.requestAccessForMediaType_completionHandler_(
    AVMediaTypeVideo, handler
)

granted_event.wait(timeout=30)

if result[0]:
    print("\nCamera access GRANTED. Run main.py now.")
else:
    print("\nCamera access DENIED or timed out.")
    print("Fix: System Settings → Privacy & Security → Camera → enable your terminal app.")
