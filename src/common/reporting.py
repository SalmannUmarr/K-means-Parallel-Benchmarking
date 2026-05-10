"""Standardized benchmark reporting for the PDC K-means project.

Keep this module implementation-agnostic so serial, threaded, MPI, and GPU
versions can reuse exactly the same report layout for side-by-side comparison.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
import platform
import subprocess
from typing import Optional


REPORT_WIDTH = 64


@dataclass(frozen=True)
class BenchmarkReport:
    algorithm_version: str
    dataset_file: str
    points: int
    dimensions: int
    clusters: int
    threads_used: int
    execution_time_seconds: float
    iterations: int
    memory_usage_mb: Optional[float]
    speedup: Optional[float] = None
    power_metrics: str = "N/A"
    status: str = "SUCCESS"

    @property
    def throughput_points_per_second(self) -> float:
        if self.execution_time_seconds <= 0.0:
            return 0.0
        return self.points / self.execution_time_seconds


def get_hardware_info() -> str:
    """Return a concise, stable hardware description for report screenshots."""
    machine = platform.machine()
    system = platform.system()

    if system == "Darwin":
        chip = _run_command(["sysctl", "-n", "machdep.cpu.brand_string"])
        if not chip:
            chip = _run_command(["sysctl", "-n", "hw.model"])
        cores = _run_command(["sysctl", "-n", "hw.physicalcpu"])
        if not cores and os.cpu_count():
            cores = f"{os.cpu_count()} logical"
        memory_bytes = _run_command(["sysctl", "-n", "hw.memsize"])
        memory_gb = _format_memory_gb(memory_bytes)
        if not chip and machine == "arm64":
            chip = "Apple Silicon"
        core_label = f"{cores} cores" if cores and "logical" not in cores else f"{cores} cores"
        return _join_known([chip, machine, core_label if cores else "", memory_gb])

    processor = platform.processor() or machine
    cores = os.cpu_count()
    core_text = f"{cores} logical cores" if cores else ""
    return _join_known([processor, core_text])


def get_power_metrics_note() -> str:
    """Collect lightweight power availability info without requiring sudo.

    macOS detailed power sampling generally needs powermetrics with elevated
    privileges, so the report records availability instead of failing the run.
    """
    if platform.system() == "Darwin":
        if _run_command(["which", "powermetrics"]):
            return "Available via powermetrics (requires sudo)"
        return "Not available on this system"
    return "N/A"


def format_report(report: BenchmarkReport) -> str:
    """Format benchmark metrics using one canonical console layout."""
    speedup_text = "N/A" if report.speedup is None else f"{report.speedup:.4f}x"
    memory_text = (
        "N/A" if report.memory_usage_mb is None else f"{report.memory_usage_mb:.2f} MB"
    )

    lines = [
        "=" * REPORT_WIDTH,
        "K-MEANS PERFORMANCE ANALYSIS".center(REPORT_WIDTH),
        "=" * REPORT_WIDTH,
        "",
        _field("Algorithm Version", report.algorithm_version),
        _field("Dataset File Name", report.dataset_file),
        _field("Dataset Dimensions", f"{report.points:,} x {report.dimensions}"),
        _field("Number of Points", f"{report.points:,}"),
        _field("Number of Clusters", f"{report.clusters}"),
        _field("Threads Used", f"{report.threads_used}"),
        "",
        "-" * 19 + " PERFORMANCE METRICS " + "-" * 24,
        _field("Execution Time", f"{report.execution_time_seconds:.6f} seconds"),
        _field("Speedup", speedup_text),
        _field("Throughput", f"{report.throughput_points_per_second:,.2f} points/sec"),
        _field("Convergence Iterations", f"{report.iterations}"),
        _field("Memory Usage", memory_text),
        _field("Power Metrics", report.power_metrics),
        "",
        "-" * 19 + " HARDWARE INFORMATION " + "-" * 22,
        _field("Performed On", get_hardware_info()),
        "",
        _field("# Status", report.status),
        "=" * REPORT_WIDTH,
    ]
    return "\n".join(lines)


def save_report(report_text: str, output_path: str) -> None:
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as report_file:
        report_file.write(report_text)
        report_file.write("\n")


def _field(label: str, value: str) -> str:
    return f"{label:<23}: {value}"


def _run_command(command: list[str]) -> str:
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


def _format_memory_gb(memory_bytes: str) -> str:
    if not memory_bytes:
        return ""
    try:
        return f"{int(memory_bytes) / (1024 ** 3):.0f} GB RAM"
    except ValueError:
        return ""


def _join_known(parts: list[str]) -> str:
    known_parts = [part for part in parts if part]
    return ", ".join(known_parts) if known_parts else platform.platform()
