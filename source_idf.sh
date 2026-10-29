#!/usr/bin/env bash
# source_idf.sh - Load ESP-IDF v5.3 environment for ESP32 Scooter Dashboard
# Target: ESP32-C3 SuperMini (RISC-V)
# Usage: source source_idf.sh  OR  . source_idf.sh

# 1. Guard against direct execution (must be sourced to export variables to caller shell)
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    echo "[ERROR] This script must be sourced, not executed!" >&2
    echo "Usage: source source_idf.sh  OR  . source_idf.sh" >&2
    exit 1
fi

# 2. Strict redirection of tools and caches to NVMe storage
export IDF_TOOLS_PATH="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif"
export UV_CACHE_DIR="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.cache/uv"
export PIP_CACHE_DIR="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.cache/pip"
export IDF_COMPONENT_CACHE_PATH="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.cache/esp_component_manager"
export CCACHE_DIR="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.cache/ccache"
export ESP_IDF_USE_UV=1

# 3. Enforce pyenv Python 3.11 as base interpreter (bypasses Arch Linux Python 3.14)
if [[ ":$PATH:" != *":/home/nixon/.pyenv/versions/3.11.14/bin:"* ]]; then
    export PATH="/home/nixon/.pyenv/versions/3.11.14/bin:$PATH"
fi
export ESP_PYTHON="/home/nixon/.pyenv/versions/3.11.14/bin/python3"

# 4. Set IDF_PATH and verify export.sh exists
export IDF_PATH="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf"

if [ ! -f "${IDF_PATH}/export.sh" ]; then
    echo "[ERROR] ESP-IDF export script not found at ${IDF_PATH}/export.sh" >&2
    echo "Please ensure ESP-IDF v5.3 is cloned and installed in ${IDF_PATH}" >&2
    return 1
fi

# 5. Source ESP-IDF official export.sh
. "${IDF_PATH}/export.sh"

echo "[OK] ESP-IDF v5.3 environment loaded successfully for ESP32 Scooter Dashboard (ESP32-C3)."
