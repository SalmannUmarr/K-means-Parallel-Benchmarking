#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "powermetrics is only available on macOS. Power metrics will be N/A."
    exit 0
fi

if ! command -v powermetrics >/dev/null 2>&1; then
    echo "powermetrics was not found. It is included with macOS on Apple Silicon."
    exit 1
fi

echo "powermetrics found: $(command -v powermetrics)"
echo "Refreshing sudo credentials for benchmark power sampling..."
sudo -v

echo "Taking one lightweight validation sample..."
VALIDATION_FILE="$(mktemp /tmp/pdc_powermetrics_validation_XXXXXX.txt)"
cleanup() {
    if [[ -n "${VALIDATION_FILE:-}" && -f "$VALIDATION_FILE" ]]; then
        rm -f "$VALIDATION_FILE" 2>/dev/null || true
    fi
}
trap cleanup EXIT

sudo powermetrics --samplers cpu_power -n 1 -i 500 >"$VALIDATION_FILE"

echo "Power metrics support is ready."
echo "Run benchmarks with:"
echo "  ENABLE_POWER_METRICS=1 src/Serial_Baseline/run_serial_benchmarks.sh"
