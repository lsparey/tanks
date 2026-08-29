#pragma once

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

private:
    GLFWwindow* window_;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
    bool firstUpdate_ = true;
    glm::vec2 mouseDelta_{0.0f};
};
