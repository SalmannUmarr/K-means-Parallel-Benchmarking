#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include "../utils/benchmark_utils.h"
#include "../utils/csv_logger.h"
#include "../utils/power_metrics.h"
#include "../utils/reporting.h"

const int DEFAULT_MAX_ITERATIONS = 20;
const int DEFAULT_CLUSTER_COUNT = 5;
const int DEFAULT_THREAD_COUNT = 8;

struct Dataset {
    std::vector<float> data;
    int points = 0;
    int dimensions = 0;
    int cluster_count = 0;
};

struct KMeansResult {
    int iterations = 0;
};

struct ThreadRange {
    int begin = 0;
    int end = 0;
};

Dataset load_csv_dataset(const std::string &dataset_path);
std::vector<float> initialize_centroids(const Dataset &dataset);
KMeansResult run_optimized_kmeans(const Dataset &dataset,
                                  std::vector<float> &centroids,
                                  int max_iterations,
                                  int thread_count);
float squared_distance_neon(const float *point, const float *centroid, int dimensions);
std::vector<ThreadRange> partition_ranges(int point_count, int thread_count);
double serial_baseline_time_seconds(const std::string &dataset_file_name);
std::vector<std::string> parse_csv_line(const std::string &line);
std::string default_csv_path();
std::string default_text_path();

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <dataset.csv> [threads] [clusters] [max_iterations] [csv_file] [text_file]\n";
        return 1;
    }

    try {
        const std::string dataset_path = argv[1];
        const int requested_threads = argc >= 3 ? std::atoi(argv[2]) : DEFAULT_THREAD_COUNT;
        const int requested_clusters = argc >= 4 ? std::atoi(argv[3]) : 0;
        const int max_iterations = argc >= 5 ? std::atoi(argv[4]) : DEFAULT_MAX_ITERATIONS;
        const std::string csv_path = argc >= 6 ? argv[5] : default_csv_path();
        const std::string text_path = argc >= 7 ? argv[6] : default_text_path();
        const std::string dataset_file_name = std::filesystem::path(dataset_path).filename().string();
        const int thread_count = std::max(1, requested_threads);

        Dataset dataset = load_csv_dataset(dataset_path);
        if (requested_clusters > 0) {
            dataset.cluster_count = requested_clusters;
        }

        std::vector<float> centroids = initialize_centroids(dataset);

        PowerMetricsSession power_session(dataset_file_name + "_optimized");
        power_session.start();

        BenchmarkTimer timer;
        const CpuUsageSnapshot cpu_start = capture_cpu_usage();
        timer.start();
        KMeansResult result = run_optimized_kmeans(dataset, centroids, max_iterations, thread_count);
        const double execution_time = timer.stop_seconds();
        const CpuUsageSnapshot cpu_end = capture_cpu_usage();
        const double cpu_utilization = process_cpu_utilization_percent(cpu_start, cpu_end, execution_time);
        PowerMetrics power_metrics = power_session.stop_and_collect(execution_time, cpu_utilization);

        const double serial_time = serial_baseline_time_seconds(dataset_file_name);
        const double speedup = serial_time > 0.0 && execution_time > 0.0 ? serial_time / execution_time : 0.0;

        BenchmarkReport report{
            "Optimized Parallelism",
            dataset_file_name,
            dataset_tier_label(dataset_file_name),
            static_cast<long long>(dataset.points),
            dataset.dimensions,
            dataset.cluster_count,
            thread_count,
            execution_time,
            result.iterations,
            execution_time > 0.0 ? static_cast<double>(dataset.points) / execution_time : 0.0,
            peak_memory_usage_mb(),
            power_metrics,
            speedup,
            hardware_information(),
            "SUCCESS"};

        const std::string report_text = format_benchmark_report(report);
        std::cout << report_text << '\n';
        append_benchmark_text(text_path, report_text);
        append_benchmark_csv(csv_path, report);
    } catch (const std::exception &error) {
        std::cerr << "Optimized Parallelism failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

Dataset load_csv_dataset(const std::string &dataset_path) {
    std::ifstream input_file(dataset_path);
    if (!input_file) {
        throw std::runtime_error("Unable to open dataset: " + dataset_path);
    }

    std::string header;
    if (!std::getline(input_file, header)) {
        throw std::runtime_error("Dataset is empty: " + dataset_path);
    }

    int column_count = 1;
    for (char character : header) {
        if (character == ',') {
            ++column_count;
        }
    }

    Dataset dataset;
    dataset.dimensions = column_count - 1;
    if (dataset.dimensions <= 0) {
        throw std::runtime_error("Dataset must contain features and a label column");
    }

    std::set<int> labels;
    std::string line;
    while (std::getline(input_file, line)) {
        if (line.empty()) {
            continue;
        }

        const char *cursor = line.c_str();
        char *next = nullptr;
        for (int dim = 0; dim < dataset.dimensions; ++dim) {
            const float value = std::strtof(cursor, &next);
            dataset.data.push_back(value);
            cursor = (*next == ',') ? next + 1 : next;
        }

        const int label = static_cast<int>(std::strtol(cursor, &next, 10));
        labels.insert(label);
        ++dataset.points;
    }

    if (dataset.points == 0) {
        throw std::runtime_error("No points found in dataset: " + dataset_path);
    }

    dataset.cluster_count = labels.empty() ? DEFAULT_CLUSTER_COUNT : static_cast<int>(labels.size());
    return dataset;
}

std::vector<float> initialize_centroids(const Dataset &dataset) {
    if (dataset.cluster_count <= 0 || dataset.cluster_count > dataset.points) {
        throw std::runtime_error("Invalid number of clusters");
    }

    std::vector<float> centroids(static_cast<size_t>(dataset.cluster_count * dataset.dimensions));
    std::copy(dataset.data.begin(),
              dataset.data.begin() + static_cast<ptrdiff_t>(dataset.cluster_count * dataset.dimensions),
              centroids.begin());
    return centroids;
}

KMeansResult run_optimized_kmeans(const Dataset &dataset,
                                  std::vector<float> &centroids,
                                  int max_iterations,
                                  int thread_count) {
    const int points = dataset.points;
    const int dimensions = dataset.dimensions;
    const int clusters = dataset.cluster_count;
    const int actual_threads = std::min(std::max(1, thread_count), points);
    const std::vector<ThreadRange> ranges = partition_ranges(points, actual_threads);

    std::vector<int> assignments(static_cast<size_t>(points), -1);
    std::vector<float> next_centroids(static_cast<size_t>(clusters * dimensions), 0.0f);
    std::vector<int> counts(static_cast<size_t>(clusters), 0);

    // Per-thread buffers eliminate atomics during the hot assignment loop.
    std::vector<float> local_sums(static_cast<size_t>(actual_threads * clusters * dimensions), 0.0f);
    std::vector<int> local_counts(static_cast<size_t>(actual_threads * clusters), 0);

    int iterations = 0;
    bool changed = true;

    while (changed && iterations < max_iterations) {
        ++iterations;
        std::atomic<bool> assignment_changed(false);
        changed = false;

        std::fill(local_sums.begin(), local_sums.end(), 0.0f);
        std::fill(local_counts.begin(), local_counts.end(), 0);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(actual_threads));

        for (int thread_id = 0; thread_id < actual_threads; ++thread_id) {
            workers.emplace_back([&, thread_id]() {
                const ThreadRange range = ranges[static_cast<size_t>(thread_id)];
                float *thread_sums = local_sums.data() +
                                     static_cast<size_t>(thread_id * clusters * dimensions);
                int *thread_counts = local_counts.data() +
                                     static_cast<size_t>(thread_id * clusters);

                for (int point_index = range.begin; point_index < range.end; ++point_index) {
                    const float *point = dataset.data.data() +
                                         static_cast<size_t>(point_index * dimensions);
                    int best_cluster = 0;
                    float best_distance = squared_distance_neon(point, centroids.data(), dimensions);

                    for (int cluster_index = 1; cluster_index < clusters; ++cluster_index) {
                        const float *centroid = centroids.data() +
                                                static_cast<size_t>(cluster_index * dimensions);
                        const float distance = squared_distance_neon(point, centroid, dimensions);
                        if (distance < best_distance) {
                            best_distance = distance;
                            best_cluster = cluster_index;
                        }
                    }

                    if (assignments[static_cast<size_t>(point_index)] != best_cluster) {
                        assignment_changed.store(true, std::memory_order_relaxed);
                        assignments[static_cast<size_t>(point_index)] = best_cluster;
                    }

                    float *sum = thread_sums + static_cast<size_t>(best_cluster * dimensions);
                    for (int dim = 0; dim < dimensions; ++dim) {
                        sum[dim] += point[dim];
                    }
                    ++thread_counts[best_cluster];
                }
            });
        }

        for (std::thread &worker : workers) {
            worker.join();
        }

        std::fill(next_centroids.begin(), next_centroids.end(), 0.0f);
        std::fill(counts.begin(), counts.end(), 0);

        for (int thread_id = 0; thread_id < actual_threads; ++thread_id) {
            const float *thread_sums = local_sums.data() +
                                       static_cast<size_t>(thread_id * clusters * dimensions);
            const int *thread_counts = local_counts.data() +
                                       static_cast<size_t>(thread_id * clusters);

            for (int cluster_index = 0; cluster_index < clusters; ++cluster_index) {
                counts[static_cast<size_t>(cluster_index)] += thread_counts[cluster_index];
                float *destination = next_centroids.data() +
                                     static_cast<size_t>(cluster_index * dimensions);
                const float *source = thread_sums + static_cast<size_t>(cluster_index * dimensions);
                for (int dim = 0; dim < dimensions; ++dim) {
                    destination[dim] += source[dim];
                }
            }
        }

        changed = assignment_changed.load(std::memory_order_relaxed);

        for (int cluster_index = 0; cluster_index < clusters; ++cluster_index) {
            const int count = counts[static_cast<size_t>(cluster_index)];
            if (count == 0) {
                continue;
            }

            float *centroid = centroids.data() + static_cast<size_t>(cluster_index * dimensions);
            const float *sum = next_centroids.data() + static_cast<size_t>(cluster_index * dimensions);
            const float inv_count = 1.0f / static_cast<float>(count);
            for (int dim = 0; dim < dimensions; ++dim) {
                const float updated = sum[dim] * inv_count;
                if (std::fabs(centroid[dim] - updated) > 1e-6f) {
                    changed = true;
                }
                centroid[dim] = updated;
            }
        }
    }

    return {iterations};
}

