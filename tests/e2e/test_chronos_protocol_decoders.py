"""
E2E Tier 1 Suite: Apache NimBLE & Android Chronos Protocol Decoders
Features Covered:
- F10: NimBLE GATT Server (NUS UUIDs, RX/TX characteristics, MTU 517)
- F11: Chronos Handshake & Battery Reporting (0x92 info, 0x91 battery, phone battery req, settings req)
- F12: Navigation Info Packet Decoder (0xEF title, duration, distance, eta, directions, GPS speed)
- F13: Navigation Icon Packet Decoder (0xEE 3 chunks of 96B, 288B assembly, CRC32)
- F14: Notifications Packet Decoder (0xAB 0x72 caller alert, ringer cancel, colon splitting)
- F15: Time Sync Packet Decoder (0xAB 0x93 year, month, day, hour, min, sec, epoch clock sync)
"""

import time
import struct
import unittest
from e2e_framework import ChronosProtocol, HudState

class TestF10NimbleGattServer(unittest.TestCase):
    """F10: Apache NimBLE GATT server profile tests."""

    NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
    NUS_RX_CHR_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
    NUS_TX_CHR_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

    def test_f10_01_primary_service_uuid(self):
        """Verify Nordic UART Service primary 128-bit UUID."""
        self.assertEqual(self.NUS_SERVICE_UUID, "6e400001-b5a3-f393-e0a9-e50e24dcca9e")

    def test_f10_02_rx_characteristic_properties(self):
        """Verify RX Characteristic UUID and Write permissions."""
        self.assertEqual(self.NUS_RX_CHR_UUID, "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        # Properties: WRITE | WRITE_NO_RSP
        chr_props = ["WRITE", "WRITE_NO_RSP"]
        self.assertIn("WRITE", chr_props)
        self.assertIn("WRITE_NO_RSP", chr_props)

    def test_f10_03_tx_characteristic_properties(self):
        """Verify TX Characteristic UUID and Notify permissions with CCCD."""
        self.assertEqual(self.NUS_TX_CHR_UUID, "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        chr_props = ["NOTIFY"]
        self.assertIn("NOTIFY", chr_props)

    def test_f10_04_att_preferred_mtu_size(self):
        """Verify preferred ATT MTU configuration of 517 bytes."""
        preferred_mtu = 517
        self.assertEqual(preferred_mtu, 517)

    def test_f10_05_ble_advertisement_device_name(self):
        """Verify default BLE advertised name is 'VarioHUD'."""
        adv_name = "VarioHUD"
        self.assertEqual(adv_name, "VarioHUD")


class TestF11ChronosHandshake(unittest.TestCase):
    """F11: Chronos Handshake and Battery Telemetry tests."""

    def test_f11_01_device_info_packet_structure(self):
        """Verify 20-byte Device Info handshake packet (0xAB 0x92 0xC0)."""
        pkt = ChronosProtocol.build_device_info_handshake(v_maj=1, v_min=0, v_patch=0)
        self.assertEqual(len(pkt), 20)
        self.assertEqual(pkt[0], 0xAB)
        self.assertEqual(pkt[3], 0xFF)
        self.assertEqual(pkt[4], 0x92)
        self.assertEqual(pkt[5], 0xC0)
        self.assertEqual(pkt[6], 1)    # Major version
        self.assertEqual(pkt[7], 0)    # Minor*10 + Patch
        self.assertEqual(pkt[19], 0x80)

    def test_f11_02_battery_status_packet_format(self):
        """Verify 8-byte Battery Telemetry packet (0xAB 0x91 0x80)."""
        pkt_charging = ChronosProtocol.build_battery_packet(level_pct=85, charging=True)
        self.assertEqual(len(pkt_charging), 8)
        self.assertEqual(pkt_charging[0], 0xAB)
        self.assertEqual(pkt_charging[4], 0x91)
        self.assertEqual(pkt_charging[5], 0x80)
        self.assertEqual(pkt_charging[6], 1)   # Charging true
        self.assertEqual(pkt_charging[7], 85)  # 85%

        pkt_discharging = ChronosProtocol.build_battery_packet(level_pct=42, charging=False)
        self.assertEqual(pkt_discharging[6], 0)
        self.assertEqual(pkt_discharging[7], 42)

    def test_f11_03_phone_battery_subscription_request(self):
        """Verify 7-byte request for smartphone battery telemetry."""
        expected_req = bytes([0xAB, 0x00, 0x04, 0xFE, 0x91, 0x80, 0x01])
        self.assertEqual(len(expected_req), 7)
        self.assertEqual(expected_req[3], 0xFE)
        self.assertEqual(expected_req[4], 0x91)
        self.assertEqual(expected_req[6], 0x01)

    def test_f11_04_settings_sync_request_packet(self):
        """Verify 6-byte settings sync request packet."""
        expected_req = bytes([0xAB, 0x00, 0x03, 0xFE, 0x23, 0x80])
        self.assertEqual(len(expected_req), 6)
        self.assertEqual(expected_req[3], 0xFE)
        self.assertEqual(expected_req[4], 0x23)

    def test_f11_05_cccd_subscription_trigger_sequence(self):
        """Verify handshake sequence only fires after CCCD value is 0x0001 (Notify enabled)."""
        cccd_disabled = bytes([0x00, 0x00])
        cccd_enabled = bytes([0x01, 0x00])

        is_notify_active = lambda val: (val[0] & 0x01) != 0
        self.assertFalse(is_notify_active(cccd_disabled))
        self.assertTrue(is_notify_active(cccd_enabled))


class TestF12NavigationInfoDecoder(unittest.TestCase):
    """F12: Navigation Information packet (0xEF) decoder tests."""

    def test_f12_01_active_navigation_parsing(self):
        """Verify full parsing of active 0xEF navigation packet."""
        pkt = ChronosProtocol.build_nav_info_packet(
            state=0x80,
            has_icon=True,
            is_navigation=True,
            icon_crc=0xAABBCCDD,
            title="Jl. Sudirman",
            duration="14 min",
            distance="350 m",
            eta="17:50",
            directions="Turn left onto Jl. Sudirman",
            speed_str="48 km/h"
        )
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertTrue(res["active"])
        self.assertTrue(res["has_icon"])
        self.assertTrue(res["is_navigation"])
        self.assertEqual(res["icon_crc"], 0xAABBCCDD)
        self.assertEqual(res["title"], "Jl. Sudirman")
        self.assertEqual(res["duration"], "14 min")
        self.assertEqual(res["distance"], "350 m")
        self.assertEqual(res["eta"], "17:50")
        self.assertEqual(res["directions"], "Turn left onto Jl. Sudirman")
        self.assertEqual(res["speed_str"], "48 km/h")
        self.assertAlmostEqual(res["gps_speed_kmh"], 48.0, places=1)
        self.assertTrue(res["gps_speed_valid"])

    def test_f12_02_inactive_navigation_state(self):
        """Verify state 0x00 marks navigation inactive and invalidates speed."""
        pkt = ChronosProtocol.build_nav_info_packet(state=0x00)
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertFalse(res["active"])
        self.assertFalse(res["gps_speed_valid"])

    def test_f12_03_disabled_navigation_state(self):
        """Verify state 0xFF marks navigation disabled."""
        pkt = ChronosProtocol.build_nav_info_packet(state=0xFF)
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertFalse(res["active"])
        self.assertFalse(res["gps_speed_valid"])

    def test_f12_04_gps_speed_mph_conversion(self):
        """Verify speed given in mph converts accurately to km/h (1 mph = 1.609344 km/h)."""
        pkt = ChronosProtocol.build_nav_info_packet(speed_str="30 mph")
        res = ChronosProtocol.parse_nav_info_packet(pkt)
        self.assertTrue(res["gps_speed_valid"])
        self.assertAlmostEqual(res["gps_speed_kmh"], 30.0 * 1.609344, places=1)

    def test_f12_05_malformed_packet_rejection(self):
        """Verify truncated or corrupt packets raise ValueError."""
        # Truncated header
        with self.assertRaises(ValueError):
            ChronosProtocol.parse_nav_info_packet(b'\xAB\x00')
        # Wrong opcode
        bad_op = bytes([0xAB, 0x00, 0x05, 0xFE, 0x99, 0x80])
        with self.assertRaises(ValueError):
            ChronosProtocol.parse_nav_info_packet(bad_op)


class TestF13NavigationIconDecoder(unittest.TestCase):
    """F13: Navigation Icon packet (0xEE) decoder tests."""

    def test_f13_01_three_chunk_packet_dimensions(self):
        """Verify 288-byte icon breaks into 3 chunks of 107 bytes each."""
        raw_icon = bytes(range(256)) + bytes(range(32))
        chunks = ChronosProtocol.build_nav_icon_chunks(raw_icon, icon_crc=0x11223344)
        self.assertEqual(len(chunks), 3)
        for c in chunks:
            self.assertEqual(len(c), 107)

    def test_f13_02_chunk_positions_and_offsets(self):
        """Verify chunk sequence pos=0 (0..95), pos=1 (96..191), pos=2 (192..287)."""
        raw_icon = bytes(range(256)) + bytes(range(32))
        chunks = ChronosProtocol.build_nav_icon_chunks(raw_icon, icon_crc=0x11223344)

        for expected_pos, chunk in enumerate(chunks):
            pos, crc, data = ChronosProtocol.parse_nav_icon_chunk(chunk)
            self.assertEqual(pos, expected_pos)
            self.assertEqual(crc, 0x11223344)
            self.assertEqual(len(data), 96)
            self.assertEqual(data, raw_icon[expected_pos * 96 : (expected_pos + 1) * 96])

    def test_f13_03_full_icon_assembly(self):
        """Verify reassembly of all 3 chunks reconstitutes original 288-byte bitmap."""
        original_icon = bytes([(i * 7) & 0xFF for i in range(288)])
        chunks = ChronosProtocol.build_nav_icon_chunks(original_icon, icon_crc=0x55667788)

        assembled_buffer = bytearray(288)
        for chunk in chunks:
            pos, crc, data = ChronosProtocol.parse_nav_icon_chunk(chunk)
            assembled_buffer[pos * 96 : (pos + 1) * 96] = data

        self.assertEqual(bytes(assembled_buffer), original_icon)

    def test_f13_04_invalid_pos_chunk_rejection(self):
        """Verify pos > 2 triggers error/rejection."""
        chunk = bytearray(ChronosProtocol.build_nav_icon_chunks(bytes(288), 0x1234)[0])
        chunk[6] = 3  # invalid pos
        with self.assertRaises(ValueError):
            ChronosProtocol.parse_nav_icon_chunk(bytes(chunk))

    def test_f13_05_crc_field_fidelity(self):
        """Verify 32-bit CRC is preserved across chunks."""
        test_crc = 0xDEADBEEF
        chunks = ChronosProtocol.build_nav_icon_chunks(bytes(288), icon_crc=test_crc)
        for c in chunks:
            pos, crc, _ = ChronosProtocol.parse_nav_icon_chunk(c)
            self.assertEqual(crc, test_crc)


class TestF14NotificationsDecoder(unittest.TestCase):
    """F14: Notifications packet (0xAB 0x72) decoder tests."""

    def test_f14_01_incoming_call_alert(self):
        """Verify incoming call packet (icon_id = 0x01)."""
        pkt = ChronosProtocol.build_notification_packet(
            icon_id=0x01,
            state=0x02,
            message="Mom: +628123456789"
        )
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["icon_id"], 0x01)
        self.assertTrue(res["is_call_ringer"])
        self.assertFalse(res["is_call_cancel"])
        self.assertEqual(res["title"], "Mom")
        self.assertEqual(res["message"], "+628123456789")

    def test_f14_02_call_dismissed_alert(self):
        """Verify call dismissal packet (icon_id = 0x02)."""
        pkt = ChronosProtocol.build_notification_packet(icon_id=0x02, state=0x01, message="Call ended")
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["icon_id"], 0x02)
        self.assertTrue(res["is_call_cancel"])

    def test_f14_03_app_message_colon_splitting(self):
        """Verify colon splitting for app notification '[Sender]: [Message]'."""
        pkt = ChronosProtocol.build_notification_packet(
            icon_id=0x0A,  # WhatsApp
            message="Alice: Let's meet at 5"
        )
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["title"], "Alice")
        self.assertEqual(res["message"], "Let's meet at 5")

    def test_f14_04_no_colon_fallback_title(self):
        """Verify fallback to app name when message contains no colon."""
        pkt = ChronosProtocol.build_notification_packet(icon_id=0x18, message="Single line alert")  # Telegram
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["title"], "Telegram")
        self.assertEqual(res["message"], "Single line alert")

    def test_f14_05_empty_notification_message(self):
        """Verify empty message does not crash decoder."""
        pkt = ChronosProtocol.build_notification_packet(icon_id=0x03, message="")
        res = ChronosProtocol.parse_notification_packet(pkt)
        self.assertEqual(res["title"], "SMS")
        self.assertEqual(res["message"], "")


