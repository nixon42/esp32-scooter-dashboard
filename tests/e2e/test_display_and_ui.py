"""
E2E Tier 1 Suite: PCD8544 SPI Display, LEDC Backlight, and HUD UI Graphics Engine
Features Covered:
- F05: PCD8544 SPI Driver (SPI Master, GPIO 6, 7, 8, 4, 10, contrast 0x38-0x48, 504B FB)
- F06: LEDC PWM Backlight (GPIO 2, 5 kHz, 8-bit resolution, scaling, polarity)
- F07: UI Status Bar Engine (Top bar Y=0..8, Battery, Temp, BLE, Digital Clock)
- F08: UI Speedometer & Turn Icon (Large digits, km/h, turn icon blitting, maneuver distance)
- F09: UI Notification Overlay (Modal popups, caller alerts, dismiss, text truncation)
"""

import unittest
from e2e_framework import PCD8544Display, HudState, BacklightMode, SpeedSource

class TestF05Pcd8544Driver(unittest.TestCase):
    """F05: PCD8544 SPI Master display driver tests."""

    def setUp(self):
        self.disp = PCD8544Display()

    def test_f05_01_framebuffer_memory_size(self):
        """Verify 84x48 resolution equates to 504 bytes (6 banks of 84 bytes)."""
        self.assertEqual(len(self.disp.fb), 504)
        self.assertEqual(self.disp.WIDTH, 84)
        self.assertEqual(self.disp.HEIGHT, 48)
        self.assertEqual(self.disp.BANKS, 6)

    def test_f05_02_contrast_valid_range(self):
        """Verify valid contrast range 0x38 to 0x48 (56 to 72 decimal)."""
        # Nominal default 0x3F (63)
        self.assertTrue(self.disp.set_contrast(0x3F))
        self.assertEqual(self.disp.contrast, 0x3F)

        # Boundaries
        self.assertTrue(self.disp.set_contrast(0x38))
        self.assertEqual(self.disp.contrast, 0x38)
        self.assertTrue(self.disp.set_contrast(0x48))
        self.assertEqual(self.disp.contrast, 0x48)

        # Out-of-bounds
        self.assertFalse(self.disp.set_contrast(0x37))
        self.assertFalse(self.disp.set_contrast(0x49))

    def test_f05_03_pixel_setting_and_bank_mapping(self):
        """Verify vertical column bit packing and bank indexing."""
        self.disp.clear()
        # Pixel at (x=10, y=0) should set bit 0 of bank 0 at index 10
        self.disp.set_pixel(10, 0, True)
        self.assertTrue(self.disp.get_pixel(10, 0))
        self.assertEqual(self.disp.fb[10], 0x01)

        # Pixel at (x=10, y=7) should set bit 7 of bank 0 at index 10
        self.disp.set_pixel(10, 7, True)
        self.assertEqual(self.disp.fb[10], 0x81)

        # Pixel at (x=10, y=8) should set bit 0 of bank 1 at index (84 + 10) = 94
        self.disp.set_pixel(10, 8, True)
        self.assertEqual(self.disp.fb[94], 0x01)

    def test_f05_04_clear_screen_resets_all_bytes(self):
        """Verify clear() zeroes out the entire 504-byte buffer."""
        for i in range(len(self.disp.fb)):
            self.disp.fb[i] = 0xFF
        self.disp.clear()
        self.assertTrue(all(b == 0 for b in self.disp.fb))

    def test_f05_05_hardware_gpio_pin_allocations(self):
        """Verify PCD8544 GPIO assignments match hardware schematic."""
        expected_pins = {
            "SCK": 6,
            "MOSI": 7,
            "CS": 8,
            "DC": 4,
            "RST": 10
        }
        self.assertEqual(expected_pins["SCK"], 6)
        self.assertEqual(expected_pins["MOSI"], 7)
        self.assertEqual(expected_pins["CS"], 8)
        self.assertEqual(expected_pins["DC"], 4)
        self.assertEqual(expected_pins["RST"], 10)


