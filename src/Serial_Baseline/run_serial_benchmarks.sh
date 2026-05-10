#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BINARY="${SCRIPT_DIR}/serial_kmeans"
RESULTS_DIR="${PROJECT_ROOT}/results"
MAX_ITERATIONS="${MAX_ITERATIONS:-20}"
CSV_PATH="${RESULTS_DIR}/Serial_Baseline_Results.csv"
TXT_PATH="${RESULTS_DIR}/Serial_Baseline_Results.txt"
export ENABLE_POWER_METRICS="${ENABLE_POWER_METRICS:-1}"

mkdir -p "${RESULTS_DIR}"
rm -f "${CSV_PATH}" "${TXT_PATH}"

if [[ ! -x "${BINARY}" ]]; then
    make -C "${SCRIPT_DIR}"
fi

datasets=(
    "tier1_small.csv"
    "tier2_medium.csv"
    "tier3_large.csv"
)

for dataset in "${datasets[@]}"; do
    dataset_path="${PROJECT_ROOT}/data/${dataset}"

    echo "Running Serial Baseline on ${dataset}..."
    "${BINARY}" "${dataset_path}" 0 "${MAX_ITERATIONS}" "${CSV_PATH}" "${TXT_PATH}"
    echo
done

echo "Aggregate CSV saved to ${CSV_PATH}"
echo "Aggregate text report saved to ${TXT_PATH}"
