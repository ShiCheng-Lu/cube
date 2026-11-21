#pragma once

#include <glm/glm.hpp>

#include <array>

/**
 * @brief view-projection matrix for each display face of the cube
 */
struct ProjectionMatrices {
    glm::mat4 x;
    glm::mat4 y;
    glm::mat4 z;
};

/**
 * @brief return the three projections if the cube sits at (0, 0, 0) to (-1, -1, 1)
 * with displays on the +x, +y, -z faces.
 *
 * Each display is a window into the cube: its matrix maps world space to that
 * display's clip space as seen from `eye`, with clip x = -1..1 running left to
 * right and clip y = -1..1 bottom to top across the panel (see kFaces in
 * render.cpp for which corner is which). Anything in front of the glass is
 * clipped, and so is anything farther than `far` from the eye.
 *
 * `eye` is in the same units as the cube (edge length 1) and should be on the
 * outside of all three faces (x > 0, y > 0, z < 0) to see all of them.
 */
ProjectionMatrices projections(glm::vec3 eye, float far);

/**
 * @brief the four corners of each display in cube space: lower-left,
 * lower-right, upper-right, upper-left, as someone facing the display sees them
 */
struct PanelCorners {
    std::array<glm::vec3, 4> x;
    std::array<glm::vec3, 4> y;
    std::array<glm::vec3, 4> z;
};

PanelCorners panelCorners();
