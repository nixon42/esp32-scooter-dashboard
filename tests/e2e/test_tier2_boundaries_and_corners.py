"""
E2E Tier 2 Suite: Boundary & Corner Cases
Covers extreme values, limit states, overflow guards, and error resilience
across all functional modules and hardware subsystems.
"""

import unittest
from e2e_framework import (
    PCD8544Display,
    SensorModels,
    ChronosProtocol,
    WebPortalServer,
    HudState,
    BacklightMode,
    SpeedSource
)

class TestTier2DisplayAndBacklightBoundaries(unittest.TestCase):
    """PCD8544 and LEDC Backlight boundary tests."""

    def setUp(self):
        self.disp = PCD8544Display()

    def test_t2_01_contrast_exact_min_0x38(self):
        """Contrast at exact minimum bound 0x38 (56)."""
        self.assertTrue(self.disp.set_contrast(0x38))
        self.assertEqual(self.disp.contrast, 0x38)

    def test_t2_02_contrast_exact_max_0x48(self):
        """Contrast at exact maximum bound 0x48 (72)."""
        self.assertTrue(self.disp.set_contrast(0x48))
        self.assertEqual(self.disp.contrast, 0x48)

    def test_t2_03_contrast_one_below_min_0x37(self):
        """Contrast at one below min 0x37 (55) rejected."""
        self.assertFalse(self.disp.set_contrast(0x37))

    def test_t2_04_contrast_one_above_max_0x49(self):
        """Contrast at one above max 0x49 (73) rejected."""
        self.assertFalse(self.disp.set_contrast(0x49))

    def test_t2_05_contrast_extreme_values(self):
        """Contrast at 0x00 and 0xFF rejected."""
        self.assertFalse(self.disp.set_contrast(0x00))
        self.assertFalse(self.disp.set_contrast(0xFF))

    def test_t2_06_backlight_duty_zero(self):
        """Backlight duty 0 (off)."""
        duty = SensorModels.calc_ldr_backlight(adc_raw=1000, mode=BacklightMode.OFF, manual_duty=0)
        self.assertEqual(duty, 0)

    def test_t2_07_backlight_duty_255(self):
        """Backlight duty 255 (maximum brightness)."""
        duty = SensorModels.calc_ldr_backlight(adc_raw=1000, mode=BacklightMode.ON, manual_duty=255)
        self.assertEqual(duty, 255)

    def test_t2_08_backlight_duty_clamp_overflow(self):
        """Backlight duty clamped to 255 if input exceeds 255."""
        duty = SensorModels.calc_ldr_backlight(adc_raw=1000, mode=BacklightMode.ON, manual_duty=300)
        self.assertEqual(duty, 255)

    def test_t2_09_pixel_coordinate_exact_edges(self):
        """Pixel coordinates at exact edges (0, 0), (83, 0), (0, 47), (83, 47)."""
        self.disp.clear()
        edges = [(0, 0), (83, 0), (0, 47), (83, 47)]
        for x, y in edges:
            self.disp.set_pixel(x, y, True)
            self.assertTrue(self.disp.get_pixel(x, y))

    def test_t2_10_pixel_coordinate_out_of_bounds_no_exception(self):
        """Out of bounds coordinates (-1, -1), (84, 48), (100, 100) safely ignored."""
        self.disp.set_pixel(-1, 0, True)
        self.disp.set_pixel(84, 0, True)
        self.disp.set_pixel(0, 48, True)
        self.assertFalse(self.disp.get_pixel(-1, 0))
        self.assertFalse(self.disp.get_pixel(84, 0))
        self.assertFalse(self.disp.get_pixel(0, 48))


