from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProvisioningContract(unittest.TestCase):
    def test_setup_and_recovery_use_shared_password(self):
        source = (ROOT / "src" / "stream_firmware.cpp").read_text()
        self.assertIn('#define STAGECORE_SETUP_AP_PASSWORD "StageCoreSetup"', source)
        self.assertIn("kSetupApPassword", source)
        self.assertIn("WiFi.softAP(apSsid.c_str(), kSetupApPassword)", source)
        self.assertNotIn("randomPassword", source)
        self.assertNotIn("esp_random()", source)


if __name__ == "__main__":
    unittest.main()
