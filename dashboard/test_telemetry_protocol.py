import unittest

from telemetry_protocol import parse_packet


class ProtocolTest(unittest.TestCase):
    CMP = "CMP 1327 1 1 1 1 1 0 1 603 512 8 140 212 -152 400 740 -1 1 22000 31"

    def test_compass_has_independent_heading_and_units(self):
        packet = parse_packet(self.CMP)
        self.assertEqual(packet["kind"], "CMP")
        self.assertEqual(packet["heading_x10"], 1327)
        self.assertEqual(packet["reference_x10"], 740)
        self.assertEqual(packet["raw_y"], -152)
        self.assertEqual(packet["direction_verified"], 1)
        self.assertEqual(packet["fit_error_ppm"], 22000)
        self.assertEqual(packet["tilt_deg10"], 31)
        self.assertEqual(packet["selftest_result"], 0)
        self.assertEqual(packet["selftest_status"], 0)
        self.assertNotIn("yaw", packet)

    def test_compass_legacy_frames_never_invent_direction_verification(self):
        packet = parse_packet(" ".join(self.CMP.split()[:18]))
        self.assertEqual(packet["direction_verified"], 0)
        self.assertEqual(packet["fit_error_ppm"], -1)
        self.assertEqual(packet["tilt_deg10"], -1)
        self.assertEqual(packet["selftest_result"], 0)
        self.assertEqual(packet["selftest_status"], 0)

    def test_compass_r8_selftest_fields_and_bounds(self):
        for result in range(6):
            packet = parse_packet(self.CMP + f" {result} 255")
            self.assertEqual(packet["selftest_result"], result)
            self.assertEqual(packet["selftest_status"], 255)
            self.assertEqual(packet["heading_x10"], 1327)
        for suffix in (" -1 0", " 6 0", " 1 -1", " 1 256", " 1 0 0", " 1 NaN"):
            self.assertIsNone(parse_packet(self.CMP + suffix))

    def test_compass_rejects_bad_gates_ranges_and_truncated_frames(self):
        parts = self.CMP.split()
        for field, value in ((1, "3600"), (2, "2"), (3, "0"), (4, "0"),
                             (5, "0"), (6, "0"), (7, "1"), (8, "0"),
                             (9, "-1"), (11, "9"), (16, "-1"), (17, "0"),
                             (18, "2"), (19, "-2"), (20, "1801")):
            invalid = parts.copy()
            invalid[field] = value
            self.assertIsNone(parse_packet(" ".join(invalid)), (field, value))
        for line in (" ".join(parts[:-1]), self.CMP + " 1", self.CMP.replace("1327", "NaN")):
            self.assertIsNone(parse_packet(line))

    def test_scaling_and_relative_heading(self):
        packet = parse_packet("TEL -5 123 359 1 -2 3 261 500 1012 461 2800 0 1 0")
        self.assertEqual(packet["roll"], -0.5)
        self.assertEqual(packet["pitch"], 12.3)
        self.assertEqual(packet["heading_mode"], "REL")
        self.assertEqual(packet["temp"], 26.1)

    def test_invalid_measurements_are_not_replaced_by_old_values(self):
        packet = parse_packet("TEL 0 0 0 0 0 0 -9999 -1 -1 -1 -1 0 0 0")
        self.assertIsNone(packet["temp"])
        self.assertIsNone(packet["lux"])
        self.assertEqual(packet["imu"], 0)
        self.assertEqual(packet["range_mm"], -1)

    def test_incomplete_or_bad_frames_are_rejected(self):
        for line in ("Boot ready", "TEL 1 2 3", "TEL x 0 0 0 0 0 0 0 0 0 0 0 1 0",
                     "TEL 0 0 0 0 0 0 0 0 0 0 0 0 2 0"):
            self.assertIsNone(parse_packet(line))

    def test_collision_levels_match_firmware_enum(self):
        for level, expected in enumerate(("NO DATA", "SAFE", "WARNING", "DANGER")):
            packet = parse_packet(f"TEL 0 0 0 0 0 0 260 500 1013 123 500 {level} 1 0")
            self.assertEqual(packet["range_state"], expected)


if __name__ == "__main__":
    unittest.main()