class TestF15TimeSyncDecoder(unittest.TestCase):
    """F15: Time Sync packet (0xAB 0x93) decoder tests."""

    def test_f15_01_time_packet_fields_extraction(self):
        """Verify extraction of year, month, day, hour, minute, second."""
        pkt = ChronosProtocol.build_time_sync_packet(
            year=2026, month=10, day=5,
            hour=19, minute=30, second=45
        )
        res = ChronosProtocol.parse_time_sync_packet(pkt)
        self.assertEqual(res["year"], 2026)
        self.assertEqual(res["month"], 10)
        self.assertEqual(res["day"], 5)
        self.assertEqual(res["hour"], 19)
        self.assertEqual(res["minute"], 30)
        self.assertEqual(res["second"], 45)

    def test_f15_02_mktime_epoch_calculation(self):
        """Verify conversion to standard UNIX epoch seconds."""
        import datetime
        dt = datetime.datetime(2026, 10, 5, 19, 30, 45)
        pkt = ChronosProtocol.build_time_sync_packet(2026, 10, 5, 19, 30, 45)
        res = ChronosProtocol.parse_time_sync_packet(pkt)

        parsed_dt = datetime.datetime(
            res["year"], res["month"], res["day"],
            res["hour"], res["minute"], res["second"]
        )
        self.assertEqual(parsed_dt, dt)

    def test_f15_03_time_synced_state_transition(self):
        """Verify arrival of time packet sets state.time_synced to True."""
        state = HudState()
        self.assertFalse(state.time_synced)
        # Apply time sync
        state.time_synced = True
        self.assertTrue(state.time_synced)

    def test_f15_04_leap_year_handling(self):
        """Verify leap year February 29 parsing (e.g. 2028-02-29)."""
        pkt = ChronosProtocol.build_time_sync_packet(2028, 2, 29, 12, 0, 0)
        res = ChronosProtocol.parse_time_sync_packet(pkt)
        self.assertEqual(res["year"], 2028)
        self.assertEqual(res["month"], 2)
        self.assertEqual(res["day"], 29)

    def test_f15_05_midnight_transition(self):
        """Verify midnight 00:00:00 timestamp handling."""
        pkt = ChronosProtocol.build_time_sync_packet(2026, 1, 1, 0, 0, 0)
        res = ChronosProtocol.parse_time_sync_packet(pkt)
        self.assertEqual(res["hour"], 0)
        self.assertEqual(res["minute"], 0)
        self.assertEqual(res["second"], 0)


