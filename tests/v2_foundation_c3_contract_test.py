from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class V2FoundationC3ScaffoldContract(unittest.TestCase):
    def test_c3_is_a_separate_candidate_inheriting_c2(self):
        pio = (ROOT / "platformio.ini").read_text()
        self.assertIn("[env:esp32cam_v2_foundation_c3]", pio)
        c3 = pio.split("[env:esp32cam_v2_foundation_c3]", 1)[1]
        self.assertIn("framework = espidf", c3)
        self.assertIn("sdkconfig.v2_camera.defaults", c3)
        self.assertIn("STAGECORE_CAMERA_V2_FOUNDATION_C2=1", c3)
        self.assertIn("STAGECORE_CAMERA_V2_FOUNDATION_C3=1", c3)
        self.assertIn('STAGECORE_FW_VERSION=\\\"0.2.0-dev.c3\\\"', c3)

    def test_camera_component_is_kconfig_gated_to_c3(self):
        manifest = (ROOT / "src" / "idf_component.yml").read_text()
        kconfig = (ROOT / "src" / "Kconfig.projbuild").read_text()
        defaults = (ROOT / "sdkconfig.v2_camera.defaults").read_text()

        self.assertIn("espressif/esp32-camera:", manifest)
        self.assertIn('version: "2.1.8"', manifest)
        self.assertIn(
            'if: "$CONFIG{STAGECORE_CAMERA_C3} == True"',
            manifest,
        )
        self.assertIn("config STAGECORE_CAMERA_C3", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn("CONFIG_STAGECORE_CAMERA_C3=y", defaults)

    def test_scaffold_still_has_no_camera_or_flash_execution_path(self):
        v2 = ROOT / "src" / "v2_foundation"
        source = "\n".join(
            path.read_text()
            for path in v2.glob("*.cpp")
            if path.is_file()
        )
        self.assertNotIn("esp_camera_init", source)
        self.assertNotIn("esp_camera_fb_get", source)
        self.assertNotIn("/api/v0/stream", source)
        self.assertNotIn("/api/v0/flash", source)


if __name__ == "__main__":
    unittest.main()
