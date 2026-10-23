import subprocess
import threading
import numpy as np


class FFmpegCamera:
    """
    Captures frames via ffmpeg avfoundation.
    A background thread drains the pipe continuously so the main loop
    always gets the most-recent frame with no queue build-up.
    """

    def __init__(self, device_index: int = 0, width: int = 640, height: int = 480,
                 fps: int = 30):
        self.width = width
        self.height = height
        self._frame_bytes = width * height * 3
        self._latest: np.ndarray | None = None
        self._lock = threading.Lock()
        self._running = True

        cmd = [
            "ffmpeg",
            # low-latency flags — disable internal buffering
            "-fflags", "nobuffer",
            "-flags", "low_delay",
            "-probesize", "32",
            "-analyzeduration", "0",
            # input
            "-f", "avfoundation",
            "-framerate", str(fps),
            "-video_size", f"{width}x{height}",
            "-i", f"{device_index}:none",
            # output: raw BGR pixels piped to stdout
            "-vf", "format=bgr24",
            "-f", "rawvideo",
            "-",
        ]
        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self._thread = threading.Thread(target=self._drain, daemon=True)
        self._thread.start()

    def _drain(self):
        """Continuously read frames; only keep the newest."""
        while self._running:
            raw = self._proc.stdout.read(self._frame_bytes)
            if len(raw) < self._frame_bytes:
                break
            frame = np.frombuffer(raw, dtype=np.uint8).reshape(
                self.height, self.width, 3
            ).copy()
            with self._lock:
                self._latest = frame

    def read(self) -> tuple[bool, np.ndarray | None]:
        with self._lock:
            if self._latest is None:
                return False, None
            return True, self._latest.copy()

    def isOpened(self) -> bool:
        return self._proc.poll() is None

    def release(self):
        self._running = False
        self._proc.terminate()
        try:
            self._proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self._proc.kill()
