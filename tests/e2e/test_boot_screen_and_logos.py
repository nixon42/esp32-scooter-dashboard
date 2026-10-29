"""
E2E Test Suite: Boot Animation, Selectable Boot Logos & Real-Time Linux Boot Screen
Covers:
- F24: Selectable Boot Logos (Vario, Android, Espressif, Linux Tux, Arch Linux)
- F25: NVS Persistence & Web Portal Configuration for Boot Logo
- F26: Real-Time Linux Dmesg Console Line Rolling & Screen Buffer Behavior
"""

import unittest
from e2e_framework import HudState, BootLogo, BootConsole, WebPortalServer

class TestBootLogoAndScreen(unittest.TestCase):
    """Test boot screen logo selections, console scrolling, and REST API."""

    def setUp(self):
        self.state = HudState()
        self.server = WebPortalServer(self.state)

    def test_boot_logo_enum_definitions(self):
        """Verify all requested boot logos exist with consistent enum mappings."""
        self.assertEqual(BootLogo.VARIO, 0)
        self.assertEqual(BootLogo.ANDROID, 1)
        self.assertEqual(BootLogo.ESPRESSIF, 2)
        self.assertEqual(BootLogo.LINUX, 3)
        self.assertEqual(BootLogo.ARCH, 4)
        self.assertEqual(BootLogo.MAX, 5)

    def test_default_boot_logo(self):
        """Verify default boot logo in state is Vario Wing."""
        self.assertEqual(self.state.boot_logo, BootLogo.VARIO)

    def test_web_config_get_includes_boot_logo(self):
        """Verify GET /api/config includes current boot_logo."""
        cfg = self.server.handle_get_config()
        self.assertIn("boot_logo", cfg)
        self.assertEqual(cfg["boot_logo"], BootLogo.VARIO)

    def test_web_config_post_valid_logos(self):
        """Verify user can switch to Android, Espressif, Linux Tux, and Arch Linux via web portal."""
        # Select Android (1)
        code, body = self.server.handle_post_config({"boot_logo": BootLogo.ANDROID})
        self.assertEqual(code, 200)
        self.assertEqual(self.state.boot_logo, BootLogo.ANDROID)

        # Select Espressif (2)
        code, body = self.server.handle_post_config({"boot_logo": BootLogo.ESPRESSIF})
        self.assertEqual(code, 200)
        self.assertEqual(self.state.boot_logo, BootLogo.ESPRESSIF)

        # Select Linux Tux (3)
        code, body = self.server.handle_post_config({"boot_logo": BootLogo.LINUX})
        self.assertEqual(code, 200)
        self.assertEqual(self.state.boot_logo, BootLogo.LINUX)

        # Select Arch Linux (4)
        code, body = self.server.handle_post_config({"boot_logo": BootLogo.ARCH})
        self.assertEqual(code, 200)
        self.assertEqual(self.state.boot_logo, BootLogo.ARCH)

        # Select Vario Wing (0)
        code, body = self.server.handle_post_config({"boot_logo": BootLogo.VARIO})
        self.assertEqual(code, 200)
        self.assertEqual(self.state.boot_logo, BootLogo.VARIO)

    def test_web_config_post_invalid_logo_rejected(self):
        """Verify out-of-bounds logo IDs (e.g. 5, 99, -1) return 400 Bad Request."""
        code, body = self.server.handle_post_config({"boot_logo": 99})
        self.assertEqual(code, 400)
        self.assertIn("error", body)

        code, body = self.server.handle_post_config({"boot_logo": -1})
        self.assertEqual(code, 400)

        code, body = self.server.handle_post_config({"boot_logo": BootLogo.MAX})
        self.assertEqual(code, 400)

    def test_boot_console_realtime_line_shifting(self):
        """Verify real-time Linux-style boot console shifts lines upon new hardware logs."""
        console = BootConsole(BootLogo.LINUX)
        self.assertEqual(len(console.lines), 0)

        # Step 1: NVS mounted
        console.log("[ OK ] Mounted NVS")
        self.assertEqual(len(console.lines), 1)
        self.assertEqual(console.lines[0], "[ OK ] Mounted NVS")

        # Step 2: Netif
        console.log("[ OK ] Netif & event")
        self.assertEqual(len(console.lines), 2)
        self.assertEqual(console.lines[1], "[ OK ] Netif & event")

        # Step 3: Sensors
        console.log("[ OK ] Hardware sen")
        self.assertEqual(len(console.lines), 3)
        self.assertEqual(console.lines[0], "[ OK ] Mounted NVS")
        self.assertEqual(console.lines[2], "[ OK ] Hardware sen")

        # Step 4: BLE NimBLE (causes oldest line to shift out)
        console.log("[ OK ] NimBLE stack")
        self.assertEqual(len(console.lines), 3)
        self.assertEqual(console.lines[0], "[ OK ] Netif & event")
        self.assertEqual(console.lines[1], "[ OK ] Hardware sen")
        self.assertEqual(console.lines[2], "[ OK ] NimBLE stack")

        # Step 5: Web Portal
        console.log("[ OK ] Web Portal AP")
        self.assertEqual(console.lines[0], "[ OK ] Hardware sen")
        self.assertEqual(console.lines[1], "[ OK ] NimBLE stack")
        self.assertEqual(console.lines[2], "[ OK ] Web Portal AP")

        # Step 6: System ready
        console.log("[ OK ] System Ready")
        self.assertEqual(console.lines[0], "[ OK ] NimBLE stack")
        self.assertEqual(console.lines[1], "[ OK ] Web Portal AP")
        self.assertEqual(console.lines[2], "[ OK ] System Ready")

if __name__ == "__main__":
    unittest.main()
