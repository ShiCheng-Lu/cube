#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <span>

struct GLFWwindow;
class AnimatedModel;

/**
 * @brief draws a skinned AnimatedModel into a GLFW window with Vulkan 1.3
 * (dynamic rendering, two frames in flight)
 */
class Renderer {
public:
    /// `shaderDir` holds the compiled skinned.vert.spv / skinned.frag.spv
    Renderer(GLFWwindow* window, const AnimatedModel& model, const std::filesystem::path& shaderDir);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// width / height of the current swapchain
    float aspect() const;

    /// draw one frame; `bones` is the palette from AnimatedModel::evaluate
    void drawFrame(const glm::mat4& viewProj, std::span<const glm::mat4> bones);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
