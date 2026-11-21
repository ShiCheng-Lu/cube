#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct ufbx_scene;
struct ufbx_anim;

/**
 * @brief one vertex as the GPU sees it: skinned by up to four bones
 * (`joints` index into the model's bone palette, `weights` sum to 1)
 */
struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::uvec4 joints;
    glm::vec4 weights;
};

/**
 * @brief an FBX scene flattened into one vertex/index buffer and a bone palette.
 *
 * Every mesh is drawn through the bone palette: skinned meshes get one bone per
 * skin cluster, rigid meshes get one bone for the node they hang off. So a single
 * shader and a single draw call handle both.
 */
class AnimatedModel {
public:
    /// throws std::runtime_error if the file can't be loaded or has no meshes
    static AnimatedModel load(const std::string& path);

    AnimatedModel(AnimatedModel&& other) noexcept;
    AnimatedModel& operator=(AnimatedModel&& other) noexcept;
    AnimatedModel(const AnimatedModel&) = delete;
    AnimatedModel& operator=(const AnimatedModel&) = delete;
    ~AnimatedModel();

    const std::vector<SkinnedVertex>& vertices() const { return vertices_; }
    const std::vector<uint32_t>& indices() const { return indices_; }
    size_t boneCount() const { return bones_.size(); }

    /// names of the animation stacks in the file, in order
    std::vector<std::string> animationNames() const;
    /// pick which animation stack `evaluate` plays; out of range picks none (bind pose)
    void selectAnimation(size_t index);
    /// length of the selected animation in seconds (0 if there is none)
    double duration() const { return timeEnd_ - timeBegin_; }

    /**
     * @brief fill `bones` with the geometry-to-world matrix of every bone at
     * `time` seconds into the selected animation (looping)
     */
    void evaluate(double time, std::vector<glm::mat4>& bones) const;

    /// world-space bounds of the first animation frame
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};

private:
    AnimatedModel() = default;

    struct Bone {
        uint32_t node;              // typed id of the node, into ufbx_scene::nodes
        glm::mat4 geometryToNode;   // mesh geometry -> node space at bind time
    };

    ufbx_scene* scene_ = nullptr;
    const ufbx_anim* anim_ = nullptr;
    double timeBegin_ = 0.0, timeEnd_ = 0.0;

    std::vector<SkinnedVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Bone> bones_;
};
