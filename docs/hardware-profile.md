# Hardware profile — photographed camera (2026-09-19)

**Identification from the owner's photos; electrical characteristics NOT yet measured.**

| Part | Observed | Confidence |
| --- | --- | --- |
| Main camera board | `ESP32-CAM` front silkscreen, ESP-32S RF module | Visually confirmed |
| Camera | Package says `OV2640 Camera`; flex marked `8225N V2.0 171026` | OV2640 is package identification; sensor PID still needs runtime verification |
| Programmer | Separate USB base marked `HW-818`, micro-USB connector, buttons `RST` and `FLASH` | Visually confirmed; USB-UART operation not tested |
| Antenna | PCB trace antenna and visible U.FL/IPEX connector | Visually confirmed; active RF path not electrically verified |
| Flash | Unknown until read from device | **Do not assume 4 MB** |
| PSRAM | Unknown until read from device | **Do not assume 4 MB** |

## Candidate firmware board

Use PlatformIO `board = esp32cam` with the **AI Thinker pin mapping as a provisional starting point**, not proof that this camera module implements every pin identically; `HW-818` identifies the separate USB base in the photo. Espressif's published reference: https://github.com/espressif/esp32-camera/blob/master/examples/camera_example/main/camera_pinout.h

```text
PWDN 32  RESET -1  XCLK 0  SIOD 26  SIOC 27
D7 35  D6 34  D5 39  D4 36  D3 21  D2 19  D1 18  D0 5
VSYNC 25  HREF 23  PCLK 22
```

These are **camera GPIO assignments**, not an external wiring instruction. GPIO0 is also a boot strap; GPIO4 is typically the bright flash LED. Do not move the RF antenna zero-ohm resistor or change camera flex while powered.

## First physical validation

1. Mount camera securely on USB programmer with correct orientation; do not connect separate 5 V and 3.3 V supplies simultaneously.
2. Attach a known-good **5 V USB data cable** to the USB base; use its RST/FLASH buttons according to the verified base's behavior.
3. On the Mac, discover the actual serial port; verify serial boot and probe logs.
4. Record chip model, `ESP.getFlashChipSize()`, `ESP.getPsramSize()`, PSRAM detection, camera init, detected sensor PID, and repeat JPEG captures.
5. If camera init fails, stop and check module seating, power, actual pinout, sensor type, and boot-mode behavior. Do not guess pin values or override flash size.
6. Only after probe passes, implement local stream + Wi-Fi provisioning; relay-to-four-tablet qualification is a separate gate.

**No hardware flash or test has been executed by ChatGPT.**
