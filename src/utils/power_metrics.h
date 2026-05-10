#ifndef PDC_POWER_METRICS_H
#define PDC_POWER_METRICS_H

#include <string>

struct PowerMetrics {
    double cpu_power_w = -1.0;
    double gpu_power_w = -1.0;
    double package_power_w = -1.0;
    double package_energy_j = -1.0;
    double cpu_utilization_percent = -1.0;
    double instructions_per_cycle = -1.0;
    std::string thermals = "N/A";
    std::string status = "N/A";
};

class PowerMetricsSession {
public:
    explicit PowerMetricsSession(const std::string &dataset_name);
    void start();
    PowerMetrics stop_and_collect(double execution_time_seconds,
                                  double fallback_cpu_utilization_percent);

private:
    std::string raw_output_path;
    int sampler_pid;
    bool active;
};

PowerMetrics parse_power_metrics_file(const std::string &raw_output_path,
                                      double execution_time_seconds,
                                      double fallback_cpu_utilization_percent);
bool power_metrics_enabled();
std::string format_metric_or_na(double value, const std::string &suffix, int precision);

#endif
