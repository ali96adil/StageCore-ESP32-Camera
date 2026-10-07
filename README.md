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
- Local network discovery (mDNS), reconnect, and recovery
- Future authenticated OTA update support
- Explicit compatibility contract for the StageCore camera adapter

The StageCore repository owns relay/discovery orchestration and cue actions. The existing Android tablet application's repository owns local playback, hidden preloading, show/hide, freeze-last-frame, and error fallback. This repository should not duplicate them.

## Available build targets

- `esp32cam_probe`: serial hardware probe, already physically qualified on the photographed unit.
- `esp32cam_stream`: proposed single-consumer HTTP MJPEG + local Wi-Fi provisioning; see [Wi-Fi/MJPEG bring-up](docs/wifi-mjpeg-bringup.md). **Do not infer physical streaming PASS from CI compilation.**\n- `esp32cam_foundation_v2_candidate`: **CI/source-only** ESP-IDF security candidate for persistent UUID/P-256 identity, pinned Hub discovery, pairing/authentication, and an inventory-only `stagecore.device/2` socket. It does not initialize the camera, stream MJPEG, accept flash commands, or mutate the Setup AP password.

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

## Setup Wi-Fi access point

First-run provisioning and saved-network recovery use a device-specific camera SSID with the shared StageCore setup password `12345678`. Serial Monitor is no longer required to discover a random AP password. The password may be overridden at build time with `STAGECORE_SETUP_AP_PASSWORD`; keep the same value across StageCore devices when using that override.


## Firmware Foundation v2 migration

Camera migration is staged so the proven Arduino stream image remains available
for rollback. The first native ESP-IDF candidate is deliberately inventory-only:

- reuses the Wi-Fi credentials already saved by the proven Camera setup portal;
- creates and persists a UUID plus P-256 device identity;
- discovers one eligible StageCore Hub and pins its advertised TLS SHA-256;
- uses the normal StageCore pairing/authentication flow;
- connects to the Stage Device v2 runtime only as UNASSIGNED / BLOCKER;
- holds the known flash LED GPIO low;
- provides no MJPEG, camera control, flash command, Cue authority, or Setup AP
  password mutation.

Remote Setup/Recovery AP password management remains disabled until the secure
candidate is proven and the C2 maintenance slice adds the Foundation capability.
