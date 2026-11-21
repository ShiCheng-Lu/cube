#include "render/fbx-loader.hpp"
#include "render/render.hpp"
#include "render/vulkan.hpp"
#include "video_out/window-display.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <array>
#include <filesystem>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.fbx> [animation index]\n", argv[0]);
        return 1;
    }

    try {
        AnimatedModel model = AnimatedModel::load(argv[1]);

        std::vector<std::string> animations = model.animationNames();
        size_t animation = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 0;
        model.selectAnimation(animation);

        std::printf("%s: %zu vertices, %zu triangles, %zu bones\n", argv[1],
                    model.vertices().size(), model.indices().size() / 3, model.boneCount());
        for (size_t i = 0; i < animations.size(); ++i) {
            std::printf("  %c animation %zu: %s\n", i == animation ? '*' : ' ', i, animations[i].c_str());
        }
        if (animations.empty()) std::printf("  no animations, showing the bind pose\n");

        WindowDisplay window(720, 720, "cube");
        std::filesystem::path shaderDir = std::filesystem::path(argv[0]).parent_path() / "shaders";

        // Outline each display: +x red, +y green, -z blue
        std::vector<Renderer::LineVertex> outlines;
        PanelCorners panels = panelCorners();
        auto outline = [&](const std::array<glm::vec3, 4>& corners, glm::vec3 color) {
            for (size_t i = 0; i < corners.size(); ++i) {
                outlines.push_back({corners[i], color});
                outlines.push_back({corners[(i + 1) % corners.size()], color});
            }
        };
        outline(panels.x, {1.0f, 0.3f, 0.3f});
        outline(panels.y, {0.3f, 1.0f, 0.3f});
        outline(panels.z, {0.3f, 0.5f, 1.0f});

        Renderer renderer(window.handle(), model, outlines, shaderDir);

        // Fit the model inside the cube (0, 0, 0)..(-1, -1, 1), standing on its
        // floor and turned from facing +z (the usual FBX front) to facing the
        // corner the displays share, where the viewer is
        glm::vec3 extent = model.boundsMax - model.boundsMin;
        float scale = 0.9f / glm::max(glm::max(extent.x, extent.y), glm::max(extent.z, 1e-6f));
        glm::vec3 center = (model.boundsMin + model.boundsMax) * 0.5f;
        glm::mat4 modelToCube =
            glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, -1.0f, 0.5f)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(135.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
            glm::translate(glm::mat4(1.0f), -glm::vec3(center.x, model.boundsMin.y, center.z));

        std::printf("click the window to pause / resume\n");

        // Scene time stops while paused (animation and the fake eye both freeze)
        std::vector<glm::mat4> bones;
        bool paused = false;
        double time = 0.0;
        double last = glfwGetTime();
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.takeClicks() % 2 == 1) {
                paused = !paused;
                std::printf("%s at t = %.2f s\n", paused ? "paused" : "resumed", time);
            }
            double now = glfwGetTime();
            if (!paused) time += now - last;
            last = now;
            model.evaluate(time, bones);

            // Until the camera tracks a real eye, sway a fake one around the
            // corner the three displays share (staying where all three are visible)
            float angle = glm::radians(45.0f) + glm::radians(30.0f) * std::sin(float(time) * 0.5f);
            float height = 1.2f + 0.6f * std::sin(float(time) * 0.31f);
            glm::vec3 eye = 2.5f * glm::normalize(glm::vec3(std::cos(angle), height, -std::sin(angle)));
            ProjectionMatrices faces = projections(eye, 20.0f);

            // What the eye sees of the whole cube: a plain perspective aimed at
            // its center, wide enough to fit the cube's bounding sphere
            glm::vec3 cubeCenter(-0.5f, -0.5f, 0.5f);
            float fov = 2.2f * std::asin(glm::min(0.866f / glm::length(eye - cubeCenter), 0.9f));
            glm::mat4 assembled = glm::perspective(fov, 1.0f, 0.05f, 20.0f) *
                                  glm::lookAt(eye, cubeCenter, glm::vec3(0.0f, 1.0f, 0.0f));

            // Top-left: the assembled view with the panels outlined. The rest is
            // the unfolded cube: +x and -z side by side, +y on top of -z (the
            // edge they share)
            glm::ivec2 size(renderer.size());
            int tile = glm::min(size.x / 2, size.y / 2);
            glm::ivec2 origin = (size - 2 * tile) / 2;
            std::array views{
                Renderer::View{assembled, modelToCube, {origin.x, origin.y, tile, tile}, true},
                Renderer::View{faces.x, modelToCube, {origin.x, origin.y + tile, tile, tile}},
                Renderer::View{faces.z, modelToCube, {origin.x + tile, origin.y + tile, tile, tile}},
                Renderer::View{faces.y, modelToCube, {origin.x + tile, origin.y, tile, tile}},
            };
            renderer.drawFrame(views, bones);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
