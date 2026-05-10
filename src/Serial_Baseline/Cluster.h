// Serial baseline cluster model, adapted from the provided Parallel-K-Means source.
#ifndef SERIAL_BASELINE_CLUSTER_H
#define SERIAL_BASELINE_CLUSTER_H

#include <cmath>
#include <vector>

#include "Point.h"

class Cluster {
public:
    Cluster() : size(0) {}

    explicit Cluster(const std::vector<double> &coords)
        : coordinates(coords), new_coordinates(coords.size(), 0.0), size(0) {}

    void add_point(const Point &point) {
        const std::vector<double> &point_coords = point.get_coordinates();
        for (int i = 0; i < static_cast<int>(new_coordinates.size()); ++i) {
            new_coordinates[i] += point_coords[i];
        }
        ++size;
    }

    void free_point() {
        size = 0;
        std::fill(new_coordinates.begin(), new_coordinates.end(), 0.0);
    }

    double get_coord(int index) const {
        return coordinates[index];
    }

    const std::vector<double> &get_coordinates() const {
        return coordinates;
    }

    bool update_coords() {
        if (size == 0) {
            return false;
        }

        bool moved = false;
        for (int i = 0; i < static_cast<int>(coordinates.size()); ++i) {
            const double updated_coord = new_coordinates[i] / size;
            if (std::fabs(coordinates[i] - updated_coord) > 1e-12) {
                moved = true;
            }
            coordinates[i] = updated_coord;
        }

        return moved;
    }

private:
    std::vector<double> coordinates;
    std::vector<double> new_coordinates;
    int size;
};

#endif
