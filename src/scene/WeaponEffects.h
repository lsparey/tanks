#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>

// Pure presentation state: no input, recoil, camera or projectile physics.
namespace WeaponEffects {
constexpr size_t kMaxScorches = 16;
// Room for a muzzle flash plus the radial flame tongues an explosion now
// spawns (see Application::spawnExplosion) without evicting each other.
constexpr size_t kMaxFlashes = 12;
constexpr size_t kMaxSmoke = 48;

template<class T> void addBounded(std::vector<T>& items, T item, size_t limit) {
    if (items.size() >= limit) items.erase(items.begin());
    items.push_back(item);
}
template<class T> void update(std::vector<T>& items, float dt) {
    for (auto& item : items) item.update(dt);
    std::erase_if(items, [](const auto& item) { return item.remaining <= 0; });
}

// Quad local X is width, local Z is length. Robust even when aimed vertically.
inline glm::mat4 card(glm::vec3 centre, glm::vec3 along, glm::vec3 across,
                      float width, float length) {
    along = glm::normalize(along);
    across -= along * glm::dot(along, across);
    if (glm::dot(across, across) < 1e-6f)
        across = glm::cross(along, std::abs(along.y) < .9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0));
    across = glm::normalize(across);
    return glm::mat4(glm::vec4(across * width,0),
                     glm::vec4(glm::cross(along,across),0),
                     glm::vec4(along * length,0), glm::vec4(centre,1));
}
struct Flash {
    glm::vec3 position{0}, direction{0,0,1};
    // Muzzle flashes keep the default snap; explosion flame tongues pass a
    // longer lifetime (and their own scale) when spawned.
    float remaining = .085f, lifetime = .085f, scale = 1.0f;
    void update(float dt) { remaining -= dt; }
    float opacity() const { return glm::clamp(remaining / lifetime, 0.0f, 1.0f); }
    glm::mat4 matrix(glm::vec3 camera) const {
        float length = scale * 1.45f * (.65f + .35f * opacity());
        return card(position + direction * length * .5f, direction,
                    glm::cross(direction, camera-position), scale * .72f, length);
    }
};
struct Smoke {
    glm::vec3 position{0}, velocity{0};
    float remaining = .9f, lifetime = .9f, size = .22f, seed = 0;
    // Peak opacity; explosion soot columns are denser than muzzle smoke.
    float density = .42f;
    // Selects the darker, fire-lit explosion-soot shading in basic.frag's
    // smoke-card branch (passed through PushConstants::tankSurface.w).
    bool soot = false;
    void update(float dt) {
        // Analytic drag integration keeps the short initial jet independent
        // of render rate, slowing it into a rising cloud.
        glm::vec3 drift(.12f,.5f,.04f);
        float decay = std::exp(-4.0f * dt);
        position += drift*dt + (velocity-drift)*((1-decay)/4.0f);
        velocity = drift + (velocity-drift)*decay;
        remaining -= dt;
    }
    float age() const { return 1-glm::clamp(remaining/lifetime,0.0f,1.0f); }
    float opacity() const {
        float t=age();
        return density * glm::smoothstep(0.0f,.08f,t) * (1-t)*(1-t);
    }
    glm::mat4 matrix(glm::vec3 camera) const {
        glm::vec3 normal = camera-position;
        if (glm::dot(normal,normal)<1e-6f) normal=glm::vec3(0,0,1);
        normal=glm::normalize(normal);
        glm::vec3 right=glm::cross(glm::vec3(0,1,0),normal);
        if (glm::dot(right,right)<1e-6f) right=glm::vec3(1,0,0);
        right=glm::normalize(right);
        glm::vec3 up=glm::cross(normal,right);
        float extent=size + 1.15f*(1-std::exp(-3*age()));
        return card(position,up,right,extent,extent);
    }
};
struct Scorch {
    glm::vec3 position{0};
    float radius = 1.15f, remaining = 45.0f;
    void update(float dt) { remaining -= dt; }
    // Stay readable for 33 seconds, then fade without a sudden disappearance.
    float opacity() const { return .78f * glm::smoothstep(0.0f,12.0f,remaining); }
};
} // namespace WeaponEffects
