"""Reusable benchmark helpers for framework-based K-Means implementations."""

from __future__ import annotations

from dataclasses import dataclass
import csv
import os
from pathlib import Path
import platform
import re
import resource
import signal
import subprocess
import tempfile
import time
from typing import Optional


REPORT_WIDTH = 64


@dataclass
class PowerMetrics:
    status: str = "Available via powermetrics (requires sudo)"
    cpu_power_w: Optional[float] = None
    gpu_power_w: Optional[float] = None
    package_power_w: Optional[float] = None
    package_energy_j: Optional[float] = None
    cpu_utilization_percent: Optional[float] = None
    instructions_per_cycle: Optional[float] = None
    thermals: str = "N/A"


@dataclass
class BenchmarkReport:
    algorithm_version: str
    dataset_file_name: str
    dataset_tier: str
    points: int
    dimensions: int
    clusters: int
    threads_used: int
    execution_time_seconds: float
    speedup: Optional[float]
    iterations: int
    memory_usage_mb: float
    power_metrics: PowerMetrics
    hardware_info: str
    status: str = "SUCCESS"

    @property
    def throughput_points_per_second(self) -> float:
        if self.execution_time_seconds <= 0.0:
            return 0.0
        return self.points / self.execution_time_seconds


class ProcessCpuTimer:
    def __init__(self) -> None:
        self.wall_start = 0.0
        self.cpu_start = 0.0

    def start(self) -> None:
        usage = resource.getrusage(resource.RUSAGE_SELF)
        self.cpu_start = usage.ru_utime + usage.ru_stime
        self.wall_start = time.perf_counter()

    def stop(self) -> tuple[float, float]:
        wall_elapsed = time.perf_counter() - self.wall_start
        usage = resource.getrusage(resource.RUSAGE_SELF)
        cpu_elapsed = usage.ru_utime + usage.ru_stime - self.cpu_start
        cpu_percent = (cpu_elapsed / wall_elapsed) * 100.0 if wall_elapsed > 0.0 else 0.0
        return wall_elapsed, cpu_percent


