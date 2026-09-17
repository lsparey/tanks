#pragma once

#include <vector>

#include <glm/glm.hpp>

struct GLFWwindow;

// Polls GLFW key state and accumulates mouse-delta-since-last-update. Call
// update() exactly once per frame, after which isKeyDown/mouseDelta reflect
// that frame.
class InputManager {
public:
    explicit InputManager(GLFWwindow* window);

    void update();

    bool isKeyDown(int glfwKey) const;
    glm::vec2 mouseDelta() const { return mouseDelta_; }

    // App-local diagnostic overlay (--drive-preview): makes isKeyDown report
    // the given key as held without any desktop input, same "never inject OS
    // events" policy as the weapon preview. A held key stays held across
    // update() calls until released.
    void holdKey(int glfwKey);
    void releaseKey(int glfwKey);
    // Forget the last cursor position so the next update() reports a zero
    // delta -- call after the cursor has been shown/hidden (e.g. around the
    // pause menu), where its position jumps without the player moving it.
    void resetMouse() { firstUpdate_ = true; }

private:
    GLFWwindow* window_;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
    bool firstUpdate_ = true;
    glm::vec2 mouseDelta_{0.0f};
    std::vector<int> heldKeys_;
};
