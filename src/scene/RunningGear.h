#pragma once

#include <array>
#include <set>
#include <string>
#include <vector>

#include "../io/ModelLoader.h"

// CPU-only rig construction and kinematics, shared by the tank and tests.
namespace RunningGear {
struct Path {
    std::vector<glm::vec2> points; // (Z,Y), CCW: bottom run goes toward +Z
    std::vector<double> lengths;
    double perimeter = 0;
    glm::mat4 frame(double distance, float x) const;
};
Path beltPath(const ModelLoader::Part& belt);
double wrap(double value, double period);
double signedTravel(double longitudinalTravel, double yawTravel, double localX);

struct Placement {
    unsigned side = 0; // 0 negative X, 1 positive X
    glm::vec3 centre{0};
    float radius = 0;
    double phase = 0; // distance around belt for shoes
};
struct Batch {
    ModelLoader::Part mesh;
    std::vector<Placement> placements;
    bool shoes = false;
};
struct Rig {
    std::array<Path,2> paths;
    std::array<float,2> trackX{};
    std::vector<Batch> batches;
    std::set<std::string> movingNames;
    // Separate bounded phases preserve both wheel angle and shoe distance
    // without accumulating large float translations in long sessions.
    std::array<double,2> beltPhase{};
    std::array<double,2> roadAngle{};
    std::array<double,2> endAngle{};
    bool enabled() const { return !batches.empty(); }
    void advance(double longitudinalTravel, double yawTravel);
    std::vector<std::vector<glm::mat4>> transforms(const glm::mat4& hull) const;
};
// Only the complete, named refined-model rig is animated. Legacy exports
// without that layout stay rigid, with no guessed part removal.
Rig extract(const ModelLoader::Result& model);
} // namespace RunningGear
