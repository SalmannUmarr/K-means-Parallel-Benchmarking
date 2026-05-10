// Serial baseline point model, adapted from the provided Parallel-K-Means source.
#ifndef SERIAL_BASELINE_POINT_H
#define SERIAL_BASELINE_POINT_H

#include <vector>

class Point {
public:
    Point() : cluster_id(0) {}

    explicit Point(const std::vector<double> &coords) : coordinates(coords), cluster_id(0) {}

    const std::vector<double> &get_coordinates() const {
        return coordinates;
    }

    double get_coord(int index) const {
        return coordinates[index];
    }

    int get_dimensions() const {
        return static_cast<int>(coordinates.size());
    }

    int get_cluster_id() const {
        return cluster_id;
    }

    void set_cluster_id(int new_cluster_id) {
        cluster_id = new_cluster_id;
    }

private:
    std::vector<double> coordinates;
    int cluster_id;
};

#endif
