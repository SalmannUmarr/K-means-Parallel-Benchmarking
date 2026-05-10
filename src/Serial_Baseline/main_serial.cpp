#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
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

Dataset load_csv_dataset(const std::string &dataset_path);
std::vector<Cluster> init_cluster(const std::vector<Point> &points, int num_cluster);
void compute_distance(std::vector<Point> &points, std::vector<Cluster> &clusters);
double squared_euclidean_dist(const Point &point, const Cluster &cluster);
bool update_clusters(std::vector<Cluster> &clusters);
KMeansResult run_serial_kmeans(std::vector<Point> &points, std::vector<Cluster> &clusters, int max_iterations);
std::string default_csv_path();
std::string default_text_path();

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dataset.csv> [clusters] [max_iterations] [csv_file] [text_file]\n";
        return 1;
    }

    try {
        const std::string dataset_path = argv[1];
        const int requested_clusters = argc >= 3 ? std::atoi(argv[2]) : 0;
        const int max_iterations = argc >= 4 ? std::atoi(argv[3]) : DEFAULT_MAX_ITERATIONS;
        const std::string csv_path = argc >= 5 ? argv[4] : default_csv_path();
        const std::string text_path = argc >= 6 ? argv[5] : default_text_path();
        const std::string dataset_file_name = std::filesystem::path(dataset_path).filename().string();

        Dataset dataset = load_csv_dataset(dataset_path);
        if (requested_clusters > 0) {
            dataset.cluster_count = requested_clusters;
        }

        std::vector<Cluster> clusters = init_cluster(dataset.points, dataset.cluster_count);

        PowerMetricsSession power_session(dataset_file_name);
        power_session.start();

        BenchmarkTimer timer;
        const CpuUsageSnapshot cpu_start = capture_cpu_usage();
        timer.start();
        KMeansResult result = run_serial_kmeans(dataset.points, clusters, max_iterations);
        const double execution_time = timer.stop_seconds();
        const CpuUsageSnapshot cpu_end = capture_cpu_usage();
        const double cpu_utilization = process_cpu_utilization_percent(cpu_start, cpu_end, execution_time);
        PowerMetrics power_metrics = power_session.stop_and_collect(execution_time, cpu_utilization);

        BenchmarkReport report{
            "Serial Baseline",
            dataset_file_name,
            dataset_tier_label(dataset_file_name),
            static_cast<long long>(dataset.points.size()),
            dataset.dimensions,
            dataset.cluster_count,
            1,
            execution_time,
            result.iterations,
            execution_time > 0.0 ? static_cast<double>(dataset.points.size()) / execution_time : 0.0,
            peak_memory_usage_mb(),
            power_metrics,
            0.0,
            hardware_information(),
            "SUCCESS"};

        const std::string report_text = format_benchmark_report(report);
        std::cout << report_text << '\n';
        append_benchmark_text(text_path, report_text);
        append_benchmark_csv(csv_path, report);
    } catch (const std::exception &error) {
        std::cerr << "Serial baseline failed: " << error.what() << '\n';
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

    // Deterministic initialization keeps benchmark runs comparable.
    for (int i = 0; i < num_cluster; ++i) {
        clusters.emplace_back(points[static_cast<size_t>(i)].get_coordinates());
    }

    return clusters;
}

void compute_distance(std::vector<Point> &points, std::vector<Cluster> &clusters) {
    const int clusters_size = static_cast<int>(clusters.size());

    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        double min_distance = squared_euclidean_dist(points[i], clusters[0]);
        int min_index = 0;

        for (int j = 1; j < clusters_size; ++j) {
            const double distance = squared_euclidean_dist(points[i], clusters[j]);
            if (distance < min_distance) {
                min_distance = distance;
                min_index = j;
            }
        }

        points[i].set_cluster_id(min_index);
        clusters[min_index].add_point(points[i]);
    }
}

double squared_euclidean_dist(const Point &point, const Cluster &cluster) {
    double distance = 0.0;
    for (int i = 0; i < point.get_dimensions(); ++i) {
        const double diff = point.get_coord(i) - cluster.get_coord(i);
        distance += diff * diff;
    }
    return distance;
}

bool update_clusters(std::vector<Cluster> &clusters) {
    bool conv = false;

    for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
        if (clusters[i].update_coords()) {
            conv = true;
        }
        clusters[i].free_point();
    }

    return conv;
}

KMeansResult run_serial_kmeans(std::vector<Point> &points, std::vector<Cluster> &clusters, int max_iterations) {
    bool conv = true;
    int iterations = 0;

    while (conv && iterations < max_iterations) {
        ++iterations;
        compute_distance(points, clusters);
        conv = update_clusters(clusters);
    }

    return {iterations};
}

std::string default_csv_path() {
    return "results/Serial_Baseline_Results.csv";
}

std::string default_text_path() {
    return "results/Serial_Baseline_Results.txt";
}
