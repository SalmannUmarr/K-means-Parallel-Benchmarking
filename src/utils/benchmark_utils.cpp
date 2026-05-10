#include "benchmark_utils.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <unistd.h>

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

double timeval_to_seconds(const timeval &value) {
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1000000.0;
}
}

void BenchmarkTimer::start() {
    start_time = std::chrono::high_resolution_clock::now();
}

double BenchmarkTimer::stop_seconds() {
    const auto end_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count();
}

CpuUsageSnapshot capture_cpu_usage() {
    CpuUsageSnapshot snapshot{};
    getrusage(RUSAGE_SELF, &snapshot.usage);
    return snapshot;
}

double process_cpu_utilization_percent(const CpuUsageSnapshot &start,
                                       const CpuUsageSnapshot &end,
                                       double wall_seconds) {
    if (wall_seconds <= 0.0) {
        return 0.0;
    }

    const double start_cpu = timeval_to_seconds(start.usage.ru_utime) +
                             timeval_to_seconds(start.usage.ru_stime);
    const double end_cpu = timeval_to_seconds(end.usage.ru_utime) +
                           timeval_to_seconds(end.usage.ru_stime);
    return ((end_cpu - start_cpu) / wall_seconds) * 100.0;
}

double peak_memory_usage_mb() {
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }

#ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
}

std::string hardware_information() {
#ifdef __APPLE__
    std::string cpu = run_command("sysctl -n machdep.cpu.brand_string 2>/dev/null");
    if (cpu.empty()) {
        cpu = run_command("sysctl -n hw.model 2>/dev/null");
    }
    if (cpu.empty()) {
#if defined(__aarch64__) || defined(__arm64__)
        cpu = "Apple Silicon";
#else
        cpu = "Apple/macOS";
#endif
    }

    std::ostringstream out;
    out << cpu;

    const long logical_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (logical_cores > 0) {
        out << ", " << logical_cores << " logical cores";
    }

#if defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        const double mem_gb = (static_cast<double>(pages) * static_cast<double>(page_size)) /
                              (1024.0 * 1024.0 * 1024.0);
        out << ", " << static_cast<int>(mem_gb + 0.5) << " GB RAM";
    }
#endif
    return out.str();
#else
    std::string cpu = run_command("lscpu | grep 'Model name' | cut -d ':' -f 2 | sed 's/^ *//'");
    if (cpu.empty()) {
        cpu = "CPU";
    }
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    std::ostringstream out;
    out << cpu;
    if (cores > 0) {
        out << ", " << cores << " logical cores";
    }
    return out.str();
#endif
}

std::string dataset_tier_label(const std::string &dataset_file_name) {
    if (dataset_file_name.find("tier1") != std::string::npos) {
        return "Tier 1 (Small)";
    }
    if (dataset_file_name.find("tier2") != std::string::npos) {
        return "Tier 2 (Medium)";
    }
    if (dataset_file_name.find("tier3") != std::string::npos) {
        return "Tier 3 (Large)";
    }
    return "N/A";
}
