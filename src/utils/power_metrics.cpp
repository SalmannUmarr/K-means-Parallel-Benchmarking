#include "power_metrics.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <signal.h>

namespace {
std::string run_command(const std::string &command) {
    std::array<char, 256> buffer{};
    std::string result;
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return "";
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

double to_watts(double value, const std::string &unit) {
    if (unit == "mW" || unit == "mw") {
        return value / 1000.0;
    }
    return value;
}

void accumulate_power(const std::string &line,
                      const std::regex &pattern,
                      double &sum,
                      int &count) {
    std::smatch match;
    if (std::regex_search(line, match, pattern)) {
        sum += to_watts(std::stod(match[1].str()), match[2].str());
        ++count;
    }
}

void accumulate_ipc(const std::string &line,
                    const std::vector<std::regex> &patterns,
                    double &sum,
                    int &count) {
    for (const std::regex &pattern : patterns) {
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            sum += std::stod(match[1].str());
            ++count;
            return;
        }
    }
}

double average_or_na(double sum, int count) {
    return count > 0 ? sum / static_cast<double>(count) : -1.0;
}

bool is_benchmark_process(const std::string &name) {
    return name.find("serial_kmeans") != std::string::npos ||
           name.find("naive_parallel_kmeans") != std::string::npos ||
           name.find("python") != std::string::npos ||
           name.find("Python") != std::string::npos;
}

bool parse_process_table_ipc(const std::string &line, double &ipc) {
    std::stringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }

    // powermetrics process rows end with:
    // Instr/s  Cycles/s  P-Instr/s  P-Cycles/s
    if (tokens.size() < 6 || !is_benchmark_process(tokens[0])) {
        return false;
    }

    try {
        const double instructions_per_second = std::stod(tokens[tokens.size() - 4]);
        const double cycles_per_second = std::stod(tokens[tokens.size() - 3]);
        if (cycles_per_second > 0.0) {
            ipc = instructions_per_second / cycles_per_second;
            return true;
        }
    } catch (const std::exception &) {
        return false;
    }

    return false;
}

std::string sanitize_name(const std::string &name) {
    std::string sanitized = name;
    for (char &character : sanitized) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-')) {
            character = '_';
        }
    }
    return sanitized;
}

std::string create_temp_file_path(const std::string &dataset_name) {
    std::string pattern = "/tmp/pdc_power_metrics_" + sanitize_name(dataset_name) + "_XXXXXX.txt";
    std::vector<char> path_buffer(pattern.begin(), pattern.end());
    path_buffer.push_back('\0');

    const int fd = mkstemps(path_buffer.data(), 4);
    if (fd >= 0) {
        close(fd);
        return std::string(path_buffer.data());
    }

    return "/tmp/pdc_power_metrics_" + sanitize_name(dataset_name) + "_" +
           std::to_string(static_cast<long long>(getpid())) + ".txt";
}

void remove_file_if_possible(const std::string &path) {
    if (path.empty()) {
        return;
    }
    std::error_code ignored_error;
    if (std::filesystem::exists(path, ignored_error)) {
        std::filesystem::remove(path, ignored_error);
    }
}
}

PowerMetricsSession::PowerMetricsSession(const std::string &dataset_name)
    : raw_output_path(create_temp_file_path(dataset_name)),
      sampler_pid(-1),
      active(false) {}

