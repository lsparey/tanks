#include "scene/FoliageLod.h"
#include "render/RasterInstance.h"

#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::array<float,3> weights(float pixels) {
    auto lod = FoliageLod::select(pixels);
    require(lod.fine >= 0 && lod.coarse < 3 && lod.coarse >= lod.fine,
            "Invalid foliage level");
    require(!lod.transitioning() && lod.fineCoverage == 1, "Hard LOD switch blended meshes");
    require(lod.fineCoverage >= 0 && lod.fineCoverage <= 1, "Invalid coverage");
    std::array<float,3> result{};
    result[lod.fine] += lod.fineCoverage;
    result[lod.coarse] += 1-lod.fineCoverage;
    require(std::abs(result[0]+result[1]+result[2]-1) < 1e-6f, "Transition lost coverage");
    return result;
}
}
int main() {
    auto previous = weights(0);
    for (int step=1; step<=28000; ++step) {
        auto current = weights(step*.01f);
        require(current[1]+2*current[2] <= previous[1]+2*previous[2]+1e-6f,
                "Larger bough became less detailed");
        previous = current;
    }
    require(weights(0)[2] == 1 && weights(std::numeric_limits<float>::max())[0] == 1,
            "Incorrect near/far endpoints");
    for (float boundary : {13.f, 26.f}) {
        require(FoliageLod::coverageChange(FoliageLod::select(boundary-.001f),
                                         FoliageLod::select(boundary+.001f)) == 1.f,
                "Hard switch retained stale lighting history");
    }
    require(FoliageLod::coverageChange(FoliageLod::select(0),FoliageLod::select(280)) == 1,
            "Teleport retained stale lighting history");
    // Shared wind must be invariant under uniform scale, rotate back into the
    // same world direction, and carry the actual previous rendered sample.
    auto translated = glm::translate(glm::mat4(1), glm::vec3(27,2,-19));
    auto rotated = glm::rotate(translated, 1.23f, glm::vec3(0,1,0));
    auto scaled = glm::scale(rotated, glm::vec3(1.7f));
    for (float time : {0.f, 3.4f, 64.f, 127.99f}) {
        auto wind = treeWind(translated,time);
        require(glm::length(glm::mat3(rotated)*treeWind(rotated,time)-wind) < 1e-6f,
                "Yaw changed world wind direction");
        require(glm::length(treeWind(scaled,time)-treeWind(rotated,time)) < 1e-6f,
                "Scale changed bend strength");
        require(glm::length(treeWind(translated,time+128)-wind) < 1e-6f,
                "Wind discontinuity at clock wrap");
        auto instance = windInstance(scaled,time,.1f);
        require(glm::vec3(instance.wind)==treeWind(scaled,time) &&
                glm::vec3(instance.previousWind)==treeWind(scaled,.1f), "Incorrect wind history");
    }
}
