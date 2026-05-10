from __future__ import annotations

from pathlib import Path
import os
import sys

import pandas as pd
import torch

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
ALGORITHM_VERSION = "PyTorch K-Means"
RANDOM_SEED = 42
CHUNK_SIZE = 100_000


def select_device() -> torch.device:
    if torch.backends.mps.is_available():
        return torch.device("mps")
    return torch.device("cpu")


def run_pytorch_kmeans(data_cpu, clusters: int, device: torch.device) -> int:
    torch.manual_seed(RANDOM_SEED)
    data = torch.as_tensor(data_cpu, dtype=torch.float32, device=device)
    point_count, dimensions = data.shape

    initial_indices = torch.randperm(point_count, device=device)[:clusters]
    centroids = data[initial_indices].clone()
    labels = torch.zeros(point_count, dtype=torch.long, device=device)
    iterations = 0

    for iteration in range(MAX_ITERATIONS):
        iterations = iteration + 1
        sums = torch.zeros((clusters, dimensions), dtype=torch.float32, device=device)
        counts = torch.zeros(clusters, dtype=torch.float32, device=device)
        changed = False

        for start in range(0, point_count, CHUNK_SIZE):
            end = min(start + CHUNK_SIZE, point_count)
            distances = torch.cdist(data[start:end], centroids)
            chunk_labels = torch.argmin(distances, dim=1)
            if torch.any(chunk_labels != labels[start:end]):
                changed = True
            labels[start:end] = chunk_labels
            sums.index_add_(0, chunk_labels, data[start:end])
            counts.index_add_(0, chunk_labels, torch.ones_like(chunk_labels, dtype=torch.float32))

        non_empty = counts > 0
        new_centroids = centroids.clone()
        new_centroids[non_empty] = sums[non_empty] / counts[non_empty].unsqueeze(1)
        centroid_changed = torch.max(torch.abs(new_centroids - centroids)).item() > 1e-6
        centroids = new_centroids

        if not changed and not centroid_changed:
            break

    return iterations


def run_dataset(dataset_path: Path, project_root: Path, serial_times: dict[str, float]) -> None:
    dataset_name = dataset_path.name
    frame = pd.read_csv(dataset_path, dtype="float32")
    labels = frame.iloc[:, -1].astype("int32").to_numpy()
    data = frame.iloc[:, :-1].to_numpy(dtype="float32", copy=True)
    clusters = cluster_count_from_labels(labels)
    device = select_device()

    power_session = PowerMetricsSession(dataset_name, f"pytorch_{device.type}")
    power_session.start()

    timer = ProcessCpuTimer()
    timer.start()
    iterations = run_pytorch_kmeans(data, clusters, device)
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
        threads_used=torch.get_num_threads() if device.type == "cpu" else os.cpu_count() or 1,
        execution_time_seconds=execution_time,
        speedup=speedup,
        iterations=iterations,
        memory_usage_mb=peak_memory_mb(),
        power_metrics=power_metrics,
        hardware_info=hardware_info(f"PyTorch device: {device.type}"),
    )

    report_text = format_report(report)
    print(report_text, flush=True)
    append_text_report(project_root / "results" / "PyTorch_KMeans_Results.txt", report_text)
    append_csv_report(project_root / "results" / "PyTorch_KMeans_Results.csv", report)


def main() -> int:
    project_root = Path(__file__).resolve().parents[2]
    for output_name in ("PyTorch_KMeans_Results.txt", "PyTorch_KMeans_Results.csv"):
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