void PowerMetricsSession::start() {
    if (!power_metrics_enabled()) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(raw_output_path).parent_path());
    const std::string available = run_command("command -v powermetrics");
    if (available.empty()) {
        return;
    }

    // sudo -n avoids blocking automated benchmark runs. Run `sudo -v` once in an
    // interactive terminal before benchmarking to enable privileged sampling.
    const std::string command =
        "sudo -n powermetrics --samplers tasks,cpu_power --show-process-ipc --show-process-amp -i 500 -o " + raw_output_path +
        " >/dev/null 2>&1 & echo $!";
    const std::string pid_text = run_command(command);
    if (pid_text.empty()) {
        return;
    }

    sampler_pid = std::atoi(pid_text.c_str());
    if (sampler_pid > 0) {
        active = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

PowerMetrics PowerMetricsSession::stop_and_collect(double execution_time_seconds,
                                                   double fallback_cpu_utilization_percent) {
    if (active && sampler_pid > 0) {
        kill(sampler_pid, SIGINT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        run_command("sudo -n pkill -TERM -f '" + raw_output_path + "' >/dev/null 2>&1");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    PowerMetrics metrics = parse_power_metrics_file(raw_output_path,
                                                    execution_time_seconds,
                                                    fallback_cpu_utilization_percent);
    if (!power_metrics_enabled()) {
        metrics.status = "Available via powermetrics (requires sudo)";
    } else if (!active || metrics.status == "N/A") {
        metrics.status = "Available via powermetrics (requires sudo)";
    } else if (metrics.status == "Captured") {
        metrics.status = "Captured via powermetrics";
    }
    remove_file_if_possible(raw_output_path);
    return metrics;
}

PowerMetrics parse_power_metrics_file(const std::string &raw_output_path,
                                      double execution_time_seconds,
                                      double fallback_cpu_utilization_percent) {
    PowerMetrics metrics;
    metrics.cpu_utilization_percent = fallback_cpu_utilization_percent;

    std::ifstream input(raw_output_path);
    if (!input) {
        metrics.status = "N/A";
        return metrics;
    }

    const std::regex cpu_power_pattern(R"(CPU Power:\s*([0-9.]+)\s*(mW|W))", std::regex::icase);
    const std::regex gpu_power_pattern(R"(GPU Power:\s*([0-9.]+)\s*(mW|W))", std::regex::icase);
    const std::regex package_power_pattern(R"((?:Package|Combined).*Power:\s*([0-9.]+)\s*(mW|W))", std::regex::icase);
    const std::regex thermal_pattern(R"((?:CPU|GPU).*temperature:\s*([0-9.]+)\s*C)", std::regex::icase);
    const std::vector<std::regex> ipc_patterns = {
        std::regex(R"(([0-9]+(?:\.[0-9]+)?)\s*(?:insn|instructions)\s+per\s+cycle)", std::regex::icase),
        std::regex(R"(IPC\s*[:=]\s*([0-9]+(?:\.[0-9]+)?))", std::regex::icase),
        std::regex(R"(instructions/cycle\s*[:=]\s*([0-9]+(?:\.[0-9]+)?))", std::regex::icase)
    };

    double cpu_sum = 0.0;
    double gpu_sum = 0.0;
    double package_sum = 0.0;
    double ipc_sum = 0.0;
    int cpu_count = 0;
    int gpu_count = 0;
    int package_count = 0;
    int ipc_count = 0;
    std::vector<std::string> thermal_values;

    std::string line;
    while (std::getline(input, line)) {
        accumulate_power(line, cpu_power_pattern, cpu_sum, cpu_count);
        accumulate_power(line, gpu_power_pattern, gpu_sum, gpu_count);
        accumulate_power(line, package_power_pattern, package_sum, package_count);
        accumulate_ipc(line, ipc_patterns, ipc_sum, ipc_count);

        double table_ipc = -1.0;
        if (parse_process_table_ipc(line, table_ipc)) {
            ipc_sum += table_ipc;
            ++ipc_count;
        }

        std::smatch thermal_match;
        if (std::regex_search(line, thermal_match, thermal_pattern)) {
            thermal_values.push_back(thermal_match[1].str() + " C");
        }
    }

    metrics.cpu_power_w = average_or_na(cpu_sum, cpu_count);
    metrics.gpu_power_w = average_or_na(gpu_sum, gpu_count);
    metrics.package_power_w = average_or_na(package_sum, package_count);
    metrics.instructions_per_cycle = average_or_na(ipc_sum, ipc_count);
    if (metrics.package_power_w < 0.0 && metrics.cpu_power_w >= 0.0) {
        metrics.package_power_w = metrics.cpu_power_w + (metrics.gpu_power_w > 0.0 ? metrics.gpu_power_w : 0.0);
    }
    if (metrics.package_power_w >= 0.0) {
        metrics.package_energy_j = metrics.package_power_w * execution_time_seconds;
    }
    if (!thermal_values.empty()) {
        metrics.thermals = thermal_values.back();
    }
    metrics.status = (cpu_count > 0 || gpu_count > 0 || package_count > 0) ? "Captured" : "N/A";
    return metrics;
}

bool power_metrics_enabled() {
    const char *enabled = std::getenv("ENABLE_POWER_METRICS");
    return enabled != nullptr && std::string(enabled) == "1";
}

std::string format_metric_or_na(double value, const std::string &suffix, int precision) {
    if (value < 0.0) {
        return "N/A";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value << suffix;
    return out.str();
}
