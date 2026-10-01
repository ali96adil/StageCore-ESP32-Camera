# StageCore ESP32 Camera

ESP32-CAM firmware for StageCore's live stage-camera source. This repository owns **camera-side firmware only**. It does not contain the StageCore Hub, Media Relay, or Android tablet player.

> Status: the serial-only hardware probe **physically passed** on the owner's camera. A separate development Wi-Fi/MJPEG firmware is under CI and awaits physical streaming qualification. StageCore relay and Android integration are not implemented by this repository.

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

- Camera identity and temporary password-protected local Wi-Fi provisioning
- MJPEG HTTP stream, adjustable capture settings, status/health
- Built-in AI Thinker white flash LED control on GPIO 4 through a bounded local HTTP endpoint; default/fail-safe state is OFF
- Local network discovery (mDNS), reconnect, and recovery
- Future authenticated OTA update support
- Explicit compatibility contract for the StageCore camera adapter

The StageCore repository owns relay/discovery orchestration and cue actions. The existing Android tablet application's repository owns local playback, hidden preloading, show/hide, freeze-last-frame, and error fallback. This repository should not duplicate them.

## Available build targets

- `esp32cam_probe`: serial hardware probe, already physically qualified on the photographed unit.
- `esp32cam_stream`: proposed single-consumer HTTP MJPEG + local Wi-Fi provisioning; see [Wi-Fi/MJPEG bring-up](docs/wifi-mjpeg-bringup.md). **Do not infer physical streaming PASS from CI compilation.**

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

## Live-view flash control candidate

The stream firmware exposes `POST /api/v0/flash?state=on|off` on the camera control port. Camera health reports `flash_on`.

This endpoint is intentionally local show-LAN control and is not public-internet authentication. The StageCore Pi relay is expected to be the caller: first downstream Live viewer requests ON, and the final viewer leaving requests OFF. Do **not** tie the light to `stream_active`, because the relay holds its single upstream MJPEG connection even when no tablets are viewing.

The firmware initializes GPIO 4 LOW and forces the flash OFF when camera network services are fenced. Source/CI evidence does not qualify the real LED; the exact firmware must still be flashed and physically observed before first-show promotion.
