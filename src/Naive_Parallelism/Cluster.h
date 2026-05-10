// Cluster model kept close to the Serial Baseline, with one setter used after
// reducing per-thread centroid sums in the naive parallel implementation.
#ifndef NAIVE_PARALLELISM_CLUSTER_H
#define NAIVE_PARALLELISM_CLUSTER_H

#include <vector>

class Cluster {
public:
    Cluster() {}

    explicit Cluster(const std::vector<double> &coords) : coordinates(coords) {}

    double get_coord(int index) const {
        return coordinates[index];
    }

    const std::vector<double> &get_coordinates() const {
        return coordinates;
    }

    void set_coordinates(const std::vector<double> &coords) {
        coordinates = coords;
    }

private:
    std::vector<double> coordinates;
};

#endif
