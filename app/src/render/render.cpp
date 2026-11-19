
#include <glm/glm.h>

/**
 * @brief holds viewport transform for the x, y, and z faces of the display
 * 
 */
struct Viewports {
    glm::Matrix4f x;
    glm::Matrix4f y;
    glm::Matrix4f z;
};

/**
 * @brief return the three viewports if the cube sits at (0, 0, 0) to (-1, -1, 1)
 * with displays on the +x, +y, -z faces.
 */
void viewports(glm::Vector3f eye) {
    
}