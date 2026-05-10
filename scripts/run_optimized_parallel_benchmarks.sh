#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${PROJECT_ROOT}/src/Parallelism/run_optimized_parallel_benchmarks.sh"
