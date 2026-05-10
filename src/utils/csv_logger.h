#ifndef PDC_CSV_LOGGER_H
#define PDC_CSV_LOGGER_H

#include <string>

#include "reporting.h"

void append_benchmark_csv(const std::string &csv_path, const BenchmarkReport &report);

#endif
