from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FoundationV2CandidateContract(unittest.TestCase):
    def test_candidate_is_separate_from_proven_stream_image(self):
        ini = (ROOT / "platformio.ini").read_text()
        self.assertIn("[env:esp32cam_stream]", ini)
        self.assertIn("[env:esp32cam_foundation_v2_candidate]", ini)
        self.assertIn("framework = espidf", ini)
        self.assertIn("+<foundation_candidate.cpp>", ini)

        stream = (ROOT / "src" / "stream_firmware.cpp").read_text()
        self.assertNotIn("STAGECORE_FOUNDATION_V2_CANDIDATE", stream)

    def test_candidate_is_authenticated_inventory_only(self):
        runtime = (ROOT / "src" / "foundation_runtime.cpp").read_text()
        self.assertIn('constexpr char kProtocolVersion[] = "stagecore.device/2"', runtime)
        self.assertIn('constexpr char kProfileID[] = "stagecore.esp32-camera"', runtime)
        self.assertIn("/api/v1/stage-devices/runtime", runtime)
        self.assertIn("Authorization: StageCoreSession ", runtime)
        self.assertIn('"UNASSIGNED"', runtime)
        self.assertIn('"BLOCKER"', runtime)
        self.assertIn("camera.foundation.inventory", runtime)
        self.assertNotIn("command.execute", runtime)

    def test_candidate_has_persistent_identity_and_pinned_hub(self):
        identity = (ROOT / "src" / "foundation_identity.cpp").read_text()
        hub = (ROOT / "src" / "foundation_hub.cpp").read_text()
        self.assertIn('constexpr char kNamespace[] = "stagecam_id"', identity)
        self.assertIn("MBEDTLS_ECP_DP_SECP256R1", identity)
        self.assertIn("P256_X963_SHA256", hub)
        self.assertIn("tls_sha256", hub)
        self.assertIn("capture_pinned_certificate", hub)
        self.assertIn("COMPANION_UNPAIRED", hub)

    def test_c1_has_no_camera_flash_or_password_mutation_authority(self):
        sources = "\n".join(
            path.read_text()
            for path in (ROOT / "src").glob("foundation_*")
            if path.suffix in {".cpp", ".h"}
        )
        candidate = (ROOT / "src" / "foundation_candidate.cpp").read_text()
        self.assertIn("gpio_set_level(kFlashGPIO, 0)", candidate)
        self.assertNotIn("gpio_set_level(kFlashGPIO, 1)", candidate)
        self.assertNotIn("esp_camera_init", sources)
        self.assertNotIn("maintenance.setup_ap_password", sources)
        self.assertNotIn("device.maintenance.setup-ap-password", sources)
        self.assertNotIn("/setup-ap-password", sources)


if __name__ == "__main__":
    unittest.main()
