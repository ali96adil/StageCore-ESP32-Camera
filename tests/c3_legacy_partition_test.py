"""Protect StageCore ESP32-CAM C3 legacy on-device migration layout.

Values are from the owner's 4MB full-flash dump partition table at 0x8000.
Only validates on-disk layout/build outputs; it does not validate a real device.
"""
from __future__ import annotations

import csv
import pathlib
import struct
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGET = "esp32cam_v2_foundation_c3"
CSV = ROOT / "partitions_camera_legacy.csv"
BIN = ROOT / ".pio" / "build" / TARGET / "partitions.bin"
EXPECTED = {
    "nvs": (0x9000, 0x5000, 0x01, 0x02),
    "otadata": (0xE000, 0x2000, 0x01, 0x00),
    "app0": (0x10000, 0x300000, 0x00, 0x10),
    "spiffs": (0x310000, 0xE0000, 0x01, 0x82),
    "coredump": (0x3F0000, 0x10000, 0x01, 0x03),
}
SUBTYPES = {"nvs": (1, 2), "ota": (1, 0), "ota_0": (0, 0x10),
            "spiffs": (1, 0x82), "coredump": (1, 3)}


def parse_csv():
    rows = {}
    with CSV.open(newline="") as handle:
        for row in csv.reader(line for line in handle if not line.lstrip().startswith("#")):
            if not row or not row[0].strip():
                continue
            name, kind, subtype, offset, size = (cell.strip() for cell in row[:5])
            t, s = SUBTYPES[subtype]
            assert kind == ("app" if t == 0 else "data"), (name, kind, subtype)
            rows[name] = (int(offset, 0), int(size, 0), t, s)
    return rows


def parse_binary(data):
    result = {}
    for i in range(0, min(len(data), 3072), 32):
        entry = data[i:i+32]
        if entry[:2] != b"\xaa\x50":
            break
        typ, subtype, offset, size = struct.unpack_from("<BBII", entry, 2)
        name = entry[12:28].split(b"\x00", 1)[0].decode("ascii")
        result[name] = (offset, size, typ, subtype)
    return result


class LegacyPartitionTest(unittest.TestCase):
    def test_csv_matches_physical_legacy_flash(self):
        self.assertEqual(parse_csv(), EXPECTED)
        segments = sorted((offset, offset+size, name)
                          for name, (offset, size, _, _) in parse_csv().items())
        for (_, end, _), (start, _, _) in zip(segments, segments[1:]):
            self.assertLessEqual(end, start)
        self.assertLessEqual(segments[-1][1], 0x400000)

    def test_c3_build_configuration_selects_legacy(self):
        config = (ROOT / "platformio.ini").read_text()
        target = config.split(f"[env:{TARGET}]", 1)[1].split("\n[env:", 1)[0]
        self.assertIn("board_build.partitions = partitions_camera_legacy.csv", target)
        defaults = (ROOT / "sdkconfig.v2_camera.defaults").read_text()
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", defaults)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_camera_legacy.csv"', defaults)

    def test_built_partition_table(self):
        if not BIN.exists():
            self.skipTest("Build C3 first to validate the generated partition image")
        self.assertEqual(parse_binary(BIN.read_bytes()), EXPECTED)


if __name__ == "__main__":
    unittest.main()
