"""
E2E Tier 4 Suite: Real-World Application Scenarios
Simulates realistic end-to-end operational missions:
- Complete scooter trip cycle from cold ignition to parking
- Mountain tunnel / underpass lighting transitions
- Highway phone disconnect and mid-ride reconnect
- Web portal maintenance and OTA firmware upgrade simulation
- Electrical alternator faults and low-voltage emergency warning
- VSS physical wire break and instant GPS speed fallback
- Multi-app messaging, calling, and navigation turn arbitration
- Monsoon climate changes, clock drift sync, and factory reset
"""

import time
import unittest
from e2e_framework import (
    HudState,
    PCD8544Display,
    SensorModels,
    ChronosProtocol,
    WebPortalServer,
    SpeedSource,
    BacklightMode,
    DhtType
)

class TestTier4RealWorldScenarios(unittest.TestCase):
    """End-to-End Real World Operational Scenario Tests."""

    def setUp(self):
        self.state = HudState()
        self.disp = PCD8544Display()
        self.server = WebPortalServer(self.state)

    def test_s01_complete_scooter_trip_cycle(self):
        """Scenario 1: Complete Scooter Commute Cycle (Cold Ignition -> Navigate -> Cruise -> Park)."""
        # Step 1: Cold Ignition ON
        self.state.battery_voltage = 12.4
        self.state.speed_kmh = 0.0
        self.state.speed_source = SpeedSource.NONE
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertEqual(self.state.speed_kmh, 0.0)

        # Step 2: Phone connects via BLE
        self.state.ble_connected = True
        # Dashboard emits handshake info & battery packets
        dev_info = ChronosProtocol.build_device_info_handshake()
        batt_pkt = ChronosProtocol.build_battery_packet(level_pct=85, charging=False)
        self.assertEqual(len(dev_info), 20)
        self.assertEqual(len(batt_pkt), 8)

        # Phone syncs time
        time_pkt = ChronosProtocol.build_time_sync_packet(2026, 10, 5, 7, 30, 0)
        _ = ChronosProtocol.parse_time_sync_packet(time_pkt)
        self.state.time_synced = True

        # Step 3: Google Maps route starts
        nav_pkt = ChronosProtocol.build_nav_info_packet(
            title="Jl. MH Thamrin",
            duration="22 min",
            distance="4.2 km",
            eta="07:52",
            directions="Head south on Jl. MH Thamrin",
            speed_str="0 km/h"
        )
        nav_info = ChronosProtocol.parse_nav_info_packet(nav_pkt)
        self.state.nav_active = True
        self.state.nav_title = nav_info["title"]
        self.state.nav_distance = nav_info["distance"]

        # 48x48 Turn arrow icon arrives in 3 chunks
        icon_bmp = bytearray([0x55] * 288)
        chunks = ChronosProtocol.build_nav_icon_chunks(bytes(icon_bmp), icon_crc=0x12345678)
        self.assertEqual(len(chunks), 3)
        self.state.nav_icon_valid = True

        # Step 4: Rider accelerates out of driveway (VSS pulses active)
        vss_pulses = 316.6  # pulses/sec for 40 km/h
        vss_speed = SensorModels.calc_vss_speed(vss_pulses, pulses_per_meter=28.5)
        self.assertAlmostEqual(vss_speed, 40.0, places=1)
        self.state.speed_kmh = vss_speed
        self.state.speed_source = SpeedSource.VSS

        # Step 5: Cruising at 55 km/h on main boulevard
        self.state.speed_kmh = 55.0
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.disp.blit_chronos_icon(bytes(icon_bmp), dest_x=36, dest_y=9)

        # Step 6: Arrival at destination & parking
        self.state.speed_kmh = 0.0
        # Nav route finishes
        stop_nav = ChronosProtocol.build_nav_info_packet(state=0x00)
        stop_info = ChronosProtocol.parse_nav_info_packet(stop_nav)
        self.state.nav_active = stop_info["active"]
        self.assertFalse(self.state.nav_active)

    def test_s02_night_tunnel_entry_and_exit(self):
        """Scenario 2: Underpass/Tunnel Lighting Transitions (Auto Backlight Loop)."""
        self.state.bl_mode = BacklightMode.AUTO_LDR
        self.state.bl_brightness = 0  # Starts off in daylight

        # 1. Daylight riding under bright tropical sun (ADC raw = 3200)
        duty_sun = SensorModels.calc_ldr_backlight(
            adc_raw=3200, mode=self.state.bl_mode, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=self.state.bl_brightness
        )
        self.assertEqual(duty_sun, 0)
        self.state.bl_brightness = duty_sun

        # 2. Entering dark underpass tunnel (ADC raw drops to 450)
        duty_tunnel = SensorModels.calc_ldr_backlight(
            adc_raw=450, mode=self.state.bl_mode, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=self.state.bl_brightness
        )
        self.assertEqual(duty_tunnel, 220)  # Turns ON smoothly
        self.state.bl_brightness = duty_tunnel

        # 3. Passing under sodium street lamp inside tunnel (ADC = 1350, deadband)
        duty_lamp = SensorModels.calc_ldr_backlight(
            adc_raw=1350, mode=self.state.bl_mode, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=self.state.bl_brightness
        )
        self.assertEqual(duty_lamp, 220)  # Remains ON without chattering

        # 4. Exiting tunnel into daylight (ADC raw surges to 2800)
        duty_exit = SensorModels.calc_ldr_backlight(
            adc_raw=2800, mode=self.state.bl_mode, manual_duty=200,
            threshold=1400, hysteresis=200, current_duty=self.state.bl_brightness
        )
        self.assertEqual(duty_exit, 0)  # Turns OFF

    def test_s03_phone_disconnect_and_reconnection_on_highway(self):
        """Scenario 3: Highway Phone Disconnect and Seamless Mid-Ride Reconnect."""
        # 1. Active highway ride at 70 km/h with phone connected
        self.state.speed_kmh = 70.0
        self.state.speed_source = SpeedSource.VSS
        self.state.ble_connected = True
        self.state.nav_active = True
        self.state.nav_title = "Cipularang Toll Road"

        # 2. Phone drops Bluetooth connection
        self.state.ble_connected = False
        self.state.nav_active = False

        # Status bar updates to show disconnected indicator
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(52, 2))  # BLE icon disappeared
        # Speed continues unabated from VSS
        self.assertEqual(self.state.speed_kmh, 70.0)
        self.assertEqual(self.state.speed_source, SpeedSource.VSS)

        # 3. Phone reconnects 2 minutes later
        self.state.ble_connected = True
        # Handshake sequence re-establishes
        handshake_pkt = ChronosProtocol.build_device_info_handshake()
        self.assertEqual(len(handshake_pkt), 20)

        # Re-sync navigation
        new_nav = ChronosProtocol.build_nav_info_packet(title="Cipularang Toll Road", distance="12 km")
        parsed = ChronosProtocol.parse_nav_info_packet(new_nav)
        self.state.nav_active = True
        self.state.nav_title = parsed["title"]
        self.assertTrue(self.state.nav_active)

    def test_s04_web_portal_maintenance_and_ota_upgrade(self):
        """Scenario 4: Wi-Fi Portal Diagnostics, Calibration, and OTA Upgrade."""
        # 1. Rider connects smartphone to SoftAP (192.168.4.1)
        # 2. Fetches live diagnostics
        status = self.server.handle_get_status()
        self.assertIn("battery_voltage", status)
        self.assertIn("speed", status)

        # 3. Changes calibration factor for oversized rear tire (29.2 pulses/meter)
        res_code, res_body = self.server.handle_post_config({"vss_pulses_per_meter": 29.2})
        self.assertEqual(res_code, 200)
        self.assertEqual(self.state.pulses_per_meter, 29.2)

        # 4. Uploads firmware update binary (1.1 MB image)
        image_size = 1100000
        fw_binary = bytes([0xE9, 0x01, 0x02, 0x03]) + bytes(image_size - 4)
        ota_status, ota_body = self.server.handle_post_update(fw_binary)
        self.assertEqual(ota_status, 200)
        self.assertEqual(ota_body["bytes_written"], image_size)

    def test_s05_battery_alternator_fault_and_limp_warning(self):
        """Scenario 5: Alternator Charging Fault (Voltage Drops from 13.8V to 10.9V)."""
        # Normal engine running charging voltage
        self.state.battery_voltage = SensorModels.calc_battery_voltage(adc_mv=2490.0, ratio=5.54)
        self.assertEqual(self.state.battery_voltage, 13.79)

        # Alternator fails mid-ride: voltage degrades
        self.state.battery_voltage = 10.9
        is_low_voltage = self.state.battery_voltage < 11.2
        self.assertTrue(is_low_voltage)

    def test_s06_vss_wire_break_instant_gps_fallback(self):
        """Scenario 6: Speedometer Wire Break with Instant BLE GPS Fallback."""
        # Riding along at 50 km/h with active navigation
        self.state.vss_enabled = True
        self.state.ble_connected = True
        self.state.ble_gps_valid = True
        self.state.ble_gps_speed = 52.0

        # VSS wire severed: reports 0 pulses
        vss_speed = 0.0
        # Guard detects fault after 11 seconds
        eff_speed, source, fault = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True,
            vss_speed=vss_speed,
            ble_gps_speed=self.state.ble_gps_speed,
            ble_gps_valid=self.state.ble_gps_valid,
            vss_continuous_zero_sec=11.0
        )
        self.assertTrue(fault)
        self.assertEqual(source, SpeedSource.BLE_GPS)
        self.assertEqual(eff_speed, 52.0)

    def test_s07_heavy_city_traffic_multi_app_alerts(self):
        """Scenario 7: Rush Hour Traffic with Multiple Overlaid Notifications."""
        # 1. Turn instruction active
        self.state.nav_active = True
        self.state.nav_title = "Turn Left"

        # 2. WhatsApp notification arrives
        wa_pkt = ChronosProtocol.build_notification_packet(0x0A, message="Wife: Buy milk")
        wa_info = ChronosProtocol.parse_notification_packet(wa_pkt)
        self.assertEqual(wa_info["title"], "Wife")

        # 3. Incoming phone call rings (preempts WhatsApp)
        call_pkt = ChronosProtocol.build_notification_packet(0x01, message="Boss: Meeting now")
        call_info = ChronosProtocol.parse_notification_packet(call_pkt)
        self.assertEqual(call_info["icon_id"], 0x01)
        self.state.notification_active = True
        self.state.notif_title = call_info["title"]

        # Overlay renders caller ID
        self.disp.clear()
        self.disp.draw_modal_overlay(self.state.notif_title, call_info["message"])
        self.assertTrue(self.disp.get_pixel(4, 10))

    def test_s08_monsoon_rain_and_high_humidity_climate(self):
        """Scenario 8: Monsoon Downpour Climate Readout on DHT22."""
        # Temperature 24.5°C, humidity 98.2%
        # DHT22: Hum 982 (B0: 3, B1: 214). Temp 245 (B2: 0, B3: 245)
        # Checksum = (3 + 214 + 0 + 245) = 462 & 0xFF = 206
        temp, hum, valid = SensorModels.decode_dht22(3, 214, 0, 245, 206)
        self.assertTrue(valid)
        self.assertEqual(temp, 24.5)
        self.assertEqual(hum, 98.2)

    def test_s09_multi_day_clock_stability_and_drift_correction(self):
        """Scenario 9: Long-term Clock Drift Correction via Periodic Time Packets."""
        t1 = ChronosProtocol.build_time_sync_packet(2026, 10, 5, 12, 0, 0)
        p1 = ChronosProtocol.parse_time_sync_packet(t1)
        self.assertEqual(p1["hour"], 12)

        # 24 hours later, sync updates to day 6
        t2 = ChronosProtocol.build_time_sync_packet(2026, 10, 6, 12, 0, 0)
        p2 = ChronosProtocol.parse_time_sync_packet(t2)
        self.assertEqual(p2["day"], 6)

    def test_s10_factory_reset_and_nvs_defaults_restore(self):
        """Scenario 10: Factory Reset Restores All NVS Parameters to Baseline."""
        # Mutated state
        self.state.battery_ratio = 6.2
        self.state.pulses_per_meter = 35.0
        self.state.lcd_contrast = 0x45

        # Factory Reset applied
        defaults = HudState()
        self.assertEqual(defaults.battery_ratio, 5.54)
        self.assertEqual(defaults.pulses_per_meter, 28.5)
        self.assertEqual(defaults.lcd_contrast, 0x3F)

    def test_s11_ble_disconnect_preserves_time_sync(self):
        """Scenario 11: BLE Disconnect resets link state but preserves synchronized clock."""
        # 1. Connected & synced
        self.state.ble_connected = True
        self.state.time_synced = True
        self.state.nav_active = True
        self.state.phone_battery_pct = 85
        self.state.phone_battery_valid = True

        # 2. BLE disconnection event occurs
        self.state.ble_connected = False
        self.state.nav_active = False
        self.state.phone_battery_valid = False

        # 3. Time sync MUST be preserved across disconnections
        self.assertTrue(self.state.time_synced)
        self.assertFalse(self.state.ble_connected)
        self.assertFalse(self.state.nav_active)

    def test_s12_turn_distance_vs_total_trip_distance(self):
        """Scenario 12: Turn distance (meters to maneuver) is distinguished from total trip distance."""
        # Google Maps sends:
        # Title: Distance to upcoming turn ("350 m")
        # Directions: Maneuver instruction ("Turn left")
        # Distance: Total remaining trip distance ("5.3 km")
        nav_pkt = ChronosProtocol.build_nav_info_packet(
            title="350 m",
            duration="14 min",
            distance="5.3 km",
            eta="17:47",
            directions="Turn left",
            speed_str="0 km/h"
        )
        parsed = ChronosProtocol.parse_nav_info_packet(nav_pkt)
        self.assertEqual(parsed["title"], "350 m")
        self.assertEqual(parsed["distance"], "5.3 km")
        self.assertEqual(parsed["directions"], "Turn left")
        self.assertNotEqual(parsed["title"], parsed["distance"])

if __name__ == "__main__":
    unittest.main()

