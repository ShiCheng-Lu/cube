#include "render/fbx-loader.hpp"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

glm::vec3 toGlm(const ufbx_vec3& v) {
    return {float(v.x), float(v.y), float(v.z)};
}

/// ufbx matrices are 3x4 affine (four columns of vec3)
glm::mat4 toGlm(const ufbx_matrix& m) {
    glm::mat4 r(1.0f);
    for (int c = 0; c < 4; ++c) {
        r[c] = glm::vec4(toGlm(m.cols[c]), c == 3 ? 1.0f : 0.0f);
    }
    return r;
}

std::runtime_error ufbxError(const std::string& what, const ufbx_error& error) {
    char buffer[1024];
    ufbx_format_error(buffer, sizeof(buffer), &error);
    return std::runtime_error(what + ": " + buffer);
}

glm::vec3 materialColor(const ufbx_material* material) {
    if (!material) return glm::vec3(0.8f);
    const ufbx_material_map& pbr = material->pbr.base_color;
    if (pbr.has_value) return toGlm(ufbx_vec3{pbr.value_vec4.x, pbr.value_vec4.y, pbr.value_vec4.z});
    const ufbx_material_map& fbx = material->fbx.diffuse_color;
    if (fbx.has_value) return toGlm(fbx.value_vec3);
    return glm::vec3(0.8f);
}

} // namespace

AnimatedModel AnimatedModel::load(const std::string& path) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.generate_missing_normals = true;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) throw ufbxError("failed to load " + path, error);

    AnimatedModel model;
    model.scene_ = scene;   // owned from here on, freed by the destructor if we throw

    auto addBone = [&](const ufbx_node* node, const ufbx_matrix& geometryToNode) {
        model.bones_.push_back({node->typed_id, toGlm(geometryToNode)});
        return uint32_t(model.bones_.size() - 1);
    };

    std::vector<uint32_t> triangle;
    std::vector<SkinnedVertex> corners;
    std::vector<uint32_t> cornerIndices;

    for (const ufbx_mesh* mesh : scene->meshes) {
        if (mesh->instances.count == 0 || mesh->num_triangles == 0) continue;

        const ufbx_skin_deformer* skin =
            mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;

        // A skinned mesh is placed in the world by its bones, so it's emitted
        // once. A rigid mesh is emitted once per node that instances it.
        size_t copies = skin ? 1 : mesh->instances.count;
        for (size_t instance = 0; instance < copies; ++instance) {
            const ufbx_node* node = mesh->instances.data[instance];
            uint32_t rigidBone = addBone(node, node->geometry_to_node);

            uint32_t clusterBase = uint32_t(model.bones_.size());
            if (skin) {
                for (const ufbx_skin_cluster* cluster : skin->clusters) {
                    // clusters without a bone fall back to following the mesh node
                    if (cluster->bone_node) addBone(cluster->bone_node, cluster->geometry_to_bone);
                    else addBone(node, node->geometry_to_node);
                }
            }

            corners.clear();
            triangle.resize(mesh->max_face_triangles * 3);
            for (size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex) {
                ufbx_face face = mesh->faces.data[faceIndex];
                uint32_t triangles = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh, face);

                const ufbx_material* material = nullptr;
                if (mesh->face_material.count > 0 && mesh->materials.count > 0) {
                    uint32_t m = mesh->face_material.data[faceIndex];
                    if (m < mesh->materials.count) material = mesh->materials.data[m];
                }
                glm::vec3 color = materialColor(material);

                for (uint32_t k = 0; k < triangles * 3; ++k) {
                    uint32_t index = triangle[k];
                    SkinnedVertex v{};
                    v.position = toGlm(ufbx_get_vertex_vec3(&mesh->vertex_position, index));
                    v.normal = toGlm(ufbx_get_vertex_vec3(&mesh->vertex_normal, index));
                    v.color = color;
                    if (mesh->vertex_color.exists) {
                        ufbx_vec4 c = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
                        v.color *= glm::vec3(float(c.x), float(c.y), float(c.z));
                    }
                    v.joints = glm::uvec4(rigidBone);
                    v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

                    if (skin) {
                        // weights are sorted by decreasing influence: keep the top four
                        const ufbx_skin_vertex& sv = skin->vertices.data[mesh->vertex_indices.data[index]];
                        uint32_t count = std::min(sv.num_weights, 4u);
                        float total = 0.0f;
                        for (uint32_t w = 0; w < count; ++w) {
                            const ufbx_skin_weight& weight = skin->weights.data[sv.weight_begin + w];
                            v.joints[w] = clusterBase + weight.cluster_index;
                            v.weights[w] = float(weight.weight);
                            total += v.weights[w];
                        }
                        if (total > 0.0f) {
                            for (uint32_t w = count; w < 4; ++w) {
                                v.joints[w] = clusterBase;
                                v.weights[w] = 0.0f;
                            }
                            v.weights /= total;
                        } else {
                            v.joints = glm::uvec4(rigidBone);
                            v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                        }
                    }
                    corners.push_back(v);
                }
            }

            // Merge identical corners into shared vertices
            cornerIndices.resize(corners.size());
            ufbx_vertex_stream stream{corners.data(), corners.size(), sizeof(SkinnedVertex)};
            size_t vertexCount = ufbx_generate_indices(&stream, 1, cornerIndices.data(),
                                                       cornerIndices.size(), nullptr, &error);
            if (error.type != UFBX_ERROR_NONE) throw ufbxError("failed to index mesh", error);

            uint32_t base = uint32_t(model.vertices_.size());
            model.vertices_.insert(model.vertices_.end(), corners.begin(), corners.begin() + vertexCount);
            for (uint32_t i : cornerIndices) model.indices_.push_back(base + i);
        }
    }

    if (model.indices_.empty()) throw std::runtime_error(path + " has no meshes");

    model.selectAnimation(0);

    // Bounds of the first frame, skinned on the CPU, for framing the camera
    std::vector<glm::mat4> bones;
    model.evaluate(0.0, bones);
    model.boundsMin = glm::vec3(INFINITY);
    model.boundsMax = glm::vec3(-INFINITY);
    for (const SkinnedVertex& v : model.vertices_) {
        glm::mat4 skin(0.0f);
        for (int w = 0; w < 4; ++w) skin += v.weights[w] * bones[v.joints[w]];
        glm::vec3 p = glm::vec3(skin * glm::vec4(v.position, 1.0f));
        model.boundsMin = glm::min(model.boundsMin, p);
        model.boundsMax = glm::max(model.boundsMax, p);
    }

    return model;
}

