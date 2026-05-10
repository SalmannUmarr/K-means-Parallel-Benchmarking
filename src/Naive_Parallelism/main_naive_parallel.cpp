#include <algorithm>
#include <cmath>
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

#include "Cluster.h"
#include "Point.h"
#include "../utils/benchmark_utils.h"
#include "../utils/csv_logger.h"
#include "../utils/power_metrics.h"
#include "../utils/reporting.h"

const int DEFAULT_MAX_ITERATIONS = 20;
const int DEFAULT_CLUSTER_COUNT = 5;

struct Dataset {
    std::vector<Point> points;
    int dimensions;
    int cluster_count;
};

struct KMeansResult {
    int iterations;
};

struct ThreadAccumulation {
    std::vector<std::vector<double>> sums;
    std::vector<int> counts;
};

Dataset load_csv_dataset(const std::string &dataset_path);
std::vector<Cluster> init_cluster(const std::vector<Point> &points, int num_cluster);
double squared_euclidean_dist(const Point &point, const Cluster &cluster);
bool compute_distance_parallel(std::vector<Point> &points,
                               std::vector<Cluster> &clusters,
                               int thread_count);
KMeansResult run_naive_parallel_kmeans(std::vector<Point> &points,
                                       std::vector<Cluster> &clusters,
                                       int max_iterations,
                                       int thread_count);
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
        const int requested_threads = argc >= 3 ? std::atoi(argv[2]) : 1;
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

        std::vector<Cluster> clusters = init_cluster(dataset.points, dataset.cluster_count);

        PowerMetricsSession power_session(dataset_file_name + "_t" + std::to_string(thread_count));
        power_session.start();

        BenchmarkTimer timer;
        const CpuUsageSnapshot cpu_start = capture_cpu_usage();
        timer.start();
        KMeansResult result = run_naive_parallel_kmeans(dataset.points,
                                                        clusters,
                                                        max_iterations,
                                                        thread_count);
        const double execution_time = timer.stop_seconds();
        const CpuUsageSnapshot cpu_end = capture_cpu_usage();
        const double cpu_utilization = process_cpu_utilization_percent(cpu_start, cpu_end, execution_time);
        PowerMetrics power_metrics = power_session.stop_and_collect(execution_time, cpu_utilization);

        const double serial_time = serial_baseline_time_seconds(dataset_file_name);
        const double speedup = serial_time > 0.0 && execution_time > 0.0 ? serial_time / execution_time : 0.0;

        BenchmarkReport report{
            "Naive Parallelism",
            dataset_file_name,
            dataset_tier_label(dataset_file_name),
            static_cast<long long>(dataset.points.size()),
            dataset.dimensions,
            dataset.cluster_count,
            thread_count,
            execution_time,
            result.iterations,
            execution_time > 0.0 ? static_cast<double>(dataset.points.size()) / execution_time : 0.0,
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
        std::cerr << "Naive Parallelism failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

Dataset load_csv_dataset(const std::string &dataset_path) {
    std::ifstream input_file(dataset_path);
    if (!input_file) {
        throw std::runtime_error("Unable to open dataset: " + dataset_path);
    }

    std::string line;
    if (!std::getline(input_file, line)) {
        throw std::runtime_error("Dataset is empty: " + dataset_path);
    }

    int column_count = 1;
    for (char character : line) {
        if (character == ',') {
            ++column_count;
        }
    }

    const int dimensions = column_count - 1;
    if (dimensions <= 0) {
        throw std::runtime_error("Dataset must contain feature columns and a label column");
    }

    Dataset dataset;
    dataset.dimensions = dimensions;
    std::set<int> unique_labels;

    while (std::getline(input_file, line)) {
        if (line.empty()) {
            continue;
        }

        std::vector<double> coords;
        coords.reserve(static_cast<size_t>(dimensions));
        size_t start = 0;

        for (int column = 0; column < column_count; ++column) {
            const size_t comma = line.find(',', start);
            const size_t end = comma == std::string::npos ? line.size() : comma;
            const std::string token = line.substr(start, end - start);

            if (column < dimensions) {
                coords.push_back(std::stod(token));
            } else if (!token.empty()) {
                unique_labels.insert(std::stoi(token));
            }

            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }

        if (static_cast<int>(coords.size()) != dimensions) {
            throw std::runtime_error("Invalid row found in dataset: " + dataset_path);
        }
        dataset.points.emplace_back(coords);
    }

    if (dataset.points.empty()) {
        throw std::runtime_error("No points found in dataset: " + dataset_path);
    }

    dataset.cluster_count = unique_labels.empty() ? DEFAULT_CLUSTER_COUNT : static_cast<int>(unique_labels.size());
    return dataset;
}

std::vector<Cluster> init_cluster(const std::vector<Point> &points, int num_cluster) {
    if (num_cluster <= 0 || num_cluster > static_cast<int>(points.size())) {
        throw std::runtime_error("Invalid number of clusters");
    }

    std::vector<Cluster> clusters;
    clusters.reserve(static_cast<size_t>(num_cluster));
    for (int i = 0; i < num_cluster; ++i) {
        clusters.emplace_back(points[static_cast<size_t>(i)].get_coordinates());
    }
    return clusters;
}

double squared_euclidean_dist(const Point &point, const Cluster &cluster) {
    double distance = 0.0;
    for (int i = 0; i < point.get_dimensions(); ++i) {
        const double diff = point.get_coord(i) - cluster.get_coord(i);
        distance += diff * diff;
    }
    return distance;
}

bool compute_distance_parallel(std::vector<Point> &points,
                               std::vector<Cluster> &clusters,
                               int thread_count) {
    const int point_count = static_cast<int>(points.size());
    const int cluster_count = static_cast<int>(clusters.size());
    const int dimensions = points[0].get_dimensions();
    const int actual_threads = std::min(thread_count, point_count);

    std::vector<ThreadAccumulation> local(actual_threads);
    for (ThreadAccumulation &accumulation : local) {
        accumulation.sums.assign(static_cast<size_t>(cluster_count),
                                 std::vector<double>(static_cast<size_t>(dimensions), 0.0));
        accumulation.counts.assign(static_cast<size_t>(cluster_count), 0);
    }

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(actual_threads));

    for (int thread_id = 0; thread_id < actual_threads; ++thread_id) {
        const int begin = (point_count * thread_id) / actual_threads;
        const int end = (point_count * (thread_id + 1)) / actual_threads;

        workers.emplace_back([&, thread_id, begin, end]() {
            ThreadAccumulation &accumulation = local[static_cast<size_t>(thread_id)];
            for (int i = begin; i < end; ++i) {
                double min_distance = squared_euclidean_dist(points[static_cast<size_t>(i)], clusters[0]);
                int min_index = 0;

                for (int j = 1; j < cluster_count; ++j) {
                    const double distance = squared_euclidean_dist(points[static_cast<size_t>(i)],
                                                                   clusters[static_cast<size_t>(j)]);
                    if (distance < min_distance) {
                        min_distance = distance;
                        min_index = j;
                    }
                }

                points[static_cast<size_t>(i)].set_cluster_id(min_index);
                const std::vector<double> &coords = points[static_cast<size_t>(i)].get_coordinates();
                for (int dim = 0; dim < dimensions; ++dim) {
                    accumulation.sums[static_cast<size_t>(min_index)][static_cast<size_t>(dim)] +=
                        coords[static_cast<size_t>(dim)];
                }
                ++accumulation.counts[static_cast<size_t>(min_index)];
            }
        });
    }

    for (std::thread &worker : workers) {
        worker.join();
    }

    bool conv = false;
    for (int cluster_index = 0; cluster_index < cluster_count; ++cluster_index) {
        std::vector<double> sums(static_cast<size_t>(dimensions), 0.0);
        int count = 0;

        for (const ThreadAccumulation &accumulation : local) {
            count += accumulation.counts[static_cast<size_t>(cluster_index)];
            for (int dim = 0; dim < dimensions; ++dim) {
                sums[static_cast<size_t>(dim)] +=
                    accumulation.sums[static_cast<size_t>(cluster_index)][static_cast<size_t>(dim)];
            }
        }

        if (count == 0) {
            continue;
        }

        std::vector<double> updated(static_cast<size_t>(dimensions), 0.0);
        bool moved = false;
        for (int dim = 0; dim < dimensions; ++dim) {
            updated[static_cast<size_t>(dim)] = sums[static_cast<size_t>(dim)] / count;
            if (std::fabs(clusters[static_cast<size_t>(cluster_index)].get_coord(dim) -
                          updated[static_cast<size_t>(dim)]) > 1e-12) {
                moved = true;
            }
        }

        if (moved) {
            conv = true;
        }
        clusters[static_cast<size_t>(cluster_index)].set_coordinates(updated);
    }

    return conv;
}

KMeansResult run_naive_parallel_kmeans(std::vector<Point> &points,
                                       std::vector<Cluster> &clusters,
                                       int max_iterations,
                                       int thread_count) {
    bool conv = true;
    int iterations = 0;

    while (conv && iterations < max_iterations) {
        ++iterations;
        conv = compute_distance_parallel(points, clusters, thread_count);
    }

    return {iterations};
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

        std::vector<std::string> columns = parse_csv_line(line);
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
    return "results/Naive_Parallelism_Results.csv";
}

std::string default_text_path() {
    return "results/Naive_Parallelism_Results.txt";
}
