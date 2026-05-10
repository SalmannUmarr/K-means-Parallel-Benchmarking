# PDC Project: K-Means Benchmarking System

This project implements and benchmarks multiple K-Means clustering versions for
a Parallel and Distributed Computing project. The benchmark system is designed
to produce consistent console, TXT, and CSV outputs so that every implementation
can be compared fairly in the final report.

## Scope

The project compares:

- Serial Baseline K-Means in C++
- Naive Parallelism K-Means in C++ using `std::thread`
- Optimized Parallelism K-Means in C++ tuned for Apple Silicon M2
- Scikit-Learn K-Means using `sklearn.cluster.KMeans`
- PyTorch K-Means using manual tensor-based K-Means operations

All benchmark versions use the same datasets, reporting format, result folders,
and maximum iteration cap.

## Key Benchmark Rules

- `MAX_ITERATIONS = 20` for every implementation.
- SSE / Final SSE is not calculated for runtime benchmarking.
- Output format is standardized across implementations.
- Results are saved as one TXT file and one CSV file per implementation.
- Power metrics are collected with macOS `powermetrics` where available.
- Benchmarks continue even if power metrics cannot be captured.

## Project Structure

```text
I220908_PDC_Project/
├── data/
│   ├── generate_datasets.py
│   ├── tier1_small.csv
│   ├── tier2_medium.csv
│   └── tier3_large.csv
├── results/
├── scripts/
│   ├── setup_power_metrics.sh
│   ├── run_serial_benchmarks.sh
│   ├── run_naive_parallel_benchmarks.sh
│   ├── run_optimized_parallel_benchmarks.sh
│   └── run_framework_benchmarks.sh
├── src/
│   ├── Serial_Baseline/
│   ├── Naive_Parallelism/
│   ├── Parallelism/
│   ├── Frameworks/
│   ├── utils/
│   └── common/
└── README.md
```

## Datasets

The benchmark uses three CSV datasets:

| Dataset | Tier | Shape | Clusters |
| --- | --- | ---: | ---: |
| `tier1_small.csv` | Tier 1 (Small) | 10,000 x 8 | 5 |
| `tier2_medium.csv` | Tier 2 (Medium) | 100,000 x 16 | 8 |
| `tier3_large.csv` | Tier 3 (Large) | 2,000,000 x 64 | 10 |

Regenerate datasets if needed:

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
python3 data/generate_datasets.py
```

## Power Metrics Setup

Apple Silicon power metrics use macOS `powermetrics`, which requires sudo.
Refresh sudo credentials once before running power-enabled benchmarks:

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
sudo -v
```

Optional validation:

```bash
scripts/setup_power_metrics.sh
```

Run benchmarks with power metrics enabled:

```bash
ENABLE_POWER_METRICS=1 <benchmark-command>
```

If `powermetrics` is unavailable or sudo credentials are not active, benchmarks
still run and report power fields as `N/A` or `Available via powermetrics
(requires sudo)`.

## Build C++ Implementations

Build all C++ benchmark versions:

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
make -C src/Serial_Baseline
make -C src/Naive_Parallelism
make -C src/Parallelism
```

## Run Serial Baseline

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
sudo -v
ENABLE_POWER_METRICS=1 src/Serial_Baseline/run_serial_benchmarks.sh
```

Outputs:

- `results/Serial_Baseline_Results.txt`
- `results/Serial_Baseline_Results.csv`

## Run Naive Parallelism

The Naive Parallelism version runs each tier with 1, 2, 4, and 8 threads.

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
sudo -v
ENABLE_POWER_METRICS=1 src/Naive_Parallelism/run_naive_parallel_benchmarks.sh
```

Outputs:

- `results/Naive_Parallelism_Results.txt`
- `results/Naive_Parallelism_Results.csv`

## Run Optimized Parallelism

The optimized version is tuned for Apple Silicon M2 and runs each tier with 1,
2, 4, and 8 threads.

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
sudo -v
ENABLE_POWER_METRICS=1 src/Parallelism/run_optimized_parallel_benchmarks.sh
```

Outputs:

- `results/Optimized_Parallelism_Results.txt`
- `results/Optimized_Parallelism_Results.csv`

## Run Framework Benchmarks

The framework runner executes:

- Scikit-Learn K-Means
- PyTorch K-Means

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
sudo -v
ENABLE_POWER_METRICS=1 src/Frameworks/run_framework_benchmarks.sh
```

Outputs:

- `results/Scikit_KMeans_Results.txt`
- `results/Scikit_KMeans_Results.csv`
- `results/PyTorch_KMeans_Results.txt`
- `results/PyTorch_KMeans_Results.csv`

PyTorch may take several seconds to import before printing dataset output.

## Run Everything

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
sudo -v

ENABLE_POWER_METRICS=1 src/Serial_Baseline/run_serial_benchmarks.sh
ENABLE_POWER_METRICS=1 src/Naive_Parallelism/run_naive_parallel_benchmarks.sh
ENABLE_POWER_METRICS=1 src/Parallelism/run_optimized_parallel_benchmarks.sh
ENABLE_POWER_METRICS=1 src/Frameworks/run_framework_benchmarks.sh
```