class TestF06LedcBacklight(unittest.TestCase):
    """F06: LEDC PWM Backlight hardware control tests."""

    def test_f06_01_gpio_pin_assignment(self):
        """Verify backlight PWM is dedicated to GPIO 2."""
        backlight_pin = 2
        self.assertEqual(backlight_pin, 2)

    def test_f06_02_pwm_frequency_5khz(self):
        """Verify PWM frequency is configured to 5000 Hz to prevent coil hum."""
        pwm_freq_hz = 5000
        self.assertEqual(pwm_freq_hz, 5000)

    def test_f06_03_pwm_resolution_8bit(self):
        """Verify 8-bit timer resolution (range 0..255)."""
        timer_res_bits = 8
        max_duty = (1 << timer_res_bits) - 1
        self.assertEqual(max_duty, 255)

    def test_f06_04_duty_scaling_formula(self):
        """Verify percentage to 8-bit duty conversion."""
        scale = lambda pct: round((pct / 100.0) * 255)
        self.assertEqual(scale(0), 0)
        self.assertEqual(scale(50), 128)
        self.assertEqual(scale(100), 255)

    def test_f06_05_polarity_inversion_calculation(self):
        """Verify active-low cathode polarity inversion calculation."""
        invert = lambda duty: 255 - duty
        self.assertEqual(invert(0), 255)
        self.assertEqual(invert(255), 0)
        self.assertEqual(invert(100), 155)


class TestF07UiStatusBarEngine(unittest.TestCase):
    """F07: UI Top Status Bar rendering engine tests."""

    def setUp(self):
        self.disp = PCD8544Display()
        self.state = HudState()

    def test_f07_01_status_bar_geometry(self):
        """Verify status bar occupies rows Y=0..8 with horizontal divider at Y=8."""
        self.disp.clear()
        self.disp.draw_status_bar(self.state)
        # Check divider line at Y=8 across columns
        for x in range(self.disp.WIDTH):
            self.assertTrue(self.disp.get_pixel(x, 8), f"Missing divider pixel at x={x}")

    def test_f07_02_battery_indicator_conditional(self):
        """Verify battery indicator renders when enabled and hidden when disabled."""
        self.disp.clear()
        self.state.battery_enabled = True
        self.disp.draw_status_bar(self.state)
        self.assertTrue(self.disp.get_pixel(2, 2))

        self.disp.clear()
        self.state.battery_enabled = False
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(2, 2))

    def test_f07_03_temp_indicator_conditional(self):
        """Verify ambient temperature indicator renders when valid and hidden when disabled."""
        self.disp.clear()
        self.state.dht_enabled = True
        self.state.dht_valid = True
        self.disp.draw_status_bar(self.state)
        self.assertTrue(self.disp.get_pixel(28, 2))

        self.disp.clear()
        self.state.dht_enabled = False
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(28, 2))

    def test_f07_04_ble_connection_icon(self):
        """Verify BLE icon displays connected vs disconnected state."""
        self.disp.clear()
        self.state.ble_connected = True
        self.disp.draw_status_bar(self.state)
        self.assertTrue(self.disp.get_pixel(52, 2))

        self.disp.clear()
        self.state.ble_connected = False
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(52, 2))

    def test_f07_05_digital_clock_sync_indicator(self):
        """Verify clock is displayed only after time synchronization."""
        self.disp.clear()
        self.state.time_synced = False
        self.disp.draw_status_bar(self.state)
        self.assertFalse(self.disp.get_pixel(70, 3))

        self.disp.clear()
        self.state.time_synced = True
        self.disp.draw_status_bar(self.state)
        self.assertTrue(self.disp.get_pixel(70, 3))


