from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "src" / "v2_foundation"


class V2FoundationC1Contract(unittest.TestCase):
    def test_candidate_is_isolated_from_proven_arduino_images(self):
        pio = (ROOT / "platformio.ini").read_text()
        self.assertIn("[env:esp32cam_v2_foundation_c1]", pio)
        self.assertIn("framework = espidf", pio)
        self.assertIn("-<v2_foundation/>", pio)
        self.assertIn("-DSTAGECORE_CAMERA_V2_FOUNDATION_C1=ON", pio)
        self.assertIn(
            "board_build.partitions = partitions_singleapp_large.csv",
            pio,
        )
        root_cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn(
            "include($ENV{IDF_PATH}/tools/cmake/project.cmake)",
            root_cmake,
        )
        self.assertIn("project(stagecore_esp32_camera)", root_cmake)
        cmake = (ROOT / "src" / "CMakeLists.txt").read_text()
        self.assertIn("STAGECORE_CAMERA_V2_FOUNDATION_C1", cmake)
        self.assertIn('"v2_foundation/main.cpp"', cmake)
        self.assertNotIn('"main.cpp"\n', cmake.replace('"v2_foundation/main.cpp"', ""))

    def test_candidate_has_no_camera_or_flash_execution_path(self):
        source = "\n".join(
            path.read_text()
            for path in V2.glob("*.cpp")
        )
        self.assertNotIn("esp_camera_init", source)
        self.assertNotIn("esp_camera_fb_get", source)
        self.assertNotIn("kFlashPin", source)
        self.assertNotIn("digitalWrite", source)
        self.assertNotIn("/api/v0/stream", source)
        self.assertNotIn("/api/v0/flash", source)

    def test_identity_and_hub_trust_are_persistent(self):
        identity = (V2 / "device_identity.cpp").read_text()
        config = (V2 / "config_store.cpp").read_text()
        discovery = (V2 / "hub_discovery.cpp").read_text()
        self.assertIn('kNamespace[] = "stagecore_id"', identity)
        self.assertIn("MBEDTLS_ECP_DP_SECP256R1", identity)
        self.assertIn('kV2Namespace[] = "stagecore_camv2"', config)
        self.assertIn("save_hub_binding", discovery)
        self.assertIn("TLS certificate SHA-256 pin verified", discovery)

    def test_pairing_and_runtime_are_authenticated_v2_inventory_only(self):
        security = (V2 / "hub_security.cpp").read_text()
        runtime = (V2 / "stage_device_runtime.cpp").read_text()
        self.assertIn('"P256_X963_SHA256"', security)
        self.assertIn("/api/v1/companion/auth/sessions", security)
        self.assertIn('"stagecore.device/2"', runtime)
        self.assertIn('"stagecore.esp32-camera"', runtime)
        self.assertIn('"readiness", "BLOCKER"', runtime)
        self.assertIn('"UNASSIGNED"', runtime)
        self.assertIn("Authorization: StageCoreSession ", runtime)
        self.assertIn("certificate_pinned", runtime)

    def test_c1_does_not_enable_foundation_maintenance(self):
        pio = (ROOT / "platformio.ini").read_text()
        c1 = pio.split("[env:esp32cam_v2_foundation_c1]", 1)[1]
        c1 = c1.split("[env:esp32cam_v2_foundation_c2]", 1)[0]
        self.assertNotIn("STAGECORE_CAMERA_V2_FOUNDATION_C2", c1)

        runtime = (V2 / "stage_device_runtime.cpp").read_text()
        security = (V2 / "hub_security.cpp").read_text()
        self.assertIn("#if STAGECORE_CAMERA_V2_FOUNDATION_C2", runtime)
        self.assertIn("#if STAGECORE_CAMERA_V2_FOUNDATION_C2", security)


if __name__ == "__main__":
    unittest.main()