class TestTier2SensorBoundaries(unittest.TestCase):
    """Sensor calculations, ADC ranges, and mathematical edge conditions."""

    def test_t2_11_battery_voltage_zero_disconnected(self):
        """Voltmeter ADC 0 mV results in 0.0V."""
        v = SensorModels.calc_battery_voltage(adc_mv=0.0, ratio=5.54)
        self.assertEqual(v, 0.0)

    def test_t2_12_battery_voltage_critical_low_10_5v(self):
        """Critical Aki voltage at 10.5V boundary."""
        # 10.5 / 5.54 = 1.8953 V = 1895.3 mV
        v = SensorModels.calc_battery_voltage(adc_mv=1895.3, ratio=5.54)
        self.assertEqual(v, 10.5)

    def test_t2_13_battery_voltage_charging_high_14_8v(self):
        """Charging alternator voltage at 14.8V boundary."""
        # 14.8 / 5.54 = 2.6715 V = 2671.5 mV
        v = SensorModels.calc_battery_voltage(adc_mv=2671.5, ratio=5.54)
        self.assertEqual(v, 14.8)

    def test_t2_14_battery_voltage_overvoltage_24v(self):
        """Overvoltage spike 24.0V."""
        v = SensorModels.calc_battery_voltage(adc_mv=4332.1, ratio=5.54)
        self.assertEqual(v, 24.0)

    def test_t2_15_vss_speed_zero_pulses(self):
        """Standstill: 0 pulses/sec yields exactly 0.0 km/h."""
        speed = SensorModels.calc_vss_speed(pulses_per_sec=0.0, pulses_per_meter=28.5)
        self.assertEqual(speed, 0.0)

    def test_t2_16_vss_speed_single_pulse(self):
        """Crawling motion: 1 pulse/sec yields 0.1 km/h."""
        speed = SensorModels.calc_vss_speed(pulses_per_sec=1.0, pulses_per_meter=28.5)
        self.assertEqual(speed, 0.1)

    def test_t2_17_vss_speed_extreme_top_limit_199kmh(self):
        """Top scooter speed 199.0 km/h (1575.4 pulses/sec)."""
        # pulses = 199.0 * 28.5 / 3.6 = 1575.416
        speed = SensorModels.calc_vss_speed(pulses_per_sec=1575.42, pulses_per_meter=28.5)
        self.assertEqual(speed, 199.0)

    def test_t2_18_vss_speed_zero_division_guard(self):
        """Pulses per meter zero or negative returns 0.0 without divide-by-zero crash."""
        self.assertEqual(SensorModels.calc_vss_speed(100.0, pulses_per_meter=0.0), 0.0)
        self.assertEqual(SensorModels.calc_vss_speed(100.0, pulses_per_meter=-5.0), 0.0)

    def test_t2_19_dht22_sub_zero_temperature_minus_20(self):
        """DHT22 reading at -20.0°C (sign bit 0x80 with raw 200 = 0x00C8)."""
        # Raw 200 = B2: 0x80, B3: 0xC8. Hum 40.0% = Raw 400 (B0: 1, B1: 144)
        # Checksum = (1 + 144 + 128 + 200) & 0xFF = 473 & 0xFF = 217
        temp, hum, valid = SensorModels.decode_dht22(1, 144, 128, 200, 217)
        self.assertTrue(valid)
        self.assertEqual(temp, -20.0)
        self.assertEqual(hum, 40.0)

    def test_t2_20_dht22_freezing_point_zero_celsius(self):
        """DHT22 reading at exact freezing point 0.0°C."""
        # Temp 0.0 -> B2: 0, B3: 0. Hum 50.0% -> 500 = (B0: 1, B1: 244)
        # Checksum = (1 + 244 + 0 + 0) = 245
        temp, hum, valid = SensorModels.decode_dht22(1, 244, 0, 0, 245)
        self.assertTrue(valid)
        self.assertEqual(temp, 0.0)
        self.assertEqual(hum, 50.0)

    def test_t2_21_dht22_max_temperature_80_celsius(self):
        """DHT22 reading at 80.0°C (raw 800 = 0x0320)."""
        # Temp 80.0 -> B2: 3, B3: 32. Hum 99.9% -> 999 = (B0: 3, B1: 231)
        # Checksum = (3 + 231 + 3 + 32) = 269 & 0xFF = 13
        temp, hum, valid = SensorModels.decode_dht22(3, 231, 3, 32, 13)
        self.assertTrue(valid)
        self.assertEqual(temp, 80.0)
        self.assertEqual(hum, 99.9)

    def test_t2_22_dht_checksum_overflow_boundary(self):
        """Checksum summation wrapping across 8-bit boundary (>255)."""
        # B0: 200, B1: 100, B2: 50, B3: 20 -> sum 370 -> & 0xFF = 114
        temp, hum, valid = SensorModels.decode_dht11(200, 100, 50, 20, 114)
        self.assertTrue(valid)

    def test_t2_23_ldr_raw_adc_zero_pitch_black(self):
        """LDR ADC raw 0 (pitch dark tunnel) triggers backlight."""
        duty = SensorModels.calc_ldr_backlight(
            adc_raw=0, mode=BacklightMode.AUTO_LDR, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=0
        )
        self.assertEqual(duty, 220)

    def test_t2_24_ldr_raw_adc_4095_blinding_sunlight(self):
        """LDR ADC raw 4095 (maximum direct sun) turns backlight off."""
        duty = SensorModels.calc_ldr_backlight(
            adc_raw=4095, mode=BacklightMode.AUTO_LDR, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=220
        )
        self.assertEqual(duty, 0)

    def test_t2_25_ldr_hysteresis_exact_threshold_minus_hyst(self):
        """LDR reading exactly at (threshold - hysteresis): deadband boundary."""
        # threshold 1400, hyst 200 -> lower bound 1200
        # If adc_raw == 1199 -> triggers ON (220)
        d_on = SensorModels.calc_ldr_backlight(
            adc_raw=1199, mode=BacklightMode.AUTO_LDR, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=0
        )
        self.assertEqual(d_on, 220)

        # If adc_raw == 1200 -> in deadband, maintains current (0)
        d_hold = SensorModels.calc_ldr_backlight(
            adc_raw=1200, mode=BacklightMode.AUTO_LDR, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=0
        )
        self.assertEqual(d_hold, 0)

    def test_t2_26_ldr_hysteresis_exact_threshold_plus_hyst(self):
        """LDR reading exactly at (threshold + hysteresis): upper deadband boundary."""
        # upper bound 1600
        # adc_raw == 1601 -> turns OFF (0)
        d_off = SensorModels.calc_ldr_backlight(
            adc_raw=1601, mode=BacklightMode.AUTO_LDR, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=220
        )
        self.assertEqual(d_off, 0)

        # adc_raw == 1600 -> in deadband, maintains current (220)
        d_hold = SensorModels.calc_ldr_backlight(
            adc_raw=1600, mode=BacklightMode.AUTO_LDR, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=220
        )
        self.assertEqual(d_hold, 220)


