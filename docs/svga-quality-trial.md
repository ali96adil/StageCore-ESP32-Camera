# SVGA capture experiment — not production firmware

This branch is refreshed on the current camera Wi-Fi/MJPEG candidate and inherits the saved-network recovery behavior from PR #3.

- `esp32cam_stream`: VGA 640x480 with PSRAM.
- `esp32cam_stream_svga_trial`: SVGA 800x600 with PSRAM.
- Without PSRAM both remain QVGA fallback.
- Health metadata reflects the selected frame size.
- Wi-Fi recovery, setup/recovery AP behavior, endpoints and one-upstream relay contract are shared with the current PR #3 source.

The earlier physical SVGA evidence remains valid only for that older exact firmware head. This refreshed candidate must not be treated as physically qualified until the Wi-Fi-recovery build is flashed and retested.

Before promotion, repeat: camera health, relay ingestion FPS/frame age, real tablet visual quality, controlled viewer release, saved-show-Wi-Fi outage/recovery, and rollback verification. Keep the known-good VGA image available and do not expose Wi-Fi credentials.
