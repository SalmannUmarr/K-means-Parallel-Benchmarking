#ifndef PDC_BENCHMARK_UTILS_H
#define PDC_BENCHMARK_UTILS_H

#include <chrono>
#include <string>
#include <sys/resource.h>

class BenchmarkTimer {
public:
    void start();
    double stop_seconds();

private:
    std::chrono::high_resolution_clock::time_point start_time;
};

struct CpuUsageSnapshot {
    rusage usage;
};

CpuUsageSnapshot capture_cpu_usage();
double process_cpu_utilization_percent(const CpuUsageSnapshot &start,
                                       const CpuUsageSnapshot &end,
                                       double wall_seconds);
double peak_memory_usage_mb();
std::string hardware_information();
std::string dataset_tier_label(const std::string &dataset_file_name);

#endif
