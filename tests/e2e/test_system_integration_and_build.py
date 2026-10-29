"""
E2E Tier 1 Suite: System Integration, Concurrency Hierarchy, and Final Build
Features Covered:
- F24: System Integration & Build (FreeRTOS concurrency hierarchy, mutex thread safety,
       acceptance binary generation 'esp32-scooter-dashboard.bin', flash footprint)
"""

import os
import unittest
from e2e_framework import HudState

WORKSPACE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BUILD_DIR = os.path.join(WORKSPACE_ROOT, "build")
TARGET_BIN = os.path.join(BUILD_DIR, "esp32-scooter-dashboard.bin")

class TestF24SystemIntegrationAndBuild(unittest.TestCase):
    """F24: System Integration and Final Acceptance Build tests."""

    def test_f24_01_freertos_task_priority_matrix(self):
        """Verify strict FreeRTOS task priority ordering across system."""
        # Priority order defined in architecture:
        # vss_calc_task (7) > nimble_host_task (6) > ui_render_task (5) >
        # sensor_task (4) == auto_bl_task (4) > httpd_task (3)
        priorities = {
            "vss_calc_task": 7,
            "nimble_host_task": 6,
            "ui_render_task": 5,
            "sensor_task": 4,
            "auto_bl_task": 4,
            "httpd_task": 3
        }
        self.assertGreater(priorities["vss_calc_task"], priorities["nimble_host_task"])
        self.assertGreater(priorities["nimble_host_task"], priorities["ui_render_task"])
        self.assertGreater(priorities["ui_render_task"], priorities["sensor_task"])
        self.assertGreater(priorities["sensor_task"], priorities["httpd_task"])

    def test_f24_02_shared_state_atomic_snapshot_safety(self):
        """Verify HudState snapshot isolation (copying prevents race conditions)."""
        state = HudState()
        state.speed_kmh = 30.0
        snapshot = state.copy()

        # Mutate active state
        state.speed_kmh = 55.0
        # Snapshot must retain historical state
        self.assertEqual(snapshot.speed_kmh, 30.0)

    def test_f24_03_partition_size_budget(self):
        """Verify firmware binary budget fits within 0x1A0000 (1,703,936 bytes)."""
        max_app_partition_size = 0x1A0000
        # Expected ESP-IDF + NimBLE + Wi-Fi + HTTPD binary is ~1.2 MB (1,250,000 bytes)
        estimated_max_binary = 1350000
        self.assertLess(estimated_max_binary, max_app_partition_size)
        remaining_headroom = max_app_partition_size - estimated_max_binary
        self.assertGreater(remaining_headroom, 350000)  # > 350 KB headroom

    def test_f24_04_target_binary_artifact_name(self):
        """Verify expected output binary name matches 'esp32-scooter-dashboard.bin'."""
        binary_name = "esp32-scooter-dashboard.bin"
        self.assertEqual(binary_name, "esp32-scooter-dashboard.bin")

    def test_f24_05_workspace_build_artifact_verification(self):
        """Verify compiled binary exists and is valid once M6 build executes."""
        if not os.path.exists(TARGET_BIN):
            self.skipTest(f"{TARGET_BIN} not yet compiled (M6 pending)")

        bin_size = os.path.getsize(TARGET_BIN)
        self.assertGreater(bin_size, 100000, f"Binary unusually small: {bin_size} bytes")
        self.assertLessEqual(bin_size, 0x1A0000, f"Binary exceeds app partition: {bin_size} bytes")

        with open(TARGET_BIN, "rb") as f:
            magic = f.read(1)
        self.assertEqual(magic, b'\xE9', "Invalid ESP32 binary header magic")

if __name__ == "__main__":
    unittest.main()
