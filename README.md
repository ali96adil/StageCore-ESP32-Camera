# StageCore ESP32 Camera

ESP32-CAM firmware for StageCore's live stage-camera source. This repository owns **camera-side firmware only**. It does not contain the StageCore Hub, Media Relay, or Android tablet player.

> Status: the serial-only hardware probe and legacy Arduino one-viewer MJPEG smoke **physically passed** on the owner's camera. The unified authenticated ESP-IDF C3 image is source-qualified on main (CI #159 PASS) and now awaits attended physical qualification before replacing the proven show firmware.

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
- `esp32cam_stream`: proposed single-consumer HTTP MJPEG + local Wi-Fi provisioning; see [Wi-Fi/MJPEG bring-up](docs/wifi-mjpeg-bringup.md). **Do not infer physical streaming PASS from CI compilation.**
- `esp32cam_v2_foundation_c1`: isolated authenticated inventory-only rollback candidate; no camera/output path.
- `esp32cam_v2_foundation_c2`: adds authenticated Firmware Foundation maintenance, protected Setup/Recovery AP, managed AP password override/reset, and bounded reconnect; no camera/output path.
- `esp32cam_v2_foundation_c3`: unified source-qualified candidate with C2 security/maintenance plus the proven AI Thinker OV2640 mapping, one-upstream MJPEG/relay contract, mDNS, health and flash-safe service. **Physical C3 qualification is still required.**
- Attended C3 procedure: [docs/c3-physical-qualification.md](docs/c3-physical-qualification.md).

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


## Firmware Foundation v1

Software migration is complete through C3 while preserving the proven Arduino
stream image as rollback. C3 combines authenticated `stagecore.device/2`,
managed Setup/Recovery AP password maintenance, protected first-run/recovery,
bounded reconnect, the qualified AI Thinker/OV2640 pin map, one-upstream MJPEG,
mDNS, health and flash-safe behavior.

The camera HTTP service remains unable to mutate Wi-Fi or Setup/Recovery AP
credentials. Password maintenance stays authenticated and separate from
camera/flash/Cue/show authority. The remaining gate is attended C3 physical
qualification, not additional Foundation implementation.


## TLS Foundation qualification candidate (2026-10-08)

This **Draft PR branch only** pins the Foundation TLS bootstrap fix from StageCore
[PR #447](https://github.com/ali96adil/StageCore/pull/447), commit
`4495a381cc40f06f44e0a16da4a4dc82a9bceb6c`.
It addresses the ESP-IDF self-signed Hub TLS setup failure encountered on
StageLaser. This is not a production firmware release or permission to flash.

Source build checks (no device update):

```sh
pio run -e esp32cam_v2_foundation_c3
```

C3 is source-qualified, NOT physically qualified for replacement of the Arduino show stream. Keep the proven streaming firmware as the rollback candidate; preserve the existing Wi-Fi/device identity and verify the one-upstream MJPEG, flash OFF, Media Relay and four tablets on attended bench.

**Security gate:** The initial discovery fingerprint is advertised on the local
network. Until the Hub fingerprint is verified through an independently trusted
channel or an authenticated prior binding, a spoofed first discovery must not
be treated as authenticated trust. Review wrong-pin failure, pairing,
reconnect, and credential handling before merging the Foundation fix.

Pass CI and test all affected board targets before any attended physical flash.
