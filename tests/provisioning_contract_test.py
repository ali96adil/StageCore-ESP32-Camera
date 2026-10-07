from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProvisioningContract(unittest.TestCase):
    def test_setup_and_recovery_use_shared_password(self):
        source = (ROOT / "src" / "stream_firmware.cpp").read_text()
        self.assertIn('#define STAGECORE_SETUP_AP_PASSWORD "12345678"', source)
        self.assertIn("kSetupApPassword", source)
        self.assertIn("WiFi.softAP(apSsid.c_str(), kSetupApPassword)", source)
        self.assertNotIn("randomPassword", source)
        self.assertNotIn("esp_random()", source)

    def test_setup_ap_credential_mutation_requires_authenticated_v2_transport(self):
        source = (ROOT / "src" / "stream_firmware.cpp").read_text()
        # The current Camera control server is intentionally unauthenticated.
        # Do not bolt Foundation credential mutation onto it. This test must be
        # replaced only when Camera migrates to authenticated stagecore.device/2.
        self.assertNotIn("/setup-ap-password", source)
        self.assertNotIn("maintenance.setup_ap_password", source)
        self.assertNotIn("device.maintenance.setup-ap-password", source)


if __name__ == "__main__":
    unittest.main()
