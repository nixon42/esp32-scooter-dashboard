#!/usr/bin/env bash
# ==============================================================================
# ESP32 Scooter Dashboard E2E Test Suite Runner
# Progressive & Multi-Tier Execution Harness
# ==============================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Ensure tests/e2e is on PYTHONPATH
export PYTHONPATH="${SCRIPT_DIR}:${PYTHONPATH:-}"

# Resolve Python interpreter (prefer Pyenv Python 3.11 if available)
PYENV_PY="/home/nixon/.pyenv/versions/3.11.14/bin/python3"
if [ -x "${PYENV_PY}" ]; then
    PYTHON_CMD="${PYENV_PY}"
else
    PYTHON_CMD="python3"
fi

echo "=============================================================================="
echo "ESP32 Scooter Dashboard - E2E Master Test Runner"
echo "Working Directory: ${PROJECT_ROOT}"
echo "Python Executable: $(${PYTHON_CMD} --version) (${PYTHON_CMD})"
echo "=============================================================================="

MODE="${1:---all}"
FILES_TO_RUN=()

case "${MODE}" in
    --m1)
        echo "Running Milestone M1 (Toolchain, Isolation, Partitions) Suite..."
        FILES_TO_RUN=("test_toolchain_and_partitions.py")
        ;;
    --m2)
        echo "Running Milestone M2 (PCD8544 Display, LEDC Backlight, UI) Suite..."
        FILES_TO_RUN=("test_display_and_ui.py")
        ;;
    --m3)
        echo "Running Milestone M3 (NimBLE Chronos Protocol Decoders) Suite..."
        FILES_TO_RUN=("test_chronos_protocol_decoders.py")
        ;;
    --m4)
        echo "Running Milestone M4 (Sensors, Voltmeter, Speedometer, DHT, LDR) Suite..."
        FILES_TO_RUN=("test_sensors_and_fallback.py")
        ;;
    --m5)
        echo "Running Milestone M5 (SoftAP, Web Portal, REST API, Web OTA) Suite..."
        FILES_TO_RUN=("test_web_portal_and_ota.py")
        ;;
    --m6)
        echo "Running Milestone M6 (System Integration, Concurrency, Build) Suite..."
        FILES_TO_RUN=("test_system_integration_and_build.py")
        ;;
    --tier1)
        echo "Running Tier 1 (Feature Coverage F01-F24) Suites..."
        FILES_TO_RUN=(
            "test_toolchain_and_partitions.py"
            "test_display_and_ui.py"
            "test_chronos_protocol_decoders.py"
            "test_sensors_and_fallback.py"
            "test_web_portal_and_ota.py"
            "test_system_integration_and_build.py"
        )
        ;;
    --tier2)
        echo "Running Tier 2 (Boundary & Corner Cases) Suite..."
        FILES_TO_RUN=("test_tier2_boundaries_and_corners.py")
        ;;
    --tier3)
        echo "Running Tier 3 (Cross-Feature Combinatorial Interactions) Suite..."
        FILES_TO_RUN=("test_tier3_cross_feature_interactions.py")
        ;;
    --tier4)
        echo "Running Tier 4 (Real-World Application Scenarios) Suite..."
        FILES_TO_RUN=("test_tier4_real_world_scenarios.py")
        ;;
    --all|*)
        echo "Running ALL Test Suites (Tiers 1, 2, 3, and 4)..."
        FILES_TO_RUN=(
            "test_toolchain_and_partitions.py"
            "test_display_and_ui.py"
            "test_chronos_protocol_decoders.py"
            "test_sensors_and_fallback.py"
            "test_web_portal_and_ota.py"
            "test_system_integration_and_build.py"
            "test_tier2_boundaries_and_corners.py"
            "test_tier3_cross_feature_interactions.py"
            "test_tier4_real_world_scenarios.py"
        )
        ;;
esac

TOTAL_FAILURES=0
TOTAL_TESTS=0

echo ""
printf "%-45s | %-10s\n" "Test Suite" "Status"
echo "---------------------------------------------------------------"

for test_file in "${FILES_TO_RUN[@]}"; do
    test_path="${SCRIPT_DIR}/${test_file}"
    if [ ! -f "${test_path}" ]; then
        printf "%-45s | %-10s\n" "${test_file}" "MISSING"
        TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
        continue
    fi

    # Run unittest via discovery of the specific file
    output=$(${PYTHON_CMD} -m unittest discover -s "${SCRIPT_DIR}" -p "${test_file}" 2>&1)
    exit_code=$?

    if [ ${exit_code} -eq 0 ]; then
        printf "%-45s | %-10s\n" "${test_file}" "PASSED"
    else
        printf "%-45s | %-10s\n" "${test_file}" "FAILED"
        echo "${output}"
        TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
    fi
done

echo "---------------------------------------------------------------"
if [ ${TOTAL_FAILURES} -eq 0 ]; then
    echo ">> ALL REQUESTED TEST SUITES COMPLETED SUCCESSFULLY! <<"
    exit 0
else
    echo ">> TEST RUN FINISHED WITH ${TOTAL_FAILURES} FAILING SUITE(S) <<"
    exit 1
fi
