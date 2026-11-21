#include "video_out/window-display.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

WindowDisplay::WindowDisplay(int width, int height, const char* title) {
    glfwSetErrorCallback([](int code, const char* message) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
    });
    if (!glfwInit()) throw std::runtime_error("failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("failed to create window");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            static_cast<WindowDisplay*>(glfwGetWindowUserPointer(window))->clicks_++;
        }
    });
}

WindowDisplay::~WindowDisplay() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

bool WindowDisplay::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

void WindowDisplay::pollEvents() const {
    glfwPollEvents();
}

int WindowDisplay::takeClicks() {
    int clicks = clicks_;
    clicks_ = 0;
    return clicks;
}
