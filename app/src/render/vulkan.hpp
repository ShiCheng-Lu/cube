#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <span>

struct GLFWwindow;
class AnimatedModel;

/**
 * @brief draws a skinned AnimatedModel into a GLFW window with Vulkan 1.3
 * (dynamic rendering, two frames in flight).
 *
 * Each frame the model is skinned once by a compute shader, then drawn once per
 * view, so extra views only cost a matrix multiply per vertex.
 */
class Renderer {
public:
    /// one picture of the scene and where it goes in the window
    struct View {
        glm::mat4 viewProj;           // world -> clip
        glm::mat4 model{1.0f};        // model -> world, for the animated model only
        glm::ivec4 rect{0};           // x, y, width, height in pixels, from the top-left
        bool lines = false;           // draw the overlay lines on top
    };

    /// overlay line endpoints in world space, two per line
    struct LineVertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    /// `shaderDir` holds the compiled shaders (skin.comp.spv, mesh.*.spv, line.*.spv)
    Renderer(GLFWwindow* window, const AnimatedModel& model, std::span<const LineVertex> lines,
             const std::filesystem::path& shaderDir);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// size of the window's swapchain in pixels
    glm::uvec2 size() const;

    /// draw one frame; `bones` is the palette from AnimatedModel::evaluate
    void drawFrame(std::span<const View> views, std::span<const glm::mat4> bones);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
