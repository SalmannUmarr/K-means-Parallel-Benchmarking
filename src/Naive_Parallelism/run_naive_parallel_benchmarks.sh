#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BINARY="${SCRIPT_DIR}/naive_parallel_kmeans"
RESULTS_DIR="${PROJECT_ROOT}/results"
MAX_ITERATIONS="${MAX_ITERATIONS:-20}"
CSV_PATH="${RESULTS_DIR}/Naive_Parallelism_Results.csv"
TXT_PATH="${RESULTS_DIR}/Naive_Parallelism_Results.txt"
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

threads=(
    "1"
    "2"
    "4"
    "8"
)

for dataset in "${datasets[@]}"; do
    dataset_path="${PROJECT_ROOT}/data/${dataset}"
    for thread_count in "${threads[@]}"; do
        echo "Running Naive Parallelism on ${dataset} with ${thread_count} thread(s)..."
        "${BINARY}" "${dataset_path}" "${thread_count}" 0 "${MAX_ITERATIONS}" "${CSV_PATH}" "${TXT_PATH}"
        echo
    done
done

echo "Aggregate CSV saved to ${CSV_PATH}"
echo "Aggregate text report saved to ${TXT_PATH}"
