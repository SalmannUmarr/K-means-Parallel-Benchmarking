#include "csv_logger.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
std::string csv_escape(const std::string &value) {
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

std::string number_or_na(double value, int precision) {
    if (value < 0.0) {
        return "N/A";
    }
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
}

void append_benchmark_csv(const std::string &csv_path, const BenchmarkReport &report) {
    std::filesystem::create_directories(std::filesystem::path(csv_path).parent_path());
    const bool write_header = !std::filesystem::exists(csv_path) ||
                              std::filesystem::file_size(csv_path) == 0;

    std::ofstream csv(csv_path, std::ios::app);
    if (write_header) {
        csv << "Algorithm_Version,Dataset_File_Name,Dataset_Tier,Dataset_Dimensions,"
            << "Number_of_Points,Number_of_Clusters,Threads_Used,"
            << "Execution_Time_Seconds,Speedup,Throughput_Points_Per_Second,"
            << "Convergence_Iterations,Memory_Usage_MB,Power_Metrics_Status,"
            << "CPU_Power_W,Package_Power_W,Package_Energy_J,CPU_Utilization_Percent,"
            << "Instructions_Per_Cycle,Hardware_Info,Status\n";
    }

    const std::string dimensions = format_count(report.points) + " x " +
                                   std::to_string(report.dimensions);
    const std::string power_status = report.power_metrics.status.empty() ||
                                             report.power_metrics.status == "N/A" ||
                                             report.power_metrics.status == "Disabled"
                                         ? "Available via powermetrics (requires sudo)"
                                         : report.power_metrics.status;

    csv << csv_escape(report.algorithm_version) << ','
        << csv_escape(report.dataset_file_name) << ','
        << csv_escape(report.tier) << ','
        << csv_escape(dimensions) << ','
        << report.points << ','
        << report.clusters << ','
        << report.threads_used << ','
        << number_or_na(report.execution_time_seconds, 6) << ','
        << (report.speedup > 0.0 ? number_or_na(report.speedup, 4) : "N/A") << ','
        << number_or_na(report.throughput_points_per_second, 2) << ','
        << report.iterations << ','
        << number_or_na(report.memory_usage_mb, 2) << ','
        << csv_escape(power_status) << ','
        << number_or_na(report.power_metrics.cpu_power_w, 4) << ','
        << number_or_na(report.power_metrics.package_power_w, 4) << ','
        << number_or_na(report.power_metrics.package_energy_j, 4) << ','
        << number_or_na(report.power_metrics.cpu_utilization_percent, 2) << ','
        << number_or_na(report.power_metrics.instructions_per_cycle, 4) << ','
        << csv_escape(report.hardware_information) << ','
        << csv_escape(report.status) << '\n';
}
