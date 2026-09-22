# Wi-Fi MJPEG bring-up — v0.1.0-dev.1

**Status:** development firmware. CI build alone does not prove functional Wi-Fi, MJPEG, relay or four-tablet playback. Serial-only probe remains a separate PlatformIO environment.

## Implemented by this build

- First boot: creates a WPA2 setup AP called StageCore-CAM-XXXXXX. A random 16-character setup password is printed **only on the local serial monitor**. Neither network credential is committed.
- Join that AP and open http://192.168.4.1/setup; enter the show's 2.4 GHz Wi-Fi SSID and password. Credentials are stored in NVS. After reboot, firmware connects as a STA; a 20-second initial connect timeout or 45-second sustained disconnect leads to AP setup fallback.
- On connected Wi-Fi: health on port 80 at /api/v0/health; MJPEG on port 81 at /api/v0/stream. Separate server workers keep health accessible while the stream is active.
- A single active JPEG stream consumer is allowed. A second active connection is not supported; the single HTTP stream worker can leave additional clients waiting or timing out instead of returning an immediate 503. Use StageCore Media Relay for four tablets.
- Stable camera ID derived from the interface MAC suffix; mDNS hostname and _stagecore-camera._tcp service on port 80 with stream-port=81 TXT. A configured IP remains a fallback.
- Nominal VGA 640x480 with PSRAM, JPEG quality 12 and approximately 80ms between frames. These are settings, not verified FPS/latency guarantees.

## Build on the Mac

After confirming the previously probed physical board and flash (4MB):

    cd ~/StageCore-ESP32-Camera
    git fetch origin
    git switch feat/wifi-mjpeg-v0
    git pull --ff-only
    source ~/stagecore-cam-probe-venv/bin/activate
    pio run -e esp32cam_stream

Do not flash before matching CI passes. Flashing will replace the serial hardware probe. Enter the proven download mode (hold FLASH, briefly press RST, then release FLASH), then:

    pio run -e esp32cam_stream -t upload --upload-port /dev/cu.usbserial-1130
    pio device monitor -p /dev/cu.usbserial-1130 -b 115200

Serial port is an example only; reconnects may change it. Check ls /dev/cu.* first. Expected first line: STAGECORE_CAM_STREAM_v0.

If the unit has no saved credentials, read its temporary setup AP password **locally** from the serial monitor, join the AP, browse to http://192.168.4.1/setup and save show-network credentials. Do not paste real network credentials into issues or chat. Rejoin the show Wi-Fi.

Once local serial prints the assigned URL, check:

    curl --max-time 5 http://<camera-ip>/api/v0/health

Then open http://<camera-ip>:81/api/v0/stream in a browser for **one viewer**. Close the browser before relay testing. Capture stream behavior, Wi-Fi RSSI, disconnect recovery, FPS, brownout and reset logs.

## Security and limitations

- Local setup uses a temporary randomized AP password, but the STA stream/health endpoints use plaintext unauthenticated HTTP on the **trusted show LAN**. Do not expose them via port forwarding, an untrusted/shared Wi-Fi or the internet. Authentication and signed OTA are future work.
- Single-client admission is not cryptographic relay identity verification. The source does **not** broadcast directly to four tablets; there is no StageCore relay in this repository.
- ESP32-CAM is transmitting JPEG frames, not an H.264 video codec. Frame-perfect synchronization is not guaranteed.
- External antenna attachment does not prove RF antenna selection; qualify with RSSI later. Never change an RF selection resistor with power connected.
