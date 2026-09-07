#include "HydraulicErosion.h"

#include <algorithm>
#include <barrier>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace HydraulicErosion {
namespace {
constexpr double kGravity = 9.81;
constexpr double kDryDepth = 1e-8;
constexpr int kDx[4]{-1, 1, 0, 0}, kDz[4]{0, 0, -1, 1}, kOpposite[4]{1, 0, 3, 2};
using Flux = std::array<double, 4>;
bool range(double x, double low, double high) { return std::isfinite(x) && x >= low && x <= high; }

// One bounded team for all passes. Each job owns complete rows; barriers
// separate neighbour reads from the next mutation. This is probe-only today;
// game integration must schedule it under the same CPU budget as tree builds.
class Rows {
public:
    Rows(int rows, int workers) : rows_(rows), workers_(workers), barrier_(workers) {
        try {
            for (int id = 1; id < workers_; ++id) threads_.emplace_back([this, id] {
                for (;;) {
                    barrier_.arrive_and_wait();
                    if (stopping_) return;
                    execute(id);
                    barrier_.arrive_and_wait();
                }
            });
        } catch (...) {
            stopping_ = true;
            for (int id = int(threads_.size()) + 1; id < workers_; ++id) barrier_.arrive_and_drop();
            barrier_.arrive_and_wait();
            for (auto& thread : threads_) thread.join();
            throw;
        }
    }
    ~Rows() {
        stopping_ = true;
        barrier_.arrive_and_wait();
        for (auto& thread : threads_) thread.join();
    }
    template<class F> void run(F&& job) {
        if (workers_ == 1) { for (int z = 0; z < rows_; ++z) job(z); return; }
        job_ = std::forward<F>(job);
        barrier_.arrive_and_wait();
        execute(0);
        barrier_.arrive_and_wait();
    }
private:
    void execute(int id) { for (int z = rows_ * id / workers_; z < rows_ * (id + 1) / workers_; ++z) job_(z); }
    int rows_, workers_;
    bool stopping_ = false;
    std::barrier<> barrier_;
    std::vector<std::thread> threads_;
    std::function<void(int)> job_;
};
struct RowBudget { double rain = 0, infiltration = 0, evaporation = 0, waterExport = 0, sedimentExport = 0, speed = 0; };
}

size_t Result::payloadBytes() const {
    return (water.capacity() + sediment.capacity() + erosion.capacity() + deposition.capacity() +
            waterExposure.capacity() + throughflow.capacity() + relaxation.capacity()) * sizeof(double);
}