Equivalent wrapper scripts are also available in `scripts/`:

```bash
scripts/run_serial_benchmarks.sh
scripts/run_naive_parallel_benchmarks.sh
scripts/run_optimized_parallel_benchmarks.sh
scripts/run_framework_benchmarks.sh
```

## Standard Output Format

Every benchmark prints and saves blocks like this:

```text
================================================================
K-MEANS PERFORMANCE ANALYSIS
============================

Algorithm Version      : <implementation name>
Dataset File Name      : <dataset file>
Dataset Tier           : <tier>
Dataset Dimensions     : <points> x <dimensions>
Number of Points       : <points>
Number of Clusters     : <clusters>
Threads Used           : <threads>

------------------- PERFORMANCE METRICS ------------------------
Execution Time         : <seconds> seconds
Speedup                : <speedup or N/A>
Throughput             : <points/sec> points/sec
Convergence Iterations : <iterations>
Memory Usage           : <MB> MB
Power Metrics          : <powermetrics status>
CPU Power              : <W or N/A>
GPU Power              : <W or N/A>
Package Power          : <W or N/A>
Package Energy         : <J or N/A>
CPU Utilization        : <% or N/A>
Instructions/Cycle     : <IPC or N/A>
Thermals               : <thermal data or N/A>

------------------- HARDWARE INFORMATION ----------------------
Performed On           : <hardware information>

# Status               : SUCCESS

================================================================
```

## CSV Result Columns

CSV files use standardized columns for side-by-side comparison:

```text
Algorithm_Version
Dataset_File_Name
Dataset_Tier
Dataset_Dimensions
Number_of_Points
Number_of_Clusters
Threads_Used
Execution_Time_Seconds
Speedup
Throughput_Points_Per_Second
Convergence_Iterations
Memory_Usage_MB
Power_Metrics_Status
CPU_Power_W
Package_Power_W
Package_Energy_J
CPU_Utilization_Percent
Instructions_Per_Cycle
Hardware_Info
Status
```

## Implementation Notes

### Serial Baseline

The serial version is the baseline K-Means implementation adapted from the
provided source. It uses one thread and provides the reference runtime for
speedup calculations.

### Naive Parallelism

The naive parallel version keeps the same K-Means structure and applies simple
thread-level parallelism to the point assignment and distance computation work.
It is intentionally simple and is used to study basic scaling behavior.

### Optimized Parallelism

The optimized implementation is designed for Apple Silicon M2:

- Flat `float32` row-major dataset layout
- NEON SIMD distance calculation where applicable
- Thread-local centroid sums and counts
- Reduced synchronization overhead
- Reused buffers across iterations
- 1, 2, 4, and 8 thread benchmark runs

### Frameworks

The Scikit-Learn implementation uses `sklearn.cluster.KMeans` with
`max_iter = 20`. The PyTorch implementation manually performs K-Means using
tensor operations and uses MPS acceleration if available, otherwise CPU.

## Troubleshooting

### Powermetrics Shows N/A

Run:

```bash
sudo -v
ENABLE_POWER_METRICS=1 <benchmark-command>
```

`powermetrics` only works on macOS and usually needs Apple Silicon hardware.

### PyTorch Pauses Before Output

PyTorch may take several seconds to import. Test the installation with:

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
env/bin/python -c "import torch; print(torch.__version__); print(torch.backends.mps.is_available())"
```

### Reinstall PyTorch

If PyTorch import fails or hangs abnormally:

```bash
cd /Users/salman/Desktop/I220908_PDC_Project
env/bin/python -m pip install --force-reinstall --no-cache-dir torch
```

### Temporary File Permission Issues

The benchmark framework uses unique temporary files for powermetrics sampling.
If old root-owned files remain from manual experiments, remove them with:

```bash
sudo rm -f /tmp/pdc_power_metrics_*.txt /tmp/pdc_framework_power_*.txt
```

## Expected Final Results Folder

After all benchmarks run, `results/` should contain:

```text
Serial_Baseline_Results.txt
Serial_Baseline_Results.csv
Naive_Parallelism_Results.txt
Naive_Parallelism_Results.csv
Optimized_Parallelism_Results.txt
Optimized_Parallelism_Results.csv
Scikit_KMeans_Results.txt
Scikit_KMeans_Results.csv
PyTorch_KMeans_Results.txt
PyTorch_KMeans_Results.csv
```

These files are the main benchmark artifacts for the final report.
