#include "InputManager.h"

#include <algorithm>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

InputManager::InputManager(GLFWwindow* window) : window_(window) {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void InputManager::update() {
    double x, y;
    glfwGetCursorPos(window_, &x, &y);

    if (firstUpdate_) {
        lastX_ = x;
        lastY_ = y;
        firstUpdate_ = false;
    }

    mouseDelta_ = glm::vec2(static_cast<float>(x - lastX_), static_cast<float>(y - lastY_));
    lastX_ = x;
    lastY_ = y;
}

bool InputManager::isKeyDown(int glfwKey) const {
    for (int held : heldKeys_) {
        if (held == glfwKey) return true;
    }
    return glfwGetKey(window_, glfwKey) == GLFW_PRESS;
}

void InputManager::holdKey(int glfwKey) {
    if (std::find(heldKeys_.begin(), heldKeys_.end(), glfwKey) == heldKeys_.end())
        heldKeys_.push_back(glfwKey);
}

void InputManager::releaseKey(int glfwKey) {
    heldKeys_.erase(std::remove(heldKeys_.begin(), heldKeys_.end(), glfwKey), heldKeys_.end());
}
