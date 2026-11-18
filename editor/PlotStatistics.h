#pragma once
// Statistics of equally weighted, finite frame measurements, never of the peak-thinned history.
// Welford's centred recurrence avoids subtracting two large squared sums (Technometrics, 1962).
#include "PlotSeries.h"

#include <vector>

struct PlotStatistics {
    size_t count = 0, gaps = 0;
    double firstTime = 0, lastTime = 0;
    double minimum = 0, maximum = 0, mean = 0, deviation = 0;
    double lowerEdge = 0, upperEdge = 1;
    bool shortened = false;
    std::vector<size_t> bins;
};

PlotStatistics plotStatistics(const PlotSeries& series, double from, double to, int bins);