AnimatedModel::AnimatedModel(AnimatedModel&& other) noexcept {
    *this = std::move(other);
}

AnimatedModel& AnimatedModel::operator=(AnimatedModel&& other) noexcept {
    if (this != &other) {
        if (scene_) ufbx_free_scene(scene_);
        scene_ = std::exchange(other.scene_, nullptr);
        anim_ = std::exchange(other.anim_, nullptr);
        timeBegin_ = other.timeBegin_;
        timeEnd_ = other.timeEnd_;
        vertices_ = std::move(other.vertices_);
        indices_ = std::move(other.indices_);
        bones_ = std::move(other.bones_);
        boundsMin = other.boundsMin;
        boundsMax = other.boundsMax;
    }
    return *this;
}

AnimatedModel::~AnimatedModel() {
    if (scene_) ufbx_free_scene(scene_);
}

std::vector<std::string> AnimatedModel::animationNames() const {
    std::vector<std::string> names;
    for (const ufbx_anim_stack* stack : scene_->anim_stacks) {
        names.emplace_back(stack->name.data, stack->name.length);
    }
    return names;
}

void AnimatedModel::selectAnimation(size_t index) {
    if (index < scene_->anim_stacks.count) {
        const ufbx_anim_stack* stack = scene_->anim_stacks.data[index];
        anim_ = stack->anim;
        timeBegin_ = stack->time_begin;
        timeEnd_ = stack->time_end;
    } else {
        anim_ = nullptr;
        timeBegin_ = timeEnd_ = 0.0;
    }
}

void AnimatedModel::evaluate(double time, std::vector<glm::mat4>& bones) const {
    double length = duration();
    double t = timeBegin_ + (length > 0.0 ? std::fmod(time, length) : 0.0);

    // Evaluating the whole scene handles every FBX transform quirk (pivots,
    // scale inheritance, ...) for us. It only copies transforms, not meshes.
    const ufbx_scene* posed = scene_;
    ufbx_scene* evaluated = nullptr;
    if (anim_) {
        ufbx_error error;
        evaluated = ufbx_evaluate_scene(scene_, anim_, t, nullptr, &error);
        if (!evaluated) throw ufbxError("failed to evaluate animation", error);
        posed = evaluated;
    }

    bones.resize(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        const ufbx_node* node = posed->nodes.data[bones_[i].node];
        bones[i] = toGlm(node->node_to_world) * bones_[i].geometryToNode;
    }

    if (evaluated) ufbx_free_scene(evaluated);
}
