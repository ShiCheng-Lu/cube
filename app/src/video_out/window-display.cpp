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
