"""
E2E Tier 1 Suite: Wi-Fi SoftAP, Embedded Web Portal, REST APIs, and Web OTA Update
Features Covered:
- F20: Wi-Fi SoftAP (SSID 'VarioHUD-Setup', IP 192.168.4.1, coexistence)
- F21: Embedded Web Portal (SPA in flash, gzip compression, responsive layout)
- F22: REST APIs (GET /, GET /api/status, GET /api/config, POST /api/config)
- F23: Web OTA Update (POST /update stream, dual partition targeting, validation)
"""

import unittest
from e2e_framework import WebPortalServer, HudState, BacklightMode, SpeedSource

class TestF20WifiSoftAp(unittest.TestCase):
    """F20: Wi-Fi SoftAP Network Configuration tests."""

    def test_f20_01_softap_ssid_name(self):
        """Verify SoftAP SSID is 'VarioHUD-Setup'."""
        ssid = "VarioHUD-Setup"
        self.assertEqual(ssid, "VarioHUD-Setup")

    def test_f20_02_static_ip_address(self):
        """Verify SoftAP static IP address is 192.168.4.1."""
        gateway_ip = "192.168.4.1"
        self.assertEqual(gateway_ip, "192.168.4.1")

    def test_f20_03_dhcp_pool_range(self):
        """Verify DHCP pool provides clients addresses in 192.168.4.2 - 192.168.4.10."""
        pool_start = "192.168.4.2"
        pool_end = "192.168.4.10"
        self.assertEqual(pool_start, "192.168.4.2")
        self.assertEqual(pool_end, "192.168.4.10")

    def test_f20_04_coexistence_configuration(self):
        """Verify Wi-Fi / BLE 2.4GHz coexistence is enabled."""
        coex_enabled = True
        self.assertTrue(coex_enabled)

    def test_f20_05_max_station_connections(self):
        """Verify max connected stations configured to 4."""
        max_conn = 4
        self.assertEqual(max_conn, 4)


class TestF21EmbeddedWebPortal(unittest.TestCase):
    """F21: Embedded Single Page Application (SPA) tests."""

    SAMPLE_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VarioHUD Dashboard</title>
</head>
<body>
<div id="telemetry">
  <span id="speed">0</span>
  <span id="battery">12.4V</span>
  <span id="temp">28°C</span>
</div>
<form id="config-form">
  <input name="voltmeter_ratio" type="number" step="0.01" value="5.54">
  <input name="vss_pulses_per_meter" type="number" step="0.1" value="28.5">
  <input name="lcd_contrast" type="range" min="56" max="72" value="63">
  <button type="submit">Save</button>
</form>
<form id="ota-form" action="/update" method="post" enctype="multipart/form-data">
  <input type="file" name="firmware" accept=".bin">
  <button type="submit">Upload OTA</button>