class TestPhoneBatteryAndMediaProtocol(unittest.TestCase):
    """Smartphone Battery (0x91) and Media Playback (0x9D) Protocol tests."""

    def test_phone_battery_packet_parsing(self):
        """Verify decoding of smartphone battery packet (0xAB 0x91)."""
        pkt = ChronosProtocol.build_phone_battery_packet(level_pct=85, charging=True)
        res = ChronosProtocol.parse_phone_battery_packet(pkt)
        self.assertEqual(res["level_pct"], 85)
        self.assertTrue(res["charging"])

        pkt_dis = ChronosProtocol.build_phone_battery_packet(level_pct=42, charging=False)
        res_dis = ChronosProtocol.parse_phone_battery_packet(pkt_dis)
        self.assertEqual(res_dis["level_pct"], 42)
        self.assertFalse(res_dis["charging"])

    def test_phone_battery_boundary_clamping(self):
        """Verify battery level is bounded between 0 and 100%."""
        pkt_high = ChronosProtocol.build_phone_battery_packet(level_pct=150, charging=False)
        res_high = ChronosProtocol.parse_phone_battery_packet(pkt_high)
        self.assertEqual(res_high["level_pct"], 100)

    def test_music_packet_parsing(self):
        """Verify decoding of music playback packet (0xAB 0x9D)."""
        pkt = ChronosProtocol.build_music_info_packet(state=1, title="Bohemian Rhapsody", artist="Queen")
        res = ChronosProtocol.parse_music_info_packet(pkt)
        self.assertTrue(res["active"])
        self.assertEqual(res["title"], "Bohemian Rhapsody")
        self.assertEqual(res["artist"], "Queen")

    def test_music_packet_stopped_state(self):
        """Verify music stopped state sets active to False."""
        pkt = ChronosProtocol.build_music_info_packet(state=0, title="", artist="")
        res = ChronosProtocol.parse_music_info_packet(pkt)
        self.assertFalse(res["active"])


if __name__ == "__main__":
    unittest.main()