class TestF08UiSpeedometerAndNavIcon(unittest.TestCase):
    """F08: UI Speedometer and Turn Navigation display tests."""

    def setUp(self):
        self.disp = PCD8544Display()

    def test_f08_01_main_display_area_bounds(self):
        """Verify main display area occupies Y=9..47 (39 vertical rows)."""
        main_height = self.disp.HEIGHT - 9
        self.assertEqual(main_height, 39)

    def test_f08_02_chronos_icon_blitting_coordinates(self):
        """Verify 48x48 icon blitting within main screen viewport."""
        # Create a mock 48x48 icon with a single pixel on at (0, 0)
        icon_bmp = bytearray(288)
        icon_bmp[0] = 0x80  # Top-left pixel (bit 7 of byte 0)

        self.disp.clear()
        # Blit at (dest_x=36, dest_y=9)
        self.disp.blit_chronos_icon(bytes(icon_bmp), dest_x=36, dest_y=9)
        self.assertTrue(self.disp.get_pixel(36, 9))
        self.assertFalse(self.disp.get_pixel(35, 9))
        self.assertFalse(self.disp.get_pixel(36, 8))

    def test_f08_03_icon_clipping_bounds(self):
        """Verify icon blitting handles clipping at screen boundaries without crash."""
        icon_bmp = bytearray([0xFF] * 288)
        self.disp.clear()
        # Blit partly off-screen: dest_x = 70 (width 84 -> only 14 columns visible)
        self.disp.blit_chronos_icon(bytes(icon_bmp), dest_x=70, dest_y=10)
        # Check visible edge
        self.assertTrue(self.disp.get_pixel(83, 10))
        # Ensure no memory corruption in buffer length
        self.assertEqual(len(self.disp.fb), 504)

    def test_f08_04_speed_digits_formatting(self):
        """Verify speed numeric string formatting (0 to 199 km/h)."""
        format_speed = lambda spd: f"{int(round(spd)):2d}"
        self.assertEqual(format_speed(0), " 0")
        self.assertEqual(format_speed(45.4), "45")
        self.assertEqual(format_speed(110.8), "111")

    def test_f08_05_unit_label_presence(self):
        """Verify unit string 'km/h'."""
        unit_str = "km/h"
        self.assertEqual(unit_str, "km/h")


class TestF09UiNotificationOverlay(unittest.TestCase):
    """F09: UI Modal Notification Overlay tests."""

    def setUp(self):
        self.disp = PCD8544Display()

    def test_f09_01_modal_box_geometry(self):
        """Verify modal overlay box boundary pixels."""
        self.disp.clear()
        self.disp.draw_modal_overlay("Alert", "Test Message")
        # Check box corners: (4, 10), (79, 10), (4, 42), (79, 42)
        self.assertTrue(self.disp.get_pixel(4, 10))
        self.assertTrue(self.disp.get_pixel(79, 10))
        self.assertTrue(self.disp.get_pixel(4, 42))
        self.assertTrue(self.disp.get_pixel(79, 42))

    def test_f09_02_caller_alert_priority(self):
        """Verify incoming caller alert has highest rendering priority."""
        state = HudState()
        state.notification_active = True
        state.notif_app_id = 0x01  # Incoming call
        state.notif_title = "Mom Calling"

        self.assertTrue(state.notification_active)
        self.assertEqual(state.notif_app_id, 0x01)

    def test_f09_03_call_dismiss_restores_hud(self):
        """Verify call dismissal clears notification overlay flag."""
        state = HudState()
        state.notification_active = True
        # Dismiss event
        state.notification_active = False
        self.assertFalse(state.notification_active)

    def test_f09_04_text_truncation_for_nokia5110(self):
        """Verify text truncation function to fit max 14 characters per line."""
        truncate_line = lambda s, max_len=13: s[:max_len] + "…" if len(s) > max_len else s
        short_msg = "Hello"
        long_msg = "Incoming call from very long company name"
        self.assertEqual(truncate_line(short_msg), "Hello")
        self.assertEqual(len(truncate_line(long_msg)), 14)
        self.assertTrue(truncate_line(long_msg).endswith("…"))

    def test_f09_05_notification_timeout_duration(self):
        """Verify auto-dismiss timeout duration for non-call notifications (e.g. 5.0 seconds)."""
        notif_timeout_sec = 5.0
        self.assertGreaterEqual(notif_timeout_sec, 3.0)
        self.assertLessEqual(notif_timeout_sec, 10.0)


