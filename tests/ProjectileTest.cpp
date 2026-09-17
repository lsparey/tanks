#include "scene/CollisionSystem.h"
#include "scene/Projectile.h"

#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(glm::vec3 a, glm::vec3 b) {
    return glm::length(a - b) < 1e-4f;
}

Projectile levelShot() {
    Projectile shell;
    shell.position = {0, 10, 0};
    shell.previousPosition = shell.position;
    shell.velocity = {0, 0, 25};
    return shell;
}

// Muzzle-velocity range for manual aiming's shot power (0..1). Must match
// Tank::kMinShotSpeed/kMaxShotSpeed -- duplicated rather than shared because
// Projectile is deliberately Tank-agnostic, the same way tree_shadow.vert
// and basic.vert duplicate RasterInstance's layout across a boundary that
// can't easily share a header.
constexpr float kMinShotSpeed = 4.5f;
constexpr float kMaxShotSpeed = 45.0f;
constexpr float kMuzzleHeight = 1.5f;  // representative height above flat ground

// Integrates a shell fired from (0, kMuzzleHeight, 0) until it returns to
// ground level, returning the horizontal distance traveled. Uses its own
// unbounded lifetime (not Projectile's shipped default) so it measures the
// true ballistic landing point regardless of whether a real shot would time
// out first -- see Projectile.h's lifetimeRemaining comment.
float landingDistance(float speed, float elevationRadians) {
    Projectile shell;
    shell.position = {0, kMuzzleHeight, 0};
    shell.previousPosition = shell.position;
    shell.velocity = {0, speed * std::sin(elevationRadians), speed * std::cos(elevationRadians)};
    shell.lifetimeRemaining = 1e6f;
    constexpr float kStep = 1.0f / 240.0f;
    float simulated = 0.0f;
    while (shell.position.y > 0.0f) {
        shell.update(kStep);
        simulated += kStep;
        if (simulated > 300.0f) throw std::runtime_error("shell did not land within a bounded simulation window");
    }
    // Linear interpolation between the last two samples for a smoother
    // landing point than the raw step granularity would give.
    float t = shell.previousPosition.y / (shell.previousPosition.y - shell.position.y);
    glm::vec3 landing = glm::mix(shell.previousPosition, shell.position, t);
    return glm::length(glm::vec2(landing.x, landing.z));
}

int main() {
    auto shell = levelShot();
    shell.update(1.0f);
    require(close(shell.position, {0, 9.85f, 25}), "Level shot drops 0.15 units at 25 units range");
    require(close(shell.previousPosition, {0, 10, 0}), "Sweep starts at pre-step position");
    shell.update(1.0f);
    require(close(shell.position, {0, 9.4f, 50}), "Drop grows with flight time squared");
    require(close(shell.previousPosition, {0, 9.85f, 25}), "Sweep advances with each step");

    // Different frame rates must produce the same trajectory, including
    // a shot with both sideways travel and initial upward velocity.
    for (glm::vec3 velocity : {glm::vec3(0, 0, 25), glm::vec3(15, 5, 20)}) {
        auto whole = levelShot();
        whole.velocity = velocity;
        auto split = whole;
        whole.update(1.0f);
        for (int i = 0; i < 120; ++i) split.update(1.0f / 120.0f);
        require(close(whole.position, split.position), "Ballistic position is frame-rate independent");
        require(close(whole.velocity, split.velocity), "Ballistic velocity is frame-rate independent");
        require(close(glm::vec3(split.worldMatrix()[2]), glm::normalize(split.velocity)),
                "Shell nose follows the current flight direction");
        require(close(glm::vec3(split.worldMatrix()[3]), split.position),
                "Shell transform follows ballistic position");
    }

    // A thin target between frame endpoints must still be hit while the
    // shell descends; neither endpoint is inside the target.
    shell = levelShot();
    shell.update(1.0f);
    shell.update(0.1f);
    float hitT = -1.0f;
    require(CollisionSystem::segmentIntersectsAABB(
                shell.previousPosition, shell.position, {-0.1f, 9.82f, 26.2f},
                {0.1f, 9.85f, 26.3f}, &hitT),
            "Descending shells retain swept collision against thin targets");
    require(hitT > 0.0f && hitT < 1.0f, "Impact lies inside the swept segment");

    shell = levelShot();
    shell.update(0.0f);
    require(close(shell.position, {0, 10, 0}) && close(shell.velocity, {0, 0, 25}),
            "Zero elapsed time leaves flight unchanged");
    shell.update(11.5f);
    require(shell.alive, "Shell survives before its lifetime expires");
    shell.update(0.5f);
    require(!shell.alive, "Ballistic shell retains lifetime cleanup");

    // Manual aiming's (elevation, power) -> landing distance table: for
    // every tested elevation, landing distance strictly increases with
    // power. Elevations match Tank's authored Challenger 2 gun limits
    // (-10 to +20 degrees); powers span the full 0..1 range the T/G keys
    // adjust.
    const float elevations[] = {glm::radians(-10.0f), glm::radians(0.0f),
                                 glm::radians(10.0f), glm::radians(20.0f)};
    const float powers[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (float elevation : elevations) {
        float previous = -1.0f;
        for (float power : powers) {
            float speed = kMinShotSpeed + power * (kMaxShotSpeed - kMinShotSpeed);
            float distance = landingDistance(speed, elevation);
            require(distance > previous, "landing distance is not strictly monotonic in power");
            previous = distance;
        }
    }

    // Extreme cases named in the acceptance text. Minimum power lands
    // within a few hull lengths (Tank::hullLength ~4.48 m); maximum power
    // at a level shot reaches across the boundary (Application's default
    // terrain worldSize 180 m, boundary half-extent 72 m after the 10%
    // inset -- see Application.cpp's kBoundaryInsetFraction).
    float minLevel = landingDistance(kMinShotSpeed, 0.0f);
    require(minLevel > 5.0f && minLevel < 20.0f, "minimum power does not land within a few hull lengths");
    float maxLevel = landingDistance(kMaxShotSpeed, 0.0f);
    require(maxLevel > 72.0f, "maximum power at a level shot does not reach the boundary");
}
