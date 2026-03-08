#include "car.h"
#include <algorithm>  // for clamp
#include <cstdlib>    // for rand
#include "model.h"

Car::Car(Object* renderObject) : renderObject(renderObject) {
  position = glm::vec3(0.0f, 0.0f, 0.0f);
  front = glm::vec3(0.0f, 0.0f, 1.0f);  // Initially facing +Z
  up = glm::vec3(0.0f, 1.0f, 0.0f);
  right = glm::cross(front, up);

  speed = 0.0f;
  headingAngle = 0.0f;

  // Initialize object transform
  if (renderObject) {
    renderObject->transformMatrix = glm::translate(glm::mat4(1.0f), position);
  }
}

void Car::setWheels(const std::vector<Object*>& wObjs, const std::vector<glm::mat4>& wOffsets) {
  wheelRenderObjects = wObjs;
  wheelModelOffsets = wOffsets;
}

void Car::setBrakeLight(Object* obj, Model* model) {
  brakeLightObject = obj;
  brakeLightModel = model;
}

// Helper mix function
glm::vec3 lerp(glm::vec3 a, glm::vec3 b, float f) { return a + f * (b - a); }

void Car::update(float deltaTime, const InputState& input,
                 const std::function<std::pair<float, int>(float, float)>& heightMap) {
  // 1. Update Steering Angle (Analog + Speed Sensitivity)
  float lowSpeedThreshold = 20.0f;
  float highSpeedThreshold = 45.0f;
  float t = std::clamp((speed - lowSpeedThreshold) / (highSpeedThreshold - lowSpeedThreshold), 0.0f, 1.0f);
  float currentMaxSteer = glm::mix(maxSteerLowSpeed, maxSteerHighSpeed, t);

  // Analog Steering Target
  // Left Stick < 0 -> steerValue < 0. We want Positive Angle (Left Turn).
  // So Target = -steerValue * currentMaxSteer
  float targetSteer = 0.0f;

  if (std::abs(input.steerValue) > 0.01f) {
    targetSteer = -input.steerValue * currentMaxSteer;
  } else {
    // Keyboard Fallback (Digital)
    if (input.left)
      targetSteer = currentMaxSteer;
    else if (input.right)
      targetSteer = -currentMaxSteer;
    else
      targetSteer = 0.0f;
  }

  // Smoothly interpolate steeringAngle towards targetSteer
  float steerDelta = (std::abs(targetSteer) > std::abs(steeringAngle)) ? steerSpeed : steerAutoCenter;
  if (steeringAngle < targetSteer) {
    steeringAngle += steerDelta * deltaTime;
    if (steeringAngle > targetSteer) steeringAngle = targetSteer;
  } else if (steeringAngle > targetSteer) {
    steeringAngle -= steerDelta * deltaTime;
    if (steeringAngle < targetSteer) steeringAngle = targetSteer;
  }

  // Clamp just in case
  steeringAngle = std::clamp(steeringAngle, -currentMaxSteer, currentMaxSteer);

  // 2. Update Speed (Magnitude) with Analog Throttle/Brake and Drag
  float effectiveThrottle = input.throttleValue;
  if (effectiveThrottle < 0.01f && input.up) effectiveThrottle = 1.0f;  // Keyboard Fallback

  float effectiveBrake = input.brakeValue;
  if (effectiveBrake < 0.01f && (input.down || input.brake)) effectiveBrake = 1.0f;  // Keyboard Fallback

  // Natural Forces (Drag + Rolling Resistance)
  float airDensity = 0.5f;                                  // Simplified
  float dragCoef = 0.001f;                                  // [Modified] Reduced from 0.05 to 0.001
  float dragForce = airDensity * dragCoef * speed * speed;  // Quadratic Drag

  // Use user-configurable coastingDeceleration instead of hardcoded rollingResistance
  float totalDecel = dragForce + coastingDeceleration;

  if (effectiveThrottle > 0 && effectiveThrottle > effectiveBrake) {
    // Accelerate
    speed += acceleration * effectiveThrottle * deltaTime;
  }

  if (effectiveBrake > 0) {
    if (speed > 1.0f) {
      // Braking
      speed -= brakingDeceleration * effectiveBrake * deltaTime;
    } else {
      // Reverse (only if nearly stopped)
      if (input.down) {  // Only reverse on button/keyboard 'down', simplified
        speed -= acceleration * effectiveBrake * deltaTime;
      }
    }
  }

  // Apply Drag (Always opposes motion)
  if (speed > 0) {
    speed -= totalDecel * deltaTime;
    if (speed < 0) speed = 0;  // Don't drag into reverse
  } else if (speed < 0) {
    speed += totalDecel * deltaTime;
    if (speed > 0) speed = 0;
  }

  speed = std::clamp(speed, -maxSpeed / 2.0f, maxSpeed);

  // 3. Update Heading (Bicycle Model Kinematics)

  if (std::abs(speed) > 0.001f) {
    float turnRate = (speed / wheelbase) * std::tan(steeringAngle);
    headingAngle += turnRate * deltaTime;
  }

  // Calculate direction vectors based on new Heading (XZ plane)
  glm::vec3 forwardDir(sin(headingAngle), 0.0f, cos(headingAngle));
  glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, glm::vec3(0.0f, 1.0f, 0.0f)));

  // Ackermann Steering Calculations (Internal)
  // ... (Omitted visual update, math only)

  // 4. Update Velocity (Drift Physics)
  // Target velocity aligns with the car's front
  glm::vec3 targetVelocity = forwardDir * speed;

  float currentTraction = gripTraction;
  // If Handbrake is held and moving fast enough -> Low Traction (Drift)
  if (input.brake && std::abs(speed) > 5.0f) {
    currentTraction = driftTraction;
  }

  // Interpolate current velocity towards target velocity
  // High traction = instant snap. Low traction = smooth slide.
  float lerpFactor = currentTraction * deltaTime;
  if (lerpFactor > 1.0f) lerpFactor = 1.0f;

  if (std::abs(speed) < 0.1f) {
    velocity = targetVelocity;  // Snap at low speeds to prevent floatiness
  } else {
    velocity = lerp(velocity, targetVelocity, lerpFactor);
  }

  // 5. Update Position using Velocity Vector
  // [NEW] Boundary Check / Off-Road Physics
  const float INVALID_HEIGHT = -5000.0f;
  glm::vec3 nextPos = position + velocity * deltaTime;

  std::pair<float, int> mapData = heightMap(nextPos.x, nextPos.z);
  float nextH = mapData.first;
  int surfaceType = mapData.second;  // 0=Road, 1=OffRoad

  bool isOffTrack = (nextH < INVALID_HEIGHT) || (surfaceType == 1);

  if (isOffTrack) {
    // Off-track Logic (Grass/Sand)

    float grassDrag = 0.3f; // 草地阻力很大
    this->speed -= this->speed * grassDrag * deltaTime; 

    // 1. Deceleration Penalty (-20.0f per second)
    // We apply this against the current speed direction
    if (speed > 0.0f) {
      speed -= 15.0f * deltaTime;
      if (speed < 0.0f) speed = 0.0f;
    } else if (speed < 0.0f) {
      speed += 15.0f * deltaTime;
      if (speed > 0.0f) speed = 0.0f;
    }

    // 2. Speed Cap (30.0f)
    float offRoadLimit = 15.0f;
    if (speed > offRoadLimit) {
      speed -= 15.0f * deltaTime;  // Strong braking to force it down
      if (speed < offRoadLimit) speed = offRoadLimit;
    } else if (speed < -offRoadLimit) {
      speed += 15.0f * deltaTime;
      if (speed > -offRoadLimit) speed = -offRoadLimit;
    }

    // Recalculate velocity with new speed
    // Maintain direction but reduce magnitude
    if (std::abs(speed) > 0.001f) {
      velocity = glm::normalize(velocity) * speed;
    } else {
      velocity = glm::vec3(0.0f);
    }

    // 3. Allow Movement (don't stop)
    // If height is invalid, keep previous Y (flat ground assumption outside track)
    // or use a default ground level if position.y is way off.
    // For now, nextH is invalid, so we don't update Y from map.
    // We just move X/Z.
    position.x += velocity.x * deltaTime;
    position.z += velocity.z * deltaTime;
    // position.y remains unchanged (flat driving off-track)

  } else {
    // On Track
    position += velocity * deltaTime;
    // Y will be updated by heightMap in Step 6 (Terrain Following)
  }

  // 6. Terrain Following (4-Wheel Alignment)
  float halfWheelbase = wheelbase / 2.0f;
  float halfTrack = trackWidth / 2.0f;

  // Calculate coordinates of 4 wheels on the ground plane (relative to car position)
  // FL, FR, BL, BR
  glm::vec3 wheelOffsets[4] = {
      forwardDir * halfWheelbase - rightDir * halfTrack,   // FL
      forwardDir * halfWheelbase + rightDir * halfTrack,   // FR
      -forwardDir * halfWheelbase - rightDir * halfTrack,  // RL
      -forwardDir * halfWheelbase + rightDir * halfTrack   // RR
  };

  wheels.resize(4);
  float totalY = 0.0f;

  for (int i = 0; i < 4; i++) {
    glm::vec3 wheelPos = position + wheelOffsets[i];
    // Sample height for this wheel
    std::pair<float, int> wData = heightMap(wheelPos.x, wheelPos.z);
    float h = wData.first;

    // [NEW] Clamp wheel height if off-track (to prevent visual artifact)
    if (h < INVALID_HEIGHT) {
      // If the car body is off-track, we kept its Y.
      // We should keep wheels relative to car body or at the same "fake floor".
      // Since we didn't update position.y in step 5 for off-track,
      // position.y is the last valid height.
      // Let's set wheel height relative to that.
      h = position.y - 0.35f;
    }

    wheelPos.y = h;
    wheels[i] = wheelPos;
    totalY += h;
  }

  // Set car height to average of wheels + offset
  position.y = (totalY / 4.0f) + 0.05f;  // Lowered suspension height

  // Calculate new basis vectors based on wheel heights
  // Front axle midpoint
  glm::vec3 frontAxle = (wheels[0] + wheels[1]) * 0.5f;
  // Rear axle midpoint
  glm::vec3 rearAxle = (wheels[2] + wheels[3]) * 0.5f;
  // Left side midpoint
  glm::vec3 leftSide = (wheels[0] + wheels[2]) * 0.5f;
  // Right side midpoint
  glm::vec3 rightSide = (wheels[1] + wheels[3]) * 0.5f;

  // Recalculate Front and constant Right
  this->front = glm::normalize(frontAxle - rearAxle);
  glm::vec3 sideVector = glm::normalize(rightSide - leftSide);
  this->up = glm::normalize(glm::cross(sideVector, this->front));
  this->right = glm::cross(this->front, this->up);  // Re-orthogonalize right

  // 7. Update Render Object Transform
  if (renderObject) {
    // Construct rotation matrix from basis vectors
    glm::mat4 rotation(1.0f);
    rotation[0] = glm::vec4(right, 0.0f);
    rotation[1] = glm::vec4(up, 0.0f);
    rotation[2] = glm::vec4(-front, 0.0f);  // OpenGL camera looks down -Z, but model might look +Z
    // Wait, typical identity is Right=(1,0,0), Up=(0,1,0), Forward=(0,0,1).
    // If our 'front' is the direction car is moving, e.g. (0,0,1).
    // The rotation matrix columns are X, Y, Z.
    // If car flows +Z, then Z axis is Front.
    // But usually +Z is backwards for standard OpenGL view.
    // Let's assume standard: X=Right, Y=Up, Z=Back.
    // So Z column should be -front.

    // However, our model might be oriented differently.
    // Original code: rotate 90.
    // Let's build a clean basis matrix.

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);

    // Apply orientation
    glm::mat4 orientation(1.0f);
    orientation[0] = glm::vec4(right, 0.0f);  // X axis
    orientation[1] = glm::vec4(up, 0.0f);     // Y axis
    orientation[2] = glm::vec4(front, 0.0f);  // Z axis (Forward)

    // Logic check: if front is (0,0,1), right is (-1,0,0) [cross(001, 010) = -100]
    // Wait, cross((0,0,1), (0,1,0)) = (-1, 0, 0)? No.
    // i j k
    // 0 0 1
    // 0 1 0
    // i(0-1) - j(0) + k(0) = (-1, 0, 0).
    // So right is -X. That's inverted X.
    // Usually right should be +X.
    // Let's assume Front is the car's forward direction.
    // If we want Z to be forward in the matrix (which is common for object space if model faces Z):
    // Then Col2 = Front.
    // And Col0 = Right.

    model = model * orientation;

    // Fix Model specific orientation (Rotate 90 if needed, or if model faces +X)
    // The original code had: rotate(heading), then rotate(90).
    // If the model faces +X originally?
    // Rotate 90 deg around Y makes +X become -Z (or +Z).
    // Let's stick to the previous hardcoded fix logic but adapt it to basis.
    // If model points towards +X, and we want it to point along 'front'.
    // Then we need to rotate the model so its +X aligns with our 'front' (Z in our generated matrix).
    // Actually, let's keep it simple:
    // The matrix 'orientation' transforms (0,0,1) to 'front'.
    // If the model natively faces +Z, we are good.
    // If the model natively faces -Z, we need 180.
    // If model faces +X (common in some tools), we need -90.

    // Original car model: "Formula 1 mesh.obj"
    // Usually these face Z or X.
    // Previous code: rotate(heading), rotate(90).
    // Heading 0 => front=(0,0,1).
    // If we rotate(0) then rotate(90), we get 90 deg rotation.
    // This implies the model needs 90 deg to align with Z.
    // So model likely faces X.

    // To replicate "Rotate 90":
    // We apply a pre-rotation to the model matrix.
    // [Updated] User requested flip (180 -> 0).
    model = glm::rotate(model, glm::radians(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Apply scale (Restored 0.01f for GLB as likely cm -> m)
    // [Updated] User requested "little bit bigger". 0.01 -> 0.015
    model = glm::scale(model, glm::vec3(0.02f));

    // [NEW] Engine Vibration
    float vibrationAmp = 0.0f;
    float absSpeed = std::abs(speed);

    // 0. Stationary -> No Vibration
    if (absSpeed < 0.1f) {
      vibrationAmp = 0.0f;
    }
    // 1. Starting/Idle Rumble (0.1 - 5 km/h) - Slight rumble
    else if (absSpeed < 5.0f) {
      // Peak at 0.1, fade to 0 at 5.0
      // Max Amplitude: 0.002f (2mm) was 0.035f
      vibrationAmp = 0.002f * (1.0f - (absSpeed / 5.0f));
    }
    // 2. High Speed Shake (> 60 km/h)
    else if (absSpeed > 60.0f) {
      // Starts at 0.001f, increases slightly
      vibrationAmp = 0.001f + (absSpeed - 60.0f) * 0.0001f;
    }
    // 3. Cruising - Very stable
    else {
      vibrationAmp = 0.0f;
    }

    // Apply jitter to translation
    float jx = ((float)rand() / RAND_MAX - 0.5f) * vibrationAmp;
    float jy = ((float)rand() / RAND_MAX - 0.5f) * vibrationAmp;
    float jz = ((float)rand() / RAND_MAX - 0.5f) * vibrationAmp;

    // Apply loosely to the model matrix (which is already translated/rotated)
    // NOTE: applying translate here adds it in Local or Global space depending on order.
    // model = Translate * Rotate * Scale
    // If we multiply on right: model * translate => Local vibration?
    // Scale is 0.02. If we translate by 0.001 (meters), in Local space (before scale), it is HUGE?
    // Wait, matrix multiplication order:
    // model = globalPos * orientation * preRot * scale
    // If we want world-space jitter (independent of car size/scale), we should apply it to 'position' OR post-multiply
    // (left side). But 'model' is already computed. If we do: model = translate(jitter) * model; -> World space jitter.
    // Let's do World Space Jitter.

    // Scale vibration amplitude to be reasonable in World Units (Meters).
    // 0.002 = 2mm. Visible.

    glm::mat4 jitter = glm::translate(glm::mat4(1.0f), glm::vec3(jx, jy, jz));
    renderObject->transformMatrix = jitter * model;

    // 8. Update Wheel Transforms
    if (wheelRenderObjects.size() == 4 && wheelModelOffsets.size() == 4) {
      // Update Spin Angle
      // v = r * omega => omega = v / r
      // Radius ~ 0.3m?
      float wheelRadius = 0.34f;  // F1 wheel radius approx
      wheelRotation += (speed / wheelRadius) * deltaTime;

      for (int i = 0; i < 4; ++i) {
        glm::mat4 anim = glm::mat4(1.0f);

        // Steering (Front wheels: 0, 1)
        if (i == 0 || i == 1) {
          // Steering is around Y axis
          // Check signs: Input Left -> steeringAngle > 0?
          // logic: targetSteer = -input.steerValue (Left is negative input, target positive).
          // So SteeringAngle > 0 means LEFT Turn.
          // Wheel should rotate LEFT (Positive Y).
          anim = glm::rotate(anim, -steeringAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        }

        // Spin (Around Z Axis - detected as correct axle for native orientation)
        anim = glm::rotate(anim, wheelRotation, glm::vec3(0.0f, 0.0f, 1.0f));

        // Combine: CarTransform * WheelOffset * Anim
        // Note: WheelOffset places the wheel relative to Car Center.
        // Anim rotates the wheel around its own center (because it's applied last, local to the mesh).
        // Requires mesh vertices to be centered at (0,0,0).
        wheelRenderObjects[i]->transformMatrix = renderObject->transformMatrix * wheelModelOffsets[i] * anim;
      }
    }

    // 9. Update Brake Light
    if (brakeLightObject && brakeLightModel) {
      // Position: Center Rear, slightly up.
      // Car local: Front is +Z (based on code context), but model rotated?
      // renderObject->transformMatrix is the Car's World Transform.
      // We want to attach relative to it.
      // Offset: (0, 0.4, -2.15) roughly for F1 rear.
      // Wait, car flows +Z or -Z?
      // In setWheels logic, +Z checks seemed forward.
      // If Front is +Z, Rear is -Z.
      // Try offset (0, 0.35, -2.3).
      // Scale: Small box.
      // BrakeLight Model is 1x1x1 Cube centered at 0.

      glm::mat4 lightTrans = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.35f, -2.25f));  // Local Offset
      lightTrans = glm::scale(lightTrans, glm::vec3(0.1f, 0.05f, 0.05f));                      // Flattened box

      brakeLightObject->transformMatrix = renderObject->transformMatrix * lightTrans;

      // Logic: If Braking, Emissive Red. Else, Off (or dim).
      // InputState in Update has 'brake' boolean and 'brakeValue'.
      bool isBraking = input.brake || input.brakeValue > 0.1f;

      if (!brakeLightModel->meshes.empty()) {
        if (isBraking) {
          // Flash? Or Solid? Start with Solid Bright Red.
          // 50.0 intensity for bloom effect
          brakeLightModel->meshes[0].emissiveFactor = glm::vec3(50.0f, 0.0f, 0.0f);
        } else {
          // Off or very dim
          brakeLightModel->meshes[0].emissiveFactor = glm::vec3(0.0f);
        }
      }
    }
  }
}
