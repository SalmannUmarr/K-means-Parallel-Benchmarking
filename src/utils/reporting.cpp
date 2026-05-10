#include "reporting.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
const int REPORT_WIDTH = 64;

std::string field(const std::string &label, const std::string &value) {
    std::ostringstream out;
    out << std::left << std::setw(23) << label << ": " << value;
    return out.str();
}

std::string format_double(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string format_count(long long value) {
    std::string digits = std::to_string(value);
    for (int insert_position = static_cast<int>(digits.length()) - 3; insert_position > 0; insert_position -= 3) {
        digits.insert(static_cast<size_t>(insert_position), ",");
    }
    return digits;
}

std::string power_status_text(const PowerMetrics &metrics) {
    if (!metrics.status.empty() && metrics.status != "N/A" && metrics.status != "Disabled") {
        return metrics.status;
    }
    return "Available via powermetrics (requires sudo)";
}
}

std::string format_benchmark_report(const BenchmarkReport &report) {
    const std::string speedup_text = report.speedup > 0.0 ? format_double(report.speedup, 4) + "x" : "N/A";
    const std::string memory_text = report.memory_usage_mb > 0.0 ? format_double(report.memory_usage_mb, 2) + " MB" : "N/A";

    std::ostringstream out;
    out << std::string(REPORT_WIDTH, '=') << '\n';
    out << "K-MEANS PERFORMANCE ANALYSIS\n";
    out << "============================\n\n";

    out << field("Algorithm Version", report.algorithm_version) << '\n';
    out << field("Dataset File Name", report.dataset_file_name) << '\n';
    out << field("Dataset Tier", report.tier) << '\n';
    out << field("Dataset Dimensions", format_count(report.points) + " x " + std::to_string(report.dimensions)) << '\n';
    out << field("Number of Points", format_count(report.points)) << '\n';
    out << field("Number of Clusters", std::to_string(report.clusters)) << '\n';
    out << field("Threads Used", std::to_string(report.threads_used)) << "\n\n";

    out << "------------------- PERFORMANCE METRICS ------------------------\n";
    out << field("Execution Time", format_double(report.execution_time_seconds, 6) + " seconds") << '\n';
    out << field("Speedup", speedup_text) << '\n';
    out << field("Throughput", format_count(static_cast<long long>(report.throughput_points_per_second)) + " points/sec") << '\n';
    out << field("Convergence Iterations", std::to_string(report.iterations)) << '\n';
    out << field("Memory Usage", memory_text) << '\n';
    out << field("Power Metrics", power_status_text(report.power_metrics)) << '\n';
    out << field("CPU Power", format_metric_or_na(report.power_metrics.cpu_power_w, " W", 4)) << '\n';
    out << field("GPU Power", format_metric_or_na(report.power_metrics.gpu_power_w, " W", 4)) << '\n';
    out << field("Package Power", format_metric_or_na(report.power_metrics.package_power_w, " W", 4)) << '\n';
    out << field("Package Energy", format_metric_or_na(report.power_metrics.package_energy_j, " J", 4)) << '\n';
    out << field("CPU Utilization", format_metric_or_na(report.power_metrics.cpu_utilization_percent, " %", 2)) << '\n';
    out << field("Instructions/Cycle", format_metric_or_na(report.power_metrics.instructions_per_cycle, " IPC", 4)) << '\n';
    out << field("Thermals", report.power_metrics.thermals) << "\n\n";

    out << "------------------- HARDWARE INFORMATION ----------------------\n";
    out << field("Performed On", report.hardware_information) << "\n\n";

    out << field("# Status", report.status) << "\n\n";
    out << std::string(REPORT_WIDTH, '=');
    return out.str();
}

void append_benchmark_text(const std::string &text_path, const std::string &report_text) {
    std::filesystem::create_directories(std::filesystem::path(text_path).parent_path());
    std::ofstream output(text_path, std::ios::app);
    output << report_text << "\n\n";
}