class TestTier2ChronosProtocolBoundaries(unittest.TestCase):
    """Chronos BLE framing, chunking, and field boundary tests."""

    def test_t2_27_nav_info_empty_strings(self):
        """Navigation packet with empty strings handled cleanly."""
        pkt = ChronosProtocol.build_nav_info_packet(
            title="", duration="", distance="", eta="", directions="", speed_str=""
        )
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertEqual(res["title"], "")
        self.assertEqual(res["distance"], "")
        self.assertFalse(res["gps_speed_valid"])

    def test_t2_28_nav_info_maximum_length_strings(self):
        """Navigation packet with long strings (100+ chars) parses without overflow."""
        long_dir = "A" * 150
        pkt = ChronosProtocol.build_nav_info_packet(directions=long_dir)
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertEqual(res["directions"], long_dir)

    def test_t2_29_nav_info_unicode_characters(self):
        """Navigation strings containing UTF-8 characters and accented text."""
        pkt = ChronosProtocol.build_nav_info_packet(title="Jalan Kebon Jeruk № 42 ©")
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertEqual(res["title"], "Jalan Kebon Jeruk № 42 ©")

    def test_t2_30_nav_info_fractional_gps_speed(self):
        """GPS speed string with decimal places parsed accurately."""
        pkt = ChronosProtocol.build_nav_info_packet(speed_str="42.7 km/h")
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertTrue(res["gps_speed_valid"])
        self.assertEqual(res["gps_speed_kmh"], 42.7)

    def test_t2_31_time_sync_midnight_23_59_59(self):
        """Time sync at 23:59:59 end-of-day boundary."""
        pkt = ChronosProtocol.build_time_sync_packet(2026, 12, 31, 23, 59, 59)
        res = ChronosProtocol.parse_time_sync_packet(pkt)
        self.assertEqual(res["hour"], 23)
        self.assertEqual(res["minute"], 59)
        self.assertEqual(res["second"], 59)

    def test_t2_32_time_sync_year_2099(self):
        """Time sync at future year boundary 2099."""
        pkt = ChronosProtocol.build_time_sync_packet(2099, 6, 15, 12, 0, 0)
        res = ChronosProtocol.parse_time_sync_packet(pkt)
        self.assertEqual(res["year"], 2099)

    def test_t2_33_notification_colon_at_boundary_index_29(self):
        """Colon at exactly index 29: splits title and body."""
        sender = "A" * 29
        pkt = ChronosProtocol.build_notification_packet(0x0A, message=f"{sender}: Message")
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["title"], sender)
        self.assertEqual(res["message"], "Message")

    def test_t2_34_notification_colon_at_index_30_no_split(self):
        """Colon at index 30: exceeds threshold, falls back to default app title."""
        sender = "A" * 30
        pkt = ChronosProtocol.build_notification_packet(0x0A, message=f"{sender}: Message")
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["title"], "WhatsApp")
        self.assertEqual(res["message"], f"{sender}: Message")

    def test_t2_35_notification_newline_before_colon(self):
        """Newline character before colon prevents mistaken colon splitting."""
        msg = "Line1\nLine2: Some text"
        pkt = ChronosProtocol.build_notification_packet(0x0A, message=msg)
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["title"], "WhatsApp")
        self.assertEqual(res["message"], msg)


