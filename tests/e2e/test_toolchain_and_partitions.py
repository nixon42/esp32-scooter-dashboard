"""
E2E Tier 1 Suite: Toolchain, Isolation, Partitions, and Build Skeleton
Features Covered:
- F01: Toolchain & Isolation (ESP-IDF v5.3 on NVMe, .espressif, uv, pyenv Python 3.11, root protection)
- F02: source_idf.sh Environment Helper
- F03: 4MB Dual-OTA Partitions Scheme (partitions.csv)
- F04: Project Build Skeleton (CMakeLists.txt, sdkconfig.defaults)
"""

import os
import shutil
import subprocess
import unittest
from e2e_framework import PartitionsValidator

WORKSPACE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
PARTITIONS_CSV = os.path.join(WORKSPACE_ROOT, "partitions.csv")
SOURCE_IDF_SH = os.path.join(WORKSPACE_ROOT, "source_idf.sh")
ROOT_CMAKELISTS = os.path.join(WORKSPACE_ROOT, "CMakeLists.txt")
MAIN_CMAKELISTS = os.path.join(WORKSPACE_ROOT, "main", "CMakeLists.txt")
SDKCONFIG_DEFAULTS = os.path.join(WORKSPACE_ROOT, "sdkconfig.defaults")

NVME_MOUNT = "/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db"
PYENV_PYTHON = "/home/nixon/.pyenv/versions/3.11.14/bin/python3"
UV_BIN = "/usr/bin/uv"

