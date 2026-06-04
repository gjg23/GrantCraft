#pragma once
// -------------------------------------------------------
// camera.hpp
// Handles view only — yaw, pitch, look direction, view matrix.
// Position is set externally by Player each frame.
// -------------------------------------------------------

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 position;
    float yaw;         // horizontal angle (degrees)
    float pitch;       // vertical angle (degrees, clamped +-89)
    float sensitivity;

    Camera(glm::vec3 startPos = {0, 0, 0});

    glm::mat4 getViewMatrix() const;
    glm::vec3 getFront() const;

    // Rotate by mouse delta (pixels)
    void rotate(float dx, float dy);
};