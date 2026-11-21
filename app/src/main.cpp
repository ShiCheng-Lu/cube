#include "render/fbx-loader.hpp"
#include "render/vulkan.hpp"
#include "video_out/window-display.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
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
        Renderer renderer(window.handle(), model, shaderDir);

        // Frame the model and slowly orbit around it
        glm::vec3 center = (model.boundsMin + model.boundsMax) * 0.5f;
        float radius = glm::max(glm::length(model.boundsMax - model.boundsMin) * 0.5f, 1e-3f);

        std::vector<glm::mat4> bones;
        while (!window.shouldClose()) {
            window.pollEvents();
            double time = glfwGetTime();
            model.evaluate(time, bones);

            float angle = float(time) * 0.3f;
            glm::vec3 eye = center + radius * 2.2f * glm::vec3(std::sin(angle), 0.35f, std::cos(angle));
            glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), renderer.aspect(),
                                              radius * 0.01f, radius * 10.0f);
            renderer.drawFrame(proj * view, bones);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
