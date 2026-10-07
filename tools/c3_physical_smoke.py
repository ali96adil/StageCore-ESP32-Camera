#!/usr/bin/env python3
"""Attended physical smoke probe for the unified StageCore ESP32-CAM C3 image.

This tool intentionally defaults to read-only checks. It never changes Wi-Fi,
Setup/Recovery AP credentials, Hub trust, or Stage Device assignment.

Optional --exercise-flash briefly toggles the camera flash ON then forces it OFF
again. Use it only while physically attending the camera.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any


MAX_STREAM_BYTES = 1024 * 1024
READ_CHUNK = 8192


class ProbeError(RuntimeError):
    pass


@dataclass
class ProbeResult:
    name: str
    passed: bool
    detail: str

    def as_dict(self) -> dict[str, Any]:
        return {"name": self.name, "passed": self.passed, "detail": self.detail}


def http_json(url: str, timeout: float = 5.0) -> dict[str, Any]:
    req = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        if response.status != 200:
            raise ProbeError(f"{url} returned HTTP {response.status}")
        content_type = response.headers.get("Content-Type", "")
        if "application/json" not in content_type.lower():
            raise ProbeError(f"{url} returned non-JSON Content-Type {content_type!r}")
        data = response.read(64 * 1024)
    try:
        parsed = json.loads(data)
    except json.JSONDecodeError as exc:
        raise ProbeError(f"{url} returned invalid JSON: {exc}") from exc
    if not isinstance(parsed, dict):
        raise ProbeError(f"{url} returned JSON that is not an object")
    return parsed


def validate_health(payload: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if payload.get("state") != "ready":
        errors.append(f"state={payload.get('state')!r}, expected 'ready'")

    firmware = payload.get("firmware_version")
    if not isinstance(firmware, str) or ".c3" not in firmware:
        errors.append(f"firmware_version={firmware!r}, expected C3")

    camera_id = payload.get("camera_id")
    if not isinstance(camera_id, str) or not camera_id.startswith("stagecam-"):
        errors.append(f"camera_id={camera_id!r}, expected stagecam-*")

    stream = payload.get("stream")
    if not isinstance(stream, dict):
        errors.append("stream object missing")
    else:
        if stream.get("path") != "/api/v0/stream":
            errors.append(f"stream.path={stream.get('path')!r}")
        if stream.get("port") != 81:
            errors.append(f"stream.port={stream.get('port')!r}, expected 81")
        if stream.get("format") != "mjpeg":
            errors.append(f"stream.format={stream.get('format')!r}, expected 'mjpeg'")
        if stream.get("width") not in (320, 640):
            errors.append(f"stream.width={stream.get('width')!r}")
        if stream.get("height") not in (240, 480):
            errors.append(f"stream.height={stream.get('height')!r}")

    if payload.get("flash_on") is not False:
        errors.append("flash_on must be false before qualification begins")

    wifi = payload.get("wifi")
    if not isinstance(wifi, dict) or not isinstance(wifi.get("rssi_dbm"), int):
        errors.append("wifi.rssi_dbm missing or non-integer")

    return errors


def resolve_mdns(host: str) -> list[str]:
    normalized = host.rstrip(".")
    infos = socket.getaddrinfo(normalized, None, family=socket.AF_INET)
    addresses = sorted({info[4][0] for info in infos if info[4]})
    if not addresses:
        raise ProbeError(f"{normalized} returned no IPv4 address")
    return addresses


def read_first_jpeg(url: str, timeout: float = 10.0) -> tuple[int, str]:
    req = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        if response.status != 200:
            raise ProbeError(f"stream returned HTTP {response.status}")
        content_type = response.headers.get("Content-Type", "")
        if "multipart/x-mixed-replace" not in content_type.lower():
            raise ProbeError(
                f"stream Content-Type is not multipart MJPEG: {content_type!r}"
            )

        data = bytearray()
        while len(data) < MAX_STREAM_BYTES:
            chunk = response.read(READ_CHUNK)
            if not chunk:
                break
            data.extend(chunk)
            start = data.find(b"\xff\xd8")
            if start < 0:
                continue
            end = data.find(b"\xff\xd9", start + 2)
            if end >= 0:
                frame_bytes = end + 2 - start
                if frame_bytes < 4:
                    raise ProbeError("JPEG frame is implausibly short")
                return frame_bytes, content_type

    raise ProbeError(
        f"no complete JPEG frame found within {MAX_STREAM_BYTES} stream bytes"
    )


def post_flash(control_url: str, on: bool, timeout: float = 5.0) -> dict[str, Any]:
    body = b'{"on":true}' if on else b'{"on":false}'
    req = urllib.request.Request(
        control_url,
        data=body,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=timeout) as response:
        if response.status != 200:
            raise ProbeError(f"flash endpoint returned HTTP {response.status}")
        raw = response.read(4096)
    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ProbeError(f"flash endpoint returned invalid JSON: {exc}") from exc
    if payload.get("flash_on") is not on:
        raise ProbeError(
            f"flash endpoint reported flash_on={payload.get('flash_on')!r}, expected {on}"
        )
    return payload


def run(args: argparse.Namespace) -> tuple[list[ProbeResult], dict[str, Any]]:
    results: list[ProbeResult] = []
    evidence: dict[str, Any] = {}

    if args.camera_host:
        try:
            addresses = resolve_mdns(args.camera_host)
            results.append(
                ProbeResult("mDNS", True, f"{args.camera_host} -> {', '.join(addresses)}")
            )
            evidence["mdns_ipv4"] = addresses
        except Exception as exc:
            results.append(ProbeResult("mDNS", False, str(exc)))

    try:
        health = http_json(args.health_url, args.timeout)
        evidence["health"] = health
        errors = validate_health(health)
        if errors:
            results.append(ProbeResult("health", False, "; ".join(errors)))
        else:
            results.append(
                ProbeResult(
                    "health",
                    True,
                    f"{health.get('camera_id')} {health.get('firmware_version')} "
                    f"RSSI={health.get('wifi', {}).get('rssi_dbm')} dBm",
                )
            )
    except Exception as exc:
        health = {}
        results.append(ProbeResult("health", False, str(exc)))

    try:
        frame_bytes, content_type = read_first_jpeg(args.stream_url, args.stream_timeout)
        evidence["first_jpeg_bytes"] = frame_bytes
        results.append(
            ProbeResult(
                "MJPEG",
                True,
                f"first complete JPEG={frame_bytes} bytes; {content_type}",
            )
        )
    except Exception as exc:
        results.append(ProbeResult("MJPEG", False, str(exc)))

    if args.exercise_flash:
        forced_off_ok = False
        try:
            post_flash(args.flash_url, True, args.timeout)
            time.sleep(args.flash_hold_seconds)
            after_on = http_json(args.health_url, args.timeout)
            if after_on.get("flash_on") is not True:
                raise ProbeError("health did not report flash_on=true after ON")
            results.append(ProbeResult("flash ON", True, "camera reported flash_on=true"))
        except Exception as exc:
            results.append(ProbeResult("flash ON", False, str(exc)))
        finally:
            try:
                post_flash(args.flash_url, False, args.timeout)
                after_off = http_json(args.health_url, args.timeout)
                forced_off_ok = after_off.get("flash_on") is False
                if not forced_off_ok:
                    raise ProbeError("health did not report flash_on=false after OFF")
                results.append(
                    ProbeResult("flash OFF", True, "camera returned to safe OFF state")
                )
            except Exception as exc:
                results.append(
                    ProbeResult(
                        "flash OFF",
                        False,
                        "ATTENTION: failed to verify safe OFF state: " + str(exc),
                    )
                )
        evidence["flash_forced_off_verified"] = forced_off_ok

    return results, evidence


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Attended StageCore ESP32-CAM C3 physical smoke probe"
    )
    parser.add_argument(
        "--camera-host",
        help="Optional stable mDNS host, e.g. stagecam-d44a4c.local",
    )
    parser.add_argument(
        "--health-url",
        required=True,
        help="Camera health URL, e.g. http://stagecam-xxxxxx.local/api/v0/health",
    )
    parser.add_argument(
        "--stream-url",
        required=True,
        help="Camera stream URL, e.g. http://stagecam-xxxxxx.local:81/api/v0/stream",
    )
    parser.add_argument(
        "--flash-url",
        help="Camera flash URL; required with --exercise-flash",
    )
    parser.add_argument(
        "--exercise-flash",
        action="store_true",
        help="Physically toggle flash ON then force it OFF; default is read-only",
    )
    parser.add_argument(
        "--flash-hold-seconds",
        type=float,
        default=0.5,
        help="How long to leave flash ON during attended test (default: 0.5)",
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--stream-timeout", type=float, default=10.0)
    parser.add_argument(
        "--json-report",
        help="Optional path to write a machine-readable qualification report",
    )
    args = parser.parse_args()
    if args.exercise_flash and not args.flash_url:
        parser.error("--flash-url is required with --exercise-flash")
    return args


def main() -> int:
    args = parse_args()
    results, evidence = run(args)

    for result in results:
        state = "PASS" if result.passed else "FAIL"
        print(f"{state:4}  {result.name}: {result.detail}")

    report = {
        "schema_version": 1,
        "tool": "stagecore-camera-c3-physical-smoke",
        "passed": all(item.passed for item in results),
        "results": [item.as_dict() for item in results],
        "evidence": evidence,
    }

    if args.json_report:
        with open(args.json_report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
            handle.write("\n")
        print(f"REPORT {args.json_report}")

    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