class PowerMetricsSession:
    def __init__(self, dataset_name: str, suffix: str) -> None:
        safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", f"{dataset_name}_{suffix}")
        handle = tempfile.NamedTemporaryFile(
            prefix=f"pdc_framework_power_{safe_name}_",
            suffix=".txt",
            dir="/tmp",
            delete=False,
        )
        self.raw_output_path = Path(handle.name)
        handle.close()
        self.process: Optional[subprocess.Popen[bytes]] = None

    def start(self) -> None:
        if os.environ.get("ENABLE_POWER_METRICS", "1") != "1":
            return
        if not _command_output(["/bin/sh", "-lc", "command -v powermetrics"]):
            return

        # sudo -n uses cached credentials and never blocks automated runs.
        command = [
            "sudo",
            "-n",
            "powermetrics",
            "--samplers",
            "tasks,cpu_power",
            "--show-process-ipc",
            "--show-process-amp",
            "-i",
            "500",
            "-o",
            str(self.raw_output_path),
        ]
        try:
            self.process = subprocess.Popen(
                command,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            time.sleep(0.2)
        except OSError:
            self.process = None

    def stop_and_collect(
        self, execution_time_seconds: float, fallback_cpu_utilization: float
    ) -> PowerMetrics:
        if self.process is not None and self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        try:
            subprocess.run(
                ["sudo", "-n", "pkill", "-TERM", "-f", str(self.raw_output_path)],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=1,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass
        time.sleep(0.1)

        metrics = parse_power_metrics(
            self.raw_output_path, execution_time_seconds, fallback_cpu_utilization
        )
        _safe_unlink(self.raw_output_path)
        return metrics


def parse_power_metrics(
    raw_output_path: Path, execution_time_seconds: float, fallback_cpu_utilization: float
) -> PowerMetrics:
    metrics = PowerMetrics(cpu_utilization_percent=fallback_cpu_utilization)
    if not raw_output_path.exists():
        return metrics

    text = raw_output_path.read_text(errors="ignore")
    cpu_values = _extract_power_values(text, r"CPU Power:\s*([0-9.]+)\s*(mW|W)")
    gpu_values = _extract_power_values(text, r"GPU Power:\s*([0-9.]+)\s*(mW|W)")
    package_values = _extract_power_values(
        text, r"(?:Combined|Package).*Power.*:\s*([0-9.]+)\s*(mW|W)"
    )
    ipc_values = _extract_ipc_values(text)
    ipc_values.extend(_extract_process_table_ipc_values(text))

    if cpu_values:
        metrics.cpu_power_w = sum(cpu_values) / len(cpu_values)
    if gpu_values:
        metrics.gpu_power_w = sum(gpu_values) / len(gpu_values)
    if package_values:
        metrics.package_power_w = sum(package_values) / len(package_values)
    elif metrics.cpu_power_w is not None:
        metrics.package_power_w = metrics.cpu_power_w + (metrics.gpu_power_w or 0.0)

    if metrics.package_power_w is not None:
        metrics.package_energy_j = metrics.package_power_w * execution_time_seconds
        metrics.status = "Captured via powermetrics"
    if ipc_values:
        metrics.instructions_per_cycle = sum(ipc_values) / len(ipc_values)

    thermal_matches = re.findall(r"(?:CPU|GPU).*temperature:\s*([0-9.]+)\s*C", text, re.I)
    if thermal_matches:
        metrics.thermals = f"{thermal_matches[-1]} C"

    return metrics


def format_report(report: BenchmarkReport) -> str:
    speedup = "N/A" if report.speedup is None else f"{report.speedup:.4f}x"
    lines = [
        "=" * REPORT_WIDTH,
        "K-MEANS PERFORMANCE ANALYSIS",
        "============================",
        "",
        _field("Algorithm Version", report.algorithm_version),
        _field("Dataset File Name", report.dataset_file_name),
        _field("Dataset Tier", report.dataset_tier),
        _field("Dataset Dimensions", f"{report.points:,} x {report.dimensions}"),
        _field("Number of Points", f"{report.points:,}"),
        _field("Number of Clusters", str(report.clusters)),
        _field("Threads Used", str(report.threads_used)),
        "",
        "------------------- PERFORMANCE METRICS ------------------------",
        _field("Execution Time", f"{report.execution_time_seconds:.6f} seconds"),
        _field("Speedup", speedup),
        _field(
            "Throughput",
            f"{int(report.throughput_points_per_second):,} points/sec",
        ),
        _field("Convergence Iterations", str(report.iterations)),
        _field("Memory Usage", f"{report.memory_usage_mb:.2f} MB"),
        _field("Power Metrics", report.power_metrics.status),
        _field("CPU Power", _metric(report.power_metrics.cpu_power_w, " W", 4)),
        _field("GPU Power", _metric(report.power_metrics.gpu_power_w, " W", 4)),
        _field("Package Power", _metric(report.power_metrics.package_power_w, " W", 4)),
        _field("Package Energy", _metric(report.power_metrics.package_energy_j, " J", 4)),
        _field(
            "CPU Utilization",
            _metric(report.power_metrics.cpu_utilization_percent, " %", 2),
        ),
        _field(
            "Instructions/Cycle",
            _metric(report.power_metrics.instructions_per_cycle, " IPC", 4),
        ),
        _field("Thermals", report.power_metrics.thermals),
        "",
        "------------------- HARDWARE INFORMATION ----------------------",
        _field("Performed On", report.hardware_info),
        "",
        _field("# Status", report.status),
        "",
        "=" * REPORT_WIDTH,
    ]
    return "\n".join(lines)


def append_text_report(path: Path, report_text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(report_text)
        handle.write("\n\n")


def append_csv_report(path: Path, report: BenchmarkReport) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    write_header = not path.exists() or path.stat().st_size == 0
    with path.open("a", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        if write_header:
            writer.writerow(
                [
                    "Algorithm_Version",
                    "Dataset_File_Name",
                    "Dataset_Tier",
                    "Dataset_Dimensions",
                    "Number_of_Points",
                    "Number_of_Clusters",
                    "Threads_Used",
                    "Execution_Time_Seconds",
                    "Speedup",
                    "Throughput_Points_Per_Second",
                    "Convergence_Iterations",
                    "Memory_Usage_MB",
                    "Power_Metrics_Status",
                    "CPU_Power_W",
                    "Package_Power_W",
                    "Package_Energy_J",
                    "CPU_Utilization_Percent",
                    "Instructions_Per_Cycle",
                    "Hardware_Info",
                    "Status",
                ]
            )

        writer.writerow(
            [
                report.algorithm_version,
                report.dataset_file_name,
                report.dataset_tier,
                f"{report.points:,} x {report.dimensions}",
                report.points,
                report.clusters,
                report.threads_used,
                f"{report.execution_time_seconds:.6f}",
                "N/A" if report.speedup is None else f"{report.speedup:.4f}",
                f"{report.throughput_points_per_second:.2f}",
                report.iterations,
                f"{report.memory_usage_mb:.2f}",
                report.power_metrics.status,
                _csv_number(report.power_metrics.cpu_power_w, 4),
                _csv_number(report.power_metrics.package_power_w, 4),
                _csv_number(report.power_metrics.package_energy_j, 4),
                _csv_number(report.power_metrics.cpu_utilization_percent, 2),
                _csv_number(report.power_metrics.instructions_per_cycle, 4),
                report.hardware_info,
                report.status,
            ]
        )


def load_serial_baseline_times(project_root: Path) -> dict[str, float]:
    csv_path = project_root / "results" / "Serial_Baseline_Results.csv"
    if not csv_path.exists():
        return {}

    times: dict[str, float] = {}
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            try:
                times[row["Dataset_File_Name"]] = float(row["Execution_Time_Seconds"])
            except (KeyError, TypeError, ValueError):
                continue
    return times


def dataset_tier(dataset_name: str) -> str:
    if "tier1" in dataset_name:
        return "Tier 1 (Small)"
    if "tier2" in dataset_name:
        return "Tier 2 (Medium)"
    if "tier3" in dataset_name:
        return "Tier 3 (Large)"
    return "N/A"


def cluster_count_from_labels(labels) -> int:
    return int(len(set(int(value) for value in labels)))


def hardware_info(extra: str = "") -> str:
    machine = platform.machine()
    cpu = _command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
    if not cpu:
        cpu = "Apple Silicon" if machine == "arm64" else platform.processor() or "CPU"
    cores = os.cpu_count() or 1
    mem = _command_output(["sysctl", "-n", "hw.memsize"])
    memory_text = ""
    if mem:
        try:
            memory_text = f", {int(mem) / (1024 ** 3):.0f} GB RAM"
        except ValueError:
            memory_text = ""
    if not memory_text:
        try:
            pages = os.sysconf("SC_PHYS_PAGES")
            page_size = os.sysconf("SC_PAGE_SIZE")
            memory_text = f", {pages * page_size / (1024 ** 3):.0f} GB RAM"
        except (AttributeError, OSError, ValueError):
            memory_text = ""
    suffix = f", {extra}" if extra else ""
    return f"{cpu}, {cores} logical cores{memory_text}{suffix}"


def peak_memory_mb() -> float:
    usage = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if platform.system() == "Darwin":
        return usage / (1024 * 1024)
    return usage / 1024


def _field(label: str, value: str) -> str:
    return f"{label:<23}: {value}"


def _metric(value: Optional[float], suffix: str, precision: int) -> str:
    if value is None:
        return "N/A"
    return f"{value:.{precision}f}{suffix}"


def _csv_number(value: Optional[float], precision: int) -> str:
    if value is None:
        return "N/A"
    return f"{value:.{precision}f}"


def _extract_power_values(text: str, pattern: str) -> list[float]:
    values: list[float] = []
    for amount, unit in re.findall(pattern, text, flags=re.I):
        watts = float(amount)
        if unit.lower() == "mw":
            watts /= 1000.0
        values.append(watts)
    return values


def _extract_ipc_values(text: str) -> list[float]:
    values: list[float] = []
    patterns = [
        r"([0-9]+(?:\.[0-9]+)?)\s*(?:insn|instructions)\s+per\s+cycle",
        r"IPC\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
        r"instructions/cycle\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
    ]
    for pattern in patterns:
        values.extend(float(value) for value in re.findall(pattern, text, flags=re.I))
    return values


def _extract_process_table_ipc_values(text: str) -> list[float]:
    values: list[float] = []
    benchmark_names = ("serial_kmeans", "naive_parallel_kmeans", "python", "Python")
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        parts = stripped.split()
        if len(parts) < 6 or not any(name in parts[0] for name in benchmark_names):
            continue
        try:
            # powermetrics process rows end with:
            # Instr/s  Cycles/s  P-Instr/s  P-Cycles/s
            instructions_per_second = float(parts[-4])
            cycles_per_second = float(parts[-3])
        except ValueError:
            continue
        if cycles_per_second > 0.0:
            values.append(instructions_per_second / cycles_per_second)
    return values


def _command_output(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout.strip()


def _safe_unlink(path: Path) -> None:
    try:
        if path.exists():
            path.unlink()
    except OSError:
        pass
