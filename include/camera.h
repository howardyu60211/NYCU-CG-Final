#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Camera {
 public:
  Camera(glm::vec3 _position);
  void initialize(float aspectRatio);
  void move(GLFWwindow* window);
  void updateViewMatrix();
  void updateProjectionMatrix(float aspectRatio);
  void setLastMousePos(GLFWwindow* window);

  // TPS Support
  void updateTPS(glm::vec3 carPos, glm::vec3 carFront, float deltaTime);
  // FPP Support
  void updateFPP(glm::vec3 carPos, glm::vec3 carFront, glm::vec3 carUp);

  const float* getProjectionMatrix() const { return glm::value_ptr(projectionMatrix); }
  const float* getViewMatrix() const { return glm::value_ptr(viewMatrix); }
  const float* getPosition() const { return glm::value_ptr(position); }

 private:
  glm::vec3 position;
  glm::vec3 up;
  glm::vec3 front;
  glm::vec3 tpsFront = glm::vec3(0, 0, 1);  // For TPS Lag
  glm::vec3 right;

  glm::quat rotation;

  constexpr static float keyboardMoveSpeed = 0.5f;
  constexpr static float mouseMoveSpeed = 0.001f;

  // matrix
  glm::mat4 projectionMatrix;
  glm::mat4 viewMatrix;
  double lastMouseX = 0.0;
  double lastMouseY = 0.0;
};