static Result simulate(MacroTerrain::Fields& fields, const Settings& s, const InitialState& initial) {
    int n = fields.heightmap.resolution;
    if (n < 2 || n > 4097 || !range(fields.spacing, .0001, 1000000) ||
        !std::isfinite(fields.heightmap.worldSize) ||
        std::abs(double(fields.heightmap.worldSize) - double(fields.spacing) * (n - 1)) >
            1e-6 * std::max(1.0, double(fields.heightmap.worldSize)))
        throw std::invalid_argument("invalid erosion grid");
    size_t count = size_t(n) * n;
    if (fields.heightmap.heights.size() != count || fields.bedrock.size() != count ||
        fields.soil.size() != count || fields.erodibility.size() != count || fields.openFaces.size() != count)
        throw std::invalid_argument("erosion field dimensions disagree");
    if (!range(s.duration, 0, 1000) || !range(s.rainDuration, 0, 1000) ||
        !range(s.maxTimestep, .000001, 1) || !range(s.cfl, .01, .5) ||
        s.maxSteps < 1 || s.maxSteps > 100000 || !range(s.rainfall, 0, 1) ||
        !range(s.infiltration, 0, 1) || !range(s.evaporation, 0, 1) || !range(s.drag, 0, 100) ||
        !range(s.capacity, 0, 100) || !range(s.maxConcentration, 0, 1) || !range(s.erosionRate, 0, 10) || !range(s.depositionRate, 0, 10) ||
        !range(s.bedrockRate, 0, 1) || !range(s.maxChangeRate, 0, 1) ||
        s.talusPasses < 0 || s.talusPasses > 100 || !range(s.talusSlope, 0, 10) ||
        !range(s.talusFraction, 0, .25) || !range(s.maxTalusDepth, 0, 1) || s.workers < 1 || s.workers > 4)
        throw std::invalid_argument("invalid erosion settings");
    for (const auto* values : {&initial.water, &initial.sediment}) {
        if (!values->empty() && values->size() != count) throw std::invalid_argument("invalid initial erosion field size");
        for (double value : *values)
            if (!range(value, 0, 1000)) throw std::invalid_argument("invalid initial water or sediment depth");
    }
    double dx = fields.spacing;
    Result result;
    std::vector<double> area(count), rock(count), soil(count), available(count), nextWater(count), nextSediment(count);
    std::vector<Flux> flux(count), nextFlux(count);
    std::vector<RowBudget> rows(size_t(n), RowBudget{});
    for (auto* values : {&result.water, &result.sediment, &result.erosion, &result.deposition,
                         &result.waterExposure, &result.throughflow, &result.relaxation}) values->resize(count, 0);
    if (!initial.water.empty()) result.water = initial.water;
    if (!initial.sediment.empty()) result.sediment = initial.sediment;
    auto& budget = result.budget;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            size_t i = size_t(z) * n + x;
            if (!range(fields.bedrock[i], -1000000, 1000000) || !range(fields.soil[i], 0, 1000) ||
                !range(fields.erodibility[i], 0, 1) || !std::isfinite(fields.heightmap.heights[i]) ||
                fields.heightmap.heights[i] != fields.bedrock[i] + fields.soil[i])
                throw std::invalid_argument("invalid erosion material column");
            int boundary = (x == 0 ? 1 : 0) | (x == n - 1 ? 2 : 0) | (z == 0 ? 4 : 0) | (z == n - 1 ? 8 : 0);
            if (fields.openFaces[i] & ~boundary) throw std::invalid_argument("erosion outlet is not an exterior face");
            area[i] = dx * dx * (x == 0 || x == n - 1 ? .5 : 1) * (z == 0 || z == n - 1 ? .5 : 1);
            rock[i] = fields.bedrock[i];
            soil[i] = fields.soil[i];
            budget.initialSoil += soil[i] * area[i];
            budget.initialWater += result.water[i] * area[i];
            budget.initialSediment += result.sediment[i] * area[i];
        }
    }
    result.peakWorkingBytes = result.payloadBytes() + count * (6 * sizeof(double) + 2 * sizeof(Flux)) + rows.capacity() * sizeof(RowBudget);
    auto neighbour = [&](int x, int z, int direction) -> int {
        int nx = x + kDx[direction], nz = z + kDz[direction];
        return nx >= 0 && nx < n && nz >= 0 && nz < n ? nz * n + nx : -1;
    };
    double maxSpeed = 0;
    Rows executor(n, s.workers);
    while (result.simulatedSeconds < s.duration) {
        if (result.steps >= s.maxSteps)
            throw std::runtime_error("erosion exceeded step limit at " + std::to_string(result.simulatedSeconds) +
                                     " seconds; maximum speed " + std::to_string(maxSpeed));
        double maxWater = *std::max_element(result.water.begin(), result.water.end()) + s.rainfall * s.maxTimestep;
        // Half-width boundary cells set the most restrictive spatial scale.
        double dt = std::min({s.maxTimestep, s.cfl * dx * .5 / (std::sqrt(kGravity * maxWater) + maxSpeed + 1e-12),
                              s.duration - result.simulatedSeconds});
        if (!std::isfinite(dt) || dt <= 0 || (dt < 1e-9 && s.duration - result.simulatedSeconds > 1e-9))
            throw std::runtime_error("erosion timestep became unstable at " + std::to_string(result.simulatedSeconds) +
                                     " seconds; speed " + std::to_string(maxSpeed) + ", water " + std::to_string(maxWater));
        double rainTime = std::clamp(s.rainDuration - result.simulatedSeconds, 0.0, dt);
        executor.run([&](int z) {
          rows[z] = {};
          for (size_t i = size_t(z) * n; i < size_t(z + 1) * n; ++i) {
            double rain = s.rainfall * rainTime;
            double water = result.water[i] + rain;
            double infiltration = std::min(water, s.infiltration * dt);
            water -= infiltration;
            double evaporation = std::min(water, s.evaporation * dt);
            double remaining = water - evaporation;
            // Discharge carries momentum through a wet cross-section. When
            // that section shrinks, reduce its old discharge proportionally;
            // retaining it through a drying cell would create infinite speed
            // and collapse the CFL timestep despite the water-volume limiter.
            double momentumScale = available[i] > kDryDepth ? std::min(1.0, remaining / available[i]) : 0;
            for (double& q : flux[i]) q *= momentumScale;
            available[i] = remaining;
            // Losses concentrate solids. Settle the excess before advection,
            // so every donor and all mixtures stay within the concentration cap.
            double settle = std::max(0.0, result.sediment[i] - s.maxConcentration * remaining);
            result.sediment[i] -= settle;
            soil[i] += settle;
            result.deposition[i] += settle;
            rows[z].rain += rain * area[i];
            rows[z].infiltration += infiltration * area[i];
            rows[z].evaporation += evaporation * area[i];
          }
        });
        // Each vertex owns its outgoing pipes. Limit their combined volume to
        // available water, then gather neighbours in a separate pass.
        executor.run([&](int z) {
            for (int x = 0; x < n; ++x) {
                size_t i = size_t(z) * n + x;
                double head = rock[i] + soil[i] + available[i], sum = 0;
                for (int d = 0; d < 4; ++d) {
                    int j = neighbour(x, z, d);
                    double width = dx * ((d < 2 ? z == 0 || z == n - 1 : x == 0 || x == n - 1) ? .5 : 1);
                    bool open = j < 0 && (fields.openFaces[i] & (1 << d));
                    double otherHead = j >= 0 ? rock[j] + soil[j] + available[j] : rock[i] + soil[i];
                    double acceleration = kGravity * available[i] * width * (head - otherHead) / (open ? dx * .5 : dx);
                    nextFlux[i][d] = j >= 0 || open ? std::max(0.0, (flux[i][d] + dt * acceleration) / (1 + s.drag * dt)) : 0;
                    sum += nextFlux[i][d];
                }
                double scale = sum > 0 ? std::min(1.0, available[i] * area[i] / (dt * sum)) : 0;
                for (double& q : nextFlux[i]) q *= scale;
            }
        });
        executor.run([&](int z) {
            for (int x = 0; x < n; ++x) {
                size_t i = size_t(z) * n + x;
                double out = 0, in = 0, sedimentIn = 0;
                double concentration = available[i] > 0 ? result.sediment[i] / available[i] : 0;
                for (int d = 0; d < 4; ++d) {
                    int j = neighbour(x, z, d);
                    out += nextFlux[i][d];
                    if (j >= 0) {
                        double q = nextFlux[j][kOpposite[d]];
                        in += q;
                        if (available[j] > 0) sedimentIn += q * (result.sediment[j] / available[j]);
                    } else {
                        rows[z].waterExport += nextFlux[i][d] * dt;
                        rows[z].sedimentExport += nextFlux[i][d] * concentration * dt;
                    }
                }
                // Clamp only cancellation roundoff; the donor limiter already
                // guarantees positivity for both conserved transported fields.
                nextWater[i] = std::max(0.0, available[i] - out * dt / area[i]) + in * dt / area[i];
                nextSediment[i] = std::max(0.0, result.sediment[i] - out * concentration * dt / area[i]) + sedimentIn * dt / area[i];
                result.throughflow[i] += out * dt / area[i];
            }
        });
        maxSpeed = 0;
        double erodeFraction = 1 - std::exp(-s.erosionRate * dt);
        double depositFraction = 1 - std::exp(-s.depositionRate * dt);
        // All capacity/slope calculations read the old ground. Exchanges write
        // scratch rock/soil deltas first so traversal order cannot alter slopes.
        executor.run([&](int z) {
            for (int x = 0; x < n; ++x) {
                size_t i = size_t(z) * n + x;
                Flux incomingVelocity{}, outgoingVelocity{};
                for (int d = 0; d < 4; ++d) {
                    int j = neighbour(x, z, d);
                    double width = dx * ((d < 2 ? z == 0 || z == n - 1 : x == 0 || x == n - 1) ? .5 : 1);
                    if (j >= 0 && available[j] > kDryDepth)
                        incomingVelocity[d] = nextFlux[j][kOpposite[d]] / (available[j] * width);
                    if (available[i] > kDryDepth)
                        outgoingVelocity[d] = nextFlux[i][d] / (available[i] * width);
                    rows[z].speed = std::max(rows[z].speed, outgoingVelocity[d]);
                }
                // Use the donating wet cross-section for each face velocity.
                // Dividing incoming discharge by a newly wetted receiver's
                // vanishing depth invents a dx/dt velocity at the wet front.
                double vx = .5 * (outgoingVelocity[1] - outgoingVelocity[0] + incomingVelocity[0] - incomingVelocity[1]);
                double vz = .5 * (outgoingVelocity[3] - outgoingVelocity[2] + incomingVelocity[2] - incomingVelocity[3]);
                double speed = std::sqrt(vx * vx + vz * vz);
                rows[z].speed = std::max(rows[z].speed, speed);
                // Face drops detect cell-scale extrema that a centred gradient
                // would miss (alternating high/low cells have zero centred
                // gradient). Never erode a column below its lowest neighbour
                // in one exchange; flat basin floors can still receive deposits.
                double drop = 0;
                double downhill[2]{};
                for (int d = 0; d < 4; ++d) {
                    int j = neighbour(x, z, d);
                    if (j >= 0) {
                        double difference = rock[i] + soil[i] - rock[j] - soil[j];
                        drop = std::max(drop, difference);
                        downhill[d / 2] = std::max(downhill[d / 2], difference);
                    }
                }
                // Combine downhill derivatives on both axes. This preserves
                // planar slope magnitude while giving basin minima zero
                // carrying capacity, allowing deposits to fill them smoothly.
                double slope = std::sqrt(downhill[0] * downhill[0] + downhill[1] * downhill[1]) / dx;
                double capacity = std::min(s.maxConcentration * nextWater[i],
                    s.capacity * speed * nextWater[i] * slope);
                double eroded = 0, deposited = 0, rockEroded = 0;
                if (nextWater[i] <= kDryDepth) {
                    // Complete dry settlement preserves solids even when it
                    // exceeds the ordinary per-second exchange cap.
                    deposited = nextSediment[i];
                } else if (nextSediment[i] > capacity) {
                    deposited = std::min((nextSediment[i] - capacity) * depositFraction, s.maxChangeRate * dt);
                } else {
                    double requested = std::min({(capacity - nextSediment[i]) * erodeFraction, s.maxChangeRate * dt, .5 * drop});
                    eroded = std::min(soil[i], requested);
                    rockEroded = std::min(requested - eroded, s.bedrockRate * fields.erodibility[i] * dt);
                }
                nextSediment[i] += eroded + rockEroded - deposited;
                result.erosion[i] += eroded + rockEroded;
                result.deposition[i] += deposited;
                result.waterExposure[i] += nextWater[i] * dt;
                // Reuse the old flux buffer for material deltas; nextFlux is
                // still the sole source of flow during this entire pass.
                flux[i][0] = deposited - eroded;
                flux[i][1] = -rockEroded;
            }
        });
        executor.run([&](int z) {
            for (size_t i = size_t(z) * n; i < size_t(z + 1) * n; ++i) {
                soil[i] += flux[i][0];
                rock[i] += flux[i][1];
            }
        });
        for (const auto& row : rows) {
            budget.rainfall += row.rain;
            budget.infiltration += row.infiltration;
            budget.evaporation += row.evaporation;
            budget.exportedWater += row.waterExport;
            budget.exportedSediment += row.sedimentExport;
            maxSpeed = std::max(maxSpeed, row.speed);
        }
        flux.swap(nextFlux);
        result.water.swap(nextWater);
        result.sediment.swap(nextSediment);
        result.simulatedSeconds += dt;
        ++result.steps;
    }

    // Limited mobile-soil relaxation: conservative pair-volume transfers,
    // closed at the outer boundary, with no bedrock smoothing.
    for (int pass = 0; pass < s.talusPasses; ++pass) {
        executor.run([&](int z) {
            for (int x = 0; x < n; ++x) {
                size_t i = size_t(z) * n + x;
                double sum = 0;
                for (int d = 0; d < 4; ++d) {
                    int j = neighbour(x, z, d);
                    double excess = j >= 0 ? rock[i] + soil[i] - rock[j] - soil[j] - s.talusSlope * dx : 0;
                    flux[i][d] = j >= 0 ? std::max(0.0, excess) * s.talusFraction / (1 / area[i] + 1 / area[j]) : 0;
                    sum += flux[i][d];
                }
                double scale = sum > 0 ? std::min(1.0, std::min(soil[i], s.maxTalusDepth) * area[i] / sum) : 0;
                for (double& q : flux[i]) q *= scale;
            }
        });
        executor.run([&](int z) {
            for (int x = 0; x < n; ++x) {
                size_t i = size_t(z) * n + x;
                double out = 0, in = 0;
                for (int d = 0; d < 4; ++d) {
                    int j = neighbour(x, z, d);
                    out += flux[i][d];
                    if (j >= 0) in += flux[j][kOpposite[d]];
                }
                available[i] = std::max(0.0, soil[i] - out / area[i]) + in / area[i];
                result.relaxation[i] += available[i] - soil[i];
            }
        });
        soil.swap(available);
    }
    // Validate before committing, so failed settings/step limits leave the
    // caller's terrain intact. Final rounded geometry participates in budgets.
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(rock[i] + soil[i] + result.water[i] + result.sediment[i]) ||
            soil[i] < 0 || result.water[i] < 0 || result.sediment[i] < 0)
            throw std::runtime_error("non-finite or negative erosion result");
    }
    for (size_t i = 0; i < count; ++i) {
        float roundedRock = float(rock[i]), roundedSoil = float(soil[i]);
        budget.convertedBedrock += (double(fields.bedrock[i]) - roundedRock) * area[i];
        budget.solidRoundingDelta += (double(roundedRock) - rock[i] + double(roundedSoil) - soil[i]) * area[i];
        fields.bedrock[i] = roundedRock;
        fields.soil[i] = roundedSoil;
        fields.heightmap.heights[i] = roundedRock + roundedSoil;
        budget.finalWater += result.water[i] * area[i];
        budget.finalSoil += roundedSoil * area[i];
        budget.finalSediment += result.sediment[i] * area[i];
    }
    budget.waterResidual = budget.finalWater + budget.exportedWater + budget.infiltration + budget.evaporation - budget.initialWater - budget.rainfall;
    budget.solidResidual = budget.finalSoil + budget.finalSediment + budget.exportedSediment - budget.initialSoil - budget.initialSediment - budget.convertedBedrock;
    return result;
}

Result run(MacroTerrain::Fields& fields, const Settings& settings, const InitialState& initial) {
    auto started = std::chrono::steady_clock::now();
    auto result = simulate(fields, settings, initial);
    // Include allocation, all passes, finalization and scratch/team teardown.
    result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}

} // namespace HydraulicErosion
