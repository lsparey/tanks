#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "FoliageLod.h"

// Decorative placement of shared pine, ash or oak geometry. Bark retains
// whole-tree LOD; each foliage bough tracks its own progressive selection.
struct TreeInstance {
    glm::vec3 position;
    float yaw = 0.0f;
    float scale = 1.0f;
    int meshVariant = 0;
    int lod = 0;  // bark: updated from projected screen size with hysteresis
    std::vector<FoliageLod::Selection> foliageLods;

    glm::mat4 worldMatrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::scale(m, glm::vec3(scale));
    }
};
