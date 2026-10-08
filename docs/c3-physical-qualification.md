# C3 attended physical qualification

Status: **SOURCE QUALIFIED / PHYSICAL QUALIFICATION NOT YET RUN**. Do not flash a new image until the specific GitHub CI run and local generated C3 partition table are verified.

Canonical source image:
- Camera main commit: `e57392aa0f9867567b414838f148ac6c7e28b787` (older baseline, **NOT** the migration candidate).
- Candidate: Draft PR #20, branch `chore/foundation-tls-qualification-20261008` including the legacy layout fix.
- Main Firmware build: **#159 PASS** is not sufficient to qualify the new candidate; check its exact-commit PR CI.
- Build target: `esp32cam_v2_foundation_c3`
- The 2026-10-08 physical backup has SHA-256 `edbe87a619e181a43fde59388ccafa3160addee6896cbdabdec960ff4a1f8d03` and size 4,194,304 bytes. Keep this image private; it contains credentials.

### Required partition migration check

The original Arduino 4MB camera dump has NVS at 0x9000/0x5000, otadata 0xe000/0x2000, OTA app0 0x10000/0x300000, SPIFFS 0x310000/0xe0000 and coredump 0x3f0000/0x10000. The default ESP-IDF single-app layout used by C3 **overlapped live OTA metadata**. The C3 candidate now requires `partitions_camera_legacy.csv` and `tests/c3_legacy_partition_test.py` to validate its *generated* `partitions.bin`. A passing CI on an older SHA is not acceptable. **Do not use erase-flash or other destructive flashing commands.**

The backup was obtained through ROM `esptool --no-stub read-flash` because CH340/stub transfers were unstable. A verified backup plus matching partition offsets is a prerequisite, not proof that UART flashing, boot, Wi-Fi credentials or TLS runtime will work. Keep a tested rollback path and use attended bench testing only.

The existing Arduino `esp32cam_stream` firmware remains the rollback path until
this checklist is completed on the real camera.

## Safety and evidence rules

- Do not paste Wi-Fi passwords, Setup/Recovery AP passwords, StageCore tokens,
  private keys, or pairing material into issues or reports.
- CI/source qualification is not physical qualification.
- Keep the camera and relay on the trusted Stage LAN only.
- Flash output must begin OFF and must be verified OFF again after every flash
  test and after a Stage-LAN outage.
- Stop on brownout, repeated reset, camera-init failure, unexpected GPIO
  behavior, or inability to force flash OFF.
- Record the exact Camera commit, StageCore/relay commit, power source, antenna
  configuration, AP/router, RSSI, and date.

## 1. Build and flash the exact C3 image

On the Mac:

```sh
cd ~/StageCore-Camera-C3-qualification-20261008
git fetch origin
git switch chore/foundation-tls-qualification-20261008
git pull --ff-only

source ~/.venv-pio/bin/activate
# Re-generate the C3 SDK config after changing partition defaults.
# Check the exact source revision and CI PASS before taking the next step.
pio run -e esp32cam_v2_foundation_c3
python3 -m unittest tests/c3_legacy_partition_test.py -v

ls /dev/cu.*
pio run -e esp32cam_v2_foundation_c3 -t upload --upload-port /dev/cu.<verified-port>
pio device monitor -p /dev/cu.<verified-port> -b 115200
```

**PAUSE HERE** until the migration gate, physical flash/power setup, exact SHA/CI, and safe serial method have been reviewed. Use the verified programmer port only; it may change between unplug/replug. Do not guess a serial device.

## 2. Boot / identity / Stage LAN

Verify from serial and StageCore:

- no brownout/reset loop
- PSRAM detected
- OV2640 camera init PASS
- stable `stagecam-xxxxxx` camera ID
- saved Stage LAN joins without erasing identity
- device appears in StageCore inventory through authenticated
  `stagecore.device/2`
- device can remain UNASSIGNED and still exposes Setup/Recovery AP maintenance
- no Cue/show authority is acquired by maintenance

