#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// Forward declarations
struct Object;
class Model;
struct InputState {
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool brake = false;

  // [NEW] Analog Inputs (-1.0 to 1.0 or 0.0 to 1.0)
  float steerValue = 0.0f;     // -1.0 (Left) to 1.0 (Right)
  float throttleValue = 0.0f;  // 0.0 to 1.0
  float brakeValue = 0.0f;     // 0.0 to 1.0
};

class Car {
 public:
  Car(Object* renderObject);
  ~Car() = default;

  void update(float deltaTime, const InputState& input,
              const std::function<std::pair<float, int>(float, float)>& heightMap);

  std::vector<glm::vec3> wheels;  // Physics suspension points
  // Wheel objects (FL, FR, RL, RR)
  void setWheels(const std::vector<Object*>& wObjs, const std::vector<glm::mat4>& wOffsets);
  // [NEW] Brake Light
  void setBrakeLight(Object* obj, Model* model);

  // Getters for camera and rendering
  glm::vec3 getPosition() const { return position; }
  void setPosition(const glm::vec3& p) { position = p; }
  void setHeading(float angle) { headingAngle = angle; }
  void setY(float y) { position.y = y; }
  glm::vec3 getFront() const { return front; }
  Object* getRenderObject() const { return renderObject; }
  float getSpeed() const { return speed; }
  glm::vec3 getUp() const { return up; }
  glm::vec3 getRight() const { return right; }

 private:
  // Rendering
  Object* renderObject;  // Pointer to the scene object (has transform matrix)
  std::vector<Object*> wheelRenderObjects;
  std::vector<glm::mat4> wheelModelOffsets;  // Initial Transform relative to Car Center
  bool isOffRoad();

  // Brake Light
  Object* brakeLightObject = nullptr;
  Model* brakeLightModel = nullptr;
  float wheelRotation = 0.0f;  // Current spin angle

  // Physics state
  glm::vec3 position;
  glm::vec3 front;
  glm::vec3 up;
  glm::vec3 right;

  glm::vec3 velocity;  // Actual movement vector (can diverge from front)

  float speed;         // Magnitude of velocity (roughly)
  float headingAngle;  // Radians
  float steeringAngle = 0.0f;

  // Config
  const float maxSpeed = 60.0f;              // Increased significantly (was 60.0f)
  const float maxOffTrackSpeed = 30.0f;      // Maximum speed for off-track
  const float acceleration = 30.0f;          // [Modified] Reduced from 50.0f to 25.0f for smoother control
  const float coastingDeceleration = 12.5f;  // Natural coasting
  const float brakingDeceleration = 30.0f;   // Stronger braking for high speeds

  // Bicycle Model Config
  const float maxSteerLowSpeed = glm::radians(12.f);  // 低速時可轉 12 度
  const float maxSteerHighSpeed = glm::radians(8.f);  // 高速時只准轉 8 度
  const float steerSpeed = 2.0f;                      // Speed of turning the wheel
  const float steerAutoCenter = 5.0f;                 // Speed of wheel returning to center
  const float wheelbase = 4.0f;                       // Distance between axles
  const float trackWidth = 2.0f;                      // Distance between left and right wheels

  // Drift Config
  const float driftTraction = 5.0f;  // Low traction when drifting (slides)
  const float gripTraction = 15.0f;  // High traction normally (grips)
};
