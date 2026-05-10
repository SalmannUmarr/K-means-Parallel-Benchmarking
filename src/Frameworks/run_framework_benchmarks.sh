#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PYTHON="${PROJECT_ROOT}/env/bin/python"
export ENABLE_POWER_METRICS="${ENABLE_POWER_METRICS:-1}"
if [[ -z "${LOKY_MAX_CPU_COUNT:-}" ]]; then
    LOKY_COUNT="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
    export LOKY_MAX_CPU_COUNT="${LOKY_COUNT:-8}"
fi
export LOKY_PHYSICAL_CORES="${LOKY_PHYSICAL_CORES:-${LOKY_MAX_CPU_COUNT}}"

echo "Starting Scikit-Learn framework benchmark..."
"${PYTHON}" -u "${SCRIPT_DIR}/scikit_kmeans_benchmark.py"

echo "Starting PyTorch framework benchmark..."
"${PYTHON}" -u "${SCRIPT_DIR}/pytorch_kmeans_benchmark.py"

echo "Framework benchmark results saved to:"
echo "  ${PROJECT_ROOT}/results/Scikit_KMeans_Results.txt"
echo "  ${PROJECT_ROOT}/results/Scikit_KMeans_Results.csv"
echo "  ${PROJECT_ROOT}/results/PyTorch_KMeans_Results.txt"
echo "  ${PROJECT_ROOT}/results/PyTorch_KMeans_Results.csv"
