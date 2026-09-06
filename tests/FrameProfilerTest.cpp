#include "core/FrameProfiler.h"

#include <limits>
#include <stdexcept>

void require(bool condition) {
    if (!condition) throw std::runtime_error("FrameProfiler regression");
}

int main() {
    FrameProfiler profiler;
    require(profiler.summary().count == 0);

    // Descending input checks percentile sorting, not insertion order.
    for (int i = 100; i >= 1; --i) {
        FrameProfiler::Sample sample;
        sample.ms[FrameProfiler::Frame] = i;
        sample.ms[FrameProfiler::Simulation] = 2;
        sample.draws = 12;
        sample.visibleProps = 20;
        sample.tlasInstances = 30;
        profiler.add(sample);
    }
    auto summary = profiler.summary();
    require(summary.count == 100 && summary.mean.ms[FrameProfiler::Frame] == 50.5);
    require(summary.p95 == 95 && summary.p99 == 99 && summary.worst == 100);
    require(summary.mean.ms[FrameProfiler::Simulation] == 2 && summary.mean.draws == 12);
    require(summary.mean.visibleProps == 20 && summary.mean.tlasInstances == 30);

    profiler.clear();
    require(profiler.summary().count == 0 && profiler.summary().p99 == 0);
    for (int i = 1; i <= 300; ++i) {
        FrameProfiler::Sample sample;
        sample.ms[FrameProfiler::Frame] = i;
        profiler.add(sample);
    }
    summary = profiler.summary();
    require(summary.count == 240 && summary.mean.ms[FrameProfiler::Frame] == 180.5);
    require(summary.p95 == 288 && summary.p99 == 298 && summary.worst == 300);

    profiler.clear();
    FrameProfiler::Sample sample;
    profiler.add(sample);
    sample.ms[FrameProfiler::Frame] = std::numeric_limits<double>::quiet_NaN();
    profiler.add(sample);
    require(profiler.summary().count == 0);
    sample.ms[FrameProfiler::Frame] = 16;
    profiler.add(sample);
    summary = profiler.summary();
    require(summary.count == 1 && summary.p95 == 16 && summary.p99 == 16);
}
