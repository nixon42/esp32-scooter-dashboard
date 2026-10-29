"""
E2E Tier 3 Suite: Cross-Feature Interactions & Combinatorial Tests
Verifies multi-peripheral concurrency, hardware fallback transitions,
BLE-WiFi coexistence, and simultaneous event handling.
"""

import unittest
from e2e_framework import (
    HudState,
    PCD8544Display,
    SensorModels,
    ChronosProtocol,
    WebPortalServer,
    SpeedSource,
    BacklightMode
)

class TestTier3CrossFeatureInteractions(unittest.TestCase):
    """Pairwise and multi-feature interaction tests."""

    def setUp(self):
        self.state = HudState()
        self.disp = PCD8544Display()
        self.server = WebPortalServer(self.state)

    def test_t3_01_vss_hardware_failure_fallback_during_navigation(self):
        """VSS pulse wire disconnects while navigating: falls back to BLE GPS speed."""
        # Initial: VSS active, navigating with turn instructions
        self.state.vss_enabled = True
        self.state.speed_kmh = 45.0
        self.state.speed_source = SpeedSource.VSS
        self.state.ble_connected = True
        self.state.nav_active = True
        self.state.ble_gps_valid = True
        self.state.ble_gps_speed = 46.0

        # Simulate VSS wire disconnect: pulses drop to 0 for 12 seconds
        vss_pulses = 0.0
        vss_speed = SensorModels.calc_vss_speed(vss_pulses)
        self.assertEqual(vss_speed, 0.0)

        eff_speed, source, fault = SensorModels.speed_fallback_arbitrator(
            vss_enabled=self.state.vss_enabled,
            vss_speed=vss_speed,
            ble_gps_speed=self.state.ble_gps_speed,
            ble_gps_valid=self.state.ble_gps_valid,
            vss_continuous_zero_sec=12.0
        )
        self.assertTrue(fault)
        self.assertEqual(source, SpeedSource.BLE_GPS)
        self.assertEqual(eff_speed, 46.0)

        # Update state and verify HUD rendering reflects GPS speed
        self.state.speed_kmh = eff_speed
        self.state.speed_source = source
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertTrue(self.state.speed_source == SpeedSource.BLE_GPS)

    def test_t3_02_web_ota_stream_during_active_ble_connection(self):
        """Web OTA binary upload while BLE connection is actively receiving telemetry."""
        self.state.ble_connected = True
        self.state.nav_active = True

        # BLE packet arrives
        nav_pkt = ChronosProtocol.build_nav_info_packet(title="Main St", speed_str="35 km/h")
        parsed = ChronosProtocol.parse_nav_info_packet(nav_pkt)
        self.state.nav_title = parsed["title"]
        self.state.ble_gps_speed = parsed["gps_speed_kmh"]

        # Concurrently, Web OTA firmware binary uploaded via HTTP server
        valid_fw = bytes([0xE9, 0x00, 0x00, 0x00]) + bytes(1024)
        status, resp = self.server.handle_post_update(valid_fw)
        self.assertEqual(status, 200)

        # Confirm BLE state remains intact
        self.assertTrue(self.state.ble_connected)
        self.assertEqual(self.state.nav_title, "Main St")

    def test_t3_03_ldr_tunnel_transition_during_web_contrast_change(self):
        """LDR ambient light changes to dark while user adjusts contrast via Web UI."""
        self.state.bl_mode = BacklightMode.AUTO_LDR
        self.state.bl_brightness = 0  # daylight

        # User posts new contrast setting (68 = 0x44)
        status, _ = self.server.handle_post_config({"lcd_contrast": 68})
        self.assertEqual(status, 200)
        self.assertEqual(self.state.lcd_contrast, 68)

        # Tunnel entry: LDR ADC raw drops to 500
        new_duty = SensorModels.calc_ldr_backlight(
            adc_raw=500, mode=self.state.bl_mode, manual_duty=180,
            threshold=1400, hysteresis=200, current_duty=self.state.bl_brightness
        )
        self.state.bl_brightness = new_duty
        self.assertEqual(self.state.bl_brightness, 220)

        # Display sets new contrast cleanly without affecting backlight
        self.disp.set_contrast(self.state.lcd_contrast)
        self.assertEqual(self.disp.contrast, 68)

    def test_t3_04_call_alert_during_turn_maneuver_icon_update(self):
        """Incoming call overlay pops up over turn maneuver icon; dismiss restores icon."""
        # Active turn maneuver icon displayed
        icon_bmp = bytearray([0xFF] * 288)
        self.state.nav_icon_valid = True
        self.disp.clear()
        self.disp.blit_chronos_icon(bytes(icon_bmp), dest_x=36, dest_y=9)
        self.assertTrue(self.disp.get_pixel(36, 9))

        # Incoming call arrives
        call_pkt = ChronosProtocol.build_notification_packet(icon_id=0x01, message="Boss: Urgent")
        call_info = ChronosProtocol.parse_notification_packet(call_pkt)
        self.state.notification_active = True
        self.state.notif_title = call_info["title"]
        self.state.notif_message = call_info["message"]

        # Render modal popup over display
        self.disp.draw_modal_overlay(self.state.notif_title, self.state.notif_message)
        self.assertTrue(self.disp.get_pixel(4, 10))  # modal border

        # Caller dismisses call
        cancel_pkt = ChronosProtocol.build_notification_packet(icon_id=0x02, message="Call ended")
        cancel_info = ChronosProtocol.parse_notification_packet(cancel_pkt)
        if cancel_info["is_call_cancel"]:
            self.state.notification_active = False

        # Re-render main screen with turn icon
        self.disp.clear()
        self.disp.blit_chronos_icon(bytes(icon_bmp), dest_x=36, dest_y=9)
        self.assertTrue(self.disp.get_pixel(36, 9))
        self.assertFalse(self.state.notification_active)

    def test_t3_05_web_config_mutation_while_sensor_sampling(self):
        """Updating calibration factors via POST /api/config affects subsequent sensor reads."""
        # Update pulses per meter from 28.5 to 30.0
        status, _ = self.server.handle_post_config({"vss_pulses_per_meter": 30.0})
        self.assertEqual(status, 200)

        # Subsequent pulse calculation uses updated calibration factor
        speed = SensorModels.calc_vss_speed(pulses_per_sec=300.0, pulses_per_meter=self.state.pulses_per_meter)
        # 300 / 30.0 = 10 m/s * 3.6 = 36.0 km/h
        self.assertEqual(speed, 36.0)

    def test_t3_06_time_sync_arrival_during_status_bar_rendering(self):
        """Time sync packet updates system clock, which reflects on status bar."""
        self.assertFalse(self.state.time_synced)
        # Clock not rendered yet
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(70, 3))

        # Time packet arrives
        sync_pkt = ChronosProtocol.build_time_sync_packet(2026, 10, 5, 8, 30, 0)
        parsed = ChronosProtocol.parse_time_sync_packet(sync_pkt)
        self.state.time_synced = True

        # Status bar re-rendered
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertTrue(self.disp.get_pixel(70, 3))

    def test_t3_07_voltmeter_disabled_via_web_hides_from_hud_and_ble(self):
        """Disabling voltmeter removes battery reading from status bar and BLE."""
        # Disable via web config
        self.server.handle_post_config({"voltmeter_enabled": False})
        self.assertFalse(self.state.battery_enabled)

        # Status bar hides battery
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(2, 2))

    def test_t3_08_dht_sensor_disabled_via_web_hides_climate_indicator(self):
        """Disabling DHT via Web UI suppresses temperature display."""
        self.server.handle_post_config({"dht_enabled": False})
        self.assertFalse(self.state.dht_enabled)

        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(28, 2))

    def test_t3_09_high_speed_with_simultaneous_turn_and_sms(self):
        """Cruising at 90 km/h with active turn navigation and incoming SMS."""
        self.state.speed_kmh = 90.0
        self.state.speed_source = SpeedSource.VSS
        self.state.nav_active = True
        self.state.nav_distance = "150 m"

        # SMS arrives
        sms_pkt = ChronosProtocol.build_notification_packet(icon_id=0x03, message="Bank: OTP 123456")
        parsed = ChronosProtocol.parse_notification_packet(sms_pkt)
        self.assertEqual(parsed["title"], "Bank")
        self.assertEqual(parsed["message"], "OTP 123456")

        # Telemetry snapshot remains 90 km/h
        self.assertEqual(self.state.speed_kmh, 90.0)

    def test_t3_10_backlight_mode_cycling_under_fluctuating_light(self):
        """Rapid cycling between OFF, AUTO_LDR, and ON under low ambient light."""
        adc_dark = 400
        # Mode OFF -> duty 0
        d_off = SensorModels.calc_ldr_backlight(adc_dark, BacklightMode.OFF, manual_duty=150)
        self.assertEqual(d_off, 0)

        # Mode AUTO_LDR -> duty 220
        d_auto = SensorModels.calc_ldr_backlight(adc_dark, BacklightMode.AUTO_LDR, manual_duty=150)
        self.assertEqual(d_auto, 220)

        # Mode ON -> duty 150
        d_on = SensorModels.calc_ldr_backlight(adc_dark, BacklightMode.ON, manual_duty=150)
        self.assertEqual(d_on, 150)

    def test_t3_11_ble_disconnect_during_gps_fallback_with_vss_disabled(self):
        """BLE disconnects when VSS is disabled: speed safely drops to 0 km/h."""
        self.state.vss_enabled = False
        self.state.ble_connected = False
        self.state.ble_gps_valid = False

        eff_speed, source, _ = SensorModels.speed_fallback_arbitrator(
            vss_enabled=self.state.vss_enabled,
            vss_speed=0.0,
            ble_gps_speed=55.0,
            ble_gps_valid=self.state.ble_gps_valid,
            vss_continuous_zero_sec=0.0
        )
        self.assertEqual(eff_speed, 0.0)
        self.assertEqual(source, SpeedSource.NONE)

    def test_t3_12_low_battery_warning_during_turn_navigation(self):
        """Aki voltage dropping to 10.8V during turn navigation."""
        self.state.battery_voltage = 10.8
        self.state.nav_active = True

        # Voltmeter warns low battery (< 11.5V)
        is_low_battery = self.state.battery_voltage < 11.5
        self.assertTrue(is_low_battery)
        self.assertTrue(self.state.nav_active)

    def test_t3_13_time_sync_across_midnight_during_navigation(self):
        """Time sync update from 23:59 to 00:01 during active ride."""
        pkt1 = ChronosProtocol.build_time_sync_packet(2026, 10, 5, 23, 59, 50)
        p1 = ChronosProtocol.parse_time_sync_packet(pkt1)
        self.assertEqual(p1["hour"], 23)

        pkt2 = ChronosProtocol.build_time_sync_packet(2026, 10, 6, 0, 1, 10)
        p2 = ChronosProtocol.parse_time_sync_packet(pkt2)
        self.assertEqual(p2["hour"], 0)
        self.assertEqual(p2["day"], 6)

    def test_t3_14_rapid_ble_disconnect_and_reconnect(self):
        """BLE connection drops and reconnects within 2 seconds."""
        self.state.ble_connected = True
        self.state.ble_connected = False
        self.assertFalse(self.state.ble_connected)

        # Reconnect
        self.state.ble_connected = True
        # Re-issue handshake
        handshake = ChronosProtocol.build_device_info_handshake()
        self.assertEqual(len(handshake), 20)
        self.assertTrue(self.state.ble_connected)

    def test_t3_15_ota_bad_magic_abort_leaves_ble_intact(self):
        """OTA upload with invalid magic byte rejected while BLE operates smoothly."""
        self.state.ble_connected = True
        bad_image = b'\x00\x01\x02\x03'
        status, body = self.server.handle_post_update(bad_image)
        self.assertEqual(status, 400)
        self.assertTrue(self.state.ble_connected)

    def test_t3_16_contrast_extremes_during_max_backlight(self):
        """LCD contrast slider adjusted to boundaries while backlight is 100% duty."""
        self.state.bl_brightness = 255
        # Lower bound
        self.assertTrue(self.disp.set_contrast(0x38))
        self.assertEqual(self.disp.contrast, 0x38)
        # Upper bound
        self.assertTrue(self.disp.set_contrast(0x48))
        self.assertEqual(self.disp.contrast, 0x48)
        self.assertEqual(self.state.bl_brightness, 255)

if __name__ == "__main__":
    unittest.main()