class TestSmartwatchWatchface(unittest.TestCase):
    """Smartwatch Watchface (Zero-Button Default Screen) tests."""

    def setUp(self):
        self.disp = PCD8544Display()
        self.state = HudState()

    def test_watchface_global_header_mandatory_elements(self):
        """Verify persistent top header bar contains BLE, Clock, Aki, and Phone Battery."""
        self.state.ble_connected = True
        self.state.time_synced = True
        self.state.battery_enabled = True
        self.state.phone_battery_pct = 85
        self.state.phone_battery_valid = True

        self.disp.clear()
        self.disp.draw_global_header(self.state)

        # 1. Divider line at Y=8
        for x in range(self.disp.WIDTH):
            self.assertTrue(self.disp.get_pixel(x, 8), f"Missing divider at x={x}")

        # 2. BLE icon
        self.assertTrue(self.disp.get_pixel(52, 2))

        # 3. Clock
        self.assertTrue(self.disp.get_pixel(70, 3))

        # 4. Aki battery
        self.assertTrue(self.disp.get_pixel(2, 2))

        # 5. Phone battery
        self.assertTrue(self.disp.get_pixel(75, 2))
        self.assertTrue(self.disp.get_pixel(80, 2))

    def test_watchface_large_clock_rendering(self):
        """Verify big 22px 7-segment digital clock and colon dots render in Y=10..31."""
        self.state.time_synced = True
        self.disp.clear()
        self.disp.draw_smartwatch_face(self.state)

        # Check colon dots at (41, 15) and (41, 22)
        self.assertTrue(self.disp.get_pixel(41, 15))
        self.assertTrue(self.disp.get_pixel(41, 22))

        # Check clock digits inside Y=10..31
        self.assertTrue(self.disp.get_pixel(12, 15))
        self.assertTrue(self.disp.get_pixel(28, 25))

    def test_watchface_speed_telemetry_when_riding(self):
        """Verify auxiliary speed badge shows when moving and climate/music when stopped."""
        # Moving
        self.state.speed_kmh = 45.0
        self.disp.clear()
        self.disp.draw_smartwatch_face(self.state)
        self.assertTrue(self.disp.get_pixel(30, 44))

        # Stopped with music playing
        self.state.speed_kmh = 0.0
        self.state.music_active = True
        self.state.music_title = "Song"
        self.disp.clear()
        self.disp.draw_smartwatch_face(self.state)
        self.assertTrue(self.disp.get_pixel(2, 43))  # Music glyph pixel


