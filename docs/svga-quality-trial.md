# SVGA capture experiment — not production firmware

## Scope

This branch stacks on Draft PR #3 (`feat/wifi-mjpeg-v0`), **not main**.
The existing `esp32cam_stream` environment remains VGA 640x480 when PSRAM
is present, and 320x240 otherwise. The opt-in `esp32cam_stream_svga_trial`
environment requests SVGA 800x600 when PSRAM is present; without PSRAM it
still uses 320x240. Health metadata reflects the requested size.
No endpoints, streaming fan-out, camera Project ID, Wi-Fi recovery, or
tablet player logic changes. This experiment does **not** fix weak Wi-Fi.

## Baseline before any flash

Record 4+ consecutive samples at intervals of 10 s while all four tablets
show the same relay feed: camera `/api/v0/health` RSSI and state, relay
`/api/v0/health` frames_received, last_frame_age_ms, viewers and state.
Calculate observed relay upstream FPS as delta frames_received / delta
seconds, not target_fps. Record whether all four tablet images visibly
update, drop or freeze, and estimated latency. Capture the installed
camera firmware identity. If RSSI remains ~-80 dBm or worse, improve antenna
selection / placement / AP proximity and retest **before** higher resolution.
Do not change RF solder selector while powered.

## Build and physical trial (explicit owner approval required)

After baseline and backup/rollback preparation, build locally using
`pio run -e esp32cam_stream_svga_trial`. CI compiles both environments
but passing CI is not a physical quality gate. Verify Mac USB serial
port and board, 4 MB flash and PSRAM before flashing; preserve stored
credentials (do not run erase_flash). Never publish Wi-Fi credentials.
Only one video source client is supported on camera port 81: connect
Pi relay as upstream and tablets to relay port 9081.

After flashing, confirm camera health returns width=800 and height=600;
compare steady-state upstream FPS, frame age, Wi-Fi RSSI, visual quality,
four actual tablets' playback and latency with VGA baseline. New JPEG
sizes must remain below the relay 512 KiB single-frame limit.
Repeat first-frame, Hide Live viewer slot release and bounded fifth
viewer rejection. If instability/latency rises or Wi-Fi disconnects,
stop trial and restore the known-working `esp32cam_stream` baseline
using the verified programmer and port. Never merge before physical results.

## Separate limitation

Automatic recovery of saved show Wi-Fi after transient outage is tracked
by issue #4; this experiment does not change connection behavior.
