CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET := naive_parallel_kmeans
UTILS := ../utils/benchmark_utils.cpp ../utils/power_metrics.cpp ../utils/reporting.cpp ../utils/csv_logger.cpp
SOURCES := main_naive_parallel.cpp $(UTILS)

.PHONY: all clean run-all

all: $(TARGET)

$(TARGET): $(SOURCES) Point.h Cluster.h ../utils/benchmark_utils.h ../utils/power_metrics.h ../utils/reporting.h ../utils/csv_logger.h
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run-all: $(TARGET)
	./run_naive_parallel_benchmarks.sh

clean:
	rm -f $(TARGET)
