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
    shell.update(2.5f);
    require(shell.alive, "Shell survives before its lifetime expires");
    shell.update(0.5f);
    require(!shell.alive, "Ballistic shell retains lifetime cleanup");
}
