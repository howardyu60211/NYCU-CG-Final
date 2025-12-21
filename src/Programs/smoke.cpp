#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>
#include <vector>
#include "car.h"
#include "context.h"
#include "gl_helper.h"  // createTexture
#include "program.h"

extern Car* car;

bool SmokeProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  // Load Texture using helper
  textureId = createTexture("assets/texture/smoke.jpg");
  if (textureId == 0) {
    std::cout << "Failed to load smoke texture" << std::endl;
  }

  // Set up standard particle system VAO
  VAO = new GLuint[1];
  glGenVertexArrays(1, VAO);

  return programId != 0;
}

void SmokeProgram::doMainLoop() {
  // 1. Logic Update
  if (car && glfwGetKey(ctx->window, GLFW_KEY_SPACE) == GLFW_PRESS) {
    if (car->wheels.size() >= 4) {
      static std::mt19937 rng(std::random_device{}());
      // [Update] Continuous emission (always true or very high chance)
      std::uniform_real_distribution<float> chance(0.0f, 1.0f);

      // Emit every frame for continuity
      if (true) {
        std::uniform_real_distribution<float> distOffset(-0.2f, 0.2f);
        std::uniform_real_distribution<float> distVel(-0.5f, 0.5f);
        std::uniform_real_distribution<float> distRot(0.0f, 6.28f);

        glm::vec3 carBackDir = -glm::normalize(car->getFront());

        // Emit fewer particles per frame but continuously
        for (int k = 0; k < 2; k++) {
          Particle p;
          int wheelIdx = (chance(rng) > 0.5f) ? 2 : 3;

          p.position = car->wheels[wheelIdx] + glm::vec3(distOffset(rng), 0.1f, distOffset(rng));
          // Slower initial velocity so it lingers
          p.velocity = carBackDir * 1.0f + glm::vec3(distVel(rng), 0.2f, distVel(rng));

          p.life = std::uniform_real_distribution<float>(1.0f, 2.0f)(rng);
          p.maxLife = p.life;

          p.size = 0.05f;  // Start very small
          p.rotation = distRot(rng);

          particles.push_back(p);
        }
      }
    }
  }

  // Update Particles
  float dt = 0.016f;
  for (auto it = particles.begin(); it != particles.end();) {
    it->life -= dt;
    if (it->life <= 0.0f) {
      it = particles.erase(it);
    } else {
      it->position += it->velocity * dt;

      it->velocity.x *= 0.95f;
      it->velocity.z *= 0.95f;
      it->velocity.y += 0.5f * dt;  // Rise slowly

      // Rapid expansion
      it->size += dt * 4.0f;  // Expand quickly

      ++it;
    }
  }

  // 2. Render
  if (particles.empty()) return;

  glUseProgram(programId);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glUniform1i(glGetUniformLocation(programId, "smokeTexture"), 0);

  const float* p = ctx->camera->getProjectionMatrix();
  glUniformMatrix4fv(glGetUniformLocation(programId, "Projection"), 1, GL_FALSE, p);

  const float* v = ctx->camera->getViewMatrix();
  glUniformMatrix4fv(glGetUniformLocation(programId, "ViewMatrix"), 1, GL_FALSE, v);

  glm::mat4 viewMat = glm::make_mat4(v);
  glm::vec3 CameraRight = glm::vec3(viewMat[0][0], viewMat[1][0], viewMat[2][0]);
  glm::vec3 CameraUp = glm::vec3(viewMat[0][1], viewMat[1][1], viewMat[2][1]);

  std::vector<float> vertices;
  vertices.reserve(particles.size() * 6 * 6);

  for (const auto& pt : particles) {
    glm::vec3 center = pt.position;
    float s = pt.size;

    // Billboarding & Rotation
    float cr = cos(pt.rotation);
    float sr = sin(pt.rotation);
    struct Corner {
      float x, y;
    };
    Corner corners[4] = {{-0.5f * s, -0.5f * s}, {0.5f * s, -0.5f * s}, {0.5f * s, 0.5f * s}, {-0.5f * s, 0.5f * s}};
    glm::vec3 finalPos[4];
    for (int i = 0; i < 4; i++) {
      float rotX = corners[i].x * cr - corners[i].y * sr;
      float rotY = corners[i].x * sr + corners[i].y * cr;
      finalPos[i] = center + CameraRight * rotX + CameraUp * rotY;
    }

    float lifeRatio = pt.life / pt.maxLife;
    // Start at 0.4 opacity and fade to 0
    float alpha = 0.4f * lifeRatio;

    vertices.insert(vertices.end(), {finalPos[0].x, finalPos[0].y, finalPos[0].z, 0.0f, 0.0f, alpha});
    vertices.insert(vertices.end(), {finalPos[1].x, finalPos[1].y, finalPos[1].z, 1.0f, 0.0f, alpha});
    vertices.insert(vertices.end(), {finalPos[2].x, finalPos[2].y, finalPos[2].z, 1.0f, 1.0f, alpha});
    vertices.insert(vertices.end(), {finalPos[0].x, finalPos[0].y, finalPos[0].z, 0.0f, 0.0f, alpha});
    vertices.insert(vertices.end(), {finalPos[2].x, finalPos[2].y, finalPos[2].z, 1.0f, 1.0f, alpha});
    vertices.insert(vertices.end(), {finalPos[3].x, finalPos[3].y, finalPos[3].z, 0.0f, 1.0f, alpha});
  }

  if (!vertices.empty()) {
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    // Use Alpha Blending (Transparency) instead of Additive
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint VBO;
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO[0]);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);

    // Attr 0: Pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    // Attr 1: Tex
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    // Attr 2: Alpha
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 6);

    glDeleteBuffers(1, &VBO);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  glUseProgram(0);
}