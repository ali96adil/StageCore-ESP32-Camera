# Physical qualification — ESP32-CAM -> four tablets

Status: checklist only. **No tests have been executed.** Record firmware commit, StageCore/relay commit, APK version, board revision, antenna, power source, router/AP settings, date, and the observed results for every run.

## Bench bring-up (camera)

- [ ] Confirm module, camera sensor, flash size, PSRAM, and pin mapping.
- [ ] Confirm regulated supply and USB/serial flasher voltage levels.
- [ ] Verify boot, camera init, stable Wi-Fi, identity, mDNS and IP fallback.
- [ ] Check `health` fields and recover after Wi-Fi/power interruption.
- [ ] Measure source frame rate, frame sizes, dropped frames, and latency.

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
