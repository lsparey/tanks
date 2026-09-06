#pragma once

#include <algorithm>
#include <cmath>

namespace FoliageLod {
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
// Compare actual level weights, so entering/leaving a blend band does not
// abruptly reset lighting history when its geometry coverage barely changes.
inline float coverageChange(const Selection& a, const Selection& b) {
    float change = 0;
    for (int lod=0;lod<3;++lod) change = std::max(change,std::abs(a.coverage(lod)-b.coverage(lod)));
    return change;
}
inline float smoothCoverage(float pixels, float low, float high) {
    float t=std::clamp((pixels-low)/(high-low),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
// Projected bough radius in pixels. Narrow, non-overlapping blend bands
// keep at most two levels active, without a full-tree detail switch.
inline Selection select(float pixels) {
    if (pixels >= 48.f) return {0,0,1};
    if (pixels > 32.f) return {0,1,smoothCoverage(pixels,32,48)};
    if (pixels >= 24.f) return {1,1,1};
    if (pixels > 16.f) return {1,2,smoothCoverage(pixels,16,24)};
    return {2,2,1};
}
}
