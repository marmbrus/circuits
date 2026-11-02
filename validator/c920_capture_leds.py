"""Capture a low-exposure frame so LEDs show their true color instead of blowing out to white.

The C920 resets exposure/white balance when a stream starts, so the controls are applied
while the stream is already running, then restored to auto afterwards.

usage: c920_capture_leds.py OUT.jpg [--exposure N] [--wb KELVIN]
"""
import argparse, subprocess, cv2

DEV = "/dev/video0"

def ctl(**kw):
    args = sum([["-c", f"{k}={v}"] for k, v in kw.items()], [])
    subprocess.run(["v4l2-ctl", "-d", DEV] + args, check=True)

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--exposure", type=int, default=3, help="exposure_time_absolute, 3-2047 (default 3)")
ap.add_argument("--wb", type=int, default=4000, help="white balance temperature in K (default 4000)")
a = ap.parse_args()

cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920); cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
try:
    for _ in range(5): cap.read()          # stream is now running
    ctl(auto_exposure=1, exposure_dynamic_framerate=0, white_balance_automatic=0)
    ctl(gain=0, white_balance_temperature=a.wb, exposure_time_absolute=a.exposure)
    for _ in range(10): ok, f = cap.read()  # let the new settings take effect
    if not ok: raise SystemExit("failed to read frame")
    cv2.imwrite(a.out, f)
    print(f"{a.out}: exposure {a.exposure}, brightness {cv2.cvtColor(f, cv2.COLOR_BGR2GRAY).mean():.0f}/255")
finally:
    ctl(auto_exposure=3, white_balance_automatic=1, exposure_dynamic_framerate=1)
    cap.release()