float squared_distance_neon(const float *point, const float *centroid, int dimensions) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t accum = vdupq_n_f32(0.0f);
    int dim = 0;
    for (; dim + 4 <= dimensions; dim += 4) {
        const float32x4_t p = vld1q_f32(point + dim);
        const float32x4_t c = vld1q_f32(centroid + dim);
        const float32x4_t diff = vsubq_f32(p, c);
        accum = vfmaq_f32(accum, diff, diff);
    }

    float result = vaddvq_f32(accum);
    for (; dim < dimensions; ++dim) {
        const float diff = point[dim] - centroid[dim];
        result += diff * diff;
    }
    return result;
#else
    float result = 0.0f;
    for (int dim = 0; dim < dimensions; ++dim) {
        const float diff = point[dim] - centroid[dim];
        result += diff * diff;
    }
    return result;
#endif
}

std::vector<ThreadRange> partition_ranges(int point_count, int thread_count) {
    std::vector<ThreadRange> ranges(static_cast<size_t>(thread_count));
    for (int thread_id = 0; thread_id < thread_count; ++thread_id) {
        ranges[static_cast<size_t>(thread_id)] = {
            (point_count * thread_id) / thread_count,
            (point_count * (thread_id + 1)) / thread_count};
    }
    return ranges;
}

double serial_baseline_time_seconds(const std::string &dataset_file_name) {
    std::ifstream csv("results/Serial_Baseline_Results.csv");
    if (!csv) {
        return 0.0;
    }

    std::string line;
    std::getline(csv, line);
    while (std::getline(csv, line)) {
        if (line.find(dataset_file_name) == std::string::npos) {
            continue;
        }

        const std::vector<std::string> columns = parse_csv_line(line);
        if (columns.size() > 7) {
            try {
                return std::stod(columns[7]);
            } catch (const std::exception &) {
                return 0.0;
            }
        }
    }
    return 0.0;
}

std::vector<std::string> parse_csv_line(const std::string &line) {
    std::vector<std::string> columns;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];
        if (character == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (character == ',' && !in_quotes) {
            columns.push_back(current);
            current.clear();
        } else {
            current += character;
        }
    }
    columns.push_back(current);
    return columns;
}

std::string default_csv_path() {
    return "results/Optimized_Parallelism_Results.csv";
}

std::string default_text_path() {
    return "results/Optimized_Parallelism_Results.txt";
}
