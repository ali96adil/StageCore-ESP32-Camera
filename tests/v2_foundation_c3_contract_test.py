from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "src" / "v2_foundation"


class V2FoundationC3Contract(unittest.TestCase):
    def test_c3_is_separate_and_inherits_authenticated_c2(self):
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
        cmake = (ROOT / "src" / "CMakeLists.txt").read_text()

        self.assertIn("espressif/esp32-camera:", manifest)
        self.assertIn('version: "2.1.8"', manifest)
        self.assertIn(
            'if: "$CONFIG{STAGECORE_CAMERA_C3} == True"',
            manifest,
        )
        self.assertIn("config STAGECORE_CAMERA_C3", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn("CONFIG_STAGECORE_CAMERA_C3=y", defaults)
        self.assertIn("CONFIG_SPIRAM=y", defaults)
        self.assertIn("STAGECORE_CAMERA_V2_FOUNDATION_C3", cmake)
        self.assertIn('"v2_foundation/camera_service.cpp"', cmake)

    def test_proven_ai_thinker_camera_and_relay_contract_is_preserved(self):
        camera = (V2 / "camera_service.cpp").read_text()

        for pin in (
            "config.pin_d0 = 5",
            "config.pin_d1 = 18",
            "config.pin_d2 = 19",
            "config.pin_d3 = 21",
            "config.pin_d4 = 36",
            "config.pin_d5 = 39",
            "config.pin_d6 = 34",
            "config.pin_d7 = 35",
            "config.pin_xclk = 0",
            "config.pin_pclk = 22",
            "config.pin_vsync = 25",
            "config.pin_href = 23",
            "config.pin_sccb_sda = 26",
            "config.pin_sccb_scl = 27",
            "config.pin_pwdn = 32",
        ):
            self.assertIn(pin, camera)

        self.assertIn("PIXFORMAT_JPEG", camera)
        self.assertIn("FRAMESIZE_VGA", camera)
        self.assertIn("jpeg_quality = 12", camera)
        self.assertIn("fb_count = g_psram_available ? 2 : 1", camera)
        self.assertIn("CAMERA_GRAB_LATEST", camera)
        self.assertIn("kFrameIntervalMs = 80", camera)
        self.assertIn('"/api/v0/health"', camera)
        self.assertIn('"/api/v0/stream"', camera)
        self.assertIn('"/api/v0/flash"', camera)
        self.assertIn("kControlPort = 80", camera)
        self.assertIn("kStreamPort = 81", camera)
        self.assertIn("g_stream_active.exchange(true)", camera)
        self.assertIn("_stagecore-camera", camera)
        self.assertIn('{"stream-port", "81"}', camera)
        self.assertIn('"stagecam-%02x%02x%02x"', camera)

    def test_flash_is_hardware_safe_and_not_maintenance_authority(self):
        camera = (V2 / "camera_service.cpp").read_text()
        runtime = (V2 / "stage_device_runtime.cpp").read_text()

        self.assertIn("kFlashPin = GPIO_NUM_4", camera)
        self.assertIn("set_flash(false);", camera)
        self.assertIn("WIFI_EVENT_STA_DISCONNECTED", camera)
        self.assertIn(
            "loss of Stage LAN can never leave flash ON",
            camera,
        )
        self.assertNotIn("camera_service", runtime)
        self.assertNotIn("esp_camera_", runtime)
        self.assertNotIn("/api/v0/flash", runtime)

    def test_camera_http_surface_cannot_mutate_setup_credentials(self):
        camera = (V2 / "camera_service.cpp").read_text()
        for forbidden in (
            "save_setup_ap_password",
            "clear_setup_ap_password",
            "save_wifi_config",
            "setup_ap_pass",
            '"/setup"',
            '"/save"',
        ):
            self.assertNotIn(forbidden, camera)


if __name__ == "__main__":
    unittest.main()