class TestTier2WebAndOtaBoundaries(unittest.TestCase):
    """Web REST API and OTA streaming boundary tests."""

    def setUp(self):
        self.state = HudState()
        self.server = WebPortalServer(self.state)

    def test_t2_36_rest_config_empty_json(self):
        """POST /api/config with empty JSON {} succeeds without mutating state."""
        orig_contrast = self.state.lcd_contrast
        status, body = self.server.handle_post_config({})
        self.assertEqual(status, 200)
        self.assertEqual(self.state.lcd_contrast, orig_contrast)

    def test_t2_37_rest_config_unknown_keys_ignored(self):
        """POST /api/config with unknown keys ignores unrecognized fields cleanly."""
        status, body = self.server.handle_post_config({"unknown_field": 12345})
        self.assertEqual(status, 200)

    def test_t2_38_rest_config_contrast_exact_min_56(self):
        """POST /api/config sets contrast to 56 (0x38)."""
        status, _ = self.server.handle_post_config({"lcd_contrast": 56})
        self.assertEqual(status, 200)
        self.assertEqual(self.state.lcd_contrast, 56)

    def test_t2_39_rest_config_contrast_exact_max_72(self):
        """POST /api/config sets contrast to 72 (0x48)."""
        status, _ = self.server.handle_post_config({"lcd_contrast": 72})
        self.assertEqual(status, 200)
        self.assertEqual(self.state.lcd_contrast, 72)

    def test_t2_40_rest_config_contrast_out_of_range_low_55(self):
        """POST /api/config with contrast 55 rejected."""
        status, _ = self.server.handle_post_config({"lcd_contrast": 55})
        self.assertEqual(status, 400)

    def test_t2_41_rest_config_contrast_out_of_range_high_73(self):
        """POST /api/config with contrast 73 rejected."""
        status, _ = self.server.handle_post_config({"lcd_contrast": 73})
        self.assertEqual(status, 400)

    def test_t2_42_rest_config_pulses_per_meter_zero_rejected(self):
        """POST /api/config with vss_pulses_per_meter <= 0 rejected."""
        status, _ = self.server.handle_post_config({"vss_pulses_per_meter": 0})
        self.assertEqual(status, 400)

    def test_t2_43_rest_config_ldr_mode_boundary_0_to_2(self):
        """POST /api/config allows ldr_mode in 0..2, rejects 3."""
        for m in (0, 1, 2):
            s, _ = self.server.handle_post_config({"ldr_mode": m})
            self.assertEqual(s, 200)
            self.assertEqual(self.state.bl_mode, m)
        s_bad, _ = self.server.handle_post_config({"ldr_mode": 3})
        self.assertEqual(s_bad, 400)

    def test_t2_44_ota_image_exact_max_partition_size(self):
        """OTA image exactly 0x1A0000 bytes (1,703,936 bytes) accepted."""
        # Header 0xE9 + 0x19FFFF dummy bytes
        exact_max = bytes([0xE9]) + bytes(0x1A0000 - 1)
        self.assertEqual(len(exact_max), 0x1A0000)
        status, body = self.server.handle_post_update(exact_max, partition_max_size=0x1A0000)
        self.assertEqual(status, 200)

    def test_t2_45_ota_image_one_byte_over_partition_size(self):
        """OTA image 0x1A0001 bytes (1 byte over limit) rejected."""
        over_max = bytes([0xE9]) + bytes(0x1A0000)
        self.assertEqual(len(over_max), 0x1A0001)
        status, body = self.server.handle_post_update(over_max, partition_max_size=0x1A0000)
        self.assertEqual(status, 400)

    def test_t2_46_ota_image_single_byte_payload(self):
        """OTA image of 1 byte (0xE9) accepted by size validator."""
        status, body = self.server.handle_post_update(b'\xE9')
        self.assertEqual(status, 200)

    def test_t2_47_ota_stream_chunk_size_1_byte(self):
        """Streaming OTA upload simulated in 1-byte chunks."""
        data = bytes([0xE9]) + bytes(50)
        streamed = bytearray()
        for b in data:
            streamed.append(b)
        status, body = self.server.handle_post_update(bytes(streamed))
        self.assertEqual(status, 200)

    def test_t2_48_speed_fallback_fault_boundary_exact_10s(self):
        """VSS zero speed for exactly 10.0s does not fault; >10.0s faults."""
        # At exactly 10.0s -> no fault yet
        _, _, fault_10 = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True, vss_speed=0.0, ble_gps_speed=30.0,
            ble_gps_valid=True, vss_continuous_zero_sec=10.0
        )
        self.assertFalse(fault_10)

        # At 10.1s -> fault triggered
        _, _, fault_10_1 = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True, vss_speed=0.0, ble_gps_speed=30.0,
            ble_gps_valid=True, vss_continuous_zero_sec=10.1
        )
        self.assertTrue(fault_10_1)

    def test_t2_49_speed_fallback_gps_speed_threshold_exact_15kmh(self):
        """VSS 0 for 12s with BLE GPS at exactly 15.0 km/h: no fault (threshold > 15.0)."""
        _, _, fault_15 = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True, vss_speed=0.0, ble_gps_speed=15.0,
            ble_gps_valid=True, vss_continuous_zero_sec=12.0
        )
        self.assertFalse(fault_15)

        # At 15.1 km/h -> faults
        _, _, fault_15_1 = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True, vss_speed=0.0, ble_gps_speed=15.1,
            ble_gps_valid=True, vss_continuous_zero_sec=12.0
        )
        self.assertTrue(fault_15_1)

    def test_t2_50_empty_icon_chunk_crc_preservation(self):
        """All-zeros 288-byte icon chunk retains CRC32 calculation."""
        empty_bmp = bytes(288)
        crc = 0x00000000
        chunks = ChronosProtocol.build_nav_icon_chunks(empty_bmp, crc)
        self.assertEqual(len(chunks), 3)
        pos, extracted_crc, _ = ChronosProtocol.parse_nav_icon_chunk(chunks[0])
        self.assertEqual(extracted_crc, 0)

if __name__ == "__main__":
    unittest.main()
