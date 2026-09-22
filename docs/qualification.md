# Physical qualification — ESP32-CAM -> four tablets

Status: **serial hardware probe and initial one-viewer local MJPEG smoke test PASS from user-reported logs (2026-09-22)**. Stream soak, network recovery, Relay, and four-tablet physical qualification remain NOT RUN. Record firmware commit, StageCore/relay commit, APK version, board revision, antenna, power source, router/AP settings, date, and observed results for each run.

## Bench bring-up (camera)

- [x] Confirm module, camera sensor, flash size, PSRAM, and pin mapping (see `docs/hardware-profile.md`).
- [ ] Confirm regulated supply and USB/serial flasher voltage levels.
- [x] Verify serial boot and camera init; STA Wi-Fi connected and stable camera ID reported on first local test. mDNS discovery and IP fallback remain NOT RUN.
- [x] Verify initial /api/v0/health JSON (ready, 640x480, target_fps=12, Wi-Fi RSSI -67 dBm) and visually open MJPEG in one browser. This does not measure actual FPS or streaming stability.
- [ ] Measure /api/v0/health during an active stream and verify recovery after Wi-Fi/power interruption (NOT RUN).
- [ ] Measure streaming source frame rate, frame sizes, dropped frames, and latency (ten VGA JPEG capture frames succeeded in the serial probe, not a streaming performance test).

## Relay

- [ ] Verify exactly one upstream connection from relay to camera, even with four downstream clients.
- [ ] Test one -> two -> four tablets; measure per-client FPS, bandwidth, visible latency, and skew.
- [ ] Verify latest-frame bounded queue: slow/paused client cannot block others or grow memory indefinitely.
- [ ] Verify upstream disconnect/reconnect, stalled stream, and service restart recovery.
- [ ] Verify relay load does not block StageCore Hub's cue execution.

## Stage operation

- [ ] Test prepare all four while hidden, observe per-tablet readiness.
- [ ] GO/show all four; record observed screen-to-screen skew without claiming frame-perfect sync.
- [ ] Hide, stop, repeated cues, tablet reconnect and app restart.
- [ ] Test stream failure fallbacks (freeze, black, previous content) and operator notifications.
- [ ] Repeat on intended Archer C6/AP network alongside DMX, other ESP devices and actual show traffic.
- [ ] Conduct extended soak and unattended recovery test; publish raw measurements and PASS/FAIL.

## Gates

CI build success is **not** physical qualification. Do not label multi-tablet performance PASS until hardware tests and metrics exist. OTA and field deployment need separate verification of partition sizes, firmware compatibility and recovery.
