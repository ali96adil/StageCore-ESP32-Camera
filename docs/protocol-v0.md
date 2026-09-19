# StageCore Camera Protocol v0 (proposal)

**Status:** design contract, not an implemented endpoint list. Freeze only after firmware, relay, and Android integration tests. No endpoint in this document should be assumed live.

## Transport

- Camera -> relay: HTTP MJPEG (`multipart/x-mixed-replace`) over trusted local LAN.
- Relay -> tablets: relay-managed MJPEG fan-out, without decoding or transcoding in the initial release.
- Control: StageCore issues cue actions to tablets; the camera firmware must not be coupled to OSC or tablet IDs.
- Relay should maintain at most **one upstream connection per camera**; downstream clients must have separate bounded latest-frame queues. A slow tablet must not block other viewers.

## Proposed device identity

- Stable `camera_id` (example: `stagecam-01`), not an IP address.
- Advertise `_stagecore-camera._tcp.local.` by mDNS after the endpoint format is frozen; discovery must also work using a configured IP/hostname if multicast is unavailable.
- Device may have a human-readable name; don't use the human-readable name as the identifier.

## Proposed local endpoints

| Endpoint | Purpose | Notes |
| --- | --- | --- |
| `GET /api/v0/health` | Health and capabilities | JSON; should not reveal Wi-Fi credentials or secrets |
| `GET /api/v0/stream` | MJPEG source | One upstream relay consumer in primary deployment |
| `GET /api/v0/config` | Safe non-secret settings | Future; authenticated when available |
| `POST /api/v0/config` | Modify camera settings | Future; authenticated, validated inputs |
| `POST /api/v0/reboot` | Explicit maintenance | Future; authenticated, not part of a live cue |
| `POST /api/v0/ota` | Firmware maintenance | Future only after signed/verified firmware + rollback design |

### Example health response (illustrative only)

```json
{
  "schema_version": 0,
  "camera_id": "stagecam-01",
  "firmware_version": "0.1.0-dev",
  "state": "ready",
  "stream": {
    "path": "/api/v0/stream",
    "format": "mjpeg",
    "width": 640,
    "height": 480,
    "target_fps": 12
  },
  "wifi": { "rssi_dbm": -60 },
  "uptime_s": 120
}
```

Actual resolution, frame rate, bitrate, latency, and stability are **measurement targets**, not guarantees. Avoid embedding passwords, tokens, private IPs in committed examples.

## Tablet cue behavior (owned by StageCore + tablet app)

- `prepare(camera_id)`: ask the tablet to connect to the relay and report readiness while the camera layer stays hidden.
- `show(camera_id)`: reveal an already prepared layer, optionally using a separately configured transition.
- `hide(camera_id)`: hide the layer; keep/disconnect stream according to the scene policy.
- `stop(camera_id)`: release its connection and resources.
- On loss: choose an explicit fallback (freeze last frame, black, or previous content) and report a failure to StageCore.

`GO` dispatch to four tablets is not frame-perfect synchronization; measure actual display skew. Do not claim a guaranteed target before hardware qualification.

## Networking and safety

Use wired Ethernet for Pi/relay when available; the ESP32-CAM and tablets may use Wi-Fi. Keep video and administrative endpoints isolated on the show LAN, without exposing the camera directly to the internet. The relay must impose connection/time/memory limits and propagate upstream disconnect/recovery health.
