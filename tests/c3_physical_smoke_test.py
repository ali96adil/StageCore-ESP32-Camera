import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "c3_physical_smoke.py"

spec = importlib.util.spec_from_file_location("c3_physical_smoke", MODULE_PATH)
smoke = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(smoke)


class C3PhysicalSmokeUnitTest(unittest.TestCase):
    def healthy_payload(self):
        return {
            "schema_version": 0,
            "camera_id": "stagecam-d44a4c",
            "firmware_version": "0.2.0-dev.c3",
            "state": "ready",
            "stream": {
                "path": "/api/v0/stream",
                "port": 81,
                "format": "mjpeg",
                "width": 640,
                "height": 480,
                "target_fps": 12,
            },
            "wifi": {"rssi_dbm": -67},
            "uptime_s": 42,
            "stream_active": False,
            "flash_on": False,
        }

    def test_healthy_c3_payload_passes(self):
        self.assertEqual(smoke.validate_health(self.healthy_payload()), [])

    def test_flash_must_start_safe_off(self):
        payload = self.healthy_payload()
        payload["flash_on"] = True
        errors = smoke.validate_health(payload)
        self.assertTrue(any("flash_on" in item for item in errors))

    def test_rejects_wrong_stream_contract(self):
        payload = self.healthy_payload()
        payload["stream"]["port"] = 80
        payload["stream"]["path"] = "/stream"
        errors = smoke.validate_health(payload)
        self.assertTrue(any("stream.port" in item for item in errors))
        self.assertTrue(any("stream.path" in item for item in errors))

    def test_rejects_non_c3_firmware(self):
        payload = self.healthy_payload()
        payload["firmware_version"] = "0.1.0-dev.1"
        errors = smoke.validate_health(payload)
        self.assertTrue(any("expected C3" in item for item in errors))


if __name__ == "__main__":
    unittest.main()
