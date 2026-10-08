from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "src" / "v2_foundation"
FOUNDATION_SHA = "d6946da3f003e8c0c2a72216804ef2035bf1288c"


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
        self.assertIn("stagecore_foundation", cmake)
        self.assertNotIn('"main.cpp"\n', cmake.replace('"v2_foundation/main.cpp"', ""))

    def test_candidate_has_no_camera_or_flash_execution_path(self):
        c1_files = (
            "main.cpp",
            "config_store.cpp",
            "network_station.cpp",
            "stage_device_runtime.cpp",
        )
        source = "\n".join((V2 / name).read_text() for name in c1_files)
        self.assertNotIn("esp_camera_init", source)
        self.assertNotIn("esp_camera_fb_get", source)
        self.assertNotIn("kFlashPin", source)
        self.assertNotIn("digitalWrite", source)
        self.assertNotIn("/api/v0/stream", source)
        self.assertNotIn("/api/v0/flash", source)

    def test_identity_and_hub_trust_are_owned_by_pinned_foundation(self):
        main = (V2 / "main.cpp").read_text()
        config = (V2 / "config_store.cpp").read_text()
        header = (V2 / "config_store.h").read_text()
        manifest = (ROOT / "src" / "idf_component.yml").read_text()

        self.assertIn("stagecore::DeviceIdentity identity", main)
        self.assertIn("identity.LoadOrCreate()", main)
        self.assertIn('#include "foundation_store.h"', header)
        self.assertIn('FoundationStore store(kV2Namespace)', config)
        self.assertIn("foundation_store().LoadHubBinding", config)
        self.assertIn("foundation_store().SaveHubBinding", config)
        self.assertIn("stagecore_foundation:", manifest)
        self.assertIn(f"version: {FOUNDATION_SHA}", manifest)
        for name in (
            "device_identity.cpp", "device_identity.h",
            "trusted_clock.cpp", "trusted_clock.h",
            "hub_discovery.cpp", "hub_discovery.h",
            "hub_security.cpp", "hub_security.h",
        ):
            self.assertFalse((V2 / name).exists(), name)

    def test_pairing_and_runtime_are_authenticated_v2_inventory_only(self):
        main = (V2 / "main.cpp").read_text()
        runtime = (V2 / "stage_device_runtime.cpp").read_text()
        self.assertIn("ensure_paired_and_authenticate", main)
        self.assertIn('descriptor.hostname_prefix = "stagecore-camera-"', main)
        self.assertIn('descriptor.architecture = "xtensa"', main)
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
        main = (V2 / "main.cpp").read_text()
        self.assertIn("#if STAGECORE_CAMERA_V2_FOUNDATION_C2", runtime)
        self.assertIn("#if STAGECORE_CAMERA_V2_FOUNDATION_C2", main)
        descriptor = main.split(
            "stagecore::FoundationDeviceDescriptor foundation_descriptor()", 1
        )[1].split("return descriptor;", 1)[0]
        self.assertIn("kSetupAPPasswordCapability", descriptor)


if __name__ == "__main__":
    unittest.main()
