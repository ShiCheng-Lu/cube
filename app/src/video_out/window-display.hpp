#pragma once

struct GLFWwindow;

/**
 * @brief desktop window to render into while developing on a PC
 * (no OpenGL context: Vulkan draws into it through a surface)
 */
class WindowDisplay {
public:
    WindowDisplay(int width, int height, const char* title);
    ~WindowDisplay();
    WindowDisplay(const WindowDisplay&) = delete;
    WindowDisplay& operator=(const WindowDisplay&) = delete;

    GLFWwindow* handle() const { return window_; }
    bool shouldClose() const;
    void pollEvents() const;

private:
    GLFWwindow* window_ = nullptr;
};
