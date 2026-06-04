// camera.cpp
#include "game_core/camera.hpp"
#include <algorithm>

Camera::Camera(glm::vec3 startPos): 
    position(startPos), yaw(-90.0f), pitch(0.0f), sensitivity(0.1f) {}

glm::vec3 Camera::getFront() const {
    // Convert yaw+pitch to a direction vector
    glm::vec3 dir;
    dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    dir.y = sin(glm::radians(pitch));
    dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    return glm::normalize(dir);
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + getFront(), glm::vec3(0,1,0));
}

void Camera::rotate(float dx, float dy) {
    yaw   += dx * sensitivity;
    pitch -= dy * sensitivity; // subtract so mouse-up = look up
    
    // Clamp pitch so we can't flip upside down
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}
