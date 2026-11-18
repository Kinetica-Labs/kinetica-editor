// Exact sample counts and population standard deviation over the retained, unthinned window.
// Bin intervals are [left, right), except the last one includes the maximum.
#include "PlotStatistics.h"

#include <algorithm>
#include <cmath>

PlotStatistics plotStatistics(const PlotSeries& series, double from, double to, int binCount) {
    PlotStatistics out;
    out.bins.resize(size_t(std::clamp(binCount, 1, 200)));
    const size_t raw = series.firstRaw();
    const size_t first = std::max(raw, series.firstAtOrAfter(from));
    out.shortened = raw > series.firstAtOrAfter(from);
    double m2 = 0;
    for (size_t i = first; i < series.size() && series.time(i) <= to; ++i) {
        const double v = series.value(i);
        if (!std::isfinite(v)) { ++out.gaps; continue; }
        if (out.count++ == 0) {
            out.minimum = out.maximum = v;
            out.firstTime = series.time(i);
        }
        out.lastTime = series.time(i);
        out.minimum = std::min(out.minimum, v);
        out.maximum = std::max(out.maximum, v);
        const double delta = v - out.mean;
        out.mean += delta / double(out.count);
        m2 += delta * (v - out.mean);
    }
    if (!out.count) return out;
    out.deviation = std::sqrt(std::max(0.0, m2 / double(out.count)));
    out.lowerEdge = out.minimum;
    out.upperEdge = out.maximum;
    if (out.minimum == out.maximum) {
        const double pad = std::max(1e-12, std::abs(out.minimum) * 0.05);
        out.lowerEdge -= pad;
        out.upperEdge += pad;
    }
    for (size_t i = first; i < series.size() && series.time(i) <= to; ++i) {
        const double v = series.value(i);
        if (!std::isfinite(v)) continue;
        const double fraction = (v - out.lowerEdge) / (out.upperEdge - out.lowerEdge);
        const size_t bin = std::min(out.bins.size() - 1, size_t(std::max(0.0, fraction) * double(out.bins.size())));
        ++out.bins[bin];
    }
    return out;
}