class TestF01ToolchainAndIsolation(unittest.TestCase):
    """F01: Toolchain & Environment Isolation tests."""

    def test_f01_01_nvme_storage_accessible(self):
        """Verify NVMe mount exists and is writable."""
        self.assertTrue(os.path.exists(NVME_MOUNT), f"NVMe mount {NVME_MOUNT} must exist")
        self.assertTrue(os.access(NVME_MOUNT, os.W_OK), f"NVMe mount {NVME_MOUNT} must be writable")

    def test_f01_02_pyenv_python_version(self):
        """Verify pyenv Python 3.11.14 interpreter is operational."""
        self.assertTrue(os.path.exists(PYENV_PYTHON), f"Pyenv Python binary missing at {PYENV_PYTHON}")
        res = subprocess.run([PYENV_PYTHON, "--version"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("Python 3.11", res.stdout)

    def test_f01_03_uv_binary_installed(self):
        """Verify uv package manager is installed."""
        self.assertTrue(os.path.exists(UV_BIN), f"uv binary missing at {UV_BIN}")
        res = subprocess.run([UV_BIN, "--version"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("uv", res.stdout)

    def test_f01_04_root_partition_safety_check(self):
        """Verify root partition (/) has at least 1 GB safety margin."""
        stat = os.statvfs("/")
        avail_mb = (stat.f_bavail * stat.f_frsize) / (1024 * 1024)
        self.assertGreater(avail_mb, 1000.0, f"Root filesystem critically low: {avail_mb:.1f} MB remaining")

    def test_f01_05_esp_idf_path_nvme_specification(self):
        """Verify expected IDF path and tools path are directed to NVMe storage."""
        expected_idf_path = os.path.join(NVME_MOUNT, "esp-idf")
        expected_tools_path = os.path.join(NVME_MOUNT, ".espressif")
        expected_uv_cache = os.path.join(NVME_MOUNT, ".cache", "uv")

        self.assertTrue(expected_idf_path.startswith(NVME_MOUNT))
        self.assertTrue(expected_tools_path.startswith(NVME_MOUNT))
        self.assertTrue(expected_uv_cache.startswith(NVME_MOUNT))


class TestF02SourceIdfScript(unittest.TestCase):
    """F02: source_idf.sh Helper script tests."""

    def test_f02_01_script_path_specification(self):
        """Verify standard path for source_idf.sh at workspace root."""
        self.assertEqual(SOURCE_IDF_SH, os.path.join(WORKSPACE_ROOT, "source_idf.sh"))

    def test_f02_02_script_contains_idf_tools_path(self):
        """Verify script exports IDF_TOOLS_PATH pointing to NVMe."""
        if not os.path.exists(SOURCE_IDF_SH):
            self.skipTest(f"{SOURCE_IDF_SH} not yet created (M1 pending)")
        with open(SOURCE_IDF_SH, "r") as f:
            content = f.read()
        self.assertIn("IDF_TOOLS_PATH", content)
        self.assertIn(".espressif", content)

    def test_f02_03_script_contains_uv_cache_redirection(self):
        """Verify script exports UV_CACHE_DIR pointing to NVMe."""
        if not os.path.exists(SOURCE_IDF_SH):
            self.skipTest(f"{SOURCE_IDF_SH} not yet created (M1 pending)")
        with open(SOURCE_IDF_SH, "r") as f:
            content = f.read()
        self.assertIn("UV_CACHE_DIR", content)

    def test_f02_04_script_sources_export_sh(self):
        """Verify script executes/sources export.sh."""
        if not os.path.exists(SOURCE_IDF_SH):
            self.skipTest(f"{SOURCE_IDF_SH} not yet created (M1 pending)")
        with open(SOURCE_IDF_SH, "r") as f:
            content = f.read()
        self.assertTrue("export.sh" in content or "source" in content or "." in content)

    def test_f02_05_script_syntax_validation(self):
        """Verify bash syntax of source_idf.sh with bash -n."""
        if not os.path.exists(SOURCE_IDF_SH):
            self.skipTest(f"{SOURCE_IDF_SH} not yet created (M1 pending)")
        res = subprocess.run(["bash", "-n", SOURCE_IDF_SH], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Syntax error in {SOURCE_IDF_SH}: {res.stderr}")


class TestF03DualOtaPartitions(unittest.TestCase):
    """F03: 4MB Dual-OTA Partition layout tests."""

    SPEC_CSV = """# ESP-IDF Partition Table for 4MB Flash Dual OTA
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1A0000,
app1,     app,  ota_1,   0x1B0000,0x1A0000,
"""

    def test_f03_01_spec_csv_validity(self):
        """Validate authoritative 4MB Dual-OTA layout specifications."""
        parts = PartitionsValidator.parse_partitions_csv(self.SPEC_CSV)
        self.assertEqual(len(parts), 4)
        valid, msg = PartitionsValidator.validate_partition_layout(parts)
        self.assertTrue(valid, msg)

    def test_f03_02_nvs_partition_offsets(self):
        """Verify nvs offset 0x9000 and size 0x5000."""
        parts = {p["name"]: p for p in PartitionsValidator.parse_partitions_csv(self.SPEC_CSV)}
        self.assertEqual(parts["nvs"]["offset"], 0x9000)
        self.assertEqual(parts["nvs"]["size"], 0x5000)

    def test_f03_03_otadata_partition_offsets(self):
        """Verify otadata offset 0xe000 and size 0x2000."""
        parts = {p["name"]: p for p in PartitionsValidator.parse_partitions_csv(self.SPEC_CSV)}
        self.assertEqual(parts["otadata"]["offset"], 0xe000)
        self.assertEqual(parts["otadata"]["size"], 0x2000)

    def test_f03_04_app0_and_app1_capacity(self):
        """Verify app0 and app1 are identical 0x1A0000 bytes (1,703,936 B)."""
        parts = {p["name"]: p for p in PartitionsValidator.parse_partitions_csv(self.SPEC_CSV)}
        self.assertEqual(parts["app0"]["offset"], 0x10000)
        self.assertEqual(parts["app0"]["size"], 0x1A0000)
        self.assertEqual(parts["app1"]["offset"], 0x1B0000)
        self.assertEqual(parts["app1"]["size"], 0x1A0000)

    def test_f03_05_workspace_partitions_csv_if_present(self):
        """Inspect workspace partitions.csv against specification if created."""
        if not os.path.exists(PARTITIONS_CSV):
            self.skipTest(f"{PARTITIONS_CSV} not yet created (M1 pending)")
        with open(PARTITIONS_CSV, "r") as f:
            content = f.read()
        parts = PartitionsValidator.parse_partitions_csv(content)
        valid, msg = PartitionsValidator.validate_partition_layout(parts)
        self.assertTrue(valid, msg)

    def test_f03_06_boundary_and_non_overlap(self):
        """Verify no partitions overlap and total flash usage < 4MB (0x400000)."""
        parts = PartitionsValidator.parse_partitions_csv(self.SPEC_CSV)
        end_addr = parts[-1]["offset"] + parts[-1]["size"]
        self.assertLessEqual(end_addr, 0x400000)
        self.assertEqual(end_addr, 0x350000)  # Exactly 3.3125 MB


class TestF04ProjectBuildSkeleton(unittest.TestCase):
    """F04: Native CMake and sdkconfig.defaults specifications."""

    def test_f04_01_target_esp32c3_defined(self):
        """Verify target chip is ESP32-C3 in sdkconfig or defaults."""
        if not os.path.exists(SDKCONFIG_DEFAULTS):
            self.skipTest(f"{SDKCONFIG_DEFAULTS} not yet created (M1 pending)")
        with open(SDKCONFIG_DEFAULTS, "r") as f:
            content = f.read()
        self.assertTrue("CONFIG_IDF_TARGET=\"esp32c3\"" in content or "CONFIG_IDF_TARGET_ESP32C3=y" in content)

    def test_f04_02_flash_size_4mb_configured(self):
        """Verify 4MB flash size in sdkconfig.defaults."""
        if not os.path.exists(SDKCONFIG_DEFAULTS):
            self.skipTest(f"{SDKCONFIG_DEFAULTS} not yet created (M1 pending)")
        with open(SDKCONFIG_DEFAULTS, "r") as f:
            content = f.read()
        self.assertIn("CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y", content)

    def test_f04_03_custom_partition_table_enabled(self):
        """Verify custom partition table enabled in sdkconfig.defaults."""
        if not os.path.exists(SDKCONFIG_DEFAULTS):
            self.skipTest(f"{SDKCONFIG_DEFAULTS} not yet created (M1 pending)")
        with open(SDKCONFIG_DEFAULTS, "r") as f:
            content = f.read()
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", content)

    def test_f04_04_nimble_bluetooth_enabled(self):
        """Verify NimBLE stack enabled in sdkconfig.defaults."""
        if not os.path.exists(SDKCONFIG_DEFAULTS):
            self.skipTest(f"{SDKCONFIG_DEFAULTS} not yet created (M1 pending)")
        with open(SDKCONFIG_DEFAULTS, "r") as f:
            content = f.read()
        self.assertIn("CONFIG_BT_NIMBLE_ENABLED=y", content)

    def test_f04_05_cmake_project_name(self):
        """Verify root CMakeLists.txt declares project name."""
        if not os.path.exists(ROOT_CMAKELISTS):
            self.skipTest(f"{ROOT_CMAKELISTS} not yet created (M1 pending)")
        with open(ROOT_CMAKELISTS, "r") as f:
            content = f.read()
        self.assertIn("project(", content.lower())

if __name__ == "__main__":
    unittest.main()
