from __future__ import annotations

from pathlib import Path
import os
import sys

os.environ["LOKY_MAX_CPU_COUNT"] = os.environ.get("LOKY_MAX_CPU_COUNT") or str(os.cpu_count() or 1)
os.environ["LOKY_PHYSICAL_CORES"] = os.environ.get("LOKY_PHYSICAL_CORES") or str(os.cpu_count() or 1)

import pandas as pd
from sklearn.cluster import KMeans

from benchmark_common import (
    BenchmarkReport,
    PowerMetricsSession,
    ProcessCpuTimer,
    append_csv_report,
    append_text_report,
    cluster_count_from_labels,
    dataset_tier,
    format_report,
    hardware_info,
    load_serial_baseline_times,
    peak_memory_mb,
)


MAX_ITERATIONS = 20
ALGORITHM_VERSION = "Scikit-Learn K-Means"


def run_dataset(dataset_path: Path, project_root: Path, serial_times: dict[str, float]) -> None:
    dataset_name = dataset_path.name
    frame = pd.read_csv(dataset_path, dtype="float32")
    labels = frame.iloc[:, -1].astype("int32").to_numpy()
    data = frame.iloc[:, :-1].to_numpy(dtype="float32", copy=True)
    clusters = cluster_count_from_labels(labels)

    power_session = PowerMetricsSession(dataset_name, "scikit")
    power_session.start()

    timer = ProcessCpuTimer()
    timer.start()
    model = KMeans(
        n_clusters=clusters,
        max_iter=MAX_ITERATIONS,
        random_state=42,
        n_init=1,
        algorithm="lloyd",
    )
    model.fit(data)
    execution_time, cpu_utilization = timer.stop()

    power_metrics = power_session.stop_and_collect(execution_time, cpu_utilization)
    serial_time = serial_times.get(dataset_name)
    speedup = serial_time / execution_time if serial_time and execution_time > 0.0 else None

    report = BenchmarkReport(
        algorithm_version=ALGORITHM_VERSION,
        dataset_file_name=dataset_name,
        dataset_tier=dataset_tier(dataset_name),
        points=data.shape[0],
        dimensions=data.shape[1],
        clusters=clusters,
        threads_used=os.cpu_count() or 1,
        execution_time_seconds=execution_time,
        speedup=speedup,
        iterations=int(model.n_iter_),
        memory_usage_mb=peak_memory_mb(),
        power_metrics=power_metrics,
        hardware_info=hardware_info("framework: scikit-learn"),
    )

    report_text = format_report(report)
    print(report_text, flush=True)
    append_text_report(project_root / "results" / "Scikit_KMeans_Results.txt", report_text)
    append_csv_report(project_root / "results" / "Scikit_KMeans_Results.csv", report)


def main() -> int:
    project_root = Path(__file__).resolve().parents[2]
    for output_name in ("Scikit_KMeans_Results.txt", "Scikit_KMeans_Results.csv"):
        output_path = project_root / "results" / output_name
        if output_path.exists():
            output_path.unlink()

    serial_times = load_serial_baseline_times(project_root)
    datasets = [
        project_root / "data" / "tier1_small.csv",
        project_root / "data" / "tier2_medium.csv",
        project_root / "data" / "tier3_large.csv",
    ]

    for dataset_path in datasets:
        print(f"Running {ALGORITHM_VERSION} on {dataset_path.name}...", flush=True)
        run_dataset(dataset_path, project_root, serial_times)
        print(flush=True)

    return 0


if __name__ == "__main__":
    exit_code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(exit_code)
