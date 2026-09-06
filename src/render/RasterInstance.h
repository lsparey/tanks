#pragma once

#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>

// std430 layout shared with basic.vert. Wind is evaluated once per placement,
// then copied to any bark/foliage batches that reference that placement.
struct RasterInstance {
    glm::mat4 model{1};
    glm::vec4 wind{0};
    glm::vec4 previousWind{0};
    // x: fine coverage, y: +1 fine/-1 coarse/0 stable, z: stable group seed; w: history reactivity.
    glm::vec4 foliageFade{0};
};
static_assert(offsetof(RasterInstance, wind) == 64);
static_assert(offsetof(RasterInstance, previousWind) == 80);
static_assert(offsetof(RasterInstance, foliageFade) == 96);
static_assert(sizeof(RasterInstance) == 112);

inline glm::vec3 treeWind(const glm::mat4& model, float seconds) {
    constexpr float omega = 6.28318530718f / 128.f;
    float phase = glm::dot(glm::vec2(model[3].x, model[3].z), glm::vec2(.08f,.05f));
    float along = .007f + .009f * std::sin(seconds*omega*13.f+phase)
                        + .003f * std::sin(seconds*omega*5.f+phase*1.7f);
    float across = .004f * std::sin(seconds*omega*17.f+phase+1.2f);
    glm::vec2 wind = glm::vec2(.8f,.6f)*along + glm::vec2(-.6f,.8f)*across;
    glm::mat3 rotation = glm::mat3(model) / glm::length(glm::vec3(model[1]));
    glm::vec3 bend = glm::transpose(rotation) * glm::vec3(wind.x,0,wind.y);
    bend.y = 0;
    return bend;
}
inline RasterInstance windInstance(const glm::mat4& model, float current, float previous) {
    return {model, glm::vec4(treeWind(model,current),0), glm::vec4(treeWind(model,previous),0), glm::vec4(0)};
}