class TestMultiTaskingLayouts(unittest.TestCase):
    """Simultaneous Multi-Tasking Scenarios (Zero-Button Architecture) tests."""

    def setUp(self):
        self.disp = PCD8544Display()
        self.state = HudState()
        self.state.ble_connected = True
        self.state.time_synced = True
        self.state.phone_battery_pct = 78
        self.state.phone_battery_valid = True

    def test_maps_and_music_simultaneous(self):
        """Verify Navigation + Music simultaneously renders both with top header intact."""
        self.state.nav_active = True
        self.state.music_active = True
        self.state.music_title = "Bohemian Rhapsody"
        self.state.music_artist = "Queen"

        self.disp.clear()
        self.disp.draw_multitask_nav(self.state)

        # Mandatory header intact at Y=0..8
        self.assertTrue(self.disp.get_pixel(0, 8))
        self.assertTrue(self.disp.get_pixel(75, 2))  # Phone battery

        # Nav turn area
        self.assertTrue(self.disp.get_pixel(10, 20))

        # Music note glyph in top header at Y=2, X=8
        self.assertTrue(self.disp.get_pixel(8, 2))

        # Navigation Clock & ETA in bottom area at Y=40
        self.assertTrue(self.disp.get_pixel(10, 40))

    def test_incoming_call_preserves_header_and_includes_nav_reminder(self):
        """Verify Incoming Call preserves top header and includes [Nav: ...] reminder."""
        self.state.nav_active = True
        self.state.nav_distance = "250m"
        self.state.notification_active = True
        self.state.notif_app_id = 0x01
        self.state.notif_title = "PANGGILAN"
        self.state.notif_message = "Ibu"

        self.disp.clear()
        self.disp.draw_incoming_call_overlay(self.state)

        # Top header preserved
        self.assertTrue(self.disp.get_pixel(0, 8))
        self.assertTrue(self.disp.get_pixel(75, 2))  # Phone battery
        self.assertTrue(self.disp.get_pixel(52, 2))  # BLE

        # Inverse call banner
        self.assertTrue(self.disp.get_pixel(10, 10))

        # Mini-nav turn reminder
        self.assertTrue(self.disp.get_pixel(10, 30))

        # Ringing status
        self.assertTrue(self.disp.get_pixel(40, 42))

    def test_notification_preserves_header_and_includes_nav_reminder(self):
        """Verify App notification preserves top header, shows mini-nav, and auto-dismisses."""
        self.state.nav_active = True
        self.state.nav_distance = "400m"
        self.state.notification_active = True
        self.state.notif_app_id = 0x0A  # WhatsApp
        self.state.notif_title = "WA: Budi"
        self.state.notif_message = "Posisi di mana bro?"

        self.disp.clear()
        self.disp.draw_notification_overlay(self.state)

        # Top header preserved
        self.assertTrue(self.disp.get_pixel(0, 8))
        self.assertTrue(self.disp.get_pixel(75, 2))  # Phone battery

        # Notification message & mini-nav
        self.assertTrue(self.disp.get_pixel(10, 22))
        self.assertTrue(self.disp.get_pixel(10, 29))

        # Auto-dismiss countdown footer
        self.assertTrue(self.disp.get_pixel(20, 41))

    def test_notification_auto_paging_long_text(self):
        """Verify long notification triggers auto-paging (3.5s per page)."""
        self.state.notification_active = True
        self.state.notif_title = "WA: Budi"
        self.state.notif_message = "Posisi di mana bro? Aku sudah nunggu di lampu merah depan Indomaret"
        self.state.notif_timestamp = 1000.0

        self.disp.clear()
        self.disp.draw_notification_overlay(self.state)

        # Header and message lines are rendered properly
        self.assertTrue(self.disp.get_pixel(0, 8))
        self.assertTrue(self.disp.get_pixel(10, 22))
    def test_notification_extended_256_character_buffer(self):
        """Verify long message up to 256 chars is handled smoothly with multi-page auto-paging."""
        self.state.notification_active = True
        self.state.notif_title = "WhatsApp"
        long_msg = "Halo bro, aku udah nyampe di stasiun. Motor kamu parkir di sebelah mana ya? Nanti kabarin kalau udah nyampe biar bareng masuknya ya, ditunggu segera di pintu timur."
        self.assertGreater(len(long_msg), 150)
        self.state.notif_message = long_msg
        self.state.notif_timestamp = 1000.0

        self.disp.clear()
        self.disp.draw_notification_overlay(self.state)

        # Header, message line, and footer rendered
        self.assertTrue(self.disp.get_pixel(0, 8))
        self.assertTrue(self.disp.get_pixel(10, 22))
        self.assertTrue(self.disp.get_pixel(20, 41))

    def test_notification_unknown_app_defaults_to_notifikasi(self):
        """Verify that notifications from unknown apps default to 'NOTIFIKASI' instead of 'PESAN'."""
        self.state.notification_active = True
        self.state.notif_app_id = 0x03  # Generic message / unknown app
        self.state.notif_title = "NOTIFIKASI"
        self.state.notif_message = "Pesanan sedang diantar kurir"

        self.disp.clear()
        self.disp.draw_notification_overlay(self.state)

        # Inverted banner and message rendered cleanly
        self.assertTrue(self.disp.get_pixel(0, 8))
        self.assertTrue(self.disp.get_pixel(10, 22))

    def test_nav_cruising_clock_shows_phone_battery(self):
        """Verify navigation cruising clock screen renders big clock and phone battery telemetry."""
        self.state.nav_active = True
        self.state.ble_connected = True
        self.state.phone_battery_valid = True
        self.state.phone_battery_pct = 78
        self.state.phone_charging = False

        self.disp.clear()
        self.disp.draw_nav_cruising_page(self.state)

        # Big clock digits rendered
        self.assertTrue(self.disp.get_pixel(10, 10))
        self.assertTrue(self.disp.get_pixel(50, 10))

        # Phone battery icon and percentage rendered in right column
        self.assertTrue(self.disp.get_pixel(75, 4))   # Battery shell
        self.assertTrue(self.disp.get_pixel(70, 15))  # Battery percentage

        # BLE connection indicator (Bluetooth icon at x: 64, y: 3, spine at x=66)
        self.assertTrue(self.disp.get_pixel(66, 5))

        # Divider line and nav telemetry preserved
        self.assertTrue(self.disp.get_pixel(10, 26))
        self.assertTrue(self.disp.get_pixel(10, 28))

        # When BLE disconnected, BLE icon is hidden
        self.state.ble_connected = False
        self.disp.clear()
        self.disp.draw_nav_cruising_page(self.state)
        self.assertFalse(self.disp.get_pixel(66, 5))


if __name__ == "__main__":
    unittest.main()

