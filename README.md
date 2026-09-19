# StageCore ESP32 Camera

ESP32-CAM firmware for StageCore's live stage-camera source. This repository owns **camera-side firmware only**. It does not contain the StageCore Hub, Media Relay, or Android tablet player.

> Status: foundation / proposed v0.1 contract. No working firmware, relay, APK integration, or physical qualification is claimed yet.

## Intended live path

```text
ESP32-CAM -- one MJPEG HTTP source --> StageCore Media Relay (Pi 5)
                                       |-- Tablet 1
                                       |-- Tablet 2
                                       |-- Tablet 3
                                       +-- Tablet 4
```

The separate relay should ingest one stream and fan out the **same JPEG frames**, without transcoding. StageCore's cue/control path stays separate from video transport. The Mac is not required to run the show.

## Scope

- Camera identity and safe local Wi-Fi provisioning
- MJPEG HTTP stream, adjustable capture settings, status/health
- Local network discovery (mDNS), reconnect, and recovery
- Future authenticated OTA update support
- Explicit compatibility contract for the StageCore camera adapter

The StageCore repository owns relay/discovery orchestration and cue actions. The existing Android tablet application's repository owns local playback, hidden preloading, show/hide, freeze-last-frame, and error fallback. This repository should not duplicate them.

## First milestone

1. Confirm the exact ESP32-CAM module/pin map and available PSRAM before selecting a board profile.
2. Implement and locally build basic camera capture with a configurable stream.
3. Expose the minimal versioned API described in [docs/protocol-v0.md](docs/protocol-v0.md).
4. Add reproducible firmware build CI and publish versioned firmware artifacts.
5. Physically qualify one camera -> relay -> four tablets; see [docs/qualification.md](docs/qualification.md).

Configuration secrets, Wi-Fi passwords, and device-local data **must not** be committed. Keep provisioning restricted to a local trusted network; don't expose the camera endpoints to the public internet.

## Related project

- [StageCore](https://github.com/ali96adil/StageCore) — control plane and future media relay.

No Flash/OTA instructions are provided until the board profile, partition layout, and rollback strategy are validated.
