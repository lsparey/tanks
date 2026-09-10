#pragma once

#include <algorithm>
#include <cmath>

namespace FoliageLod {
inline constexpr float kMiddleVoxelSize = .18f;
inline constexpr float kFarVoxelSize = .5f;
struct Selection {
    int fine = 0;
    int coarse = 0;
    float fineCoverage = 1;
    bool transitioning() const { return fine != coarse; }
    float coverage(int lod) const {
        if (!transitioning()) return lod == fine ? 1.f : 0.f;
        return lod == fine ? fineCoverage : lod == coarse ? 1.f-fineCoverage : 0.f;
    }
};
// Reset lighting history when a hard LOD switch changes the geometry.
inline float coverageChange(const Selection& a, const Selection& b) {
    float change = 0;
    for (int lod=0;lod<3;++lod) change = std::max(change,std::abs(a.coverage(lod)-b.coverage(lod)));
    return change;
}
// Projected bough radius in pixels. Exactly one level is active per bough.
// The 2:1 size thresholds put the far switch at twice the middle distance.
inline Selection select(float pixels) {
    if (pixels >= 26.f) return {0,0,1};
    if (pixels >= 13.f) return {1,1,1};
    return {2,2,1};
}
// Project the representation's cell width, rather than the enclosing bough.
// Small deterministic offsets spread hard switches across boughs. Hysteresis
// retains a selected level within 10% of its budget, without drawing a pair.
inline Selection selectByFootprint(float pixelsPerModelUnit, int previousLod, unsigned seed = 0) {
    const float stagger = .9f + .1f * float((seed * 747796405u) & 255u) / 255.f;
    // Budgets doubled from 1.5/2.0 after profiling on the Arc A370M target:
    // the old values kept full-detail boughs out to ~60 units (~3M foliage
    // triangles/frame through the depth, lighting and cascade-0 shadow
    // passes). A ~3px cell footprint on leaf blobs is below what reads at
    // 720p, especially with TAA now smoothing the switch.
    const float middleBudget = 3.f * stagger * (previousLod >= 1 ? 1.1f : 1.f);
    const float farBudget = 4.f * stagger * (previousLod >= 2 ? 1.1f : 1.f);
    if (pixelsPerModelUnit * kFarVoxelSize <= farBudget) return {2,2,1};
    if (pixelsPerModelUnit * kMiddleVoxelSize <= middleBudget) return {1,1,1};
    return {0,0,1};
}
}
