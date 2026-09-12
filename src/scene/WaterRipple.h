#pragma once

#include <cmath>

#include <glm/glm.hpp>

// A single expanding wave source on a standing water surface -- spawned by
// shell splashes and by the tank's tracks while wading (see Application::
// spawnWaterRipple). Not drawn as geometry: each live source is fed to the
// water shader through FrameUBO::waterWaves, where it becomes an expanding
// ring wave packet -- a normal perturbation that bends reflections around
// the propagating crests, plus a band of white foam riding the front (see
// basic.frag's water branch).
struct WaterRipple {
    glm::vec3 position{0.0f};  // centre on the water surface (y unused by the shader)
    float initialRadius = 0.3f;
    float growthRate = 2.0f;  // wavefront-radius growth in units/second
    float lifetimeRemaining = 1.1f;
    float initialLifetime = 1.1f;
    // Peak wave-slope amplitude for the shader packet; scales both the
    // reflection-bending steepness and the crest foam density.
    float waveAmplitude = 0.3f;
    bool alive = true;

    void update(float deltaTime) {
        lifetimeRemaining -= deltaTime;
        if (lifetimeRemaining <= 0.0f) alive = false;
    }

    float radius() const {
        float age = initialLifetime - glm::max(lifetimeRemaining, 0.0f);
        return initialRadius + growthRate * age;
    }

    // Current wave-slope amplitude for the shader: the peak amplitude damped
    // quadratically over the ripple's life and by cylindrical spreading (a
    // ring's energy thins as its circumference grows).
    float waveSlope() const {
        float t = glm::clamp(lifetimeRemaining / initialLifetime, 0.0f, 1.0f);
        return waveAmplitude * t * t / std::sqrt(glm::max(radius(), 0.6f));
    }
};