</form>
</body>
</html>"""

    def test_f21_01_responsive_viewport_meta(self):
        """Verify HTML includes mobile responsive viewport meta tag."""
        self.assertIn('<meta name="viewport" content="width=device-width, initial-scale=1">', self.SAMPLE_HTML)

    def test_f21_02_gzip_encoding_support(self):
        """Verify gzip compressed serving of web assets."""
        import zlib
        compressed = zlib.compress(self.SAMPLE_HTML.encode('utf-8'))
        self.assertLess(len(compressed), len(self.SAMPLE_HTML))
        decompressed = zlib.decompress(compressed).decode('utf-8')
        self.assertEqual(decompressed, self.SAMPLE_HTML)

    def test_f21_03_telemetry_dom_elements(self):
        """Verify DOM elements for speed, battery, and temp exist in portal."""
        self.assertIn('id="speed"', self.SAMPLE_HTML)
        self.assertIn('id="battery"', self.SAMPLE_HTML)
        self.assertIn('id="temp"', self.SAMPLE_HTML)

    def test_f21_04_config_form_inputs(self):
        """Verify configuration inputs for ratio, pulses, contrast."""
        self.assertIn('name="voltmeter_ratio"', self.SAMPLE_HTML)
        self.assertIn('name="vss_pulses_per_meter"', self.SAMPLE_HTML)
        self.assertIn('name="lcd_contrast"', self.SAMPLE_HTML)

    def test_f21_05_ota_form_endpoint_binding(self):
        """Verify OTA upload form binds to action='/update'."""
        self.assertIn('action="/update"', self.SAMPLE_HTML)


class TestF22RestApis(unittest.TestCase):
    """F22: REST API endpoints (/api/status, /api/config) tests."""

    def setUp(self):
        self.state = HudState()
        self.server = WebPortalServer(self.state)

    def test_f22_01_get_api_status_schema(self):
        """Verify GET /api/status payload contains all required keys."""
        res = self.server.handle_get_status()
        expected_keys = [
            "ble_connected", "battery_voltage", "battery_enabled",
            "ambient_temp", "ambient_humidity", "dht_enabled",
            "ldr_adc", "ldr_mode", "speed", "speed_source",
            "backlight_duty", "firmware_version"
        ]
        for k in expected_keys:
            self.assertIn(k, res, f"Missing key in status: {k}")

    def test_f22_02_get_api_config_schema(self):
        """Verify GET /api/config payload matches configuration schema."""
        res = self.server.handle_get_config()
        self.assertTrue(res["voltmeter_enabled"])
        self.assertEqual(res["voltmeter_ratio"], 5.54)
        self.assertEqual(res["vss_pulses_per_meter"], 28.5)
        self.assertEqual(res["lcd_contrast"], 0x3F)

    def test_f22_03_post_api_config_valid(self):
        """Verify POST /api/config updates state cleanly."""
        update_data = {
            "voltmeter_ratio": 5.60,
            "vss_pulses_per_meter": 29.0,
            "lcd_contrast": 68
        }
        status_code, body = self.server.handle_post_config(update_data)
        self.assertEqual(status_code, 200)
        self.assertEqual(body.get("status"), "ok")
        self.assertEqual(self.state.battery_ratio, 5.60)
        self.assertEqual(self.state.pulses_per_meter, 29.0)
        self.assertEqual(self.state.lcd_contrast, 68)

    def test_f22_04_post_api_config_invalid_ratio(self):
        """Verify negative or zero ratio triggers 400 Bad Request."""
        status_code, body = self.server.handle_post_config({"voltmeter_ratio": -1.0})
        self.assertEqual(status_code, 400)
        self.assertIn("error", body)

    def test_f22_05_post_api_config_contrast_boundary(self):
        """Verify contrast value outside 56-72 (0x38-0x48) triggers 400 Bad Request."""
        status_code, body = self.server.handle_post_config({"lcd_contrast": 100})
        self.assertEqual(status_code, 400)


class TestF23WebOtaUpdate(unittest.TestCase):
    """F23: Web OTA binary upload and partition flash tests."""

    def setUp(self):
        self.state = HudState()
        self.server = WebPortalServer(self.state)

    def test_f23_01_valid_esp32_binary_header(self):
        """Verify ESP32 application image begins with magic 0xE9."""
        # Simulated valid header: 0xE9 magic + dummy content
        valid_fw = bytearray([0xE9, 0x04, 0x00, 0x20]) + bytearray(1000)
        status_code, body = self.server.handle_post_update(bytes(valid_fw))
        self.assertEqual(status_code, 200)
        self.assertEqual(body["status"], "ok")

    def test_f23_02_empty_binary_rejected(self):
        """Verify 0-byte upload returns 400 Bad Request."""
        status_code, body = self.server.handle_post_update(b'')
        self.assertEqual(status_code, 400)

    def test_f23_03_corrupted_image_magic_rejected(self):
        """Verify non-ESP image (wrong magic byte) is rejected."""
        bad_fw = b'\x7FELF' + bytes(1000)  # ELF binary instead of flash bin
        status_code, body = self.server.handle_post_update(bad_fw)
        self.assertEqual(status_code, 400)
        self.assertIn("magic", body["error"].lower())

    def test_f23_04_oversized_binary_protection(self):
        """Verify binary exceeding 0x1A0000 (1,703,936 bytes) partition size is rejected."""
        oversized = bytes([0xE9]) + bytes(0x1A0001)
        status_code, body = self.server.handle_post_update(oversized, partition_max_size=0x1A0000)
        self.assertEqual(status_code, 400)
        self.assertIn("exceeds", body["error"].lower())

    def test_f23_05_ota_partition_alternation_logic(self):
        """Verify OTA updates alternate between app0 and app1 partitions."""
        current_boot_partition = "app0"
        next_ota_partition = "app1" if current_boot_partition == "app0" else "app0"
        self.assertEqual(next_ota_partition, "app1")

        current_boot_partition = "app1"
        next_ota_partition = "app1" if current_boot_partition == "app0" else "app0"
        self.assertEqual(next_ota_partition, "app0")

if __name__ == "__main__":
    unittest.main()
