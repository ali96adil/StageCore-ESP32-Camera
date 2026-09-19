# Initial USB hardware probe (NOT a production release)

The first firmware only prints chip/flash/PSRAM information, initializes the camera using the provisional AI Thinker map, and attempts a JPEG capture every five seconds. It does **not** start Wi-Fi, MJPEG, mDNS, or the StageCore relay.

## Prerequisites
- Confirm the USB base and ESP32-CAM are aligned and fully seated.
- Connect the USB base to a Mac with a USB cable capable of carrying data.
- Install Python 3 and `platformio==6.1.18` in a virtual environment on the Mac.
- Source: `platformio.ini`; build environment: `esp32cam_probe`.

## Build (Mac terminal)

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install platformio==6.1.18
pio run -e esp32cam_probe
```

**Before flashing**, find and verify the correct `/dev/cu.*` port; ensure it is the ESP32-CAM programmer, not another USB device. This probe build inherits PlatformIO's reference 4 MB board profile; don't flash it if the actual flash capacity differs.

When the port and flash capacity are confirmed, upload with `pio run -e esp32cam_probe -t upload --upload-port /dev/cu.<verified-port>` (replace the placeholder). If automatic bootloader entry fails, use the base's FLASH and RST buttons while uploading; release FLASH for normal boot. Monitor using `pio device monitor -b 115200 -p /dev/cu.<verified-port>`. Some bases implement their buttons differently; verify the USB programmer behavior rather than forcing unknown wiring.

Record logs showing `STAGECORE_CAM_PROBE_v0`, `flash_bytes`, `psram_detected`, `sensor_pid`, `camera_init=PASS`, and repeated `capture=PASS` without reset/brownout. If init fails, **stop**, collect the exact error and boot log; don't guess a pinout.

CI passing indicates only that firmware was compiled; hardware qualification, Wi-Fi configuration, livestream and four-tablet relay are not included.
