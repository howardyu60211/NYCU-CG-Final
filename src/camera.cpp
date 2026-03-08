#include "camera.h"

Camera::Camera(glm::vec3 _position)
    : position(_position),
      up(0, 1, 0),
      front(0, 0, -1),
      right(1, 0, 0),
      rotation(glm::identity<glm::quat>()),
      projectionMatrix(1),
      viewMatrix(1) {}

void Camera::initialize(float aspectRatio) {
  updateProjectionMatrix(aspectRatio);
  updateViewMatrix();
}

void Camera::move(GLFWwindow* window) {
  bool ismoved = false;
  if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    lastMouseX = xpos;
    lastMouseY = ypos;
    return;
  }
  // Mouse part
  if (lastMouseX == 0 && lastMouseY == 0) {
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
  } else {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    float dx = mouseMoveSpeed * static_cast<float>(xpos - lastMouseX);
    float dy = mouseMoveSpeed * static_cast<float>(lastMouseY - ypos);
    lastMouseX = xpos;
    lastMouseY = ypos;
    if (dx != 0 || dy != 0) {
      ismoved = true;
      glm::quat rx(glm::angleAxis(dx, glm::vec3(0, -1, 0)));
      glm::quat ry(glm::angleAxis(dy, glm::vec3(1, 0, 0)));
      rotation = rx * rotation * ry;
    }
  }
  // Keyboard part
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    position += front * keyboardMoveSpeed;
    ismoved = true;
  } else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    position -= front * keyboardMoveSpeed;
    ismoved = true;
  } else if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    position -= right * keyboardMoveSpeed;
    ismoved = true;
  } else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    position += right * keyboardMoveSpeed;
    ismoved = true;
  }
  // Update view matrix if moved
  if (ismoved) {
    updateViewMatrix();
  }
}

void Camera::setLastMousePos(GLFWwindow* window) {
  if (!window) return;
  glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
}

void Camera::updateViewMatrix() {
  constexpr glm::vec3 original_front(0, 0, -1);
  constexpr glm::vec3 original_up(0, 1, 0);

  front = rotation * original_front;
  up = rotation * original_up;
  right = glm::cross(front, up);
  viewMatrix = glm::lookAt(position, position + front, up);
}

void Camera::updateProjectionMatrix(float aspectRatio) {
  constexpr float FOV = glm::radians(60.0f);
  constexpr float zNear = 0.1f;
  constexpr float zFar = 1000.0f;

  projectionMatrix = glm::perspective(FOV, aspectRatio, zNear, zFar);
}

void Camera::updateTPS(glm::vec3 carPos, glm::vec3 carFront, float deltaTime) {
  constexpr float distance = 6.0f;
  constexpr float height = 1.5f;
  constexpr float lagSpeed = 8.0f;

  // Interpolate tpsFront towards carFront
  // If first run or invalid, snap to carFront
  if (glm::length(tpsFront) < 0.1f) tpsFront = carFront;

  // Mix: Current = Current + (Target - Current) * Factor
  // Using lerp for vector:
  float factor = lagSpeed * deltaTime;
  if (factor > 1.0f) factor = 1.0f;

  tpsFront = glm::normalize(tpsFront + (carFront - tpsFront) * factor);

  // Position: Behind (-tpsFront) and Up
  position = carPos - tpsFront * distance + glm::vec3(0.0f, height, 0.0f);

  // Target: carPos (or slightly above)
  glm::vec3 target = carPos + glm::vec3(0.0f, 1.0f, 0.0f);

  viewMatrix = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));

  // Update internal vectors for consistency
  // Note: 'front' usually implies where camera is looking.
  // In `updateViewMatrix` it is calculated from Rotation quaternion.
  // Here we override it.
  front = glm::normalize(target - position);
  right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
  up = glm::normalize(glm::cross(right, front));
}

void Camera::updateFPP(glm::vec3 carPos, glm::vec3 carFront, glm::vec3 carUp) {
  // Position: Slightly above the car center/hood, and slightly forward
  // Adjust these offsets to match the driver's eye position
  constexpr float forwardOffset = 0.2f;
  constexpr float heightOffset = 1.1f;

  // Use car's Up vector for height offset instead of global Y
  position = carPos + carFront * forwardOffset + carUp * heightOffset;

  // Target: Look forward from the car's perspective
  glm::vec3 target = position + carFront;

  // Use car's Up vector for the camera's up reference
  viewMatrix = glm::lookAt(position, target, carUp);

  // Update internal vectors
  front = carFront;
  // Recalculate right and up based on the new orientation
  right = glm::normalize(glm::cross(front, carUp));
  up = carUp;
}