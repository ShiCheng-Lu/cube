#include "render/render.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace {

/**
 * @brief a display panel, given by three of its corners in world space
 * (cube units), as seen by someone looking at the screen
 */
struct Face {
    glm::vec3 lowerLeft;
    glm::vec3 lowerRight;
    glm::vec3 upperLeft;
};

// The three displays meet at the corner (0, 0, 0). The two side panels stand
// upright (+y is up) and the top panel's lower edge is the one it shares with
// the -z panel. Seen from outside, the +x panel is left of the shared vertical
// edge and the -z panel right of it.
//
// TODO: match these to how each panel is physically mounted (which edge is its
// first scanline): swapping corners here rotates/flips that display's image.
const Face kFaceX{{0, -1, 1}, {0, -1, 0}, {0, 0, 1}};
const Face kFaceY{{0, 0, 0}, {-1, 0, 0}, {0, 0, 1}};
const Face kFaceZ{{0, -1, 0}, {-1, -1, 0}, {0, 0, 0}};

/**
 * @brief off-axis perspective through a rectangular screen from an arbitrary
 * eye point (Kooima, "Generalized Perspective Projection", 2008)
 */
glm::mat4 screenProjection(const Face& face, glm::vec3 eye, float far) {
    // orthonormal basis of the screen: right, up, and normal towards the viewer
    glm::vec3 right = glm::normalize(face.lowerRight - face.lowerLeft);
    glm::vec3 up = glm::normalize(face.upperLeft - face.lowerLeft);
    glm::vec3 normal = glm::normalize(glm::cross(right, up));

    // eye -> corners, and the eye's distance to the screen plane
    glm::vec3 toLowerLeft = face.lowerLeft - eye;
    glm::vec3 toLowerRight = face.lowerRight - eye;
    glm::vec3 toUpperLeft = face.upperLeft - eye;
    // From behind the panel nothing on it is visible; clamping keeps the
    // matrix finite (the image is meaningless then, but nobody can see it).
    float distance = std::max(-glm::dot(toLowerLeft, normal), 1e-4f);

    // Put the near plane on the glass itself: the screen edges are then the
    // frustum edges at the near plane, and depth precision is best.
    float near = distance;
    float left = glm::dot(right, toLowerLeft);
    float rightEdge = glm::dot(right, toLowerRight);
    float bottom = glm::dot(up, toLowerLeft);
    float top = glm::dot(up, toUpperLeft);
    glm::mat4 projection = glm::frustum(left, rightEdge, bottom, top, near, std::max(far, near * 1.001f));

    // world -> screen-aligned axes, with the eye at the origin
    glm::mat4 rotation(1.0f);
    rotation[0] = glm::vec4(right.x, up.x, normal.x, 0.0f);
    rotation[1] = glm::vec4(right.y, up.y, normal.y, 0.0f);
    rotation[2] = glm::vec4(right.z, up.z, normal.z, 0.0f);
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -eye);

    return projection * rotation * translation;
}

std::array<glm::vec3, 4> corners(const Face& face) {
    glm::vec3 upperRight = face.lowerRight + face.upperLeft - face.lowerLeft;
    return {face.lowerLeft, face.lowerRight, upperRight, face.upperLeft};
}

} // namespace

PanelCorners panelCorners() {
    return {corners(kFaceX), corners(kFaceY), corners(kFaceZ)};
}

ProjectionMatrices projections(glm::vec3 eye, float far) {
    return {
        screenProjection(kFaceX, eye, far),
        screenProjection(kFaceY, eye, far),
        screenProjection(kFaceZ, eye, far),
    };
}
