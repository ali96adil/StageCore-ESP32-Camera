from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "src" / "v2_foundation"


class V2FoundationC2Contract(unittest.TestCase):
    def test_c2_is_a_separate_source_only_candidate(self):
        pio = (ROOT / "platformio.ini").read_text()
        self.assertIn("[env:esp32cam_v2_foundation_c2]", pio)
        c2 = pio.split("[env:esp32cam_v2_foundation_c2]", 1)[1]
        self.assertIn("framework = espidf", c2)
        self.assertIn("STAGECORE_CAMERA_V2_FOUNDATION_C2=1", c2)
        self.assertIn('STAGECORE_FW_VERSION=\\\"0.2.0-dev.c2\\\"', c2)

        cmake = (ROOT / "src" / "CMakeLists.txt").read_text()
        self.assertIn(
            "STAGECORE_CAMERA_V2_FOUNDATION_C1 OR "
            "STAGECORE_CAMERA_V2_FOUNDATION_C2",
            cmake,
        )

    def test_setup_ap_override_is_persistent_and_never_read_back_on_wire(self):
        header = (V2 / "config_store.h").read_text()
        store = (V2 / "config_store.cpp").read_text()
        runtime = (V2 / "stage_device_runtime.cpp").read_text()

        self.assertIn('kSetupAPPasswordKey[] = "setup_ap_pass"', store)
        self.assertIn("load_setup_ap_password", header)
        self.assertIn("save_setup_ap_password", header)
        self.assertIn("clear_setup_ap_password", header)
        self.assertIn("nvs_commit(handle)", store)

        self.assertIn("device.maintenance.setup-ap-password", runtime)
        self.assertIn("maintenance.setup_ap_password", runtime)
        self.assertIn("maintenance.setup_ap_password.result", runtime)
        self.assertIn('"connection_generation"', runtime)
        self.assertIn('"RESET_DEFAULT"', runtime)
        self.assertIn("save_setup_ap_password", runtime)
        self.assertIn("clear_setup_ap_password", runtime)
        self.assertNotIn('cJSON_AddStringToObject(root, "password"', runtime)

    def test_maintenance_remains_non_camera_non_flash_authority(self):
        runtime = (V2 / "stage_device_runtime.cpp").read_text()
        start = runtime.index("esp_err_t process_setup_ap_maintenance")
        end = runtime.index("#endif\n\n}  // namespace", start)
        maintenance = runtime[start:end]
        for forbidden in (
            "esp_camera_",
            "kFlashPin",
            "digitalWrite",
            "/api/v0/stream",
            "/api/v0/flash",
        ):
            self.assertNotIn(forbidden, maintenance)

        security = (V2 / "hub_security.cpp").read_text()
        self.assertIn("device.maintenance.setup-ap-password", security)
        self.assertIn("#if STAGECORE_CAMERA_V2_FOUNDATION_C2", security)


if __name__ == "__main__":
    unittest.main()
