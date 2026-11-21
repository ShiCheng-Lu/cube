
#include <glm/glm.hpp>

/**
 * @brief holds viewport transform for the x, y, and z faces of the display
 * 
 */
struct ProjectionMatrices {
    glm::mat4 x;
    glm::mat4 y;
    glm::mat4 z;
};

/**
 * @brief return the three projections if the cube sits at (0, 0, 0) to (-1, -1, 1)
 * with displays on the +x, +y, -z faces.
 */
ProjectionMatrices projections(glm::vec3 eye, float far) {
    ProjectionMatrices transforms;

    // x face maps:
    // (0, 0, 0) -> (-1, -1)
    // (0, 1, 0) -> ()



    return transforms;
}
