# validator

Camera/mic checks for hardware under development: LED state and color via a camera, speaker output via a microphone.

## Layout

Capture scripts are camera-specific and prefixed with the camera they target (`<camera>_capture_*.py`).
Analysis scripts take an image or WAV file and are camera-independent.

| Script | Purpose |
| --- | --- |
| `c920_capture_leds.py` | Logitech C920: low-exposure capture so LEDs show true color |
| `analyze_image.py` | Lists bright saturated blobs (candidate lit LEDs) in an image |
| `analyze_audio.py` | Level, band energy and prominent tones in a WAV file |

To support a new camera, add `<camera>_capture_leds.py` producing a JPEG/PNG, and a section below.

Requires `v4l-utils ffmpeg alsa-utils python3-opencv python3-numpy python3-scipy`, and membership in the
`video` and `audio` groups (or sudo).

## Logitech C920

Devices: video `/dev/video0` (`/dev/video1` is a metadata node), mic ALSA `plughw:C920`.

```bash
python3 c920_capture_leds.py leds.jpg                 # exposure 3 (default) — individual LEDs visible with room lights on
python3 c920_capture_leds.py leds.jpg --exposure 10   # brighter, if LEDs are dim
python3 analyze_image.py leds.jpg

arecord -D plughw:C920 -f S16_LE -r 48000 -c 1 -d 5 clip.wav
python3 analyze_audio.py clip.wav
```

The C920 resets exposure and white balance when a stream starts, so settings applied with `v4l2-ctl` beforehand
have no effect. `c920_capture_leds.py` applies them while the stream is open and restores auto afterwards.

Normal auto-exposed frame (LEDs will blow out to white):

```bash
ffmpeg -f v4l2 -input_format mjpeg -video_size 1920x1080 -i /dev/video0 \
       -vf "select=gte(n\,15)" -frames:v 1 frame.jpg
```

Caveat: at exposure 3 (0.3 ms) a frame may catch only part of an LED's PWM cycle, so brightness/hue can vary
between shots. For precise color, use a longer exposure (5–10 ms+) with an ND filter.
