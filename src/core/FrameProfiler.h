#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>

// Fixed storage and no per-frame sorting/allocation. Summaries are requested
// only when displaying a report. CPU phases are disjoint wall-clock times;
// GPU measurements are asynchronous and must not be added to CPU times.
class FrameProfiler {
public:
    using Clock = std::chrono::steady_clock;
    enum Phase : size_t {
        Frame, Simulation, Visibility, TlasGather, Recording,
        FrameFence, HistoryFence, Acquire, Submit, Present, PhaseCount
    };
    using Timings = std::array<double, PhaseCount>;
    struct Sample {
        Timings ms{};
        double draws = 0;
        double visibleProps = 0;
        double tlasInstances = 0;
    };
    struct Summary {
        Sample mean{};
        double p95 = 0;
        double p99 = 0;
        double worst = 0;
        size_t count = 0;
    };
    static constexpr size_t kCapacity = 240;

    static double elapsedMs(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    void clear() { next_ = count_ = 0; }
    void add(const Sample& sample) {
        if (!std::isfinite(sample.ms[Frame]) || sample.ms[Frame] <= 0) return;
        samples_[next_] = sample;
        next_ = (next_ + 1) % kCapacity;
        count_ = std::min(count_ + 1, kCapacity);
    }
    Summary summary() const {
        Summary result;
        result.count = count_;
        if (!count_) return result;
        std::array<double, kCapacity> sorted{};
        for (size_t i = 0; i < count_; ++i) {
            for (size_t phase = 0; phase < PhaseCount; ++phase)
                result.mean.ms[phase] += samples_[i].ms[phase];
            result.mean.draws += samples_[i].draws;
            result.mean.visibleProps += samples_[i].visibleProps;
            result.mean.tlasInstances += samples_[i].tlasInstances;
            sorted[i] = samples_[i].ms[Frame];
        }
        for (double& ms : result.mean.ms) ms /= static_cast<double>(count_);
        result.mean.draws /= static_cast<double>(count_);
        result.mean.visibleProps /= static_cast<double>(count_);
        result.mean.tlasInstances /= static_cast<double>(count_);
        std::sort(sorted.begin(), sorted.begin() + count_);
        // Nearest-rank percentiles: p99 is a slow frame time, not "1% low FPS".
        result.p95 = sorted[static_cast<size_t>(std::ceil(0.95 * count_)) - 1];
        result.p99 = sorted[static_cast<size_t>(std::ceil(0.99 * count_)) - 1];
        result.worst = sorted[count_ - 1];
        return result;
    }

private:
    std::array<Sample, kCapacity> samples_{};
    size_t next_ = 0;
    size_t count_ = 0;
};
