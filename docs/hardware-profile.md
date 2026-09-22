# Hardware profile — physically probed camera (2026-09-22)

Evidence: owner photos, esptool v4.9.0 `flash_id` output, and PlatformIO serial hardware probe logs. This is a **user-reported physical test**, not a GitHub CI hardware test.

| Part | Observation | Verification |
| --- | --- | --- |
| Main board | `ESP32-CAM` front silkscreen, `ESP-32S` RF module | Photos |
| USB programmer | Separate `HW-818` base with micro-USB, RST / FLASH | Photos; manual FLASH + RST achieved serial bootloader |
| Chip | `ESP32-D0WDQ6-V3`, revision 3 | esptool and serial probe |
| SPI flash | `4194304` bytes / **4 MB** | esptool detected 4 MB; serial probe confirms |
| PSRAM | Detected; probe reports `4192123` bytes (~4 MB) | Serial probe; retain exact reported number, don't normalize it |
| Sensor | `sensor_pid=0x0026`, consistent with OV2640 package label | Camera init and physical capture PASS |
| Capture | Ten successive `capture=PASS` reports, JPEG `640x480` | Serial probe; no extended soak |
| External antenna | User attached a physical antenna to the U.FL/IPEX jack | RF selection resistor / active antenna path unverified |
| Wi-Fi | Not tested by the serial-only probe | Pending RSSI/stream qualification |

## Camera pinout

This board successfully initialized and captured at VGA using the provisional PlatformIO `board = esp32cam` / AI Thinker pin mapping:

```text
PWDN 32  RESET -1  XCLK 0  SIOD 26  SIOC 27
D7 35  D6 34  D5 39  D4 36  D3 21  D2 19  D1 18  D0 5
VSYNC 25  HREF 23  PCLK 22
```

The camera pin assignment is therefore validated for this photographed and probed specimen. It is not a guarantee about other clones or external GPIO wiring. GPIO0 is also a boot strap; GPIO4 usually controls the flash LED. Do not move antenna RF resistors while powered.

## Observed serial test

- USB port: identified locally and used successfully (omit unit-specific ID from public documents).
- esptool: manual FLASH/RST followed by `--chip esp32 --before no_reset flash_id` read 4 MB flash. Crystal estimated 41.24 MHz with a warning, then normalized to 40 MHz. Read and write worked; investigate only if later instability arises.
- PlatformIO `esp32cam_probe`: flash upload completed in ~23 seconds, with all written image segment hashes verified.
- Boot: `STAGECORE_CAM_PROBE_v0`, `camera_init=PASS`, `sensor_pid=0x0026`, `psram_detected=yes`, and ten consecutive VGA `capture=PASS`.
- No evidence yet of functional Wi-Fi, streaming, four concurrent clients, relay performance, or antenna selection.

## Next gate

Implement Wi-Fi provisioning and a local single-consumer MJPEG endpoint. Validate one-camera-one-relay connection, then separately test StageCore Media Relay fan-out to four tablets. CI compilation cannot replace those measurements.
