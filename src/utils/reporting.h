#ifndef PDC_REPORTING_H
#define PDC_REPORTING_H

#include <string>

#include "power_metrics.h"

struct BenchmarkReport {
    std::string algorithm_version;
    std::string dataset_file_name;
    std::string tier;
    long long points;
    int dimensions;
    int clusters;
    int threads_used;
    double execution_time_seconds;
    int iterations;
    double throughput_points_per_second;
    double memory_usage_mb;
    PowerMetrics power_metrics;
    double speedup;
    std::string hardware_information;
    std::string status;
};

std::string format_benchmark_report(const BenchmarkReport &report);
void append_benchmark_text(const std::string &text_path, const std::string &report_text);

#endif