If the camera has no saved Stage LAN, first-run Setup AP must be WPA2 protected
with the locally stored override or fallback `12345678`.

## 3. Direct C3 camera smoke

Close any direct browser stream before running this tool; the camera permits one
upstream MJPEG consumer.

From the Mac or Pi on the Stage LAN:

```sh
cd ~/StageCore-ESP32-Camera

python3 tools/c3_physical_smoke.py \
  --camera-host stagecam-xxxxxx.local \
  --health-url http://stagecam-xxxxxx.local/api/v0/health \
  --stream-url http://stagecam-xxxxxx.local:81/api/v0/stream \
  --json-report /tmp/stagecore-camera-c3-smoke.json
```

Required PASS:
- mDNS resolves the stable `.local` name
- health reports C3, `state=ready`, valid RSSI, expected stream contract
- flash begins OFF
- first complete JPEG frame is received from port 81

For the attended flash check:

```sh
python3 tools/c3_physical_smoke.py \
  --camera-host stagecam-xxxxxx.local \
  --health-url http://stagecam-xxxxxx.local/api/v0/health \
  --stream-url http://stagecam-xxxxxx.local:81/api/v0/stream \
  --flash-url http://stagecam-xxxxxx.local/api/v0/flash \
  --exercise-flash
```

The tool toggles ON briefly and always attempts OFF in `finally`. Confirm the
physical LED actually follows ON -> OFF. A software PASS without observing the
LED is not a hardware PASS.

## 4. Relay qualification

Use the StageCore relay, not four direct connections to the ESP32-CAM.

Source:

```text
http://stagecam-xxxxxx.local:81/api/v0/stream
```

Flash control:

```text
http://stagecam-xxxxxx.local/api/v0/flash
```

On the Pi, use the StageCore repository's existing relay smoke tool:

```sh
cd ~/StageCore
python3 tools/camera-relay-smoke.py \
  --base-url http://127.0.0.1:9081 \
  --viewers 4 \
  --seconds 5
```

Required PASS:
- relay health ready
- one camera upstream remains healthy
- four downstream readers receive valid JPEG frames
- fifth relay viewer is rejected by the relay's configured viewer limit
- source frame count continues advancing
- no relay reconnect storm or camera reset

## 5. Relay flash modes

With the relay configured with the camera flash-control URL, test:

- AUTO with no requesting viewer -> physical flash OFF
- AUTO with a requesting viewer -> physical flash ON
- viewer disconnect -> AUTO returns OFF
- Force ON -> physical flash ON
- Force OFF -> physical flash OFF

Finish every sequence in Force OFF or AUTO/no-requesters and visually confirm
the LED is OFF.

## 6. Router / Stage-LAN outage

While the camera is streaming through the relay:

1. force relay/manual flash OFF
2. interrupt the Stage LAN/AP
3. verify the camera flash becomes physically OFF immediately
4. verify authenticated runtime and camera network services go unavailable
   without reset/brownout
5. keep the Stage LAN absent until Recovery AP is due
6. verify protected Recovery AP appears and still uses the managed override or
   fallback `12345678`
7. restore the original Stage LAN
8. verify AP+STA recovery rejoins automatically
9. verify Recovery AP disappears
10. verify stable `stagecam-xxxxxx.local`, health, authenticated v2 runtime,
    relay ingest and MJPEG all return
11. verify flash remains OFF until explicitly requested

## 7. Acceptance

Do not replace the proven show firmware until all of these are recorded PASS:

- C3 boot + PSRAM + OV2640
- StageCore authenticated v2 inventory
- Setup AP + managed password behavior
- Recovery AP + automatic return to saved Stage LAN
- stable mDNS hostname
- health + direct one-upstream MJPEG
- relay four-viewer smoke
- flash AUTO / Force ON / Force OFF
- Wi-Fi-loss forced OFF
- no brownout/reset loop
- acceptable RSSI and sustained stream behavior on the intended show network

After PASS, update Camera issue #11 with the redacted JSON report and measured
results, then the old Arduino stream can remain only as rollback rather than the
active show image.
